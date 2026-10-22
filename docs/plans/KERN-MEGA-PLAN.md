# `kern_mega` — a 286+ protected-mode kernel

**Status: M1 IS UNDER WAY, AND kern_mega BOOTS INTO 286 PROTECTED MODE ON QEMU** (section 10.1) - plus 2.4.1's CS-write ratchet (2026-10-17, branch `protected-mode`,
cut from `elendilon` at `c783a6a`). The owner asked what a 286+ protected-mode
build would take: one with room for the kernel to grow to 512KB, though
nothing requires it to. They asked three things:

* what has to be different to take advantage of protected mode;
* what MUST be different for it to run at all;
* how much of it can be shared with `kern_big`.

This file answers those from three surveys of this tree: segment arithmetic in
`kernel/`, BIOS/ROM dependence across `kernel/` and `drivers/`, and the package
ABI across `apps/` and `drivers/`.

**Every count below comes from regex heuristics over the source.** Read them
as the size of a population, not as an exact list. Every cycle figure for the
286 comes from Intel's 80286 datasheet and is **not measured here**. The one
field figure used, the cost of a reset round trip on the Victory 286, is
measured and its source is named.

---

## 0. The answer, in eight lines

1. **What protected mode buys is not room for the kernel. It is room for the
   kernel *outside the 640KB*.** A real-mode kernel can already be 512KB:
   `.cold` is a second segment today, and nothing stops a third. What it
   cannot do is run code above 1MB+64KB, so every byte it grows is a byte the
   packages lose. kern_big already spends the HMA on `.cold` (SPEC.md 2.10.7);
   that is the 64KB real mode can reach, and it is used. Past it, only
   protected mode lets kernel code live in extended memory. Neither LOADALL
   nor a loaded CS base survives an interrupt, so neither is an alternative
   (section 6).
2. **A 286 cannot leave protected mode except by resetting the CPU.** It also
   has no V86 mode. Every BIOS call is therefore a reset round trip, measured
   at **412 µs** on the Victory 286 (docs/plans/LOADALL-PLAN.md). Or it gets
   replaced by our own driver. Today's kernel makes 65 `int` calls across 8
   vectors and chains the ROM's `int 08h` and `int 09h` on every tick and
   every key. That ROM dependence is the largest MUST.
3. **AT-class hardware makes those drivers small.** Every 286 has the same
   765 FDC, 8237 DMA, 8042 keyboard controller, MC146818 CMOS and 8259 pair,
   and normally a WD1003/ATA task file at 1F0h. HDD.DRV's rung 1 already
   drives that task file directly (SPEC.md 52.1). On a 286 the "BIOS
   replacement" is four drivers plus mode-set register tables, not a zoo.
4. **Segment values become selectors, and one design keeps most of the tree
   unchanged.** Map the first megabyte *by identity* in the GDT at 128-byte
   granularity: selector `N` has base `N × 16` for every `N` that is a
   multiple of 8. Then every baked constant (`0x0060`, `0xA000`, `0xB800`,
   `0x0040`, LOW_SEG, every heap base) keeps working, and so does every step
   of `+0x1000`, `+0x800` or `+32`. Code segments and anything above 1MB go in
   the LDT, where selectors can never collide with the identity ones
   (section 3).
5. **Code is not writable in protected mode, and the tree writes through CS a
   lot.** The kernel has **468** writes through a `[cs:]` override and **95**
   `push cs / pop ds|es` pairs. The packages have **~1,064** such writes. The
   kernel's share is mechanical. Some of the packages' share is not: on a 286
   there is no FS or GS to free up, so hot loops that keep their state in CS
   while DS and ES point at a claim or at VRAM have to be restructured.
6. **Packages: ~24 of 58 look clean, ~5 need an SDK fix, ~29 need real
   work.** That is with ring 0 and the identity map. The DOS box cannot run
   *under* 286 protected mode. It can run *beside* it: drop to real mode for
   the bracket, run kern_dos from the HMA, and park the kernel in extended
   memory. That is feasible, ~630KB, and needs no hibernate (2.7).
7. **Overlap with kern_big is high by line count and low by risk.** About
   38% of the kernel's 147,067 lines sit in the 21 files that contain any
   mode-bound code, and roughly a third of those lines are actually bound.
   So something like **85-90% of the kernel builds unchanged** under a
   `KERN_MEGA` define, after a mechanical sweep. What does change is the
   kernel's foundations: the heap allocator, interrupts, the scheduler's
   stack switch, the disk transfer layer, boot, and the module and overlay
   machinery.
8. **The binding cost is not code, it is the test bench.** MartyPC is an
   8088 and runs nothing here. QEMU is a 386+, which can hide 286 mistakes.
   86Box has 286s but cannot be scripted. Nearly every emulator row in
   `tests/suite.py` is a MartyPC row. A kern_mega starts with almost no gate
   behind it (section 9).

---

## 1. What "286 protected mode" gives, and what it does not

| | real mode, today | 286 protected mode |
|---|---|---|
| physical address space | 1MB + the HMA's 64KB | **16MB** (24-bit descriptors) |
| a segment | 64KB, base = value × 16 | 64KB, base and limit from a descriptor |
| segments addressable | 65,536 overlapping windows | 8,192 GDT + 8,192 LDT descriptors |
| code above 1MB | only in the HMA | **anywhere** |
| protection | none | limit checks, read-only code, rings 0-3 |
| paging, V86, 32-bit registers, FS/GS | — | **none, all 386** |
| way back to real mode | — | **CPU reset only** (8042 pulse or triple fault, then the CMOS shutdown byte); `lmsw` cannot clear PE |

The last row decides most of what follows. The fourth row is what the request
is really about.

**How far can a kernel grow today?** The near model puts `.text` and `.bss` in
one 64KB segment (`KERN_CODE_MAX`, 49,047 used). `.cold` is a second segment
of 38,131 bytes. Modules, the boot overlay and the blob are further segments.
Real mode has no 64KB ceiling on the *kernel*. The ceiling is on the kernel
*and the packages together*: kern_big is 98,816 resident bytes that no
package can use. In protected mode the kernel can live entirely above 1MB,
and the 640KB is all heap. For a 512KB kernel that difference is the whole
machine.

**Protected mode still has 64KB segments.** A 512KB kernel is at least eight
code and data segments with far calls between them, which is the `.cold`
pattern repeated. Protected mode gives no flat address space; only a 386
gives that, and only in 32-bit code. So the kernel's *structure* does not get
simpler. Its *placement* is what changes.

---

## 2. What MUST be different for it to run at all

### 2.1 Entering protected mode, and staying in

Stage 2 loads the kernel in real mode as it does now; the loader stays
untouched (the boot code's ~25 `int` sites are real mode by nature). Then,
before `kmain`:

* **A20 open for the life of the session.** XMEM.DRV already proves the gate
  and keeps it open (SPEC.md 41.2). The known hazard, `int 15h AH=87h`
  closing it, goes away with the BIOS.
* **The PIC moves.** IRQ0-7 arrive on vectors 08h-0Fh, which in protected
  mode are CPU exceptions: double fault, #GP, #NP and so on. The master 8259
  must be reprogrammed to 20h or above before `sti`. Real mode never has to
  care about this. Nothing today does it, and reverting it is part of every
  route back to real mode (2.6).
* **A GDT, an IDT and exception handlers.** The handlers must name the fault
  on the glass (CS:IP, the error code, the selector). In real mode, a wild
  pointer corrupts something. In protected mode it is a #GP, and that is
  only an improvement if somebody can read the report (section 5).
* **NMI masked across the switch**, as `xm_arm` and `ub_set` already do
  (SPEC.md 41.4, `apps/os88ub.inc`).
* **No 386 instruction anywhere.** NASM's `cpu 286` is the
  `cpu 8086 -w+error` of this build. The reserved high word of every
  descriptor must be zero: a 386 reads it as base bits 31-24, and a 286
  ignores it, so a careless descriptor works on QEMU and nowhere else.

### 2.2 The BIOS: replace or reflect

Survey result: **65 `int` sites in `kernel/`** —

* `13h` × 19
* `16h` × 17
* `10h` × 16
* `19h` × 5
* `1Ah` × 4
* `11h` × 2
* `12h` × 1
* `15h` × 1

— plus two chains to the ROM:

* `sch_isr`'s `call far [sch_old08]` on every tick. The ROM increments
  40:6Ch, runs the floppy motor-off countdown, and **sends the EOI**.
* `kbm_isr`'s `jmp far [cs:kbm_old9]` on every key. The ROM acknowledges the
  8042, translates scancodes with the shift state at 40:17h, and fills the
  buffer at 40:1Eh, which `ui_task` then polls through `int 16h` on every pass.

On a 286 there are two ways to keep a BIOS service:

* **Reflect it.** Reset to real mode, make the call, re-enter protected
  mode. Measured at ~412 µs fixed on the Victory 286 (the AH=87h figure in
  docs/plans/LOADALL-PLAN.md, attributed there to the reset). For the length
  of the call the machine is in real mode. Any IRQ that arrives needs a
  *real-mode* handler, and kernel data above 1MB is out of its reach.
* **Replace it** with our own driver.

What follows for each service:

| service | today | kern_mega | why |
|---|---|---|---|
| tick, EOI, motor-off | ROM `int 08h` chain | **native** | every 55 ms; reflecting it is 0.75% of the CPU for nothing |
| keyboard | ROM `int 09h` + `int 16h` poll | **native**: an 8042 ISR, a scancode-set-1 table, and our own shift state and buffer | `int 16h` is polled on every `ui_task` pass, so a reflected poll would put a reset in the idle loop. `kbm_isr` already reads port 60h; what is missing is the ROM's translation |
| floppy | `int 13h`, ~400 ms a call with IRQs live (SPEC.md 15.3.8 counted 21 ticks inside one call) | **native**: 765 + 8237 | a reflected `int 13h` is 400 ms in real mode. Pointer, tick and sound would all need real-mode twins, or would stop. The native driver is the cheaper of those two |
| hard disk | HDD.DRV rung 0 (`int 13h`) / rung 1 (ATA PIO, 286-gated) | **rung 1 native, rung 0 reflected with IRQs masked** | rung 1 already takes every BIOS-known drive on a 286 (SPEC.md 52.1.1). Only ROM-only controllers (XT-IDE, SCSI, PicoMEM) need rung 0 |
| video mode sets | `int 10h` AX=0012h / mode 6 / mode 3 / fsx modes | **reflect, IRQs masked**, then replace with register tables | rare and short. CGA and Hercules mode tables are a few dozen `out`s; VGA 12h is ~60 registers |
| font pointer | `int 10h` AX=1130h | read once **before** entering protected mode | it is a boot-time question |
| clock | CMOS rung 1 directly, `int 1Ah` as rung 4 | **CMOS only** | every 286 has the MC146818; rungs 2-4 are XT cards |
| reboot | `int 19h` | **8042 pulse** (reset line), which a 286 needs anyway | there is no warm boot from protected mode |
| equipment, memory size | `int 11h`, `int 12h`, `int 15h` AH=88h | read before entering protected mode | boot-time questions |

**The trap in "reflect everything first, replace later":** a reflected call
runs real-mode code while IRQs arrive. Today's ISRs are real-mode code at
`KERNEL_SEG`, so in principle they could run there. But under kern_mega they
would only work if they were *bimodal*: touching only identity selectors,
never a data alias, and never data above 1MB. OS/2 1.x drivers lived that way,
and it is a lifetime discipline. Making the floppy native removes the one long
reflected call and, with it, every reason for a bimodal ISR. That is why the
floppy driver is a MUST in the first wave and not an optimisation.

### 2.3 Segments are selectors

Kernel survey: **230** loads of DS, ES or SS from a general register.

| source | sites |
|---|---|
| a symbolic constant | 61 |
| zero (the IVT or the BDA through ES=0) | 37 |
| a stored variable | 33 |
| a copy of another segment register | 23 |
| arithmetic immediately before | ~21 |
| a parameter, popped, unknown | ~55 |

Beyond those, `memory.inc` alone has 44 `add`/`sub`/`cmp` lines on
`MC_SEG`/`MC_PARA`/`mem_base`/`mem_top`. There are also linear-address
computations for the DMA 64KB check (`dsk_runcap`, `disk.inc:2157`). Segment
values are also used as owner IDs: `MC_OWN` relies on a segment never
reaching 0xFBxx (`memory.inc:2964`). The identity map in section 3 is what
lets most of this stand. The rest:

* **ES = 0.** The null selector loads but faults on any access. Those 37
  sites are mostly IVT pokes, which become IDT writes, and BDA reads, which
  use selector 0x40.
* **Off-grid arithmetic**: `dec es` in `compress.inc` (9 sites), `loader.inc:1011`'s
  prefix >> 4, `KDS_CDELTA` in `hiber.inc`, and `HMA_SEG = 0xFFFF`. These need
  rewriting. The HMA is dropped outright (section 4).
* **The heap.** A claim returns a segment that *is* its address. Under the
  identity map that stays true for conventional memory. Claims above 1MB get
  an LDT selector (section 3.2).

### 2.4 Code is read-only, and CS is not DS

The near model's founding assumption is CS = DS = `KERNEL_SEG`, for the
kernel and for every package. In protected mode a selector names *one*
descriptor, either code or data, never both. A readable code segment can be
loaded into DS, but only for reading.

* **The kernel**: **473** writes through CS and **105** `push cs / pop ds|es`
  pairs, in 26 files under kernel/ and 5 under drivers/. The original
  survey's 468 and 95 were a coarser grep. The counts are exact now, because
  `tests/unit/t_cswrite.py` counts them and holds them to
  `tests/cswrites.txt` (2.4.1). **The SECTION of a site decides how it
  converts, and the split is lopsided**:

  | section | writes | what it is | kern_mega's conversion |
  |---|---|---|---|
  | `.mod*` | ~390 | on-demand MODULES writing their own image, which SPEC.md 2.8 allows (compress 124, stikmenu 105, ascw 49, hiber 45, shutdown 16, winmod 10, dockmod 9, diskops 6) | each module gets a data alias for its image. kern_mega keeps modules resident (section 4), so this is per-module restructuring and not a rename |
  | `.boot2` | ~41 | stage 2 of the boot, in real mode (splash 37, vidsel 3, desksc 1) | **none**, if the switch to protected mode comes after stage 2, which 2.1 already places it |
  | `.text`, `.cold`, `%macro` | ~42 | ISRs saving SP or state before DS is the kernel's (sched 15, mouse 3), renderer state kept through CS while DS is the caller's (font, vga12), the API's inline cells, the boot spinner, a knob's trace | per site: `push ds / mov ds, [cs:k_dsel]` or a register freed |

  So **the CS-write problem is a module problem, not a kernel-wide one**. The
  resident share is about forty sites. The pairs have three distinct
  meanings, which is why they are not one macro:
  * "DS = the kernel", in an ISR or an API stub;
  * "ES = the kernel", for a routine that takes ES:DI;
  * "DS = my own image", in a driver.

  **The API cells cannot simply grow.** `OSAPI_SLOT` is 8 bytes exactly, and
  the table's offsets ARE the ABI (SPEC.md 20.3). So kern_mega's hot cell
  becomes `OSAPI_XCELL`'s shape (`push bp / mov bp, target / jmp api_pm`,
  7 bytes plus one of padding), with the segment load done once in the
  shared stub. The rare 6-byte cells already go through shared `api_r*`
  routines, so only those change.

  The ISRs that write through CS do so because DS is unknown on entry. They
  become `push ds / mov ds, [cs:k_dsel]`: 17 clocks for the segment load in
  protected mode against 2 in real mode.

#### 2.4.1 The ratchet — BUILT (2026-10-17)

`tests/unit/t_cswrite.py` is in the fast tier, so every `make` runs it.
* A file under kernel/ or drivers/ that is not in `tests/cswrites.txt` may
  use neither idiom.
* A registered file may not exceed its counts, or fall below them: a
  conversion lowers the line.
* `--list` prints every site with its section, which is M1's worklist.

It is a ratchet and not a clean gate for `t_textrules.py`'s reason. kern_big
is correct as it stands, so the only failure worth preventing today is a NEW
site in a file kern_mega will share.

**The macro sweep is NOT done ahead of M1, on purpose.** A macro that
expanded to today's bytes on kern_big would be easy to prove harmless with
`cmp`. But one name cannot carry the three meanings above, the module half is
restructuring rather than renaming, and which sites run before the switch is
decided by where M1 puts the switch. A rename now would write those guesses
into 31 files. The ratchet holds the line instead, and the conversion
happens site by site, once, in M1.
* **The packages**: ~1,064 `[cs:]` writes in 16 packages, plus the shared
  `apps/os88spk.inc` (59), `os88spkfx.inc` (29) and `os88pcm.inc` (4).
  * The worst are pixel (593), weave (153), excitebike (104) and video (84).
  * The emulator cores (c64, apple2, runcpm) keep CPU state in CS because DS
    is the guest's memory.
  * A 286 has no spare segment register to replace CS with, so those loops
    need restructuring, not substitution.
* **Code in a claim.** Overlays and parts are far-called through the
  claim's *data* segment: `cc_ovbind` stamps `[cc_ovseg]` and `cc_ovthunk`
  `retf`s to it (`apps/cc/crt0.asm:1270,1325`). Under kern_mega a claim that
  holds code has to be handed a *code* alias. That is one SDK change and a
  rebuild of six C packages (cword, loom, weave, runcpm, apple2, c64), plus
  `apps/os88partsbody.inc` for the twelve that embed parts.

### 2.5 The package ABI survives, with one exception and one ring

`KERNEL_SEG equ 0x0060` is assembled into every package. All 186
`call KERNEL_SEG:0xNNNN` cells and the 3-byte dispatcher at `seg:000C`
(SPEC.md 20.2) are far calls through constants. **0x0060 is a well-formed
selector**: GDT index 12, TI = 0, RPL = 0.

* Make GDT[12] a *readable code* descriptor whose base is wherever the
  kernel is (base 0x600 for M1, above 1MB from M3), and every
  existing package's call into the kernel works without reassembly.
* Package reads through the callback ES (`[es:bx+W_W]`, 78 sites) work too,
  because readable code can be read.
* What breaks is *writes* through ES = 0x60: 4 in paint and 2 in
  `apps/os88stik.inc` (729, 1262). They are small fixes.

**This is the one exception to the identity map**: 0x60 is code, not data.

**It also fixes the ring at 0.** A call gate ignores the offset of a far call,
so 186 cells behind one selector cannot be gates, and a ring-3 package could
not call `0x60:NNNN` at all. So does port I/O: 22 packages program the video
card directly inside fsx brackets (SPEC.md 53.7). Ring 3 is possible, but
only as a flag day: every cell becomes its own gate selector, every package
is reassembled, and IOPL or an I/O permission policy is decided. Section 5
treats it as a later wave.

Under kern_mega the loader gives each instance **two** selectors: a code
selector for CS and for `W_SEG`'s far calls, and the identity data selector
for DS. A package that compares CS with DS, or registers CS as its data
(`os88stik.inc:629`, `crt0.asm:432`), needs to learn the difference.

### 2.6 What is real-mode by design

* **The DOS box** (SPEC.md 96) runs native real-mode code. It banks and
  rewrites the whole IVT, and its programs call the ROM, program the PIT and
  PICs, and may close A20. It cannot run *under* protected mode on a 286.
  It can run *beside* it, for the length of an fsx bracket. Section 2.7 is
  the feasibility check. The DOS box is one program and not part of this
  design, so 2.7 is a verdict, not a plan.
* **Hibernate** (`hiber.inc`, `hbstub.inc`). The resume stub lives in the
  text framebuffer and reads the image back with `int 13h`. A protected-mode
  image has state above 1MB, so the stub must be able to land it there. Use
  the reset trick: read into conventional memory, then copy up the way
  XMEM.DRV does. This is the last wave, not the first.
* **XMEM.DRV, LOADALL and `ub_set`** (SPEC.md 41.13, 41.15) become
  pointless. Extended memory is just a descriptor away. The video player's
  decode-in-place path is what it is today on a 386, but done properly.

### 2.7 The DOS box: drop to real mode for the bracket — FEASIBLE

The owner's proposal:

* Leave protected mode for the length of the fsx bracket.
* Carry the "real-mode kernel bits" as a `DOS.O88` part that ships only on
  kern_mega.
* Load that part into the HMA.
* Leave the protected-mode kernel parked, untouched, in extended memory.

The program then gets 600+KB without a hibernate. **This holds up, and most
of it is already built.**

* **The real-mode kernel bits exist.** kern_dos *is* that part. It is the
  kernel's own `disk.inc`/`diskw.inc` plus the DOS core, shipped inside
  `DOS.O88` (SPEC.md 96.49, `kerndos/`). Its image rung is
  **33KB** (`KD_IMG_KB`, `kerndos/kdlayout.inc`), so it fits the HMA's
  65,520 bytes with half to spare. `.cold` already runs from the HMA at
  `vstart=0x10` (SPEC.md 2.10.7), which is the precedent for assembling
  kern_dos the same way.
* **What stays in conventional memory**:
  * the IVT and BDA (1.25KB);
  * the mount buffers and stack (`KD_LOW_KB` = 3);
  * the FAT snapshot;
  * a low entry stub.

  The buffers must stay low: `dsk_runcap` already keeps HMA buffers out of
  `int 13h`, because a BIOS computes a 20-bit DMA address. The stub's whole
  job is to re-open A20 before jumping to `FFFF:`, which is how DOS=HIGH
  survives programs that close the gate. **Estimate: ~5-8KB low, so ~630KB
  for the program**, against 584KB through today's hibernate path.
* **The package heap has to go somewhere.** In kern_mega the conventional
  heap is full of packages. Before the drop, the kernel copies the heap up
  into extended memory, and on return copies it back *to the same
  addresses*. Identity selectors mean nothing needs relocating. At the
  Victory's ~0.3 ms/KB that is ~0.2 s each way.
* **The transitions:**
  * **In** is one reset: PIC back to 08h, the BIOS's IVT and BDA banked at
    boot and restored, shutdown code 0Ah, then 40:67 to the stub. ~412 µs,
    once.
  * **Out** is cheap: real mode to protected mode needs no reset, just
    `lmsw` and a far jump.
* **The machine has to be big enough.** It needs extended memory for the
  kernel, the parked heap (up to ~600KB) and the HMA. That is a ~1.6MB
  machine or more. Below that, the hibernate route kern_dos already uses
  stays as the fallback.
* **The risk is not real mode. It is DOS programs that use extended memory
  themselves**: `int 15h` AH=87h/88h users, VDISK-style RAM disks, and
  286 DOS extenders (DOS/16M, Phar Lap 286, Lotus 1-2-3 r3). Today none of
  them can hurt an os8088 that lives in conventional memory. Here they can
  overwrite the parked kernel. Three defences:
  1. Report AH=88h as 0.
  2. Hand out extended memory only through the box's XMS door (SPEC.md
     96.15), from what lies above the kernel.
  3. **Checksum the kernel's extended memory on the way out and on the way
     back in.** On a mismatch, refuse to resume and cold-boot with a message
     that names the program, instead of running a corrupted kernel.
  The checksum costs about 0.3 s per 512KB on a 16 MHz 286, which is an
  estimate. Programs that respect XMS or AH=88h (the ones that worked under
  HIMEM) are safe. A program that ignores both is caught by the checksum
  rather than trusted.
* **Not "every DOS program".** The box's own limits still stand (SPEC.md 96,
  and DOS-EXEC-PLAN's section 10). What changes is the memory and the speed
  of getting in and out: no hibernate write, no kernel reload.

---

## 3. The design that keeps the tree: an identity-mapped first megabyte

### 3.1 The rule

**GDT entry `i` describes base `i × 128` (that is, segment `8i`), limit
64KB, writable data, DPL 0**, for every `i` from 1 to 8191. That covers the
whole first megabyte, so selector `N` means exactly what segment `N` means
today whenever `N` is a multiple of 8.

* The GDT is the CPU's maximum, 8,192 entries, 64KB. It lives in extended
  memory and is built once.
* **The exceptions are the kernel's own named segments**: 0x0060 (the kernel
  and the API table), `COLD_SEG` and `LOW_SEG`. Their entries describe
  wherever the kernel actually is, which is above 1MB (section 4), not
  `N × 16`. 0x0060 and `COLD_SEG` are readable code, and the kernel's writes
  go through data aliases in the LDT. Nothing outside the kernel does
  arithmetic on these three, so leaving the grid costs nothing.
* Entries 1-7 (selectors 0x08-0x38, bases in the IVT) are never loaded as
  data. That makes them free for the TSS, the LDT descriptor and the IDT's
  own alias.

**Which arithmetic this keeps working:** `add seg, 0x1000` (64KB steps),
`add seg, 0x800`, `add seg, 32` (512 bytes), every whole-KB heap base,
`HEAP_SEG` (512-aligned under guard 6), LOW_SEG and FAT_SEG (512-rounded
rungs), `0xA000`, `0xB000`, `0xB800`, `0xB000:8000` aliasing `0xB800`
(`vid_cga_alias`), `0x0040` and `0xF000`. The survey found every heap base
to be a multiple of 32 paragraphs today, so the whole allocator's output is
already on the grid.

**Which arithmetic it does not keep:** anything off the 8-paragraph grid.
That means `base + (addr >> 4)` (`apps/frotz/zmem.inc:137`,
`apps/tracker/trkplay.inc:626`, `apps/runcpm/runcpm.c:734`), `dec es`, and
a PSP at segment + 16 when the segment is not on the grid. These are real
rework, one site at a time.

**What it gives up:** isolation below 1MB. Any code at ring 0 can still load
any identity selector and write anywhere in the first megabyte, *exactly as
today*. What it gains even so:

* **Limits.** Every segment stops at 64KB, so the 16-bit offset wrap that
  real mode allows is a fault.
* **Read-only code.** A wild write into code faults instead of corrupting it.
* **A hard wall around everything above 1MB.**

The kernel's growth lives up there behind LDT selectors, and nothing below
can reach it by arithmetic.

### 3.2 Above 1MB: the LDT

All selectors with TI = 1 are LDT selectors, values ≡ 4 (mod 8). **They can
never equal an identity selector**, so no arithmetic on a conventional
segment can land on one by accident.

* Kernel segments above 1MB, extended-memory claims and code aliases come
  from the LDT.
* Allocation stays below 0xFB00 so `MC_OWN`'s tag range keeps its meaning
  (2.3).
* An LDT selector is **stable across a move**. Compaction copies the bytes
  and rewrites the descriptor base, and nobody holding the selector needs to
  know. That deletes, *for LDT claims*, the whole problem
  docs/plans/HEAP-UNPIN-PLAN.md is about: `MC_RLOC`, relocation procs,
  `mem_rr_tab`, the worker frame that pins a region (SPEC.md 66.6.2), the
  66 kernel words naming a driver image.
* Identity claims keep today's rules, because their selector *is* their
  address.

**A claim over 64KB** (Sheet takes ~99KB) gets consecutive LDT selectors,
each one 8 apart, which is Windows' `__AHINCR` shape. So kern_mega needs one
new SDK constant, `OS88_HUGE_INCR`. In conventional memory the identity map
gives `0x1000` for nothing, which is why the first-wave packages need nothing
new.

---

## 4. What SHOULD differ, to take advantage of it

In rough order of what each one buys:

1. **The kernel moves above 1MB, all of it.**
   * 0x0060 is an *exception* to the identity map (3.1), so its descriptor's
     base can be anywhere. Point it above 1MB and every package's
     `call 0x60:NNNN`, and every callback read through `[es:bx+W_W]`, lands
     in a kernel that has no byte below 640KB.
   * `COLD_SEG` and `LOW_SEG` (the task stacks and disk buffers) work the
     same way. Every 286 DMA address is below 16MB, so the 8237 can still
     reach the buffers.
   * `.cold`, every module and the boot overlay's survivors move above 1MB
     as well.
   * Conventional memory becomes package heap: the ~523KB a 640KB machine
     offers a package today becomes ~600KB.
   * **The modules stay resident.** `mod_need` exists to page `CTRL.DRV` and
     its eight siblings in and out of conventional memory (SPEC.md 2.8). With
     the kernel in extended memory that saving is worth nothing, while the
     disk read and the "system disk required" failure still cost something.
2. **Most of the size machinery is not wanted, and kern_mega should NOT
   carry it.** These all exist because conventional memory is scarce and
   `KERN_CODE_MAX` binds, and in kern_mega neither holds:
   * the HMA `.cold` and its driver pool (SPEC.md 2.10.7, `COLDHMA.SYS`);
   * the ROM `.cold`;
   * the `rp_walk` fixups (257 on kern_big);
   * `mem_unblob`;
   * the `.ovl`/`.ovlw` FAT-window loading.

   Each is a `%ifndef KERN_MEGA` rather than a rewrite. The rung banner
   still applies to the *conventional* footprint: one rule, tested by booting.
3. **A heap that spans the machine.** One allocator over identity
   conventional memory and LDT extended memory. It hands out stable
   selectors, so it can compact without relocation procs. A package that
   asks for more than 64KB, or for "whatever is going"
   (docs/plans/REGION-SELF-COMPACT-PLAN.md section 8), gets megabytes.
   `OSAPI_MEM_AVAIL` answers in KB already, and its 16-bit answer reaches
   64MB, which is enough.
4. **Caches sized to the machine.** The `dirw` directory cache, the FAT
   snapshot, the read-ahead and a real sector cache stop being purgeable
   claims fighting packages for 640KB. docs/plans/DISK-CPU-PLAN.md found that
   81% of the kernel's time in a file-heavy load was the stateless-by-name
   API re-walking directories and chains. Memory to keep a whole volume's
   directory tree and FAT is part of that answer.
5. **The video player's XMS bank without a copy.** The bank is addressed in
   place through a selector. LOADALL (SPEC.md 41.13) and `ub_set` (SPEC.md
   41.15) were both attempts at exactly this, under real mode.
6. **Faults as diagnosis.** In real mode a wild write corrupts the heap and
   shows up ten minutes later. In protected mode, a write past a claim's
   limit or into code is a #GP at the instruction that did it. The kernel can
   kill that *instance* (`inst_*` already tears an instance down) and name
   the CS:IP in a toast. That is SPEC.md's "every byte read off a disk is
   hostile" given teeth.

What it should **not** do in the first build: ring 3, task gates or a TSS per
task (the scheduler's stack switch is cheaper than a 286 hardware task
switch), or any 32-bit code.

---

## 5. What it costs per call, from the datasheet

80286 clocks, real mode / protected mode at the same privilege level
(Intel's figures, not measured here):

| | real | PM |
|---|---|---|
| `mov sreg, r16` | 2 | 17 |
| `pop sreg` | 5 | 20 |
| `mov sreg, m16` | 5 | 19 |
| `call far` direct | 13 | 26 |
| `retf` | 15 | 25 |
| `int n` | 23 | 40 |
| `iret` | 17 | 31 |

An `OSAPI_SLOT` round trip (the caller's `call far`, two segment loads and
the `retf`) is about **+53 clocks** under protected mode. That is ~6.6 µs at
8 MHz and ~3.3 µs on the Victory's 16 MHz. A drawing call on a 286 costs
hundreds of µs, so this is single-digit percent per primitive. An interrupt
is about +31 clocks before its own segment loads. The tick runs at 18.2 Hz
and the mouse ISR at packet rate, so this is noise.

**Where it does bite**: inner loops that reload a segment per element. The
paragraph-stepping copies (`mem_bcopy` steps 64 paragraphs a chunk, the LZ
window slides) pay 17 clocks for every `mov es, ax` that used to cost 2.
None of those loops is per-pixel.

The figure that matters on the owner's machine is the **reflection: ~412 µs
a round trip** on the Victory 286. That is why nothing on a per-event path
may be reflected (2.2).

---

## 6. Alternatives considered and refused

* **Real mode with more of the kernel in the HMA.** It is already done
  (SPEC.md 2.10.7), and 64KB is all the HMA holds.
* **Executing above 1MB by LOADALL (286) or a loaded CS base (386, the
  `ub_set` trick).** Both leave CS's hidden base above 1MB, and the first
  interrupt reloads CS from the IVT. IRET then reloads it as `selector × 16`.
  Code can be *reached* that way but cannot keep running.
* **Identity-mapping at 1-paragraph granularity.** The GDT and LDT together
  hold 16,384 descriptors, and selector values that differ only in the RPL
  bits share one descriptor. The finest grid is 4 paragraphs using the LDT
  as well, and that leaves no room for anything above 1MB. Eight paragraphs
  in the GDT alone is the grid that keeps the LDT free.
* **A 386 floor with V86.** This is the real alternative and the owner should
  weigh it (section 8).

---

## 7. How much can be shared with kern_big

`KERN_EMU` is the precedent (SPEC.md 9.11.7): an *additive* define that
implies `KERN_BIG`, so every `%ifdef KERN_BIG` keeps the big feature set.
`KERN_MEGA` would be the same. It is *subtractive* for the size machinery of
section 4 item 2 and adds a protected-mode layer.

**By file**, of the kernel's 147,067 lines:

| class | files | lines | what happens |
|---|---|---|---|
| contains mode-bound code | memory, sched, mouse (the keyboard half), disk (the transfer layer), viddet, vidsel, loader, driver, mod, xmem, compress, lz, splash, cpudet, shutdown, fsx, hiber + hbstub/hbstage/hbmark, kernel.asm | **56,664 (38%)** | roughly a third of these lines are actually bound. That is the allocator core, ISRs, transfer sites, mode sets, the module and HMA loaders, and the boot ladder. The rest (FAT logic, the caches, `mem_avail`'s planning, the video tables) is shared |
| mode-neutral | wm, menu, ctrl, files, diskw, font, vga12, ui, events, icons, dock, desk, toast, fdlg, filecp, assoc, clock, ascw, stikmenu, … | **~90,400 (62%)** | builds unchanged after the mechanical sweep: the 468 `[cs:]` writes, 95 `push cs` pairs, `OSAPI_SLOT`'s DS load. Drawing code reaches VRAM through `[vid_seg]`, and those values are identity selectors |

So ~85-90% of lines are shared and ~10-15% are `%ifdef KERN_MEGA` or new.

The new code:

* the protected-mode entry and exit;
* GDT, LDT and IDT management;
* exception reporting;
* FDC and DMA;
* keyboard translation;
* mode-set tables;
* the 8042 reset.

At this tree's density that is plausibly **6-12KB of new code**. This is an
estimate, not a measurement.

**The real cost of the overlap is permanent.** Every change to a shared file
from then on has to stay correct under both models. For example, a new
`[cs:]` write in `wm.inc` is fine on kern_big and a #GP on kern_mega. That
needs a gate in the fast tier, and that gate is **built**: 2.4.1's
`t_cswrite.py` holds both idioms to a per-file registry. Without it the
shared 85% rots in one direction only.

**Packages** (58 live, 51 assembly and 7 C):

* ~24 run unmodified: arkanoid, bounce, browser, calc, chart, deskcal,
  fontview, fptest, fractal, ftpd, hello, imgtest, mines, pacman, paccman,
  piano, rlapply, sheet, solitaire, tamegram, texpad, thewire, timer and
  wire.
* ~5 need an SDK fix or a one-line one: notepad, taskmgr, artful, recorder
  and cword.
* ~29 need real rework.
* The DOS box goes through kern_dos.

**Drivers**: 6 of 13 are clean at ring 0. 7 need work: sound's DMA physical
address and IVT hook, hdd's `int 13h` and stepping, xmem (obsolete), usbmouse,
ems, ramdisk and hddtool.

---

## 8. The floor: 286 — DECIDED (2026-10-17)

**The owner has decided: the 286 is the target.** kern_mega is still a
build for slow, old machines, not for Pentiums, and the 286 is the oddball
any such build has to meet. The table stays as the record of what that choice
costs against a 386 floor. The one thing worth keeping from the 386 column is
a later *tier inside* kern_mega: a 386 could use V86 for unmodified packages
without changing the 286 build.

| | 286 floor (this file) | 386 floor |
|---|---|---|
| unmodified packages | ~24 of 58 | **nearly all**, each in a V86 task; a `call 0x60:NNNN` traps to the monitor (~200 clocks a call on a 386) |
| the DOS box | kern_dos handoff only | **in a V86 task beside the desktop** |
| BIOS | four native drivers + reflection by reset | **reflected through V86**, cheaply; native drivers become optional |
| leaving protected mode | reset | `mov cr0` |
| FS/GS for the `[cs:]` loops | none | yes |
| paging | none | yes: 4KB pages, and compaction is a page-table edit |
| machines it runs on | the owner's Victory 286, mr286, every AT | 386SX and up; **not** the owner's 286s |

A 386 floor is architecturally the *easier* product: it is Windows/386's
shape, and most of section 2 disappears into the V86 monitor. A 286 floor is
the one that runs on the owner's own 286 machines, and it costs roughly
half the package catalogue in rework. Whichever is chosen, the identity map
of section 3 is the right map. On a 386 it becomes page tables, but the
grid argument does not change.

---

## 9. How it would be tested — the real cost

* **MartyPC is an 8088.** None of its ~hundreds of rows can boot kern_mega.
* **QEMU** is on the fallback list for exactly this ("286 and 386",
  docs/TESTING.md). Its CPU is a 386 or better, so it executes 286 protected
  mode correctly. It does *not* catch:
  * a 386 instruction (NASM's `cpu 286` does);
  * a non-zero descriptor high word, which needs an assertion in the GDT
    builder;
  * **a store through a code segment, or past a segment's limit** -
    MEASURED in M1 (10.1, item 6): the mouse ISR wrote through CS on every
    packet and nothing faulted. 2.4.1's ratchet is the only instrument for
    that rule until a 286 runs it;
  * timing.
* **86Box** has the `286`, `286-525`, `286-sound` and `286-video` profiles
  for looking, but it cannot be scripted.
* **An 86Box harness is the owner's preferred route.** That means adding to
  86Box the socket MartyPC's debug server gives this tree: screenshot, key
  and mouse injection, and guest-memory reads, so that `os88ui` can drive
  it. It is a big, standalone project and not part of this branch. Until it
  exists, QEMU carries protected-mode semantics, and 86Box is for looking
  only.
* **MAME's `ibm5170`** is a 286 with a debugger and Lua scripting. It is
  **not evaluated here**. It is the fallback candidate if the 86Box harness
  stalls.
* **The owner's Victory 286** is the field reference, and the only place the
  reflection cost has been measured.

The suite's `os88ui` vocabulary resolves positions from guest tables through
`os88sym`. It would need a QEMU-backed transport and a protected-mode-aware
symbol reader (kernel bodies above 1MB, behind LDT bases), before a single
`disp*` row could run.

---

## 10. Waves, if it goes ahead

| wave | content | boots? |
|---|---|---|
| **M0** | decide: ring 0 (the floor is 286, section 8), the identity map, whether kern_mega is `KERN_BIG`-additive | — |
| **M1** | protected-mode entry after stage 2. GDT identity map, IDT, PIC remap, a #GP/#NP/#SS reporter. Native IRQ0, keyboard and CMOS. Floppy and video by reflection **with IRQs masked**. Kernel still at 0x0060, with code and data selectors split. The sweep: `[cs:]` writes, `push cs`, the `OSAPI_SLOT` cells, worked off `t_cswrite.py --list` (the gate itself is built, 2.4.1). Only the ~24 clean packages run | QEMU, a desktop |
| **M2** | native 765 + 8237 floppy. HDD rung 1 absorbed into the kernel. Reflection stays only for ROM-only disks, which refuse if they are slow | QEMU + 86Box 286 |
| **M3** | LDT allocator. `.cold`, modules and caches above 1MB. Conventional memory becomes package heap. Stable selectors for LDT claims, so no relocation procs | QEMU, measured heap |
| **M4** | SDK: code aliases for claims (`cc_ovbind`, `os88partsbody.inc`), `OS88_HUGE_INCR`, the shared speaker and pcm includes. The C packages are rebuilt | +11 packages |
| **M5** | per-package rework, worst first by usefulness rather than by size | — |
| **M6** | the DOS box beside protected mode (2.7); hibernate. The plan is docs/plans/KERN-MEGA-DOS-PLAN.md: the whole-machine handoff with the kernel parked above 1MB, an XMS door of its own, and a two-section hibernate image | field |
| later | ring 3 + call gates (an ABI flag day, SPEC.md 20.2.0.1); a 386 tier inside kern_mega | — |

M1 is the experiment that settles whether this is worth continuing.

### 10.1 M1 as built (2026-10-17)

`make mega` builds kern_mega into build/megak/ and `build/mega.img`, a 1.44MB
system disk that pairs with the shipped build/apps.img. kernel/mega.inc is
the switch; the rest is a sweep of the shared files, every arm of it
`%ifdef KERN_MEGA`, and **kern_big assembles byte-identical with all of it
in** (compared against a clean-HEAD build).

**What runs in protected mode, on QEMU:** the desktop and menus; the
serial mouse; the keyboard, through mega_k09 and mega_i16 (the BIOS's own
buffer, so no caller of `int 16h` changed); drives A: and B: listed, a
folder opened and B: mounted for the first time, through REFLECTED
`int 13h`; Calculator launched, driven by mouse and keyboard, and closed;
Fractal, whose worker task and VGA drawing ran to 100%; isoTron's windowed
title screen; the PS/2 mouse (QEMU's aux port, IRQ12) as well as the serial
one; the Sticky Apps menu (STIKMENU.DRV) and the Control Panel (CTRL.DRV,
every page), the first on-demand modules to run in protected mode; a desktop
shortcut dropped, which wrote SYSTEM.CFG through a reflected `int 13h` write
(the image verifies clean afterwards). A fault is reported on the debug port AND on the glass - one line under
the menu bar, vector, error code, CS:IP and SP - so a machine with no debug
port can be read by photograph.

**What the bring-up found, in the order it found it** - each is a rule for
whoever extends this:

1. **A selector is not a segment, and the BIOS only takes segments.** The
   reflection hands DS and ES to the ROM as real-mode segments, and the first
   version assumed every GDT selector was an identity one. The loader's
   scratch selector (MEGA_SEL_ODD) went down as segment 0x0030, and the
   floppy's DMA put a sector of the package over the API table, where no
   watchpoint could see it. mega_s2seg now reads the base out of the
   descriptor.
2. **"Is this the kernel?" is asked by comparing a DS with CS**, and in
   protected mode the kernel's DS is its data alias: wm_create filed the
   Disk window as a package's. kbm_isr's `.selfchain` guard is the same
   question asked the other way - "is the vector I chain to my own CS?" -
   and swallowed every key until the ROM-half chains were given a different
   selector for the same code (MEGA_SEL_BCODE).
3. **A far call into a package needs the package's CODE alias.** Every place
   the kernel enters one - wm_pkgcall, the loader's entry call, the driver
   dispatcher, a new task's frame - stored the DATA segment, which is also
   what every near pointer in the record is relative to. MEGA_PCALL,
   MEGA_CSELTOP and mega_frame_cs convert at the call.
4. **Off-grid arithmetic is real, and it is the FILE LAYER's**: the
   decompressor wants DI = 0, so the loader points ES at the body's own
   paragraph, 4 along from the region's base, and diskw's by-name I/O
   normalised every caller's ES:BX to an offset of 0..15 by moving whole
   paragraphs into the segment. On kern_mega dskw_norm moves whole 128-byte
   GRANULES (offset 0..127, the segment staying an identity selector), the
   entry points store the segment the caller's selector stands for
   (mega_s2segf), and the two places that need an exact paragraph - a
   compressed file's expansion target, and lz's 64KB slide of it - ask
   mega_oddsegf for a selector. The module loader's slot table holds the
   CODE alias (modules never move), and the modules' `*_go` trampolines push
   the kernel's code selector rather than DS (PUSH_KCODE).
5. **Bimodal state has to be where the early boot can read it.** k_dsel -
   the data selector KDS/KES read - was first placed with the rest of
   mega.inc, late in `.text`, and the boot splash calls vid_apply while that
   part of the image is still arriving: KES read 0 and the video block was
   copied over the API table. It sits beside the table now.
6. **QEMU DOES NOT ENFORCE A CODE SEGMENT'S READ-ONLY BIT.** The mouse ISR
   stored through CS on every packet and nothing faulted. So **QEMU-green is
   not 286-green for 2.4's rule**: tests/cswrites.txt is the list of what
   still faults on iron, and since this round it counts only the arms
   kern_mega actually assembles. The resident `.text` sites M1's desktop
   reaches are converted (both mouse ISRs, font_run's unaligned path, the
   inline API cells, gfx_points and the clip-table reload, vid_ctx_pre, the
   compactor's call into a relocation proc - which also far-called a
   package's proc through its DATA segment, a #GP even QEMU sees); and the
   ratchet now counts `mov reg, cs` and `mov reg, KERNEL_SEG` into DS or ES
   as well as `push cs / pop`. What is left is knob-only, real mode by
   construction (`.boot2`, `.ovl`, the hibernate stub, mega_svc's real-mode
   half), and **the on-demand modules - ~390 writes in CTRL.DRV,
   DISKOPS.DRV, DOCK.DRV, STIKMENU.DRV and the rest**, which will fault on a
   real 286 the first time each is used.
7. **A kern_mega conversion must be a no-op in REAL MODE, and the boot is
   real mode.** The first Victory run (2026-10-17) froze at *Loading Driver
   2/2 (Hard Drive)* with the floppy motor on - before the switch, in code
   QEMU had run a hundred times. The boot SNIFFS for drivers (SPEC.md
   51.3.2: an OPL2 answers, `int 13h AH=08h` reports a fixed disk), and the
   Victory's PicoMEM has both, so its boot loaded SOUND.DRV and HDD.DRV where
   every QEMU run here had loaded none. Expanding the compressed SOUND.DRV
   ran mega_oddsegf and mega_s2seg, which assumed protected mode: in real
   mode they wrote a "descriptor" into the IVT and handed back segment
   0030h, the image was expanded over the front of the kernel, and k_dsel
   read 7400h - so the next IRQ0 chained through 0000:0029. Five places had
   the same assumption and all now ask the mode first: those two helpers,
   MEGA_CSELTOP (`[cs:k_dsel]` in `.text`, `[ss:mega_pm]` in `.cold`), the
   compactor's call into a package's relocation proc, and the module slots,
   which the boot stores as segments and mega_enter converts
   (mega_mods_fix - with DOCK.DRV's and WIN95.DRV's vector tables, which
   those images fill with their own `cs`). A task that predates the switch
   cannot be converted at all - its stack holds real-mode return frames -
   so mega_enter now REFUSES the switch if a package or driver task exists,
   which only AUTOSTART can arrange, and every refusal says why on the glass
   (`real mode: ...`) as well as on E9.
8. **Every far call into a driver goes through a selector conversion**, and
   two did not: the service call (`drv_svc_call_x`, a sound driver's tick
   and play verbs) and the block call (`drv_blk_call_x`, a driver-backed
   volume's sectors - opening C: through HDD.DRV was #GP 9CC0 at
   `0C60:CE8D`). Both are MEGA_PCALL now.
9. **A reflection must not re-initialise the 8259.** It used to move the
   PIC's base to 08h for real mode and back to 20h after, and ICW1 resets
   the edge latches: a serial-mouse byte that arrived while IRQ4 was masked
   for the call was LOST, the UART kept its line high, never made another
   edge, and the mouse was dead for the rest of the session. With both
   drivers loaded the reflections were long enough to hit it every time.
   The PIC now stays at 20h throughout, mega_enter copies IVT 08h-0Fh to
   20h-27h so real mode finds the BIOS's handlers there, and the masked edge
   is still latched when the masks come off. A 286's reset hands control to
   the BIOS, which might re-initialise the 8259 on the way to 40:67:
   mega_r286 compares the masks with what it left and re-initialises only
   if they changed.

10. **A20 is XMEM.DRV's sequence, not a shorter one.** The second Victory
    run reached the desktop and refused the switch with `real mode: A20
    will not open`. mega_a20 probed once after each poke, and the 8042
    acknowledges its output-port write long before the line moves; it also
    ran the D1h/DFh pair with interrupts on, where a keyboard ISR can take
    the controller's pending command. XMEM.DRV's sequence had already opened
    this machine's gate (docs/reports/LOADALL-VICTORY286-2026-10-14.md), so
    mega_a20 is that sequence now: a probe inside `cli` that complements the
    alias and puts it back, port 92h only when it does not read as absent,
    the 8042 pair in one `cli` window, and 512 probes of settle after each.
    QEMU's BIOS leaves the gate open, so it was checked by shutting it first
    in a scratch build, once through the 8042 and once through port 92h:
    both reopened and switched.

11. **Segment 0 is the NULL selector, and QEMU does not fault on it
    either.** The third Victory run switched - A20 opened - and faulted at
    once: `#GP err 0000 at 0060:3453`, which is kbm_shf reading the BIOS's
    shift flags at 0000:0417 through DS = 0. The kernel reads the IVT and the
    BIOS data area that way all over, to save the byte `mov ax, 40h` costs,
    and loading a null selector is legal - only the access faults. kbm_shf
    runs on EVERY UI pass of a machine with no mouse found, which the
    Victory is (nothing plugged in) and no QEMU run here had been, since
    QEMU always has a PS/2 mouse. The sites kern_mega reaches after the
    switch - the keyboard pointer's shift state and buffer, the equipment
    byte, the video mode byte, CTRL.DRV's EGA records, the Disk window's
    modifier read, the compactor's IVT walk, the mouse's vector checks, the
    speaker's IRQ0 restore and the scheduler's stop - load MEGA_SEG0 now:
    MEGA_SEL_LOW0 (base 0, limit 4FFh) once mega_enter has run, 0 before.
    **And QEMU let the original run clean** - built with `NOPS2=1` and no
    serial mouse, so the keyboard pointer was live and its shift read ran
    every pass, the unfixed kbm_shf did not fault. Its TCG checks no segment
    on a data access, so this is item 6's blind spot a second time and
    `tests/unit/t_seg0.py` is item 6's answer a second time: a fast-tier
    ratchet over `tests/seg0.txt`, on t_cswrite's walker, that goes red with
    the old kbm_shf put back. What it lists that is still WRONG on kern_mega
    is the drivers: the Sound Blaster's IRQ discovery is deferred to the
    first stream, so it runs in protected mode, faults there, and hooks an
    IVT vector the IDT never reads - **digital audio through a Sound Blaster
    does not work on M1**, and how a driver hooks an IRQ under kern_mega is a
    driver-SDK decision (tools/abi.lock.json) rather than a rename. The same
    holds for HDD.DRV's XMS ring and for any driver loaded from the Control
    Panel after the switch, whose attach does its IVT work then. A related
    gap with no fault: a compaction that moves a driver image patches IVT
    vectors (mem_rr_walk) and not the IDT gates built from them.
    *(2026-10-22: all three are resolved later. 10.3 item 17's `mega_null0`
    and `mega_ivt_sync` (kernel/mega.inc:3704, :3791) complete a driver's
    `ES = 0` hook and copy it into the IDT, so the Sound Blaster plays on
    kern_mega - four completed traps a stream, 14.46.1, and BEVERLY.MOD on
    86Box's 286, 14.65.4; HDD.DRV's XMS ring refuses under protected mode
    (`hd_xa_arm`, drivers/hdd/hdxms.inc, tests/megapkg.txt); and a driver
    move re-syncs the IDT (kernel/memory.inc:2770). tests/seg0.txt's
    `drivers/sound/sb.inc` comment still carries the old "M1 gap" wording.)*

12. **The Victory reached it: protected mode, `protected, 286 reset`, the
    mouse live - THE 286's WAY BACK WORKS ON IRON** (2026-10-17, the fourth
    disk). Opening A: then faulted, `#GP 0000 at 0C60:73B2`: the `rep movsw`
    of dsk_copy_seg_x, whose destination was KERNEL_SEG - in protected mode
    the kernel's read-only CODE selector. Three disk-layer wrappers named
    the kernel that way (`dsk_copy_in_x`, `dsk_get_dir_x`, `dsk_ico_stage`)
    and the dock's icon path named it as `mov ax, cs`, and neither ratchet
    sees a segment handed to a callee in a register. dsk_copy_seg_x takes
    the data alias for a KERNEL_SEG destination now, once, for every
    caller. Beside it, menu.inc's "is this a kernel window" asked CS and
    not the data segment (item 2's trap, harmless only because the strings
    are at one address either way).
13. **BOCHS ENFORCES WHAT QEMU DOES NOT, and is now the check before a
    field run.** Ubuntu's `bochs` 2.7 runs this tree with every segment
    check a 286 makes: it reproduced item 12 EXACTLY - the same vector,
    error code, CS:IP and even SP (1A65) as the photograph - from the disk
    the Victory ran, and passed the fixed one through A:, C:, B:,
    Calculator and Fractal, then faulted the Sticky Apps menu in
    STIKMENU.DRV's own image (`2684:0018`, an LDT code alias), which is
    the module work item 6 lists. The recipe: `BIOS-bochs-legacy` (the
    default BIOS's 32-bit init crashes before booting anything here),
    `pci: enabled=0`, `clock: sync=realtime` (otherwise the guest's clock
    runs far ahead and the screen saver and a double-click's timing are
    nonsense), `port_e9_hack: enabled=1` so the fault reporter's E9 dump
    reaches stdout, and `mouse: type=ps2` driven on Xvfb with xdotool's
    relative moves after Ctrl+middle-click grabs it (the guest moves one
    pixel per two X pixels). The Debian build has the debugger compiled
    in and stops at reset, so `c` goes in on stdin. Its CPU is a 386, so
    the reflection takes CR0 and the 286 RESET path stays the Victory's to
    prove - which it now has. A tool for it in tools/ is the next
    instrument, not yet built.

14. **The fifth disk ran everything in 10.2** - the Control Panel's pages
    too, though CTRL.DRV writes its own image on some paths - and three
    things past it: the Hard Drive page, Paint, and NumLock.
    * **An image far-calling another image** (HDD.DRV into HDDTOOL.DRV
      through `hd_tfar`, and back through `hd_rdisp`) loads a data
      selector into CS: `#GP 9500 at 2724:03A1` is `hd_tool_call`. A
      driver cannot know to convert, and the SDK is not changed for it:
      kern_mega's #GP handler (mega_gpfix) recognises the case by its
      error code - a 512-aligned identity selector below A000h, which is
      an image's base and has an exact LDT code alias - decodes the far
      CALL, JMP or RETF at CS:IP, checks the target, and finishes the
      transfer through the alias. A fault per such call, which here is a
      page click; anything that does not match goes to the reporter.
    * **A package's own segment arithmetic** is section 3's "real rework,
      one site at a time", and Paint was a site the survey missed: its
      per-row table (`pt_rowseg`, base + offset >> 4) is off the grid for
      most rows, `#GP 3588 at 25B4:0830` being `mov es, [pt_rowseg+bx]`.
      (A #GP error code drops the selector's low two bits, so 3588 is the
      descriptor named, not the value loaded.) The table holds 128-byte
      granules now - segment & ~7, offset & 127 - which real mode reads as
      the same bytes; kern_big's Paint was checked drawing on QEMU.
    * **NumLock** toggled KB_FLAG but never the keyboard's light, and
      without the ROM's "held" byte (40:18) a typematic repeat toggled it
      back. mega_k09 keeps 40:18 and sends EDh with the LED byte now;
      Bochs's status bar shows the light follow.
    All three were reproduced or checked under Bochs (item 13), Paint's to
    the CS:IP and SP of the photograph.

15. **CTRL.DRV is the first module converted, and the conversion has a
    shape now.** Closing the Control Panel after a change faulted on the
    Victory (no photograph), and Bochs reproduced it first time: `#GP 0000
    at 26E4:095E` with ES = CS, `cpc_ser` building SYSTEM.CFG in the
    image's own buffer through `push cs / pop es`. A module's CS on
    kern_mega is its LDT alias (S >> 2) | 4, so its data segment is
    (CS & ~7) << 2, and two macros in kernel.asm spell that: PUSH_CSDATA
    for `push cs`, and `CSDO op [CSEG x], ...` for `op [cs:x], ...`, which
    runs the one instruction with DS = the image and keeps FLAGS. Both are
    the original bytes off kern_mega, and kern_big was compared identical.
    All of CTRL.DRV is converted - ctrl.inc, shutdown.inc, driver.inc's
    settings macros and desksc.inc's `.modc` half, 33 writes and 9 pairs -
    and the tools learned the spelling: t_cswrite counts a `[CSEG x]`
    outside CSDO as a write, os88ovlchk reads CSEG as naming the image, and
    stkbalance knows PUSH_CSDATA and PUSH_KCODE push one word and CSDO
    moves SP as its instruction does. Bochs saves SYSTEM.CFG through it and
    the disk verifies.

16. **Every package STARTS under Bochs, and the sweep is a tool now.**
    `tests/megarun` is a package that starts every package in its folder
    one every five seconds and names each on port E9 first;
    `tools/os88bochs.py sweep` builds a C: disk with it as `0RUN.O88` (so
    it lists first) beside a batch of packages, boots `build/mega.img`
    under Bochs, opens the folder with the mouse and charges a `kern_mega
    FAULT` - or a minute with no new E9 line - to the package last named;
    the next boot carries on after it, and every start is screenshotted.
    The 44 shipped packages: all 44 start and draw, Sheet included alone
    (in a batch of six it is refused `04` for memory). Two mechanisms got
    them there, neither a package change:
    * **A CS: write** (`mov [cs:x], ax`, 125 sites in tests/cswrites.txt
      and more in packages) is completed by the #GP handler (`mega_gpw`):
      the instruction is decoded for length, copied to a trampoline with
      its CS: prefix turned into DS:, and run there with DS = the image's
      data segment; a DS or ES holding a CODE selector (`push cs / pop
      ds`) is swapped for its data segment and the instruction re-run.
      A fault per write - fine for a settings store, wrong for a loop, so
      the kernel's own sites are still converted (CSDO, PUSH_CSDATA) and
      the ratchet stays.
    * **The modules**: STIKMENU, the compressor, the association writer,
      the WIN95 theme and DOCK take CSDO/PUSH_CSDATA/KDATAR_M where they
      wrote through CS, and CSDO is two far helpers (`mega_dsimg`,
      `mega_csdata`) rather than an inline expansion, which took
      STIKMENU over `MOD_MAX_KB` when it was one.
    `mem_reloc_call` far-called a relocation proc through the region's
    DATA segment, which is what Sheet faulted on.

17. **Segment 0 is emulated for code the kernel does not own, and the IDT
    follows the IVT.** A driver hooks its IRQ through `ES = 0`
    (SB.DRV's IRQ discovery at the first stream, USBMOUSE.DRV), and the
    IDT is built once at the switch - so the hook faulted, and had it not,
    the IDT would never have seen it. `mega_null0` takes a #GP(0) whose DS
    or ES is the null selector, puts LOW0 in it and runs that ONE
    instruction under the trap flag with IF off; `mega_db` puts the 0 back
    (unless the instruction loaded the register itself) and calls
    `mega_ivt_sync`. No decoding: the CPU runs whatever it is, and a REP
    string op traps per element. `mega_ivt_sync` compares IVT 08h-0Fh and
    70h-77h with a 64-byte copy and, on a difference, writes back any
    LDT alias a driver stamped as its CS (`mov [es:si+2], cs` stores the
    PROTECTED-mode selector, which the next reflection would jump through)
    as the segment it stands for, refreshes IVT 20h-27h (where a real-mode
    IRQ lands, the 8259 staying at 20h) and rebuilds IDT 22h-27h and
    70h-77h through a new IDT alias, MEGA_SEL_IDT (GDT entry 7). **Not
    20h and 21h**: their IVT cells hold the BIOS's halves for a
    reflection while the IDT runs the kernel's, and the first build that
    rebuilt them handed the tick and the keyboard to the EOI stub - a
    desktop that froze the moment a driver hooked anything. The memory
    compactor's IVT walk (`mem_rr_walk`, a driver image moving) calls
    `mega_ivt_syncf` after it. Under Bochs with an SB16 and SYSTEM.CFG
    wanting SOUND.DRV, a WAV double-clicked on C: plays to the end through
    DMA 1 and IRQ 5 - 0.4 s of output in Bochs's capture, the length of
    the file. ETHER.DRV needed nothing new - it hooks no vector and polls
    - and with `--ne2k` (an NE2000 at 300h on Bochs's own slirp) the Wire
    zone comes up and the Browser fetches a page served on the host at
    10.0.2.2: DHCP, ARP, TCP and HTTP, all in protected mode.
    `os88bochs.py --sb --cfg BITS` is that machine, and `dbg`
    breaks into the running Bochs, runs debugger commands and continues,
    which is how IDT 20h was seen to have become `mega_eoi1`. ONE BOCHS AT
    A TIME: clicks go to the top window of the display, and two machines
    on it read exactly like a guest that froze, which cost an
    hour here; `boot` refuses while one runs and `stop` ends them.

18. **Off-grid packages are one site at a time, and two are done.** 1942
    derived band segments 2,688 bytes apart; the strides are whole
    128-byte granules now. Clear Skies decompresses three things into its
    own segment at addresses that are not multiples of 8 paragraphs - the
    vocabulary wherever the image ends, every art band 1,360 bytes along -
    so `cs_xpand` expands at the granule below and moves the bytes up,
    keeping the up-to-112 it covered on the stack; and `cs_artes` returns
    ES = the package's own segment with SI the band's offset in it,
    rather than the union's own segment. 56 bytes of image, on every
    kernel, and Skies flies in Mode X under Bochs and comes back.
    **Frotz** (`zm_map`, `zm_addr`, a jump's negative wrap, @verify's
    walk) and **Tracker** (pattern p at blob + 64 + 64p offset 60, a
    sample at blob + 8 x (start >> 7) offset start & 127) normalise to
    128-byte granules now - the same bytes in real mode, so neither
    package has a kern_mega arm: a 136KB v5 story compiled for the test
    (routines above 64KB, @verify) plays to the same transcript under the
    harness and on kern_mega, and BEVERLY.MOD plays through a Sound
    Blaster there. **RunCPM needed nothing** - the `rc_zseg + (off >> 4)`
    the survey named has `off` a multiple of 512 - and runs MBASIC under
    Bochs; CWORD, the C64, PaccMan, Weave and Loom all start too. The
    **DISKOPS compressor** was the kernel's own case: `cmz_far` and
    `cmz_move` normalised to 0..15 and the header was written at
    (ES-1):8; kern_mega normalises to granules and writes at (ES-8):120,
    a package's Uncompress takes `mega_oddsegf` for its body as the loader
    does, and a package's COMPRESS refuses in protected mode - the encoder
    wants its source at offset 0, and the body starts past the clear
    prefix. Plain files round-trip byte-exact. Still open: HDD.DRV's 386
    XMS ring (SPEC.md 52.1.4) loads ES's hidden base the unreal way and
    probes A20 through FFFF, neither of which protected mode allows - not
    reachable on M1, which ships no XMS driver for a package to arm it
    with. A generic answer was costed and refused for now: an exact selector per paragraph does not exist in an identity
    map (the GDT is full, and a value with RPL bits set names an entry
    shared with its granule), so it would mean rewriting the register's
    VALUE, which breaks every program that does arithmetic on it after.

The alternating `kernel-full.bin` noted while measuring kern_big's
identity is a `make mega` in the same tree: the sub-make leaves something
in the shared `build/` that kern_big's next assembly reads, so the identity
check is made in a fresh worktree, HEAD against the change.

Items 7-9 reproduce on QEMU once it is given the Victory's devices - an
AdLib (`-device adlib`) and a formatted IDE disk (`tools/os88hdd.py
--noboot --raw`). **That is now the QEMU configuration for kern_mega**, and
the one with no drivers is the easy case.

### 10.3 M2 and M3 as built (2026-10-17)

**M2 - the floppy is native; the fixed disk already was.**

1. **`int 13h` AH=02h/03h on drive 0 or 1 is run by the 765 and the 8237**
   (`mega_fd13`, entered from `mega_svc` before it reflects anything). It
   waits for IRQ6 with interrupts ON, so the pointer and the sound no
   longer stop for every floppy transfer - which is what a reflected call
   did, masking every IRQ but the BIOS's own for ~200 ms. The IDT's IRQ6
   gate is `mega_fdisr`, which sets the BIOS's own flag (40:3E bit 7), so a
   reflected call and the native path wait on the same bit.
2. **It is an accelerator, not a second driver.** It shares the BIOS's
   state - the recalibrated bits (40:3E), the motor bytes (40:3F/40:40,
   whose countdown `mega_t08` already ran), the last data rate (40:8B) -
   and hands everything it does not recognise to the BIOS: another
   function (reset, format, parameters stay reflected - short or rare), a
   call that is not `dsk_xfer`'s, a buffer across a DMA page, and ANY
   failure but a write-protected disk. The BIOS then does what it always
   did, so the path can only be faster or equal. Eight fallbacks in a row
   switch it off for the session, on E9 and in a toast.
3. **The media is derived, not read from 40:90.** That byte's encoding is
   the BIOS's own - Bochs's says 25h for an established 1.2MB disk, with a
   double-step bit no 1.2MB disk wants - so the rate comes from the
   volume's sectors per track (`dsk_xfer` writes it into the parameter
   table as EOT before every call) and the drive's CMOS type: 18 and 15
   are 500 kbps, 9 in a 1.2MB drive is 300 kbps double-stepped, 9
   elsewhere 250. Only `dsk_xfer`'s calls are taken (`[mega_fdx]`),
   because only for those is that EOT the volume's: a run across a head
   with a wrong one would read the wrong sectors WITHOUT failing. A wrong
   rate guess fails (missing address mark) and falls back - QEMU showed
   exactly that once, for A:'s boot sector read while B:'s spt was still
   in the table.
4. **The change line** (CCR bit 7) is cleared natively by a step pulse - a
   recalibrate and, since a recalibrate at track 0 pulses nothing, a seek
   to cylinder 1. Still set after that is no disk, the BIOS's to report.
   The kernel gives 06h no meaning of its own (it retries; a swapped disk
   is found by the volume), so nothing is lost by not reporting it.
5. **Checked under Bochs** - 1.44MB in A:, and in B: 1.2MB, 720KB in a
   1.44MB drive and 360KB in a 1.2MB drive (double-stepped, 300 kbps): a
   60KB file copied C: to B: is byte-identical on every one, every
   transfer native (`kern_mega: native floppy A`/`B` on E9, no fallback) -
   and under QEMU. One defect on the way is worth the note: replacing the
   40:90 read also deleted the line that pointed ES at the BDA, so the
   waits polled, and the motor bookkeeping WROTE, through the caller's ES
   - its buffer. A boot that does nothing but list a disk cannot see that;
   a byte-compare of a copy can.
6. **The fixed disk needs nothing**: HDD.DRV's rung 1 (ATA PIO) already
   serves a BIOS-known IDE drive on a 286 (SPEC.md 52.1.1), and a 60KB
   read from C: under Bochs reflected no transfer at all - `mega_svc` says
   on E9 the first time a fixed-disk transfer IS reflected, which is what
   a ROM-only controller (the Victory's PicoMEM) will show. "Absorbing rung
   1 into the kernel" is only owed for the kernel's OWN boot partition,
   and kern_mega has no hard-disk boot image yet, so it waits for one.
7. **`CPU_F_PM` (0x10)** is `OSAPI_CPU_INFO`'s new feature bit, set by
   `mega_enter` with `CPU_F_A20` (verified there) and on no other kernel -
   section 12.1, an SDK addition with no format bump.

**M3 - `.cold` and the XMEM pool above 1MB.**

8. **`.cold` runs above 1MB** (`mega_coldup`): copied to 0x125000, its GDT
   descriptor re-based so COLD_SEG:COLD_VSTART is the copy's first byte.
   The selector does not change, so no far reference anywhere is touched,
   and its 37KB rung becomes heap through the ROM path's own word -
   `[rom_vgabw]` moves the planar decoder's buffers to the bottom of the
   dead rung and `mem_floor_ax` drops the floor onto it (ROM-PLAN 3.4.2).
   Measured: the Task Manager's heap 534K -> 570K on Bochs. Nothing in real
   mode may run the old copy, and nothing does: every vector the kernel
   installs is in `.text` (the IVT was read back under Bochs), and a
   reflection runs only the BIOS.
9. **The XMEM cells are served by the kernel** (section 12.2): `xm_dsp`'s
   kern_mega arm calls `mx_caps`/`mx_alloc`/`mx_free`/`mx_copy`/
   `mx_relall`, which are XMEM.DRV's block table, first-fit walk, 1KB
   granularity and error codes over a pool from 0x130000 to the top of
   extended memory (16MB at most), with the copy a `rep movsw` through two
   scratch descriptors - GDT 1FFEh/1FFFh, the identity selectors of
   segments FFF0/FFF8, which nothing loads. `tests/megaxm` (on demand,
   `make megarun` builds it) allocates 64KB, writes and reads back sixteen
   4KB patterns and frees it: `caps 3B40`, `alloc 00130000`, `ok` on Bochs.
10. **What M3 still owes**, and why it did not fit this round: MODULES and
    CACHES above 1MB, and LDT claims with stable selectors. A module's data
    segment is derived from its code alias ((CS & ~7) << 2, `mega_dsimg`)
    and its claim's segment is a real segment the file layer reads into and
    normalises (`dskw_norm`, `mega_s2segf`); above 1MB neither is true, and
    the disk caches are DMA and reflection targets that real mode cannot
    reach. The shape that works is code/data selector PAIRS from the top of
    the LDT (entries 1280-2047 alias segments A000-FFE0, which nothing far-
    calls), with `mega_cs2ds`'s rule extended to "data = code + 8" there and
    a bounce for any transfer that must go through the BIOS - which is a
    round of its own, not a corner of this one.
11. **The hard-disk install runs, and the installed disk boots, under
    protected mode** (Bochs, 2026-10-18). `tools/os88bochs.py boot --c blank
    --cfg 2` gives the machine a ZEROED 20MB C: and HDD.DRV. The Hard Drive
    page's Format partitioned and formatted it, and Install copied both
    disks and wrote BOOTHD.SYS (kern_mega's own boot record, from the source
    disk) and the MBR. A reboot with `--c keep --bootc --noa`, from C: alone,
    reached protected mode and the desktop, and the Calculator launched off
    C:\APPS. Nothing faulted on either boot. Booting the harness's own
    pre-built C: (`--bootc --noa`) works too.
12. **The Victory's second run found one more far frame built with `push
    ds`**: `KGO_PAIR`, the Control Panel's own `CPXK` hop (and the boot
    blob's), pushed DS as the segment of a RETF into KERNEL_SEG. On
    kern_mega DS is the data alias 0010, so the Display page's paint
    faulted `#GP 0010 at 2644:1B08`, which is CTRL.DRV's RETF. It now pushes
    `PUSH_KCODE`, as extmod, stikmenu and winmod's trampolines already did,
    and so does fdlg.inc's `fdt_go`. All nine pages paint under Bochs, and
    Force Color ticks.
13. **Hibernate WAS greyed on kern_mega in protected mode** (withdrawn by
    item 71: M6 W5 built it), `Hibernate (PM)`
    (SPEC.md 47 rule 3), because an image would leave out everything above
    1MB - `.cold` among it - and the stub resumes in real mode. It is
    offered as soon as a hard disk exists, so an installed machine would
    otherwise have reached it. The real thing is still M6's (2.6).
14. **NMI is handled, not fatal.** Copying BEVERLY.MOD from the PicoMEM's
    C: to a floppy stopped the Victory on `FAULT 02 at 0060:5C4D`. That is
    vector 2, NMI, landing in `mega_fdirq`'s wait - where a floppy write
    spends its time, so the address says nothing about the cause. Real mode
    hands an NMI to the BIOS, and this IDT handed it to the fault reporter.
    `mega_nmi` now does what the AT BIOS does: port 61h bits 7 and 6 say
    parity or channel check, and an NMI with neither is spurious and
    dismissed. A latched one has its latch cleared and is reported -
    `NMI 61h=XX x N` in the menu bar from `ui_task` and on E9 - and after
    eight both sources stay off, so a held line cannot loop. Which line it
    was is the field run's question. It is SPECULATION that the PicoMEM is
    the source; whether kern_big's BIOS halts on the same copy is the other
    half of the answer.
15. **The floppy stutters through a C: copy on the Victory**, where it
    normally makes one smooth head move every half second. Two instruments
    now report on the glass, because the field machine has no E9. (a) The
    FIRST native fallback of a boot shows as `FD BIOS ss s0s1s2 Dmm`: the
    step, ST0-ST2, the drive and the media byte. The fallback count resets
    on every success, so a path that alternates never reaches the eight
    that switch it off, and the BIOS redoing every write is heard but not
    seen. (b) Every C: call on a 286 is a reflection through a CPU RESET
    and the BIOS's shutdown-0Ah path, which only the Victory has run.
    `mega_r286` compares 40:3E/3F with what they were before the reset, and
    the first difference shows as `RST 3E aa>bb 3F cc>dd`. A set 3E bit 7 is
    an IRQ6 inside the reset path, which means a controller reset. The
    motor bits are not asked when a tick ran the motor count out.
    (c) `mega_fdrprobe` asks the 765 itself, in `mega_r286` and at the top
    of every native transfer. A reset controller holds an interrupt for
    each drive until SENSE INTERRUPT STATUS, and an idle one answers 80h.
    A reset found this way is sensed out, every drive is marked
    uncalibrated in 40:3E, and the motors are marked off in 40:3F, so the
    next seek recalibrates first. Without that, a controller that believes
    it is at cylinder 0 steps N more from wherever the head really is. That
    is the mechanism that fits the field report: a drive that stutters, a
    copy that still succeeds (the wrong-cylinder write falls back and the
    BIOS recalibrates), no fallback ever reaching eight, and rails out of
    line at the next power-on. It shows as `FDC RESET R nn F nn`, by where
    it was found. The reports queue behind any toast already up. **Bochs
    cannot test the 286 path**: its triple fault resets the whole machine,
    devices included, where an AT resets only the CPU. A forced-286 build
    with C: through the BIOS hangs opening C:, with or without the probe.
    An earlier claim here that it copied identically was wrong: C: never
    reflected in that run. The Victory is the test.
16. **The NMI was a PARITY check**: `NMI 61h=A0 x 08` on the Victory, in
    one copy in three. A0 is bit 7 (parity) plus bit 5, the timer-2 output.
    Eight of them hit the cap, so parity checking is off for the rest of
    that session, which makes any further corruption silent. The line now
    also carries the CS:IP that the NMI interrupted.
17. **Restart hung at a blinking cursor on the Victory**: the PicoMEM's
    warm reset in `dsk_fdd_park_x` is `mov es, 0` and `jmp FFFF:0000`, and in
    protected mode FFFF is a selector. In protected mode the park now skips
    it and reaches `int 19h`, which `mega_svc` has always turned into the
    8042's reset line. It now sets the 1234h warm-boot flag first, so POST
    skips the RAM test, as Ctrl-Alt-Del does. Restart comes back to a fresh
    desktop under Bochs. (The first attempt jumped past the PicoMEM test's
    `pushf` to its `popf`, loaded a stack word into FLAGS, and stopped on a
    #DB at `dsk_fdd_park_x.unit`. That was found under Bochs and fixed.)
18. **The PicoMEM's Sound Blaster needs its option ROM's IRQ handler.** A
    PicoMEM 1.x has one line, its ROM's multiplexer owns it, and on it the
    CPU copies each 128-byte "DMA" block and the SB's IRQ is raised as a
    software `int 8+n` (SPEC.md 34.10.1). Every ROM IRQ was dismissed with
    an EOI, so no block was copied, SOUND.DRV's watchdog ended each stream,
    and Tracker cycled Buffering/Playing about three times a second. An
    IRQ whose IVT handler is an OPTION ROM's (C000-EFFF; and IRQ 9 when the
    BIOS's redirect leads to one) now goes to real mode through `mega_svc`,
    marked as an IRQ. The IMR is kept, because the multiplexer raises a
    line only when it is unmasked. The handler is entered with IF off. The
    IVT entries for IRQ 2-7 that name kernel or driver code (0Ah-0Fh and
    22h-27h) are capture stubs for the window, and each line they catch is
    replayed through its IDT gate once back in protected mode. The
    caller's flags are left alone. A throwaway Bochs build that routed the
    SB16's own IRQ 5 this way played BEVERLY.MOD through 6,434 reflections
    and 6,434 replays. Each IRQ on a 286 is a CPU reset, and a 22 kHz
    stream is ~170 blocks a second; that cost is the field run's to read.
19. **Both NMIs landed on `mega_fdirq`'s `hlt`** (0060:5C4D and 5E99 in the
    two builds), with the CPU halted, so the read that tripped parity was
    the floppy write's DMA. The line now gives the op, the 24-bit DMA
    address and the sector count when a native transfer is in flight
    (`NMI ss W llllll nn`). The `FD BIOS 08 400100 A90` the Victory showed is
    benign: a mount-time read of A: before the volume's sector count is
    known, at a guessed 250 kbps, which fails and falls back by design.
    The reset probe now skips while a native transfer is in flight and
    takes only MSR = 80h exactly as idle, so a seek's drive-busy bits keep
    it out.
20. **The reflected multiplexer now runs, and Tracker plays ONE note.**
    Restart is confirmed on the Victory with its PicoMEM. Tracker went from
    nothing to one note, then Buffering, in a loop: the first pre-rolled
    block plays and the next block IRQ misses SOUND.DRV's watchdog. The
    PicoMEM BIOS source (pmbios.asm's `PM_Int` and `IRQCMD_IRQ5`) shows the
    SB's `int 0Dh` is fire-and-forget, so capture-and-replay's ordering is
    not the cause. The suspect is the COST: every 128-byte block is an IRQ,
    and every IRQ reflection on a 286 is a CPU reset through the BIOS's
    shutdown path. `mega_rfsec` measures it with PIT channel 0 and shows
    `RFL rrrr/s uuuu` (count a second and average us, hex) once a second
    when there were 20 or more, at most every 15 seconds. Bochs's 386 CR0
    path, with the SB16's IRQ 5 forced through it, reads ~6Ch/s at ~62h us.
21. **The parity NMI is a track WRITE from conventional RAM**:
    `NMI B0 W 034600 12`, 18 sectors from 210K, at the tail of a 1MB copy.
    The DMA address is the buffer's own, so a misdirected transfer is ruled
    out. An A/B image with the native floppy off (`mega_fdoff` = 1) is the
    next question, for this and for the jitter: if both go, they are the
    native path's; if they stay, they belong to the 286 reflection.
22. **The A/B answered the NMI: it is the native floppy path's.** With
    `mega_fdoff` = 1, ten 1MB copies through the BIOS raised none. The DMA
    programming is the BIOS's byte for byte. What differs is what the CPU
    does during the transfer: `mega_fdirq` HALTED where the BIOS spins, and
    both field NMIs were on that `hlt`, the 8237 reading RAM under a halted
    286, which kern_big never does. It spins now. (Some 286 chipsets are
    reported to mis-sample parity on DMA cycles during a halt bus cycle;
    whether this one does is what the next run says.)
23. **Tracker is not a throughput problem**: 22 kHz, 11 kHz, XT Mode and
    5.5 kHz all play one note, and 5.5 kHz two. The chain from the card's
    interrupts to SOUND.DRV breaks after the first block or two at any data
    rate. `RFL 001C/s 04B1` was 28 reflections a second at 1.2 ms each, so
    the reset is cheap. A `Q cc ss oo bb vv rr` line now follows each RFL:
    per second, the PicoMEM ROM's PC-command IRQs (its DMA copies), its
    raise-an-IRQ commands (its Sound Blaster's), others, results that were
    not COMPLETED and the last of them (12h = the SB line masked, 14h = the
    same line as the card's), and the reflections that caught and replayed
    a kernel line. Read off `BV_IRQSource`/`BV_IRQStatus` at ROM segment +
    4000h, per pmbios/pm_vars.asm. **Superseded the same day**: the
    owner's firmware (`firmware/picomem1/`, Elendilon/ISA-PicoMEM
    `pm1x-pr` at 5e507be) links a PREBUILT `src/rom/pmbios.bin`, not the
    tree's `pmbios.asm`, and its multiplexer works off a request list at
    ROM + 4356h (`IRQ_VAR_OFFS`): [0] DMA copy, [1] audio IRQ, [2]-[5]
    network, mouse, keyboard, PC command. The Q line reads that now:
    `Q cc ss oo bb ii rr` = copies, audio requests, others, audio requests
    that raised none of our lines, the IMR the ROM saw, replays.
24. **The Tracker defect, found in that binary's ISR.** Its audio stub
    raises the SB's IRQ only when `in al, 21h` shows the line UNMASKED.
    `mega_r286` decided whether the BIOS's reset path had re-initialised the
    8259 by comparing the IMR with `imr1 | B8h`, the mask a SERVICE
    reflection sets. An IRQ reflection keeps the IMR, so the compare always
    failed: `mega_r286` re-initialised the PIC and wrote `imr1 | B8h`,
    masking the SB's line, and the multiplexer skipped every block IRQ. The
    stream died after its pre-roll at any rate, on the 286 only, because a
    386's CR0 path never comes through `mega_r286`. It now expects the mask
    the reflection actually set.
25. **Confirmed on the Victory 286 (2026-10-18)**: Tracker plays
    BEVERLY.MOD at 22 kHz through the PicoMEM's Sound Blaster.
    `RFL 002B/s 04CA` is 43 IRQ reflections a second at ~1.2 ms, about 5% of
    the CPU. `Q 24 08 00 00 18 08` is 36 copies and 8 audio IRQs a second,
    no audio request without a raised line, IMR 18h (the SB line open) and
    8 replays. No parity NMI in at least eight 1MB copies after the
    floppy wait stopped halting. Restart works on the PicoMEM. The jitter
    that is left is in a copy's first two seconds only, which fits
    `dskw_alloc`'s first-fit rover (back to cluster 2 at every mount)
    filling a fragmented disk's front holes before reaching contiguous free
    space. That allocator is kern_big's too, and the A/B to settle it is
    the same copy onto a freshly formatted floppy.
26. **The D: after an install, and the C: that says "No os8088 disk", are
    one defect, and it is kern_mega's alone.** Reproduced under Bochs with
    C: through the BIOS (a throwaway HDD.DRV with its rung-1 probe skipped):
    install, reboot from A: with that C: kept, open C:, Install again. An E9
    trace on the volume slots read `A2` at boot and `D2F A3` in the install:
    OSAPI_VOL_DEL(2) was REFUSED by `osapi_vol_fence`, and the install's own
    mount then took row 3. The fence compares the caller's DS with the
    published DATA segment, and HDDTOOL.DRV calls the resident with DS =
    `[hd_rseg]`, which `hd_tool_need` fills with `push cs` - on kern_mega the
    resident's LDT CODE alias. Reads through it work and the first write
    faults into `mega_gpw`, which converts DS; HSV_MOUNT and the page's
    Unmount both write a byte before their kernel call, so they passed,
    while HSV_UNMOUNT's first act is the call. So the row kept its icon and
    its letter, `hd_vols` forgot it (a C: whose handle names nothing: "No
    os8088 disk"), and every later mount of that partition - the install's,
    and the owner's Mount on the Victory - landed on D:. The fence now takes
    an LDT selector as `(S & ~7) << 2`, mega_cs2ds's rule, inside `%ifdef
    KERN_MEGA` (the same fence guards `desk.inc`'s driver test). After it the
    trace reads `D2 [2 A2` and C: comes back as C:; a blank install and a
    second install over it on rung 1 show no D: either. kern_big never had it:
    `push cs` there is the image's segment, which is also its data.
27. **The installer's refusal now says what it tried**, because the field
    machine has no debugger: `No system disk: A:k31 B:g1`. One entry per
    volume `hd_isrc_ck` asked, in the order it asked, at most four (the line
    is HIW_CELLS = 42 wide). `X:g<n>` = the quiet goto to X:'s root refused
    with FERR n, and 1 is every way a mount fails (no disk, a read error, not
    a FAT disk). `X:k<n>` = X: mounted, its root was walked n entries far and
    KERNEL.SYS was not among them; k0 is a root with nothing readable in it.
    The first entry is where the search STARTED - the volume the user was on,
    or A: when that was the destination - and the rest are the floppies in
    index order, so a first letter that is not A: says the search began
    somewhere else. Verified under Bochs with a TEST build that looked for
    KERNEL.SYX and a blank 720KB disk in B:: exactly the line above.
28. **A refusal before the erase puts the destination back.** `hd_iunmount_dst`
    runs before the source check, by design (the source must not be the
    disk about to be written), so "No system disk" and "Older disk" left C:
    unmounted until the next boot - the "nothing done" was true of the data
    and not of the desktop. It now records the slots it dropped and which one
    the user stood on, and `hd_iremount` mounts them again on those two paths,
    with the user back at that volume's root. In the KERNEL.SYX run C: came
    back as C: (`A2`), on rung 0. These two items change HDDTOOL.DRV's bytes
    on kern_big as well, deliberately: the message and the remount are right
    on both kernels. HDD.DRV and kern_big's `kernel.bin` are byte-identical.
29. **"No system disk" itself did NOT reproduce under Bochs** - rung 0 for C:,
    a kept install, the C: window open first, a 720KB disk in B: or a blank
    one. Two things differ on the Victory and neither can be shown here.
    (a) **Every floppy mount's first read takes a fallback there and never
    here.** `disk_mount` reads LBA 0 at the 9-sector fallback geometry, so
    `mega_fd13` guesses 250 kbps for a 1.44MB disk; the Victory's 765 fails
    it (item 19's `FD BIOS 08 400100 A90`) and the BIOS reads it through a
    286 RESET. Bochs does not check the data rate: no Bochs run here printed a
    single `FD BIOS` line, so the fallback inside an install's batch is a path
    only the iron has run. (b) The page's own `Need the system disk` is what
    HDD.DRV draws when the tool will not load - `hd_tool_need`'s quiet goto
    to the volume it was loaded from (A:\SYSTEM) refusing - so the owner's
    report that the PAGE refused until the panel was closed and reopened
    fits the same cause: A: transiently unmountable after the install's
    unmount, then readable again. The next photograph of item 27's line
    decides it: `A:g1` is the floppy path, `A:k<n>` is the directory walk,
    and a line that does not start with A: is the search order.

**M3, continued - the LDT allocator and the first claim above 1MB
(2026-10-18, KERN-MEGA-HANDOFF track 1).**

30. **The LDT allocator** (`mega_ldt_alloc`, `mega_ldt_free`): entries
    1280-2047 are built FREE (access byte 0) and handed out with any 24-bit
    base and limit, data or code, freed by selector; the scan and the claim
    run under `pushf`/`cli`. They are written through `MEGA_SEL_LDTD`
    (FFE8h, GDT entry 1FFDh: the identity selector of a segment nothing
    loads, `MEGA_SEL_XA`'s argument), whose limit is exactly the table so
    nothing written through it reaches the IDT or `.cold`. Every routine
    that reads a selector's base now reads the DESCRIPTOR in this range
    (`mega_selbase`, `mega_s2seg` - which answers FFFFh for a base above
    1MB, as it already did for a GDT one), and the two that turn a code
    alias into its image (`mega_cs2ds`, `mega_ivt_sync`) refuse it, so a
    #GP with DS holding one is not "fixed" into a wild segment.
31. **Kernel-owned extended memory comes out of the XMEM pool** (13.4):
    `mega_hiclaim` is `mx_alloc` under owner FEh plus a data descriptor
    over exactly the bytes asked for, `mega_hifree` the reverse. One table,
    one first-fit walk, one free figure, so the cells' CAPS (and the Task
    Manager's XMS line) count what the kernel took.
32. **The read-ahead store runs above 1MB** (SPEC.md 18.95; 13.3 step 2).
    `mega_enter` re-solves it (`dsk_rah_redof`, after `sti`): the boot's
    mount claimed it in real mode. In protected mode `dsk_rah_want` asks
    `dsk_rah_wanthi` first - the full width, DSK_RAH_RUNS, under
    `[dsk_rah_cap]` (the free-run and arena-share bounds are about
    CONVENTIONAL memory, which this does not spend) - and falls back to
    the conventional claim only when that is refused. `[dsk_rah_seg]` is
    then a data selector, only ever loaded (`dsk_copy_seg_x` is the one
    reader), and `[dsk_rah_hi]` says so. Every fill reads into a 5KB
    LANDING below 1MB and is copied up: a reflected `int 13h` reaches only
    a real-mode ES:BX, and on the Victory every C: transfer is one. The
    landing is `MEM_P_RAHB` - the store's own rank, purgeable, page-safe
    (`mem_claim_dma_x`) so a fill is one transfer - and a shed of it stops
    the fills, not the hits, until the next mount re-takes it.
    Measured under Bochs, Task Manager open from C:\RUN: the store is LDT
    2804h, base 130000h, limit 7DFFh (7 x 4,608 bytes), the pool's first
    block 32KB under FEh, and the claim table's read-ahead entry is the 5KB
    landing where the kern_mega before this held the 32KB store at 1AE0h -
    **27KB of conventional memory back**. The Task Manager's HEAP figure
    reads 16/568K on both, because it counts a purgeable cache as free; the
    gain is the RAM a shed would otherwise have had to take back (and the
    fills it would then have lost), and what M6's DOS program will see.
    PIXEL.O88 (110,489 bytes) opened off the BIOS-reflected C: and copied
    C: -> B: (a blank 1.2MB floppy) byte-identical, and A:\APPS\DOS.O88
    (47,610 bytes) A: -> B:, native floppy at both ends, byte-identical.
    `tools/os88bochs.py sweep` over the 34 packages on the shipped
    apps.img: all 34 start with no fault - SHEET and SKIES only alone,
    refused 04/05 for memory inside the batch of six that also holds
    PAINT, PIXEL and PXSTEIN, which the kern_mega before this refuses
    identically (an A/B on that batch).
    The SHED path, simulated from the debugger (`[dsk_rah_bnc]` zeroed,
    since PIXEL, SHEET, PAINT and SKIES open together without one): the
    next mount - opening C: - took a new landing, and with it zeroed again
    and no mount between, PIXEL loaded off C: and ran with the word still
    0, so the fills stayed off and the reads went direct.
33. **A reflected transfer to a buffer above 1MB is refused, not made.**
    `mega_s2seg` has always answered FFFFh for such an ES, and the BIOS was
    then handed FFFF:BX. `mega_svc` now answers an `int 13h` 02h/03h with
    that ES as the 8237 would, CF = 1 and AH = 09h, without reflecting it.
    Nothing reaches it today (the store is never a transfer's target); it
    is the fence for whatever is placed high next.
34. **Two things deliberately NOT done.** (a) The native floppy could DMA
    straight into the store (`mega_fd13` takes the base off the descriptor
    and the 8237 reaches 16MB), saving a 4.5KB copy, ~1 ms on a 286, per
    floppy fill against a ~200 ms transfer - but its fallback is the BIOS,
    which would then be refused 09h and walk `dsk_xfer`'s retry ladder.
    The landing is uniform and costs nothing measurable. (b) MODULES above
    1MB (13.3 step 2's second half) are the next round, and its shape is
    13.5.

**Track 2 - the size machinery out of kern_mega (section 4 item 2,
KERN-MEGA-HANDOFF.md).** Every figure below is kern_mega at ONE build number
against the commit before it (`OS88_BUILDNUM=1600`, `tools/kernsize.py`'s
section line), and kern_big is byte-identical across each change.

35. **ROM_COLD splits into a LADDER and an ADOPTION.** The ladder - `.cold`
    on the top rung, under `.vgabuf` - is what kern_mega STANDS on:
    `mega_coldup` drops the heap floor onto the dead rung through the ROM
    path's own word, `[rom_vgabw]`, and `mem_floor_ax` reads it. So
    `ROM_COLD` stays defined. The adoption is the new `ROM_ADOPT`, which is
    `ROM_COLD` on every kernel but kern_mega: stage 2's doorbell at F400,
    `[rom_mfp]` and its seed `rom_none`, `mod_need`'s three ROM arms (SIZE,
    COPY, FIX) and the hibernation head's "where `.cold` runs" byte. A
    kern_mega `.cold` has a home of its own at `MEGA_COLD_LIN`, nothing cuts
    a ROM or an HMA image from it, and a kern_big ROM at F400 could only
    refuse it. **`.text` -6, `.cold` -24, `.boot2` -27.** The Control Panel
    (a module, so `mod_need`) opens under Bochs. On the same commit
    `osapi_sys_kb` asks `[mega_coldhi]` rather than `[api_coldseg]` whether
    the cold rung is heap - on kern_mega that selector never changes, so the
    Task Manager billed `SK_COLD_KB` (37K) of heap to System. It reads
    **System 73K, HEAP 568K** under Bochs now.
36. **The XMEM.DRV transport is not assembled.** The HMA `.cold` and its
    driver pool (SPEC.md 2.10.7) arrive through XMEM.DRV, which loads
    `COLDHMA.SYS`; kern_mega already had neither (`HMA_DRV` was never
    defined there, `MEGADRIVERS` filters both files off `mega.img`, and
    `xm_sniff`'s kern_mega arm never wants the image). What was still
    assembled was the attach path: `xm_row` and its file name, `xm_boot_x`
    in the blob, `xm_dsp`'s image arm, the driver-call guard's `xm_row`
    compare and the compactor's `xm_row` word. None of it could run -
    `[xm_kb]`, the gate every cell tests, is written by `mega_xminit` alone,
    next to `[mega_xmnat]` - so `xm_dsp` is the native arm and nothing else.
    **`.text` -66, `.cold` -6, `.boot2` -106.** `[mega_xmnat]` itself is
    now read by nothing; it is left in `mega_xminit` for track 1, whose
    file that is.
37. **The other three return NOTHING resident, measured:**
    * **`rp_walk`'s fixups** are `boot/osrom.asm`'s - code and tables in
      the ROM and in `COLDHMA.SYS`, generated by `tools/os88romfix.py` - and
      the kernel carries no list of them. kern_mega builds neither image.
      0 bytes, and nothing to guard.
    * **`mem_unblob`** is 19 bytes of `.cold` that hand back the 5.7KB
      blob (`.boot2` 5,721 after 27). It is not scarcity machinery on any
      machine: the blob is the boot, and the boot runs in real mode on
      kern_mega exactly as on kern_big. It stays.
    * **`.ovl`/`.ovlw`** lands on `FAT_SEG` and is overwritten by the first
      mount, and the code that reads it is the blob's, so it costs 0
      resident bytes already. Folding its 5,116 bytes into `.cold` would
      cost 5,116 more conventional bytes FOR THE BOOT (the cold rung, before
      `mega_coldup` frees it), 5,116 of the 7,423 `.cold` has left under
      `MEGA_XPOOL`, and an `os88ovlchk` arm of its own, for no byte after
      the boot. Refused. The real-mode fallback (`mega_enter` refusing) is
      the other reason none of these three may go: everything the boot
      needs before `mega_enter`, it needs on that path as well.
    Total with 39 below: **`.text` -74, `.cold` -30, `.boot2` -133**. The heap
    figure does not move - `.text`+`.bss` is 58,232 against the 58,368 of
    its rung - which is the rung banner's point and not a reason to stop.
38. **The rung `mega_coldup` frees is a HOLE, and a compaction would not
    close it.** Read off `mem_tab` under Bochs after the boot: `[mem_base]`
    11A0 (the dead rung, under the moved `.vgabuf`), the `dirw` cache
    (`MEM_P_DIRW`, 32K, movable) at 1AE0 - the floor the boot claimed
    from - a FAT window (5K, purgeable but PINNED) at 24E0, then a 472K
    run. Running `mem_unblob`'s compaction after `mega_coldup` would move
    the `dirw` cache down and leave 45K under the FAT window, so the
    largest run is 472K either way; a claim bigger than that already gets
    the lot, because `mem_claim` refuses through compact, SHED, retry and
    both caches are purgeable. Nothing is added. The hole goes away with
    track 1's caches above 1MB, which is the right fix: the caches were
    what walled it.
39. **`api_far` pushes `.cold`'s selector as an immediate.** `[api_coldseg]`
    is a WORD so that a ROM or the HMA could re-point it, and on kern_mega
    nothing does after 35 - `mega_coldup` re-bases the descriptor and keeps
    the selector. So `push word COLD_SEG` (a 286's push) replaces the read
    and the word is not assembled: **`.text` -2**, every far call through an
    N cell a memory operand shorter, and the push no longer reads DS. One
    `push` line on both arms (a `%define`), because `stkbalance` reads the
    source and a constructed far jump is two pushes and a `retf` with nothing
    between them - not even a `cpu` directive.
40. **Checked, at the head of the track** (Bochs; real mode under QEMU):
    `make` and `make mega` clean, kern_big byte-identical at build 1600 after
    every change; a sweep of all 49 shipped packages and MEGAXM starts every
    one with no fault, the five refusals (`MEGAXM`, `DESKCAL`, `DESKCLOK`,
    `RLAPPLY`, `STIKNOTE`, error 04) being the base commit's too, and MEGAXM
    `ok` through the native `xm_dsp`; A:, B: and C: open; the Control Panel
    (a module) opens; with `--sb --cfg 1` Tracker plays BEVERLY.MOD, the Audio
    Player a WAV off C: and MIDIRack a `.MID` it reads by name, all with
    sound in `sb.wav`. The REAL-MODE FALLBACK, forced by `qemu -m 1` (under
    192KB above 1MB): the desktop, the Control Panel off the system disk,
    and the Task Manager billing `.cold` to System (106K) because there it
    is not heap. **One trap in the handoff's identity recipe**: a bare
    `make build/kernel.bin` builds the packed kernel's PASS ONE (`KZ_SECS=0`
    placeholders, Makefile `KZDEF`), and a tree that has run a whole `make`
    builds pass two - so the two differ from byte 4344 on with no source
    change at all. Compare like with like (both whole `make`s, or both bare
    targets in fresh trees).

**Track 1, after the merge.**

41. **The Sticky Apps refused "No desk layer (DESK.DRV)" on kern_mega** -
    all three (DESKCAL, DESKCLOK, STIKNOTE), on the kern_mega before this
    round too (62dfe2c0, the same sweep). DESK.DRV's LIB verb hands the
    library's segment back as `mov dx, cs / add dx, offset >> 4`, and in
    protected mode CS is the image's LDT code alias, so the sum named an
    unrelated descriptor and the client's magic check failed. On kern_mega
    the library sits on a 512-byte boundary of the image (up to 511 bytes
    of padding, kern_mega's image only), which gives it a code alias of its
    own, `(CS & ~7) + offset / 64`, and LIB answers that when CS is an alias,
    so the clients far-call it with no fault and read its magic through
    it (code is readable). Real mode keeps the paragraph arithmetic. kern_big's
    DESK.DRV and kernel are byte-identical. Under Bochs all three start
    and draw. This was found by the module census of 13.5.

**M4 - the SDK's code aliases (2026-10-18, KERN-MEGA-HANDOFF track 3).**
Every count below is from a throwaway kern_mega that writes one E9 line per
#GP in `mega_fault` (`T<error> <CS>:<IP>`, track 4's logger by hand), under
Bochs, old package against new on the same image and the same clicks.

42. **The selector to far call code in a claim is an SDK macro**,
    `OS88_CODESEL reg, <AH of OSAPI_CPU_INFO>` (os88api.inc): the segment
    itself on every real-mode kernel, `(S >> 2) | 4` under `CPU_F_PM`. It
    sits beside `OS88_HUGE_INCR` (0x1000, 3.2), which is a constant today
    and stops being one only when an LDT claim hands out consecutive
    selectors - an ABI decision, not an edit. It has no consumer yet: no
    code in `apps/cc/` or the parts standard steps a segment by exactly
    64KB (the carve walks in 32KB of paragraphs, `add ax, 0x800`), and
    paragraph arithmetic of that kind is what an LDT claim will need
    replacing anyway, not renaming. The parts standard gains
    `op_cseg`, opt-in with `OP_WANT_CSEG` (`OP_WANT_READ`'s reason: what
    decides it is a call no row sees), so no package that does not ask
    changed a byte. ABI lock: no bump, additions only.
43. **The C runtime far-calls its overlay through the alias.** `cc_entry`
    banks CPU_INFO's AH before anything binds; `cc_ovbind` stamps the
    module's vectors from `[cc_ovcs]` and `cc_ovthunk` returns through it,
    while `[cc_ovseg]` stays the data segment op_seg, op_drop and MEM_FREE
    want. Before: every resident->module call, module->resident return AND
    module->module call (they are far too, 73.14) was a #GP - 5 per round
    trip in `tests/covl` (25 for five SPACEs, 0 after, the gate's five
    numbers all right on the glass), 120 for CWORD's Format > Character
    (0 after, the dialog drawn), and **6,909 for opening `WELCOME.RTF`
    and typing three characters** - the RTF reader is overlay code calling
    itself and the resident half all the way down - against 0 after, the
    document rendered with its formats and the edit taken. LOOM's preview
    and WEAVE's canvas core,
    the two C packages that far-call a part themselves, bind the same way.
44. **The emulator cores wrote through CS, and that was the census's
    biggest number.** C64's and the Apple II's 6502 cores (one derived from
    the other) wrote the register file back through CS at every slice's
    exit - eight #GPs a slice - and loaded DS from CS before each C I/O
    handler, so every C store there trapped too: **57,778 and 57,168 #GPs**
    in a sweep's few seconds at READY and `]`. The exit now writes through
    ES (the package, from the run's own frame word), and the handlers take
    DS from a word banked at each door in from C. RunCPM's Z80 core never
    loaded ES, so ES is the package for the whole run and every static
    store - IX/IY halves, EXX, EX AF, IFF, IM, the exit - is `[es:x]`; its
    idle CCP was 248 #GPs. All three: **0** after, at the same prompt; the
    C64's status row read 391% -> 478% of a C64 (the logger's E9 output
    inflates the "before", so that ratio is an upper bound).
45. **kern_big byte-identical**, and so is every shipped `.o88`, `.drv` and
    floppy at one build number (OS88_BUILDNUM=1600, both trees): only
    c64, apple2, runcpm, cword, loom, weave and covl changed (and, with
    item 48, every C package by one byte). One prefix byte for another, so
    the real-mode cores run the same clocks: ZEXDOC (`make rcz80test`),
    `make a2cputest` and `make c64cputest` (Dormann's functional test and
    twelve rows each) all PASS on the converted cores. On a clean mega.img
    the sweep starts all seven C packages, Notepad and Paint, no FAULT.
46. **The speaker ring player cannot run on kern_mega yet, and that is a
    KERNEL gap, not the include's.** `os88spk_go` writes IRQ0's IVT cell
    (through segment 0, so `mega_null0` runs it) and reprograms channel 0
    to the sample rate after `OSAPI_FSX_SPK` opens. `mega_ivt_sync` builds
    gates from IRQ2 up - IRQ0 and IRQ1 are the kernel's own there - so the
    ISR is never entered and `sch_isr` would tick at the SAMPLE rate. Read,
    not run: no speaker play was tried under Bochs. The ISR's ~20 CS
    stores (`os88spk_rp`, `os88spk_left`, the `os88spk_isrm` toggle) are
    per sample, so converting them only pays once IRQ0 reaches the
    package. Until then `OSAPI_FSX_SPK` on kern_mega should refuse
    (`SPK_E_*`), which every caller already handles, and the conversion
    wants a PM-only ISR variant chosen in `os88spk_go`
    (`push ds / mov ds, [cs:banked]` per sample is what the real-mode
    ISRs were cycle-counted to avoid, SPEC.md 34.11). os88spkfx.inc and
    os88pcm.inc are the producer's side of the same play and wait with it.
47. **Left for the census (track 4)**: PiXEL's part calls (`px_pfar`,
    `px_wfar`) and PIXELSTEIN's per-column bodies (`px_qcur*`, `px_drvp`)
    are far calls through a part's DATA segment - the latter in the
    raycaster's column loop, and its relocation table adds a paragraph
    delta to those words, which is wrong for an alias (an alias moves by
    delta >> 2), so it wants `op_cseg` at bind AND the proc re-deriving
    rather than adding. Not swept here.
48. **No package region is movable on kern_mega, and it is one compare.**
    `OS88_REGION_MOVABLE` (and crt0's copy of it) hands
    `OSAPI_MEM_MOVABLE` `DX = CS`, which is the region's LDT CODE alias
    there; `mem_find_own` compares DX with `MC_SEG`, an identity segment
    whose bit 2 is always clear, so it can never match and the call refuses
    - silently, by design (`a refusal is not worth reporting`). READ OFF
    `mem_tab` under Bochs (`xp` at LOW_SEG:mem_tab, CWORD and Notepad
    started by MEGARUN): CWORD's region, owner 2, `MC_RLOC 0`; Notepad's,
    owner 3, `MC_RLOC 0` - while the claims each declared through a DATA
    segment (CWORD's overlay carve, Notepad's buffer) carry their procs.
    crt0 now passes `DX = DS`, the same word on every real-mode kernel, and
    the same read shows CWORD's region at `MC_RLOC 01D2` (`cc_regreloc`).
    The assembly macro is in every other package, already built and
    shipped, so **the fix that reaches them is the KERNEL's**: on kern_mega
    `osapi_mem_movable_x` (kernel/memory.inc, track 1's file) should map a
    DX with bit 2 set through `mega_cs2ds` before `mem_find_own` - the
    same conversion `mega_gpw` makes for a code selector in DS. Until then
    a kern_mega compaction can move no package region but a C one.
49. **WEAVE's VM and the two far-called modules: measured, not converted.**
    PONG.WAB double-clicked from C: costs 63 #GPs to load and 45 in the 30
    seconds after Serve. 38 of the 63 are two sites at the entry of the
    canvas core (`wcanvas.asm` `wsm_entry`, CS 061C): `mov [cs:wsm_cds], ds`
    and then the first store after `push cs / pop ds` - two per VERB call,
    the second because `mega_gpw` swaps the read-only DS for the data
    segment and the instruction runs again, so the rest of that call is
    free. LOOM's preview module (`lmpvmod.asm` `lpv_entry`) has the same
    two. The cure is the module's own DATA segment on entry, which the
    package has (it bound the vector from it) and the module cannot work
    out without CPU_F_PM - so it is a module ABI change (WSM_ABI, the
    preview's header, WEAVE-SPEC 1.2.2) for two #GPs a verb, and is left.
    The VM (`wvm.inc`, ~150 CS stores: DS is the VM claim and ES the
    bundle for the whole slice, so no register is free) stored per EVENT
    in this run, not per op - the remaining 25 load-time and 45 play-time
    #GPs are its - so its restructure waits on a bundle that shows a loop.
50. **What track 3 leaves outstanding**, in the order it would be taken:
    * **Kernel, not SDK** (track 1's files): `osapi_mem_movable_x` maps a
      DX with bit 2 set through `mega_cs2ds` (item 48) - one compare that
      makes every shipped assembly package's region movable on kern_mega.
      The SDK-side alternative, `mov dx, ds` in `OS88_REGION_MOVABLE`, is
      the same byte count but rebuilds ~61 packages and cannot reach the
      ones already on disks.
    * **Kernel, then SDK**: IRQ0 for a package's speaker ISR on kern_mega
      (item 46). First `OSAPI_FSX_SPK` should refuse there, so callers fall
      back; then the gate is routed, and `os88spk.inc` gains a PM-only ISR
      chosen in `os88spk_go`, with os88spkfx.inc and os88pcm.inc after it.
    * **The parts packages' own far calls** (item 47): PiXEL's `px_pfar` /
      `px_wfar` and PIXELSTEIN's per-column `px_qcur*` / `px_drvp*` to
      `op_cseg` (define `OP_WANT_CSEG`), and PIXELSTEIN's `px_reloc`
      re-deriving those words rather than adding a paragraph delta.
    * **`os88ui_gring`** (apps/os88ui.inc, the radio ring): eight CS stores
      and DS = CS per radio drawn, in ~20 packages, five drivers and
      CTRL.DRV. Not a loop, so not taken; the conversion stages the four
      points on the stack (DS = SS for the call), which has to be checked
      against every includer's stack class (SPEC.md 8.7) first.
    * **The two far-called modules' entries** (item 49): two #GPs a verb,
      a module ABI change (WSM_ABI and the preview's header), and the VM's
      ~150 CS stores if a bundle is found that stores per op.
    * **`OS88_HUGE_INCR`** (item 42) has no consumer; it stops being a
      constant only for a claim above 1MB with consecutive LDT selectors,
      which is an ABI decision.
    * **Not yet exercised**: an overlay or carve MOVED by a kern_mega
      compaction (`cc_ovreloc` / `cc_ovcarve` re-deriving `[cc_ovcs]`);
      RunCPM running a CP/M program and the Apple II booting a disk under
      Bochs (both only reached their prompts); LOOM packing on kern_mega.
    * **The Victory**: the M4 test disk (COVL, CWORD + WELCOME.RTF, C64,
      APPLE2, RUNCPM) on the 286. Everything above is Bochs, a 386 CPU.
    * **No suite row**: the #GP counts here were taken with a throwaway
      logger and scratch scripts. A row that holds them at 0 wants track
      4's trap-census knob (KERN-MEGA-HANDOFF track 4 item 1) first.

**Review of tracks 1 (to M3), 2 and 3, after their merges (2026-10-18).**
Read against the code at `4da2a7b0`. At `OS88_BUILDNUM=1600`, both trees
whole `make`s: kern_big's `KERNEL.SYS` is byte-identical across tracks 1 and
2 (the one system-disk difference being `HDDTOOL.DRV`, item 26's installer
change), and across track 3's merge `KERNEL.SYS`, all six 360KB images and
the 1.44MB pair are byte-identical, the fast tier 53/53; under Bochs PAINT, SHEET, TEXPAD, TRACKER, TASKMGR and NOTEPAD start on the
merged kern_mega. No defect was found in the code. What is NOT complete is
below, and it is the work list: item 50 stays track 3's own account, and
this repeats none of it beyond naming whose file each piece is in.

51. **Open after tracks 2 and 3**, by owner:
    * **Track 2 delivered its whole brief** - all five items of plan 4
      item 2 are either out of kern_mega (35, 36, 39) or measured and kept
      with a reason (37). Two loose ends, neither its file: `[mega_xmnat]`
      is written by `mega_xminit` and read by nothing since 36 (one byte of
      `.text` and a store, mega.inc - track 1's); and item 38's HOLE IS NOT
      CLOSED by item 32 alone. Moving the read-ahead store above 1MB frees
      its 32K beside the dead rung, so the run under the FAT window (5K,
      purgeable, PINNED, at 24E0 in 38's reading) grows to ~70K - but that
      window still walls it off from the 472K run above, so the largest
      run is unchanged until the FAT window too is claimed top-down or
      above 1MB (13.3). **Not yet re-read off `mem_tab`** after 32; the
      figures here are 38's plus 32's arithmetic.
    * **Track 3 delivered steps 1-3 of its brief and deferred step 4**
      (the speaker includes) on a kernel reason, item 46 - correctly: a
      converted ISR that IRQ0 never reaches converts nothing. Its two
      KERNEL follow-ups are in TRACK 1's files and neither is taken yet:
      `osapi_mem_movable_x` mapping a DX with bit 2 set through
      `mega_cs2ds` before `mem_find_own` (48 - every shipped assembly
      package's region stays pinned on kern_mega until then), and
      `osapi_fsx_spk` (kernel/snd.inc) refusing on kern_mega with an
      `SPK_E_*` until IRQ0 can be routed to a package (46).
    * **`OS88_CODESEL` is right only for an IDENTITY data segment, and it
      is now compiled into binaries.** `(S >> 2) | 4` is the LDT alias of a
      claim BELOW A000, the only place a claim is today. 13's direction is
      claims above 1MB behind `mega_ldt_alloc` selectors (item 30), and for
      one of those the shift names an unrelated entry - possibly one in the
      allocator's own range - with no fault to say so. The macro is in
      crt0 (every C package with an overlay), `op_cseg`, LOOM's preview and
      WEAVE's canvas core, so this is an ABI question to settle BEFORE a
      package claim can land above 1MB, not after: either such claims are
      never handed to a package that far-calls into them (the kernel keeps
      code claims below 1MB, 13.2's list grows a line), or the answer
      moves into the kernel - an `OSAPI_MEM_CODESEL`-shaped slot that
      returns a code selector for a claim, allocating an alias descriptor
      when the claim is not on the identity map - and the macro becomes its
      real-mode fast path. The second keeps the macro's binaries right on
      today's layout and is the one that does not paint 13 into a corner.
      `OS88_HUGE_INCR` (42) has the same shape and the same deadline.
    * **For the DOS round (track 6)**: the read-ahead store above 1MB (32)
      is a kernel-owned block (`MX_KOWN`) in the very pool the owner's
      answer 7 hands to the DOS program whole. The handoff must flush and
      free it first - `dsk_rah_redo` with the width at 0, or `osapi_dsk_cache`
      AL = 0 - and the return re-solve it, exactly as `mega_enter` does at
      boot. A store the DOS program overwrote and the kernel then SERVED
      would hand back another program's bytes as a disk's.
    * **The Victory has run none of 30-49.** The read-ahead landing (32) is
      the piece Bochs cannot stand for: on the Victory every C: transfer is
      a BIOS reflection through the PicoMEM's option ROM (13.2), which is
      exactly the path the landing exists for, and a C: to B: copy and
      compare there is its first real test. mega.img at `321e43c6` was
      sent for that.

    *(2026-10-22: closed below - region movability is item 64,
    `OSAPI_FSX_SPK`'s refusal item 65, `[mega_xmnat]` item 66, the FAT
    window's wall items 67 and 86, the `OS88_CODESEL` question item 72
    (`OSAPI_MEM_CODESEL`), the speaker includes 14.7 and 14.16.1, and
    `os88ui_gring`'s CS stores 14.18. Still as written: `OS88_HUGE_INCR`
    has no consumer, and WEAVE's canvas core and LOOM's preview still take
    two completed traps a verb (`mov [cs:wsm_cds], ds`,
    apps/weave/wcanvas.asm:89).)*

**Track 1, continued.**

52. **Modules run above 1MB** (13.3 step 2's second half; 13.5 is the
    design and what it found). `mod_need` loads and checks a module as it
    always did, and then, in protected mode, `mod_hi` copies the WHOLE claim
    - image and the bss past it - into a CODE/DATA PAIR from the allocator
    (`mega_ldt_alloc2`: code at entry n, data at n + 1, one mx block under
    them), re-arms every slot that named the old alias with the pair's code
    selector, and frees the conventional claim; `[MODR_SEG]` holds the code
    selector and its TI bit tells `mod_free_row` to give the pair and the
    block back. The data half is code + 8, and that is the whole of the new
    rule: `mega_dsimg` and `mega_csdata` (CSDO and PUSH_CSDATA) add 8 for a
    selector in the allocator's range, and `mega_cs2ds` (the CS-write and
    code-in-DS fixups) asks the descriptor's executable bit first. A module
    goes high only if the census clears it (`MOD_HIOK`): DOCK, EXTD, WIN95,
    DESK and STIKMENU. DESK.DRV's library, whose LIB verb answered a sum on
    CS, gets a code descriptor of its own over the library's bytes
    (`mega_dklibf`, freed with the image). Measured under Bochs: DESK.DRV
    5KB at 138000h with its library's descriptor at 138200h (all three
    Sticky Apps draw), WIN95.DRV 2KB (the Windows theme draws), DOCK.DRV
    3KB (the strip moves to the left edge), STIKMENU.DRV 16KB (the panel
    composes every card and picture); switching the theme and the dock back
    freed both pairs and both blocks, and switching again re-took the same
    block under the same selector. CTRL.DRV stayed conventional, as the
    census says it must. kern_big's kernel and all six images are
    byte-identical.
    **Two limits, both deliberate.** A module loaded at BOOT, in real mode,
    is not moved at `mega_enter`: DOCK.DRV and WIN95.DRV store their own CS
    into kernel vector tables when they hook (`dkv`, `thv`), so moving one
    after its hook would leave those far pointers on the freed copy - a
    module loaded in protected mode moves BEFORE its hook runs, which is why
    `mod_hi` sits between `mod_check` and the return. And CTRL.DRV stays
    low: its two file writes are the census's blockers, and the panel is
    held only while it is open, so 16KB for the length of a settings
    change is not worth a staging buffer in every write path it has.
    **Both limits are lifted by 14.47**: every module goes high, CTRL.DRV
    included and with no staging buffer (item 77 is the landing), and the
    boot's modules move at the switch, `mega_modvfixf` re-pointing `dkv`
    and `thv`.

53. **The kernel's blocks come from the TOP of the pool** (13.4.1).
    `mx_alloc` searches downward for owner FEh - the candidate ends at the
    pool's top and an overlap moves it to end where that block starts, so
    it strictly falls and terminates - and upward for everybody else, as it
    always did. Under Bochs the read-ahead store's base is FF8000h and
    `tests/megaxm` still allocates at 130000h; compaction above 1MB is
    refused for the two reasons 13.4.1 gives.

**The installer, after the merge - the Victory's install refusal.**

54. **`No system disk: A:k30 B:k5` on the Victory, and the line now shows
    the compare.** The owner's run of item 27's line, booted from
    `mega.img` with a kern_big install on C: (Slot 1, 503M): A: mounted and
    its root walked to the end - 30 entries, which is that root less its
    label (31 with a SYSTEM.CFG on it), KERNEL.SYS the first - and B:'s 5
    likewise, with no name matching on either. The fence passed (a package
    would have seen 7 of A:'s 30, the rest being hidden and system), so the
    DIRECTORY was read and the NAME COMPARE never agreed. Under Bochs the
    same install says Done, natively and with every floppy access forced
    through the BIOS (item 29), so the cause is the 286 or the PicoMEM.
    `hd_istat` fetches with `push cs / pop es`, so on kern_mega the
    kernel's `rep movsb` of the name into `hd_ifind` (dsk_find_x's
    `.staged`) is a WRITE through the LDT code alias: it faults, `mega_gpw`
    converts ES in the frame and the string op runs again, and
    `hd_iname_same` then reads the buffer through DS. By reading: the bases
    agree by construction (DS is `[hd_tseg]`, the alias is `[hd_tseg] >> 2
    | 4`), a reflection cannot nest inside `mega_svc` (every gate is an
    interrupt gate, and the real-mode half's IRQs are the ROM's own), and
    `mega_gpw` restarts a `rep movsb` exactly. So the leading suspect is the
    one thing Bochs's 386 cannot show: a 286 restarting a MOVS whose WRITE
    faulted with SI or DI already stepped, which puts the name one byte off
    in every record and changes no count. Not proven, so nothing is changed
    on its strength. The refusal now carries the experiment instead -
    `hd_isd_rec`, after a `k` entry, fills the record with `#`, fetches
    ordinal 0 again and shows four fields:
    `No system disk: A:k30 RRRRRR CCC DDDD WWW`
    * **R** (6) - fetched the walk's way, ES = CS, as DS reads it;
    * **C** (3) - that same buffer read through CS;
    * **D** (4) - ordinal 0 fetched again with ES = DS, so nothing faults;
    * **W** (3) - the wanted name through DS (`KER`).

    `#` is a byte nothing wrote, `?` one that is not printable. A healthy
    record reads `KERNEL KER KERN KER` (Bochs, with a TEST build seeking
    KERNEL.SYX: `A:k31 KERNEL KER KERN KER`). **R shifted - `ERNEL.` or
    `#KERNE` - with D right** is the fault restart, and the fix is to take
    the fault out of the path (HDDTOOL's fetches with ES = DS, or a write
    to ES:DI before the string op in `dsk_find_x`); **R `######` with C
    `KER`** is a write that landed at CS's base and not DS's; **R and C
    both `######`** is a write that went somewhere else entirely; and **R,
    D and W all right** puts it in the compare. The fields cost the line
    its next entry (B: no longer fits after A:), and it is the same
    question asked twice. HDDTOOL.DRV changes on kern_big too, as items 27
    and 28 did - the line is right on both kernels; HDD.DRV (HDTOOL_KB
    still 17) and kern_big's `kernel.sys` are byte-identical, two worktrees
    with `OS88_BUILDNUM` pinned and whole `make`s. The Bochs install (blank
    C:, Format, Install) still ends `Done - remove the floppy, Restart`.
54.1. **The answer: a real 286 restarts a faulted string write one element
    late.** The Victory's line read `A:k30 #ERNEL #ER KERN KER`. The fetch
    that faults (R, and C, the same buffer read through CS) never wrote its
    FIRST byte and wrote every later one IN PLACE, while the fetch that does
    not fault (D) is right - so when `mega_gpw` converted ES in the frame
    and ran `dsk_find_x`'s `rep movsb` again, the frame's SI and DI had
    already moved one element past the write that faulted. Bochs's 386
    restarts it exactly, which is why item 54's install passed there. It is
    not the installer's defect: EVERY string write a package makes through
    its own code alias or segment 0 - `push cs / pop es` then `rep stos` or
    `movs`, 146 such pairs across the shipped packages (2.4) - loses its
    first element on a 286, and whether CX moved too decides whether it also
    writes one past the end.
    **The fix measures rather than assumes.** Nothing is ever written by
    the faulting instruction - ES names a code segment (or is null) for the
    whole of it, so the FIRST write is the one that faults - so the right
    restart is the instruction's own starting registers, and the only
    unknown is how far the CPU moved them. `mega_strcal`, once at
    `mega_enter` with interrupts off, loads ES with `KERNEL_SEG` (the code
    selector) and faults a `rep movsb` and a plain `movsb` of its own;
    `mega_strcal_gp` records the frame's SI, DI, CX and IP and resumes at a
    LABEL after each (no instruction length assumed), and the advance -
    0 or 1 element each, anything else read as 0 - lands in `mega_sdr` /
    `mega_sdp`. `mega_strfix`, called from `mega_gpw` and `mega_null0`
    wherever they have just converted ES, re-reads the faulting instruction
    off the frame and, for MOVS and STOS only, takes that advance back off
    DI (and SI for MOVS), sized by the element and signed by DF, and off CX
    when there is a REP. Everything else returns at its first test, and a
    CPU that measured 0 returns before reading the instruction at all.
    The result goes to E9 as `kern_mega: string restart r<si><di><cx><ip>
    p<si><di><cx><ip>` - the IP digit being how far past the instruction's
    first byte the CPU put the saved IP, 0 being the prefix, reported and
    acted on by nothing. **Bochs reads `r0000 p0000`**, so the 386 path is
    unchanged. Bochs cannot produce a 286's advance, so the correction was
    exercised by POKING `mega_sdr`/`mega_sdp` to 1,1 in a running guest: the
    installer then got past the name compare and failed copying KERNEL.SYS
    - the over-correction a CPU that restarts exactly would suffer, and the
    proof that the path runs and moves the registers. `tests/cswrites.txt`
    allows mega.inc one more pair, the probe's deliberate `mov ax,
    KERNEL_SEG / mov es, ax`. kern_big is untouched (mega.inc is KERN_MEGA
    only). **On the Victory the install should now read `KERNEL KER KERN
    KER` and proceed**; if it does not, the E9 line is the next reading,
    and it needs a debug port this machine does not have - the photo of the
    installer line is the field instrument.
    **CONFIRMED ON THE VICTORY 286** (mega.img at `4ef5edf7`): the install
    over the existing kern_big C: started, completed, and the machine booted
    from C: to the desktop. The C: carried an earlier SYSTEM.CFG with
    ETHER.DRV wanted, and that came up too - The Wire opened and transferred
    files, the first field run of the network stack under protected mode -
    on the PicoMEM 1.x's emulated NE2000, the same card as its SB 2.0 and
    its BIOS-only C:.
54.2. **The DOS box on the Victory: "Not our handoff record".** Every DOS
    program the owner ran was refused at `hbm_dosrun`'s first step, the
    check that the record DOS.O88 posted begins `KDH1`. Bochs runs the same
    image to `exit code 042` and back at 16MB, at 4MB (the Victory's RAM,
    `OS88_BOCHS_MEGS=4`, new in `tools/os88bochs.py`) and at 4MB with
    SOUND.DRV, HDD.DRV and ETHER.DRV loaded, so it is not the heap and not
    the drivers. DOS.O88 posts from a data segment it has just written
    through, so the pointer is an identity segment the relocation table
    follows. What only the 286 does is RESTART A FAULTED STRING WRITE
    (54.1), and that copy was one: HIBER.DRV fetched the record with
    `push cs / pop es / rep movsb`, ES its own read-only alias. **It now
    copies through `PUSH_CSDATA`** - `push cs` on every other kernel, so
    kern_big's HIBER.DRV is unchanged - and the copy never faults. That is
    the source fix 54.1 called the fast path, and it does not explain why
    `mega_strfix` would get this copy wrong where it got the installer's
    right, so **the refusal now says what it saw**: `H ssss:oooo bbbbbbbb
    rrr`, the posted pointer, the first four bytes there read directly (a
    good record is `4B444831`) and `mega_strcal`'s REP MOVS figures for SI,
    DI and CX. Bochs, with the check forced to fail in a throwaway build:
    `H 9180:AE22 4B444831 000`. If the Victory now runs DOS, the restart
    path is still owed an answer for this shape, and hiber.inc's other 45
    CS writes and 10 pairs - which the park and the way back run through on
    kern_mega - are where it would bite next.
    **Confirmed on the Victory**: Test Drive III launched and came back
    (`exit code 000 (Arena: 586KB)`, `DOSP 0639K o 0171 b 0071`).
54.3. **...and the PS/2 KEYBOARD was dead after the return, the PS/2 mouse
    alive.** Bochs reproduces it exactly (its mouse is PS/2 too), and it is
    NOT kern_mega's: after the trip the PIC has IRQ1 unmasked with nothing
    pending, IDT gate 21h is `kbm_isr`, `kbm_old9` is `mega_k09`, and keys
    leave the BIOS buffer untouched - the 8042 is not raising IRQ1 at all.
    `hbm_wake`'s PS/2 restore (SPEC.md 96.45.2.2) rewrites the command byte
    with `mou_p2_init`'s PROBE-WINDOW value, `MOU_P2KOFF` (bit 4, the
    keyboard interface disabled) set, and never sent the `0xAE` that is
    `mou_p2_init`'s own last step. So every kernel's PS/2-mouse machine came
    back from a DOS run or a hibernate with a dead keyboard - kern_big's
    HIBER.DRV is fixed with it. Bochs after the fix: `ab` typed at the DOS
    window's prompt, the BIOS buffer 1Eh -> 22h. `soak -k mouresume -k
    kdreturn` green (the serial path, which this branch does not run). Beside
    it, kern_mega's park now BANKS the 8042 command byte and `mega_dwake`
    puts it back - the 3.3 ledger item - with `0xAE` and the keyboard's
    `0xF4`, so a DOS program that leaves the keyboard off on a machine
    WITHOUT a PS/2 mouse comes home typing too.

**M5 - the trap census (track 4, 2026-10-18).** Every #GP fixup is correct
and costs a fault, so a site in a loop is a slow program that looks like a
working one. The census counts them; it also found three things that are
not slow but WRONG.

55. **The instrument.** `make megatrap` builds kern_mega with `MEGATRAP=1`
    (`-DMEGA_TRAPLOG`) into `build/megatrap/`, so `build/mega.img` is never
    the census kernel by accident. `mega_fault` then writes one E9 line per
    #GP before the fixup runs: `T error CS:IP DS ES NAME`, NAME being the
    `O8` header's name at +16 of the image CS runs in (`-` for a C overlay
    or a Pixel part, which carry none). The shipped kern_mega and kern_big
    carry none of it (kern_big byte-identical, checked at one build
    number). `tools/os88bochs.py --build build/megatrap` boots it,
    `--linger N` keeps a sweep's boot running after MEGARUN so idle loops
    reach the log, and `tools/megatraps.py E9.OUT` ranks the sites, says
    which fixup took each (`xfer` mega_gpfix, `write` mega_gpw, `null0`)
    and resolves the IP against the image's own nasm listing, found by
    header name (`--lst NAME=PATH` for a part). The handoff's hand-made
    logger was this, once.
56. **Starting is quiet; using is not.** A startup sweep of all 48 shipped
    packages logged 829 lines, 760 of them isoTron's colour presenter, and
    a second sweep that left 23 likely loopers running a minute each logged
    29 (1942's `n_pushall`, once per call). Idle loops, attract modes and
    animations trap nothing. The cost is in what a program DOES: a WAV, a
    module, a picture. So a census is an exercise with the census kernel
    (`os88bochsui.py`), not a sweep. The other startup sites, for the
    record: `os88ui_gring` stores eight corner points through CS per radio
    button drawn (`apps/os88ui.inc`, shared, not a loop).
57. **SOUND.DRV: one trap per DMA block, gone.** `sbl_isr` and the IRQ
    discovery stubs loaded DS with `push cs / pop ds`; on kern_mega that is
    the read-only code alias, so the first write of every block trapped.
    A 9 s WAV 38 -> 0; Tracker on BEVERLY.MOD ~106 a second -> 0.
    **CORRECTED at review (2026-10-18):** the first fix BANKED DS in
    `sbl_dseg` at the hook, on the ground that `mem_can_move`'s IVT scan
    pins a hooked image - which SPEC.md 66.6.3.1 replaced with a PATCH:
    `mem_region_reloc` moves a hooked, idle image and rewrites the IRQ
    vectors to follow it, and nothing tells the driver, so after any such
    move the next block's ISR loaded the OLD base - freed memory or the next
    claim's - on kern_big as much as kern_mega (`tests/sndmove.py` is that
    scenario and stays green only because it never plays after the move).
    The ISRs now DERIVE DS from CS at the interrupt (`SBL_DSCS`): CS itself
    on every real-mode kernel, `(CS & ~7) << 2` for kern_mega's LDT alias of
    an image below A000h, which is where a driver image always is (only
    kernel modules go above 1MB, `mod_hi`). No bank, nothing to go stale.
    **That is the pattern for any driver ISR**, and a banked segment is the
    one to refuse.
58. **isoTron: 760 -> 0.** `it_praw4p`'s per-row words (strip row, two
    strides, last logical row) were written through CS because DS is the
    work claim in that loop. They live in the claim's unused bytes after
    the strip now; plain stores, a byte and a prefix cheaper on an 8088 too.
59. **A segment built by PARAGRAPH ARITHMETIC is not a segment here, and
    can be worse than a fault.** Only a multiple of 8 paragraphs is an
    identity selector (3.1). Any other value either faults (RPL bits set:
    Video, `vp_khdr`'s `mov ds, dx` with DX = 138Bh, the moment a .V88 is
    opened) or - bit 2 set - loads an LDT CODE ALIAS at base index x 512,
    readable, so reads come from the wrong memory with no fault at all; and
    a WRITE through it reaches `mega_gpw`, whose `mega_cs2ds` turns the
    alias into data segment `(S & ~7) << 2` and completes the write THERE.
    That is silent corruption of whatever lives at that address. Found in
    PiXEL (the work claim's sub-buffers, the master's row address and a
    flat file's trailing palette, all rounded to one paragraph - fixed, 8
    now), in Paint (`pt_srowset`, a staged BMP's rows: opening SAMPLE.BMP
    faulted on 4E41h - fixed, and a document session in TexPad, Word, Sheet
    and Paint is then 28 traps in all) and in the Video Player, which
    normalises to one paragraph at about fifteen sites in `video.asm`
    (`vp_khdr`'s callers, `vp_rptr`, `vp_raud`, the ring and layer paths) and
    so CANNOT PLAY A FILE ON KERN_MEGA TODAY. Video is not converted here: it
    wants its own session and the `vid*` rows behind it. Two things follow
    for the owner: an SDK rule (normalise to 8 paragraphs, `and si, 127`,
    never 15), and a kernel guard - `mega_cs2ds` should refuse an LDT alias
    that names no loaded image rather than redirect a write. Neither is this
    track's file.
60. **`mega_gpw` stores the WRONG SEGMENT for `mov [cs:x], ds`.** It runs a
    CS: store in its trampoline with DS swapped to the image's data
    segment, so a store whose SOURCE is DS writes that segment and not the
    caller's. PiXEL's PNG part built its service table that way, far-called
    its own claim and ran wild on HOUSE.PNG. The parts store through AX now
    (`pxpart.inc` rule 5). Still in the tree: `apps/os88spk.inc:496` (track
    3's include), `apps/loom/lmpvmod.asm:130` and `apps/weave/wcanvas.asm:89`
    (neither on mega.img yet). The kernel fix is one case in the
    trampoline - `8C /r` with reg = DS must store the saved DS - and it is
    the right place, because nothing else can know.
61. **PiXEL opens every format on kern_mega now** - LAKE.JPG and FLOWER.JPG
    (JPEG, grey and colour), HOUSE.PNG, CAT.GIF (interlaced) - where LAKE.JPG
    faulted on the decoder scratch and HOUSE.PNG ran wild. LAKE.JPG's traps
    13,853 -> 533 (the IDCT's `pj_u` and the Huffman fill's `pj_hi` moved into
    the decoder scratch; the viewer's DDA loops stop on `cmp di, [cs:px_cend]`
    instead of decrementing a counter through CS). What is left, ranked, for
    the next census session: the JPEG colour path's SELF-MODIFYING `imul`
    displacements (three sites at 14,400 each for FLOWER.JPG - an 8088 speed
    design, so its replacement wants MartyPC's numbers beside it); GIF's
    LZW and scaler state words (`PS_*`, ~3,000 a site); PNG's inflate state
    (~11,000 a picture); and two per-row costs, `px_ks_emit`'s `push cs / pop
    ds` service entry and the part's far call into the package through the
    package's DATA segment (one `xfer` a row - the context would have to
    carry the package's code selector, which is track 3's helper).
62. **For the Victory.** `build/mega.img` from this branch in A:, and in B:
    the census exercise disk - Video, PiXEL, Paint, TexPad, Word, Sheet,
    Audio, Tracker and Note Pad in APPS, and in MEDIA the four sample
    pictures, a WAV, BEVERLY.MOD, the logo .V88 and a document for each
    editor (`os88disk.py` over `build/*.o88`, sent beside this branch at
    1.44MB and 1.2MB). Tracker and Audio as before: the SB ISR no longer
    traps, so the `RFL` line should not move and the CPU it leaves should.
    PiXEL should open all four pictures, which it could not. The Video
    Player will fault on the .V88 (item 59) and is expected to.

63. **What track 4 leaves outstanding**, in the order it would be taken:
    * **Kernel, one case in `mega_gpw`'s trampoline** (item 60, `kernel/
      mega.inc`): a CS: store whose SOURCE is DS (`8C /r`, reg = 3) must
      store the DS the code had, not the image's data segment the trampoline
      swapped in. Until it does, three source sites store the wrong segment
      on kern_mega: `apps/os88spk.inc:496` (`os88spk_rp+2`, the speaker
      include track 3 owns), `apps/loom/lmpvmod.asm:130` and
      `apps/weave/wcanvas.asm:89` (neither on mega.img yet). Each is also
      fixable at the source through AX, PiXEL's `pxpart.inc` rule 5.
      **Reviewed 2026-10-18: the fix as written is half of it** - the
      trampoline also runs with CS = KERNEL_SEG, so `8C /r` with reg = CS
      (1) stores 0060h instead of the image's code selector
      (`apps/os88spk.inc:247` and `:511`, `mov [cs:os88spk_rp+2], cs`).
      Store the saved DS (`[mega_tds]`) for reg 3 and the saved CS
      (`[mega_tret+2]`) for reg 1. It is disjoint from item 54.1's
      `mega_strfix`, which is on the no-prefix arm.
    * **Kernel, a guard in `mega_cs2ds`** (item 59): refuse an LDT alias
      that names no loaded image instead of turning a write through it into
      a write at `(S & ~7) << 2`. It costs a heap-table walk on a path that
      is already a fault, and it turns silent corruption into a report.
    * **The Video Player cannot play a file on kern_mega** (item 59). The
      one-paragraph normalisations in `apps/video/video.asm`: the key reads
      before `vp_khdr` (the `.part` loop and the `.kp` reload), `vp_rptr`,
      `vp_raud`, a landed block's `add [vp_rdseg], ax`, the step-over-a-
      record pair (`add si, ax` then normalise, twice), the `VC_PO`
      advance, the timed `.ld` path, the layer record's `mov dx, es`, the
      audio staging's `and si, 15`, the super-packet chunk address and the
      EMS page frame's. Two need more than `and si, 127` / `and al, 0xF8`:
      the walk that compares DX:SI against an end held as a paragraph and
      an offset (BP:DI) must normalise both the same way, and `vp_xmk`'s DX
      is an XMS hold handle, so check it is never loaded as a segment
      before touching it. The bar is the `vid*` soak rows on kern_big and a
      .V88 playing under Bochs with the census kernel.
    * **An SDK rule and a ratchet** (track 3's `os88api.inc`): normalise a
      far pointer to EIGHT paragraphs, never one. The scan that found
      PiXEL's and Paint's - an `and r, 15` within a few lines of a
      `shr ax, cl` or `or ax, dx` feeding `mov es`/`mov ds` - is the shape
      a `tests/cswrites.txt`-style ratchet would take. Today it finds
      Video's and three PiXEL sites the census exercise never reached:
      `pxgif.asm`'s two master-row addresses on the ANIMATION path (`.r`
      and `.c1`, the same fix as `px_mrow`), and `pxedit.asm`'s Rotate
      column walk (`pe_srow`'s caller at `.cw`), which steps the SEGMENT by
      `pe_q` paragraphs a row and is therefore off the grid by design - it
      wants an offset walk (SI += the width a row) that moves the segment
      only by multiples of 8 paragraphs when the offset nears 64KB, not a
      mask.
    * **PiXEL's decoder state through CS** (item 61), each a per-part
      conversion with `pxbench`'s ceilings beside it: the JPEG colour path's
      SELF-MODIFYING `imul` displacements (`pj_blk`, 3 x 14,400 traps on
      FLOWER.JPG - a speed design for the 8088, so the replacement is
      measured on MartyPC first); PNG's inflate words (`pz_bias`, `pz_bsb`,
      `pz_rd`, `pz_ft` - ~11,000 a picture); GIF's LZW and scaler `PS_*`
      words (~3,000 a site); and one trap a row in the part services'
      `push cs / pop ds` entries (`px_ks_emit`, `px_ks_scat`, `px_c2p`). The
      part's far call into the package (one `xfer` a row) is track 3's item
      50 and `OS88_CODESEL` (item 42) is now the way to make it.
    * **Not yet censused**: the games played with keys (Tank, Skies,
      WireBall, PIXELSTEIN, 1942 past its title), MIDIRack playing, the
      DOS box, Browser and FTPD (`--ne2k`), and the C packages that are not
      on mega.img (CWORD, WEAVE, LOOM, RUNCPM, C64, APPLE2, PACCMAN). The
      method is item 56's: the census kernel, a B: disk with the program
      and its data, and the program USED.
    * **Red on `origin/protected-mode` too, found while verifying, not
      track 4's**: `pxbenchvga`'s BIG.BMP "first rows on the glass" reads
      4.62 s against a 3.90 s ceiling, 2 of 2 runs on the base; `picomem`'s
      two "channel 1 RACING" checks and its `khx` leg, whose `kslice.inc`
      ends inside an `%if`.
    * **The Victory has not run item 62's images yet.**

    *(2026-10-22: most of this list is closed further down. The trampoline's
    `8C /3` and `8C /1` (kernel/mega.inc:3562) and the alias guard
    `mega_cs2dsw` (:3626) are 14.39; the Video Player plays on kern_mega
    since 14.16; the eight-paragraph rule's ratchet is
    `tests/unit/t_megagrid.py` (apps/ only - 14.69's drivers are outside
    it); PiXEL's writes through CS are 14.40; and most of the uncensused
    programs were run in 14.46.1 (the games, the DOS box, Browser, FTPD,
    the Apple II, LOOM), 14.46.5 (CWORD) and 14.52.3 (PaccMan). Left of it:
    Lemmings has never run on kern_mega (tests/megahigh.txt), no census
    here records RunCPM running a CP/M program or the C64 past its prompt,
    and nothing here re-reads `pxbenchvga` or `picomem`.)*

**Track 1, closing item 51's kernel follow-ups.**

64. **Package regions are movable on kern_mega** (item 48). `osapi_mem_
    movable_x` maps a DX with bit 2 set - the region's LDT code alias,
    which is what `OS88_REGION_MOVABLE`'s `DX = CS` hands it there - to its
    segment before the fence, `(DX & ~7) << 2`, mega_cs2ds's rule, so every
    shipped assembly package's declaration lands with no package rebuilt.
    THE MOVE ITSELF WAS NEVER RUN before, and is now: `tests/heapfrag`
    under Bochs, `heapcheck.py`'s scenario by hand (PAINT then HEAPFRAG,
    Paint closed, R). Before the change the region suite reads R1-R4
    `0 0 1 0` - R3, *my region moved up*, FAILS, the region at 95C0 with
    `MC_RLOC 0`; after it, `0 0 0 0`, the region moved 95C0 -> 9DC0 into
    Paint's hole with its proc recorded, its three 50K claims re-owned by
    the new segment, and the package's own menu then opened and Close ran
    from the new home with no fault. The data-claim suite passes on
    both, every row it draws reading PASS.
65. **`OSAPI_FSX_SPK` refuses on kern_mega in protected mode** (item 46),
    with `SPK_E_NOTRATE` - kern_small's answer to the byte, so every
    caller's existing fallback is the path. IRQ0 is the kernel's gate
    there and never reaches a package's ISR; until it can be routed, a
    speaker ring would play nothing and channel 0 at the sample rate would
    run `sch_isr` at it.
66. **`[mega_xmnat]` is gone** - written by `mega_xminit`, read by nothing
    since item 36.
67. **The FAT windows the boot claimed no longer wall the floor** (item 38,
    and item 51's second bullet). Read off `mem_tab` under Bochs at the
    desktop: C:'s window (`MEM_P_FATW` + 2, 5K, purgeable, pinned) stood at
    2540 on a 72K hole - the dead rung and the read-ahead's freed 32K under
    it - and the largest run was **481K of 567K**. `mega_enter` now gives
    back every FAT window that is a claim, clean and not the live one
    (`dsk_fatw_lowf`, `dsk_fatw_drop` per volume, nothing at all if a FAT
    sector is dirty), and the volume's next mount takes it again first-fit
    from the floor: **558K** at the desktop, and **541K** after opening C:,
    whose window came back at 13A0, three claims above the floor. The
    remaining walls are the read-ahead's 5K landing (pinned, at the floor
    itself) and whatever the session claims.
    **For the DOS round (item 51's fourth bullet)**: the module pairs of
    item 52 are kernel-owned blocks in the same pool as the read-ahead
    store, so the handoff must drop the held modules (`mod_drop` - DESK.DRV
    and WIN95.DRV are held for as long as their feature is on) as well as
    the store before the pool is handed over, and the return re-take them
    on demand as it does today.
68. **M6 W3, the XMS door, is built on `kern_dos`'s side** (SPEC.md
    96.15.4, docs/plans/KERN-MEGA-DOS-PLAN.md 6). Under `-DKD_MEGA`,
    `kerndos/kdxms.inc` replaces the four refusing doors with a first-fit
    block table over the arena the launch block's tail names (`KDL_XBASE`,
    1KB-aligned and at or above 1MB, and `KDL_XKB`; no zeroing needed, cut at
    16MB), moves through `int 15h AH=87h` by the ROM's saved vector, and
    hooks `int 15h` for the program: `AH=88h` answers 0, and `AH=87h` with an
    end above 1MB outside the arena is refused with CF = 1, AH = 03h. The
    core's refusal of an extended-to-extended move is lifted under
    `-DDOS_MEGA` (+67 bytes, `DI = 2` with a linear source in `ES:SI`).
    **Measured** at the foot of `kerndos/kdos.asm`: `kern_dos` 33,585 ->
    34,611 bytes of image plus bss (842 code, 98 bss, 6 of calls, 80 of
    `CORE_MAX` hole), so kern_mega's `KD_IMG_KB` is 34 (35 with `DOSRMARK=1`)
    and the program pays a kilobyte; kern_mega's `DOS.O88` 47,610 -> 48,375
    bytes on disk. The plan's estimate was ~500. kern_big's `kernel.bin`,
    `hiber.drv`, `kerndos.bin`, the three other parts and `DOS.O88` are
    byte-identical at build 1600. `make build/XMSPROBE.COM` is the probe and
    its negative control (a raw `AH=87h` at linear 130000h); it has not run
    yet - the kernel's half (the arena claim in `hbm_dosrun`, the park) is
    another lane's, and Bochs is that lane's too. **Since run** (item 69):
    exit code 048 through the park, the control refused.
69. **M6 W0-W2: the DOS box parks the session above 1MB and comes back**
    (docs/plans/KERN-MEGA-DOS-PLAN.md 1.1, 2.5, 10.1; SPEC.md 96.40.9).
    Before any change Bochs confirmed both faults the plan predicted, at
    other instructions than it named: the windowed box faults in its own
    segment arithmetic (`mov es, ax` with 1A72h, off the 8-paragraph grid),
    and arm 3 in HDD.DRV's PIO write of `HIBERNAT.IMG` through
    `MEGA_SEL_LOW0`'s 4FFh limit. **And Bochs will not execute out of video
    memory at all** (`prefetch: getHostMemAddr vetoed`), so nothing of the
    park runs there: `mega_dropr` runs the loader stage from a copy under
    the top of the park, and kern_dos enters the park stub IN PLACE above
    1MB on the GDT the kernel left in the park's header. The snapshot is
    taken at `hbm_wrimg`'s moment and returns twice; the sum is one word per
    64-byte line, distributed; a mismatch restarts with a sentence naming
    the program; a small pool parks what fits and lowers `KDL_CAP`. Two
    `DOS.O88`s: `mega.img` carries a `-DDOS_MEGA`/`-DKD_MEGA` build, which
    refuses off kern_mega, and kern_mega's loader refuses kern_big's by the
    `'KMEG'` mark it lacks. **Measured** in Bochs: KDHELLO round trip,
    586 KB arena, `DOSP 0639K o 0018 b 0004`; three of four break-on-purpose
    writes caught (the fourth on an unsampled word, the accepted trade); ten
    trips with no `mx_tab` growth; a WAV through the SB16 before and after;
    a 192K park gives the program 130 KB and comes back. kern_mega `.text`
    **+2,094**, `.cold` +88, HIBER.DRV +642 of content inside its padding,
    kern_dos 35,002 (`KD_IMG_KB` 35 on kern_mega only). kern_big byte-
    identical. **The 286's way down (W2, `mega_r286`'s `[mega_drop]` branch)
    is built and has not run anywhere**: it is the Victory's.
70. **M6 W4 and W6: kern_dos in the HMA, option-ROM lines, and the keyboard**
    (docs/plans/KERN-MEGA-DOS-PLAN.md 10.2). kern_mega's kern_dos runs at
    FFFF:0010 with low trampolines that re-prove A20 on every vector; the
    arena is **621 KB** (586 before); `LOADFIX <program>` at the box's prompt
    is the off switch; `A20SHUT.COM` passes and goes red without the
    trampolines. The drop opens every option ROM's IRQ line for kern_dos and
    the wake primes it (unverified: the Victory's). And no key reached the
    desktop after a trip on a PS/2 machine, because `hbm_wake` writes the
    8042 command byte with the keyboard disabled and never sends `0xAE`;
    kern_mega sent it; **kern_big's live resume had the same defect**, and
    54.3 (b793f294) has since fixed it on every kernel, so the kern_mega-only
    copy went at the merge.
71. **M6 W5: hibernate on kern_mega in protected mode, and a resume that
    never leaves it** (docs/plans/KERN-MEGA-DOS-PLAN.md 10.3, SPEC.md 87.9).
    The image carries an extended section (the GDT, LDT and IDT, and every
    `mx_tab` block), and the fresh kernel reads the whole file into a block
    above 1MB and runs the PARK stub over it, so a 286 spends no reset on a
    resume. Three hibernate/resume rounds in a row under Bochs with a
    package holding 64KB of XMS: the 64KB equal every time, no fault, and a
    byte flipped in the section on the host is refused. Item 13 is
    withdrawn: `Hibernate (PM)` is gone. Two kernel defects found on the
    way, both fixed: **a code alias in ES across a 386 reflection** made
    every BIOS store through ES fault in real mode for ever (a 386 keeps a
    segment's attributes across the switch; `mega_svc` now loads ES from DS
    before clearing PE), and **`mega_n0` set in the image** (the picture is
    read one stepped word at a time through a null segment) made every later
    null fixup refuse. kern_big's 186 built artefacts are byte-identical
    across all of M6.

*(72-74 were written as 68-70 on a parallel track and renumbered at the
merge; their commits cite the old numbers.)*

72. **`OSAPI_XMEM_SEL` (0x0499) and `OSAPI_MEM_CODESEL` (0x049F) are in
    the table** *(renumbered at the elendilon merge of 2026-10-19, which
    brought `OSAPI_WM_ONWHEEL` in at 0x0499: kern_mega's four cells are now
    XMEM_SEL 0x049F, CODESEL 0x04A5, XMEM_WALK 0x04AB and MEM_HUGE 0x04B1,
    `osapi_table_end` 0x04B7 - none of them had shipped, so it is a
    pre-ship renumber and not a moved cell)* - the owner's decision, 2026-10-18, on 12.2's proposal and
    item 51's third bullet. SPEC.md 41.16 is the contract. Both are rare
    cells appended at the end (1,173 bytes, `osapi_table_end` 0x04A5), so no
    cell moved and `abilock` took the change without a `PKG_FMT` bump. XMEM_
    SEL hangs ONE selector off each pool block (`MX_SEL`, a word the entry
    had spare), re-aimed by a second call and freed by `mx_free` and
    `mx_relall`; CODESEL answers the segment itself in real mode and, under
    `CPU_F_PM`, the image alias, a code selector as it is, or a pair's code
    half for its data half - so `OS88_CODESEL` is its fast path and a claim
    above 1MB, the day a package has one, is answered rather than
    mis-shifted. The pool's table grew to `MX_NBLK` = 24 entries on kern_mega
    alone: the kernel's own blocks (the store, the module pairs) would
    otherwise have left a package two of `XM_MAX_BLKS`' eight. Cost:
    kern_big `.text` +26 (the cells and two bodies, the refusal written six
    bytes so every hot routine after it keeps its parity - `t_hotpar`),
    kern_small +27 with `OSAPI_XMEM_SEL` as its `xm_copy` (no body), no rung
    crossed on either. `tests/megaxm` is the gate (the block read and
    written in place, the re-aim, a window past the block refused 2, the
    alias for its own segment and the data selector refused): `sel 280C
    code 2754`, `ok` under Bochs - and `bad 0D` with `mega_codesel`
    amputated to answer DX unchanged, so the row can fail.

73. **The DOS park's region list follows the pool's table** (M6, merged
    beside item 68). `megados.inc` sized it `XM_MAX_BLKS + 10` because it
    records every pool block, and item 68 made kern_mega's table `MX_NBLK`
    = 24 entries - and a full list DROPS a region silently
    (`mega_dreg_add`'s `.full`), so a block of the kernel's own could have
    gone uncovered by the park's sum. `MPK_RMAX` is `MX_NBLK + 10` now (34
    regions, 0x518 of the header's 0x800). Checked under Bochs on the merged
    tree: KDHELLO.COM from C: through the park - `parked 027FK at 130000`,
    577 KB above its PSP, `exit code 042 (Arena: 586KB)` and `unparked`,
    with the read-ahead store's selector (2804h, FF8000h) intact after it
    and the pool holding the store alone, the park and arena rows freed.

74. **The DOS park takes the read-ahead store's KB** (KERN-MEGA-DOS-PLAN
    10.2's review: on a 1MB AT, ~192KB of pool less a 32KB store can put the
    park under `MPK_MIN_KB` and refuse the box). `mega_dosclaimf` first calls
    `dsk_rah_dropf`, which is `dsk_rah_redo` with the width ceiling held at
    0 for the one call - the store and its landing given back, the solve it
    falls into refusing at the floor, `[dsk_rah_cap]` put back as it was -
    and the next mount after the return takes the store again above 1MB.
    Bochs, KDHELLO.COM through the park: the park took the store's freed row
    0, and after the return and a B: mount the store was row 0 again at the
    pool's top under the same selector (2804h, FF8000h). The module pairs
    (DESK.DRV 5KB, WIN95.DRV 2KB while their features are on) stay summed
    and held; they are what a 1MB machine has left to argue about.
    Item 68 also answers the review's `mx_tab has 8 rows`: it is 24.

75. **M6 W7: a DOS session survives Ctrl-Alt-Del** (KERN-MEGA-DOS-PLAN
    10.3.1, SPEC.md 96.40.9). An anchor in GDT entry 0 names the park while
    it stands; the boot blob reads it before `drv_boot` - kern_mega writes
    nothing above 1MB until `mega_enter` - clears it, and enters the park
    stub as kern_dos would. Bochs: KDHELLO back after a reset, and a damaged
    park refused into an ordinary boot. `.text` +60, `.boot2` +494.
76. **M6's review answered, and the park is the runs in use**
    (KERN-MEGA-DOS-PLAN 10.4, 10.6). The park banks and restores the table
    bytes its sum's lines missed, checks its header against kern_dos's
    launch block and the build's constants, sums its own list, clears W7's
    anchor when it refuses, and puts the MSW back; the XMS door has 0Ch-0Fh,
    08h's largest block and 0Bh's handle bounds; `int 15h` E801h and its
    kin are refused; a W7 recovery tells the box why its program ended. And
    the park is `[0, mem_base)` plus the claims, a bit a KB: **196KB where it
    was 639KB**, the arena still 621KB - the 1MB AT's case.

**Packages above 1MB (13.3 step 4), wave P1: the file layer takes a buffer
there.**

77. **A selector above 1MB is a file buffer now.** It is the first step
    of packages-high, because every package's file I/O goes through it.
    It is also testable without moving any package: `OSAPI_XMEM_SEL`
    already hands out exactly such a selector. Two layers, as the survey
    found:
    * **The upper layer** (`dskw_*`, kernel/diskw.inc). The two doors ask
      `dskw_hiseg`. Where `mega_s2seg` answers FFFFh it keeps the caller's
      selector in `[dskw_seg]` and sets `[dskw_hi]`, and the body then
      moves the OFFSET and never the segment:
      - `dskw_norm` does nothing;
      - `dskw_runmax` answers 0, so every sector goes through `dskw_one`
        and the scratch;
      - `dskw_xclus`'s stage adds 512 to `[dskw_src]`.

      The compressed read keeps R as an offset inside the one window and
      refuses a stream that would cross it (U of 64KB or more, R past a
      word). `dskw_czexp` takes the selector as it is.
    * **The lower layer** (`dsk_xfer`, kernel/disk.inc).
      - `dsk_runcap` answers 0 for any LDT selector, which puts every BIOS
        volume on the existing `dsk_secbuf` bounce. That covers native
        floppy and reflected `int 13h` alike, and a CODE ALIAS used as a
        buffer, whose page arithmetic was wrong before.
      - A driver volume whose buffer is an LDT selector gets `.drvhi`, a
        sector a call through `dsk_secbuf`. HDD.DRV's rung 0 would be
        refused 09h by `mega_svc`'s fence otherwise.
    * **lz's source slide** (`lz_at`, +800h to DS) goes through
      `mega_slidef` for an LDT selector. That re-bases `MEGA_SEL_SLD`,
      GDT FFE0h, the identity of a ROM segment nothing loads. It shares
      `MEGA_SEL_ODD`'s caveat: one scratch per machine.

    **Measured** under Bochs at 4MB with `tests/megaxm`'s new file leg, off
    HDD.DRV's PIO volume and again off the BIOS boot partition
    (`--bootc`). Each run:
    - writes 40,000 bytes straight from a 64KB window above 1MB;
    - reads them back into a second window at offset 1234h with
      `FILE_READ`;
    - reads 16KB with `READ_AT`;
    - expands a 241-byte packed file into 50,000 bytes in place. Its R is
      49,759, so the decoder slides at once.

    **`MEGAXM: file C350` / `ok` on both.** kern_big's KERNEL.SYS is
    byte-identical.

    Two bugs the gate caught on the way:
    - A byte placed between `dskw_src` and `dskw_seg` broke
      `lds si, [dskw_src]` everywhere, and C: never mounted. There is an
      assertion now.
    - The high arm STORED R over the caller's offset where it must ADD it.

    **Not covered: the redirected volumes** (a RAM disk, a cable share).
    Their FSV verbs take the buffer in DX and RAMDISK.DRV normalises it,
    so a high buffer there is wrong. That is a refusal to build before a
    package placed high can run off one, and it is item 78's to settle.
    `OSAPI_XMEM_FREE` of a block whose selector the caller still holds in
    ES faults at the cell's own `pop es`. This is the SDK's "reload the
    register" rule; the test now obeys it.

78. **A package that says `OS88_HIGH` runs above 1MB** (wave P2).
    **The opt-in** is bit 7 of the header's stack-class byte (decision 14.1).
    `inst_pkg_spawn` drops it by shifting the index left first, which costs
    no byte on any kernel; `os88pkg.py` masks it before its class check;
    `abilock` took it as additive.
    **The move.** `ld_hi`, at the head of `ld_start` so the disk path and
    `OSAPI_PKG_START` share it, finds the image in an ordinary conventional
    claim with ES on it. `mega_pkghif` copies the whole region into a
    code/data pair from the pool. The block's owner is 80h | slot, which
    `mx_relall` and the cells match exactly and so never touch. The
    conventional claim goes back, and `[ld_base]` - and with it `I_SPTR`
    and `W_SEG` - becomes the pair's DATA selector. There is no room or no
    entry? It stays low, which is not a refusal.
    **The kernel's arms**, each one test of bit 2 (an LDT selector, so a
    pair's data half whose code is 8 below):
    - `MEGA_CSELTOP` and so `MEGA_PCALL` (the entry proc, `wm_pkgcall`'s
      every callback, `sch_rhook`, `drv_pkg_disp`);
    - `mega_frame_cs` (a worker's frame);
    - `mem_reloc_call`'s package arm;
    - `mega_gpfix` (a far transfer through its own DS, its code half checked
      by `mega_codesel`);
    - `mem_own`, which takes the region being loaded (`[ld_base]`) or a live
      instance's `I_SPTR`, since a pool block has no `mem_tab` record and
      the entry proc claims before step 9 publishes it.

    `mem_free_rec_x` and `ld_unreserve` give the pair and its block back.
    **Measured** under Bochs at 4MB with `tests/megahi` (OS88_HIGH,
    `OS88_STACK_192`), launched twice:
    - `ds 2814 caps 0B1B` both times. That is 5KB under the pool's 0B20,
      and the same KB the second time, so the teardown gave it back.
    - `claim ok` (an `OSAPI_MEM_CLAIM` from the entry proc, written, read
      and freed), `file ok` (its own .o88 into its bss), `tick` (a timer
      callback), `worker` (a spawned worker), `tick`, `ok` (the window
      destroyed with the worker alive in `OSAPI_TASK_ALIVE`).

    The first build's own worker RETURNED, which a worker must never do:
    there is nothing under its frame. The `ret` executed the package's
    header and ORed 47h into its own code. That was the test's bug, and
    the instrument is the record of the rule.
    **What it does not do yet**: no shipped package says OS88_HIGH. The
    census of which can (no arithmetic on their own CS/DS, no `OS88_CODESEL`
    of their own DS, no IRQ vector stamped with their own CS, no re-home,
    no RAM disk or cable share for their own buffers) is item 79's.
    `mega_idt_gate` and `mega_ivt_sync` would mis-take an IRQ vector a
    high package stamped with its own CS; the census keeps such packages
    low rather than teaching those two.

79. **The census, and 24 packages say `OS88_HIGH`.** Three read-only
    agents walked every shipped assembly package's own code and every
    `apps/*.inc` against item 78's rules: no arithmetic on its own CS/DS,
    no `OS88_CODESEL` of its own DS, no linear address of its own segment,
    no IRQ vector stamped with its own CS, no DMA from its own image, no
    re-home.
    **Opted in**: Hello, Timer, Mines, Font Viewer, Calculator, Solitaire,
    Piano, Fractal, Chart, the Task Manager, Note Pad, TexPad, Paint,
    Sheet, The Wire, Telnet, Browser, FTPD, Recorder, Frotz, ArtfulType,
    Audio, Tracker and MIDIRack, plus ModPlug (an on-demand build).
    **Kept low:**
    - Word and Scribe. Each re-homes into a parts carve (`OS88_REHOME`),
      and lifting only its loader would re-home a pool region, an untested
      path. A high-aware `OSAPI_PKG_REHOME` is what would lift them.
    - The three Sticky Apps, deliberately rather than of necessity. Their
      library calls back through each app's own DS, which costs a
      `mega_gpfix` trap per callback. That is correct but unmeasured on a
      286, and their windows redraw often.
    - Everything else, unreviewed: the fullscreen games (which hook IRQs
      and rate hooks), the C packages (crt0's `OS88_CODESEL` is applied to
      claims, but its overlay binding was not walked), and DOS.O88, PiXEL
      and Skies, which re-home.

    **The includes that are NOT clean**, so a package using them stays low:
    - `os88la`, `os88ub` and `os88xgap`: a linear address of its own DS.
    - `os88spk`: its own CS into IRQ0's vector.

      Audio, Tracker and MIDIRack use `os88spk`, and are in only because
      `OSAPI_FSX_SPK` refuses on kern_mega in protected mode
      (`kernel/snd.inc`). Should that cell ever be granted, they must come
      back out first.
    - `os88parts` where it re-homes.

    The census also found one kernel nit, fixed: `osapi_mem_movable_x`
    decoded a pair's code selector as a paragraph (`(DX & F8h) << 2`). It
    now refuses a pool pair outright, which is the right answer for a
    region that never moves.
    Two latent bugs it found, both off today:
    - Audio's `AP_HANDOFF` build compares CS with `SSI_SEG`, which is the
      DATA selector for a high instance.
    - Every package's own file I/O on a RAM disk or a cable share is not
      covered (item 77).

    **Claims stay conventional.** Paint's row table and several others do
    paragraph arithmetic on their CLAIMS, so `OSAPI_MEM_CLAIM` handing a
    high package a selector would need a second, separate opt-in. That is
    decision 14.8.

80. **The Task Manager: RAM is RAM.** Under `CPU_F_PM` (`[tm_pm]`, asked
    once at launch), memory above 1MB is memory a program runs in, not a
    store.
    - The performance page's RAM figure is the whole machine, in MB to one
      place so it keeps `tm_kpair`'s eight characters: `RAM 0.3/3.6M` on a
      4MB Bochs. That is 640KB plus the 3,040KB above 1MB; 640KB-1MB is
      video and ROM.
    - The memory page's pool line reads `HIGH used/sizeK`. It counts the
      kernel's 192KB of tables and `.cold` under the pool's base, worked out
      from `XMEM_CAPS`'s base rather than assumed.
    - A package placed high reads `HIGH` in ADDR and is left off the
      conventional map, which drew it at its selector's paragraph before.
    - Its HEAP column adds the pool blocks it holds through the XMEM cells.

    That last needs to know whose a pool block is. `OSAPI_XMEM_WALK`
    (0x04AB since item 72's renumber, SPEC.md 41.16) answers it: a rare cell at the table's end,
    `abilock` additive. On every other kernel the cell is
    `osapi_xmem_sel`'s refusal, so it costs them the 6-byte cell and no
    body.
    **Measured** under Bochs at 4MB with Calculator, Mines and the Task
    Manager all loaded high:
    - `HIGH 242/3072K`: 192 kernel, 32 the read-ahead store, 18 the three
      regions.
    - Their three rows say `HIGH` with 6K, 2K and 10K.
    - The System row and the buffer rows are unchanged.

    The small build has no memory view and is untouched.
    **The page found a bug in item 78's loader, now fixed.** The heap page
    showed `TaskMgr Data 9C40 10K HELD` for a package whose region was
    high: its CONVENTIONAL region, never freed. `ld_hi` asked `ld_slot`,
    which answers off DI, the instance record, and at `ld_start`'s head DI
    is not the record. So the free was refused for the wrong owner, and the
    pool block was stamped 80h | garbage, which the teardown's free by the
    real slot would then miss.
    After the fix, `mem_tab` read off the running machine holds no region
    claim for either high package. MEGAHI twice reads the same pool KB.

81. **A package that says `OS88_HIGHMEM` claims above 1MB** (wave P3,
    decision 14.12). Bit 6 of the class byte, beside `OS88_HIGH`'s bit 7;
    `inst_pkg_spawn` masks both with `and bx, 3Fh` (a `nop` keeps
    `ui_task` on its parity) and `os88pkg.py` the same.
    - **The claim.** `mmf_osapi_mem_claim` and `_claim_hi` ask
      `mega_claimhf` first. Under protected mode, for a package whose
      header says so, 1..64KB is a pool block owned 0A0h | slot and a data
      selector over exactly those KB, kept in the block's `MX_SEL` so
      `mx_free` takes both. Anything else - over 64KB, no room, no LDT
      entry, a driver, a package that did not ask - falls to the
      conventional heap as before. `OSAPI_MEM_FREE` routes a pool selector
      to `mega_claimfreef`; the instance's teardown frees the slot's claims
      with its XMEM blocks (`mx_relinst`).
    - **The resize.** `OSAPI_MEM_REGROW` on one stays in the pool while it
      can and keeps DX: a shrink is the block's length and the descriptor's
      limit; a grow is a new block, a scratch selector for the copy, and
      the two blocks trading selectors so the old one goes back with the
      scratch. Past 64KB, or with no room up there, the claim MOVES DOWN -
      a conventional claim, the bytes copied, the pool block freed - which
      is a path-3 regrow and answers a new DX exactly as one does.
    - **The services, censused.** An agent walked every `OSAPI_*` body
      against a pool selector in ES or DX. Fixed:
      - `OSAPI_XMEM_COPY` turned ES into ES x 16 - below 1MB for every LDT
        selector, so a copy to or from a high claim (or an `OS88_HIGH`
        package's own segment, since wave P2) silently moved CONVENTIONAL
        memory nobody named. It reads the descriptor now, and `mx_copy`
        takes a conventional end above 1MB when it is inside the caller's
        own region or claims.
      - `OSAPI_DECOMP` refuses more than 64KB of output into a pool
        selector, and `lz_dbump` leaves one where it is instead of bumping
        it onto 0FFF0h.
      - `OSAPI_PKG_REHOME` and an external sound ring (`SND_OPENF_EXT`)
        refuse one.
      - **A redirected volume** takes one now: `drv_fs_hi` moves a
        transfer verb 512 bytes a call through `dsk_secbuf` (READ and
        READAT as READATs, WRITE as a WRITE and APPENDs), because RAMDISK
        and NET do paragraph arithmetic on DX. That closes item 77's open
        end and decision 14.4.
      - `mega_gpfix`: a `pop es` or `pop ds` of a FREED allocator selector
        loads the null one. Real mode lets a program free the claim its ES
        names and carry on; the cell's own `pop es` was a #GP.
    - **The Task Manager** counts 0A0h | slot in a package's HEAP column,
      and `OSAPI_XMEM_WALK` documents the owner.

    **Measured** under Bochs at 4MB with `tests/megahi` (OS88_HIGHMEM),
    twice: `claim 281C ok`, `regrow 281C 18E0 ok` (8KB in the pool with the
    selector and both test words kept, then 80KB moved down to segment
    18E0), `claim file ok` (an 8KB claim filled by `OSAPI_FILE_READ` and
    left held), and `caps 0B1B` on both runs, so the teardown gives the
    held claim back. The first build's grow left the scratch selector in
    ES across `mx_free`, whose own `pop es` then faulted - the bug the
    gpfix arm above now also forgives in a package.
    **Opted in**: the twelve claims-safe packages that claim at all - Note
    Pad, TexPad, Sheet, Fractal, Chart, The Wire, Telnet, Browser, FTPD,
    Recorder, ArtfulType and Audio. A second census walked every claim
    each one makes for paragraph arithmetic and found none; three of them
    regrow and four declare claims movable (refused for a pool claim and
    ignored by all four, the claim never moving). **Kept low**: Paint (its
    row table is paragraph arithmetic), Frotz, Tracker and MIDIRack, and
    the eight `OS88_HIGH` packages that make no claim of their own.
    **Swept** under Bochs at 4MB: all twelve open and draw (Sheet, TexPad,
    Fractal's Mandelbrot, The Wire's no-card page among them). With Sheet,
    Fractal, Note Pad and the Task Manager up, the Task Manager reads
    `RAM 0.5/3.6M` and `HEAP 7/561K` - 7KB of the conventional heap in use
    with four programs loaded, Sheet's row reading 54K - where before this
    wave Sheet's claims alone were conventional.

82. **A driver that says `OS88_DRV_HIGHOK` runs above 1MB** (wave P4).
    Bit 7 of the header's +15, the DSV table length, which every kernel
    already clamps to its own table and kern_big now masks (decision 16).
    `drivers/os88drv.inc` sets it when the driver `%define`s
    `OS88_DRV_HIGHOK` before the include.
    - **At load.** `drv_hi`, right after `drv_bss` in `drv_load_at` - the
      image checked and zeroed and nothing of it run - copies image and bss
      into a pool block owned `MX_KOWN` under a code/data pair
      (`mega_imghif`, `mega_pkghif`'s body with the owner as an argument)
      and calls `mem_region_reloc` with the old segment and the pair's DATA
      half. That is the compactor's own walk (SPEC.md 66.6.3), so
      `drv_tab`, the class fast paths, `ss_row`, `drv_blkseg`, the claim
      owners and the IRQ vectors - and through `mega_ivt_syncf` the IDT -
      all follow without a list of their own. The claim is freed and
      `DRVR_KB` reads 0, as an HMA driver's does.
    - **At boot.** SYSTEM.CFG's drivers load in real mode, before
      `mega_enter`, so `drv_hiboot_f` runs once right after it and moves
      each loaded row that says so and that `mem_can_move` would let move
      (no frame in it, no service task), interrupts off for the copy and
      the re-pointing.
    - **The kernel's arms**: `drv_release` gives a high image's block and
      pair back (`mega_drvhifree`); `mem_own` asks `drv_owns_seg_x` for a
      selector no package owns, so a high driver's claims are its own;
      `osapi_vol_fence` maps a pair's code half to its data half
      (`mega_cs2dsf`); `mega_idt_gate` takes a pair from the IVT - the code
      half as it is, the data half (what the move re-pointed a vector to)
      as its code twin.
    - **The drivers' arms**: SOUND.DRV's `SBL_DSCS` derives DS as CS + 8
      for a pair; HDD.DRV's `hd_raw` bounces its own buffer when its image
      is a selector, as it already did in the HMA.
    **Opted in**: SOUND, HDD, ETHER, NET and EMS, and the SAVER and THMPAL
    overlays. **Kept low**: RAMDISK.DRV (its seed folds offsets into its
    own CS, `rd_seed_one`), USBMOUSE.DRV (the PicoMEM's ROM calls its
    `int 33h` hook in real mode) and HDDTOOL.DRV and RAMPAGE.DRV, which
    are a resident's second images and not `drv_load`'s.
    **Measured** under Bochs at 4MB with an SB16, an NE2000 and the ATA
    disk, SYSTEM.CFG wanting sound, the hard disk and the card:
    - `kern_mega: above 1MB: Sound`, `Hard Drive`, `ether` on E9 at boot.
    - SBTEST's click plays its two-second 1 kHz tone into `sb.wav`
      (352,256 bytes, all of it signal) - DMA, the IRQ through the moved
      vector, the refill - and again after the Control Panel unticked and
      re-ticked Sound, the reload placing it high from `drv_hi`.
    - Telnet to a server on the host (10.0.2.2:2323) connects and shows its
      banner through the moved ETHER.DRV.
    - MEGAXM's file leg (40,000 bytes written from a window above 1MB to
      C:, read back, READ_AT, the 50,000-byte expansion) passes through the
      moved HDD.DRV.
    - The Task Manager's heap page, with Sound and ETHER moved and before
      HDD.DRV was opted in, lists one driver image on the conventional
      map: HDD.DRV's 4K.
    **Not measured here**: a hibernate with a driver placed high (measured
    with item 83: it passes), and the Victory's PicoMEM, whose SB "DMA" is its ROM reading the ring in
    real mode - the ring is a conventional claim, so it is unchanged.

83. **A driver that says `OS88_DRV_HICLAIM` claims above 1MB** (bit 6 of
    +15 beside item 82's bit 7; `drv_publish` masks `3Fh`). A census of every
    driver claim found one worth it: ETHER.DRV's socket pool, the 6, 11 or
    17KB ladder `tcp.inc` claims top-down, which is only ever loaded into a
    segment register and whose `sk_reloc` is one store. SOUND.DRV's pools
    feed the 8237 or the PicoMEM's ROM through `sbl_ring_phys`'s page
    arithmetic, and HDD.DRV's claims are BIOS buffers or HDDTOOL's code, so
    neither opts in.
    - `mega_ownerof` answers the pool owner for a caller: 0A0h | slot for an
      `OS88_HIGHMEM` package, **0C0h | row** for an `OS88_DRV_HICLAIM`
      driver (`drv_row_ixf`), and every claim, free, find and regrow arm of
      item 81 now asks it.
    - `drv_release` frees the row's pool claims with its image
      (`mx_relallf`).
    - **The boot's claims are moved, not re-made**: ETHER's attach claimed
      its pool in real mode, so `drv_hiboot_f` calls `mem_hiclaims` after
      moving the image - every claim the driver holds that it declared
      movable, with no DMA head and of 64KB or less, is copied into a pool
      block under a selector with interrupts off, its relocation proc told
      through `PKG_DISP` (BX the old base, DX the selector), and the
      conventional claim freed.
    **Measured** under Bochs at 4MB with SOUND, HDD and ETHER at boot: the
    Task Manager's heap page reads `HELD 2K (1)` - one package's data claim
    - where before item 82 it read `HELD 23K (3)` (HDD.DRV's image and
    ETHER's pool among them). Telnet to the host server connects, shows its
    banner and sends a typed line through the moved pool.
    **The hibernate round trip passes with drivers high**: hibernate at
    16MB, the 4MB boot refuses with `No room above 1MB`, the 16MB boot
    resumes - the About box witness intact and SOUND and ETHER placed high
    again by their reload.

84. **The kernel's caches above 1MB** - what decision 13 deferred, taken
    because "RAM is RAM" is about the whole machine. A read-only census of
    every kernel claim on a running desktop sorted them:
    - **High now**, by tag (`mem_hitag`), at the top of `mem_claim_x` under
      `[ss:mega_pm]`, 63KB or less, falling back to conventional on any
      refusal: the window save-unders (`MEM_P_WSAVE + slot`, up to 63KB
      each), the menu save-under (`MEM_P_MSAVE`), the icon store
      (`MEM_P_ICO`), the multi-file list (`MEM_P_FLIST`) and the clipboard
      (`MEM_K_CLIP`). Every use of each is a segment-register load with
      offsets inside it, through `gfx_save`/`gfx_restore`, `rep movs` and
      the icon walk. `mem_free_x` gives a selector with one of those tags to
      `mega_hifreef`. A pool block has no `mem_tab` record, so it is never
      shed, moved or counted, which is right: shedding it would free no
      conventional memory.
    - **The boot's**, claimed in real mode: `mem_hicaches_f`, right after
      `drv_hiboot_f`, DROPS the save-unders (the next raise re-takes each
      high) and MOVES the icon store (the listings hold references into it).
    - **Stay conventional**: `MEM_P_RAHB`, the read-ahead's landing (it IS
      the bounce; the Task Manager names it `RdLand` now, where it showed as
      `FE03`); `MEM_K_COPY` and `MEM_K_CLONE` (64KB DMA-page transfers that
      a selector would serialize to a sector a call); `MEM_K_CMPR`
      (paragraph arithmetic off one base); `MEM_K_HIB` (the resume stub);
      `MEM_K_BAND` and `MEM_K_SC` (boot-time, 1-2KB, and SC regrows).
    - **Not yet: the FAT windows** (`MEM_P_FATW`, 5KB a volume). Feasible,
      but a selector window would load nine sectors one bounce at a time;
      the right shape is to load and flush through the 9-sector landing
      under `sch_lock`, which is its own change.
    **Measured** under Bochs at 4MB with SOUND, HDD and ETHER at boot and
    Note Pad, the Task Manager and the Disk window up: the heap page's
    System row reads **10K** - the landing and one FAT window - where it
    read 45K to 49K with the save-unders and icons in it; HELD 2K, PURGE
    10K. Raises redraw from their save-unders, a menu drops and restores,
    icons draw from the moved store, and Note Pad's Select All / Copy /
    Paste twice through the clipboard gives `hellohellohello`.
    The first build's KB-to-bytes conversion clobbered its own count
    (`mov cl, 10` over the KB in CX), so a 30KB save-under got a 1KB block
    behind a 64KB descriptor and overwrote the package region above it: a
    `#UD` at the region's dispatcher on the first raise.
    **The package sweep on this kernel** (items 81-84 together): 23 of the
    24 `OS88_HIGH` packages open and draw under Bochs at 4MB; Solitaire's
    file was missed by the sweep's list and passed in item 79's.

85. **A buffer above 1MB moves a RUN at a time, not a sector** - decision
    3's upgrade, taken now that 24 packages, their claims and the kernel's
    caches are all such buffers.
    - **The lower layer** (`dsk_xfer`'s `.bounce`): a BIOS volume's transfer
      into an LDT selector bounces up to nine sectors - the run the track
      allows - through the read-ahead's LANDING (`[dsk_rah_bnc]`, nine
      page-safe sectors that are scratch between fills) where it bounced one
      through `dsk_secbuf`. `[dsk_bnc_n]` is the run; `.bcp` copies it and
      `.unbounce` advances by it; a run of one keeps the `dsk_secbuf` path;
      and the read-ahead's gate refuses while a bounce is live, because a
      fill would land in the landing the bounce is using. No landing (shed,
      or never claimed) is the old path exactly.
    - **The upper layer** (`dskw_runmax`, `dskw_flushrun`): a high buffer's
      run is bounded by the OFFSET's 64KB wrap instead of being 0, and a
      spent run advances `[dskw_src]` by its bytes instead of the segment -
      so a file read or write into one coalesces exactly as a conventional
      one does and reaches the bounce as runs.
    **Measured** under Bochs at 4MB with MEGAXM run off B:, a 1.2MB floppy
    on the native 765 driver: its whole file leg passes (40,000 bytes
    written from a window above 1MB, read back into another at offset
    1234h, READ_AT, the expansion) with the writes and reads going as runs
    of 9, 5, 9, 2, 9, 8, 9, 3, 7, 7, 5, 4... sectors - one call per run where
    the old path made one per sector, which on a floppy is one revolution
    each. It passes again off HDD.DRV's volume. **Two bugs on the way**: a
    run that came out as ONE sector went to the landing while `.bcp` copied
    `dsk_secbuf` (`MEGAXM: at 3200 / bad 29`, READ_AT's sector 25), so one
    sector keeps the old path; and the cache-gate guard above.
    **RAMDISK.DRV says `OS88_DRV_HIGHOK` now** (decision 18 withdrawn for
    it): its two paragraph folds of `[rd_bufs]` are one routine, `rd_fold`,
    which leaves a pool selector's offset unfolded - the only such buffer is
    the seed's, read out of its own image. It loads and attaches above 1MB
    (`kern_mega: above 1MB: Ram Disk`); its volume, which mounts only once a
    size is set, was not exercised.

86. **The FAT windows above 1MB** - item 84's one deferral, which item 85
    made cheap: a selector window is nine sectors and loads and flushes as
    ONE run through the landing. `mem_hitag` takes the `MEM_P_FATW` range;
    `dsk_fatw_want` skips the conventional `mem_avail_lvl` gate under
    `[ss:mega_pm]` (the claim is a pool block, so the heap's figure is not
    its gate); an assertion keeps `FAT_SEG`, the pin every window is
    compared with, from ever reading as a pool selector. The pin itself and
    the boot's live window stay where they are, and `dsk_fatw_lowf` already
    drops the boot's other windows at `mega_enter`, so the next mount of
    each re-claims it - high now.
    **Measured** under Bochs at 4MB, B: a 1.2MB floppy mounted and MEGAXM's
    write, read-back and delete run off it (the FAT dirtied and flushed
    through a high window): `MEGAXM: ok`, and the Task Manager's heap page
    shows the kernel's conventional claims as **5K - the read-ahead landing
    alone**, which is the bounce and has to be conventional.

87. **The second census: 29 more packages say `OS88_HIGH`.** A read-only
    agent walked every assembly package item 79 left unreviewed and the C
    SDK, against item 78's rules plus three facts it checked in the kernel
    first: an `FSXF_RATE` hook is called through `MEGA_PCALL` (`sch_rhook`),
    so a pair is fine; `mega_gpw` decodes a pair's code half, so a `[cs:]`
    write or `push cs / pop ds` traps the same high as low (a low package's
    CS is already its LDT alias); and a re-home is refused only for a pool
    DESTINATION, so a high loader would hand its pool region to step 8a's
    free, the untested path item 79 named.
    - **Opted in, assembly (22)**: 1942, Arkanoid, Bounce, Gorillas, Missile,
      Pac-Man, TameGram, Cyclone, Dot Delirium, Dr Marco, Excitebike,
      isoTron, Tank, WireBall, the three Sticky Apps (Sticky Note, Desk
      Calendar, Desk Clock), FPTEST, IMGTEST, RLAPPLY, WIRE and PiXEL.
      `OS88_HIGHMEM` as well for the five whose claims are only ever loaded
      into a segment register: Dot Delirium, isoTron, Sticky Note, Desk
      Clock and IMGTEST.
    - **Opted in, C (all 7)**: crt0 does no arithmetic on its own segments,
      `cc_ovbind` puts the CODE half in the overlay's call-back vectors, and
      `OS88_CODESEL` is applied only to the overlay claim, which stays
      conventional. `CC_STACK_CLASS` carries the bit (Apple II, C64 and LOOM
      say it alone, keeping the default class). None says `OS88_HIGHMEM`:
      the overlay claim takes `OS88_CODESEL`'s shift, and RunCPM and LOOM do
      paragraph arithmetic on their claims.
    - **Kept low (7)**: Redline (`int 13h` and `int 15h E820` into its own
      DS), the Video Player (`os88xgap`, `os88ub`, `os88la` and `os88spk`),
      and the five re-homers - PIXELSTEIN 3D, Skies, DOS.O88's loader,
      Scribe and Word. (Item 79 named PiXEL among the re-homers; it is not
      one - the re-home was PIXELSTEIN's.)
    - **Decision 9 is withdrawn**: the Sticky Apps' library far-calls
      through the app's DS once a callback, which traps through `mega_gpfix`
      exactly as it does for a LOW package (`.gpok` there, `.pairq` here),
      so placing them high costs nothing the low placement did not.
    **Swept** under Bochs at 4MB, all 29 plus Solitaire: every one opens and
    draws - the fullscreen games, the Sticky Apps, PiXEL and the seven C
    packages (CWORD, the C64, the Apple II, RunCPM, WEAVE, LOOM, PaccMan)
    among them. RLAPPLY reports `refused 04`, which is its design: it
    applies the chipset setting, shows a toast and answers CF = 1 with no
    window, so the loader frees it - a high region freed at the entry proc's
    own refusal, which the sweep had not met before.
    **kern_big's side of items 81-87** - the class and DSV masks, HDD.DRV's
    bounce test, RAMDISK's `rd_fold`, the Task Manager - checked with the
    rows those edits can reach, one scoped soak: `sndmove drvmove drvcall
    drvup hdtake pkgrun pkgfence regapp heapcheck rdmove rdup fsclass tmload
    tmsmall lzdrv`, **15/15 ok**.
88. **Item 82 put a 286 into a boot loop, and Bochs could not have seen
    it.** Reported off 86Box's mr286 (16 MHz 286, OTI-067, SB16, NE1000,
    IDE): the loading screen, at most one frame of desktop, then POST
    again, for ever. Reproduced on the same 86Box profile and bisected:
    item 81's tree boots, item 82's (`4b840530`) loops. The fault was found
    with a locally patched 86Box that prints every exception and every
    shutdown reset to stderr: a `#GP` with error `FFE8` at
    `0028:mega_idt_gate`, the `mov es, si` of the high-image arm loading
    `MEGA_SEL_LDTD`, while CS was still the bootstrap's `MEGA_SEL_BCODE`.
    `mega_enter`'s FIRST IDT build runs on the six-entry bootstrap GDT, which
    has no entry `FFE8`; the arm is reached by any IVT segment of 2800h or
    more with bit 2 set, and this machine's ROM has one, so the `#GP` had
    nowhere to go and shut the CPU down. **Two defects in seven lines**:
    - the arm ran before anything could be above 1MB, so it now refuses
      while `[k_dsel]` still says real mode, and also refuses a FREE LDT entry
      (a real segment that only looks like a pair) rather than calling it a
      data half;
    - it also sent every 512-aligned heap segment at 2800h or above - every
      conventional driver image but the lowest - to the ROM path, where item
      81's code had given it its LDT alias. Bit 2 decides now: clear means a
      real-mode segment and the alias rule; set means a pair, looked up.
    A Bochs CPU is a 386, but that is not why it missed this: Bochs' BIOS
    simply has no such vector. **Verified on the mr286**: the fixed kernel
    reaches the desktop and stays there, B: and APPS open through the
    keyboard mouse, and CALC.O88 (`OS88_HIGH`) opens and draws above 1MB. The
    86Box patch is two `fprintf`s (`x86_doabrt` and `softresetx86` in
    `src/cpu/`); the BIOS's own triple faults at `F000:C3D2` are its memory
    test and block moves and are filtered out. **It is a tool now**:
    `tools/86box/` builds that 86Box with upstream's automation socket and
    `tools/os88box.py` drives it headless - `launch 286-mega` is this
    machine, `faults` is this diagnosis (docs/86BOX.md). The same run read
    the CALC check through it: `12*12` typed, 144 on the glass.
89. **The DOS park after packages-high, and CP/M's CALL 5 on every host**
    (KERN-MEGA-DOS-PLAN 10.7 and 10.5.1 item 3). P4 put driver images above
    1MB, and `hbm_down`'s `drv_shutdown_x` after the park's snapshot then
    freed their selectors and rewrote their state in place, so every DOS trip
    was refused as "wrote over the parked session". The park route tears
    nothing down after its snapshot now, and the tables' accessed bits are
    set before the sum. CALL 5 reaches `dos_cpm` on kern_big's windowed box
    and under kern_dos too (48 bytes of the core on both hosts),
    `tests/call5.py` the row.

### 10.2 The Victory 286 check (M1's first field run)

**What it is for:** the two things no emulator here can say - whether the
286's way back to real mode (CMOS shutdown 0Ah, 40:67, triple fault) works
on a real AT, and whether anything on the desktop's path stores through a
code segment (QEMU lets it; a 286 does not).

**Disks:** A: is `build/mega.img` (1.44MB, `make mega`); B: is the ordinary
1.2MB apps disk, `build/apps120.img`. No SYSTEM.CFG ships on mega.img, but
that does NOT mean no driver loads: the boot sniffs an OPL2 and a BIOS fixed
disk (SPEC.md 51.3.2), so on the Victory the PicoMEM's Sound Blaster and its
C: bring SOUND.DRV and HDD.DRV in (the network stays out). The first run
froze there, in real mode (10.1 item 7); the second disk carries the fix.

**Steps, and what each outcome means:**

1. **Boot.** The loading screen and the desktop are kern_big's, in real
   mode. Then the menu bar says **`protected, 286 reset`** for eight seconds.
   * No toast, but a black-on-white line under the menu bar: the switch
     faulted. Photograph the line - it is vector, error code, CS:IP and SP.
   * No toast and no line (a hang or a reboot): the switch itself failed,
     before the reporter existed.
   * The toast says **`real mode: ...`**: mega_enter refused, and the rest
     of the toast is why. The desktop is kern_big's and works; photograph it.
2. **Double-click C:** (HDD.DRV's volume, through the converted block call)
   and then **move the mouse**.
3. **Double-click B:.** The first read of a drive the boot did not mount, so
   it is a reflected `int 13h` - on a 286, **a CPU reset per call**. A
   listing means the reset path works. A hang or a spontaneous reboot here
   means it does not. **Then move the mouse**: a pointer that stops after a
   disk read is 10.1 item 9 coming back.
4. **Open APPS, double-click CALC.O88, type `12*12` and Enter** (144), then
   **close it and open FRACTAL.O88** (its worker draws the set).
5. **Expected to FAULT on a 286 and not on QEMU:** the Sticky Apps menu, the
   Control Panel and anything else that loads a module (KERN-MEGA-PLAN 10.1
   item 6) - each writes its own image through CS. A photographed fault line
   from one of those is a result, not a failure of the run.

**On the fifth disk, after A:**, the steps above stand, and the Sticky Apps
menu and the Control Panel fault as listed (Bochs has shown the first).

**Not on this run:** pressing Play in the Audio Player or anything else that
streams digital sound through the Sound Blaster - its IRQ discovery is the
known fault of 10.1 item 11. So does the speaker's SAMPLE player, which
takes IRQ0 through the IVT (spk_on). A beep or a tune through the FM chip is
fine to try.

**Timing is not the question yet**: the run answers "does it work", and one
photograph per outcome is the whole report.

**What is NOT done, and is M1's remaining work:**

* **The 286's way back is BUILT AND UNTESTED.** A 386 clears CR0.PE; a 286
  sets CMOS shutdown code 0Ah, points 40:67 at mega_r286 and triple-faults,
  and the way back into protected mode reloads GDTR, IDTR and LDTR on both
  CPUs (which QEMU does exercise, through the 386 arm). QEMU cannot test the
  286 arm - its CPU is a 386, and its reset resets the whole machine - so it
  is the Victory 286's first check.
* **The reported fault line on the glass, and on a 286.** The reporter
  draws through font_run with the gfx state forced clear; a fault inside
  font_run comes back once and then leaves only the E9 line.
* **The modules' CS writes** (above), and the drivers that point DS at their
  own image (tests/cswrites.txt's last block - Sound Blaster, NE2000 packet
  shim, LapLink, CH375, the HDD installer).
* **A package with a stack of its own** drawing unaligned text: font_run's
  kern_mega state is SS-relative and assumes LOW_SEG.
* **Interrupts during a reflected call** are the BIOS's alone: the mouse and
  every driver IRQ are masked for its length, so the pointer stops for a
  floppy transfer as it did before SPEC.md 7.4. M2's native floppy driver is
  what gives that back.
* **The modules stay on-demand** and the kernel stays in conventional
  memory: section 4's prize is M3's. It needs
no package changes, reaches a desktop with a third of the catalogue, and
shows what the sweep really costs in a shared file.

---

## 11. Open questions

1. ~~The floor~~ — **286, decided** (section 8).
2. **Minimum extended memory.** kern_mega needs the GDT (64KB) plus the
   kernel above 1MB. A 1MB 286 (384KB extended) holds today's kern_big-sized
   kernel. A 512KB kernel needs a ~1.6MB machine. Is a kern_mega machine
   *required* to have that, or does the kernel spill into conventional
   memory below some size?
3. **Is a bimodal ISR set ever wanted?** It would only be needed to keep the
   pointer and sound alive through a reflected ROM-only disk transfer. The
   recommendation is no: such disks refuse, or freeze the pointer, on
   kern_mega.
4. **MAME as the 286 bench**: whether it boots this tree's floppies and can
   be driven like `os88marty`.
5. **Whether a kern_mega machine still wants the 360KB system disk.** A 286
   has a 1.2MB or 1.44MB drive. The geometry set could shrink to two.

*(2026-10-22: none of these is open any more. 2 - no minimum beyond
fitting (14.1 answer 16): under 192KB of extended memory the tables take
82KB of conventional memory, `mega_tlowf` (kernel/mega.inc:7464, 14.17).
3 - the pointer moves through a reflected call without a bimodal ISR set
(14.9). 4 - superseded: Bochs (docs/BOCHS.md) and 86Box with its automation
socket (docs/86BOX.md) are the 286/386 benches. 5 - it shrank to two:
`make mega` builds `mega.img` (1.44MB) and `mega120.img` (14.11), and no
720KB or 360KB kern_mega disk.)*

---

## 12. After the waves: protected mode is discoverable, and XMS becomes RAM

The owner's framing, 2026-10-17: under kern_mega **there is no XMS - it is
just RAM.** Everything above 1MB is reachable by a descriptor, so the machinery
real mode needed to reach it - XMEM.DRV, `int 15h AH=87h` and its CPU reset,
LOADALL (SPEC.md 41.13), unreal mode (`ub_set`, SPEC.md 41.15) - has nothing
left to do, and the parts of it that load a segment register the real-mode way
cannot run at all. Two things follow, and they are the end of this plan.

### 12.1 A driver or package must be able to ASK

Today nothing can tell kern_mega in protected mode from kern_big: the API
table is the same, and so is everything `OSAPI_CPU_INFO` answers. The
proposal is one more `CPU_F_*` bit in AH - **`CPU_F_PM` (0x10): segments are
SELECTORS** - set by `mega_enter` and never on any other kernel, so a binary
built before it reads 0 and keeps its real-mode behaviour on every machine it
already ran on. It is an SDK addition and not a re-contract (SPEC.md
20.2.0.1: `abilock.py --update` without a `PKG_FMT` bump). What the bit
promises, written once in os88api.inc beside the other four:

* segment arithmetic must stay on the 8-paragraph grid (section 3.1, item 18);
* no LOADALL, no unreal mode, no `int 15h AH=87h`, no `int 15h AH=88h`
  sizing of memory a program then addresses itself;
* the `OSAPI_XMEM_*` cells are the kernel's own and work from any context
  (12.2).

A refusal on the bit is the ordinary path (SPEC.md 47): a feature that has no
protected-mode route yet greys itself with the reason rather than faulting.

### 12.2 The routes, by kind of user

**Most XMS users never touch extended memory themselves** - they call
`OSAPI_XMEM_ALLOC`, `_COPY` and `_FREE`, with an opaque 32-bit base. Those
keep their ABI byte for byte if kern_mega implements the five cells itself:
a pool above its own tables (the GDT, LDT and IDT end at 0x124800; a 1MB 286
has 384KB above 1MB), and COPY as a `rep movsw` through a scratch descriptor
- no BIOS, no reset, and so no reason for the UI-task-only rule (os88api.inc's
note on tier 1), which kern_mega can drop. XMEM.DRV is then not shipped on
kern_mega at all (section 7 already said so). These users, from the survey:

| user | what it keeps in XMS | route |
|---|---|---|
| `apps/os88partsbody.inc` | lazy and scratch parts (SPEC.md 20.12) | the cells, unchanged |
| `apps/os88xgap.inc` | the shared gap buffer | the cells, unchanged |
| DOS box (`apps/dos`) | the box's XMS door (2.7) | the cells - and M6's question |
| Pixel editor | undo and layers | the cells, unchanged |
| RAM disk (`drivers/ramdisk`) | the store and its image | the cells, unchanged |
| EMS (`drivers/ems`) | EMS pages backed by XMS | the cells; the page frame is a descriptor rebase, which is a cheaper EMS than the copy |
| Video player (`apps/video`) | the XMS bank, through the cells | the cells, unchanged |
| Task Manager | shows the pool | CAPS, unchanged |

**Three users reach past the cells**, and they are the ones that need a new
route rather than an implementation:

| user | what it does | the protected-mode route |
|---|---|---|
| `apps/os88ub.inc` (Video player) | loads a segment register's HIDDEN base above 1MB (unreal) and addresses the bank in place | a selector: the bank IS addressable, which is section 4 item 5 |
| `apps/os88la.inc` (Video player, the 286) | LOADALL for the same in-place reach | the same selector - LOADALL is a 286 instruction kern_mega must never let a package run |
| `drivers/hdd/hdxms.inc` (HDD.DRV rung 1) | `rep insw` with ES's hidden base at the XMS address, and an A20 probe through FFFF | the same selector, and the probe deleted: `mega_enter` verified A20 and the bit says so |

So the one new cell the section needs is **`OSAPI_XMEM_SEL`** (BUILT,
10.3 item 72, with `OSAPI_MEM_CODESEL` beside it): DX:AX = a base
you own, CX = a window of up to 64KB inside it -> an LDT data selector whose
descriptor covers exactly that window, freed with the block. It is a refusal
(CF = 1) on every other kernel, which is what lets one binary carry both
routes and choose by `CPU_F_PM` rather than by build. The selector is stable
across a compaction (3.2), so no relocation proc is owed for it. This is M3's
LDT allocator used from outside the kernel for the first time, so it lands
after M3, and 12.1's bit can land now - it costs a byte of `mega_enter` and
makes every refusal above writable today.

## 13. Memory: high first, and the low region that must stay

The owner's direction, 2026-10-18: **load the kernel at the top of RAM and
take every claim from the top down.** Compaction packs everything but modules
and claims in use, so taking a low claim from the top no longer causes the
permanent fragmentation the lo/hi split was built to prevent. And going high
first leaves the bottom 640KB free the longest, which is what a DOS program
run beside protected mode (M6) needs most.

### 13.1 Why it works now

* **LDT selectors do not change when a claim moves** (3.2). Compacting above
  1MB copies the bytes and rewrites one descriptor base. No relocation proc,
  no `mem_rr_tab` entry, no pinned worker frame: HEAP-UNPIN-PLAN's whole
  problem does not exist for an LDT claim.
* **One direction is enough** once compaction runs. The lo/hi split kept
  long-lived claims (drivers, modules) and short-lived ones (packages) from
  walling each other in; packing toward one end does that job instead.
* **Conventional memory is used last**, so it stays contiguous for as long
  as the machine has anything above it.

### 13.2 What must stay below 1MB

High-first is the rule for everything EXCEPT a small fixed low region, and
each item in it is there because real mode or a ROM has to reach it:

| item | why it is low | found by |
|---|---|---|
| the reflection's real-mode half: `mega_svc` from `.rm` to the return, `mega_r286`, the capture stubs (`mega_cap2`..), `mega_capon/off` | it RUNS in real mode at 0060:xxxx, and a 286's BIOS jumps to `mega_r286` through 40:67 | 10.3 items 18, 24 |
| bounce buffers for BIOS-reflected transfers | a reflected `int 13h` gets a real-mode ES:BX; a BIOS-only disk (the Victory's PicoMEM C:) reads through it on every call | 10.3 item 11 |
| any DMA buffer a ROM touches | the PicoMEM's Sound Blaster "DMA" is its ROM doing a real-mode `rep movsw` from a seg:off it builds out of the 8237's page and offset, so the SB ring must be below 1MB on that card | 10.3 items 18, 20 |
| DMA buffers in general | below 16MB and inside one 64KB page, as today | the 8237 |
| the IVT, the BDA, LOW_SEG's task stacks | real mode reads them across every reflection | 2.2 |
| packages that do segment arithmetic | above 1MB a selector is not a segment, so `seg + N` is meaningless; only a package that declares itself clean (a header bit) or uses `OS88_HUGE_INCR` (M4) may be placed high | 3.1, 3.2 |

Everything else - `.cold` (done, at 0x125000), modules, the disk caches, the
FAT snapshot, the read-ahead, driver images that do no segment arithmetic, and
clean packages - is placed from the top of extended memory down.

### 13.3 The order to build it in

1. **The LDT allocator** (M3): descriptors with arbitrary base and limit from
   a reserved LDT range, code/data pairs where code lives, freed with the
   claim. Entries 1280-2047 alias segments A000-FFE0, which no image
   occupies, so they are free for it (10.3 item 10). BUILT (10.3 item 30)
   for single descriptors; the pair is 13.5.
2. **High-first for kernel-owned claims**: the caches first, through a bounce
   buffer for any transfer that goes to the BIOS, then the modules. The
   read-ahead store is BUILT (10.3 item 32), and so are the modules the census
   clears (item 52, 13.5).
3. **Compaction above 1MB** by descriptor rebase. REFUSED in favour of
   placement (13.4.1, 10.3 item 53).
4. **Packages high**, behind the clean bit and M4's SDK, after which the
   conventional heap is only what the low region and unclean packages need.


### 13.4 Extended memory has ONE allocator - decided

The handoff (track 1, step 2) left a choice: carve a region for the kernel
out of the `mx_*` pool, or give the pool and the heap one table. Neither:
the kernel takes its blocks from the pool's OWN table, under an owner byte
of its own (FEh, `MX_KOWN`). Why:

* **One free figure.** A fixed carve-out is memory the cells can never
  have and the kernel may never use; two tables would each need the other's
  figure to answer CAPS honestly. The pool's walk already does first fit
  at 1KB granularity, which is what a cache or a module wants.
* **The heap's table is the wrong shape for it.** `mem_tab` records are
  segments, relocation procs, page heads and MC_DMA doors - all of it about
  conventional memory a real-mode ES can name. A block above 1MB is named
  by a selector and moves by a descriptor rebase (3.2); none of `mem_tab`'s
  machinery applies, and the compactor's planner would have to learn to
  skip it everywhere.
* **The cells cannot reach it.** `mx_free` and `mx_copy` check the
  caller's owner byte, and no instance is FEh; `mx_relall` releases one
  instance's blocks. The kernel's own XMEM-cell blocks keep FFh, which a
  package's proc frees.

The cost is that the pool's table (`XM_MAX_BLKS` entries) is shared, so a
kernel block is one entry fewer for packages. Compaction above 1MB (13.3
step 3) is then a walk of this one table, rebasing descriptors.

#### 13.4.1 Placement instead of compaction - decided

13.3's step 3, compaction above 1MB by descriptor rebase, is REFUSED, and
what it was for is done by placement. Two facts decide it:

* **Only the kernel's own blocks could move.** A package's block is named
  by its LINEAR base (`OSAPI_XMEM_ALLOC`'s token, which `os88ub.inc` and
  `os88la.inc` even address in place), so nothing can rebase it; a
  compactor could only shuffle the read-ahead store and module pairs.
* **A module pair can be on the stack.** The running code's CS descriptor
  is cached by the CPU, so a module moved while one of its routines is
  executing goes on running the old copy - fine until the copy overlaps it.
  Every move would have to prove that no module frame is live, which is the
  pinned-worker problem of HEAP-UNPIN-PLAN in a new place.

So `mx_alloc` places the kernel's blocks (owner FEh) FIRST FIT FROM THE
TOP and packages' from the bottom, as `mem_claim_hi` and `mem_claim` split
the conventional heap (SPEC.md 50.3): the read-ahead store and the module
pairs never stand between two runs a package could have joined, and a
module dropped leaves its hole among the kernel's blocks, where the next
module refills it. Measured under Bochs: the store's descriptor base is
FF8000h, the top of 16MB, and `tests/megaxm`'s 64KB allocation still gets
130000h, the pool's base, with CAPS 32KB less than before the store
(3B20h against 3B40h).

### 13.5 Modules above 1MB - the design, BUILT (10.3 item 52)

What had to change, found while doing 13.4 and then built as written (items
2 to 4; the census of item 5 decided which modules):

1. **The read.** `mod_need` claims (`mem_claim_hi_x`) and `dskw_read_x`
   reads the whole file into the claim's segment. High, it reads into a
   conventional transient and copies up with `mega_lincopy`, then frees it.
   BUILT with no transient of its own: the module's ordinary conventional
   claim IS the transient, so the read, `mod_check` and the arming are
   unchanged and `mod_hi` runs after them.
2. **A code/data PAIR.** `mega_ldt_alloc` gains a two-entry form:
   code at entry n, data at n + 1, so the rule is DATA = CODE + 8 for an
   allocator selector - one compare in `mega_cs2ds`, `mega_dsimg` and
   `mega_csdata` (they refuse the range today, item 30) - and in
   `osapi_vol_fence` (`kernel/disk.inc`, item 26), which converts ANY LDT
   selector by the alias rule. BUILT in the first three; the fence needs
   nothing, because it compares the result with a published DRIVER
   segment, a driver is always conventional, and so a module's selector is
   refused there either way - which is the right answer for a module. A
   CS write in a module completes through `mega_gpw` exactly as before.
3. **The slots.** `mod_check` arms each slot with the image's code alias
   (`(S >> 1) | 4` after the shift it already does); high, it stores the
   pair's code selector, and `mega_mods_fix` has nothing to convert.
4. **The drop.** `mod_free_row` frees by segment through `mem_free`; high,
   by selector through `mega_hifree`, and the row has to remember which.
5. **What must stay low - the census comes FIRST, and it is why this
   round was not started on 2026-10-18.** Nothing in a module runs in
   real mode (13.2), but a module's DATA selector reaches places that
   treat it as a segment: a buffer inside its own image handed to the file
   layer is NORMALISED (`dskw_norm` adds BX >> 4 to ES - arithmetic on a
   selector), and one handed to a reflected `int 13h` is refused 09h by
   item 33's fence. CTRL.DRV writes SYSTEM.CFG, FORMAT.DRV and
   DISKOPS.DRV move whole tracks; any of them would fail on the Victory's
   BIOS-only C: and only there. So the round starts with a census of every
   module's buffers handed to `dskw_*`, `dsk_*`, `int 13h` and the
   `gfx_*` calls that take a segment, and moves high only the modules it
   clears - a row flag in `mod_tab` - with the others kept low as today.

   **The census, by reading (2026-10-18)**, for kern_big's modules:
   CTRL.DRV is blocked by the file layer (the SYSTEM.CFG save from
   `cpc_buf` and the shortcut's `.LNK` write, `dskw_write*` with ES = the
   image); DISKOPS.DRV likewise (`CLO_RSEQ cs`, a read-sequence cursor in
   the image handed to `dskw_read_seq`); HIBER.DRV by the file layer and
   by `apic_font_str` with DS = the image (and it refuses protected mode
   anyway); DESK.DRV by its LIB verb's paragraph arithmetic (item 41:
   correct on an alias now, but not on a pair). FORMAT.DRV moves tracks
   only through LOW_SEG's `dsk_secbuf`; DOCK.DRV and EXTD.DRV hand nothing
   of their own out; STIKMENU.DRV hands its own buffers only to `dsk_find`
   (a copy, no transfer) and `gfx_blit1` (no arithmetic). Every
   `dskw_*` path converts ES with `mega_s2segf`, so the file layer can
   never take a buffer above 1MB without a landing of its own. So the clean
   set is DOCK, EXTD and STIKMENU - 2 to 6KB each, transient - and the
   round is worth building only once CTRL.DRV (14KB, the largest) stages
   its two writes through a low buffer.

   **Superseded by 14.47 (2026-10-21): every module goes high.** The
   census above was taken before item 77 gave the file layer a landing of
   its own for a selector above 1MB (`dskw_hiseg`), and that landing is
   exactly what CTRL.DRV's and DISKOPS.DRV's blockers needed; HIBER.DRV's
   "refuses protected mode" stopped being true at W5. A module the boot
   loaded is moved at the switch (`mod_hiboot`).

### 13.6 RAM is RAM - packages above 1MB, and what the machine says about it

The owner's direction, 2026-10-18: *"get to packages and kernel at the top,
and 'ram is ram' - with task manager (for kern mega) updated to show that."*
Built in waves (10.3 items 77-87):

1. **The file layer takes a buffer above 1MB** (item 77). Every package's
   file I/O goes through it, so it came first. `OSAPI_XMEM_SEL` already
   made such a buffer, so it was testable before any package moved.
2. **A package that says `OS88_HIGH` is loaded above 1MB** (item 78). Its
   region is a code/data pair from the pool, named by the data half
   wherever the segment was, and every place the kernel turns a package's
   segment into a CS learned the pair.
3. **The census opted 24 shipped packages in** (item 79). **The Task
   Manager counts the whole machine** (item 80), through
   `OSAPI_XMEM_WALK`.
4. **A package that says `OS88_HIGHMEM` claims above 1MB too** (item 81):
   its claims of 64KB or less are pool selectors, resized in the pool or
   moved down past it, and every kernel service that takes a segment takes
   one - the redirected volumes included, 512 bytes a call.
5. **Drivers run above 1MB, and ETHER.DRV's socket pool with them** (items
   82-83); **the kernel's caches and FAT windows are pool blocks** (items
   84, 86); **a high buffer moves a run at a time** (item 85); **a second
   census put 29 more packages high** (item 87), the Sticky Apps and all
   seven C packages among them.

On a Bochs desktop with SOUND, HDD and ETHER loaded and programs open, the
kernel's conventional claims come to 5K - the read-ahead landing, which is
the bounce - and every program that passed a census runs above 1MB.

What is NOT yet RAM is RAM, in the order it would be taken:
- the packages whose CLAIMS do paragraph arithmetic or far-call into a claim
  (Paint's row table, Frotz, Tracker, MIDIRack, 1942, Dr Marco, Excitebike,
  Tank, PiXEL, every C package with an overlay) - each a change in the
  package, rows or offsets inside 64KB selectors;
- a claim over 64KB, which stays conventional - a huge claim above 1MB
  wants a selector per 64KB and a package that steps them, which is a new
  ABI rather than an opt-in;
- the five re-homers (Word, Scribe, Skies, PIXELSTEIN 3D, DOS.O88's
  loader), Redline and the Video Player, which stay low by a census; a
  re-homed program runs in a conventional carve, so placing its loader high
  gains nothing;
- SOUND.DRV's rings and HDD.DRV's buffers, which the 8237, the PicoMEM's
  ROM or the BIOS must reach; USBMOUSE.DRV, whose hook a ROM calls in real
  mode;
- the kernel's own image and data, `.text` and `.bss` at KERNEL_SEG and the
  LOW_SEG stacks, which the reflection's real-mode half runs in (13.2).

`docs/plans/KERN-MEGA-HANDOFF.md` is the parallel work cut from this and
from section 10.

## 14. Decisions taken while the owner was away (2026-10-18), to look back over

Each is a call made without asking, because a reasonable default existed.
Each says where it lives, so it can be reversed.

1. **The packages-high opt-in is bit 7 of the header's stack-class byte
   (+15)**, spelled `OS88_HIGH`, rather than a new field. `LD_H_FLAGS` has
   no free bit. An out-of-range class already falls back to the default on
   every kernel (instance.inc's `cmp bx, SCH_NCLASS`), so an opted-in
   package on an older kernel gets the largest stack and nothing worse.
   The new kernel masks the bit off before the class lookup.
2. **A high region is named by its pair's DATA selector** wherever the
   segment is stored today (`I_SPTR`, `[ld_base]`, `W_SEG`, `MC_OWN`).
   Code is data - 8. That is the opposite of `MODR_SEG`, which holds the
   code half, and it is chosen because those words all come from the
   caller's DS or ES.
3. **(Upgraded by item 85: runs, through the landing.)** **A high buffer moves a sector at a time** (item 77). The file layer
   bounces through `dsk_secbuf` rather than claiming a landing of its own,
   which costs one reflection per 512 bytes on a BIOS-only C:. A
   multi-sector landing like the read-ahead store's is the upgrade if the
   Victory shows it matters.
4. **The redirected volumes are not covered** for a high buffer (item 77),
   and must refuse before a package placed high can run off one.
   **Superseded by item 81**: they are covered, by a bounce 512 bytes a
   call (`drv_fs_hi`) rather than a refusal, because a refusal would have
   been every high package's file I/O on a RAM disk.
5. **CALL 5 on kern_big stays PSP:0050's gate** (KERN-MEGA-DOS-PLAN 10.5.1
   item 3). The real CP/M dispatcher is kern_mega's HMA run only.
6. **The conventional hibernate image still has no sum on either kernel**
   (KERN-MEGA-DOS-PLAN 10.5.1 item 5).
7. **W6 primes every option-ROM line** (KERN-MEGA-DOS-PLAN 10.5.1 item 6),
   because the 8259 cannot say which line is held.
8. **A package placed high still gets CONVENTIONAL claims** (item 79).
   Paint's row table and others do paragraph arithmetic on their claims,
   so a selector from `OSAPI_MEM_CLAIM` would need a second opt-in. That
   is the next step toward all of RAM being RAM for a program, and it is
   not taken yet.
   **Taken by item 81** as exactly that second opt-in, `OS88_HIGHMEM`.
9. **(Withdrawn by item 87: the trap is the same low or high.)** **The Sticky Apps are kept low** (item 79) until a `mega_gpfix` trap
   per library callback is measured on a 286.
10. **Audio, Tracker and MIDIRack are high only because `OSAPI_FSX_SPK`
    refuses in protected mode** (item 79). If that cell is ever granted,
    they must come back out first, because `os88spk.inc` writes its own CS
    into IRQ0.
11. **The Task Manager's RAM figure counts only RAM**: conventional memory
    plus everything above 1MB. It leaves out the 384KB of video and ROM in
    between, so a 4MB machine reads 3.6M.
12. **`OS88_HIGHMEM` is bit 6 of the class byte** (item 81), the same
    argument as decision 1: no flag bit is free, and an old kernel treats
    the out-of-range class as the default one. Only claims of 64KB or less
    go high - one selector each - and a regrow past that MOVES the claim
    down rather than refusing, so a document that outgrows a segment keeps
    working exactly as on any other kernel.
13. **(Superseded by item 84 for the caches.)** **The kernel's own claims stay conventional for now** (13.3's "kernel
    claims at the top"). Measured rather than argued: 546K of the 562K
    conventional heap is free and in one run with the desktop and the high
    packages up, so the FAT window and caches are not walling anything a
    program can feel. Item 51's design stands for the day a measurement
    says otherwise.
14. **A freed selector popped back into DS or ES becomes NULL** rather than
    a fault (item 81, `mega_gpfix`). It is the closest protected mode can
    come to real mode's "a segment is only a number": the pop succeeds and
    the first USE of the register faults naming itself. Only a free entry
    in the allocator's range, and only `pop ds` / `pop es`, are forgiven.
15. **Paint stays conventional for its claims** even though its region is
    high: its row table derives each row's segment from the claim's by
    paragraph arithmetic. Making it high-claim-safe is a Paint change (rows
    as offsets inside 64KB selectors), not a kernel one. **Done in 14.14**:
    Paint declares `OS88_HIGHMEM`, and a canvas that fits one segment is
    laid out flat.
16. **A driver's high opt-in is bit 7 of +15** (item 82), the DSV table
    length. Every kernel built before it clamps that byte to its own table
    length (`drv_publish`'s `cmp cl, DSV_SIZE`), so a new driver on an old
    kernel publishes exactly what it did; the alternative, a new header
    field, would have been a `DRV_VER` bump for one bit.
17. **The boot's drivers are MOVED, not reloaded** (item 82): after
    `mega_enter`, by the compactor's own re-pointing walk. Reloading them
    would have re-read each file and re-run each attach - DHCP again for
    ETHER.DRV - where a move costs a copy.
18. **(RAMDISK.DRV: withdrawn by item 85, `rd_fold`.)** **RAMDISK.DRV and USBMOUSE.DRV stay conventional** (item 82): the RAM
    disk's seed does paragraph arithmetic on its own CS, and the PicoMEM's
    ROM runs the USB mouse's `int 33h` hook in real mode. The first is a
    small RAMDISK change if it is ever wanted; the second is not fixable
    from this side.
19. **A driver's claims have their own opt-in bit** (item 83), not
    `OS88_DRV_HIGHOK`'s: SOUND.DRV's image is clean and its pools are not.
20. **Kernel caches go high by an allowlist of tags** (item 84), not by a
    property of the claim: what makes a cache safe is how every reader
    uses its segment, which only a census can say. A new cache stays
    conventional until it is added to `mem_hitag`.

### 14.1 The owner's answers (2026-10-19)

The list put to the owner numbered the open calls 1-21; these answer its
first four.

1. **The conventional hibernate image gets a SPARSE sum, on both kernels**,
   if it costs under 200 ms. Only a sparse sum fits that on every machine: a
   full sum of ~640KB is ~2 s on a 4.77 MHz 8088 (and ~0.3 s on a 16 MHz
   286), while one word sampled every 256 bytes is ~2,560 reads - a few ms
   anywhere. It is distributed across the whole image, as KERN-MEGA-DOS-PLAN
   12.1 answer 3 asked of the park's sum, so it catches a wrong, truncated
   or cross-linked image and misses a stray byte. NOT BUILT YET (KERN-MEGA-
   DOS-PLAN 10.5.1 item 5).
2. **W6's prime stays generic** - every ROM-owned line, whatever card owns
   it. That is what is built; the PicoMEM-gated version was the
   card-specific alternative and is dropped (KERN-MEGA-DOS-PLAN 10.5.1
   item 6).
3. **`OS88_HIGH`/`OS88_HIGHMEM` stay in the class byte's top bits** (decisions
   1 and 12). Breaking new apps on old kernels would have been acceptable; not
   having to is preferred. They leave six bits for the class, against a
   handful of classes in use.
4. **The driver bits stay in +15 as well** (decisions 16 and 19). A driver ships
   only on a system disk, beside its own kernel, so it may break more
   readily still - but one `.drv` binary is shared by kern_big's, kern_small's
   and kern_mega's disks at a commit, so a change must suit all three kernels
   built from that commit. The one limit: with bits 6 and 7 taken, the
   service table (`DSV_SIZE`, 36 today) can grow to 62 bytes before +15
   stops holding its length.
5. **Two conventions for a pair are fine as long as they are clear**
   (decision 2). SPEC.md 8.7.2 now carries the table: which fields hold the
   data half, which the code half, and the interrupt vector as the one field
   that can hold either.
6. **Claims of a megabyte and more must work** for a program written for
   kern_mega, on a machine that has the memory. Decision 12's 64KB limit
   (and moving a grown claim back below 1MB) is therefore the CURRENT state,
   not the design. NOT BUILT: it needs a run of consecutive LDT selectors per
   claim (today's allocator hands out one or a pair) and 7's answer.
7. **The step between a big claim's selectors: an ADDITION is fine, a break
   probably, with more context first.** Proposed as an addition, so no
   break is needed: a new cell appended at the table's end, a "huge claim"
   taking AX = KB like `OSAPI_MEM_CLAIM` and answering DX = the first
   selector and CX = the step - 8 for a run above 1MB, 0x1000 for a claim in
   conventional memory on every kernel. `OSAPI_MEM_CLAIM` and the constant
   `OS88_HUGE_INCR` keep meaning exactly what they mean today, so nothing
   already built changes; only a program that calls the new cell walks by
   CX. The break would be making `OSAPI_MEM_CLAIM` itself answer a run: every
   `OS88_HIGHMEM` program stepping a claim by the constant 0x1000 would then
   walk into the wrong descriptors, silently, and `PKG_FMT` would bump.
   Ceiling either way: a 286 descriptor's base is 24 bits, so a claim tops
   out with the machine at 16MB.
8. **Kernel-specific app builds, and kern_mega-only apps, are acceptable
   when there is a good reason** - the two `DOS.O88`s being the first.
9. **The freed-selector leniency stays** (decision 14). The reload it
   forgives is the ordinary `push es` / free the block ES named / `pop es`
   shape, which real mode never noticed; in protected mode the `pop`
   itself would fault. Kept unless it starts hiding defects.
10. **POLICY: never hide RAM; every KB the user has is accounted for, and
    the front page's figures always add up** - "free" is what a program
    could use, the rest is the system's, and the two sum to the total.
    The front page shows the total and free (in M where that reads better)
    and one line for IVT+BIOS with today's "Pad" folded into it; the Memory
    page carries the detail and the map; the heap is the base 640K less the
    IVT, BIOS data and whatever system stays there, plus everything above
    1MB except the system at the top. Decision 11 (leave out the 384K) is
    REVERSED. It reaches kern_big's Task Manager too: a 1MB machine's HMA has
    to be shown and accounted for. NOT BUILT. What "total" can be is the
    open half: the BIOS reports base + extended (3,712K on the mr286's 4MB),
    and whether the other 384K is lost, shadowed or remapped is the board's
    business. **Settled (2026-10-19): report what is KNOWN, never guess** -
    the total is base + extended as the BIOS reports them, plus the HMA;
    the Memory page maps 640K-1MB from what can be read safely (the
    adapter's framebuffer, option ROMs by their 55AAh signature and length,
    the system BIOS) and calls the rest of that range unreported rather than
    attributing RAM to it.
11. **A BIOS transfer into a high buffer must WORK** (item 33's refusal is
    reversed): correct and fast where possible, refused only if the concept
    itself is bad. The shape: the native paths (`mega_fd13`'s DMA, which
    the AT's 8237 can aim anywhere below 16MB, and HDD.DRV's PIO) go
    straight into the high buffer; a BIOS-only disk bounces through the low
    landing a run at a time, as the file layer already does. NOT BUILT.
12. **Compaction above 1MB is in the design; placement was a stopgap**
    (13.4.1's refusal is reversed). The target: the system pinned at the
    TOP, a small real-mode part at the bottom (the part the DOS park moves),
    and EVERY program region and EVERY program claim above 1MB, compacted
    when a claim needs it, so a 2MB machine (1,664K usable on this BIOS)
    gives one program ~1,550K. The first real consumer is the Video Player.
    NOT BUILT. What it needs, all recorded here so it is not re-derived:
    - selector-named blocks move with NO relocation proc - the selector is
      stable and only its descriptor's base changes - provided nothing has
      it loaded in a segment register at the time, which is the safe point
      the conventional compactor already finds (`mem_frameless`);
    - blocks named by LINEAR address (`OSAPI_XMEM_ALLOC`'s token, read in
      place by `os88ub.inc`/`os88la.inc`) cannot move without an opt-in of
      their own - they are what pins the pool today;
    - "every claim high" needs every program clean: the ones that do
      paragraph arithmetic on a claim (Paint's row table, RunCPM, LOOM,
      item 87's kept-low seven) are fixed first, or they would be handed a
      selector and break silently;
    - a huge claim made of 64KB selectors (14.1 answers 6 and 7) needs each
      64KB piece contiguous but NOT the pieces adjacent to each other, so
      compaction need only make 64KB holes, and one claim can use
      conventional and extended memory together.
13. **The boot's drivers keep being MOVED** (decision 17): drivers are
    movable already (HEAP-UNPIN-PLAN), so a reload would buy nothing a move
    does not.
14. **The speaker must work on kern_mega** (decision 10's cell). Options in
    the owner's words: the whole program low, or a code claim that runs just
    what must be low. A third, to price first: leave the program high and
    have `OSAPI_FSX_SPK` install the IRQ0 handler as a PM gate to its code
    selector, the way `mega_ivt_sync` already builds gates for a high
    driver's vectors - nothing low at all. NOT BUILT.
15. **USBMOUSE.DRV stays low** (decision 18): it belongs with the small part
    of the system that must run in real mode. Splitting a driver into a low
    real-mode stub and a high body is a possible long-term shape, not a
    concern now.
16. **No minimum extended memory beyond FITTING** (11's question 2). Today
    `mega_enter` refuses under 192KB of extended memory and the machine runs
    in real mode, because the GDT, LDT and IDT sit at fixed addresses above
    1MB (`MEGA_GDT_LIN` 110000h - the pool at 130000h). A 1MB 286 whose BIOS
    reports no extended memory is a 640K machine and should still get
    protected mode, so the tables must be placeable in conventional memory -
    ~82KB of it (the identity GDT is a full 64KB, the LDT 16KB, the IDT 2KB),
    which is the cost to weigh against what protected mode buys there. NOT
    BUILT.

The Video Player, from 12's answer: it CRASHES on kern_mega today (its
`os88la.inc` path runs LOADALL), so it needs a protected-mode branch -
using RAM natively through the >64KB claim (answers 6, 7), not by
pretending a pool block is XMS and bouncing it. The owner notes a ~30%
difference it found from ALIGNMENT on interleaved-memory machines; the
pool's 1KB granularity already gives every base 1KB alignment, and where
that measurement lives decides whether more is needed.

How the Video Player picks its alignment (owner, 2026-10-19): it takes its
claim, speed-tests copies at offsets 0, 512, 1K and 2K into it, and wastes
the bottom of the claim when one offset is faster - interleaved memory. Its
protected-mode path must keep that probe: it works unchanged inside a
selector, the offsets being the selector's own.

14 again: the PM-gate option is the one to take if it works.
16 again: DEFERRED TO LAST. On a machine with no extended memory kern_mega
is a worse kern_big, until programs exist that need protected mode AND fit.

17. **CALL 5 reaching the real CP/M dispatcher on both kernels is wanted** -
    a compatibility gap old DOS programs may use, worth its 48 bytes.
18. **The pointer should keep moving through a reflected BIOS call**, if
    that is not a huge ordeal. What the owner sees on the PicoMEM is
    kern_big, where SPEC.md 7.4 already moves it; on kern_mega the
    reflection masks the mouse's IRQ (`mega_svc`'s B8h/3Fh) because the ISR
    is protected-mode code. The likely shape is cheap: the kernel's own code
    sits at KERNEL_SEG in conventional memory and is 8086 code, so the real
    `mou_isr` can be the vector during a reflection instead of the capture
    stub - after an audit of its path for anything that loads a SELECTOR
    (`[k_dsel]` is 10h in protected mode and means nothing in real mode).
    This reverses 11's question 3, which recommended no bimodal ISR.
    LONGER TERM, after kern_mega: a system that does not freeze for disk
    transfers at all - probably AT and up only, so that no per-controller
    driver is needed. Kept in mind, not planned.
19. **kern_mega's system disk is 1.2MB and 1.44MB only** - no 360KB or
    720KB, so a protected-mode program has room. `make mega` builds the
    1.44MB disk today; a 1.2MB twin is owed.
20. **The 86Box harness calls stand** - the patched GDB stub, the hidden
    F000 lines, `vm/286-mega` without the owner's VHD. The harness is
    expected to grow: much of what only 86Box emulates has until now meant
    a disk sent to the owner, and patching the emulator to suit the harness
    is the accepted way.

### 14.2 The work the answers make (2026-10-19), in order

1. The Video Player crashes on kern_mega (LOADALL): stop the crash first.
2. The Task Manager's RAM, by answer 10's policy, on kern_mega and on
   kern_big (the HMA). The owner's note on SYSTEM: kern_big SUBTRACTS
   `.cold` when it is placed in the HMA or a ROM, because that memory is no
   program's to use; on kern_mega the top of extended memory IS a program's
   to use, so everything the kernel keeps there - `.cold`, the modules, the
   caches, the tables - is counted in System.
3. The sparse hibernate sum, both kernels (answer 1).
4. BIOS transfers into high buffers work (answer 11).
5. The speaker on kern_mega through a PM gate (answer 14).
6. The pointer through a reflected BIOS call (answer 18).
7. Huge claims: a run of selectors per claim and the new cell (answers 6, 7).
8. **Every program works in protected mode, high and with its claims high**
   (owner, 2026-10-19). Whatever keeps a program low, or its claims low,
   because of what it does inside itself is reworked. The rule for HOW: a
   generic change when it is small; a kern_mega-only build of the program
   (answer 8's shape) when the change would grow it by more than 512 bytes
   or make it visibly slower on kern_big. The list at this commit:
   - not placed high at all (7): DOS.O88's loader, PIXELSTEIN 3D, Redline,
     Scribe, Skies, the Video Player, Word - the re-homers and the ones
     that program hardware or `int 13h`/`int 15h` into their own DS
     (item 87);
   - high, but their claims kept low (15 that claim and do not say
     `OS88_HIGHMEM`): 1942, Apple II, C64, CWORD, Excitebike, Frotz, LOOM,
     MIDIRack, ModPlug, Paint, PiXEL, RunCPM, Tank, Tracker, WEAVE. Some are
     only unaudited rather than unclean - the census (item 87) cleared the
     image and not every claim - so each is read before it is changed;
   - Audio, Tracker and MIDIRack's speaker path is item 5, not this one.
9. Compaction above 1MB, every program and claim high (answer 12) - after 8,
   since a program that is not clean breaks when its claim is a selector.
10. The Video Player's native protected-mode path, keeping its probe (12) -
    BUILT, 14.16.
11. kern_mega's 1.2MB system disk (answer 19).
12. LAST: the tables in conventional memory for a machine with no extended
    memory (answer 16).

### 14.3 Item 1 as built: the Video Player on kern_mega (2026-10-19)

**It was never LOADALL that crashed it.** The owner's report was right that
it crashed and the cause found on the mr286 (`tools/os88box.py`, the fault
log's new `OS88WHY` line) was a different one: `#GP 1528` at `vp_khdr`'s
`mov ds, dx`, 86Box's reason `loadseg(): Normal segment RPL > DPL`. A #GP's
error code masks the selector's low two bits, so DX was really 1529h-152Bh:
the player normalises a read's DX:SI to the nearest PARAGRAPH, which leaves
the segment off the 8-paragraph grid - the defect Frotz and Tracker had
(10.1 item 18). Fourteen normalisations now move whole 128-byte GRANULES
(offset 0..127, 8 paragraphs a step), which real mode reads as the same
bytes: +29 bytes of image, no measurable cost on kern_big. The LOADALL and
loaded-base arms are ALSO switched off under `CPU_F_PM` (+5 bytes), since
they cannot work there either: `[vp_xchk]` reads "in place failed" from the
start and the pool is copied with `OSAPI_XMEM_COPY`.

**Measured on the mr286 profile**: DEMO.V88 opens, draws its first frame and
PLAYS - the bar moves and the picture changes - with no fault the kernel did
not complete. **What it costs there is item 10's work**: a play takes ~5,500
`#GP`s that `mega_gpw` completes, every one a write through CS while DS is a
record's - `vp_lrget` (5 a call, 858 calls), `vp_rbnd`, and the decoder's
per-list map and its patched sites in `vdec.inc` (220 each). The player
works at those; it is not fast at them, and with no free segment register on
an 8086 the cure is to keep that state in registers or on the stack, which is
a decoder rework and belongs with the native protected-mode path.

### 14.4 Item 2 as built: the Task Manager's RAM, both kernels (2026-10-19)

SPEC.md 28.4.8 is the contract. What changed, against answer 10:

- **`IVT+Pad` is `IVT+BIOS`**, and it carries the BIOS's extended data area
  when the BIOS **states** one: 40:0Eh must be exactly `int 12h`'s top and the
  area's first byte its size, inside 640K. Both the row and the total take it,
  so base memory still adds up. Nothing is guessed: a BIOS that does not
  describe its area gets 0.
- **System includes what the kernel holds above 1MB**, on both kernels, from
  `OSAPI_XMEM_WALK`: the KB under the pool's base (kern_mega's tables and
  `.cold`; kern_big's HMA) and every block owned C0h and up. The memory view
  gets an `Above 1MB` row under System so its rows still sum.
- **RAM counts the BIOS's extended memory on kern_big too**, in MB. HEAP
  counts it only on kern_mega, where it is a program's to run in; on kern_big
  it is a store, and the `XMS` line is the pool.
- **A program's MEM column on the PROCESS page** now adds its blocks above 1MB
  (`tm_slotxkb`). Only the memory view did before, so a high-claiming program
  read smaller on the front page than on the second.
- **`OSAPI_XMEM_WALK` answers on kern_big with a store** - an ABI addition,
  a refusal that became an answer (abilock updated without a bump). The body
  is `XMEM.DRV`'s (`XMV_WALK`, overlay ABI 1 -> 2) and the kernel's half is
  24 bytes of `.text` and 2 of `.bss`.

**Measured on 86Box**: the mr286 (4MB, kern_mega) reads `RAM 0.3/3.6M`,
`HEAP 0.0/3.2M`, System 343K = Code+data 68 + Stacks 3 + Above 1MB 267 +
FAT snap 5; an AMI 286 with 2MB on kern_big reads `RAM 0.1/1.6M`, System
122K of which 64K is the HMA, `XMS 0/960K`. The rows add to RAM used on both.

**Decisions to look back over:**

1. **The AMI 286 at 1MB reports NO extended memory** (its setup reads
   `Ext. memory size: 0 KB`): the 384K over 640K is shadow on that board and
   the BIOS never offers it. So "a 1MB machine" shows 640K there, which is
   what the machine says it has. A board that does report 384K shows
   `RAM .../1.0M`.
2. **MB figures round DOWN to a tenth**, so `HEAP 0.0/3.2M` is 12K claimed.
   It is the RAM pair's existing format; a KB figure would not fit the line
   beside a total past 999K.
3. **The `Above 1MB` row costs the memory view's last instance row on a full
   slot table**: one more row was cheaper than re-deriving the window's
   height, and the clip is the one `tm_mrow_open` already does.
4. **Answer 10's read-only map of 640K-1MB is a list, not a second bar**
   (SPEC.md 28.4.9): Video A000 128K, every option ROM with a 55AAh
   signature at its segment and length, BIOS F000 64K, after the programs.
   A list says the address and the size in the window's own columns; a bar
   384K wide beside a 640K one would have needed a scale of its own.

### 14.5 Item 3 as built: the hibernate image's sum, both kernels (2026-10-19)

SPEC.md 87.4.1 is the contract. **The sum covers what the PROGRAMS hold, not
the whole of conventional memory**, and that is a finding rather than a
corner cut: `hbm_wrimg` writes the image LIVE through the file layer, so the
FAT and directory buffers it updates, the stack it runs on, the BDA, the tick
counters and the pointer's save-under are not, at any one instant, the bytes
the disk got. A whole-memory sum, taken before or after, would refuse good
images. A package's memory is what nothing touches between the sum and the
transfer (`[sch_lock]` stops its workers and the UI task is the writer).

**Checked by the RESTORED kernel at `hbm_wake`, not by the stub**: the
records, the routine and the figure are all the image's own, so nothing is
staged and neither the stub nor `kern_dos`'s resume changes. A mismatch
discards the session (pointer and image deleted, a sentence, a key, an
ordinary boot). The code is in `HIBER.DRV`, so it costs no resident byte.

**Gated by `hibsumok` and `hibsumbad`** (MartyPC, an XT with a hard disk):
Mines open across the hibernate, the machine powered off between the write
and the resume, and in the second row 1KB of Mines's region flipped inside
HIBERNAT.IMG on the host. **The broken-on-purpose row was red on its first
run for a real reason**: `hbm_psum` read `mem_tab` through DS, and `mem_tab`
is LOW_SEG's (`.lowbss`), so the sum covered nothing on BOTH sides, matched,
and the damaged session resumed with Mines's window blank. It reads through
SS now. Also green on the new kernel: `hibernate`, `hibernatem`,
`hibernatedrv`, `stikhiber`, `mouresume` and the DOS handoff's returns
(`kdhand`, `kdreturn`, `kdreturnm`, `kdreturngfx`, `kdreturncx`). **Not yet
run on kern_mega in protected mode**, where the same routine reads the
conventional records through identity selectors and skips the high ones; the
park's own Fletcher sum covers those.

**Decisions to look back over:**

1. **A mismatch DISCARDS the whole session** rather than closing only the
   damaged programs. There is no forced close in the kernel (a close runs the
   program's own code, which is the thing that is damaged), so per-program
   recovery would be a new mechanism; the discard is the safe floor.
2. **The kernel's own memory is not covered.** A damaged kernel image fails
   at once rather than quietly, so it is the programs' memory - the user's
   documents - that the sum protects.

### 14.6 Item 4 as built: a program's own int 13h into a buffer above 1MB (2026-10-19)

The file layer already bounced its own transfers (item 85) and `mega_fd13`
already DMAs straight into a high buffer for `dsk_xfer`'s calls. What was
left was a program that issues `int 13h` ITSELF with ES a selector above
1MB: item 33's fence answered it 09h. `mega_hibnc` (`.cold`) answers it
instead, a run at a time through the read-ahead's landing (nine page-safe
sectors) or `dsk_secbuf` when the landing is shed, each run a NESTED
`int 13h` that goes native or is reflected like any other, and copies the
run up or down. A request stays inside one track, so a run advances CL's
sector number and nothing else. IF stays 0 between runs, so no task can take
the landing between the copy and the transfer.

**Measured** under Bochs at 4MB: MEGAXM's new step reads drive A's track 0,
18 sectors (two runs of nine), into a window above 1MB at an odd offset,
compares sectors 1-8 and 9-16 against the same sectors read low, writes the
18 back from the window and compares again: `MEGAXM: i13 ok`, then
`MEGAXM: ok`. On the old fence that step is `no 30`.

**Decision to look back over:** it lives in `.cold` because kern_mega's
`.text` + `.bss` was 33 bytes past the one 64KB segment with it in `.text`.
That segment is now the binding quantity on kern_mega, and the next resident
mega feature will meet it.

### 14.7 Item 5 as built: the speaker in protected mode (2026-10-19)

SPEC.md 34.11.1.1 is the contract. The owner's third option, priced and
taken: the program stays high and **the door writes IRQ0's GATE**
(`OSAPI_FSX_SPK` AL = 2, kern_mega in protected mode only; AL = 0 is
refused there as before, so an older library falls back as it always did).
The door answers the chain, `KERNEL_SEG:sch_isr`, which is a code selector
in protected mode. The close and `fsx_restore` put the gate back and leave
the IVT's cell, which is the BIOS's for a reflection, alone.

**The ISR had to change as well as the door**: a high package's CS is a
read-only code alias, so `os88spk.inc`'s state writes through CS would have
trapped once a sample. The ISR family is now one macro assembled twice:
the real-mode copy is unchanged, and the protected-mode copy writes its
state through ES, loaded with the package's own data selector. 436 bytes
of each of Audio, Tracker, MIDIRack and the Video Player, and no cycle on
kern_big.

A reflected BIOS call puts channel 0 back to the ROM's rate while the door
is open, and the sample's count after, because in real mode IRQ0 is the
BIOS's handler and would count a tick per sample.

**Measured** on the mr286 profile in 86Box: SPKBENCH's speaker rows, which
were skipped on kern_mega before, play at 4,800, 5,512 and 8,000 Hz. The ISR
takes 66, 75 and 108 per mille of the machine (86Box is not cycle-accurate,
so the figures are indicative). The fault log shows **no trap in the ISR**.
kern_big's speaker rows are green: `apspk`, `apspkcovox`, `midirackspk`,
`spkbench`, `trkspk`, `trkspkcovox`, `vidspk`, `vidspk2` (two pulses a
sample), `vidspkcovox`, `spkfx`.

**What it found, for item 8**: `apps/os88spkfx.inc`, the speaker SHAPER that
Audio and SPKBENCH produce samples with, writes eight words of state through
CS for every 256-sample span, so a high Audio pays eight traps per span on
kern_mega (4,112 per site in SPKBENCH's run). It works; it is slow, and it is
the same fix as the ISR's.

### 14.8 Item 7 as built: claims of a megabyte and more (2026-10-19)

SPEC.md 50.3.5 is the contract: `OSAPI_MEM_HUGE` at 0x04B1 (0x04AB until the
elendilon merge renumbered it, item 72), appended to the
table, so an ADDITION and no `PKG_FMT` bump (answer 7). AX = KB, DX = the first
segment or selector, CX = the step: 1000h conventional on every kernel, 8 for
a run above 1MB. On kern_mega it is ONE pool block under consecutive LDT
selectors, the last selector's limit the claim's own end. `mx_unsel` frees
the whole run with the block, by the rule that a claim's block over 64KB has
a selector per 64KB. Cost: the 6-byte cell and a 10-byte body (one of them a
parity pad that keeps `dsk_xfer` even) on kern_big and kern_small; on
kern_mega the body is `.cold` and 31 bytes of `.text` (mx_unsel's count and
three 4-byte far doors).

**Measured** under Bochs at 4MB: MEGAHI claims 200KB in four pieces, writes
and reads each back, frees it and claims it again on the same first selector
(`MEGAHI: huge 2844 step 0008 ok`), and the rest of MEGAHI passes.

**Decisions to look back over:**

1. **The run is taken for `OS88_HIGHMEM` packages only.** That header bit
   already means "my claims may be selectors", so a package that walks by
   CX but did not declare it gets a conventional claim with step 1000h,
   which is still correct; it only cannot have more than the heap holds.
   Taking the run for any caller of the cell would also have needed the
   free path to recognise a non-HIGHMEM package's high block.
2. **A huge claim does not regrow above 64KB and is not compacted.** Regrow
   would mean moving the run and re-aiming every selector. Compaction above
   1MB is item 9, and the run is what it will have to move: the selectors
   stay fixed and only their bases change.
3. **The pieces are contiguous**, because the block is. Answer 12 noted they
   need not be, which would let a claim mix conventional and extended memory
   and let compaction make 64KB holes only. Contiguous is the simple first
   version; item 9 can relax it, and nothing a program sees changes.

### 14.9 Item 6 as built: the pointer moves through a reflected BIOS call (2026-10-19)

An audit of the mouse ISRs' whole path came first: `mou_isr`, `mou_isr3`
and `mou_p2_isr`, through `mou_apply`, `cur_move`, every adapter's renderer
and the save-unders. It found the cheap version true. Every
selector-dependent load on that path is `[k_dsel]`, there is no far call
into `.cold`, and no `[cs:]` write. The real-mode IVT already names the
kernel's ISRs at 23h, 24h and 74h, because `mega_enter` and
`mega_ivt_sync` copy 08h-0Fh to 20h-27h. Only the reflection's mask kept
them from running. So:

- `[k_dsel]` is KERNEL_SEG for the reflection, set where real mode begins
  (`.rmc`, shared by the 386 path and the 286 reset path) and put back where
  protected mode resumes, with IF = 0 throughout.
- `mega_rmaskf` (`.cold`) takes the mouse's line off the mask, and only
  when its ISR can run: the mouse has settled, it is not fed by a driver
  (whose ISR may be an image above 1MB), the real-mode vector is the
  kernel's (a thief the theft repair has not synced), and the call is not
  `int 10h`, where the arrow must not draw in the middle of a mode set or a
  font load. SPEC.md 7.4's gates apply inside a disk transfer as on kern_big.
- The mask is BANKED, `[mega_rmask]`, and `mega_r286` compares the masks
  this reflection actually set. Its constant 3FB8h would have read every
  286 return with the mouse open as a BIOS that re-initialised the 8259, and
  the re-initialisation is the lost-edge, dead-mouse failure the mask exists
  to avoid.

**Measured** on the mr286 profile in 86Box with MEGAI13 (tests/megai13),
forty 18-sector `int 13h` reads back to back by a package, every one
reflected. The harness sends forty mouse packets of +5 during them. **Before:
the pointer did not move at all**, every packet lost. **After: 200 pixels**,
every packet. MEGAXM, MEGAHI and MEGAI13 pass under Bochs, whose mouse is
PS/2, so the IRQ12 arm is covered too. kern_mega's `.text` grew 18 bytes; the
segment has 2 bytes left in it (65,534 of 65,536).

**Left as found, for the owner to look back over:**

1. **A BIOS that runs `int 13h` on a stack of its own** (SeaBIOS, so QEMU and
   Bochs) puts SS somewhere else when the mouse ISR interrupts it.
   `MOUPRIV_ENTER` correctly declines to swap stacks, but `gfx_rowbase`
   and `evq_push` address LOW_SEG through SS. The same hazard exists on
   kern_big today through SPEC.md 7.4. Real IBM and AT ROMs do not switch
   stacks. The cure is a small real-mode stub that sets SS for the ISR, and
   it is not built.
2. **`[ticks]` stands still during a reflection** and is credited after it,
   so two clicks inside one long reflected call carry the same birth tick.
   Minor.
3. **kern_mega's segment is full.** `.text` + `.bss` is 2 bytes short of
   64KB, so the next resident kern_mega feature needs a size pass first or
   goes in `.cold`, as both of this round's did.

*(2026-10-22: 1 is resolved by 14.39 item 3 - the mouse ISR loads its own
SS (SPEC.md 9.10.1) - and 3 by 14.33, kern_mega's own `.mega` segment. 2
stands as written.)*

### 14.10 A size pass on kern_mega's segment (2026-10-19)

Items 4 to 7 left kern_mega's `.text` + `.bss` 2 bytes short of the one 64KB
segment, so item 9 could not start. The CLAIM FAMILY moved to `.cold`:
`mega_claimhf`, `mega_claimfreef`, `mega_claimkbf` and
`mega_claimregrowf`, with their two helpers. Every entry was already far
and reached from `.cold`, they run only in protected mode, and their
`.text` helpers are reached through four-byte far doors (one new,
`mega_xmself`). The segment went 65,534 -> **65,172 bytes**, 364 spare.
MEGAHI (claim, regrow in the pool and past 64KB, free, huge), MEGAXM,
Notepad, TexPad and Sheet pass under Bochs at 4MB.

### 14.11 Item 11 as built: kern_mega's 1.2MB system disk (2026-10-19)

`make mega` builds `build/mega120.img` beside `build/mega.img`. It has the
same files, 798 of 2,371 clusters, and its own boot sector for 15 sectors a
track (`$(MEGADIR)/boot120.bin`, the rule kern_big's 1.2MB disk uses). There
is no 360KB or 720KB kern_mega disk, by answer 19.

**Booted** on `vm/286-525` in 86Box, the only profile with 5.25" HD drives.
It is an AMI 286, given 2MB because at 1MB that board reports no extended
memory and kern_mega would stay in real mode. Result: the desktop, both
5.25" drive icons, and `[k_dsel]` = 0010h, which is protected mode.

### 14.12 Item 8, part 1: the C packages' claims high, ModPlug on the grid (2026-10-19)

**The C SDK.** Three things in it kept a C package's claims low.

- `apps/os88partsbody.inc`'s two part claims (`op_claim`, `op_fetch`)
  address parts by paragraph arithmetic on the carve (`add ax, [op_base]`).
  They now claim through the DMA doors with CX = 0, which stay conventional
  on kern_mega and are an ordinary claim everywhere else. The same goes for
  the overlay claim in `apps/cc/crt0.asm`. A part is the package's own code
  and data, so "high" for it is "re-home high" (below), not a selector.
- **`os88_file_read_segoff` / `os88_file_write_segoff`** (new,
  `apps/cc/os88.h`). These are the seg:off form of the `_seg` pair. A
  package that wants a file at a place INSIDE a claim was writing
  `seg + (off >> 4)`, which is arithmetic on a selector on kern_mega. Now
  it names the claim and the offset. The rule is the same: the offset keeps
  the 512-byte alignment the `_seg` calls ask for.
- **`os88_mem_huge` / `os88_mem_huge_step`** (new): item 7's cell for C.
  The step is what a package adds to a selector to reach the next 64KB,
  and 1000h (a paragraph step) on every other kernel.

Each new thunk is gated like the rest (`CC_U__*`), so a package that does
not call it carries none of it.

**The packages.**

- **LOOM**: a source slot was `lm_srcseg + slot * 384` paragraphs. It is
  now an offset (`lm_soff`), read and written with the `_segoff` pair.
- **RunCPM**: its three reads into the Z80 claim (AUTOEXEC.TXT and the two
  transient loads) were `rc_zseg + (off >> 4)`. They now use
  `_segoff` at `rc_zseg:off`. The host harness `rcuitest.c` models the new
  call as the paragraph form.
- **Apple II, C64, CWORD and WEAVE** needed nothing beyond the SDK.
- All six now declare `OS88_HIGHMEM`. **PaccMan** claims nothing it could
  move, so it is unchanged.

**Verified.**

- `make` and the C toolchain build all six.
- A Bochs sweep at 4MB opens all six on kern_mega.
- RunCPM was run with a real `A/0` and an AUTOEXEC.TXT that runs INFO. The
  CCP took the poke and INFO.COM loaded and ran on every warm boot, which
  is RunCPM's own behaviour: both `_segoff` reads, into a 64KB claim that
  is a selector there.
- LOOM opened FORM.WML on kern_mega. One character was typed and File ▸
  Save answered "Saved FORM.WML, 919 bytes". The edited byte is on the
  disk image.
- `tools/os88bochs.py` gained `--put FOLDER:path`, and its third click now
  skips the folders `--put` makes in RUN/.

**ModPlug was faulting on kern_mega whatever HIGHMEM said.** Its patterns
lived at blob + 67 + 64p paragraphs and its samples at blob + (start >> 4),
which are both off kern_mega's 8-paragraph grid of conventional
descriptors. This is item 18's Tracker defect again in a package that was
never given Tracker's fix. It now has Tracker's layout:

- patterns at blob + 64 + 64p, offset 60;
- samples at blob + 8·(start >> 7), offset start & 127.

Every pair names the same linear address it did, so real mode reads the
same bytes. The mixer's bound moves from ≤ 60,015 to ≤ 60,127, against the
64,015 its own comment allows. No row in the suite plays a module through
ModPlug, so this is that arithmetic, not a measurement.

**Decisions for the owner to look back over.**

1. Parts are kept conventional rather than given selectors. A C package's
   parts are its own code segments, and lifting them is the re-home-high
   step below, not a claim flag.
2. The LOOM and RunCPM changes are generic: the same source on every
   kernel, a few bytes each. They are well inside answer 8's 512-byte rule.

**What the audits found for the rest of item 8.** None of these are built
yet, and every byte count is an estimate from reading the code.

- **Two more existing kern_mega faults, conventional or not:** MIDIRack's
  wavetable samples (`mwt_dseg = bseg + doff >> 4`, then `+ MRS_PARA`), and
  Frotz's picture band (`zp_bseg = zp_seg + offset >> 4`). Both are
  paragraph steps off the grid.
- **A "flat arm" for claims of 64KB or less** in Frotz, Tracker, ModPlug
  and MIDIRack. The granule trick `base + 8·(A >> 7)` is right for an
  identity selector and wrong for an LDT one. A claim of 64KB or less that
  came back a selector (`test dl, 4`) is addressed as segment = claim,
  offset = the absolute address. Claims over 64KB stay conventional by the
  contract, so the granule paths stay right for them. About 60-80 bytes a
  package; kern_big never takes the arm.
- **1942, Excitebike, Tank and Paint**: segment arithmetic on their own
  claims, plus `OSAPI_MEM_CODESEL` where compiled code is far-called out
  of a claim. PiXEL stays as it is.
- **`apps/os88spkfx.inc`**: about eight CS writes a span. That is a speed
  item rather than a placement one, and it is fixed generically by
  assembling the writers twice, the way `OS88SPK_ISRS` is.
- **The seven kept low.** All five re-homers' loaders are clean. A high
  loader buys nothing, though, because the PROGRAM runs in the
  conventional carve, and the loader step that frees it (8a) LEAKS a high
  loader's pool pair: `mem_free_x` is handed a selector, finds no record
  and the carry is ignored. The prize is a kernel **re-home high** step
  (~50-70 bytes of `.cold`), and its open question is how much of the
  carve to lift, since Word and Scribe address part 1 inside their own CS.
  Redline (its E820 buffer, `int 15h` writing to a high DS) and the Video
  Player (`xg_gap` measuring from DS) are each a few bytes from
  `OS88_HIGH`.

### 14.13 Item 8, part 2: Frotz, Tracker, ModPlug and MIDIRack high, with their claims (2026-10-19)

All four now declare `OS88_HIGHMEM`. The rule that makes it safe, written
once per package:

- **A claim over 64KB stays conventional by the contract**, so those keep
  their 128-byte granules, `base + 8·(A >> 7)`.
- **A claim of 64KB or less may come back a selector** (bit 2 of DX, the
  LDT; a conventional base is a multiple of 32 paragraphs, so the bit is
  clear there). That one is addressed **flat**: segment = the claim,
  offset = the absolute address. The granule trick is only right for an
  identity selector; added to an LDT selector it lands on another entry.
- **A selector faults past its limit where a segment reads garbage.** So
  each package makes sure every offset its inner loop can form is inside the
  claim, which is the one place the four differ.

| package | what was flat-armed | the limit rule | bytes |
|---|---|---|---|
| Frotz | `zm_map`'s high arm, `zm_addr`, `zm_norm`, `@verify`'s walk | a story claim that comes back a selector under 64KB is freed and claimed again at 64KB: a story may name any 16-bit address | +93 |
| Tracker | `mp_patseg` (one helper for the three pattern sites) and each sample's base | `trk_claimsel`: a module of 62KB or less gets a whole 64KB selector; one of 63-64KB is claimed at 65KB, which is conventional (the mixer assumes a limit of 64,015 or less); the trim is skipped on a selector | +78 |
| ModPlug | the same, `mpm_patseg` and `mpp_claimsel` | the same | +84 |
| MIDIRack | `mwt_place`: each voice's segment and start, from the bank's 32-bit data offset; the bank's 32KB chunks step by offset | `mwt_lim`: no voice's end past the claim on a selector | +104 |

Every byte count is the image against the commit before. All are generic
code, under answer 8's 512 bytes, and on kern_big a flag test per seek,
row or note-on.

**Three faults that were there whatever HIGHMEM said, now fixed:**

1. **Frotz's picture band** was `zp_seg + offset >> 4`, a paragraph step
   off the grid. It is now `zp_place`, the same two arms.
2. **MIDIRack's wavetable** had samples at `bseg + doff >> 4 + para`, the
   same defect. Reproduced first: the commit before this one, given the SB
   wavetable on kern_mega, stops with `kern_mega FAULT 0D err 19E4`.
3. **Frotz never found a story's pictures when it was opened from the
   dialog, on EITHER kernel.** `zi_load` copied the story's name with
   `rep movsb` without setting ES, and inside a callback ES is the kernel's
   (`KERNEL_SEG`, or 0010h on kern_mega). So `zi_story` stayed empty, no
   `.PIX` was looked for, and 13 bytes of KERNEL data were overwritten on
   every open. `make zgfx` never saw it because its harness opens the story
   another way. Found by E9 tracing on kern_mega. It is now
   `push ds / pop es`.

**A fourth, in MIDIRack, also on every kernel:** `mrb_start` built the
stream's open flags in BL and THEN called `OSAPI_SND_CAPS`, which returns BL
as an output. The flags reached the driver as 81h, with no EXT and no ring
size, so the external ring of SPEC.md 34.5.3 has never been used and every
card play took the grant ring. The flags now go into BL after the call
(0 bytes).

**Verified under kern_mega:**

- **Bochs at 4MB:**
  - Tracker plays a generated 30KB module through a selector. The SB
    capture has exactly the 7.7 s of signal its one note pattern holds.
  - Tracker plays the 116KB BEVERLY.MOD through granules.
  - ModPlug plays both, opened from its own dialog.
  - Frotz draws `zpictest.z6`'s three pictures at the sizes
    `zpictest.expect` names.
- **86Box (the owner's mr286 profile, 4MB, SB16):** MIDIRack plays a song
  through the SB wavetable with the synthetic 6,872-byte bank, so the bank
  is a selector. Bochs could not show this one: its SB16 underruns every
  pass and ends the song, on the old code as on the new.
- **kern_big:** `midirackwt` on MartyPC and `make zgfxpic` both pass.

**What is left of item 8** (§14.12's audit list):

- the graphics packages: 1942, Excitebike, Tank and Paint;
- `apps/os88spkfx.inc` and MIDIRack's patched `adc` immediate. Both are
  CS writes in a hot loop: traps on kern_mega, a speed item rather than a
  placement one. 86Box's fault log shows the `adc` trap firing per span;
- the re-homers and the "re-home high" kernel step;
- Redline's E820 buffer and the Video Player's `xg_gap`.

**Decisions for the owner to look back over.**

1. **A small claim that comes back a selector is RE-CLAIMED at 64KB**,
   rather than every inner loop bound-checked against the selector's limit.
   It costs up to 64KB of extended memory per open document and nothing in
   the loop. On kern_big it never happens.
2. **A module of 63-64KB is claimed at 65KB on kern_mega**, so it is
   conventional and takes the granules. The mixer's own bounds are written
   for a limit of 64,015 or less, and changing them is a hot-loop change
   for two KB of module size.

### 14.14 Item 8, part 3: the graphics packages, Redline, the Video Player, and two kernel holes (2026-10-19)

**Who now says what:**

- `OS88_HIGH | OS88_HIGHMEM`: 1942, Excitebike, Tank, Paint and Redline.
- `OS88_HIGH` alone: the Video Player. Its ring is paragraph arithmetic
  over 34 sites, and lifting it would need OSAPI_MEM_HUGE's step.

The four graphics packages were each done by an agent in a worktree and
merged here. Their records follow, with Paint's in full.

**Two kernel holes, found by Redline running above 1MB:**

- **`mega_svc`'s return to protected mode reloaded DS but not ES.** ES
  still held the segment the BIOS returned, and the `push es / pop es`
  around the tick read then loaded it as a selector. For a caller whose
  ES was above 1MB that is FFFFh (`mega_s2seg` gives the BIOS FFFFh for a
  high selector), so it faulted. ES now gets the kernel's data selector
  with DS. The caller's own ES comes back from the frame at `.out`, as it
  always did. This costs 2 bytes of `.text`.
- **`mega_gpfix`'s `.stale` arm then faulted inside the handler.** It
  indexed the LDT with what was a GDT selector and ran off the alias. It
  now refuses anything that is not an LDT selector inside the LDT.
- **Re-home step 8a** handed `mem_free_x` a high loader's selector,
  ignored the carry, and leaked the pool block and both LDT entries on every
  launch. It now frees the pair as `ld_unreserve` does. No shipped loader
  says `OS88_HIGH` yet, so this was latent.

**Redline:**

- The E820 entry is on the task stack. Placed in DS, it read back as no
  memory at all, through FFFF:DI.
- The "RAM twin" reference times LOW_SEG instead of its own DS.
- Measured under Bochs at 4MB: the whole benchmark runs, E820 reads Usable
  3,708KB, and Free conventional goes 499KB -> 559KB because Redline
  itself has left it. The commit before this one, running low, also ran
  clean. Redline running high faulted until the kernel fix above.
- -17 bytes.

**Video Player:**

- `xg_gap` (`apps/os88xgap.inc`) timed its copies from DS:0 as the
  conventional end. It now takes an 8KB scratch claim at the DMA door,
  which is conventional on every kernel.
- `vp_erate` times SS instead of DS.
- The Live demo plays on kern_mega with the package above 1MB.

**1942** (+97 bytes):

- The splash's band arithmetic is now offsets. A selector canvas
  decompresses each band through a 3KB scratch claim, because
  `OSAPI_DECOMP` wants ES:0.
- The compiled CGA sprites are far-called through `OSAPI_MEM_CODESEL`.
- `n_pushall` no longer stores its return through CS, which was item 56's
  trap per call.
- Under Bochs: the splash draws, a VGA game plays, and Esc returns.

**Excitebike** (+2 bytes):

- The art-part claim and the compiled-pose claim go through the DMA door
  with CX = 0. Both are paragraph arithmetic, and a HIGHMEM claim is a
  data-only selector that `OSAPI_MEM_CODESEL` refuses.
- The poses' code selector is set once, at load.
- Its other claims may go high.
- Under Bochs: the title and a race with the riders drawing.

**Tank** (+26 / +28 small arm):

- The HUD template was `shadow + 3E8h` paragraphs in one claim. On a
  selector it becomes a claim of its own, and the shadow claim stays 32KB
  because the device-row table sits at a constant offset in its tail.
- Under Bochs: it plays in Mode X, which takes no claim.
- On 86Box's AMI 286 with a CGA card and 2MB: it plays on the CGA shadow
  path, with the HUD panel drawn from the template, and nothing faults
  beyond the 286's own reflection resets. 832KB of the pool was free, so
  the 32KB and 16KB claims should have come from it, but their selectors
  were NOT read back.

**Decisions for the owner to look back over:**

1. **Re-home HIGH is deferred, not refused.** The five re-homers' loaders
   stay low, and so do their programs. A program's image plus bss is
   51-61KB (Skies 61,409; Word 59,392; Scribe 51,712; Pixelstein's game
   58,263), and the carve it runs in is bigger. Lifting a whole carve under
   64KB therefore almost never applies. Lifting image plus bss alone leaves
   the rest of the carve conventional at a segment the program is not
   told, which is an ABI addition, and Word and Scribe reach part 1 through
   their own CS. The gain is at most 64KB a program, for five programs that
   are not background applications. What would make it worth doing is a
   carve layout that puts the program last.
2. **Tank on kern_mega holds 48KB above 1MB a game, of which about 16KB
   is unused.** The shadow claim stays the size kern_big's blit needs.

**Not yet seen on the glass:**

- Paint's canvas resize across 64KB. kern_big's `paintmove` and
  `paintshrink` cover its real-mode arm. *(2026-10-22: seen - 14.46.1,
  under Bochs.)*
- 1942's CGA fullscreen and its cartridge build. *(2026-10-22: still
  unseen, 14.46.6: no emulator here gives kern_mega a CGA.)*

#### Paint

Paint declares `OS88_HIGH | OS88_HIGHMEM` now. 14.13's rule, applied to the
one package whose canvas is not ONE claim walked one way but five claims
and two layouts:

- **The row table is decided by the LAYOUT, not by the claim.** A canvas
  whose layout fits one segment (`[pt_cvparas]` <= 1000h) is laid out
  **flat**: every `pt_rowseg[y]` is `[pt_base]` and `pt_rowoff[y]` the
  whole offset. A bigger one keeps the 128-byte granules, and its claim is
  over 64KB and so conventional. Deciding by the layout is what keeps the
  undo image to one table: flat, `rowseg + [pt_undelta]` is exactly
  `[pt_unseg]` whichever of the two claims is a selector; granular, the undo
  claim is the canvas claim's size and so conventional too. Deciding by the
  claim would have broken the one mixed case the pool can produce - a
  conventional canvas of 64KB or less (the pool was full when it was
  claimed, or a regrow moved it) with a selector for its undo image.
- **`pt_orowset` and `pt_mstep`** (the resize's copy-back) answer a
  selector flat and everything else on granules. **Both were one PARAGRAPH**
  before this - item 59's defect, missed because only `pt_srowset` faulted
  on the census disk - so a canvas resize could fault on kern_mega whatever
  HIGHMEM said. `pt_cvmove` puts the destination pair onto granules once,
  the flat layout's offsets being too large for `pt_mstep`'s signed step.
- **`pt_srowset`** (a staged BMP's rows) addresses a selector staging claim
  flat.
- **Limits.** The canvas and undo claims end at least two bytes past the
  pixels (the header is 2 mod 4 and the stride 0 mod 4), so `pt_cb_grab`'s
  `[es:si+1]` read stays inside. `pt_bmp_pal` read the palette at
  `14 + biSize` with nothing checking it against the file, and now stops at
  the bytes read; `pt_stagefit` keeps one byte past the file, which
  `pt_gbits`' word-and-a-byte read can touch, and no longer regrows UP when
  a file filled the claim. The GIF writer was already capped at its claim,
  the clipboard and the fill stack index inside theirs, the LZW tables are
  16KB exactly by assertion.
- **A fifth, on every kernel:** `pt_blit_1`'s band height was `65520 /
  stride`, written for 0..15-byte row offsets; with 128-byte granules a band
  of a canvas over 64KB at the widest strides could read up to 111 bytes
  past its segment. It is `65408 / stride`.

+112 bytes of `paint.bin` (26,969 -> 27,081), +115 on the small arm (17,215
-> 17,330); no kernel byte. On kern_big a canvas of 64KB or less is now
flat, which is the same bytes through the same instructions; the costs are
a test per row of a BMP decode, two per carried row of a resize, and one per
palette entry of a load - nothing per pixel. The selector arms are never
taken there.

### 14.15 Item 9 as built: the pool compacted above 1MB (2026-10-19)

**When it runs.** `mx_alloc` compacts once on any claim that finds no run
of the size it wants, and then tries again. That covers a package region
(80h|slot), a program's claim (0A0h|slot), a driver's claim (0C0h|row)
and a huge claim's single block. The kernel's own blocks (MX_KOWN) come
from the top and never need it. The claim side is untouched, because
every door already goes through `mx_alloc`.

**What it does** (`mx_compactf` / `mx_cpmove`, kern_mega `.cold`).

- The blocks are walked in ascending order of where they are. A cursor
  follows them:
  - a **pinned** block raises the cursor past itself;
  - a **movable** one slides down to the cursor, which is always free
    space, every block below it having been placed already.
- Each move copies the block down 32KB at a time. A forward copy is safe,
  because the destination is lower.
- It then **rebases every present LDT descriptor whose base lies inside
  the block**. That one scan finds a pair's two halves, a claim's
  selector, a regrow's scratch window and a huge claim's run, without
  knowing which is which.
- Each block's copy and rebase run with interrupts off, so no ISR sees half
  a move. That is up to 64KB at a time, about 10 ms on a 12MHz 286.

**What moves:**

| Moves | Stays |
|---|---|
| package regions (80h\|slot) | the kernel's own blocks (MX_KOWN), which are the system at the top |
| programs' claims (0A0h\|slot) | `OSAPI_XMEM_ALLOC`'s blocks (owner = a slot): the package holds the LINEAR address, and `os88ub.inc`/`os88la.inc` read it in place, which is what answer 12 said pins the pool |
| drivers' claims (0C0h\|row) | a huge claim (over 64KB): moving one with interrupts off for that long is not worth it until something needs it |

**Why no relocation proc is owed:**

- The selector a package holds never changes; only its descriptor's base
  does.
- A segment register picks the new base up at its next load. Every holder
  outside the call is a PUSHED register, which reloads on its pop: a
  suspended task's frame, a cell's `push ds`, an interrupted ISR's
  registers, a far return address.
- Inside the call the compactor reloads ES on its way out. DS is the
  kernel's and CS the kernel's.

`apps/os88api.inc`'s `OS88_HIGHMEM` note says so: "a pool claim MAY move…
but its SELECTOR never changes".

**Cost:** about 360 bytes of `.cold`, which is above 1MB on kern_mega, and
30 bytes of `.text`: the retry, one far door and the flag. kern_big is
byte-identical.

**Verified.** `tests/megacp` (MEGACP), under Bochs at 2MB:

1. It fills the pool with 60KB claims, then the tail with smaller ones,
   until 0 KB is free.
2. It frees every other 60KB claim, leaving 360KB in 60KB holes.
3. It claims 64KB. The compactor moves eight blocks down, from KB 121,
   241, 361, … to 61, 121, 181, … The claim comes back as selector 284Ch.
4. Every surviving block's stamps (offsets 0, 30,000 and the last word)
   read back intact through its unchanged selector, and the new block takes
   writes at both ends.
5. MEGARUN carries on to `done`, and no fault is reported.

**Still open:**

- A region moving while its OWN program is the caller is covered by the
  argument above (the cell's `retf` reloads CS), but MEGACP's region sat
  below everything it claimed and did not move. A test that starts a
  second high package first would put it in the path. *(2026-10-22:
  resolved - 14.46.5 runs it, `tests/megaown`, row `megaown`.)*
- Nothing yet gives a program the LINEAR address of an `OS88_HIGHMEM`
  claim. If a cell ever does, that claim must be pinned the way
  `OSAPI_XMEM_ALLOC`'s are.

**Decisions for the owner to look back over:**

1. **Compaction runs inside the claim, interrupts off a block at a time.**
   That is the conventional compactor's place (`mem_claim`'s refusal path),
   not a background pass. A serial mouse can drop bytes during a long one:
   at 1,200 baud a byte is 8.3 ms, so one 64KB move on a slow 286 is about
   one byte.
2. **Drivers' own IMAGES stay pinned** (MX_KOWN), and so do huge claims.
   Both could move by the same rebase, but nothing needs them to yet.

### 14.16 Item 10 as built: the Video Player's native protected-mode path (2026-10-19)

Two parts, in the order 14.2 asked for them. SPEC.md 98.3.18.16 is the
contract for the second and carries the first's summary.

**Part 1: no write through CS on a path that runs per frame or per read.**
14.3 left a play paying ~5,500 #GPs that `mega_gpw` completed. Measured
again here under Bochs with `make megatrap` and `tools/megatraps.py`, the
counts taken between the Play click and the end of the play:

| play (Bochs, 4MB, SB16) | before | after |
|---|---|---|
| `apps/video/demo/cga/11-LIVE.V88`, Live on the desktop, 300 frames | **7,048** | **0** |
| a 640x200 LIN80 clip (`tests/vidxip.py`'s), in the window, 182 frames | **2,002** | **0** |
| a VGA4 SUB2 write-mode-3 clip, window, full screen and a seek's toast | crashed (below) | **0** |

(The Live play's four other lines are SOUND.DRV's `null0` at start, not the
player's.) What they were, and what replaced each:

- **`vdec.inc`'s list map** - `[cs:vd_map]` stored and shifted at every
  list's head: eleven a frame on every play. The map is DX through the six
  poke lists, which never touch DX, and a word on the stack through the
  four span lists, which count rounds in DX. This is the decoder's own inner
  chain, so it was measured on kern_big too: MartyPC's 5150 running
  `tests/vidbench` against a synthetic `VIDBENCH.DAT` (`os88vid.py synthxdv`
  + `benchdat --synth`), base against new, `nat ram` cycles a frame:
  empty 1,440 -> 1,273, P1 x400 18,066 -> 17,892, SL16 x150 44,592 ->
  44,404, RUN40 x60 24,682 -> 24,506 - **every one of the 16 frames
  ~150-180 cycles FASTER**, the pictures checked equal on both paths. A
  `shr dx, 1` is 2 bytes where the memory shift was 5. The VD_PAR pads were
  re-derived so every `.seg` and round head keeps the parity it had, with
  one exception noted in the source: `vd_n_run`'s gate reaches
  `vd_n_slicel` at exactly 127 bytes, so no pad goes in front of the long
  lists, whose heads were never aligned to begin with.
- **`vp_rbnd`** (a resident play's load-time check, six a record): the map
  in DX, the bound read from CS where it is compared.
- **`vp_lrget`** (a Live frame's blit runs, five a call): DS is ours and the
  record is read through ES (`es lodsb`), so the runs are stored with no
  prefix at all; `vp_decrec` hands it the record's segment it pushed.
- **`vp_khdr`** (a key in parts): likewise, the read through ES.
- **`vd_walk` and the text box's clip** (`vosd.inc`): their walk state, and
  `vd_walk`'s routine and colour (which were `vw_fn` and `vp_m3c` at CS), are
  BP frames on the stack, the origin the frame's first word. +14 bytes of
  stack at the deepest; the hook runs on the UI task's stack in a bracket.
- **Four bytes a sub-record** (`vc_mask`, `vp_m3on`) go through `VP_OWNB`, an
  ES load of `[vp_self]` - our own segment, kept by the region's relocation
  proc (`vp_reloc`, which was `vp_noreloc`'s bare `ret`).

The LOADALL and loaded-base machinery (`vp_xbeg`, `vp_xld`, `vd_yield`,
`vp_xmk`'s handle table) still writes through CS - it is never reached
under `CPU_F_PM`.

**Two crashes found on the way, both on kern_mega only:**

- `vp_zero` faulted at a VGA4 RAM image's plane 1: `[vp_plsp]` is the plane
  spacing in paragraphs, `h x 80 / 16` = 500 for a 160x100 canvas, and a
  segment off the 8-paragraph grid is not a selector. Rounded up to 8 under
  `CPU_F_PM` alone, so a real-mode image is the size it was.
- `HDD.DRV`'s `HDV_ARM` ran its A20 probe (`mov ds, FFFFh`) under protected
  mode at the player's first fill of a streamed file: a #GP the kernel could
  not complete, `kern_mega FAULT` in the driver. It refuses under `CPU_F_PM`
  now (AX = 1, "not this machine", which the player banks as for good): the
  read straight into XMS is a loaded-base trick with no protected-mode
  meaning. +6 bytes of image inside its 4,096.

**Part 2: the hold and the bank read through selectors.** The owner's
answer was a >64KB claim read directly, and `OSAPI_MEM_HUGE` was the
suggested primitive. **It does not fit the reader**, and the reason is the
whole design: its run is a window every 64KB, so a super-packet or a record
that straddles a 64KB boundary of the store - which a file held at its own
offsets does wherever it likes - is cut in two, and the decoder has no way
to cross a segment mid-record. `OSAPI_XMEM_SEL`'s one window per block can
be aimed anywhere but only by the UI task, under the hook's feet. What a
reader of records of at most 32KB needs is a window every **32KB**, each
64KB long: any 32KB of the store is then wholly inside the window of
(offset >> 15), at (offset & 7FFFh). That is **`OSAPI_XMEM_RUN` (0x04B7)**,
appended at the table's end - no `PKG_FMT` bump - over the XMEM block the
player already takes, so every linear-address path it has (the fills' copies
up, the bounce, `xg_gap`, `vp_xok`'s compare, the layer's bank) is
untouched. `vp_xmk` turns the linear address into that selector; it is
below `VP_XSEL`, so every `VP_DSDX` is a plain `mov ds, dx` and `VP_DSEND`
nothing: no borrow, no interrupts off, and no yield (a selector survives
every interrupt; the yield sites stay jumps). The card says `XMS in place
(PM)` / `XMS bank in place (PM), N KB`.

Measured under Bochs: the 794KB LIN80 clip held whole at 4MB plays
**240/240 in place, 0 stalls, 0 late, 0 #GP**; at 2MB it is banked (672KB,
so the bank wraps and the mirror is read) and plays the same; each play's
last picture is identical to a `VP_NOXIP` build's copying play
(`Held whole in XMS` / `Banked in XMS`). The SUB2 clip's held play goes in
place too, with the toast drawn round in full screen.

**Sizes** (bytes; `video.bin`, the image):

| arm | before (protected-mode at bbcd7fbc) | after part 1 | after part 2 |
|---|---|---|---|
| plain | 57,783 | 57,660 | 57,840 |
| `VPDIAG=1` | 59,347 | 59,224 | - |
| `NOLIVESND=1` | 57,550 | 57,431 | - |

bss unchanged (2,484). Kernels, measured section by section against
`origin/protected-mode` with the same build number: **kern_big `.text` +6,
kern_small `.text` +6** (the cell; its body is `osapi_xmem_sel`'s refusal),
**kern_mega `.text` +34, `.cold` +273**, `.bss` +0 on all three. kern_mega's
segment binds: the first spelling was +40 of `.text` and took
`make megatrap` (MEGA_TRAPLOG) over `KERN_CODE_MAX`, so the cell's
`[mega_on]` test and `mx_unsel`'s run arithmetic live in `.cold`
(`mx_runcntf`). `.bss` now ends at FF74h: 140 bytes of the segment left.

**The probe (`xg_gap`), with numbers.** It measures a COPY between
conventional and extended memory against the board's interleave - up to
1.8x on the Victory (SPEC.md 41.14; 98.3.18.12 measured a drained 32KB
chunk 11.2 -> ~6.6 ms placed at the gap). In place, a held file makes **no
copy during a play** (25 copies of 32KB down for this 794KB clip, every
play, are gone), so the ring needs no gap slack, exactly as on the 286
(`vp_xgpad` already skips for `[vp_xip]`). A bank in place still reads
each chunk into the bounce and copies it UP - one copy a banked byte where
the copying bank made two - and that copy is the one the gap places, so it
is kept for it. The decode's reads through the selector are a sequential
stream from extended memory into the adapter, with no second RAM end to set
against the first. **The probe stays, and applies to the copies that
remain.**

**Decisions for the owner to look back over:**

1. **A new cell, not `OSAPI_MEM_HUGE`.** 64KB windows cannot serve a
   reader whose records straddle them; a 32KB-step run of 64KB windows can.
   I put it on `OSAPI_XMEM_ALLOC` blocks rather than on huge claims because
   every linear-address path in the player (fills, bounce, gap, layer bank,
   `vp_xok`) then stays as it is. The alternative - the store as a huge
   claim with overlapping windows - would also need the player to say
   `OS88_HIGHMEM`, which 14.14 found it cannot yet (34 sites of paragraph
   arithmetic on its claims).
2. **One view a block.** A block with a run refuses `OSAPI_XMEM_SEL` and
   the reverse, so `mx_unsel` knows what to free from one word (`MX_SEL`
   with bit 0 set marks a run). An XMEM block never moves, so no rebase is
   owed.
3. **The bank in place keeps its bounce.** The disk layer takes an LDT
   selector as a buffer but bounces it a sector at a time through
   `dsk_secbuf` (`dsk_runcap`'s `test al, 4`), which on a BIOS disk is a
   call a sector where the bounce is a call a run. Reading straight into
   the run is the next step and wants a 286 to price it.
4. **The speaker's clock is LIFTED in place under `CPU_F_PM`** (14.16.1),
   for the hold and for the bank. Both refusals were about interrupts held
   off - a LOADALL or loaded-base borrow for the whole decode (98.3.18.14),
   and a BIOS block move for every copy (98.3.18.5) - and neither happens
   here: a selector is a `mov ds, dx`, and kern_mega's `OSAPI_XMEM_COPY` is
   a `rep movsw` between two descriptors under `[sch_lock]` alone.
5. **The shadow is LIFTED in place under `CPU_F_PM`** (14.16.1). Its
   refusal was that its decoders have no yield sites, and a selector needs
   none.
6. **The plane-spacing rounding is `CPU_F_PM`-only.** A generic rounding is
   five bytes smaller and costs every real-mode VGA4 image up to 3 x 127
   bytes; I kept kern_big's images the size they were.

**kern_big rows, once at the end** (`os88soak.py start -k ...` on the
final tree): `vidplay vidplayvga vidplayshd vidfskeysvga4m3
vidfskeysvga4pl vidfskeysmodex vidlive vidlivevga4 vidresident vidkpart
vidxms vidxip vidxipbank vidxiphd vidposter vidscreeno` - **16/16 ok**.
`vidplay`'s timing leg (all frames, no stall, no late period, ticks within
2) is the on-time check on MartyPC's 8088; `vidxip`/`vidxipbank` are the
386 in-place paths through the changed `vp_xmk`; `vidxiphd` is HDD.DRV's
arm on a real-mode 386.

**Seen on a real 286 (86Box, 2026-10-19):** the AMI NEAT 286 profile
(`vm/286`, 12.5 MHz, 4MB, no card) running `mega.img` at bbcd7fbc plus this
work, LIN80.V88 (794KB, 640x200 VGA, 24 fps, silent) off a 1.44MB B:. The
first Play, pressed before the hold had finished filling off the floppy,
copied as it should (`[vp_xfull]` was 0 when `vp_sstart` asked): `Held whole
in XMS`, Drew 240/240, stalls 54, Late 0, 224 ticks of 182. The second Play,
with the hold whole: **`XMS in place (PM)`, Drew 240/240, stalls 0, Late 0,
182 ticks of 182** - real time. Read off the machine between the two:
`[vp_pm]` 1, `[vp_xrun]` 2834h, `[vp_xchk]` 0 (the check not yet made, since
nothing had asked). No fault but `mega_strcal`'s calibration and the 286's
deliberate shutdowns into real mode.

**Not yet seen:** the mr286 profile itself; the bank in place on a pool
big enough to fill every slot before the first frame. (The speaker play is
14.16.1's.)

#### 14.16.1 The speaker and the shadow, in place under `CPU_F_PM` (2026-10-19)

Two refusals in `vp_xipq` and `vp_xbipq` now apply only where
`[vp_pm]` is 0. Every other path decides exactly what it did before: the
two tests are skipped behind one `cmp byte [vp_pm], 0`, and a real-mode
kernel never sets that byte. VIDEO.O88 57,840 -> **57,860** bytes (+20),
bss unchanged at 2,484; no kernel or driver source changed, so kern_mega's
segment is untouched.

**Why the speaker's refusal does not apply.** The speaker's clock is its
own interrupt, one a sample (98.3.15), so what it cannot have is interrupts
held off. Two things held them off:

- **A borrow** (98.3.18.11, 98.3.18.14): the decode ran inside
  `pushf / cli` for up to `VP_XYB` record bytes, because a hidden base dies
  at the first DS an ISR loads. Under `CPU_F_PM` `vp_xmk` answers a real
  selector below `VP_XSEL`, so `VP_DSDX` is a plain `mov ds, dx`,
  `VP_DSEND` is nothing, and interrupts stay as the caller had them. An ISR
  that loads DS and pops it back gets the same selector.
- **A copy** (98.3.18.5): `int 15h AH=87h` for 32 KB on a 286, the 386's
  1 KB pieces. On kern_mega `OSAPI_XMEM_COPY` is `mx_copy` ->
  `mega_lincopy`: two scratch GDT descriptors and a `rep movsw`, with
  `[sch_lock]` raised (no task switch) and IF left alone. So the bank in
  place's one copy a chunk (`vp_xbread`, bounce -> slot) stops nothing.
  A held file in place copies nothing at all during a play.

The disk is the one thing left that can still stop the speaker. A
REFLECTED `int 13h` masks every kernel IRQ and puts channel 0 back at the
ROM's rate for the call (`mega_irqrom`'s `.nospk`), so the speaker pauses
for it. kern_mega's native floppy path (KERN-MEGA-PLAN 10) waits for IRQ6
with interrupts on. That applies to every speaker play that reads the disk,
the copying ones included, and a held file in place reads none.

**Why the shadow's refusal does not apply.** The shadow decodes a record
into a RAM image (`[vp_shseg]`): a one-bit file with `vd_native`, VGA4
with `vp_decram`, C160 with `vd_c160`. The last two have no `VD_YCK`
sites, so under a borrow they would hold interrupts off for a whole record.
Under `CPU_F_PM` no site yields anyway (`vd_ypatch` is never called). All
three reach the record through the same `VP_DSDX` sites in `vp_decrec` as
a native play, read DS:SI with `lods`, and do no segment arithmetic on DS,
so a selector with SI up to 7FFFh serves them as it serves `vd_native` on
the screen.

**Measured on the 286** (86Box `vm/286`: AMI NEAT, 12.5 MHz, OTI067 VGA,
no sound card; `mega.img` of this tree on A:, a 1.44 MB B: with VIDEO.O88
and one clip). Each clip is 240 frames of `tests/vidxip.py`'s 640x200
random-patch picture at 24 fps. The speaker clips add an 11,025 Hz PCM8
sweep. The base arm is VIDEO.O88 assembled from this branch's HEAD before
the change. Play was pressed with `[vp_xfull]` 1 for a hold.

| clip, machine | arm | card's bank line | Drew | stalls | pauses | Late | ticks |
|---|---|---|---|---|---|---|---|
| SPK.V88 (906 KB, card PCM8 shaped for the speaker), 4 MB, held | base | `Held whole in XMS` | 240/240 | 0 | 0 | **477** | **375 of 181** |
| same | new | `XMS in place (PM)` | 240/240 | 0 | 0 | **0** | **182 of 181** |
| SPK.V88, 2 MB, banked (672 KB) | base | `XMS bank idle: the speaker` | 240/240 | 0 | 0 | **546** | **403 of 181** |
| same | new | `XMS bank in place (PM), 672 KB` | 240/240 | 0 | 0 | **27** | **195 of 181** |
| same, M pressed before Play (silent) | new | `XMS bank in place (PM), 672 KB` | 240/240 | 0 | - | 0 | 183 of 181 |
| SHD.V88 (814 KB, Hercules layout: the SHADOW on a VGA, `[vp_shadow]` 1), 4 MB, held | base | `Held whole in XMS` | 240/240 | 0 | - | 0 | 183 of 182 |
| same | new | `XMS in place (PM)` | 240/240 | 0 | - | 0 | 182 of 182 |

What each row says:

- **The held speaker play is real time in place and took twice as long
  without it.** Refused, a speaker play makes no XMS copy, so the floppy
  serves a file that wants 81 KB/s. The hold was full and unused, and the
  card still said `Held whole in XMS` (`vp_bkind` gives 5 only for a bank).
  That label is pre-existing; the owner may want it to say the hold was
  idle, as the bank's line does.
- **The banked speaker play went from 403 ticks to 195.** That is not real
  time, and the silent play of the same bank is (183). The difference is
  ~14 ticks and 27 late periods. This was not taken apart. The candidates
  are the speaker ISR's own CPU (11,025 interrupts a second through a
  286's protected-mode IDT) leaving the reader less time to refill a bank
  that wraps, and any reflected `int 13h` that pauses the speaker.
- **The shadow in place plays exactly as the copying one does.** Both are
  on time. What in place buys there is the drain's copies, not a speed the
  window could see. The box's picture after the play is the same pixel for
  pixel in both arms. This clip is one-bit, so it ran `vd_native` into the
  shadow; a VGA4 or C160 shadow (`vp_decram`, `vd_c160`) was not played.
- In `[vp_*]` read off memory mid-play: the held speaker play had
  `vp_xip` 1, `vp_snd` 2 (`VP_SPK`); the banked one `vp_einp` 2,
  `vp_snd` 2; the shadow one `vp_xip` 1, `vp_shadow` 1.

**Faults** (`faults --all`, against `build/megak/kernel.map`): besides
`mega_strcal`'s probes, `mega_svc.r286` and the 286's resets into real
mode, a speaker play logs **39 #GPs "Segment can't write"** in the
player's own code. They are in `os88spk_init`, `os88spk_go`,
`os88spkfx_init` and `os88spk_acct`, so they are a play's start and end,
not its samples. Both arms log the same 39, so they are not this change's.
The shadow plays log none.

**Also checked:** SPKC.V88, the same clip with its sound stored as the
speaker's COUNTS (the encoder's `spk=True`, no shaper), held at 4 MB: in
place (`vp_xip` 1, `vp_snd` 2), 240/240, stalls 0, pauses 0, Late 0, 183
ticks. Not run against the base. Its play logs 23 of the setup #GPs above
(no `os88spkfx_init`).

**Not measured:** the sound itself. 86Box's automation has no audio
capture here, so the speaker was judged by its clock: under `VP_SPK` the
frames are paced by the speaker's interrupts (98.3.15), so 240 frames in
182 ticks means its interrupts ran at the sample rate for the whole play,
and `pauses 0` means the sound never waited on the stream. Nobody has
listened to it. Not run on the mr286 profile, nor with a Covox.

**kern_big rows, once at the end** (`os88test.py soak -k ...`, on this
tree): `vidspk vidspkcounts vidbankspk vidxip vidxipsnd vidxipbank
vidplay vidplayshd vidwinshd` - **9/9 ok**. They are real-mode plays
(MartyPC's 8088, QEMU's 386), where the change decides nothing new: they
check that the speaker still keeps its refusal there (`vidbankspk` watches
the bank stay empty) and the in-place and shadow paths are as they were.

**For the owner to look back over:**

1. The bank in place under the speaker is lifted although it measured 7%
   slow. It is half the time of the refusal, which streams off the disk.
   If the owner would rather mute a banked speaker play on a 286 under PM,
   98.3.18.9's rule is where that goes.
2. `Held whole in XMS` on a speaker play that the refusal sent to the disk
   (above) - a real-mode label too, unchanged here.

### 14.17 Item 12 as built: the tables in conventional memory (2026-10-19)

Answer 16: no minimum extended memory beyond FITTING.

**When it applies.** `mega_enter` used to refuse protected mode under 192KB
of extended memory, because the GDT, LDT and IDT sit at fixed addresses
above 1MB. Now, with less than that:

- `mega_tlowf` (`.cold`, run in real mode) claims **82KB of conventional
  memory**, top-down and pinned under the new tag `MEM_K_MTBL` (FF13h).
- The tables are laid out as they would be above 1MB: the GDT at the
  claim's base, the LDT 64KB in, the IDT 16KB after that.
- Every base the switch reads is aimed at the claim: the bootstrap's
  window, GDTR, IDTR, the LDT's descriptor, the LDT's data alias (through
  the new `mega_ldtlin`) and the IDT's alias.
- The claim is asked for after the A20 test, the last refusal, so a machine
  that refuses for another reason keeps its 82KB.
- With no room even for that, the refusal now says "no 82KB for
  kern_mega's tables".

**What such a machine does not get.** None of this needed new code; each
already refuses:

- no pool (`mega_xminit`), so there are no claims above 1MB and no
  compaction;
- `.cold` stays low (`mega_coldup`);
- Hibernate greys itself (its pool test);
- the DOS box's snapshot has no pool to go to.

What remains is protected mode itself: the fault reporter, selectors with
limits, and every program clean of segment arithmetic, at 82KB less heap
than kern_big has on the same machine.

**Cost:** about 50 bytes of `.cold` and 17 of `.text`. kern_big is
byte-identical.

**Verified** on 86Box `vm/286-525`, an AMI 286 at **1MB**, whose BIOS
reports no extended memory: `mega_extkb` = 0.

- `mega120.img` boots to the desktop in protected mode (`[k_dsel]` =
  0010h).
- `mega_ldtlin` reads 9B800h, so the LDT is at linear 9B800h and the GDT
  at 8B800h, both conventional.
- The Disk window and Mines run.
- The fault log holds only `mega_strcal`'s two calibration probes and the
  286's own reflection resets, which is how a 286 leaves protected mode
  for every BIOS call.

**Decision for the owner to look back over:** the 82KB comes out of the
heap whatever the machine runs. The owner's own words were that on such a
machine kern_mega "is a worse kern_big, until programs exist that need
protected mode AND fit". So this is offered rather than chosen: the switch
is made whenever it fits, and a SYSTEM.CFG bit to decline it is not built.

### 14.18 The CS writes in shared hot paths (2026-10-19)

Under kern_mega, CS is a read-only code alias, so every `mov [cs:x], ...`
traps into `mega_gpw`. That is correct, but costs tens of microseconds a
trap. Three shared hot paths wrote through CS per span or per glyph. They
were found by audit and by 86Box's fault log, and no longer do:

- **`apps/os88spkfx.inc`, the speaker shaper** (Audio, Tracker, the Video
  Player, SPKBENCH). It wrote about 8 traps a span, 350-700 a second.
  - The level's tail and emit's control part are assembled twice. The
    protected-mode copy brackets its state writes with DS = the package's
    own data selector.
  - The copy is chosen once, at `os88spkfx_init`, by jumps the package
    writes over its own code THROUGH DS. A per-call mode test would have
    cost kern_big ~30 cycles a call, and could not reach emit's sub-block
    exits.
  - kern_big's listings are row-for-row identical over the 688 rows.
  - +476 bytes a package, so 2,380 across the five.
  - Done by an agent and merged.
- **MIDIRack's wavetable loop** (`mrwt.inc`). It patched an `adc`
  immediate, one trap a voice a span.
  - The loop is assembled twice. The protected-mode copy does
    `adc si, [MWT_VSTEP]` through the mix claim and is entered by a jump
    written at launch.
  - kern_big's loop is unchanged.
  - +166 bytes.
- **`apps/os88ui.inc`'s radio glyph** (`os88ui_gring`). It staged its four
  corner points in its own image through CS, eight traps a glyph drawn.
  That is 120 for MIDIRack's Settings dialog, and the same in every
  package or driver that draws a radio.
  - The points are now pushed on the stack and handed to
    `OSAPI_GFX_POINTS` with DS = SS. SS is writable on every kernel and
    for every includer.
  - `glyphcost` (its time budget) and `radio` pass on kern_big.
  - Seen on 86Box: the Settings dialog draws its rings correctly and
    leaves no per-glyph traps.

**Verified:**

- The scoped rows `spkfx spkfxtab spkbench apspk trkspk trkspklevel vidspk
  vidspkcounts midirackwt radio glyphcost` all pass.
- On 86Box's mr286 profile, MIDIRack playing on the SB wavetable now logs
  no per-span `#GP`. The earlier 282C:2178 site is gone, and so are the
  glyph's eight sites at 282C:8076-809B.

**Still trapping by design:** writes through CS that run once (init,
ratchet). Excitebike's per-frame renderer state and its pose loader no
longer write through CS (14.21), and 1942's frame was already clean when
this was written: its last per-call site, `n_pushall`, went in 14.14.

### 14.19 Field: the Video Player's desktop poster off the grid (2026-10-19)

**Reported** on the owner's machine, build 1271, kern_mega installed to the
hard disk: opening a 21.8MB video stops at `kern_mega FAULT 0D err 1820 at
284C:142F`. 142F is `vp_thpic`'s `mov ds, ax` before `OSAPI_DECOMP` - the
DESKTOP POSTER (SPEC.md 98.1.3.11), which came in with the elendilon merge
(bbcd7fbc) and had never run on kern_mega. `vp_rdat` answers SI = where the
poster's bytes landed in `[vp_rdseg]` - the file offset's place in its
cluster, so anything at all - and the code made that a segment of its own,
`[vp_rdseg] + (SI >> 4)`, SI & 15. That is off the 8-paragraph grid on which
kern_mega has identity selectors, and what happens next depends on the low
three bits: with RPL bits it is #GP with the selector as the error code (the
field's 1820, the RPL bits not being in an error code), and with only bit 2
it is SILENTLY an LDT code alias of some other segment - which is what 86Box's
286 did with `tests/vidposter.py`'s STORED.V88 (`[vp_rdseg]` 15A0h, SI 148h,
DS = 15B4h): no fault, and a white box decoded out of the wrong memory.

**Fixed** the way the block read at `vp_kbrd` already was (item 1): the
segment moves on in whole 128-byte steps and SI keeps the low seven bits -
`and ax, 0xFF80` before the shift, `and si, 127` after. Generic, +3 bytes of
`video.o88` (44,147 packed), one poster an open, so no speed question on
kern_big. **A/B on 86Box's AMI 286** (4MB, `mega.img` at f40506c1, the clip
on a 1.44MB B:): the old player draws a white box, the fixed one the poster's
colour gradient. No fault either side but the deliberate ones. It was the
only `+ (SI >> 4)` against a claim in the merged poster code.

### 14.20 Field: a packed file read into a claim above 1MB (2026-10-19)

**Reported** on the owner's machine, build 1271, kern_mega on the hard disk:
double-clicking README.TXT opens Note Pad empty and toasts `Disk error`;
MIDIRack says `The file could not be read` for every song. Both read a
PACKED file (SPEC.md 20.13.5: the readme and every data file ship 'CZ') into
an `OS88_HIGHMEM` claim - a selector above 1MB. The floppy fails the same way,
so the disk was never the variable.

**The defect:** above 1MB the read walks the OFFSET and not the segment -
`dskw_rflush` and the staged sector add to `[dskw_src]`, a selector having
no paragraphs to step (`[dskw_hi]`). `dskw_czexp` then did what the
conventional arm does, `lds si, [dskw_src]`, and looked for the 'CZ' header
past the end of what it had just read; the decoder refused it, `.czbad`,
FERR_IO. Found under QEMU's GDB stub (Note Pad's read answers AX = 2 with ES =
its claim 2824h; the stop at `dskw_rdata.corrupt` from `.czbad` after the
whole chain had arrived). Two things cost the hunt time and are worth
knowing: **`.cold` is not at COLD_SEG on kern_mega** - selector 0F40's base
is 121000h, the cold rung having moved above 1MB, so the F400h copy a map
suggests is stale - and a breakpoint stops again on itself until it is
deleted and stepped over.

**Fixed:** `[dskw_czsrc]` banks the packed bytes' offset where R is folded
in, and the expansion reads it under `[dskw_hi]`. +2 bytes of `.bss` and a
few of `.cold`, kern_mega only (inside `%ifdef KERN_MEGA`). QEMU: README.TXT
loads from the first instance on a fresh boot. 86Box's MR BIOS 286 (4MB,
SB16, kern_mega INSTALLED on an IDE disk): README.TXT loads, MIDIRack loads
BATTLE1.MID and plays it through the PC speaker (`PC speaker - pulse-width
synth at 16,124 Hz`). The owner's `The PC speaker is busy` is NOT that defect: it happens with the
song loaded, double-clicked with MIDIRack's saved output the PC speaker, and
it did not reproduce on 86Box's MR BIOS 286 at all - the same double-click,
the same saved setting, SB16 fitted, plays. `-DMR_SPKDIAG` (assembled by
hand: `nasm ... -DMR_SPKDIAG apps/midirack/midirack.asm`, then `os88pkg.py
--part build/mrfaces.bin`; the shipped build is byte-identical without it)
turns the message into `Speaker refused: run fl=F ax=A spk=S`: OSAPI_FSX_RUN's
FLAGS and AX, and OSAPI_FSX_SPK's code - FFFF never asked (the bracket itself
refused), 1 not in a rate bracket, 3 the speaker taken. Sent to the owner.

**Also fixed on the way, found reading `mega_hibnc`:** the bounce for a BIOS
transfer into a buffer above 1MB split the caller's run into chunks and
advanced only CL's sector number (`add CL, AL`, "inside one track"), while
a fixed disk's run goes to the cylinder's end (SPEC.md 18.91.5) and a
floppy's may cross a head (18.91.1). A chunk past a head's last sector asked
for sector 18 of a 17-sector track. It now stops each chunk at the track's
end and carries sector, head and cylinder by `[disk_spt]`/`[disk_heads]`, the
geometry the caller's CHS was made with (+1 byte of `.text`, `[mega_hbdh]`).
Not what the field hit - this path is reached only where `dsk_xfer`'s own
landing is absent - and not separately reproduced.

### 14.21 Excitebike's and 1942's frames: no write through CS (2026-10-19)

14.18 left the per-frame renderer state of Excitebike, and "1942", on the
list of things that still trapped. Both were measured under Bochs with
`make megatrap` (`tools/megatraps.py`), from the race's first frame through
about 40 seconds of riding with Z held: Selection A before, and Selections A
and B after, with lane, lean and turbo keys. The CGA rows are a second
census tree, `make BUILD=build/megatrapcga MEGATRAP=1 VIDEO=cga`, whose CGA
modes Bochs's VGA runs. (It shows the race with the HUD at the TOP: Bochs
does not wrap the CGA's 16KB start-address ring. Both builds show the same,
so it says nothing about the change. The CGA picture is checked by
`excitebikevideocga`, on MartyPC's real CGA.)

| kern_mega, Bochs | before | after |
|---|---:|---:|
| Excitebike VGA race, 40 s | **108,763** traps at 36 sites, 2,146 frames: **50.7 a frame** | **0** |
| Excitebike VGA race, Selection B (three rivals), 40 s | not taken | **0** |
| Excitebike CGA race, Selection B, 40 s | **911,764** at 44 sites: ~343 a frame | **0** |
| the bracket's pose load, VGA (`xs_shift`, `xs_compile_v`, `xv_dicts`) | **44,304** | **0** |
| the bracket's pose load, CGA (`xs_cgarow`, `xs_compile`) | **34,247** | **0** |
| 1942 VGA, 30 s and then 50 s of play | **0** | **0** |

Two of the before rows deserve a note:

- 834,878 of the CGA's 911,764 were **one instruction**:
  `xb_clk_sample`'s store, called from `xc_vr`'s retrace spin. The spin
  waited at a trap an iteration.
- 449 more were in a NAMELESS image: the compiled CGA poses. `xc_draw`
  handed them ES = `push cs / pop es`, so every byte they wrote into the box
  trapped.

From launch to the title, each after-run logged one line, `0060:04E7`, an
`xfer` in the kernel's own image. The before runs logged the same line, so
it is not the package's.

**1942 needed nothing.** No `[cs:]` store and no `push cs` is left in
`apps/1942`. Its last per-call site, `n_pushall`, went in 14.14, and the
14.18 line was stale. `1942.o88` is byte-identical to the base.

**What changed in Excitebike.** All of it is generic: the same code on
every kernel. SPEC.md 102.11 is where the state lives now.

- **The column constants** (`xc_col` / `xh_col` with `xc_run` / `xh_run`)
  are a 10-byte frame on the stack, read through BP. `xc_setx`,
  `xh_setx` and five words of bss are gone.
- **The VGA** (`xv_sync`, `xv_erase`, `xv_col`, `xv_bike`, `xv_fp_add`,
  `xv_hud`): every store is made while DS is ours. DS moves to the card
  only for the latch-copy loops, which keep their counters in registers.
  `xv_bike`'s interpreted loop keeps the layer in BP's high byte, and its
  `push cs / pop ds` is now a `push ds` / `pop ds` pair.
- **The CGA box** is ES = DS's selector, so the compiled poses, the
  interpreter and `xc_drawc`'s three words write it without a trap.
- **The clock.** `xb_clk_sample` is DS-relative. The two callers whose DS
  is foreign (the full-window column loops) call `xb_clk_samplef`, the
  CS-relative body.
  - Under `CPU_F_PM`, `xb_pmset` writes a `jmp short` over that entry's
    first two bytes THROUGH DS, at the top of every fullscreen bracket. The
    jump goes to an arm that loads DS from `[xb_self]`, banked by the same
    routine.
  - This is the one patched dispatch. A per-call mode test or a DS reload
    would have cost kern_big the full-window frames.
  - The two bytes it replaces are two one-byte instructions, and an
    assembly-time `times` refuses a jump that would not be short.
- **The pose loaders and compilers** keep DS ours and reach the art or
  the claim through ES.
- So does the custom-course copy.

**Bytes:** the image is **216 smaller**, 31,540 -> 31,324, and the bss
**10 smaller**, 9,696 -> 9,686. Every changed frame routine is shorter:

| routine | base -> now |
|---|---|
| `xv_erase` | 327 -> 290 |
| `xv_bike` | 348 -> 315 |
| `xc_run`, `xh_run` | 111 -> 91 each |
| `xc_col` with `xc_setx` | 192 -> 152 |
| `xh_col` with `xh_setx` | 320 -> 272 |
| new: `xb_clk_samplef` | 45 |
| new: `xb_pmset` | 23 |

**kern_big is faster, not merely as fast.** Measured on MartyPC's 4.77 MHz
XT with `tests/excitebike_perf.py --scroll` (and `--herc --scroll`), run
in this tree and in a worktree of the base commit, f40506c1, in the same
pass. Mean clocks a frame, about 150 frames each:

| | base | now |
|---|---:|---:|
| VGA erase | 5,185 | **4,744** |
| VGA columns | 9,016 | **8,944** |
| VGA a column write (`xv_col`) | 6,434 | **6,389** |
| VGA sprites | 8,590 | **8,485** |
| VGA HUD / flip | 1,480 / 392 | 1,479 / 393 |
| CGA columns (0.42 columns a frame both) | 6,504 | **6,211** |
| CGA boxes (RAM) | 9,297 | **9,237** |
| Hercules columns (0.49 / 0.50 columns a frame) | 8,498 | **8,122** |

The CGA's and Hercules' HUD and box-transfer rows moved by 1-3% **both
ways**. Their code is byte-identical: the same routine at a shifted address,
which an 8088 fetches a byte at a time and so cannot price differently. The
script's frames land on different scroll positions from run to run, so those
rows are the workload's spread, not the change's.

**Rows run once, after the change:** `soak -k 'excitebike*'`, **35 of 35
ok**. That covers the three adapters' video, flow, AI, custom-course, perf,
lap, Selection B, audio, geometry, low-memory, small-claim and load rows.
1942's rows were not run: its package is byte-identical, so they would boot
the same bytes.

**For the owner to look back over:**

- **The patched entry is a real-mode byte pattern that kern_mega
  rewrites.** It is the shape `os88spkfx_init` already ships (14.18), one
  routine instead of six.
- **The CGA picture under kern_mega has only been looked at on Bochs**,
  whose CGA emulation does not wrap the ring. A real 286 with a CGA or a
  Hercules has not run this build.
- **Hercules is checked statically only.** Bochs has no Hercules. No
  `[cs:]` store is left on `xh_col`'s path, and `excitebikevideoherc` passes
  on kern_big.

### 14.22 Field: every DOS trip refused on the way home (2026-10-19)

**Reported** on the owner's machine: launching Test Drive III on kern_mega
ends at `Resume failed: the programs' memory did not read back as it was
saved`, and the program never appears. **Reproduced on 86Box's MR BIOS 286**
(the 286 reset route, kern_mega installed on IDE) with DOSHELLO.COM, which
runs, prints, takes its key and exits 42 - and the trip home then refused the
same way. So not the program's.

**The defect is mine, item 3's** (0f622b1a). The programs' sum (SPEC.md
87.4.1) is taken in `hbm_wrimg`'s step 5 and checked at `hbm_wake` 3a - and
kern_mega's park route (`hbm_dosrun`, KERN-MEGA-DOS-PLAN 2.3) never calls
`hbm_wrimg`: it sweeps, locks, snapshots and on the way home jumps straight to
`hbm_wake`. So 3a compared the live sum against whatever `[hbm_psv]` held -
zero, or the last real hibernate's - and refused every trip. Bochs's runs of
the park predate item 3, which is why nothing caught it.

**Fixed:** the park route takes the sum itself after the lock and before
`mega_dosparkf`'s snapshot, so the park holds the figure and the restore
brings it back with everything else. kern_mega only. 86Box, the `DOSRMARK=1`
build: DOSHELLO.COM `ended, exit code 042 (Arena: 622KB)`, the desktop back.

**Left open:** Test Drive III itself exits 255 at once under kern_dos on
this machine (`TD3.EXE ended, exit code 255 (Arena: 622KB)`) - the round trip
is now fine, the program is what refuses. That is a kern_dos compatibility
question and is investigated on its own (docs/DOS-DEBUGGING.md).
*(2026-10-22: resolved - 14.23 found EXEPACK under the HMA, 14.24-14.26
fixed it, and both arms make the round trip on the owner's 286.)*

### 14.23 Field: Test Drive III exits 255 at once - EXEPACK under the HMA (2026-10-19)

**14.22's open item, answered.** `TD3.EXE` is packed by Microsoft's EXEPACK
(no relocation entries, entry at CS:0010, `'RB'` at CS:000E, the string
*Packed file is corrupt* before `mov ax, 4CFFh / int 21h`). Its unpacker
normalises both pointers DOWNWARD, so the bottom of its output is written
through a segment below 0 - correct only through the 1MB wrap. kern_dos in
the HMA (W4) is the one arena in the tree that is both BELOW 64KB and run
with A20 OPEN, so the wrap reaches the HMA, the unpacker reads garbage and
exits 255; its message goes to handle 2 and is gone with the program. **kern_mega
only**: kern_big's kern_dos leaves A20 as the BIOS has it, which is why the
same file runs there. Not TD3's: it is the whole EXEPACK class, and W4 named
it and answered it with `LOADFIX`, which nobody types.

**Measured, Bochs**: `TD3` exit 255 (Arena 621KB), `LOADFIX TD3` the Main
Select Screen. **Fixed** in kern_dos's loader (`kd_xpack`, SPEC.md
96.40.9.1): EXEPACK's shape below 64KB raises the floor to 1000h and reads
the image again - LOADFIX unasked, keyed on the shape EXEPACK's own
header gives it. Bochs, after: `TD3` plays and `Exit to DOS` ends `exit code 000
(Arena: 566KB)`; `XPACK.EXE` (tests/dostrap/xpack.asm) 048 at 566KB, its
spoiled-signature control `XPACKNS.EXE` 255 at 621KB, and `XPACK.EXE` on the
build before the fix 255 at 621KB. kern_dos +104 bytes on kern_mega, kern_big's
`kerndos.bin` byte-identical. **On 86Box's 286-video profile** (MR BIOS 286,
4MB, kern_mega installed on IDE, the 286 reset route): TD3 double-clicked
shows its title picture and the Main Select Screen, Esc and Y exit to DOS,
and the box reports `exit code 000 (Arena: 567KB)` with the desktop back.
The owner's own 286 is still to run it.

**And `make mega` did not rebuild it**: the two image rules reach the kernel
and DOS.O88 only through a sub-make that runs when the IMAGE is stale, and
nothing under `kernel/`, `kerndos/` or `apps/dos/` was a prerequisite - so a
change there alone printed success over the previous kern_dos. `MEGASRC` is.

### 14.24 The loader unpacks EXEPACK itself (2026-10-19)

**14.23's answer cost the class ~55KB of arena**, on exactly the programs big
enough to have been packed: Test Drive III ran at 567KB against the ~570 the
owner says it needs. The owner's call: *"1 sounds like a good fix for giving
even the most ram hungry exepack's their arena, so I think do it."* So on the
HMA arm the program's own unpacker no longer runs at all (SPEC.md 96.40.9.1):
`kd_xunp` (`kerndos/kdxpack.inc`) unpacks the image in place after
`dos_exe_setup`, from the published format and written from it - the
backward fill/copy stream, the sixteen-group relocation table after the
stub's message, both header shapes - with its pointers clamped at the load
segment where the stub's wrap below it. Everything is checked first,
including a dry walk of the whole stream, so a refusal writes nothing and
14.23's floor raise is the fallback.

**Measured, Bochs (kern_mega, 16MB)**: Test Drive III to the Main Select
Screen, Esc and Y, `TD3.EXE ended, exit code 000 (Arena: 621KB)` - against
566 before. The format probe `XUNP.EXE` (`tests/dostrap/xunp.asm`, packed by
`tools/os88xpack.py` behind a stub that only refuses) **048 at 621KB**, its
18-byte-header twin `XUNPL` 048 at 621, the spoiled-table and spoiled-stream
twins `XUNPT`/`XUNPS` 255 at 566 (refused, floor raised, stub run), and the
14.23 pair unchanged: `XPACK` 048 at 566 (shape without the format, so the
fallback), `XPACKNS` 255 at 621. **Red on purpose**: with the relocation pass
taken out `XUNP` and `XUNPL` exit 051, the probe's own code-fixup check.

**Bytes**: kern_mega's `kerndos.bin` 34,329 -> 35,305 (+976) and 38 of bss,
all of it in the HMA - `KD_IMG_KB` 36 -> 37 for the HMA arm only, the arena
unchanged; `DOS.O88` 49,763 -> 50,616 on mega.img. kern_big's `kerndos.bin`,
`doscore.bin` and `dosp.bin` byte-identical.

**Left open**: a CHILD loaded by `AH=4Bh` below 64KB still runs its own
unpacker (the core's `dos_exec_load` does not pass through `kd_entry`'s
loader); a host hook for it is the next step if a launcher program ever
needs it.

### 14.25 `Load DOS in the HMA`: a second kern_dos, low, A20 shut (2026-10-19)

**The owner's second fix**: *"Could we also have a 'load in hma' checkbox in
the settings, and thus have two possible fixes?"* 14.24 cures the class that
names itself; this one cures the wrap whoever wants it. SPEC.md 96.40.9.2 is
the contract. kern_mega's `DOS.O88` carries both kern_dos images - part 2 in
the HMA, part 3 (`-DKD_LOWDOS`) at 0060:0000 - and the box's Memory page has
a check box beside *Disable the mouse*, ticked by default, saved in a
shortcut as byte 12's bit 3. Unticked, the box posts part 3 and sets
`KDLF_LOWDOS`; `HIBER.DRV`'s stub reads the bit to choose where it expands,
copies the block into and jumps; the low image shuts A20 after `kdx_init`
(port 92h where there is one, then the 8042) and `kd_unpark` opens it again
on the way home.

**Where it lives was a choice between two conventions** and this is the one
the box's other launch-time choices already use (the Memory page, per
launch, carried by a shortcut), rather than SPEC.md 51.5's `SYSTEM.CFG`,
which is for machine-wide settings a driver or the kernel reads at boot.
Greying: the box exists only in kern_mega's `DOS.O88`, which refuses to
start anywhere but a kern_mega in protected mode, where both images run - so
there is no state in which it is a fact that it cannot be honoured. kern_big
and kern_small do not show it at all: their Memory page has no free row
(SPEC.md 96.36.2) and their `DOS.O88` stayed byte-identical.

| measured | Bochs (16MB) | 86Box 286-video (MR BIOS 286, IDE) |
|---|---|---|
| ticked: Test Drive III | 000, 621 KB | **000, 622 KB** |
| unticked: Test Drive III | 000, 585 KB | **000, 586 KB** |
| unticked: `XPACKNS.EXE` (needs the wrap, no shape) | 048, 585 KB | 048, 586 KB |
| unticked: `XPACK.EXE` | 048 | 048 |
| unticked: `A20PUB.COM` / `A20SHUT.COM` | 071 / 032 | 071 / 032 |
| ticked again: `XPACKNS` / `XUNP` | 255 at 621 / 048 at 621 | - |

The page's estimate moves with the box: 612 KB ticked against 576 unticked
on Bochs, 613 against 577 on 86Box - nine under the measurement on both
arms, the direction an estimate should err.

**Bytes**: part 3 is 33,952 bytes (13,649 packed) and `DOS.O88` on mega.img
goes 50,616 -> 64,265; the box +142 bytes, `HIBER.DRV` +35 bytes of file.
Nothing resident; kern_big's `kerndos.bin`, `doscore.bin`, `dosp.bin` and
`DOS.O88` byte-identical. The low route reads ~13KB more of `DOS.O88` at the
handoff (the one extent run covers parts 1 to 3), the default route nothing
more.

**For the owner to decide**: (1) whether the default should follow the
program rather than the box - an EXEPACK image the loader REFUSES falls
back to 14.23's floor raise at 566 KB, where the low image would have given
it 585, but kern_dos cannot switch image itself (the stub that loads it is
gone by then), so that would be the box re-posting the launch low; (2) whether a machine-wide
default belongs in `SYSTEM.CFG` as well, for a user who wants every launch
low; (3) the owner's own 286 is still to run both arms.

### 14.26 The refused EXEPACK program switches itself to the low image; and the AH=4Bh child (2026-10-19)

**14.25's open question (1) answered, and the other way round.** It asked
whether, for an EXEPACK program the HMA unpacker REFUSES, the default could
follow the program - the floor raise gives it 566 KB where the low image would
give 585 - and reasoned the only way was *"the box re-posting the launch low"*,
a full return trip to the desktop. The owner's call was to hide it entirely:
**NOT to re-post at all**. The HMA kern_dos is already in real mode with its
own file layer, so it reads part 3 of `DOS.O88` itself and becomes the low
image in place. SPEC.md 96.40.9.1 is the contract.

**Task A - the switch** (`kd_tolow`, `kerndos/kdxpack.inc`, HMA arm only).
`kd_xpack` calls it the moment `kd_xunp` refuses, before the floor raise. It:
reads part 3's file row from the launch block (the box stages `KDL_L3OFF` /
`KDL_L3ZB` in `dos_lbfill`) and `DOS.O88`'s cluster and volume (the kernel
stages `KDL_O88CL` / `KDL_O88V` at `hbm_dosrun` step 2, where the file is
found); mounts that volume; walks the chain to part 3 and reads its packed
sectors into a 64 KB staging window at the arena floor, the stream placed at
the window's TOP so the expansion below never overtakes it; expands it with
`kxl_dec`, an LZ4-only os88lz reader written for this arm (kern_dos's
`kdshim.inc` stubs `lz_decomp`, and pulling all of `kernel/lz.inc` into the
HMA kilobyte would be bigger and collide with kdshim's mirrored constants - it
is the fourth reader of the format, after os88lz.py, lz.inc and hiber.inc's
`kds_expand`, and models the last); stages the launch block with `KDLF_LOWDOS`
set and `KDLF_LOADFIX` clear; then - **touching no stack**, because the
copy-down overwrites the HMA arm's own FAT snapshot, mount buffers and stack
at 0060h (kdlayout.inc) - copies the low image to 0060:0000, the core into its
`CORE_ORG` hole from this image's own copy, and the block where the low image's
header points, and far-jumps in, exactly as `kernel/hiber.inc`'s stub would.
The park fields (`KDL_PBASE` ...) ride the block verbatim, so the way home is
the low route's. 14.23's floor raise is the fallback when part 3 cannot be
read. **Chosen only for an EXEPACK-shaped program the unpacker refused**, not
every refusal.

**Task B - the child** (`DHK_XUNP`). `AH=4Bh` does not go through
`kd_entry`'s loader, so a launcher's EXEPACK child ran its own unpacker below
64 KB with A20 open and failed. A new host hook - `DHK_XUNP`, filled with
`kd_xunp` by the HMA host only, `%ifdef DOS_MEGA` so kern_big's core stays
byte-identical - is called by `dos_exec_load` after `dos_exe_setup`, against
the same `[dos_exe_*]` cells. A child it refuses runs its own unpacker (which
works at or above 64 KB); the top-level switch is not available mid-run (the
stub is gone), so `LOADFIX` per child is what is left for a refused child that
needs the wrap.

**Measured, Bochs (16MB, kern_mega from A:)**, all on the glass:

| probe | arm it took | exit / arena |
|---|---|---|
| `XSWITCH.EXE` (shape, refused) | SWITCH -> low, A20 shut | **042 (0x2A), 585 KB** |
| `XSWITCHN.EXE` (signature spoiled) | stays HMA, A20 open | **254 (0xFE), 621 KB** |
| `XPACK.EXE` (shape, no format) | SWITCH -> low | **048, 585 KB** (was 566 via the floor raise, 14.24) |
| `XPACKNS.EXE` | stays HMA | 255, 621 KB |
| `XKIDRUN.COM` EXECs `XUNP.EXE` | child unpacked by the hook | child **48**, parent 048 @ 621 KB |
| `XKIDRUNT.COM` EXECs `XUNPT.EXE` (table spoiled) | hook declines, child's stub runs | child **255** |
| Test Drive III, ticked | HMA, `kd_xunp` unpacks | to the Main Select Screen, ~621 KB |
| Test Drive III, unticked | low, A20 shut, own unpacker | to the Main Select Screen, est 576 (585 arena) |

`XSWITCH`'s exit code is the clean proof: 042 happens ONLY on the low image
with A20 shut, which only the switch reaches from a double-clicked program.
`XPACK` moving 566 -> 585 is the same switch replacing the floor raise. The
child probe's 48-against-255 is the hook unpacking and declining correctly;
`XKIDRUN` first HUNG on the return until its own stack was moved into the 8 KB
it shrinks to - a 128 KB child overwrites a .COM's default `SP = 0FFFEh`,
which is the launcher's bug and not the kernel's (the dosexec gate's DOSKID is
too small to reach it).

**Bytes**: `kerndos.bin` (HMA) 35,305 -> 35,721 (+416, the switch and
`kxl_dec`, inside `%ifdef KD_HMA`); `dosp.bin` +16 (part 3's row);
`HIBER.DRV` +12 (`KDL_O88CL`/`KDL_O88V`, kern_mega only); the core +11 and
`CORE_BSS_SIZE` mega 3509 -> 3511 (`DHK_XUNP`); `DOS.O88` 50,616 -> 64,777.
**kern_big's `kerndos.bin`, `doscore.bin`, `dosp.bin` and `DOS.O88` are
byte-identical** - verified by `cmp` against a base build. Every byte added is
behind `KD_HMA`, `KD_MEGA` or `DOS_MEGA`.

**Still open**: the owner's own 286 is still to run both arms; 86Box's
`286-video` run of these probes (14.24/14.25's machine) is the next field step.

**Field (2026-10-20)**: both arms make the whole round trip on the owner's
286, Load DOS in the HMA ticked and unticked.

### 14.27 A write from above 1MB stamped its hint from past the end (2026-10-20)

**Reported off the owner's 286**: FTPD taking a `STOR MIDIRACK.O88` stopped
on `kern_mega FAULT 0D err 0000 at 0F40:93DE`. That is `.cold`'s first
instruction of `dskw_czhdr`, `cmp word [es:bx], DSK_CZ_MAG`, reached from
`dskw_czstamp` - the compressed-file hint (SPEC.md 20.14), which a write
derives from the file's first bytes after the data is down.

**The defect is 14.20's, on the write side.** Below 1MB the write loop
advances `[dskw_wseg]` and leaves `[dskw_seg]:[dskw_src]` on byte 0; above
1MB a buffer is one selector and the loop advances `[dskw_src]` itself, a
run at a time. So the stamp read the hint from the END of the bytes just
written - one past the limit of FTPD's selector for the file that faulted,
and for any smaller high write a word of whatever lay there, which could mark
a plain file compressed or miss a compressed one. Only whole sectors move the
offset (a partial tail goes through the stage), which is why a buffer that
ends mid-sector reads short of its limit and does not fault.

**The fix** banks byte 0's offset in `dskw_wdata`, beside `[dskw_wseg]`, into
14.20's own `[dskw_czsrc]`, and `dskw_czstamp` reads the header from there.
Below 1MB the two cells are equal, so one load serves both. Inside `%ifdef
KERN_MEGA`: kern_big and kern_small byte-identical (`md5sum` against builds
of the same commit without it).

**The gate** is a new leg of `tests/megaxm` (steps 2D-2F): 32,768 bytes, whole
sectors, written from an `OSAPI_XMEM_SEL` window exactly that size. On the
kernel without the fix Bochs reports `#GP` at `0F40:93DE`, the field's
address to the byte; with it, `MEGAXM: ok`. The existing leg's 40,000 bytes
from a 64KB window could never have caught it - the stray read landed inside
the window.

### 14.28 The installer's copy buffer, refused on every rung (2026-10-20)

**Reported off the same machine**: Control Panel -> Hard Drive -> Install
says `Not enough memory to copy`, with megabytes free.

**It is the owner test, not the memory.** `hd_ibuf_get` walks its ladder
down to 8KB and every rung is refused. The installer is HDD.DRV's SECOND
image (`HDDTOOL.DRV`, SPEC.md 52.11.7), a conventional claim HDD.DRV made -
and on kern_mega HDD.DRV is moved above 1MB at boot (item 82,
`drv_hi_move`), which re-points the tool claim's owner at the driver's new
selector and FREES the driver's own conventional claim. `mem_own` then
finds the tool's claim, takes its owner, and asks whether a `MEM_K_DRV`
claim starts there; none does any more, so the claim is refused. The arm
that answers this - `drv_owns_seg_x`, which asks `drv_tab` - was written for
a driver in the HMA, which runs in no claim either, and is compiled only
under `HMA_DRV`, which kern_mega never defines.

**The fix** compiles that arm for kern_mega too (`%if %isdef(HMA_DRV) ||
%isdef(KERN_MEGA)`), and the high arm's own copy of the same call - for a
package placed high - now falls to it instead of repeating it. kern_big and
kern_small byte-identical.

**Verified** on Bochs with `docs/BOCHS.md`'s installer recipe (blank C:,
Format, Install): the kernel without the fix stops on `Not enough memory to
copy` exactly as the 286 did, and with it the install runs to `Done - remove
the floppy, Restart`. E9 shows `above 1MB: Hard Drive` on both, the
precondition. Not yet on the owner's machine.

**Field (2026-10-20)**: FTPD's upload now works on the owner's 286; the
installer no longer refuses its copy buffer (it went on to 14.30).

### 14.29 The Video Player's bench record, off the grid (2026-10-20)

**Reported**: `kern_mega FAULT 0D err 2DC8 at 284C:5E47` the moment Play
starts. `284C:5E47` is `vp_bngen`'s first instruction, `mov es,
[vp_bnseg]`: THE BENCH (SPEC.md 98.1.9.13) is built in the ring past the
key's read and a LEAD, and the lead was added in whole paragraphs,
`([vp_alb] + 15) >> 4` - which kern_mega's grid needs in whole GRANULES
(14.16's rule, 14.19's class of fault). T69's lead is 6 x 882 bytes = 331
paragraphs, three past a granule, and selector 2DCBh's error code is 2DC8h.
**Fixed** by rounding that term up to 8 paragraphs under `[vp_pm]`
(`video.o88` +9 bytes, the real-mode arithmetic untouched). Every other
`+ [vp_ring]` in the player was read: slots are 2,048 paragraphs, the key's
lead `[vp_kbkb] << 6`, the planes 14.16's - this was the one. Not booted
here: the bench is taken only by a layout at or below Mode X, and every
such demo with a bench is 21MB or more, past Bochs's C:; SNOW (VGA8 at 320,
past Mode X) plays through and never reaches it.

### 14.30 The string-restart calibration measured nothing on a REP (2026-10-20)

**Reported**: the installer, from a mounted image and then from a real
floppy, `No system disk: A:k33 #ERNEL #ER KERN KER` - 54.1's signature
exactly: the record fetched through the faulting `rep movsb` is missing its
first byte, the one fetched without a fault is right.

**The calibration never ran its REP probe.** `mega_strcal`'s `.probe` set SI
and DI and left CX "as given", and `.take` measures the advance as `2 -
CX`, so the probe had always meant CX = 2 - but CX at `mega_enter` is
whatever the loader left. With CX = 0 the `rep movsb` moves nothing, takes
no fault, and `mega_sdr` reads 0 0 0: "this CPU restarts a REP exactly".
`mega_strfix` then corrects a plain MOVS (whose probe needs no CX) and never
a REP MOVS - and `dsk_find_x`'s name copy is a REP MOVSB. The Victory's
boot happened to arrive with CX non-zero; the owner's MR 286, booted from
its hard disk, did not.

**Measured on 86Box's `286-mega` (MR BIOS 286, the owner's board)**, reading
`mega_sdr`/`mega_sdp` (linear 62C9h) after boot: the kernel before the fix
`00 00 00 00 / 01 01 00 00`; with `.probe` setting CX = 2, `01 01 00 00 /
01 01 00 00`. 86Box models the 286's early advance, which Bochs's 386 does
not (both read 0 there). +3 bytes of `.text`, kern_mega only.

**The refusal line was changed with it**: on the owner's first attempt the
fields that would have said this were dropped for length behind `A:g1
B:g1`. The first `k` entry now starts the line over, so a volume that
mounted and failed the compare always keeps its four fields. HDDTOOL.DRV,
both kernels.

**Headroom found on the way**: kern_mega's `.text` + `.bss` is FF88h of
KERN_CODE_MAX's 10000h - **120 bytes**, against kern_big's B1F9h. `make
megatrap` overflows it by `mega_tlog` (~150 bytes) and builds with
`MSELOFF=1` beside it for now. The next kern_mega feature in `.text` meets
the segment, so it is the first thing to plan for: moving part of `mega.inc`
out of `.text` (most of it is entered from the fault and IRQ gates, which
want KDATA in DS and a near `.text` call).

### 14.31 Field, the Victory 286 (2026-10-20): what is left after 14.30

**Install now finds the system disk** (14.30 confirmed) and stops in the
COPY, naming `KERNEL.SYS`, with `FD BIOS 08 400100 A90` on the bar: the
native floppy path (`mega_fdgo`) fell back at step 8 - READ DATA's result,
ST0 40h (abnormal termination) ST1 01h (Missing Address Mark) - on drive A
at media 90h, which is 250 kbps: a 9-sector (720 KB) disk in the 1.44 MB
drive. An earlier attempt fell back at step 5 (`05 200000`), the seek's own
check. The fallback is meant to make the native path "only ever faster or
equal", and here the BIOS fallback did not rescue the copy either; an
access to A: with no disk in it then froze the machine, and the drive was
refused by the BIOS until a power cycle. OPEN: which disk was in A:, whether
the same install works from that disk on kern_big (BIOS only), and whether
the native path leaves the 765 or the BDA in a state the BIOS's own retry
cannot recover from. No emulator here reproduces a native failure.
*(2026-10-22: the install half is resolved - 14.32 fixed the KERNEL.SYS
copy and 14.34/14.35 confirmed installs from an image and from a real
1.44MB disk on the Victory, with the fallback judged only after the BIOS
(`mega_fdjudge`, kernel/mega.inc:5296). The empty-drive freeze is still
OPEN: 14.35 found it is state left by earlier use and the owner deferred
it.)*

**T69 plays to the end** (14.29 confirmed) at 1.79x its length - `Late 681,
2079 ticks of 1163` - with `RFL 003D/s 0CDC` on the bar: 61 IRQ reflections
a second at 3,292 us each, ~200 ms of every second. They are the PicoMEM's
option-ROM IRQ (mega_irqrom, SPEC.md 34.10.1): its Sound Blaster has the CPU
copy each "DMA" block in its ROM, in real mode, and a 286 reaches real mode
only by a reset - 22,050 bytes/s of PCM8 at ~361 bytes a block. The reset
is already a triple fault, so the 3.3 ms is the board's BIOS getting from
reset back to `mega_r286` plus the card's copy; fewer reflections is the only
lever. The other ~59% is not yet accounted for: whether HDD.DRV's rung 1
took C: (52.1.1's pairing and TAKE) or every read is a reflected int 13h
(not counted by RFL), and how the same file plays on kern_big on the same
machine, are the next two readings.

### 14.32 The installer's KERNEL.SYS copy: CX moves by TWO (2026-10-20)

**Reproduced on 86Box's `286-video`** (the owner's MR 286, booted from C:,
`IMGS/MEGA.IMG` mounted, Control Panel -> Hard Drive -> Install, keep the
volume): the copy stops naming `KERNEL.SYS`, and an INSTBENCH build says
step 9, `hd_iwrite_chunk`, answered FERR_PROT. That is a PLAIN
`OSAPI_FILE_WRITE` of KERNEL.SYS - the installer believed the file was not a
system one, `[hd_isys]` = 0, so `dskw_pmask` refused it as it would refuse a
package.

**Why it believed that**: `hd_istat`'s `OSAPI_FILE_FIND` record came back
with everything after the name **two bytes early** - the attribute 07 at +11
instead of +13, the size at +16 instead of +18 - so +13 read 0. The record is
written by `dsk_find_x`'s `rep movsb` of 13 name bytes into the driver's
buffer, and the driver's ES there is its CODE alias (`push cs / pop es`), so
the first write faults and 54.1's restart runs. At the fault the 286 frame
holds SI+1, DI+1 and **CX-2**: the CPU took two off the count for the one
element it never wrote. `mega_strcal` measured CX with `.one`, which answered
"1 -> 1, anything else -> 0", so it recorded 0 and `mega_strfix` gave SI and
DI back and not CX - the restart moved 11 bytes. Read off gdb on the fault
frame: entry CX 13, fault CX 11, restart CX 11, exit DI = start + 11.

**The fix measures rather than caps**: the probe runs with CX = `MEGA_SCCNT`
(8), `.take` keeps any advance below that as it is, and `mega_strfix` gives
back each register's own count - SI and DI by that many elements, CX up by
its own. +1 byte, kern_mega only; a 386 still measures 0 and runs nothing.
Re-run on the same machine, the same install reaches `done`, and the volume's
KERNEL.SYS is byte-identical to the source's (101,808 bytes).

**It is very likely the field's stop too** (14.31): the Victory's install
named KERNEL.SYS after finding the system disk, and the only record this path
moves through a faulting REP is that one. Whether the Victory's 286 takes CX
down by one or by two, the measurement now carries what it does. The
native floppy fallback on that bar (`FD BIOS 08`) is a separate question
and stays open, as does the freeze on an empty A:.


### 14.33 The kernel segment is full again: a segment of kern_mega's own (2026-10-20)

14.10 left 364 bytes in kern_mega's `.text` + `.bss`. Two days of merges later
there are **116** (65,420 of 65,536), against kern_big's 17,415. kern_mega
carries **17,299** bytes more than kern_big in that one segment, and 16,193 of
them are `mega.inc` and `megados.inc`. So the next 117 bytes kern_big adds
there break `make mega`. `.cold`'s slot above 1MB, between `MEGA_COLD_LIN`
and `MEGA_XPOOL`, has 3,247 left: the same coupling, one segment over.

**docs/plans/KERN-MEGA-SEGMENTS-PLAN.md is the plan, and its W0-W2 and most
of W3 are BUILT** (its section 8): the segment has **11,484 bytes left**,
kern_mega's overhead over kern_big there is 5,931 and `make mega` holds it to
`tests/megaseg.txt`. kern_mega gets ONE segment of its own, `.mega`. It is
protected-mode-only code under a fixed ROM-top selector (0xFFD8), placed on
the ladder after `.cold` and copied above 1MB with it. About 12.4KB moves in
its first bulk wave and ~3KB more after. That takes the segment's headroom
from 116 bytes to ~12,300, and moves ~12KB of conventional memory back to
the heap.

What stays low is what runs in REAL mode after the switch: the reflection's
real-mode half, `mega_r286`, the IRQ capture and the DOS drop. The cost is
64KB of the XMEM pool (`MEGA_XPOOL` to 0x140000). `tools/os88megaseg.py`
re-derives every per-section figure off the two maps.

### 14.34 Field, the Victory 286 (2026-10-20): after 14.32, and an empty drive

**Confirmed on the machine**: WireChat connects and chats; Install from a
mounted image completes (14.32), and so does Install from a real 1.44 MB
system disk; no freeze with A: empty. The image install put `native floppy
off: BIOS from now` on the glass on its way.

**That toast was an empty drive, not the native path.** Eight fallbacks in a
row switch native floppy off for the session (mega_fd13), and a fallback was
counted the moment the native attempt failed - before the BIOS had tried. An
EMPTY A: fails both, so an installer that walks both drives looking for the
system disk counted eight strikes against a path that had never failed a
disk it could read. 86Box does not show it: its empty drive asserts the
change line, which the native path hands to the BIOS uncounted. **Now the
BIOS decides**: the fallback is marked pending (`[mega_fdpend]`) and
`mega_fdjudge`, on `mega_svc_back`'s way out of that same call, counts it -
and puts the first-fallback toast up - only when the BIOS COMPLETED what the
native path could not. A call the BIOS failed too is the disk's. The fault
`mega_svc_lo.r286+31 [Vector > IDT limit]` that `os88box faults` shows on
these runs is the 286 reflection's own triple fault (`lidt` null, `int 3`),
not a crash.

**T69 reads through BIOS0.** The PicoMEM v1 serves its disk from its option
ROM, so HDD.DRV's rung 1 has no IDE port to pair it with (52.1.1) and every
read of the video is a reflected `int 13h` - on a 286, a CPU reset each -
beside the card's own audio IRQs (61 a second, 14.31). kern_big runs both in
real mode and pays neither. OPEN: how many `int 13h` a second the play makes,
which says whether bigger reads would buy it back.

**MIDIRack**: `The PC speaker is busy` is back with the right MIDIRACK.O88,
and kern_big does not click. Sent: MR_SPKDIAG rebuilt on this tree, and a
diagnostic kern_mega whose menu-bar `RFL` line (and the PicoMEM's `Q` line
after it) appears after any second with a reflection instead of twenty or
more - a PicoMEM raising its IRQ a few times a second would never have shown,
and each one is a ~3 ms reset gap in speaker PCM. OPEN on both readings.

### 14.35 Field, the Victory 286 (2026-10-20): after 14.34, and what is deferred

**Install from a real floppy works** on 78547a50, first thing after boot,
with no native-fallback toast - so the native floppy path served it, and
14.34's verdict-after-the-BIOS change did what it was for.

**MIDIRack is SET ASIDE by the owner** for the final-shape work: the
speaker refusal (`The PC speaker is busy` with the right MIDIRACK.O88) and
the clicks kern_big does not have are both open, with MR_SPKDIAG and the
RFL-threshold diagnostic kernel (14.34) built and sent but not read.
Resume from those two readings.

**T69 is still slow on BIOS0 (PicoMEM v1), and it does not read as
STARVED.** The owner's observation is the useful one: a disk-bound play
shows as stalls - the player waits on the ring - and this one plays smoothly
at the wrong speed, *"like its timer is wrong"*. That points at the CLOCK
rather than the disk: the Video Player paces on [ticks] (or the card), and
on a 286 every reflected `int 13h` is a reset whose time `mega_svc_back`
puts back into [ticks] from the BDA tick count (40:6C) - which only advances
if the ROM's own IRQ0 handler ran during the reflection, at 18.2 Hz
granularity. A reflection shorter than a tick, or one with IRQ0 masked,
loses that time from [ticks] altogether, and at hundreds of reflections a
second the video clock runs slow by exactly the time spent in them: a
picture paced to a clock that is itself behind never looks starved. **To
read before deciding**: the play's `int 13h` count a second, and [ticks]
against the PIT across a play. The owner's fallback - FSX on a BIOS-type
disk running the player in real mode for the bracket - is the decision
deferred behind that reading; it costs the bracket every byte above 1MB.

**The empty-drive freeze is back, and it is a STATE issue** (owner,
2026-10-20, on 78547a50): Install touched an empty floppy drive and the
machine froze, as in 14.31 - but only after the OS had been in use for a
while; first thing after boot the same install worked (above). So something
an earlier session of use leaves behind (the 765, the BDA's floppy bytes,
the native path's own state, a reflection's masks) is what the empty-drive
access then trips on. DEFERRED by the owner - recorded to come back to, not
being pursued now.

### 14.36 A reflection lost a rate bracket's periods: the clock T69 plays slow on (2026-10-20)

**The mechanism 14.35 reached for, found in the code.** A reflected call
hands the CPU to the BIOS with the IVT's int 08h the ROM's own, so every
IRQ0 that lands while it is out is the ROM's: it counts it at 40:6C and
nothing of the kernel runs. `mega_svc_back` then added the 40:6C difference
to `[ticks]` as ticks - which is right only at the ROM's rate. Inside an
FSXF_RATE bracket channel 0 runs at the BRACKET's divisor, so the ROM
counted the bracket's periods as ticks (the time of day ran ahead and
`[ticks]` was over-credited), and the periods themselves were never paid to
the hook - `[sch_rpend]` never heard of them. The Video Player paces its
frames on that hook (`vp_hook`, `[vp_owed]`), so every reflected `int 13h` on
a BIOS-served disk, and every PicoMEM IRQ reflection beside it (61 a second
at 3.3 ms in 14.31), was time its clock never saw: **a picture paced to a
clock that is itself behind plays slow and never starves**, which is the
owner's description. kern_big has no reflection, so it never happens there.
FASTTICK brackets had the same defect in the other unit.

**`mega_rfclk` pays them the way `sch_isr` would have**: under a rate
bracket the periods go to `[sch_rpend]` (the hook gets them all on its next
call) and their PIT counts into `[sch_racc]`, whose carries are the true
ticks; under FASTTICK `[sch_fcnt]` is counted down; and the time of day is
taken back by the ROM's surplus. Outside a bracket, and under the speaker's
(which already sets channel 0 to the ROM's rate for the call), nothing
changes. kern_mega only, `.mega`, ~110 bytes.

**Measured on 86Box's 286 (MR BIOS, 1MB so the play streams, native floppy
forced off so every read is reflected)**, a 20 s clip whose frames carry
their own timecode, sampled every 5.13 s of emulated time from Play:

| kernel | picture's timecode at each sample |
|---|---|
| 78547a50 | 0, 0, 1.467, 4.400, 9.400, 14.533, 19.667 |
| with `mega_rfclk` | 0, 0, 1.533, 4.533, 9.667, 14.800, 19.933 |

The old clock lost ~0.27 s over the clip and the new one ~0.07 - small,
because a reflection on 86Box is cheap and its floppy is quick; the Victory's
PicoMEM pays ~3.3 ms a reflection hundreds of times a second, which is where
the difference is the whole slowdown or none of it. **The owner's T69 run
on this kernel is the reading**; the real-mode-for-BIOS-disks fallback
(14.35) stays deferred behind it.

### 14.37 Claims of any size MOVE and REGROW in the pool; drivers' images move too (2026-10-20)

The owner's goal: *"If it's free RAM, they can get it into a single claim"*.
A program that needs 3MB should get 3MB whenever 3MB of the pool is free,
however that free space is cut up. Before this section, 14.8 decision 2 and
14.15 decision 2 pinned the two things that cut it up: an `OSAPI_MEM_HUGE`
claim (any block over 64KB) stayed where it was, and so did a driver's image
(`MX_KOWN`). One of either in the middle of the pool walled off the free KB
on both sides for the rest of the session.

**What is built.** All of it is kern_mega only: `kernel/mega.inc`, two lines
of `kernel/driver.inc`, and one instruction moved in `kernel/xmem.inc`.

1. **The compactor moves a claim of any size** (`mx_compactf`). The owner
   byte alone now decides what moves, at any size: `80h | slot` (a region),
   `0A0h | slot` (a program's claim), `0C0h | row` (a driver's claim) and
   the new `MX_KDRV` (0FDh, a driver's image). `MX_KOWN`, 0FFh and a bare
   slot (`OSAPI_XMEM_ALLOC`'s blocks) stay where they are. A huge claim is
   therefore refused only when the pool's FREE KB are short, because
   `mx_alloc` packs every movable block down before it refuses.
2. **`mx_cpmove` moves a block a 64KB PIECE at a time.** For each piece, the
   copy (two 32KB `mega_lincopy` calls) and the rebase of every LDT
   descriptor whose base is in that piece happen in one step with
   interrupts off. Interrupts are on between pieces.
   - A copy only overwrites bytes of pieces already moved, because the
     destination is below the block or clear of it. An ISR that reads or
     writes the block through any selector therefore finds each piece
     whole, at either the old place or the new one. Its writes to a piece
     not yet moved are carried with that piece.
   - That holds for a descriptor that lies inside ONE piece (offset in its
     piece + limit <= FFFFh). Every window of a run, a region's pair and a
     claim's selector qualify. If any descriptor over the block crosses a
     piece's end, the whole block moves in one step, as every block of 64KB
     or less always did.
   - The rebase is 24-bit now, for a block of megabytes. A move may also go
     UP to a place clear of the block; the regrow uses that.
   - `mega_slidef`'s GDT scratch (`MEGA_SEL_SLD`, the source slide of an lz
     decode) is rebased too, at the end of the move, because only a task
     loads it.
3. **`mx_alloc` is one step for every task.** It holds `[sch_lock]`, with
   interrupts on, across the walk, the take and any compaction. No other
   task runs mid-move, and two claims cannot interleave their walks.
   `mega_claimhf`, `mega_hugef`, `mega_claimregrowf`, `mega_imghif` and
   `xm_copy` hold the lock from the moment they read a block's base until
   they have used it. `mega_dklibf` reads its base and makes the alias with
   interrupts off. Without that, another task could compact the block away
   between the read and the use. Each of these was already a race for
   14.15's moves.
4. **A huge claim is marked `MX_FHUGE`.** The flag lives in `MX_FL`, byte 5
   of the entry, which was unused; every take clears it. `mx_unsel` frees
   `(KB + 63) / 64` selectors when the flag is set. This also fixes a
   driver's huge claim (`0C0h | row`): the old `owner = 0A0h` test leaked
   its run.
5. **`OSAPI_MEM_REGROW` takes a huge claim to ANY size and keeps it in the
   pool** (the `.huge` arm of `mega_claimregrowf`).
   - The bytes: a shrink or a level stays in place, and the block's tail is
     free at once. A grow takes the KB above the block if they are free.
     Otherwise it moves the claim into a fresh block of the new size
     (`mx_alloc`, compacting if it must, then `mx_cpmove`). If that is
     refused, the compaction has run, so it tries the KB above the block
     once more.
   - The run is cut, or extended where it is. When the LDT entries after it
     are taken, it moves to a free run of the new length and the old run is
     freed.
   - **DX is the claim's first selector NOW, and the step stays 8.** A
     refusal leaves `MX_KB`, the bytes and the run whole.
   - `mega_claimkbf` refuses a huge claim, so `osapi_mem_regrow_x` never
     moves one down to a conventional claim. The old path did (see the red
     run below): it answered a step of 1000h where the program walks by 8,
     and its word count overflowed past 64KB.
6. **Drivers' images move** (`MX_KDRV`). They are still placed from the top,
   like the kernel's own blocks, and the compactor slides them down like
   any claim.

**Task decision 3: is a driver image's move a small step? Yes, and it is
built.**

- On kern_mega every holder of a driver image names its SELECTOR pair.
  HEAP-UNPIN-PLAN's 66 kernel words in 9 tables hold segment numbers on
  kern_big but selectors here, so they do not change.
- The IDT gates and the `DSV_*` far pointers are selector:offset.
  `DESK.DRV`'s library alias is an LDT descriptor inside the image, which
  the rebase finds.
- The image's own code runs from IRQ0 (`DSV_TICK`) and from its own IRQs.
  An image is 64KB or less, though, so it moves in one step with interrupts
  off.
- `OS88_DRV_HIGHOK` already promises that the driver hands its own segment
  to no BIOS call and no DMA engine.

So HEAP-UNPIN-PLAN 2.0's wall, a driver mounted mid-session and pinned
mid-arena, is gone on kern_mega for every driver placed above 1MB. The cost
is the owner byte: the compactor's test and two `mov bl` instructions. A
driver that is NOT `HIGHOK` stays conventional under kern_big's rules.

**What makes a block legitimately unmovable at an instant.** The rule, kept
exact: a block is unmovable while something names it by a LINEAR or
PHYSICAL address. Case by case:

- **A DMA in flight.** The only DMA into a pool block is `mega_fd13`'s
  native floppy, into the caller's ES:BX, which may be any selector. It
  computes the linear address and runs the whole transfer under
  `[sch_lock]`. The compactor runs only inside `mx_alloc`, in a task and
  never from an ISR, so it cannot run during the transfer. The Sound
  Blaster ring is a conventional `MC_DMA` claim, and the NE2000 is PIO.
- **A BIOS call using the block through a bounce.** A reflection copies
  between the caller's selector and a conventional bounce buffer and
  re-reads the selector for each chunk. It runs with every IRQ but the
  BIOS's masked, so no task runs under it. Between chunks the selector
  names the new base.
- **A far call into the block** (a region's or a driver's code running).
  Every holder of its CS is a pushed CS: a return address or an iret frame,
  which reloads on `retf` or `iret`. The stub pushes DS and ES, and they
  reload on their pops. The compactor reloads its own ES. SS is never a
  pool selector.
- **A linear address held over time.** `OSAPI_XMEM_ALLOC`'s blocks (the
  package holds the address) and the kernel's own `MX_KOWN` blocks stay
  pinned as a class. The `MX_KOWN` blocks are the read-ahead store, the
  icon store, purgeable tags, module pairs (`mega_hiclaim2` hands its
  caller the linear base) and the hibernate's staging block at
  `[mega_dpbase]`.
- **A linear address held for a moment.** Covered by `[sch_lock]` or cli,
  as item 3 above describes.

**The test.** `tests/megahg` (MEGAHG, new; `make build/megahg.o88`) runs
these phases:

1. It fragments the pool with huge claims of a sixth of it, with 60KB
   claims between them, and tags every byte.
2. It unloads the drivers (`OSAPI_DRV_SUSPEND`), plugs the KB their images
   held, frees every other pair, and reloads the drivers. That puts a
   driver's image mid-pool.
3. It claims EVERY free KB in one `OSAPI_MEM_HUGE` and checks every byte of
   that claim and of every survivor. It then unloads and reloads the
   drivers again, which runs their code at the place the move put them.
4. It regrows a run from 100 to 300KB (a move and a relocated run), down to
   130KB, up to 400KB, and finally to every free KB across a compaction. It
   checks R, X1 and X3 each time.
5. It checks that the pool's free KB are back to what they were at the
   start.

**Measured:**

- **Bochs (a 386), 4MB, SB16, `SOUND.DRV` wanted.** Command:
  `OS88_BOCHS_MEGS=4 python3 tools/os88bochs.py sweep --sb --cfg 1
  build/megahg.o88`.
  - `MEGAHG: frag 0E holes free 03FE`: 14 claims, and the largest hole is
    515KB.
  - `big 29DC kb 03FE ok`: 1,022KB in one claim. That claim's compaction
    moved SOUND.DRV's image from KB 806h to 408h.
  - `drivers unloaded and reloaded`, then all four regrows ok (the last is
    `regrow 2904 kb 078A`), and `MEGAHG: ok`.
  - Without `--sb`: 1,213KB in one claim, ok.
  - MEGACP, MEGAHI and MEGAXM still pass in the same boots. MEGAXM needs
    `--put RUN:ZBIG.TXT` for its packed-file leg.
- **86Box `286-mega`** (MR BIOS 286, a 16MHz 286, 4MB, SB16), booted with a
  `SYSTEM.CFG` that wants sound, and MEGAHG on a 1.44MB floppy in B:,
  double-clicked.
  - `MEGAHG ok` appears on the menu bar. The run made 63 soft resets, which
    is the 286's reflection path.
  - Sampling `mx_tab` every 250 ms of emulated time shows the same sequence
    as Bochs. SOUND.DRV's image was reloaded at KB 81Ah between two claims,
    moved to 412h by the 1,032KB claim's compaction, then unloaded and
    reloaded at the top.
  - The only faults other than the resets are five handled `Segment can't
    write` at a conventional code alias (2764h, base 9D800h). The UNFIXED
    kernel shows the same five for the same package, so they predate this
    change.
- **Red against the kernel before this change (89dcb2ee), on the same
  machine.**
  - `MEGAHG: no 02`: the every-free-KB claim was refused. The kept huge
    claims and the driver (reloaded mid-pool as `MX_KOWN`) wall off the
    holes.
  - Built with `-DHG_RONLY` (the regrow phase alone): `no 21`. The 100 to
    300KB regrow "succeeded" into CONVENTIONAL segment 1268h with step
    1000h, so the bytes the program walks by 8 were not its bytes.
- **The test found a defect in the first build of `mx_cpmove`.** A piece's
  rebase window ran past the block's end, so the last piece's rebase also
  moved the descriptors of the block ABOVE it: X1's first selector came back
  holding R's tag. The window is now clamped to the block.

**Cost, measured** (`make mega`'s `os88megaseg` lines, same commit):

| kern_mega section | before | after | change |
|---|---|---|---|
| `.cold` | 41,170 | 41,522 | +352 |
| `.mega` | 12,691 | 12,824 | +133 |
| `.text` + `.bss` | 54,074 | 54,074 | +0 |

- The segment overhead stays at the 5,935 that `tests/megaseg.txt` allows.
  The `xm_copy` change moves an instruction rather than adding one.
- The 485 new bytes are all above 1MB once `mega_coldup` has run. Before
  that, they cross one 512-byte rung each of `.cold` and `.mega`, so the
  boot-time conventional footprint is +1KB (`HEAP_PARA` 7,744 -> 7,808).
  `mega_coldup` gives that KB back to the heap.
- kern_big is BYTE-IDENTICAL: `build/kernel.bin` was compared with `cmp`
  before and after, at one commit.
- kern_small is unchanged by reading the diff: every change is inside
  `%ifdef KERN_MEGA`, and `xmem.inc`'s `%ifndef KERN_MEGA` keeps the old
  instruction where it was.
- `abilock`: `PKG_FMT` 10 and `DRV_VER` 11 are unchanged. The contract was
  clarified, and nothing built before it changes.

**Rows.** None of this is in `tests/suite.py`. Like MEGACP and MEGAHI,
MEGAHG is run by hand, with the `tools/os88bochs.py sweep` command above
and on 86Box as described; the header of `tests/megahg/megahg.asm` carries
both commands. The rows that can see the change are the fast tier
(`cswrite`, `seg0`) and the `os88megaseg` and `os88ovlchk` gates that
`make mega` runs. No soak row boots kern_mega.

**The interface left for "re-home high".** Re-home high (a re-homed
package's carve in the pool) is being designed in parallel and is NOT built
here. A carve of any size, with any number of selectors including CODE
aliases, can live in this as it is:

- **Claim it** with `mx_alloc` (`.mega`; far door `mx_allocf`): DX:AX =
  bytes, BL = **`80h | slot`**. That is the region class: movable at any
  size, freed by the region's teardown, and never handed to a cell. The
  claim may compact, moving other blocks, including the caller's current
  region.
- **Make its selectors** with `mega_ldt_alloc` (data or code; BL = the
  access byte) or `mega_ldt_alloc2` (a code/data pair). For a run, use
  `mega_hgcnt`, `mega_hgfree` and `mega_hgfill` (`.cold`). To have the run
  freed with the block, mark the entry `MX_FHUGE` and set `MX_SEL` to the
  run's first selector; `mega_mxat` finds the entry by base and owner.
  Every move rebases every present descriptor in the allocator's range
  whose base lies inside the block. The compactor never needs to know which
  descriptor is a code alias.
- **Move it on purpose** with `mx_cpmovef` (`.mega`, far): DI = the entry,
  CX = where it is, BP = where it goes (below it, or clear of it in either
  direction). It rebases the descriptors and sets `MX_OFF`.
- **The owner is told NOTHING**: no relocation proc and no message. A
  selector never changes, and every segment register that holds one outside
  the compacting call is a pushed register that reloads.
- **Invariants the carve must keep:**
  1. Every selector's window lies inside one 64KB piece, measured from the
     block's base. Otherwise every move of the block is one interrupts-off
     step for the whole block: still correct, but deaf for about 10 ms per
     64KB on a 12MHz 286.
  2. No linear address of the carve is held across a point where another
     task can run, except under `[sch_lock]`. A code alias made from a base
     is made in one step.
  3. No real-mode code runs in it.
  4. Any DMA into it holds `[sch_lock]` from programming to completion.

**What is left, and the price:**

- **A regrow that needs only the DIFFERENCE when movable blocks sit above
  the claim.** Today a grow succeeds in three cases: the KB above the claim
  are free; the pool, after compaction, has a run of the NEW size; or the
  compaction leaves the claim last below the free run. The layout
  `[R][B...][free F]` with `F >= new - old` but `F < new` is refused. The
  cure is an upward packing pass that slides B up past R; it is the
  descending half that kern_big's compactor has (SPEC.md 66.4.1). It would
  cost about 60-90 bytes of `.mega`, plus a BACKWARD 32KB copy, because an
  upward overlapping move cannot use the forward `rep movsw` that
  `mega_lincopy` is. Not built: it makes no difference to the first claim,
  which is the owner's case.
- **`MX_KOWN` blocks stay pinned as a class.** A module dropped from between
  two of them leaves a hole at the top that a single claim cannot reach.
  Most of them name only selectors (the read-ahead store, the icon store,
  purgeable tags), so they could join `MX_KDRV`'s rule. Module pairs need
  `mega_hiclaim2`'s linear answer audited first.
- **`mx_tab` has 24 entries** (`MX_NBLK` = `XM_MAX_BLKS` + 16). The kernel's
  blocks, the drivers and every package's claims share them, so on a long
  session the TABLE refuses before the RAM does. Each entry costs 8 bytes of
  `.text`, in the segment.
- **`mega_lincopy`'s XA/XB scratch descriptors** are safe only because every
  caller holds `[sch_lock]` or cli. Nothing enforces that.

**Decisions for the owner to look back over:**

1. **Interrupts are on between 64KB pieces**, rather than off for a whole
   block. Off would be simpler and is what 14.15 did. On a 12MHz 286,
   though, each move of a 3MB claim would then be about 0.5 s of a deaf
   machine, losing serial mouse bytes, keys and ticks. The piece rule keeps
   each deaf stretch at 14.15's 64KB worst case.
2. **`mx_alloc` holds `[sch_lock]`**, so other TASKS wait out a whole
   compaction. That is up to one copy of every movable block, about 10 ms
   per 64KB on a 12MHz 286. ISRs (the pointer, sound, the clock) keep
   running.
3. **A huge regrow may answer a NEW first selector**, when the LDT entries
   after its run are taken. The contract already says "take DX". A program
   that keeps the old selector in a pushed segment register past the call
   gets a not-present selector.
4. **A grow moves the claim into a fresh block before it compacts.** That
   costs one copy of the claim. Compacting first would sometimes avoid that
   copy, but it always moves everything else.

### 14.38 Re-home HIGH: the design, with a throwaway that ran four of the five above 1MB (2026-10-20)

The owner, on 14.14's decision 1: *"the goal here is 'everything works' - so
if you have word open, and then you go to play a video, word isn't sitting at
the top of 640k, blocking off access to the bottom of 640k to the video
player, unable to go anywhere and compact ... 'If its free ram, they can get
it into a single claim'. If we can't put them high, we can't compact them and
make that free space run."* This section is the research and the design. The
kernel side is NOT built: the measurements below come off a throwaway patch
(14.38.5) that was reverted.

#### 14.38.1 What 14.14 decision 1 got wrong: four of the five keeps are ONE segment

Decision 1 deferred the work because *"the carve it runs in is bigger"* than
64KB. That stopped being true with SPEC.md 20.12.10.5's KEEP form, which cuts
the carve's tail to the last part the program reads and moves its base up to
the program. Measured off the built `.o88`s (part tables read and the program
parts unpacked with `tools/os88pkg.py`'s `image_unwrap` and
`tools/os88lz.py`) and, for the keep, off the kernel itself - the throwaway
printed the `CX` paragraphs `OS88_REHOME` handed step 8a, on kern_mega under
Bochs:

| package | part | kind | bytes (unpacked) | reached by | in the keep |
|---|---|---|---:|---|---|
| Word | 0 | SEG, COMP | 59,392 (image 50,291 + bss 9,101 = `WD_P1ORG`) | its own CS/DS | yes |
| | 1 | SEG, COMP | 3,208 (`.modc` at vstart `WD_P1ORG`: the search pattern and the Utilities **code**) | near calls through its own CS, `cs:` data | yes |
| | **keep** | | **62,608** | | **one segment** |
| Scribe | 0 | SEG, COMP | 51,712 (43,409 + 8,303 = `SC_P1ORG`) | own CS/DS | yes |
| | 1 | SEG, COMP | 9,222 (`.modc`, code) | own CS | yes |
| | **keep** | | **60,944** | | **one segment** |
| Clear Skies | 0 | SEG, COMP | 61,409 (41,045 + 20,364) | own CS/DS | yes |
| | 1 | ASSET, LAZY, SELF | 9,277 (the strings) | read by the program itself into its own memory | no |
| | **keep** | | **61,424** | | **one segment** |
| DOS.O88 (kern_mega's own, `-DDOS_MEGA`) | 0 | SEG, COMP | 51,088 (44,465 + 6,623) | own CS/DS; the loader copies part 1 INTO it at `CORE_ORG` | yes |
| | 1, 2, 3 | ASSET, LAZY, COMP | 16,038 / 35,721 / 33,952 (the INT 21h core, kern_dos for the HMA, kern_dos low) | fetched and dropped by the loader; 2 and 3 read off the disk by HIBER.DRV at a trip | no |
| | **keep** | | **51,088** | | **one segment** |
| Pixelstein 3D | 0 | SEG, COMP | 58,263 (33,815 + 24,448) | own CS/DS | yes |
| | 1 GEN | SEG, ZERO, OPT, 50KB | scratch the scalers are **generated into and far-called** (`px_drvp`, `px_qcur*`) | a SEGMENT the loader stores in the handoff (`PXH_GEN`) | yes |
| | 2 BT | ASSET, ZERO, OPT, 30KB | the byte textures, data | a segment in the handoff (`PXH_BT`), copied into GEN at `PXG_QTEX` | yes |
| | 3 LEV, 4 ART | ASSET, LAZY, COMP | 9,831 / 37,440 | `op_fetch` into conventional claims of their own (the DMA door) | no |
| | **keep** | | **141,312** (SPEC.md 20.12.10.5's table) | | **2.2 segments** |

Pixelstein's loader also claims the 60KB sprite set (`PXS_KB`) with
`OSAPI_MEM_CLAIM`; with ART and LEV that is ~107KB of slot-owned data claims
OUTSIDE the keep, conventional and never moved (`px_reloc`'s own comment
says so).

What each program does with its own segment, against item 78's rules (a grep
of every source file each program part assembles, and the four runs below):

- **No arithmetic on its own CS or DS** in Word, Scribe or DOS.O88's
  reachable kern_mega paths (`dos_psp_make`'s `mov ax, cs / add` belongs to
  the windowed box and to kern_dos, and the windowed box never runs on
  kern_mega). **Skies has ONE**: `cs_xpand` decompresses into "any paragraph
  of our segment" as `ES = our segment + granule` - the first thing the
  throwaway faulted on (`#GP`, selector 369Ch, at `cs_xpand + 12h`).
  **Pixelstein's loader has two by construction** (`PXH_GEN` and `PXH_BT` are
  `op_seg` answers: the carve's base plus paragraphs); the program's own
  `px_gen_init` `mov ax, ds` is a register load and fine.
- **No linear address of its own DS, no IRQ vector with its own CS, no DMA
  or BIOS buffer in its own segment** in any of the five. Their only
  `int 10h` calls are register-only (the CGA palette, the cursor). Skies'
  `csdiag.inc` stamps its own CS into vector 8, but only in the `CSDIAG=1`
  diagnostic build, which must not say `OS88_HIGH`.
- **CS writes** (`mov byte [cs:wd_hashid], 1` and kin) trap through
  `mega_gpw` exactly as they do low (item 87: a low package's CS is already
  its LDT alias), so they are not a placement question.
- **Workers**: Word, Scribe and Pixelstein spawn one and declare
  `OS88_WORKER_RESTARTABLE`; all five declare `OS88_REGION_MOVABLE`. Neither
  matters in the pool (14.38.3).

#### 14.38.2 The layout: one pool block per program, its selectors inside it

**The carve that puts the program LAST buys nothing now, and is refused.**
The KEEP form needs the program FIRST - the head trim returns everything
below it to the heap (SPEC.md 20.12.10.5: "the re-homed part must be the
LOWEST part of the run") - and the tail cut already separates the program
from every part it does not read. What decision 1 wanted from "program
last", a region that can leave without the rest of the carve, is what the
keep already delivers for four programs.

The layout, for every re-homer:

1. **The kept carve becomes ONE pool block**, owner `80h | slot`. That is
   what `ld_hi` makes for an `OS88_HIGH` package (item 78), so the teardown,
   `mem_own`, `MEGA_CSELTOP`/`MEGA_PCALL`, `mega_frame_cs`, the Task Manager
   and the compactor already know it.
2. **The program's code/data PAIR over its base**: the data selector named
   wherever the segment was (`[ld_base]`, `I_SPTR`, `W_SEG`), the code half 8
   below, the limit the keep or 64KB, whichever is smaller. Word's and
   Scribe's part 1 are inside that limit, so their near calls and `cs:`
   reads reach it unchanged.
3. **Only past the first 64KB (Pixelstein) does a part need a selector of
   its own**, and its base is the PART's first byte, not a 64KB tile: GEN
   starts 58KB in and ends 108KB in, so `OSAPI_MEM_HUGE`'s tiling (a
   selector per 64KB, step 8) would have it straddle a tile. Each such
   selector is a data descriptor, or a code/data pair for a part the program
   far-calls (GEN). **The compactor needs to know none of this**: 14.15's
   rebase walks every present LDT descriptor whose base lies inside the
   block, so a pair, a part's selector and a regrow's scratch window all
   move with it. 14.37 is what lifts the 64KB limit on that walk and on what
   `mx_compactf` will move.
4. **The teardown frees the block and every LDT descriptor whose base is
   inside it**: the same scan, the other way. That replaces "the pair" in
   `mega_imghifree` and makes a part selector impossible to leak.

Alternatives weighed and refused:

| layout | why not |
|---|---|
| the program pair high, the rest of the carve left conventional | Pixelstein's GEN and BT (80KB) stay low and PINNED: a worker runs inside GEN, and a claim has no frame rule (SPEC.md 66.6.2), so it cannot be a movable claim on its own |
| the block tiled by 64KB like a huge claim, the program stepping by `CX` | parts straddle tiles; padding them to tile boundaries is 192KB against 138 |
| Pixelstein's scratch parts as claims of their own | loses "sized before a sector is read", the parts standard's reason to exist, and on kern_big pins GEN for the reason in row 1 |
| claim the carve FROM THE POOL, so it is never conventional at all | the end state, but `op_read`, `op_unpack` and `op_seg` are paragraph arithmetic on `[op_base]`; deferred (14.38.6 D4) |

**The re-home cell's contract does not change on any kernel.** What changes
is step 8a on kern_mega, and only for a program that asks: **the opt-in is
`OS88_HIGH` in the PROGRAM PART's own header** (+15, bit 7), not the
loader's. The loader is freed one instruction after it returns, so its
placement is worthless (14.12), and the program is the one making the claim
the bit makes, *"I do no arithmetic on my own segment"*. Every kernel built
before this, kern_big and kern_small ignore the bit on this path (none of
them calls `ld_hi` there), and the class mask (`and bx, 3Fh`, item 81) keeps
the program's stack class right everywhere. So **no package built before
changes behaviour, and a new one on an old kernel runs low**, which is not a
refusal. No `PKG_FMT` bump: the SDK change is `OS88_HIGH`'s comment (a
re-homed program may say it) and, for Pixelstein only, one appended cell
(14.38.4 S5).

The rule step 8a applies, after the trim, the cut and the loader's free:

- protected mode, the program says `OS88_HIGH`, the KEEP form was used
  (`AX = 0`) and the keep is at most 4,095 paragraphs: `ld_hi`'s move with
  `CX` = the keep in bytes. No room or no LDT entry means it stays low,
  exactly as an `OS88_HIGH` package does;
- the same with a keep over 4,095 paragraphs: one block of the keep, the
  pair over its first 64KB (S4, after 14.37). Until S4 lands it stays low;
- anything else: today's behaviour.

#### 14.38.3 What pins a re-homed program, low and high

**Low (today, and the fallback):** SPEC.md 66.6's rules. The region moves
only when `mem_frameless` finds no frame - a worker parked in
`OSAPI_TASK_ALIVE` and declared restartable (66.6.2), no callback in
`[wm_pkgs]`, not mid-launch - and only by its relocation proc's word.
Pixelstein's `px_reloc` is the one proc in the five that is not a `ret`.

**High (this design):** the pool compactor moves an `80h` block of 64KB or
less **at any instant it runs**, with no relocation proc and no restart:
the selectors do not change, every holder is a pushed register that reloads
on its pop, and the copy runs with interrupts off, so the FSXF_RATE hook
(Skies, Pixelstein) never sees half a move (14.15). So in the pool:

- `OS88_WORKER_RESTARTABLE` and `px_reloc` stop mattering, harmlessly:
  `OSAPI_MEM_MOVABLE` refuses a pool pair (CF = 1) and `OS88_REGION_MOVABLE`
  ignores the answer, so the proc is never registered and never called with
  a selector it would add a delta to.
- All five can be movable with no change of their own except Skies'
  `cs_xpand` and Pixelstein's handoff (14.38.4).
- **What would pin one** is item 78's list for any high package: a linear
  address of its own DS handed out (none of the five), an `OSAPI_XMEM_ALLOC`
  block (a different block, not the region), or a block over 64KB until
  14.37.
- **One case nothing has tested**: a region moving while its OWN program is
  the caller (14.15's "still open"). A re-homed program that also says
  `OS88_HIGHMEM` reaches `mx_alloc` from its own callback, and a compaction
  there may move its own block. The argument (the cell's `retf` reloads CS)
  covers it; S2's gate must exercise it rather than trust it.

#### 14.38.4 The implementation plan

Costs marked MEASURED come off the throwaway at `89dcb2ee`; the rest are
estimates.

| step | files | what | bytes | depends on |
|---|---|---|---|---|
| **S1** | `kernel/loader.inc` | `ld_hi` split so its body takes CX (`ld_hicx`); step 8a's arm after `mov [ld_base], ax`: the keep form, a keep of at most 4,095 paragraphs, `ES` = the program, `call ld_hicx`. `[ld_base]` is the pair's data selector before `.call8`, so step 8's `MEGA_PCALL` and step 9's `I_SPTR` need nothing | **kern_mega `.cold` +30, MEASURED** (41,170 -> 41,200); `.text` and `.mega` +0, the megaseg overhead unchanged at 5,935; kern_big and kern_small +0 (all inside `%ifdef KERN_MEGA`) | nothing |
| **S1 docs** | `SPEC.md` 20.12.10 (a new subsection after 20.12.10.8), `apps/os88api.inc` (`OS88_HIGH`'s comment: a re-homed program may say it, and a loader gains nothing by it), `tools/abilock.py --update` (no bump) | the contract above | 0 | S1 |
| **S2** | `apps/word/word.asm`, `apps/scribe/scribe.asm`, `apps/dos/dos.asm` (its `DOS_MEGA` header only, so kern_big's `DOS.O88` stays byte-identical) | `OS88_HIGH` in the program's class byte | **+0** (one header byte each) | S1 |
| **S3** | `apps/skies/skies.asm` | `cs_xpand`: when ES (our segment) is an LDT selector, expand into a scratch claim at ES:0 and copy into place - or, with S5, ask for a selector at the target paragraph and expand there directly. Then `OS88_HIGH`, and NOT in the `CSDIAG=1` build | the scratch arm **+96, MEASURED** (the throwaway's, unoptimised); the SUBSEG arm ~+15 (estimate) | S1 (S5 for the small arm) |
| **S4** | `kernel/mega.inc` (`.mega`), `kernel/loader.inc` | a keep over 4,095 paragraphs: one `80h` block of `ceil(keep / 64)` KB, the copy in chunks of 32KB or less (`mx_cpmove`'s loop), the pair's limit 64KB; `mega_imghifree` frees every LDT descriptor whose base is inside the block (the rebase scan, freeing) | ~100-140 `.mega` and ~10 `.cold` (estimate) | **14.37**: `mx_compactf` must move an `80h` block over 64KB, and `mx_cpmove`'s rebase must compare 24-bit offsets (today `or dl, dl / jnz .dn` stops at 64KB). Without 14.37, S4 still runs and the block is pinned in the pool, which is already better than pinned in conventional memory |
| **S5** | `kernel/kernel.asm` (an appended cell), `kernel/mega.inc`, `apps/os88api.inc`, `SPEC.md` 50.3 | `OSAPI_MEM_SUBSEG` (name to taste): AX = a paragraph offset into the CALLER's own region (ES = the X stub's), BL bit 0 = it will be far-called -> CF = 0 and DX = a data selector whose base is that paragraph and whose limit runs to the block's end or 64KB, the code half at DX - 8 when asked. A descriptor that already has that base and type is returned instead of a new one, so the LDT use is bounded by the distinct paragraphs a program asks for. **CF = 1 means "you are not above 1MB: use your segment + AX"**, which is the whole body on kern_big and kern_small (`mod_gone`) and on kern_mega for a low region | the 6-byte cell on every kernel; kern_mega ~70-90 `.mega` and a door (estimate); `abilock` additive | S4 for its main customer, S1 for Skies |
| **S6** | `apps/pixelstein/pxstein.asm`, `pxgame.asm`, `pxgen.inc` | the loader writes PARAGRAPH OFFSETS from the program (`op_seg(part) - op_seg(0)`) into `PXH_GEN` and `PXH_BT`; the program's entry turns each into a segment (`OSAPI_MEM_SUBSEG`, or DS + offset on CF = 1); the far-pointer words (`px_drvp + 2`, `px_drv2p + 2`, `px_qcur* + 2`) take the CODE half and the data words (`PXG_QTEX`, the ES loads) the data half; `px_rltab` unchanged, it is kern_big's path; `OS88_HIGH` | ~+4 loader, ~+30-40 program (estimate) | S4, S5 |
| **S7** | the parts standard, the five programs | the claims OUTSIDE the keep (14.38.6 D5) | per package | after S6 |

**What to test, and on what.** Every step's artefacts move on kern_big too
(a header byte, Skies' and Pixelstein's code), so the kern_big rows come
first: `rehome rehome360 rehomemove rehomemove360 rehomeabort wdparts pxparts
dosarena wdtype skies skiesworlds skiesset pixelstein pxsfsx` (MartyPC). On
kern_mega:

- **Bochs** (`tools/os88bochs.py sweep`, at 4MB and 16MB): each of the five
  opens; Word's Edit > Search finds a word (part 1 code through the high
  CS); Skies flies and comes back; a DOS program runs and returns; and a
  **compaction** MEGACP never had - start two high programs, close the
  lower, claim to force `mx_compactf`, then use the one that moved (Word's
  search again; Pixelstein after S4). Worth a `tests/megarehome` row:
  MEGARUN starts Word, Scribe and DOS.O88 and asserts each `I_SPTR` is an LDT
  selector, and reads the conventional heap's largest run.
- **86Box `286-mega` / `286-video`**: the same five, plus a DOS trip - the
  286 reset path with a re-homed pair in the pool and the park summing it,
  which Bochs (a 386) cannot take.

#### 14.38.5 What the throwaway measured (Bochs 2.7, 16MB, kern_mega at `89dcb2ee` + S1)

The patch was S1 plus an E9 line, and `OS88_HIGH` added to the five
programs' headers; all of it reverted. S1, as it ran:

```
ld_hi:
    mov cx, [ld_need]
ld_hicx:                        ; ...the rest of ld_hi, CX now an input
    ...
    mov [ld_base], ax           ; (step 8a, after the loader's free)
%ifdef KERN_MEGA
    cmp word [ld_rehsz], 0      ; the KEEP form only...
    jne .rhnohi
    mov cx, [ld_rehkp]          ; ...and a keep of one segment
    cmp cx, 0x0FFF
    ja .rhnohi
    shl cx, 1                   ; (four of them) paragraphs -> bytes
    ...
    mov es, ax
    call ld_hicx
.rhnohi:
%endif
    push ds
    pop es
    jmp .call8
```

| program | data selector, keep (E9) | result |
|---|---|---|
| Word | `283C`, `F490` = 62,608 | opens; text typed; **Edit > Search found "zebra"** - the search is part 1, at `WD_P1ORG` = E800h and up, run through the high CS |
| Scribe | `284C`, `EE10` = 60,944 | opens and draws |
| Clear Skies | `285C`, `EFF0` = 61,424 | **#GP** in `cs_xpand` (above). With the throwaway's scratch arm the title bands draw, **Fly** loads PARIS-ISSY and renders the cockpit in its fsx bracket, and Esc comes back to the title |
| DOS.O88 | `2834`, `C790` = 51,088 | opens; a .COM run from it: `parked 0052K`, the program printed and waited for a key, `unparked`, then `exit code 007 (Arena: 621KB)` |
| Pixelstein | none (keep 141,312, over 4,095 paragraphs) | opens LOW, unchanged: the fallback |

Nothing faulted but Skies' first run. Not run: a hibernate with a re-homed
program high, the 286 (86Box), and a pool compaction moving one.

#### 14.38.6 For the owner

- **D1. The opt-in is `OS88_HIGH` in the program part's header**: no new
  bit, no new cell form, no `PKG_FMT`. Recommended: it is the claim the bit
  already makes, every old package is untouched, and a new one on an old
  kernel runs low.
- **D2. Pixelstein, the one keep over 64KB:** (a) S4 + S5 + S6, one block
  with per-part selectors - recommended, being the only layout that keeps its
  parts sized before a read and is movable once 14.37 lands; (b) leave it
  low: it is a fullscreen game, not a program left open beside a video, and
  S1-S3 are the owner's case; (c) split its carve into claims - refused
  (14.38.2).
- **D3. Skies' `cs_xpand`:** the scratch-claim arm needs no kernel and was
  measured (+96, and a claim of a few KB for an instant per band); the
  SUBSEG arm is smaller (~+15) but waits for S5. Recommended: the scratch
  arm now unless S5 is taken.
- **D4. A launch still needs the carve's room in CONVENTIONAL memory for a
  moment** (Word ~62KB, Pixelstein ~141KB), because the parts standard reads
  it there before the kernel lifts it. With everything else high,
  conventional memory is mostly free, so this rarely bites; claiming the
  carve from the pool directly is a parts-standard rewrite and is deferred.
- **D5. "Everything works" needs the programs' CLAIMS too.** The region is
  at most half of each program's conventional footprint: Word and Scribe
  make 9 and 12 `OSAPI_MEM_CLAIM` calls and declare none movable, and
  Pixelstein's sprite set, art and level (~107KB) are slot-owned and never
  moved. Each is an `OS88_HIGHMEM` census of that program (14.13's rules)
  and, for Pixelstein's two lazy parts, an `op_fetch` arm that expands into a
  pool selector (the packed stream is read R paragraphs up its own claim
  today, which is arithmetic). Recommended as S7, Word and Scribe first:
  they are the programs left open.
- **D6. How long a huge region may move with interrupts off** is 14.37's
  question, and Pixelstein's 138KB is its largest customer: ~35 ms at the
  Victory's measured 0.257 ms a KB above 1MB (KERN-MEGA-DOS-PLAN 2.4).

### 14.39 The trampoline's segment stores, the alias guard, the mouse ISR's SS, and two ratchets (2026-10-20)

Seven items 10.3 and 14.9 left open. Each is a commit of its own.

**1. `mega_gpw` stores the caller's DS and CS** (10.3 items 60 and 63). The
trampoline runs a CS: store with DS = the image's data segment and CS =
KERNEL_SEG, so `mov [cs:x], ds` stored the image's segment and `mov [cs:x],
cs` stored 0060h. An `8C /3` or `8C /1` is now rewritten into the trampoline
as `C7 /0` (two bytes longer, so the copy limit is MEGA_TRN - 2) with the
value the code had as its immediate: the faulting DS, or its own CS. ES and
SS need nothing, the trampoline leaving them as they were. The package sites
(`apps/os88spk.inc`'s two, `lmpvmod.asm:130`, `wcanvas.asm:89`) are LEFT as
they are: the kernel is the one place that is right for every package, old
ones included, and converting them through AX would only help an older
kern_mega nobody ships.

**2. A guard in the no-prefix arm** (item 59). `mega_cs2dsw` is
`mega_cs2ds` for DS or ES when a write is about to go through them: an LDT
alias below MEGA_LDT_FREE0 converts only when its segment lies inside a live
heap claim, walked through LOW_SEG (not SS, which is whatever stack faulted).
Anything else falls to `mega_null0` and on to `mega_report`. DS or ES equal
to the faulting CS - `push cs / pop ds`, the common case - skips the walk.
It NARROWS rather than closes the hole: an alias landing inside another live
claim still converts. The CS: arm and the reporter keep the unchecked
`mega_cs2ds`, CS being the image that is running. Items 1 and 2 are `.mega`
+117 bytes. kern_big and kern_small are untouched by them.

**Measured** under Bochs with `tests/megacsw` (MEGACSW says OS88_HIGH,
MEGACSL does not). The base kernel (89dcb2ee) stored CS as 0060 BAD and a
foreign DS as the image's segment BAD, and completed MEGACSL's guard store
through the alias of 0020h ("guard WROTE"). This one reads every value back
ok, in both packages, and reports the guard store as `kern_mega FAULT
vector 0D` at the instruction.

**3. The mouse ISR sets SS** (14.9, left as found 1). This is SPEC.md
9.10.1, a SHARED fix. MOUPRIV_ENTER banks SS:SP and loads
LOW_SEG:mou_pstack, so a body interrupting a ROM's own stack no longer runs
`gfx_rowbase` and `evq_push` against the ROM's segment. The compare is gone,
since `mov ss, [cs:mou_lowss]` needs no register. Measured cost:

- **kern_big** `.text` +12, which includes three never-executed parity pads
  so that every routine `t_hotpar` pins keeps its parity (`os88rdiff` reads
  every run at an even shift), and `.bss` +2.
- **kern_small** `.text` +10 and `.bss` +2.
- **kern_mega**: its entry is the size it was, so its segment overhead falls
  5,935 -> 5,931. `tests/megaseg.txt` is lowered with it.

The race cannot be won on demand. A QEMU/gdb count over 59 int 13h and
2,241 ISR entries met a foreign SS 0 times; this QEMU's SeaBIOS waits
inside its handlers with IF off, and Bochs's legacy BIOS does not hop. So
`tests/msstk` makes the case on purpose: SS:SP in an A5h claim, IRQ0 masked,
IF on. `tests/mousess.py` (soak, MartyPC, 17 s) is the gate. On the old
entry it reads 16 bytes of the claim written at offset 1336h, which is
`evq_buf`. On this one 63 of 72 ISR entries arrived on the claim's stack,
none of the bodies ran off LOW_SEG, and the claim came back untouched.

kern_mega was booted after the change:
- Under Bochs: the PS/2 mouse drives every MEGARUN sweep above.
- On 86Box `286-mega` (MR BIOS 286, the reset path): the desktop came up, a
  120x60 relative walk landed where the kernel says, B: opened through the
  disk path, and `faults` showed nothing outside the BIOS's own shutdown
  resets.

**4. `apps/os88api.inc`'s OS88_HIGH note** no longer says a RAM disk or a
cable share is unsupported for a buffer above 1MB, which item 81's
`drv_fs_hi` bounce closed. Comment only: `abilock` passes with no update.

**5. `font_run`'s SS rule is every call's rule.** No shipped package calls
the kernel on a stack of its own. The DOS box switches SS around a DOS
program and back before it calls in, and on kern_mega it is the
whole-machine handoff anyway. The one kernel path on a foreign stack was
the mouse ISR, which 3 closes. The rule is not `font_run`'s alone, either:
the claim map and `gfx_rowbase`'s row table are SS-relative on every kernel.
So the SDK's environment note now states it for every OSAPI call, and
`font.inc` says who keeps it. Comments only.

**6. `tests/unit/t_megagrid.py`** (fast, 0.6 s) is the "eight paragraphs,
never one" ratchet. It flags three shapes, each ending in a `mov ds|es`
within a few lines, in every file of a package that declares OS88_HIGH:
- a far pointer normalised to one paragraph;
- a byte count rounded up to paragraphs;
- a segment step that is not a multiple of 8.

Counts are held per file by `tests/megagrid.txt`. It finds 7 sites in 5 of
302 files, and fails on all three shapes injected into HELLO.
- `pxedit.asm`'s Rotate walk and `pxgif.asm`'s two animation rows are the
  ones another session is converting. They are registered so that whichever
  lands second lowers the lines.
- **It found a defect nobody had listed: PiXEL's histogram TAIL** is at the
  master + ceil(pixels / 16) paragraphs (`px_htail`, `pl_htail`, `pxfull`'s
  copy, and `pe_tailof`, which the scan cannot see). That is off the grid
  for any picture whose pixel count is not a multiple of 128. It is
  registered and NOT fixed: every reader and the claim's sizing have to
  move at once, and it is PiXEL's.

**7. `tests/megatrap0.py`** (soak, `needs=("bochs", "nasm")`, 199 s) runs
`MEGATRAP=1` in a private tree. Ten OS88_HIGH packages are started through
MEGARUN and left running, and any trap charged to an image outside ALLOW
fails the row, as do a refusal and a FAULT. The `bochs` capability is new,
probed by `os88build.have_bochs()` and named by `os88soak check`. ALLOW is
one entry: the boot's `mega_strcal` calibration, which faults on purpose.
It is red with MEGACSW added, which takes 5 traps.

**For the owner to look back over:**

- **The census found the three Sticky Apps fault inside `mega_tlog` itself.**
  DESKCAL, DESKCLOK and STIKNOTE each take one `xfer` at +108Fh when
  started through MEGARUN, and the logger then faults at FFD8:09AD on that
  frame. That is the instrument's defect, and it is knob-only. They are
  left out of the row, which names them.
- **An eleventh window was refused (05)** under MEGARUN on Bochs's 16MB,
  which is why the set is ten.
- **`os88bochs.py` allows one Bochs per box.** Parallel agents here ran it
  through a wrapper on displays of their own. The tool itself was left as
  it is.

### 14.40 PiXEL on kern_mega: no fixup, no off-grid segment (2026-10-20)

Items 47, 50, 59, 61 and 63's PiXEL rows, and 14.12's "PiXEL stays as it
is". **Every sample picture opens with NO #GP fixup at all**, where opening
one took up to 55,481 and four things FAULTED outright: an animated GIF's
first frame, a progressive JPEG, the editor's Rotate and Blur, and Full
Screen. kern_big and kern_small run the same binary and decode the same
pictures; only the PiXEL package and its parts changed, no kernel byte.

**The census** (`make megatrap`, Bochs at 16MB on VGA, B: holding PIXEL.O88
and `apps/pixel/samples`; trap lines per step, `tools/megatraps.py`):

| step | before | after |
|---|---|---|
| BALLOONS.PNG (palette PNG) | 55,481 | 0 |
| BOUNCE.GIF (animated) | **FAULT** on frame 0 (`pa_comp`, ES = 1992h) | 0, plays |
| CAT.GIF (interlaced, 256) | 39,160 | 0 |
| CITY.PCX | 42,632 | 0 |
| FLOWER.JPG (colour) | 46,161 | 0 |
| HOUSE.PNG (RGB) | 11,340 | 0 |
| LAKE.JPG (grey) | 533 | 0 |
| MOUNTAIN.BMP | 414 | 0 |
| ROOM.JPG (progressive) | **FAULT** (`pj_stzero`, ES = 5892h) | 0 |
| Rotate CW / CCW (Rotate tool) | **FAULT** (`pe_rowp`, DX = 2F8Ch) | 0 / 0 |
| Effects > Blur | **FAULT** (`pe_rowp`, 3334h) | 0 |
| Effects > Sepia (the plan search on the worker) | - (Blur faulted first) | 0 |
| Full Screen (F) and Esc | **FAULT** (`pxf` row, 285Ch) | 0 |
| File > Save As, PNG, onto B: | - | 0; the file read back on the host is the PCX's pixels exactly |

(Blur and Sepia each show one line, `FFD8:227C`, in the kernel's own
`.mega` - not PiXEL's, and the same before.) The four biggest counts were
one routine: **`pl_plan1`**, the palette's mixing-plan search that every
decoder part carries (`pxplan.inc`), kept its 136-byte scratch in the part
and wrote it through CS ~40,000 times a palette. `tests/pxmega.py` is the
census kept runnable (below).

**What was built.**

1. **The part ABI's two new words** (`apps/pixel/pxpart.inc`, `PXP_ABIVER`
   4 -> 5, SPEC.md 106.5): `PXP_SELF` (+16), the part's own DATA segment,
   and `PXP_PKGD` (+18), the package's, stamped with `PXP_PKG` before every
   call by `px_pstamp`. After the vectors, so no offset before them moved.
   Rule 6: a part stores into its own words through `[cs:PXP_SELF]`, never
   through CS. On a real-mode kernel the three words are CS's and DS's own
   values. It is inside one package, so it is not an SDK change and no
   `PKG_FMT` moves.
2. **Far calls through code selectors** (items 47, 50). `px_pcall` and
   `px_wcall` far-call a part through `op_cseg` (`OP_WANT_CSEG`, the
   parts standard's own). A decoder's services are far-called through
   `[cs:PXP_PKG]`, the package's CS, where they took DS, which was one
   `xfer` fixup a row. The services (`px_ks_*`) load DS from `[cs:px_dsel]`,
   which `px_pstamp` keeps, where they did `push cs / pop ds`, which was one
   `write` fixup a row. A part that repoints DS at the package takes
   `PXP_PKGD`. (Item 47's `px_reloc` is PIXELSTEIN's, not PiXEL's. PiXEL has
   none, and PIXELSTEIN is not touched here.)
3. **The stores through CS** (items 61, 63): about 470 sites in eight parts.
   - The hot ones are a segment register loaded once. `pl_plan1` writes its
     scratch through ES = `PXP_SELF`, which costs nothing on an 8088 because
     `es:` is the same prefix as `cs:`. In inflate, `pz_bias` and `pz_final`
     go into thirteen unused bytes of the PNG scratch, which is DS there,
     and lose a prefix. The scanline extraction runs with DS = the part, so
     its ~25 accesses lose theirs too. JPEG's self-modifying `imul`
     displacements (3 x 14,400 on FLOWER.JPG) are still self-modifying,
     written through `PXP_SELF` in one push/pop. The colour row's four
     patched displacements are now written only when they change, so that
     path is cheaper than before.
   - The cold ones, once per operation, header or row, go through `PXSW`,
     a one-store bracket (`push es / mov es, [cs:PXP_SELF] / store / pop
     es`), or `PXSWON`/`PXSWOFF` around a run of them. A handful that store
     SP use `PXSSP`.
   - Every `push cs / pop ds|es` in a part is `mov ds|es, [cs:PXP_SELF]`.
4. **Off the grid** (items 59, 63). Each fix names the same linear address
   as before, so real mode reads the same bytes.
   - The GIF animation's two master-row addresses (`pa_rows`, `pa_comp`)
     and the full screen's and Save As's master rows (`pxf` row, `pw_row`)
     use px_mrow's `and r, 127` / `and al, 0F8h`.
   - So does the editor's `pe_rowp`, which every Image and Effects
     operation reaches through `pe_srow`/`pe_drow`. That is why Blur faulted
     as well as Rotate.
   - Rotate itself (`pe_rot90`) is now an OFFSET walk. It adds the width to
     SI and moves the segment by 1000h only when the offset carries. The
     clockwise turn reads the column top-down and writes the row right to
     left with DF set, so it never steps a segment below the master's own.
     That matters because a segment below a claim can be `LOW_SEG` or
     `COLD_SEG`, whose descriptors are not identity. The loop is ~half the
     old one's cycles a pixel.
   - A flat file's head (`pxhmore.inc`) moves to 128-byte places.
   - **The histogram tail** (14.39's find): it was at the master + ceil(px /
     16) paragraphs, which is off the grid unless a picture's pixel count
     is a multiple of 128. It is now rounded up to 8 paragraphs in every
     reader together: `px_htail`, `pl_htail`, `pe_tailof`, the full
     screen's copy, and the XMS undo's byte count (`pv_xbytes`, which
     would otherwise have copied the tail short). The claim is
     `ceil(px / 1024)` KB + 2, which already holds the rounding.
5. **The progressive JPEG store.**
   - Each component's store starts on the grid. `pj_stsize` rounds
     paragraphs to 8, and `pj_fsizes` rounds the base.
   - The 1/8 store addresses its 8-paragraph group plus an offset.
   - The 1/4 store keeps a block a PARAGRAPH and decodes it at ES:0. That
     is a segment only every eighth block, and the AC refinement has no
     register to spare for an offset. So on kern_mega only, each block goes
     through an 8-paragraph bounce: copied in at `pj_pblock`, copied back
     at its `.out`, and read through it in `pj_oblk`.
   - The bounce is reached by three 2-byte jumps that `pj_decode` patches
     in when CS is not its own data segment. **Real mode is never patched
     and runs the bytes it ran.** HEAD sizes the scratch 8 + up to 28
     paragraphs larger, for a progressive picture only.

**Bytes.**

- The resident image is +85 (39,538 -> 39,623) and bss +2 (`px_dsel`).
- The parts' images are +3,000 together, and a part is held only while it
  is fetched: PNG +615, EXTRAS +946, JPEG +487, EDIT +368, WRITE +341, FULL
  +100, SIMPLE +97, GIF +42, HELP +4. Nearly all of it is `PXSW`'s 7 bytes
  a cold site. `PXSWON` took ~700 of them back.
- PIXEL.O88 is 111,001 -> 112,537 bytes on the disk (+1,536 packed). Every
  shipped disk still builds.

**Speed on the XT** (MartyPC, kern_big, the 5150 with a CGA, guest cycles).
`tests/pxbench.py` puts PIXEL.O88 FIRST on its disks, so a package that grows
moves every picture's clusters, and the open figures move with it. The A/B
below is a copy of the bench with the package placed LAST, so both trees read
identical picture layouts:

| open | before | after |
|---|---|---|
| MOUNTAIN.BMP | 16.70 s | 16.83 s |
| CITY.PCX | 12.41 | 12.41 |
| BIG.BMP | 34.95 | 35.12 |
| CAT.GIF | 25.38 | 25.41 |
| BALLOONS.PNG | 14.38 | 14.59 |
| HOUSE.PNG | 39.11 | 39.81 |
| VACATION.JPG 1/8, 1/2, 1/1, fast | 24.02, 43.44, 114.03, 46.80 | 23.97, 43.39, 113.95, 46.77 |
| ROOM.JPG progressive | 64.62 | 64.40 |
| zoom, pan, Fit | 0.88, 0.29, 0.47 | 0.88, 0.29, 0.48 |

The opens agree to between -0.3% and +1.8%. The bench's own breakdowns are
noisier than that: the same BASE code read inflate at 269.7 and at 293.3
cycles a byte on two disk layouts, because each is "cycles inside a routine",
which includes whatever the UI task did while the worker was inside it. So
the cycles are argued, too. In real mode the decode paths gained:

- ~45 cycles a palette entry in the plan search (256 a palette);
- ~50 a colour JPEG block where the component changes (three an MCU);
- one `PXSW` (~45) a row in each of a few row routines;

and lost a `cs:` prefix on every inflate-budget and extraction access, plus
the colour row's four stores. On VACATION.JPG at 1/1 the gain is ~180,000
cycles, 38 ms of 114 s, which is below what either run can resolve. The
progressive 1/4 path is unchanged in real mode by construction (the patch).

**Correctness on kern_big.** tests/pxedit.py --host-only (the EDIT part
under Unicorn, every operation including both quarter turns, 546 pixel
cases), pxextraemu (179 fixtures), pxanimemu (31 animated GIFs, every frame)
and pxsave.py --host-only (336 written files) are all pixelsim's, byte for
byte. `tests/pxpartemu.py` stamps the two new words and places the tail as
the parts now do. On MartyPC, `pxdecode` (every fixture against pixelsim,
byte for byte), `pxedit`, `pxparts` and `pxopenpng` were run on the final
tree, all green: pxdecode 988.9s, pxedit 346.6s, pxparts 16.3s, pxopenpng 53.8s.

**Item 4: OS88_HIGHMEM is NOT declared, and here is exactly why.** PiXEL
addresses every claim it holds by paragraph arithmetic on the claim's
segment:

- the master by `MSEG + (offset >> 7 << 3)`, at nine sites;
- the work claim cut into sub-buffers by paragraph offsets (`PX_WGRID`,
  `px_wo_*`), handed to the parts as `PXK_RSEG`/`PXK_DSEG`;
- the PNG scanlines at `PXK_DSEG + PZ_WPARA`;
- the JPEG progressive store at the scratch + `JV_SBASE`;
- the histogram tail past the master;
- the animation's backup inside its claim.

All of that is right on the identity map and meaningless on an LDT selector
over exactly the KB asked. A claim of 64KB or less (the master of any
picture up to ~250x250, the scratch, the view) would come back as one, which
is the declaration's whole effect. Declaring it needs 14.12's "flat arm" at
every site: `seg = claim`, `offset = the absolute address` when `test dl, 4`.
That is a rewrite of PiXEL's addressing, not a declaration. PiXEL stays
conventional for its claims. (`OS88_HIGH`, the region, it already says.)

**Ratchets.** `tests/unit/t_cswrite.py` now also walks `apps/pixel/`. The
package and its parts have NO write through CS left, and the two pairs that
remain are reads only, registered in `tests/cswrites.txt`. 14.39's
`tests/unit/t_megagrid.py` now recognises a `round` site whose paragraphs an
`and al, 0F8h` puts back on the grid before the load. Its five PiXEL lines in
`tests/megagrid.txt` (pxedit, pxgif, the three tail readers) are deleted,
and the file registers nothing.

**`tests/pxmega.py` (soak row `pxmega`)** is the census kept runnable. It
boots `make megatrap`'s tree under Bochs, opens the nine pictures by
double-click and Space, and turns ROOM.JPG both ways with the Rotate tool.
Every step must add no trap line, no FAULT, and a canvas that is a picture
and not the last one. It is red with `pa_comp`'s normalisation put back to
one paragraph (BOUNCE.GIF FAULTs). ~4 minutes, `needs=("bochs", "nasm")`.

**Left.**

- The palette operations' tables (`pe_lut`, `pe_bins`) and every cold
  per-operation word go through `PXSW`: ~45 cycles a store, nothing per
  pixel, and a few hundred stores per Auto Levels or Sepia.
- The Victory has not run any of it. Bochs is a 386 (docs/BOCHS.md blind
  spot 2), and nothing here is on the 286 reset path.
- TIFF, ICO, IFF and MacPaint (the EXTRAS part) were converted the same way
  and are covered by pxextraemu under Unicorn. No such picture was opened
  under Bochs.

### 14.41 The Video Player's ring above 1MB: `OS88_HIGHMEM` (2026-10-21)

The player said `OS88_HIGH` alone (14.14): its ring was paragraph
arithmetic over 34 sites. It says `OS88_HIGH | OS88_HIGHMEM` now, and under
`CPU_F_PM` a stream's ring is a pool claim. SPEC.md 98.3.18.17 is the
contract; this is the record.

**The claim needed a window every 32KB, so the kernel grew one.** 14.37's
`OSAPI_MEM_HUGE` run has a window every 64KB. A super-packet of up to 32KB
can start anywhere in a 32KB slot and run on into the next slot, so a slot
at an odd 32KB of the claim has no window it fits in. `OSAPI_XMEM_RUN` has the
right shape but only on an `OSAPI_XMEM_ALLOC` block, and those are pinned.
So `OSAPI_MEM_HUGE` takes **BX = 8000h** on kern_mega: selector k is the 64KB
window from 32KB x k, and the cell answers BX = 0. Every other kernel, and a
conventional claim, leaves BX as it was, so a caller can tell. The entry is
`MX_FHUGE | MX_FH32`. `mx_unsel` frees (KB + 31) / 32 selectors, and the
regrow refuses such a claim. It still moves, but its odd windows cross a
64KB piece, so `mx_cpmove` moves it in one interrupts-off step by its own
rule. That is about 10 ms a 64KB on a 12 MHz 286, so up to ~80 ms for the
player's largest ring of 512KB, and only when another claim compacts the
pool during a play.

**In the player**, `[vp_rhi]` marks the session:

- **Slot k** is `[vp_ring] + 8k` wherever it was `+ 2,048k` paragraphs: the
  fill, the mirror copy, the seam (`vp_warm`) and the copying bank's bounce
  (`vp_bbnc`).
- **A key's read** sits at the ring's start, 64KB at most, and is read as
  window 0 with the offset as it landed. `vp_rptr` and the two inline
  copies of its normalisation test the segment for an LDT selector.
- **A lead read apart** (`vp_spos`) and **the bench** (`vp_bnsel`) each
  take whole windows past the key's read. The bench gets no room in the
  in-place ring of three windows when `[vp_kbkb]` is over 32, and then
  times the key, its documented fallback.
- **`vp_addr`**, the one site a record reaches (several a frame), is patched
  at its head to a `jmp vp_paddr` for the session (`vp_rhpat`). The write
  goes through our data alias, as `vd_ypatch` writes, and `vp_sfree` puts
  the three bytes back. A real-mode play runs vp_addr's own instructions,
  byte for byte.
- **Sizing** takes K from the pool's free KB (`vp_rhkb`) rather than from
  conventional memory less what is claimed after it, and `vp_kroom` lets
  the keeper stand.
- **One 32KB conventional bounce** (`[vp_dbnc]`) takes every disk read the
  ring gets: the fill, `vp_xbread`/`vp_xbup`, `vp_bbnc` and `vp_xfillt`.
  So a fill makes the `int 13h` runs it always made, and the bounce is
  copied up with a `rep movsw`. Read straight into a selector, the kernel
  would bounce it nine sectors a call (`dsk_xfer`'s `.bounce`). A copy out
  of the hold goes straight to the ring: `mx_copy` takes a claim of ours
  above 1MB as the conventional end.
- **`vp_rhiq` decides** before the keeper and the sizing. The ring stays
  conventional for BIGSP, a layer to place, EMS, a resident play, or a pool
  without room for the header's slots and the mirror. It also stays
  conventional when the claim or the bounce is refused, and then
  `vp_rhfit` cuts the K the pool sized down to what conventional memory
  holds, as the real-mode sizing would have. That arm was run once under
  Bochs with `OSAPI_MEM_HUGE` replaced by `stc` in a throwaway build: the
  Repeat play took a 512KB conventional ring (all 16 slots fit there) and
  played.

**What stays conventional, and why.** `vp_mclaim` and `vp_mclhi` now go
through `OSAPI_MEM_CLAIM_DMA(_HI)` with CX = 0. That is the plain claim
(`mmf_osapi_mem_claim` is that call with CX = 0, so kern_big and kern_small
behave identically), and `OS88_HIGHMEM` does not reach it. Every claim but
the ring's therefore stays below 1MB, which is what declaring HIGHMEM needs:
each one has paragraph arithmetic somewhere.

- **The keeper and the shadow**: plane k is the segment plus `[vp_plsp]`,
  in `vp_decram` and `vp_zero`. That is the decoder's own loop, so it was
  not converted.
- **The poster, `[vp_kshd]`**: their planes, and the poster can BE the
  keeper (98.3.19.3).
- **A resident block**: `vp_raud`'s granules and `vp_ldblk`'s read at the
  claim's top.
- **`[vp_rdseg]`'s own claims**: `vp_kpic`'s and the thumbnail's
  normalisation.
- **The hold's fill buffer**: `vp_xgal` aims it at the gap.
- **The layer's own claim**: slot arithmetic.
- **The Sound Blaster's ring**: the DMA door already, and the chip reads it.

That leaves 32KB of bounce, the 17KB card ring and a ~10KB claim during a
play, where the ring used to be up to 514KB.

**Measured, kern_mega under Bochs** (16MB, SB16), on a clip made as the brief
says: `testsrc2` 320x240 at 15 fps, 20 s, `--preset vga --profile floppy`,
429,568 bytes, silent. A second clip adds a 440 Hz sine as PCM8 with sound
ahead (444,416 bytes). Both are on a 1.44MB B: with VIDEO.O88. `mx_tab` and
`mem_tab` were read off the machine with the Bochs debugger:

| play | ring (pool block) | conventional free | card |
|---|---|---|---|
| held, in place, silent | 96KB, owner A1h, flags 3, sel 28C4 | 523KB | `XMS in place (PM)`, 300/300, stalls 0, Late 0, 365 ticks of 364 |
| the same, base player | 96KB conventional | 459KB | the same line |
| Repeat (copying), silent | 512KB at pool KB 480, owner A1h | 523KB | stalls 0, Late 0, through two seams and a full-screen lap |
| the same, base player | 514KB conventional | **41KB** | - |
| in place, PCM8 on the SB16 | 96KB | - | 300/300, pauses 0, Late 147, 45 ticks of 363 (base: Late 149, the same 45 of 363) |
| Repeat, PCM8 | 512KB | 506KB | 266/300, pauses 176, Late 1932 (base 265/300, 175, 1932) |

The two sound rows are Bochs's SB16 and not the change: base and new read
the same to a frame. Closing the player gives everything back: the region,
the hold, the ring and the bounce.

**The first build faulted at Play**: `kern_mega FAULT 0D err 2A84` at
`vp_bngen`'s `mov es, [vp_bnseg]`. The bench is placed on every play, not
only a layered one, and its segment was the ring plus paragraphs. That is
the `.bh` arm now.

**Measured, 86Box `286-mega`** (the mr286: MR BIOS 286, 16 MHz, 4MB, SB16,
kern_mega booted from A: with a SYSTEM.CFG wanting sound). The PCM8 clip
was opened and Play pressed at once, while the hold was still filling off
the floppy, so the session copied (`Held whole in XMS`). The pool read:
hold 435KB at KB 60, **ring 512KB at KB 495, owner A1h, flags 3, sel 28B4**.

- Card: **Drew 300/300, stalls 0, pauses 0, Late 0, 364 ticks of 363**, so
  real time on the 286 reset path, the copy into a ring above 1MB included.
- Repeat with sound, mid-play: conventional free 507KB. 46.8 s into the
  session it was 6.8 s into the third lap with Late 0 and pauses 0.
- The base player, the same flow: ring 512KB **conventional**, 27KB free.
- `faults` shows only `mega_svc_lo.r286`'s resets and four handled
  `Segment can't read/write` at 280C:178x with DS 2814. That is another
  image's (the player's claims are owned by 2834, its DS), and they were
  not taken apart here.
- Not run: the `286-video` profile itself. Its C: wants `make videnchd
  VIDENC=...`, and its machine is the same mr286.

**kern_big, MartyPC.** The XT path:

- **The bytes a frame runs are identical.** `tools/os88rdiff.py --hot-file
  apps/video/rdiffhot.txt` against HEAD finds 0 hot routines at an odd
  shift. The only hot routines changed are three per-chunk or per-read
  ones: `vp_fill` (three not-taken `cmp byte [vp_rhi]` a 32KB chunk, ~90
  cycles against a read of about two seconds), `vp_xstep` (its claim
  through `vp_mclaim`) and `vp_rdat` (a `test cx, 4` a call past the
  first).
- **`vd_n_run`'s gate is `jnc short` now** (`VD_SGATE`). Its comment said
  it reaches `vd_n_slicel` at exactly 127 bytes, and nasm silently relaxes
  one byte more. Here it relaxed with the bytes BETWEEN unchanged, from an
  earlier pass. Forcing `short` keeps the encoding, and makes a layout that
  truly does not fit an error instead of a silent 3-byte relaxation in the
  decoder.
- **Play A/B**, the commit's own sources put in place against this tree,
  `tests/vidplay.py`'s second play (ticks, the guest's clock):

  | arm | base | new |
  |---|---|---|
  | cga | 92, 4.95 s | 92, 5.15 s |
  | lin80 | 91, 4.93 s | 92, 4.87 s |
  | cga on herc (the shadow) | 93, 5.43 s | 93, 4.85 s |

  Frames 150/150, stalls 0, late 0 in every arm. The clock moves both ways
  between runs of the same binary, so it measures the run and not the
  change.
- **Rows**, `os88test.py soak -k ...` on this tree, all ok: `vidplay
  vidplayvga vidplayshd vidrepeat vidahead vidaheadseek vidkpart vidbank
  vidxipbank vidxip vidxmsgap vidlyplay vidresident vidlive vidbigclus
  vidkeyclus vidsnd vidclose` - **18/18**. `vidbench` skipped (needs
  `xdc`); `vidbigclus` and `vidkeyclus` needed `mtools`, which was
  installed for them.

**Cost.**

- `video.bin` 58,357 -> **58,973** (+616). `VPDIAG=1` 59,923 -> 60,537
  (+614), `NOLIVESND=1` 58,124 -> 58,740 (+616). bss unchanged at 2,516.
  The shipped `VIDEO.O88` is 44,543 -> 45,043 bytes on disk. About 360 of
  the bytes are the new routines (`vp_rhiq` to `vp_rhcp`), the rest the
  sites' arms and the even-shift pads. The MartyPC rows, the A/B and the
  Bochs and 86Box plays ran on the build before `vp_rhfit` and two
  even-shift `nop`s (+~70 bytes; on a real-mode kernel it is one `cmp` and
  a `je` a session start); the fallback run above had it, and `vidplay vidrepeat vidbank vidkpart
  vidxipbank vidahead` were run again on the final build: 6/6 ok.
- kern_mega `.cold` 41,522 -> **41,606 (+84)**, `.mega` 12,941 -> **12,955
  (+14)**, `.text` + `.bss` +0, the segment overhead still the 5,931
  `tests/megaseg.txt` allows.
- kern_big and kern_small: **unchanged**. Every kernel line is in
  `kernel/mega.inc`, which is `%ifdef KERN_MEGA` from its 63rd line to its
  end (read from the diff, not built twice). The SDK comment and the
  contract in SPEC.md 50.3.5 grew. `abilock` records it without a bump
  (PKG_FMT 10, DRV_VER 11): BX was an unused input, so nothing built before
  changes.

**Rows that cover it**: the MartyPC list above (kern_big), plus the fast
tier's `abi` and `movable`, and `make mega`'s `os88megaseg` and
`os88ovlchk`. No soak row boots kern_mega. The two Bochs and 86Box plays
above were by hand, and their recipe is this section's.

**Left, and for the owner to look back over:**

1. **The ring's move is one interrupts-off step**, ~80 ms at 512KB on a 12
   MHz 286, where 14.37's pieces cap a deaf stretch at 64KB. It only happens
   when something else compacts the pool mid-play. Making it piecewise
   needs `mx_cpmove` to rebase a window that spans two pieces after both
   are copied, and to know the copy never lands in a piece a window still
   reads. Not built.
2. **The bounce is the one conventional buffer the ring keeps**, 32KB. A
   disk read straight into the high ring would cost the `int 13h` calls and
   on a floppy the revolutions (14.16 decision 3). The 286's PIO hard disk
   (HDD.DRV rung 1) could take it at no cost, and is the next step if the
   32KB matters.
3. **Everything else stays conventional**: about 60KB during a play
   (keeper/shadow up to 64KB, the poster, the key reads). The planar keeper
   is the decoder's inner loop, which the XT path must not pay for.
4. **A layered file, BIGSP and EMS keep the conventional ring.** A layer's
   carve and bounce sit at KB offsets, and BIGSP's 64KB super-packets need
   a window finer than 32KB apart.
5. **The `jnc short` in `VD_SGATE`** turns a future odd shift that pushes
   `vd_n_run`'s gate past 127 bytes into a build error. It is
   `VP_RDLOG` without `VP_DIAG` today, an arm the Makefile never builds.

### 14.42 A fault in a package closes the package, not the machine (2026-10-20)

Section 4 item 6, built. Before this, every exception that `mega_gpfix`,
`mega_gpw`, `mega_null0` and `mega_db` did not finish went to
`mega_report`: the E9 dump, the `kern_mega FAULT vv err eeee at cccc:iiii`
line under the menu bar, and `hlt`. Now `mega_report`'s first act is
`mega_fkill`. When the fault is a package's, that routine closes the package
and never returns. Otherwise it returns, and today's screen follows
unchanged. The deliberate traps are untouched, because they never reach
`mega_report`.

**Sizing, first, as the brief asked.** It is NOT a whole-kernel refactor.
Three facts already in the tree carry it:

1. Every far call into package code goes through two sites. One is
   `wm_pkgcall`. Every window callback uses it, and so does `fsx_run`'s
   proc. The other is the loader's entry call. SPEC.md 66.6.1 audited this
   as "exactly two `call far` sites" (plus the compactor's relocation proc,
   which stays fatal here).
2. Teardown routines exist for both kinds of instance. `inst_task_die`
   tears down an instance with a worker, and is entered from an arbitrary
   worker SP already (`inst_pkg_alive`'s `.die`). `app_close_win`'s
   task-less branch tears down an instance without one.
3. `ui_task` has a step that runs with nothing held: `mega_tell`, which
   already raises toasts for interrupt-time events.

The open questions were where to unwind to and what an unwind leaves
behind. Both are answered by a CATCH pushed around those two calls.

**What was built:**

- **A per-task catch** (`mega_pcin_x`/`_n` and `mega_pcout_x`/`_n`,
  `.cold`). On kern_mega, `wm_pkgcall` far-calls `mega_pcin_x` where it
  called `wm_nest_pushd_x`, which costs the same 5 bytes of `.text`. It
  calls `mega_pcout_x` in place of `wm_nest_pop_x`. The loader's entry call
  brackets itself with the `_n` pair, which needs no DS at the return.

  Under the far return, the catch leaves five words: the task's previous
  catch, SI, the gfx lock's flag/owner word at the call, the image's
  segment, and the task index. `mega_pcj` (`.lowbss`, one word per task)
  holds each task's innermost catch. It is per TASK because a worker
  reaches `wm_pkgcall` too, through `inst_task_die`'s repaint. The far
  return MEGA_PCALL pushes sits just under the catch, which is how
  `mega_fkill` finds the way home. It checks that return against the two
  known labels (`..@mega_pcret_w` in `wm.inc` and `..@mega_pcret_l` in
  `loader.inc`) before trusting it.

- **Attribution** (`mega_fkill`, `.mega`). A fault is a package's only when
  all of these hold:
  * The vector is one a program's own instruction raises: 00, 03, 04, 05,
    06, 07, 0B, 0C, 0D, or FF (`mega_unh`). 08, 09 and 0A stay fatal.
  * Neither `[sch_rbusy]` nor `[sch_lock]` is set. The FSXF_RATE hook runs
    package code inside IRQ0, where no unwind is safe.
  * The faulting CS is an LDT code selector, and `mega_cs2ds` turns it into
    S.
  * S belongs to a package instance. `mega_fown` asks the published records
    first, then the region being loaded (`[ld_base]`/`[ld_rec]`), then the
    claim containing S, whose `MC_OWN` names the instance (an overlay, a
    part).

  The published records are asked first because `[ld_base]` outlives its
  launch, so a region moved onto that segment belongs to its own record.

  Then one of two things happens.
  * **The running task has a catch** whose image is the same instance, and
    the fault is above the catch on the stack. The fault is UNWOUND: SS:SP
    goes to the catch, the far return is taken with CF = 1, SI is put back
    and the other registers are zeroed. The callback reads as having
    refused, and an entry proc as having refused to start.
  * **There is no catch, and the running task is the instance's own
    worker** on LOW_SEG's stack. The worker is killed: SP goes back to where
    it was at the fault, and the routine jumps to `inst_task_die`.

  Anything else stays FATAL: the UI task outside a callback, another
  package's code, or a catch that was overwritten.

  **A kernel routine that faults on a pointer a package handed it stays
  fatal.** The kernel frame that faulted may hold a lock or be half way
  through a table, and nothing records which. That case is the obvious next
  step, and it would need the same catch plus a per-routine statement of
  what is safe to drop.

- **What an unwind leaves consistent, and why.** Between the catch and the
  fault there is only PACKAGE code. Every kernel call the package made has
  returned, and every kernel bracket (`wm_nest`, `dsk_batch`, the clip) has
  closed. The kernel frames under the catch are intact, and they carry on.
  That is why the innermost catch is the right one. If kernel code calls
  package B from inside package A's callback and B faults, B is unwound
  back into the kernel and A carries on.

  One thing does not follow from the call structure, and that is the gfx
  lock. A callback that ran without the lock and took it itself (a wake
  handler calling OSAPI_GFX_LOCK) would leave it held for ever. The frame's
  lock word says whether this task held the lock AT THE CALL. If it did
  not and holds it now, `gfx_unlock` runs before the return. A lock the
  caller held stays held, for the caller to release. A killed worker gives
  back a lock it holds, because `inst_task_die` takes the lock itself.

- **The close** (`mega_fkdo` from `mega_tell`, ui_task step 0, with nothing
  held). The unwind marks the record dying (I_STATE 2), so its own API
  calls refuse and its worker leaves at its next OSAPI_TASK_ALIVE. It
  queues the instance in `mega_fkq`, and step 0 then closes it
  (`mega_fkclose`).

  A task-less instance gets `inst_task_die`'s sequence: `toast_owner_gone`,
  `inst_rel_rec` (sound grants, so the tone stops, and extended memory),
  `inst_unwin` (its windows and their timers, and the repaint),
  `desk_drop_x`, `mem_free_rec` (every claim and the pool pair), and the
  record freed. It does NOT go through `app_close_win`, whose first act is
  `wm_ask_close`. That would run the package's own code, which is the code
  that faulted.

  An instance with a worker gets the die flag and the hide, which is what
  `app_close_win` gives it. A fault in the loader's entry call is not
  queued, because the loader's own abort path frees the region and its
  claims.

  `.mega` reaches the near `.text` routines through `mega_tcall` (`call bp
  / retf`, the only `.text` this feature adds).

- **The report.** The toast is `NAME vv at cccc:iiii`, for example
  `FLTA 06 at 27D4:0076`, shown for ten seconds. The name is the region
  header's, cut to 8 characters, because TOAST_MAX is 24 and the line has
  to carry the vector and CS:IP to keep field reports useful. The E9 line is
  `kern_mega CLOSED NAME vv at cccc:iiii err eeee`. It is deliberately NOT
  `kern_mega FAULT`, which `tools/os88bochs.py`'s sweep treats as the end of
  a boot.

- **No loops.** One incident is one instance being closed. While it is
  open, a fault in the same instance (the same slot and region) is a
  cascade, allowed up to MEGA_FKCAS = 8 times. Cascades happen when a
  surviving window of the dying region repaints, or when an fsx proc's
  bracket returns into the callback that ran it. A fault in ANY other
  package while one is open is fatal, and so is a new incident within
  MEGA_FKGAP = 55 ticks (3 s) of the last one's start. The incident ends
  when step 0 sees its record gone.

**Cost, measured by building both arms at one commit:**

| build | `.text` | `.bss` | `.cold` | `.mega` | `.lowbss` |
|---|---|---|---|---|---|
| kern_mega | +4 | 0 | +163 | +996 | +84 |
| kern_big | byte-identical | | | | |

- kern_mega's `.text` +4 is `mega_tcall`, 3 bytes plus its pad.
  `tests/megaseg.txt` goes 5,931 to 5,935, with the reason.
- kern_mega's `.cold` +163 is the two catch pairs and the loader's two
  calls.
- kern_mega's `.mega` +996 is `mega_fkill`, `mega_fown`, the message, the
  close, and the two hooks.
- kern_mega's `.lowbss` +84 is `mega_pcj` (14 words), `mega_fkq` (12), the
  25-byte toast and the incident's 7 bytes.
- kern_big was compared by md5 of `kernel.bin`: every change is inside
  `%ifdef KERN_MEGA`. kern_small follows from the same diff.
- The hot path is one far call that grew a body. `mega_pcin_x` is about 30
  instructions and `mega_pcout_x` about 25, on a 286, per package callback.
  That is noise beside a callback's own cost, but it is not zero.

**Measured:**

- **Bochs**, with `tests/megaflt.py` (new soak row `megaflt`, 102 s, red
  with a `ret` at the top of `mega_fkill`). Seven builds of
  `tests/megaflt/megaflt.asm` were started by MEGARUN:
  * FLTA: #UD in a timer callback.
  * FLTB: an OS88_HIGH worker holding OSAPI_GFX_LOCK reads past its
    segment.
  * FLTC: #DE inside OSAPI_FSX_RUN after OSAPI_FSX_MODE set 320x200.
  * FLTD: OS88_HIGH|OS88_HIGHMEM, #GP with a 32KB claim, a timer and a
    440 Hz tone held.
  * FLTE: #GP in the entry proc.
  * FLTF: #UD in a wake handler that took the lock.
  * FLTZ: the control.

  All six were closed for the vectors expected. FLTE was refused (04). Each
  entry read the same `OSAPI_MEM_AVAIL` and XMEM_CAPS as the one of its kind
  before it, so the region, the 32KB claim and the pool pair all came back:
  low builds 0238/0238 KB with 3AB4 pool, high builds 0239/3AB3. After the
  fsx case the desktop was repainted in its own mode. FLTZ then said it was
  alive and answered a key (`61`) and a click sent from the host.

- **86Box `286-mega`** (the mr286, a real 286 reset path), the same seven
  from a scratch B: disk. Six toasts in turn
  (`FLTA 06 at 27E4:0076` ... `FLTF 06 at 27E4:0096`). The fault log names
  exactly the three #GP Limit checks the probes make, and the
  `mega_svc_lo.r286` resets are the reflection's own. Then
  `FLTZ got a key` and `FLTZ got a click` appeared as toasts.

**Not done, and why:**

- **The speaker's silence after FLTD is not observed.** No instrument here
  hears it. It rests on `inst_rel_rec`, which is the path a normal close
  takes.
- **The loop guards (the 3-second gap and the cascade cap) have no row.**
  MEGARUN starts packages five seconds apart, so a second incident inside
  the gap needs a probe of its own.
- **Not attributed:**
  * a fault in a kernel routine on a package's bad pointer (above);
  * the compactor's relocation proc;
  * the FSXF_RATE hook;
  * a package that put the UI task on a stack of its own (the unwind is
    taken all the same) or a worker on one (fatal).
- **14.5's hibernate-mismatch decision** ("there is no forced close in the
  kernel", so the whole session is discarded) could now close only the
  damaged programs on kern_mega. It does not; that is a decision for the
  owner, and kern_big still has no such path.

**For the owner to look back over:**

- **The toast cuts the name to 8 characters** to keep `vv at cccc:iiii`.
  The E9 line carries the full line and the error code.
- **A second fault anywhere within 3 s of the last close is fatal**, even in
  a different package. That is the brief's "no loops" taken
  conservatively.
- **An unwound callback returns CF = 1.** An entry proc reads that as
  refused, so the launch aborts, which is the point. A window's W_ASKCLOSE
  reads it as "refuse the close", which is harmless because the forced
  close never asks.

### 14.43 The DOS box's durability option: save the session to disk before the park (2026-10-21)

**The owner's ask**: *"Dos option to flush the session as well as move out
of its way"* - KERN-MEGA-DOS-PLAN 12.1 answer 2, *"also save the session to
the hard disk before running DOS, shown only where a BIOS fixed disk exists.
It rides on W5."* The park lives in RAM, so a DOS program that hangs the
machine, a power cycle, or a reset whose ROM clears memory loses the whole
desktop session. SPEC.md 96.40.9.3 is the contract.

**Step 1, the sizing, and why it was SMALL.** Every piece existed:
`hbm_wrimg` is kern_big's arm-3 image writer and already runs in protected
mode (W5, 87.9: the conventional picture through the stretched LOW0 selector,
the extended section, the pointer); `hbm_ask` already resumes an `HBP_DOS`
image with no question; and **`hbm_wake` step 5 already deletes both files on
every wake** - the park's way home and W7's both enter it - so "invalidate the
image after a session that came back" needed no code at all. What the feature
needed was ORDER: the park's claim (`mega_dosclaimf`) takes the XMS arena,
everything the pool has left, and the image carries every pool block in use,
so the image has to be written before the claim or it holds a section the
size of the pool that no resume could place. Nothing in `kernel/mega.inc`,
`kernel/loader.inc` or `apps/pixel` was touched.

**Built**:

- `kernel/hiber.inc` (`HIBER.DRV`, kern_mega's): in `hbm_dosrun`'s park
  branch, when the launch block has `KDLF_SAVE` and `hb_pick` names a fixed
  volume, `hbm_wrimg` (AL = 1) does the sweep, the lock and the image, and
  then the claim, the sum and the snapshot follow as before. Two unwinds: a
  write that fails refuses with *Could not save to disk* (drivers, lock,
  `[sch_lock]` and the extent list given back - `hbm_hibfail`'s body less its
  mode byte, which would have pinned the module); a claim refused after the
  write deletes the image and refuses *No room above 1MB*. And
  `hbm_dosbda`, on kern_mega, stages W7's `MPK_RESETCODE` where the BDA holds
  no code - kern_dos goes home by the park and never writes the mailbox - so a
  disk resume says *"The machine was restarted while it ran."*
- `apps/dos/dos.asm` (kern_mega's `DOS.O88` only, `%ifdef DOS_MEGA`): the box
  *Save the session to disk* under *Parks the OS above 1MB*, **unticked by
  default**, greyed by `dos_msav_di` -> `dos_hasfixed` (hb_pick's own test)
  and refused a press while grey; `KDLF_SAVE` (bit 6) in the launch block;
  byte 12's `LNK_BX_SAVE` (bit 4) in a shortcut. kern_mega's page has no
  radio, so its HDD/Network/Limit rows move down 5 pixels to make the row and
  the block still ends at 115, inside CGA's 118 (SPEC.md 96.36.2).
- `kerndos/kdlaunch.inc` / `kernel/hiber.inc`: `KDLF_SAVE equ 0x40`, mirrored.

**Cost, measured at build 254 against `2be83d0c` built the same way**:

| | before | after | |
|---|---|---|---|
| kern_mega `.text`/`.bss`/`.cold`/`.mega` | | | **+0** (one `.mega` constant moves: the park stub's offset in HIBER.DRV's image, `mega_dstubup`) |
| `HIBER.DRV` image (`.modh`) / its bss (`.modhb`) | 9,217 / 602 | 9,307 / 602 | **+90** / +0 - an on-demand module, so none resident |
| `HIBER.DRV` file | 7,980 | 8,053 | +73 |
| kern_mega's `dosp.bin` | 51,088 | 51,214 | +126 |
| kern_mega's `DOS.O88` file | 64,777 | 64,777 | +0 (512-aligned parts) |
| kern_big / kern_small | | | **all 215 artefacts byte-identical** (every `.bin`, `.o88`, `.drv`, `.sys`, `.img` under `build/` outside `megak/`) |
| disk, while the program runs | | | **800 KB** of C: (640 KB and a 160 KB section on this desktop), deleted on the way home |

**Measured on 86Box `286-mega`** (MR BIOS 286, 16 MHz, 4MB) with a 20MB
type-2 IDE C: built by `os88disk.py --hdd` with `megak/boothd.bin`,
`kernel.sys` and the system floppy's root files, booted off C: and no A: -
so the volume is the kernel's own boot partition and every write a reflected
`int 13h` and a 286 reset, the Victory's BIOS-only C: case. KDHELLO
(`-DKDHOLD`) from the box with the option ticked:

| | result |
|---|---|
| Run to the program's last line | **2.69 s** ticked against **0.57 s** parked alone (emulated; 86Box is not cycle-accurate and its disk was `ramdisk` speed, so the iron figure is the Victory's to take) |
| power off during the program, power on | the session back from the disk in ~6 s from POST, *The machine was restarted while it ran.*, the files gone |
| 86Box hard reset (it clears RAM: the W7 anchor read 0 after it) | the same, from the disk |
| Ctrl-Alt-Del (RAM kept, anchor read `PKRS` + 140000h) | W7 recovers the PARK (the `DOSP` trip line on the bar), the files gone |
| 64 bytes of the park overwritten from the socket, then a key | the stub's *KDHELLO.COM wrote over the parked session*, a key, a fresh boot - **and the session back from the disk** |
| normal return | `exit code 042 (Arena: 622KB)`, the files gone, and a power cycle after it boots an ordinary desktop |
| unticked | no `HIBERNAT.*` on C:, the trip as before |
| floppy-only machine (`mega.img` in A:, no C:) | the box greyed, a press on it does nothing |

**On Bochs** (a 386, the CR0 route; `os88bochs.py boot --bootc --noa --csys`,
DOS.O88 and KDHELLO put on C:): ticked, the 800 KB image and its pointer on
C: while KDHELLO waited; Bochs stopped and booted with `--c keep`: E9 `kern_mega:
resumed`, the DOS window back with the restart line; a second trip from the
resumed session: `parked 0086K`, `unparked`, exit 042, the files gone.

**Rows that cover it**: none registered drives this box on kern_mega yet; the
nearest are the DOS-trip and hibernate rows (`soak -k 'kdos*' 'hibern*'`),
which run kern_big's `DOS.O88` and HIBER.DRV - byte-identical - and so cannot
see it. A row would be `tests/` + 86Box, which docs/86BOX.md says no row does
yet; the recipe above (C: off `megak/boothd.bin`, CMOS type 2, power cycle
with the run's `hdd1.img` kept) is what one would script.

**For the owner to look back over**:

- **The default is OFF** - the park alone is the fast path, and the owner's
  answer offered durability "if the time it costs is worth offering". A
  shortcut carries the tick.
- **A failed write REFUSES the launch** rather than parking without the image
  (kern_big's arm 3 carries on without one): the user asked for the guarantee,
  and the sentence says what to untick. The one predictable failure is a pool
  too small for a resume to stage the image (`hbm_xplan`, a 1MB AT), which
  the box does NOT grey - it cannot read `[xm_kb]` - so a 1MB AT with a C:
  sees the box live and is refused at Run. Greying it needs a slot or a
  reading of `OSAPI_XM_CAPS`'s total, not built.
- **The greyed box has no reason caption** - the page has no free row; the
  mouse box beside it is greyed the same way. *(And that mouse box is greyed
  on EVERY kern_mega launch: `dos_mck_di` ties it to `DOS_MEM_WHOLE`, which
  kern_mega's radio-less page never selects - so `Disable the mouse` cannot be
  ticked on kern_mega at all. *(2026-10-22: since fixed - fdac077c,
  measured in 14.46.2; `dos_mck_di`'s `%ifdef DOS_MEGA` arm, apps/dos/dos.asm:7146,
  leaves the box live.)* Found here, not fixed: it is the box's
  arm logic, not this option.)*
- **A park refused by its sum now has a second chance** when the option was
  on: the stub's restart boots into the disk image. The stub's sentence still
  says only that the session was overwritten.
- **The 86Box MR BIOS stops at *"Parallel Port Configuration Change"*** after
  Ctrl-Alt-Del and after the stub's 8042 reset (a key, then setup's F10): the
  emulated BIOS's, seen on every warm boot here, not this change's.

### 14.44 Re-home HIGH, part 1: four re-homed programs above 1MB, and `OSAPI_MEM_SUBSEG` (2026-10-21)

14.38's S1, S2, S3, S5 and S7 (Word and Scribe), built on `protected-mode`
at 78436d50 (14.37 merged in first). The owner's case now holds:
**Word open beside a playing video leaves conventional memory exactly as
it was before Word opened**, and a pool compaction moves Word while it
stays usable.

**Decisions the orchestrator took on the owner's behalf (14.38.6):**

1. D1. The opt-in is the existing `OS88_HIGH` bit, in the PROGRAM part's
   own header. No new bit, no `PKG_FMT` bump, and `OSAPI_PKG_REHOME`'s
   contract is unchanged.
2. D2. Pixelstein (S4, S6, and its S7) is a LATER part, after 14.37. Its
   141,312-byte keep still falls back to low, as the design says.
3. D3. `OSAPI_MEM_SUBSEG` (S5) is TAKEN, at 0x04BD, appended. So
   `abilock --update` ran with no bump, Skies takes the small arm of its
   `cs_xpand` fix, and the +96 scratch arm is not built.
4. D4. The brief conventional room at launch is DEFERRED and still open.
   The parts loader reads the carve low, and the kernel lifts it after.
5. D5/S7 is done for Word and Scribe (14.44.3). Pixelstein's comes with
   its part.

#### 14.44.1 What was built

- **S1, `kernel/loader.inc`.** `ld_hi`'s body is now `ld_hicx`, with the
  byte count in CX. `ld_hi` is `mov cx, [ld_need]` falling into it, so
  step 7's callers are unchanged. Step 8a gains an arm, 8b in the source,
  after `mov [ld_base], ax`. It fires for the KEEP form (`[ld_rehsz]` = 0)
  with a keep of at most 0FFFh paragraphs. It then sets ES to the program
  and calls `ld_hicx`, which tests protected mode and the program header's
  bit 7. Everything after it - `.call8`, `MEGA_PCALL`, step 9's `I_SPTR` -
  reads `[ld_base]`, which comes back as the pair's data selector.
- **S5, `OSAPI_MEM_SUBSEG` (0x04BD, an RCX cell).** It answers
  `AX = paragraphs into your own region`, which it names by DS, and
  `BL bit 0 = far-called`. The answer is DX = a data selector based at
  that paragraph, with a limit running to the block's end or 64KB; with
  bit 0, its code twin is at DX - 8. A descriptor already over that base,
  of that type (the accessed bit masked), is reused. CF = 1 with every
  register kept means "your segment + AX". That is the whole cell on
  kern_big and kern_small (`mod_gone`), and on kern_mega for real mode, a
  low region, an offset past the end, or no LDT entry.
  - The `.mega` body is `mega_subsegf`. It holds `[sch_lock]` from reading
    the block's base to aiming the descriptor. This is mx_alloc's own rule:
    a compaction in between would leave a descriptor at the old base.
  - **The teardown frees them.** `mega_pkghifree` now calls
    `mega_rgnsubfree` first. That frees every present LDT descriptor in the
    allocator's range whose base is inside the region's block, except the
    pair, which `mega_imghifree` frees. The rule is bounded by the block's
    KB, because a block of under 64KB has a neighbour whose pair is inside
    the next 64KB. `mega_rgnblk` (selector to `mx_tab` entry) serves both.
    It is restricted to PACKAGE regions on purpose: DESK.DRV's LIB selector
    lies inside a driver image and is freed by `mega_dklibfree`.
- **S2.** `OS88_HIGH` is set in WORD's, SCRIBE's, CLEAR SKIES' (not
  `CSDIAG=1`) and kern_mega's DOS.O88's program headers. DOS.O88 has it
  under `%ifdef DOS_MEGA` in the parted arm. kern_big's `dosp.bin` byte 15
  is still 00, and its DOS.O88 is byte-identical.
- **S3, Skies' `cs_xpand`.** It asks SUBSEG first, with DS = ES = ours:
  the second caller's DS is the read buffer, and the cell names the region
  by DS. It expands straight into the selector it gets. On CF = 1 the
  granule path runs unchanged.
- **S7.** Word and Scribe also say `OS88_HIGHMEM` (14.44.3).
- **Docs and gates.** SPEC.md 20.12.10.9 (new) and 50.3.6 (new) carry the
  contract. `apps/os88api.inc` changes `OS88_HIGH`'s comment ("a re-homed
  program says it in its own header") and adds `OSAPI_MEM_SUBSEG`. The
  abilock history records the change as additive. `tests/megagrid.txt`
  registers `apps/dos/dos.asm`'s 5 MCB-chain sites: they sit in
  `%ifndef DOS_EXTCORE` code that the program part does not assemble, and
  the core runs them in real mode under kern_dos.

#### 14.44.2 What it cost

Measured. kern_big and kern_small were A/B'd by kernsize at one commit
before the merge, and checked against the diff.

| kernel | section | bytes | what |
|---|---|---:|---|
| kern_big | `.text` | **+6** | the cell, 42,884 -> 42,890; nothing else moved. No rung crossed: kern_big's image rung was already crossed at the base against the blessed baseline |
| kern_small | `.text` | **+6** | the cell. Every other line is inside `%ifdef KERN_MEGA` |
| kern_mega | `.text` | +6 | the cell. The megaseg overhead is unchanged (5,931 after the merge), since the cell is in kern_big too |
| | `.cold` | **+45** | 41,170 -> 41,215 before the merge: step 8b 30, the SUBSEG far body 15 |
| | `.mega` | **+315** | `mega_subsegf` 164, `mega_rgnsubfree` 101, `mega_rgnblk` 47, `mega_pkghifree` +3. That is above 14.38's 70-90 estimate, because it includes the teardown scan and the base lookup the estimate did not price |
| WORD.O88, SCRIBE.O88, kern_mega DOS.O88 | | +0 | one header byte each (the files are the same length) |
| SKIES.O88 | image+bss | +48 (61,409 -> 61,457), file +16 | the 38-byte SUBSEG arm, and alignment downstream of it |

#### 14.44.3 S7: Word's and Scribe's claims against the OS88_HIGHMEM contract

Every `OSAPI_MEM_CLAIM`/`_REGROW` site in both programs and their includes
was walked: `os88type.inc`, `os88ui.inc`, `os88img.inc`, `os88dec.inc`,
`wd*`/`sc*`. A scan was run for a register loaded from a segment and then
used in arithmetic, for any `add/sub/cmp` with a `*seg` word, for `int`
calls, and for far calls through data. Findings:

- **Every claim is 64KB or less.** The largest is the 62KB load and save
  staging, `WD_LSTGKB`/`WD_DOCCAP`/`SC_LSTGKB`. The rest are documents and
  CHP up to 30KB, arenas to 16KB, the 1KB PAP dictionary, the 9KB italic
  table, Scribe's 40KB picture scratch (shrunk in place, with the `DX
  unchanged` assumption that `mega_claimregrowf`'s shrink keeps), pictures,
  and `os88ui`'s save-unders. Each one can be a pool selector.
- **No arithmetic on any claim's segment.** The only hits are `or dx, dx`
  zero tests.
- **No far call into a claim. No DMA or BIOS call on one.** The face cache
  uses `OSAPI_MEM_CLAIM_DMA` (8KB at most, 512-aligned), and the DMA door
  stays conventional by the contract, so that claim and its `test dl, 1Fh`
  assertion are unaffected. `os88ui`'s save-unders go through
  `OSAPI_GFX_SAVE`, a kernel service.
- **Both say `OS88_HIGHMEM`.** Measured: each holds its claims as `A0h |
  slot` pool blocks, and no conventional claim of either remains. The
  default face is the kernel's 8x8, so not even the face cache is claimed.
- **What stays conventional:** a non-kernel typeface (the DMA door), and
  any claim the pool cannot hold, which falls back exactly as before.
- **One consequence, by design.** A HIGHMEM claim freed while the program
  still holds it in ES makes the X stub's `pop es` hit a freed selector.
  `mega_gpfix`'s `.stale` arm loads NULL instead. 86Box showed it twice in
  Word (`#GP 0060:04ED err 285C api_x.done+0`). This is a fault and not a
  bug, but it is a trap per occurrence. Should Word ever do it in a loop,
  `megatrap0` would be the row to add WORD to.

#### 14.44.4 What was measured, and on what

**Bochs 2.7, 16MB, `build/mega.img` with C: holding the packages:**

- **CLEAR SKIES.** `I_SPTR 2834`, a 61KB block at pool +0 (0x140000). LDT
  0x507, 0x508, 0x50A and 0x50B are four SUBSEG selectors inside it (the
  vocabulary at paragraph E44h, and the bands). The title bands draw.
  **Fly** loads PARIS-ISSY and renders the cockpit, and Esc returns to the
  title. On Close, all six of the region's descriptors are zero (0x505 to
  0x50B, the pair included).
- **DOS.O88.** `I_SPTR 2834`, a 50KB block. `cd RUN`, `doshello`, then
  `parked 0052K at 14C800`, HELLO.COM's screen, a key, `unparked`, and
  `exit code 042 (Arena: 621KB)`.
- **SCRIBE** draws and takes typing, at `I_SPTR 2834`/`284C` in the
  sessions it ran in.
- **WORD.** `I_SPTR 2854`/`285C`. Text typed, and **Edit > Search found
  "zebra"**: part 1's code at `WD_P1ORG`, through the high CS.
- **The owner's scenario.** Conventional free was **563.5K** in one run
  before Word. With Word open (its 62KB region and 3 claims in the pool)
  it was **563.5K**. With the Video Player then playing `08-WILLY.V88`
  (Live) beside it, it was **556.5K**: the player's own 7KB conventional
  claim. Its region went high too, since VIDEO.O88 says `OS88_HIGH`, and
  the video's 1,534KB is a pool block.
- **The A/B, OS88_BOCHS_MEGS=2.** The same kernel with WORD.O88 from
  before this change (no `OS88_HIGH`) lands at **8FA0** with 61.1KB of
  region and 3KB of claims. The largest conventional run falls **565.5K ->
  498.0K**. The new WORD.O88 leaves it at 565.5K.
- **A compaction MOVES a re-homed region.** Open SCRIBE (pool +0) and WORD
  (+63K), then close SCRIBE. MEGACP then fills the pool with 60KB claims,
  frees every other one, and forces `mx_compactf` with a 64KB claim:
  `MEGACP: n 0A freed 05 big 2844 ok`. WORD's block moved **+63K -> +61K**
  with its selector unchanged (285C). LDT 0x50A/0x50B now base at
  0x14F400, which is pool + 61K. **Edit > Search found "zebra" again**
  after the move.

**86Box `286-mega` (mr286, 16 MHz 286, 4MB: the 286 RESET path):**

- WORD (`2834`), SCRIBE (`285C`), CLEAR SKIES (`2884`) and DOS.O88
  (`28BC`) were all above 1MB AT ONCE, each a pool block. They were read
  off the guest with `os88box memr`.
- Word's Edit > Search found "zebra". Scribe drew and took typing. Skies
  flew and Esc returned.
- **The DOS trip went through the 286 reset with four re-homed regions in
  the pool:** `exit code 042 (Arena: 622KB)`.
- `faults` shows the expected `mega_svc_lo.r286` resets and the two
  `.stale` pops above. It also shows several `#GP 2764:xxxx [Segment can't
  write]` with ES = 28BC during the trip. These are CS writes that
  `mega_gpw` completes (item 87), from the code under selector 2764, a
  pair the trip uses. They were not A/B'd against a low DOS.O88 here, so
  whether they are new is OPEN. The trip completes either way.

**kern_big (MartyPC), the shipped packages changed for every kernel:**
`soak -k rehome -k rehomemove -k wdparts -k wdtype -k scsize -k skies -k
skiesworlds -k skiesset` ran **8/8 ok** (189 s).

**The row, `tests/megarehome.py`** (soak, `needs=("bochs",)`, 93 s
measured, 120 declared). MEGARUN starts DOS.O88, SCRIBE, SKIES and WORD by
name on Bochs. The debugger then reads `inst_tab`, `mx_tab`, `mem_tab` and
the LDT, and asserts each of 14.44's claims. It is **red** with the
pre-14.44 WORD.O88 in place: `FAIL: WORD I_SPTR 8F60 - a conventional
segment`.

#### 14.44.5 What is left, and for the owner

- **S4, S6, and Pixelstein's S7** wait for their part, after 14.37's
  interface (MX_FHUGE, `mx_alloc` with BL = 80h | slot, the rebase).
  `OSAPI_MEM_SUBSEG` with BL bit 0 is already what S6 needs for GEN's code
  half.
- **D4, the conventional room at launch**, is deferred.
- **Still untested: a region moving while its OWN program is the caller**
  (14.15, 14.38.3). The compaction above ran from MEGACP's claim, not from
  Word's. It needs a pool fragmented under the program so that one of its
  own claims cannot fit. *(2026-10-22: resolved - 14.46.5, row `megaown`.)*
- **The SUBSEG cell number.** 0x04BD was free on `protected-mode` at
  78436d50. A branch merged later that also appends a cell must take the
  next one.
- **The `[Segment can't write]` traps under 2764 on the 286 DOS trip**
  (above). Whether a low DOS.O88 takes them too is open, and a
  `megatrap0`-style count is the instrument. *(2026-10-22: resolved -
  14.46.2: they were HIBER.DRV's own stores, `CSDO` now, 0 on both arms.)*

### 14.45 The KERN-MEGA-SEGMENTS-PLAN leftovers: the park's buffers, the refusals, W4, the readings (2026-10-21)

KERN-MEGA-SEGMENTS-PLAN 8.6 and W4, built on `protected-mode` at ee3c8398.
That plan's 8.7 is the design record; this is what it cost and what was run.

**Built**:

- **The park's three buffers leave `.text`** - the region and copy list
  (`mega_dreg`, 716), the KB-in-use map (`mega_dmap`, 96) and the header's
  GDT and cells (`mega_dhc`, 82). Not into the park block, as the plan had
  it: the map sizes the block before it exists, and a hibernate's write and
  resume build and read the list with no park block at all (14.43's save
  path runs `hbm_xplan` before the claim, on purpose). They are the tail of
  `.mega`'s own rung (`MEGA_SCR_OFF`, `MPS_*` in `kernel/megados.inc`) through
  a data selector of their own, `MEGA_SEL_PSCR` = 0xFFD0, set by `mega_mdesc`
  beside `MEGA_SEL_CODE` - so above 1MB with `.mega`, given back with its rung
  at the switch, never summed. `kernel/hiber.inc`'s six readers (`hbm_xplan`,
  `hbm_xfit`, `hbm_xruns`, `hbm_xwrite`'s head and runs, `hbm_xres`'s head and
  bases) take ES (or DS for a `movsw`) = the scratch through `hbm_pses`.
  `mega_dhcfill` builds the header's cells there and `mega_dstubup` puts the
  list's 8-byte head in front of it, so each is one copy up as before.
- **`mega_enter`'s real-mode refusals are `.cold`** (`mega_gatef`): the CPU
  test, `mega_tasks_ok`, the whole A20 gate (`mega_a20`, `a20s`, `a20t`,
  `kbw` - only `mega_enter` ever called them) and the room test, the toast,
  the rung to the heap. It is 8086 code, being what an 8088 runs to refuse.
  The four debug-port sentences went with it: E9 now prints `kern_mega: `
  and the toast's own line (`kern_mega: real mode: A20 will not open`), 254
  bytes of `.text` for twelve.
- **W4: kern_mega's `COLD_VSTART` is 0x80** (`kernel/kernel.asm`). Not the
  plan's 0x10: `COLD_SEG` is a GDT selector there, so it must be on the
  8-paragraph grid - `COLD_RAM - 8`, the slack above task 0's stack. `.cold`
  may reach 65,152 bytes where it stopped at 48,896.
- **`tools/kernsize.py` knows kern_mega** (KERN-MEGA-HANDOFF Track 6):
  `--build build/megak` alone now measures the tree as kern_mega and prints
  `kernsize[mega]` - the sections with `.mega` and its scratch (a `ksm:` line
  kernel.asm emits under KERNSIZE), the rungs, the segment, and what stays
  resident once `.cold` and `.mega` are above 1MB (65,024 here) - and refuses
  `--bless`, kern_mega having no baseline. `tools/os88megaseg.py` counts the
  scratch in the high slot.

**Cost, measured at build 267 (ee3c8398) against the change built uncommitted
(one build number)**:

| | before | after | |
|---|---:|---:|---|
| kern_mega `.text` + `.bss` | 54,094 | 52,716 | **-1,378** (buffers -886, refusals -492) |
| `tests/megaseg.txt` (lowered) | 5,935 | 4,557 | -1,378 |
| `.mega` | 14,252 | 14,354 | +102 (segment prefixes, `push`/`pop es`) |
| `.mega`'s scratch tail | - | 916 | in the rung, not resident |
| `.cold` | 41,730 | 41,994 | +264 |
| resident rung (`KIMG_PARA`) | 54,272 | 52,736 | **-1,536 conventional bytes** |
| `.mega` rung (boot only) | 14,336 | 15,360 | +1,024, given back at the switch |
| HIBER.DRV (kern_mega) `.modh` / file | 9,307 / 8,052 | 9,380 / 8,122 | +73 / +70, an on-demand module |
| `KERNEL.SYS` (kern_mega) | 103,968 | 103,872 | -96 |
| kern_big / kern_small | | | **byte-identical**: `kernel.bin`, `kernel-full.bin`, `kernel.sys`, `hiber.drv` compared at one build number; every line is `%ifdef KERN_MEGA` or a kern_mega-only file, and kern_small's arms are untouched |

**The 11,264 is a reading now** (KERN-MEGA-SEGMENTS-PLAN 8.2 called it
arithmetic). Free heap at the desktop, A: `mega.img` alone, `mem_top -
mem_base` less `mem_tab`'s claims read out of the running kernel:

| kern_mega | Bochs 2.7 (a 386, 16MB) | 86Box `286-mega` (MR BIOS 286, 4MB) |
|---|---:|---:|
| `9eb5e828`, before `.mega` | 569,856 (floor 1360h) | 570,880 (1360h) |
| `cf7e263d`, `.mega` built | 581,120 (10A0h): **+11,264** | 582,144 (10A0h): **+11,264** |
| this change | 582,656 (1040h): **+1,536** | 583,680 (1040h): **+1,536** |

The two emulators differ by 1KB only in `mem_top` (Bochs's BIOS keeps a 1KB
EBDA, 9FC0h; the MR BIOS does not). One claim stands at the desktop on both
(5KB, tag FE03, at the floor), the drivers and the read-ahead being above
1MB.

**Run on the final kernel**:

- **Bochs** (`--bootc --noa --csys`, DOS.O88 and KDHELLO `-DKDHOLD` on C:):
  boot to protected mode; a DOS trip (`parked 004FK at 14CC00`, `unparked`,
  exit 042); the same trip with *Save the session to disk* ticked, Bochs
  stopped mid-program, booted with `--c keep`: `kern_mega: resumed`, *The
  machine was restarted while it ran.*; a trip from the resumed session; a
  menu Hibernate, a key, *Resume*: `resumed`, every window back; a trip after
  that. KERN-MEGA-SEGMENTS-PLAN 8.6's "a hibernate and resume under Bochs"
  is this.
- **86Box `286-mega`**, the 286 reset path: floppy boot - `mega_sdr` reads
  **`01 01 02 00`**, the calibration's two `#GP`s at `.mega` 15D1/15EA and
  nothing else of the kernel's; Calculator (12*12 = 144) and Fractal opened;
  a DOS trip off B: with both open (`DOSP 0087K`, exit 042, both still
  there); the save box greyed. Then a 20MB IDE C: (the Bochs disk, type 2,
  booted off C: with no A:): a trip unticked, a trip ticked and the machine
  powered off mid-program (`quit`), relaunched on the written disk - the
  session back, the restart line; a menu Hibernate, the MR BIOS's
  *Parallel Port Configuration Change* (14.43's note), *Resume* - the
  session back; a trip after it. `faults` over the C: run: 114 resets at
  `mega_svc_lo.r286+31` (the reflections' 286 reset), one at
  `mega_dosgo_lo.r286+32` (the drop), the calibration's pair twice, one at
  `mega_ldt_free.out` (decision 14's stale selector, by design) - and the
  `[Segment can't write]` traps under 2764 that 14.44 already lists.

**Rows that cover it**: `megagrid` (fast, run by `make`); `kernmods` (soak,
run here: `t_kernmods.py` 57 checks); and the kern_mega soak rows that boot
it - `megarehome`, `megaflt`, `pxmega`, `megatrap0`. No registered row drives
a kern_mega hibernate or DOS trip (14.43 says why); the recipe above is what
one would script.

**For the owner to look back over**:

- **The buffers are not where the plan put them**, and the plan's location
  could not have worked for the hibernate. A selector of their own is the
  plan's "optional data alias" (its 3.1) put to a different use; 0xFFD0 is in
  the park's banked set, so a trip that damages the GDT gets it back.
- **E9's refusal lines changed wording** (four sentences became the toast's
  own text). Nothing in the tree greps for them; a field note quoting the
  old ones still names the same refusal.
- **W4 took 0x80**, not 0x10 - the plan's figure would have put `.cold`'s
  selector off the grid.

### 14.46 What had never been run on kern_mega, run - and what it found (2026-10-21)

The paths 10.3 items 50, 63 and 85, 14.5, 14.14, 14.15, 14.16 and
KERN-MEGA-SEGMENTS-PLAN 8.6 listed as built and never exercised, each run
on the current kernel (14.37's movable huge claims, 14.39, 14.40, 14.42,
14.43, 14.44) under Bochs with `make megatrap`'s census, and where the 286
path matters on 86Box `286-mega`. Every count below is trap lines from
`tools/megatraps.py`; "0" means none charged to the program, the boot's
two `mega_strcal` calibration lines aside. A `kern_mega CLOSED` (14.42) was
looked for in every run and none appeared.

#### 14.46.1 The census

| program, what was done | emulator | traps | result |
|---|---|---|---|
| Tank: title, F, drive and fire to GAME OVER, Esc | Bochs, SB16 | 0 | plays fullscreen, back to the desktop |
| Clear Skies: Fly, the sim on the runway, Esc (as it is on `protected-mode` after 14.44) | Bochs | 0 | flies, back |
| WireBall: L (practice), the paddle held up and down to 1:11, Esc | Bochs | 0 | plays; the rate hook runs |
| PIXELSTEIN 3D: Space, walk and fire (as it is: another agent is re-homing it) | Bochs | **59,982 in ~20 s** | plays; see 14.46.4 |
| 1942 past its title: Enter, fly and fire to GAME OVER, Alt+Enter both ways | Bochs | 4 | VGA Mode X fullscreen; the 4 are SOUND.DRV's (below). **Its CGA fullscreen is not reachable**: 1942 picks `FSXM_CGA320` only on a CGA display, and neither Bochs nor `286-mega` has one |
| the DOS box: DOSHELLO.COM and DOSHELLO.EXE | Bochs | **315 a trip, then 0** | `exit code 042 (Arena: 621KB)` both; 14.46.2 |
| Browser over `--ne2k`: a page from a host `http.server`, then its link | Bochs | 1 | both pages drawn |
| FTPD over `--ne2k`, driven FROM THE HOST: LIST, RETR, STOR of 40KB, RETR of it back | Bochs, hostfwd | 0 | the 40KB came back byte for byte |
| the Apple II: WELCOME.BAS opened, RUN through text, lo-res and the speaker | Bochs, SB16 | 1 | runs to `]` |
| LOOM: PONG.WML opened, File > Pack Bundle | Bochs | 1 | `Packed PONG.WAB, 1008 bytes` - **byte-identical** to `build/PONG.WAB` from `tools/weavesim.py --pack` |
| Paint: 448x280 -> 590x280 (82.6KB, across 64KB) -> 590x390 (115KB) -> 353x390 -> 353x359 (63.4KB, back under); 448x200 -> 448x390 -> 448x200; strokes drawn at each | Bochs | 0 | every picture kept |
| DESKCAL, DESKCLOK, STIKNOTE | Bochs | **99 in 55 s, then 0** | 14.46.3 |
| PiXEL: BALLOONS.PNG, Effects > Blur, Effects > Sepia | Bochs | **1 + 1, then 0** | the `FFD8:227C` line of 14.40; 14.46.3 |
| RAMDISK.DRV: Store Xms, 1024K, Mount; A:\APPS copied onto it; PAINT.O88 launched off it | Bochs | 28 on the page, 1 for the copy and launch | `Mounted D: 1024K of 1024K`, 128KB copied, Paint ran from it; 14.46.4 |
| the Video Player, the bank in place: 06-SNOW.V88 (8.1MB) off C: at 4MB, left ~8 minutes before Play | Bochs, SB16 | 0 (SOUND.DRV's 4 at the stream's start) | `XMS bank in place (PM), 2656 KB`, `Drew 301/301, stalls 0, pauses 90`, `Late 150, 68 ticks of 273`. The tick figures are Bochs's clock against the card's and are not judged here. The player's claims were left alone (another agent's) |

Every `1` above is the same line: `0060:04ED`, `api_x.done`'s `pop es`,
with a selector the program had just freed still in its ES. That is
`mega_gpfix`'s `.stale` arm (decision 14) doing its job - the NULL selector
loads instead, once a free - and it is left: the pop is in `.text`, in the
stub every X cell returns through.

SOUND.DRV's four are `sbl_hook` writing the IRQ vector through ES = 0
(`mov ax, [es:si]` ... `mov [es:si+2], cs`), once a stream opens:
`mega_null0` exists for exactly that driver idiom, and they are left.

#### 14.46.2 Fixed: the DOS trip's and the hibernate's fixups (HIBER.DRV)

**The DOS trip took ~315 `mega_gpw` fixups a trip**, every one in
HIBER.DRV, which runs `hbm_dosrun` in protected mode on kern_mega:
`hbm_extat` wrote its four counters through CS once a CLUSTER of DOS.O88's
part chain (57 clusters here, 4 sites), step 3c's volume table once a ROW
(16 rows, 3 sites), and a dozen one-offs (`hbm_sweep`, `hbm_drvsusp`,
`hbm_xcap`, the launch block's fields). `tests/cswrites.txt` said the file
was "real mode by design and replaced on kern_mega", which had stopped being
true once the DOS trip's park put this path in protected mode. All 24 are
`CSDO` now - the bare instruction on kern_big. `hbm_wake`'s read of the
ROM's tick through segment 0 (two null fixups a return) is `MEGA_SEG0`.

**Measured after** (Bochs, the same .COM and .EXE): **0** on DOS.O88 as
14.44 ships it (high, `parked 0059K`) and **0** on a copy with the program
part's `OS88_HIGH` cleared (low, `parked 008BK`).

**The `#GP 2764:xxxx [Segment can't write]` lines 14.44's agent saw on
86Box** were these - HIBER.DRV's image under its LDT alias, with ES = the
DOS region - and not something 14.44's re-home introduced: the sites are the
module's own code, which does not branch on where DOS.O88 is. The pre-fix
LOW arm was not counted, so that half is argued; after the fix both arms are
0 on Bochs, and on 86Box `286-mega` two DOS trips leave `faults` with
nothing but the 286's reflection shutdowns (`mega_svc_lo.r286`,
`mega_dosgo_lo.r286`).

**`Disable the mouse` takes** (fdac077c), measured on 86Box `286-mega`,
whose mouse is serial (Bochs's is PS/2, which gets a zero base and the still
pointer either way, SPEC.md 96.45). DOSMOUSE.COM, the mouse moved 120x80
before its first read: **unticked, `POS1 x=440 y=176`; ticked, `POS1 x=0
y=0`** - kern_dos hooked no UART. **`INT 33h AX=0` answers `ax=FFFF bx=2` on
both arms**, and that is SPEC.md 96.36.8's contract ("Off, `kern_dos` hooks
nothing and INT 33h answers 96.10's still pointer"), not a defect. The brief
expected 0 there; see 14.46.6.

**The hibernate in protected mode** (14.5's sum, SEGMENTS 8.6's `.mega`):
Bochs off C:'s boot partition (`--csys --bootc --noa`), MEGAHIB holding 64KB
of XMS and a Mines board played, System menu > Hibernate, power off, boot,
Resume: `kern_mega: resumed`, the board as it was, `MEGAHIB: ok` - the
programs' sum held, so 14.5 is now run on kern_mega in protected mode. And
**on 86Box `286-mega`** (the 286 RESET path; C: a 20MB IDE disk built as
Bochs's is, booted with no A:): hibernated, a hard reset (86Box's clears
RAM), Resume, the same board.

**But the hibernate took 32,513 fixups**: `hbm_wrimg` wrote the whole
conventional picture with ONE `dskw_write` from segment 0, and HDD.DRV's
PIO loop moves it a word at a time, so every word of the first 64KB was a
`mega_null0` - two exceptions each, of the order of a second on a 16 MHz 286
by the handlers' length (not timed). **`hbm_xwconv`** writes it a chunk a
call instead: chunk 0 for its size from segment 40h (the identity map starts
at GDT entry 8), chunks 1.. appended from n x 800h, then chunk 0's first
CLUSTER put right by a write-at from segment 0 (LOW0 stretched over just
that) and the rest of it from 40h. The same file. **1,024 fixups at this
C:'s 2KB cluster**, Bochs and 86Box alike. Two shapes failed on the way and
are worth knowing: the IVT's 1KB written alone and the rest appended -
`dskw_append` refuses a file that does not hold whole clusters (SPEC.md
18.4.4), so the hibernate failed quietly and the screen saver came up - and
the cluster's write-at without the stretch, a `kern_mega FAULT` in HDD.DRV
at DI = 500h (KERN-MEGA-DOS-PLAN 10.3's W5 finding 1, again).

The hibernate's own nine one-off CS writes (`hbm_wrimg`, `hbm_ptrfix_set`,
the ask's paint) stay `mega_gpw`'s, one fixup each a hibernate: as CSDO they
were 77 bytes that took the image over `MODH_UNPAD`'s KB edge and its claim
from 10KB to 11KB while it is loaded.

#### 14.46.3 Fixed: the Sticky Apps, the census logger, and PiXEL's free

**The logger's fault** (14.39's finding). `mega_ldt2ds` took the entry after
ANY allocator code selector as its data twin. DESK.DRV's library above 1MB
has a LONE one (`mega_dklibf`), so `mega_tlog` loaded the free entry after
it into ES and faulted at `FFD8:0D91` on every Sticky App's first callback.
`mega_ldt2ds` now takes the twin only when it is one: a present writable
data entry (0x92, the CPU's accessed bit aside) over the same base. The same
routine is `mega_hifree`'s, which would have freed a neighbour's entry for a
lone selector.

**The trap itself**: `os88stik.inc`'s `stik_cb` far-called the package's
trampoline at DS - one `xfer` fixup each callback, 99 in 55 s for the three
(the tick and the cards). It asks `OSAPI_MEM_CODESEL` for DS now: DS itself
on a real-mode kernel, the code alias or a pair's code half on kern_mega
(SPEC.md 110.15 says so). `megatrap0` now starts the three in a second boot:
**13 packages, the boot's 4 lines and nothing else, 222 s under Bochs, 277
with the tree's build**. Without the `stik_cb` change the same boot charges
the 99 to them.

**The `FFD8:227C` line of 14.40** resolves, on the census build of the day,
to `mega_ldt_free`'s closing `pop es`: a claim freed with ES still on it
(PiXEL's Blur and Sepia each free one), so the pop loaded the entry the
routine had just cleared and `.stale` gave it NULL. `mega_ldt_free` puts the
NULL there itself when the caller's ES is the freed selector. Blur and
Sepia: **0**.

#### 14.46.4 Found and written down, not fixed

- **PIXELSTEIN 3D's play: ~3,000 fixups a second.** `px_cast`'s `call far
  [px_qcur]` through a data segment (6,912 in ~20 s), `px_compose`'s stores
  through CS at nine sites (`px_cdi`, `px_ltop`, `px_lbot`, `px_lu`,
  `px_cwrote`, `px_lh`, `px_lmat`, `px_lside`, `px_qp`, 5,604 each), and two
  in an unnamed image at `22C4:011D`/`0125` (428 each). That is the
  renderer's own inner loop; the re-home agent has Pixelstein, so it is
  theirs.
- **RAMDISK.DRV's Control Panel page: ~28 fixups a visit.** `rd_page_call`'s
  `call far [cs:rd_pfar]` goes through the page image's DATA segment -
  `rd_pfar`'s segment word IS `[rd_pseg]`, "storing the one sets the other"
  - and RAMPAGE.DRV calls the driver's services back with DS = the CS it was
  given (`push cs / pop dx` at RDT_INIT), so the services' stores are
  `mega_gpw`'s. The shape of the fix: a far pointer of its own whose segment
  is `OSAPI_MEM_CODESEL`'s answer at load, and the driver's DS handed to the
  page beside its CS. Cold (the page only), so left.
- **Paint**: once, asking H = 200 at W = 353 just after the canvas had come
  back under 64KB gave 359. From 448 wide, 390 -> 200 gave 200. Not chased.

#### 14.46.5 Verified new: a region and an overlay moved under their own program

**14.15's open item** - a region moving while its own program is the
caller. `tests/megaown`: MEGAHOLE (OS88_HIGH | OS88_HIGHMEM) holds three
60KB pool claims and closes itself, leaving a hole under MEGAOWN's region;
MEGAOWN fills the pool, frees every other block and claims 64KB from its own
wake handler. At 2MB: **region KB 181 -> 120** across the claim; every
stamp, its own bss and a word of its own code read back, and a callback two
seconds later the same. At 16MB the pool never fills and nothing moves,
which is why the row runs at 2MB. Row `megaown` (82 s); red with
`mx_compactf` pinning 80h | slot (181 -> 181).

**An overlay moved by a compaction**: CWORD's overlay is part 0, an EAGER
carve claimed top-down in conventional memory (`OSAPI_MEM_CLAIM_DMA_HI`),
so it is the conventional compactor and not the pool's that can move it.
MEGALOWH holds a top-down claim at the heap's ceiling, CWORD's carve lands
under it, MEGALOWH gives it back, and MEGALOWB (OS88_HIGH, so its own region
is not a wall - the first run had it low and pinned beside the carve, and
nothing moved) claims all of `OSAPI_MEM_AVAIL`: **the carve 8B40 -> 9A80**,
and then CWORD took typing and File > Save As onto C:, and `C:\DOC.RTF`
holds the line. Row `megaovl` (225 s); red with `cc_ovinit`'s
`OSAPI_MEM_MOVABLE` taken out (8B40 -> 8B40). No carve can be above 1MB
today - the parts standard claims code through the DMA door - so the
selector arm of `cc_ovcarve` is not reachable.

#### 14.46.6 Cost, rows, and for the owner

**Bytes**, both arms built at one commit:

| | |
|---|---|
| kern_big `kernel.bin`, HIBER.DRV | byte-identical (md5) |
| kern_mega `.text`, `.bss`, `.cold`, `.lowbss` | unchanged; the segment overhead 5,935 as before |
| kern_mega `.mega` | **+58** (`mega_ldt2ds`'s twin check, `mega_ldt_free`'s ES) |
| kern_mega HIBER.DRV image (`.modh`) | **+301** (9,307 -> 9,608; the claim stays 10KB, 30 bytes inside the edge), 8,052 -> 8,213 bytes on the disk |
| DESK.DRV (the library, every kernel) | **+12** (`stik_cb`) |

**Rows** (all soak, all Bochs): `megatrap0` (now 13 packages, 300 s),
`megaown` (new, 100 s), `megaovl` (new, 260 s, needs the C toolchain),
`megahiber` (new, 300 s: the hibernate and resume, its census capped at
2,048, red at 32,513 with the one write put back). The MartyPC rows that
cover `stik_cb` on kern_big: `stiknote`, `deskclock`, `deskcaledit`,
`deskcalcards`, `stikhiber`, `stikmove`. `tools/os88bochs.py` takes
`$OS88_BOCHS_SLIRP` (docs/BOCHS.md), which is how a host client reached
FTPD.

**For the owner:**

- **`Disable the mouse` gives a still pointer, not "no mouse".** `INT 33h
  AX=0` answers FFFFh/2 with the box ticked, per SPEC.md 96.36.8. A program
  that looks for a driver and adapts (no cursor, keyboard only) still finds
  one. If the intent was that it find none, that is a change to 96.36.8 and
  96.10, not a fix. *(2026-10-22: the owner chose "no mouse" (14.51) and it
  is built: `kern_dos` points 33h at an `iret` when it has no mouse to
  serve, kerndos/kdentry.inc:371, row `kdnomouse`.)*
- **The hibernate writes its picture in ~22 calls rather than one** on
  kern_mega in protected mode, each append walking the chain to the file's
  end (DISK-CPU-PLAN 6). On a hard disk that is a FAT walk a call against a
  32KB write; it was not timed, and the field machine's BIOS-only C: is
  where it would show.
- **1942's CGA fullscreen and its cartridge build** remain unseen: no
  emulator here gives kern_mega a CGA.
- **The bank in place's `68 ticks of 273`** wants a look on a machine whose
  clock is real: Bochs runs `clock: realtime` against an SB16 whose DMA
  timer is its own.

### 14.47 The action modules above 1MB: CTRL, FORMAT, DISKOPS and HIBER, and the boot's modules moved at the switch (2026-10-21)

**The owner's ask**: *"Action modules: These live only as long as the user
is taking an action, so I do want them done."* 13.5 had put five modules
above 1MB (DOCK, EXTD, WIN95, DESK, STIKMENU) and kept four low - CTRL.DRV,
FORMAT.DRV, DISKOPS.DRV, HIBER.DRV - and item 52 left two limits: CTRL.DRV
low, and a module the boot loaded in real mode never moved. A module held
low is conventional memory taken for exactly as long as the user is doing
something else that may want it: the Control Panel open beside a program, a
format, a compress, a hibernate or a DOS trip being set up.

**Why each was low, and why none of it binds now.** 13.5's census was
taken on 2026-10-18, BEFORE item 77, and its one real blocker was the file
layer: a buffer of the image's own handed to `dskw_*` was normalised and
converted with `mega_s2segf`, which answers FFFF above 1MB. Item 77 is that
case's landing (`dskw_hiseg` keeps the selector and moves the offset, a run
at a time through the scratch since item 85), and `dsk_runcap` bounces any
LDT selector through `dsk_secbuf`, so a module's buffer above 1MB reaches the
drive the way a package's does - native floppy, reflected `int 13h` and a
driver volume alike. Module by module:

| module | what the census held against it | what is true now |
|---|---|---|
| CTRL.DRV (14KB) | SYSTEM.CFG's save from `cpc_buf` and the Alt-drop `.LNK` write, ES = the image | both are `PUSH_CSDATA` - the pair's DATA half above 1MB - into `dskw_write_sys` / `dskw_write`, item 77's path. The boot sector's Cylinder byte goes through `dsk_secbuf`; the Hard Drive page's Format and Install are HDD.DRV's and HDDTOOL.DRV's, with claims of their own |
| FORMAT.DRV (4KB) | nothing - "gains nothing, held only while a format runs" | the address fields and every sector it moves are LOW_SEG's `dsk_secbuf`; the owner's ask is the gain |
| DISKOPS.DRV (11KB) | `CLO_RSEQ cs`, a read-sequence cursor in the image | the file layer copies the cursor back through ES = the module's CODE selector, which faults and completes through `mega_gpw` - exactly as it did low, the conventional alias being a code selector too. The cloner's windows are conventional claims, its single sectors `dsk_secbuf`, the XMS path's source a staging claim |
| HIBER.DRV (10KB) | the file layer, `apic_font_str` with DS = the image, "and it refuses protected mode anyway" | the last stopped being true at W5. Its pointer reads and writes are item 77's; DS = the image is a READ through a readable code selector; its real-mode code is COPIED out before it runs (`kds_stub` to the text page, the park stub by linear base in `mega_dstubup`); every wake address it stores of itself (`HBP_WAKE`, `mega_hbwk`, `KDL_WAKE`) is stored after the move. A hibernate image's run list already carries every pool block in use and run 0 the LDT, so the wake far-jumps to the hibernated copy's own pair; a resume's staging block is placed clear of the FRESH boot's HIBER.DRV block as of any other (`hbm_xfit`); a DOS trip's park claims what the pool has LEFT, so the module's block stands where it is |

**Built** (all `%ifdef KERN_MEGA`):

- `kernel/mod.inc`: `MOD_HIOK` is every module - CTRL, FORMAT, DISKOPS,
  HIBER beside the five, and `MOD_SETS`, CTRL.DRV's settings core. `mod_hi`
  sizes the settings core's pair as `mod_need` sized its claim, the WHOLE
  image (`MODC_SIZE`), because it READ only `MODS_SIZE` and its bss tail
  `cpc_buf` lies past the read: a pair cut to what was read would have put
  the SYSTEM.CFG buffer past its limit. `mod_hiboot`, called at the foot of
  `drv_hiboot_f` (so it costs `.text` nothing), walks `mod_tab` once after
  the switch and puts every module the boot loaded through `mod_hi` with BP =
  its header's `MOD_H_IMG`; `mega_mods_fix` has already armed the slots with
  the conventional code alias `mod_hi` re-arms.
- `kernel/mega.inc` (`.mega`): `mega_modvfixf` - the far pointers a module
  wrote of ITSELF while it was hooked in real mode, the two tables
  `mega_mods_fix` already converts (DOCK.DRV's `dkv`, WIN95.DRV's `thv`),
  re-pointed from the alias to the pair; a module loaded in protected mode
  moves before it hooks and never needs it. And `mega_modsayf`:
  `kern_mega: above 1MB: CTRL.DRV 00FF3800` on the debug port for every
  module that moves - the driver line's shape (`mega_drvsayf`) - so a field
  run or an emulator reads where each one went.

**Cost, measured at build 270 (ff8d47ab) against the change built
uncommitted (one build number)**:

| | before | after | |
|---|---:|---:|---|
| kern_mega `.text` / `.bss` | 47,380 / 5,336 | 47,380 / 5,336 | **+0** (`tests/megaseg.txt`'s 4,557 untouched) |
| kern_mega `.cold` | 41,994 | 42,082 | +88, above 1MB (414 left in its rung) |
| kern_mega `.mega` | 14,354 | 14,446 | +92, above 1MB (10 left in its 15,360 rung, which is boot-only) |
| the nine module files (kern_mega) | | | the same sizes within a byte: only the layout stamp and the offsets of `.cold` moved |
| kern_big / kern_small | | | **byte-identical**: kern_big's `kernel.bin`, `kernel-full.bin`, `kernel.sys` and every `.drv` under `build/` compared at one build number; kern_small by the diff - every line is inside `%ifdef KERN_MEGA` |
| conventional memory while held | CTRL 14KB, DISKOPS 11KB, HIBER 10KB, FORMAT 4KB | **0** | the same KB come from the top of the pool (owner FEh) |
| ...for the whole session, a boot that loads them | DOCK 3KB, WIN95 2KB | **0** | |

The load is unchanged: `mod_need` still reads into its conventional claim,
checks and arms there, and `mod_hi` copies the claim up and frees it, so the
claim is a TRANSIENT for the length of the read (a 286 showed HIBER.DRV at
9D80h for about a second, then at 280Ch).

**Measured on Bochs 2.7** (a 386, 16MB; the E9 lines are the new ones):

- `mega.img` in A:: the Control Panel - `CTRL.DRV 00FF3800` - Theme set to
  Windows (`WIN95.DRV 00FF3000`), Dock to Left (`DOCK.DRV 00FF2400`), closed:
  SYSTEM.CFG (163 bytes) on A:, `mod_tab`'s CTRL row back to 0. Format B:
  (1.2MB) - `FORMAT.DRV 00FF3800` - and `os88disk.py --verify` passes the
  disk, 2,371 clusters free. README.TXT Compressed - `DISKOPS.DRV 00FE8000`,
  8,088 bytes of LZ4 to 6,912 of LZB - and Uncompressed: 13,011 bytes,
  identical to `build/readme-plain.txt`.
- A blank 20MB C: (`--c blank --cfg 2`): the Hard Drive page, Format slot 1
  (FAT16, 20M) and Install to "Done - remove the floppy, Restart", with
  CTRL.DRV above 1MB throughout and FORMAT.DRV at 00FED000 inside the
  install; the installed C: then booted alone, and a desktop gesture (A:'s
  icon dragged) loaded the settings core above 1MB and wrote a 176-byte
  SYSTEM.CFG to C:.
- `--bootc --noa --csys` with that Windows-and-left-dock SYSTEM.CFG: at the
  switch `DOCK.DRV 00FF6400` and `WIN95.DRV 00FF5C00` (`mod_hiboot`), and
  the title bars and the dock's strip draw from the moved images. A menu
  Hibernate (`HIBER.DRV 00FF2000`), a key, the fresh boot's `HIBER.DRV
  00FF3400`, *Resume*: `kern_mega: resumed`, the Disk window back. A DOS trip
  (`HIBER.DRV 00FEA800`, `parked 0045K at 14CC00`, `unparked`, exit 042).
  The same trip with *Save the session to disk* ticked: an 884,736-byte
  HIBERNAT.IMG on C:, Bochs killed mid-program, booted with `--c keep`:
  `resumed`, *The machine was restarted while it ran.*

**Measured on 86Box `286-mega`** (MR BIOS 286, 16 MHz, 4MB - the 286 reset
path; no debug port, so `mod_tab` was read out of guest memory): the Control
Panel at 281Ch (TI set: above 1MB), WIN95 282Ch, DOCK 283Ch, SYSTEM.CFG
written to A: and CTRL dropped; a Compress and Uncompress with DISKOPS at
2854h, the result identical to the plain README; two DOS trips with HIBER.DRV
at 280Ch, exit 042 each; booted with the Windows/left-dock SYSTEM.CFG, DOCK and
WIN95 above 1MB at the desktop; Format B: on a blank 1.44MB image with FORMAT
at 283Ch, `--verify` OK (the 360KB `apps360.img` in that 1.44MB drive cannot
take the 720KB format it offers - the image's size, not the formatter). Then
a 20MB IDE C: (type 2, booted from A:): a menu Hibernate (HIBER 282Ch),
*Parallel Port Configuration Change* (14.43's note), *Resume* -
`hb_resumes` 1; a trip with the save ticked (HIBER 283Ch), `quit` mid-program,
relaunched on the written disk: *The machine was restarted while it ran.*
`faults`: the reflections' 286 resets, the calibration's pair at `.mega`
15D1/15EA, one `#GP` at `mega_ldt_free.out` per high module dropped (decision
14's stale selector, by design - DOCK and WIN95 pay it today), and the
`[Segment can't write]` completions in HIBER.DRV (`hbm_sweep`,
`hbm_drvsusp`, `hbm_wake`) and DISKOPS.DRV that the same code makes through
its read-only conventional alias - 14.45's "traps under 2764" under a new
selector - and nothing else of the kernel's.

**Rows that cover it**: `megagrid` (fast, run by `make`); the kern_mega soak
rows that boot it - `megarehome`, `megaflt`, `pxmega`, `megatrap0` (none
opens a module, so they show only that the boot and the switch still work);
`kernmods` (run here, 57 checks). No row drives the Control Panel, a format,
a compress, a hibernate or a DOS trip on kern_mega - 14.43 says why; the
recipes above are what one would script, and `mod_tab` read through
`os88box.py memr`/`sym` is the check that a module is above 1MB (a selector
with bit 2 set).

**For the owner to look back over**:

- **The load still takes its conventional claim for the length of the
  read.** The file layer can read straight into the pair's data half now
  (item 77), so a module could be read above 1MB with no conventional claim
  at all - which matters only on a machine whose conventional heap is too
  full for a 14KB transient while a program runs beside the panel. Not
  built: it moves `mod_check`'s ES and the compressed read's in-place window
  onto a selector, and nothing here has run short.
- **4.1's "the modules stay resident" is still not done**, and with every
  module above 1MB its reason has changed rather than gone: dropping saves
  pool KB and the next open's disk read is the cost, and `t_moddrop`'s
  "freed when done" rule binds every row. Keeping them resident on kern_mega
  is a decision to take, not a fix.
- **The debug port gains a line per module move** (part of `.mega`'s 92
  bytes). Nothing greps E9 for it.
- **Every drop of a high module takes one `#GP`** at `mega_ldt_free.out`,
  the caller's ES still holding the pair. It was already true of the five
  modules 13.5 moved; four more now pay it, once per action.

### 14.48 Re-home HIGH, part 2: PIXELSTEIN 3D above 1MB, with its claims (2026-10-21)

14.38's S4, S6 and S7 for Pixelstein, and its decision 6. Built on
`protected-mode` at ee3c8398. What now holds:

- **Pixelstein's whole 138KB keep is one pool block**, and its four claims
  (level, art, sprite set, shadow) are pool claims.
- It plays windowed and fullscreen above 1MB.
- A compaction moves the block while it is open, and it plays on.
- Closing it frees every descriptor.

All four were seen on Bochs. 86Box's `286-mega` shows the same except the
compaction. With Pixelstein open, conventional memory holds nothing of it.

#### 14.48.1 What was built

- **S4, the kernel.** The changes are in `kernel/loader.inc` and
  `kernel/mega.inc`, all inside `%ifdef KERN_MEGA`.
  - Step 8b: a KEEP over 0FFFh paragraphs calls `ld_hisi`. That is
    `ld_hicx` with CX = the keep's PARAGRAPHS and SI = the bytes the pair
    covers, `[ld_need]`, which is the program's own image + bss (I_SIZE).
    A keep of 4,095 paragraphs or less takes exactly 14.44's path
    (`ld_hicx` clears SI).
  - With SI non-zero, `mega_pkghif` calls **`mega_imghuge`**. That makes
    one `mx_alloc` of the whole keep (owner `80h | slot`, not
    `MX_FHUGE`) and copies it 32KB per `mega_lincopy` call under
    `[sch_lock]`. The pair sits over the block's base, **its limit the
    program's own image + bss**, not 64KB.
  - `mega_rgnsubfree` now compares 24 bits (the block size in BH:DI), so
    the teardown frees a part selector at any offset of a block over
    64KB. Before this, a descriptor 64KB or more into the block was
    skipped. The byte textures' selector, at +108KB, would have leaked
    on every close.
- **`OSAPI_MEM_SUBSEG` gains BL bit 1** (SPEC.md 50.3.6,
  `apps/os88api.inc`).
  - CX is the window's length in paragraphs, so a part's selector ends
    where the part does.
  - A reused descriptor must now match the LIMIT as well as the base and
    type. Asking for the CODE pair first makes its data half the answer
    to the same window's data request, so GEN costs two LDT entries, not
    three.
  - It is additive: a caller with bit 1 clear (Skies passes BL = 0) gets
    the same answer as before. `abilock --update`, no bump.
- **Decision 6: `mx_cpmove` cuts its pieces where no descriptor crosses**
  (`mx_pcut`, `mx_xcut`, `mx_dkb`).
  - The units are 512 bytes. A part starts on one (SPEC.md 20.12.2), so
    two parts that share a KB still get a cut between them.
  - The cut is the largest one at 64KB or under that no LDT descriptor
    based in the block runs across. Failing that, it is the smallest
    cut past 64KB that none runs across.
  - A run, a pair or a claim's selector still cuts every 64KB. A block
    of 64KB or less is still one step and scans nothing.
  - It replaces 14.37's rule, *"any descriptor crosses a 64KB piece
    boundary -> the whole block is one step"*. GEN starts 58KB into
    Pixelstein's block and ends 108KB in, so under that rule all 138KB
    would have moved in one step with interrupts off.
  - The scan runs with interrupts ON. No other task runs under
    `mx_alloc`'s `[sch_lock]`, and no ISR makes a descriptor.
- **S6, Pixelstein (`apps/pixelstein/`).**
  - The loader writes `PXH_GEN` and `PXH_BT` as paragraph OFFSETS past
    the program (`pxl_off`: `op_seg(part) - op_seg(0)`, 0 = refused).
  - The program says `OS88_HIGH | OS88_HIGHMEM` in its own header. Its
    entry proc (`px_hconv`) turns the offsets into segments:
    - It asks OSAPI_MEM_SUBSEG for a selector over exactly the part, with
      BL bit 1 and the part's length: GEN's code pair first, then GEN's
      data, then BT's data.
    - On CF = 1 it uses `DS + offset`. That is op_seg's own answer, so on
      kern_big and kern_small, and on kern_mega for a low region, every
      word is the one the loader used to write.
    - `[px_gencs]` is the segment for far calls. It is GEN's code
      selector; our CS when GEN was refused; and `OS88_CODESEL` of the
      sum on kern_mega's identity map.
  - **Items 47 and 50 of 10.3, the far calls through a data segment.**
    - The segment halves of `px_qcur*`, `px_drvp` and `px_drv2p` take
      `[px_gencs]`. `px_hsegs` sets them and is shared by `px_gen_init`
      and the relocation proc. `px_bseg` and `PXG_QTEX` stay data.
    - With GEN refused, the bodies are far-called through CS, where they
      used to be far-called through DS. In real mode that is the same
      word; on kern_mega it was a fixup per column.
    - `px_reloc` RE-DERIVES every word from the new segment and the kept
      offsets (`px_hrel`, which uses only the sum). It used to add a
      paragraph delta, which on kern_mega pointed every code alias at a
      different selector. Above 1MB it is never called: a pool block
      moves with no relocation proc.
- **S7, the claims.** Every claim the loader makes for the program, and
  every claim the program makes itself, can now be a pool selector.
  - The loader says `OS88_HIGHMEM`. Its claims are made under the
    instance SLOT, which the re-home keeps (`mega_slotof`'s `[ld_base]`
    arm). So they belong to the program from the start, and `mx_relinst`
    frees them at its teardown. The sprite set (60KB) is a pool claim
    directly. The 28KB reserve held across `op_load` also goes to the
    pool and is freed after it.
  - **`op_fetch` has a high arm** (`apps/os88partsbody.inc`).
    - It is opt-in with `%define OP_WANT_HIFETCH`, so every other parts
      package is byte-identical.
    - The DMA-door fetch still reads, expands and shrinks the part in
      conventional memory. It must, because that is paragraph
      arithmetic.
    - Then `op_hilift` claims `len` with OSAPI_MEM_CLAIM. When the answer
      is an LDT selector, it copies the part up and frees the
      conventional claim.
    - The cost is a brief second copy of the part (10KB, then 37KB). In
      real mode it returns at the CPU_INFO test.
  - The program's 16KB shadow is a pool claim, through the program's own
    OS88_HIGHMEM.
  - The census against `OS88_HIGHMEM`'s contract was a grep of every
    Pixelstein source for four things: a segment register moved into a
    general register, an add or sub on a `*seg` word, an `int`, and a
    far call through a claim.
    - The shadow, the sprite set, the masters and the level stream are
      only ever loaded into DS or ES.
    - No DMA, no BIOS call and no far call touches any of them.
    - The one sum was `px_gen_init`'s `mov ax, ds`, and it now goes
      through `[px_gencs]`.
    - **Nothing had to stay conventional.**

#### 14.48.2 What it cost

Measured from `make mega`'s `os88megaseg` lines and the maps, at one commit.

| kernel / package | section | bytes | what |
|---|---|---:|---|
| kern_mega | `.cold` | **+11** | 41,730 -> 41,741: `ld_hisi`'s entry and step 8b's arm |
| | `.mega` | **+232** | 14,252 -> 14,484: `mega_imghuge` 151; the piece cut +45 (`mx_pcut`, `mx_xcut`, `mx_dkb` and `mx_dunit` are 131, less the 86 of straddle test that `mx_cpmove` and `mx_desc` no longer carry); SUBSEG's length +30; `mega_pkghif` +9; `mega_rgnsubfree` -3 |
| | `.text` + `.bss` | +0 | the megaseg overhead stays 5,935 |
| kern_big | all | **+0** | `build/kernel.bin` and `kernel.sys` are `cmp`-identical before and after, at one commit |
| kern_small | all | +0 | from the diff: every kernel line is inside `%ifdef KERN_MEGA` |
| PXGAME (part 0) | image + bss | **+105** | 33,815 + 24,448 -> 33,913 + 24,455. Trimmed to land on **58,368 exactly** (114 sectors), so the keep stays 141,312 bytes on every kernel |
| PXSTEIN (the loader) | image | +86 | 2,901 -> 2,987: `op_hilift` and `pxl_off`. The loader is freed at the re-home |

#### 14.48.3 Decision 6: how long a huge region is deaf when it moves

The cuts were read at a Bochs breakpoint on `mx_pcut.set` during MEGACP's
compaction (BX = the piece's first unit, DX = its cut).

- **Pixelstein's block moved in three pieces: 58KB, 50KB and 30KB.**
  Those are the program, the scratch part and the byte textures, each
  with its own descriptors.
- Every other block moved as one piece of its own size: claims of 10,
  37, 60 and 16KB, and MEGACP's 60KB claims.

On a 286, the worst interrupts-off step is therefore one 58KB copy plus
its LDT pass:

- **~15 ms at the Victory's measured 0.257 ms per KB above 1MB**
  (KERN-MEGA-DOS-PLAN 2.4), or ~9 ms on a 16MHz 286 at 4 clocks a word.
- **~35 ms** is what 14.37's rule would have made it, the whole 138KB.
- 14.15's 64KB worst case is ~16 ms. So no block in the tree now moves
  with interrupts off for longer than a 64KB claim does.

These figures are ARITHMETIC from a measured rate, not timings taken here.
Bochs and 86Box are not cycle-accurate.

#### 14.48.4 What was measured, and on what

**Bochs 2.7, kern_mega (`build/mega.img`, with C: holding the packages):**

- **16MB, `tests/megarehome.py`.** MEGARUN started DOS.O88, SCRIBE,
  SKIES, WORD and PXSTEIN.
  - Pixelstein: `I_SPTR` 28E4, `I_SIZE` 58,368, and **one 138KB block
    owned 86h**, its pair's limit E3FF.
  - Inside the block sit exactly GEN's code/data pair (base +59,392,
    limit C7FF) and BT's data selector (+110,592, limit 77FF).
  - Four `A6h` claims are in the pool, and there is no conventional
    claim of 32KB.
  - The other four programs read as in 14.44.
- **2MB, so that MEGACP can fragment the pool.** SCRIBE then PXSTEIN were
  started.
  1. Played: Space, Up, Left, three shots (ammo 8 -> 5). Then **F**: the
     Mode X bracket, with the textured walls and the weapon drawn. Esc
     went back to the window.
  2. Scribe was closed, leaving a 63KB hole under Pixelstein's claims.
     MEGACP was run from C:\ZCP: `MEGACP: n 07 freed 03 big 2844 ok`.
  3. **The block moved 170K -> 168K** in the three pieces above. Every
     descriptor was rebased by the same 2KB (the pair 16A800 -> 16A000,
     GEN 179400 -> 178C00, BT 185C00 -> 185400), and the four claims
     moved down with it.
  4. Raised and un-paused, **it played again**: turns, shots (ammo 4 -> 0,
     the knife drawn), and the textured view both in the window and
     fullscreen.
- **16MB, PXSTEIN closed by its close box.** **`mx_tab` holds no block of
  its slot, and every LDT descriptor it had (0x506-0x50E) is free.**
- No `kern_mega FAULT` appeared in any run.

**86Box `286-mega`** (mr286, a 16MHz 286, 4MB: the 286 reset path), with
PXSTEIN on a 1.44MB floppy in B:. Tables were read with `os88box memr`.

- `I_SPTR` 284C, the 138KB block owned 81h, the pair's limit E3FF.
  GEN's pair 2854/285C and BT's 2864 are inside it, and the four claims
  (`A1h`) are in the pool.
- It played windowed and fullscreen (the textured Mode X view, with a
  door and a banner). Esc went back to the window.
- After closing: no block of its slot and no LDT descriptor left.
- `faults` shows the expected `mega_svc_lo.r286` resets and **two families
  of completed CS writes** (see 14.48.5).
- The compaction was not exercised there. To fragment a 4MB pool, MEGACP
  would need more than its share of `mx_tab`'s 24 entries.

**kern_big (MartyPC)** - the package change reaches every kernel:

- `tests/pixelstein.py` is **ok**: every measured cell of SPEC.md 97.15's
  table is within 5% (48 of 48; Textured Low res 64x80, scene A finished,
  117.5 ms, 8.51 fps). So there is **no XT slow-down**; nothing on the
  frame path changed.
- `tests/pxsmove.py` **passes**. The carve moves under the kern_big
  compactor, `px_reloc`'s re-derivation gives the same words the delta
  did, and pose A composes the same shadow byte for byte.
- `tests/pxs256.py` fails its check (c), *a dog 1.5 tiles ahead is a
  candidate*. **It fails it identically with the package from before this
  change** (md5 2f5214..., rebuilt and run at the same commit), so it
  predates this work.

**The row** is `megarehome`, with Pixelstein added (99 s measured, 120
declared). It goes red with the pre-14.48 PXSTEIN.O88 in place: `FAIL:
Pixelstein 3D I_SPTR 7C40 - a conventional segment`.

#### 14.48.5 What is left, and for the owner

- **A huge keep's pair covers the program's image + bss, not 64KB.**
  That keeps the pair out of GEN's piece. Pixelstein reads nothing of its
  own DS past its bss: no limit fault appeared in any run. A future
  re-homed program over 64KB must meet the same condition, and SPEC.md
  20.12.10.9 states it.
- **Pixelstein's CS writes are untouched, and there are many.** 86Box's
  `faults` over ~20 s of play counted:
  - ~33,000 completed `[Segment can't write]` in `px_compose` (`.tchg`,
    `.tnofloor`, `.b1`), its per-column self-modifying stores through CS;
  - ~770 in the GEN driver's sprite post (`mov byte [cs:bp], 0C3h` and
    its restore).

  A LOW Pixelstein makes the same writes on kern_mega, since its CS is an
  LDT alias there too (item 87), so this is not a regression. They are the
  next Pixelstein item for a census (`make megatrap`). The driver's two
  could become a protected-mode template copied in at `px_gen_init`, at
  no cost to the XT. On this same path, this change removes the
  per-column far-call fixups through DS.
- **D4 is still deferred.** For the moment of the launch, the 138KB carve
  and the lazy parts' fetches still need their room in conventional
  memory.
- **The kern_mega LOW path of `px_reloc` is untested on kern_mega.** That
  is the case where the pool is full and the region is a conventional
  claim that kern_big's compactor moves. `pxsmove` exercises the same sum
  path, but on kern_big.
- **`pxs256`'s check (c) fails at the base** and needs its own
  investigation.

### 14.49 The pool's open follow-ups: the split regrow, a bigger table, the 32KB windows moved in pieces, the copy's lock (2026-10-21)

14.37 and 14.41 left four things open. All four are built here, on
`protected-mode` at e45f701a (14.47, 14.48 and 14.50 merged in). Every
kernel line is kern_mega's.

**1. A regrow is refused only when the free KB are short.** 14.37's
`[R][B...][free F]`, with `F >= new - old` but `F < new`, was refused,
because no block of the new size could exist and the KB above R were
taken. `mega_claimregrowf` now tries one more thing before it refuses:
`.hpack` packs the pool DOWN (`mx_compactf`), then packs every movable
block above the claim UP against the first pinned block above it
(`mx_packupf`, new), then takes the KB above the claim (`.hroom`).

- `mx_packupf` is the descending half of a compaction, which kern_big's
  compactor has (SPEC.md 66.4.1). It walks the blocks above a floor in
  DESCENDING order. A ceiling starts at the pool's top; a pinned block
  lowers it to its own start; a movable block rises to end at it.
- **`mx_cpmove` moves UP now, overlapping or not.** For a rise it takes the
  pieces from the top down (`mx_pcutr`, the mirror of 14.48's `mx_pcut`),
  and each piece's 32KB chunks from the top down, each copied from its end
  (`mx_lcopyb`: `std` / `rep movsw`). So a copy still only overwrites bytes
  of pieces already moved, or of the one it is, and 14.37's rule that an
  ISR finds each piece whole holds for a rise too.
- **A claim of 64KB or less** (`.one`) now grows in place when the KB above
  it are free, before it is copied into a new block. When no block of the
  new size can be had, it gets the same `.hpack` before it refuses.

**2. `mx_tab` has 96 entries, and none of them is in the kernel segment.**
It moved from `.text` into `.lowbss` in LOW_SEG, beside `mem_tab`, which is
the project's standing place for kernel tables (docs/KERNEL-MEMORY.md,
"Moving data out of the segment"). Every access reads it through `SS:`.
There are 130 of them, in `mega.inc`, `hiber.inc` and `megados.inc`, each a
one-byte prefix. `tools/os88ovlchk.py` already refuses a `.lowbss` reference
that names neither SS nor ES. `mega_xminit` zeroes the table, because
`.lowbss` is not zero at boot.

- **Why LOW_SEG and not above 1MB.** SS is LOW_SEG everywhere the kernel
  runs (SPEC.md 2.1). That covers the tasks, every private ISR stack,
  `DOS.O88`'s calls (it switches back to the UI task's stack before it
  calls the kernel) and the HIBER.DRV module. So the change is a prefix and
  nothing else: no register to juggle, and no new selector. And the table
  stays CONVENTIONAL memory, which the park's image, the hibernate's image,
  the durability save and the resume stub already carry.
- Above 1MB, the table would be the first piece of persistent kernel state
  outside those mechanisms. The cheapest place there is the free 2KB
  between the IDT's end (124800h) and `.cold` (125000h). It would need a
  selector, about 28 routines rewritten to keep ES on it, run 0 of the
  hibernate extended, a region added to the park's sum, and the 82KB
  low-tables claim of 14.17 grown by a KB. That is the alternative the
  owner can take, for 768 conventional bytes (decision 1).
- **The size is measured.** Under Bochs (16MB, SB16, SOUND.DRV) Word,
  Scribe, the Video Player, Paint, Sheet, Tracker, Note Pad, TexPad and the
  Calculator, all open at once, took **35 entries**: 10 regions, 17 claims
  (Sheet's six among them), 2 driver blocks and 7 kernel blocks. That was
  read with MEGAHG's table phase on a 128-entry build: 93 more claims fit.
  So 24 entries ran out on this session. A full INST_MAX of twelve
  programs is about 60 by the same mix. 96 entries give 2.7 times the
  measured session.
- **The park's header is derived from the table.** `MPK_RMAX` and
  `MPK_CMAX` grow with `MX_NBLK`, so their lists outgrew the fixed 0x700
  bank. `MPK_SAVE`, `MPK_STACK` and `MPK_HDR` are now computed from the
  lists. At 24 entries they come out at the old +700, +800 and +800. At 96
  they are +940, +A40 and **+C00**, so a park block or a hibernate's extended
  section is 1KB longer: `parked 0050K` where 14.45 read `004FK`. Running
  out of entries is a clean refusal, as before: `mx_alloc` answers AL = 1.
- `tests/megarehome.py` read the table at `0x600 + offset`, which is
  `.text`'s address. It now reads LOW_SEG's base off the machine (SS's
  descriptor, `sreg`). The constant beside it was stale (0EC0h against the
  ladder's 0E60h), so its `mem_tab` read had been landing in the wrong
  place without saying so. That constant is now only the fallback.

**3. The 32KB-window claims move in 64KB pieces.** 14.48's `mx_pcut` cuts
each piece where no LDT descriptor crosses the cut. In a claim with a window
every 32KB, some window crosses every cut, so the whole claim had to move in
one interrupts-off step. That was ~80 ms for the Video Player's 512KB ring
on a 12 MHz 286.

- **The invariant: between two pieces, nothing can READ a half-moved
  window.** Two things make that true:
  - no task runs, because `mx_alloc` holds `[sch_lock]` for the whole move;
  - the only code of a package that runs from an interrupt is its
    `FSXF_RATE` hook, and `mx_cpmove` holds it off. It raises `[sch_rbusy]`
    from the first piece to the last. `sch_rhook` already skips while that
    byte is up, and the skipped periods stay owed in `[sch_rpend]`, so the
    next call is handed all of them.
- With that, `mx_pcut` and `mx_pcutr` cut such a claim every 64KB anyway
  (`MX_CPHOLD`, set by `mx_cpflags`). Each window is rebased with the piece
  its base is in, and every window is whole again by the last piece.
- **Where it still takes one step.** A package's sample ISR (SPEC.md 34.11)
  cannot be held, so with `[spk_seg]` open the claim moves in one step, as
  14.41 shipped it. So does a DRIVER's 32KB-window claim (owner 0C0h |
  row), whose own ISRs may read it.
- **What the hook sees: a delay and no tear.** A held hook is late by the
  move, and it is paid the periods on its next call. The player's
  `vp_hook` already treats a late call that way (`vp_gap`, `vp_owed`).
  Everything else keeps its interrupts: the pointer, the keys, the tick and
  the card's IRQ.
- **When a ring can move mid-play at all.** A play runs inside an
  `OSAPI_FSX_RUN` bracket, and a bracket freezes every task but its own, its
  kept worker and `TF_SERVICE` workers (SPEC.md 53.2). So a compaction
  during a play can only come from the player itself, or from a service
  worker's claim. Neither happens on the ring-play path. "A forced
  compaction mid-play" therefore cannot be arranged from another package.
  MEGAW32 (below) is the stand-in: one bracket, with a rate hook reading the
  odd windows of a 32KB-window claim while the bracket's own task compacts.

**4. `mega_lincopy`'s scratch rule is asserted.** The body moved into
`.mega` as `mx_lcopy` / `mx_lcopyb`. `.text` keeps a four-byte near door
(`mega_lincopy`) and a six-byte far one (`mega_lincopyf`), so 30 bytes leave
the segment.

- Under `MEGATRAP=1`, `mx_lcchk` checks that the caller holds IF = 0 or
  `[sch_lock]`. If not, it writes `L cccc:iiii wwww` on E9: the far
  caller's CS:IP and the word above. The copy still goes ahead.
- The same knob also logs every move as `M kkkk pp ff`: the block's KB, the
  interrupts-off steps, and the flags (1 a rise, 2 the hook held).
- **In every census run here, not one `L` line came up**: MEGAHG, MEGAW32,
  and the nine-program session. So every caller in the tree keeps the rule.
- It is not made structural. The two dwords `[mega_xsrc]` / `[mega_xdst]`
  are written by the CALLER before the call, so a lock taken inside
  `mx_lcopy` could not cover the race the rule is about.

**Tests.** MEGAHG (`tests/megahg`) gains three phases. Each one was red on
the kernel before this change: protected-mode at fff52479, under Bochs at
4MB.

- **U, the split regrow.** It takes R (100KB), then five claims of H above
  it. It then claims every hole the pool has, exactly, off
  `OSAPI_XMEM_WALK` (`hg_holes`), so the pool holds 0 free KB. It frees
  the top two of the five, and regrows R by every free KB, reading back R,
  the three remaining claims and every hole claim. Red on the old kernel:
  `MEGAHG: no 52`, the regrow refused. Green here: `split regrow ok`.
- **W, a 32KB-window claim moved.** It claims X (60KB) and W (512KB, BX =
  8000h) above it, and fills W through its even windows. It then frees X
  and claims every free KB, which moves W down, and reads all of W through
  every window, odd ones included. It passes on both kernels, because it
  checks correctness and not timing.
- **T, the table filled.** It takes 1KB claims until the pool refuses, then
  reads every one back and frees it. It needs at least 48. Red on the old
  kernel: `table 0011 claims` and `no 70`. Green here: **`table 0059
  claims, entries free 00`**, 89 with 7 entries held by the system.
- `-DHG_UONLY` and `-DHG_TONLY` start the test at a phase, so a kernel the
  split regrow fails on can still show the table.

MEGAW32 (`tests/megaw32`, new; `make build/megaw32.o88`) is item 3's test,
where the invariant bites:

- In its own `FSXF_RATE` bracket at 582 Hz, its hook reads, at every call,
  four words of EVERY window of a 512KB 32KB-window claim. That includes the
  upper half of each odd window, which is exactly what a tear corrupts.
- The bracket's task then frees the 60KB under the claim and claims every
  free KB, so the claim moves.
- The verdict needs **errors 0** and the hook to have run after the move.
  The toast says `MEGAW32 ok gGGGG eEEEE`, where G is the longest gap in
  periods.

**Measured:**

| run | machine | result |
|---|---|---|
| MEGAHG, every phase | Bochs, 4MB, SB16 | `MEGAHG: ok`; the census kernel's `M 0200 08 02` - the 512KB 32KB-window claim in **8 steps, the hook held**, where it was one |
| MEGAHG | 86Box `286-mega` (MR BIOS 286, 16 MHz, 4MB, SB16), double-clicked off B: | `MEGAHG ok` on the menu bar; faults only the 286's reflection resets and the five `Segment can't write` at 2764h that 14.37 found predating it |
| MEGAW32 | Bochs | ok, gap 1. Bochs's `rep movsw` speed-up (IPS ~2.5G) moves 512KB inside one period, so Bochs cannot show the hold |
| MEGAW32 | 86Box `286-mega` | **`ok g000A e0000`**: the hook held for 10 periods (17 ms) across the move, no torn read |
| MEGAW32, the hold taken out (a throwaway build without the `inc`/`dec` of `[sch_rbusy]`) | 86Box `286-mega` | **`no 05`**: the hook read torn odd windows between pieces. The break is real, and the hold is what closes it |
| U and T on the kernel before | Bochs | `no 52`; `no 70`, 17 claims |
| `tests/megarehome.py` | Bochs | passes: DOS, Scribe, Skies, Word and Pixelstein 3D regions and claims above 1MB, read through the moved table |
| a DOS trip (KDHELLO -DKDHOLD), a menu Hibernate, Resume, a trip from the resumed session | Bochs, booted off C: (`--bootc --noa --csys`) | `parked 0050K at 14CC00`, `unparked`, exit 042; `resumed` with the DOS window back; a second trip, exit 042 |
| the Video Player, in place, on 14.41's clip (`testsrc2` 320x240 15 fps 20 s, `--preset vga --profile floppy`, 429,568 bytes) | Bochs, 16MB | `XMS in place (PM)`, Drew 300/300, stalls 0, Late 0, 365 ticks of 364: 14.41's row to the frame |

**Cost, measured** (`make mega`'s `kernsize[mega]` and `os88megaseg`, this
tree against e45f701a built from an archive of it):

| kern_mega | e45f701a | this | change |
|---|---:|---:|---|
| `.text` | 47,368 | 47,146 | **-222** (the table's 192, the copy's 30) |
| `.bss` | 5,336 | 5,336 | 0 |
| `.lowbss` | 6,487 | 7,255 | **+768** (96 entries x 8) |
| `.cold` | 42,177 | 42,272 | +95 (`.hpack`, the `.one` grow in place, `SS:` prefixes) |
| `.mega` | 14,692 | 15,146 | +454 (`mx_packupf`, `mx_pcutr`, the rise, `mx_cpflags`, `mx_lcopy`, prefixes) |
| `.mega`'s scratch | 914 | 1,484 | +570 (the park's lists at 96 entries; boot-only, given back) |
| segment overhead | 4,555 | 4,323 | **-232**, and `tests/megaseg.txt` lowered to 4,323 |
| resident once above 1MB | 65,024 | 66,048 | +1,024 (the LOW rung twice, for 768 bytes) |

- kern_big and kern_small are **unchanged**, read from the diff. Every
  kernel hunk is inside `%ifdef KERN_MEGA`: `mega.inc` from its 63rd line,
  `megados.inc` whole, and `hiber.inc`'s hunks all inside its kern_mega
  block (lines 3412-4216). kern_big's `kernsize` sections are the same before
  and after.
- `abilock`: `PKG_FMT` 10 and `DRV_VER` 11, unchanged. The SDK's
  `OSAPI_MEM_HUGE` note was clarified (how a 32KB-window claim moves).

**Rows.** None of this is in `tests/suite.py`. MEGAHG and MEGAW32 are run
by hand, like MEGACP and MEGAHI: `OS88_BOCHS_MEGS=4 python3
tools/os88bochs.py sweep --sb --cfg 1 build/megahg.o88 build/megaw32.o88`,
and add `--build build/megatrap` for the `M` and `L` lines. The rows that
can see the change are `megarehome`, `megatrap0`, `megaflt` and `pxsmega`
(Bochs, soak), the fast tier's `abilock`, `cswrite` and `megagrid`, and
`make mega`'s `os88megaseg` and `os88ovlchk`.

**Not done:**

- **The durability option's trip** (14.43, *Save the session to disk*) was
  not run. It shares `hbm_xplan` and the resume with the hibernate that was.
- **The Video Player with a compaction mid-play** cannot be arranged (item
  3), so MEGAW32 stands in for it.
- **MX_KOWN blocks stay pinned**, as the brief asked. 14.47 has since put
  the action modules in the pool as MX_KOWN, so whoever unpins them gets
  `mx_packupf` for free: it moves whatever `mx_canmove` says moves.
- **The Victory 286** has not run any of it.

**Decisions for the owner to look back over:**

1. **The table is 768 bytes of conventional memory.** Net of the 192 that
   left `.text`, that is +546 bytes of conventional heap, and two LOW rungs
   at this commit. 64 entries would cost 512 bytes and cross no rung, but
   would be tight against twelve programs. Above 1MB, in the gap after the
   IDT, it would cost no conventional byte, for the work item 2 lists.
2. **A 32KB-window claim's piecewise move rests on holding the rate hook.**
   The hook is late by the whole move (17 ms for 512KB on the mr286), where
   it used to run on time behind an interrupts-off step of the same length.
   Every other interrupt now runs through the move.
3. **A small pool claim's grow now prefers the KB above it**, in place, over
   a copy into a new block. Fewer copies; same contract.
4. **The copy's lock is asserted, not enforced**: only `MEGATRAP=1` checks
   it, at no shipped byte.
5. **The park's header is 3KB where it was 2KB**: one more KB per park block
   and per hibernate's extended section.

### 14.50 PIXELSTEIN 3D's writes through CS, and `pxs256`'s check (c) (2026-10-21)

14.48.5's next census item, and its last line. Built on `protected-mode` at
fff52479. What now holds:

- **A scripted play of Pixelstein on kern_mega takes NO #GP fixup.** The
  same play took **173,932** before: the timedemo, a game in the window
  and in the Mode X bracket, walking, turning and shooting.
- **The 4.77 MHz XT is not slower.** `tests/pixelstein.py` reads 48 of 48
  cells within 5% of SPEC.md 97.15. Flat and Wire are identical to the
  tenth of a millisecond. The Textured cells are up to 0.2 ms faster, bar
  one draw 0.2 ms slower.
- **Part 0 is 9 bytes SMALLER** (58,368 -> 58,359), so the keep stays
  141,312 bytes on every kernel. No kernel byte moved.
- **`pxs256`'s check (c) was the TEST, not the game.** It counted a frame
  that was already in flight when it poked the dog.

#### 14.50.1 The census before

`tests/pxsmega.py` (below) under Bochs 2.7 at 16MB, `make megatrap`'s
kern_mega, with the pre-change PXSTEIN.O88 in B: (`--pkg`). Every line is a
completed #GP (`mega_gpw`):

| step | traps |
|---|---:|
| launch (double-click) | 0 |
| T, the timedemo (fullscreen, 81 frames) | 24,188 |
| Esc out of the bracket | 0 |
| Space, a new game in the window | 586 |
| walk, Up held 4 s | 32,694 |
| turn, Left 3 s and Right 2 s | 52,302 |
| shoot, Ctrl x3 | 1,872 |
| F, into the bracket | 1,172 |
| walk there, Up 3 s | 29,372 |
| turn there, Left 3 s | 27,032 |
| shoot there, Ctrl x3 | 4,128 |
| Esc, back to the window | 586 |
| **total** | **173,932** |

`tools/megatraps.py` names fifteen sites, and only two families:

- **`px_compose`'s stores into its own per-column memory**, through CS.
  pxcomp.inc's header says why: DS is the destination for the whole
  compose, so "everything of this package's own is read through CS".
  - `px_cdi` is stored on every column.
  - `px_ltop`, `px_lbot`, `px_lu`, `px_cwrote`, `px_lh`, `px_lmat`,
    `px_lside` and `px_qp` are stored on every changed Textured column,
    18,512 each here.
  - `px_r0`/`px_r1` are stored when the dirty rows widen.
- **The column driver's sprite-post patch** in part 1, 1,537 each at
  +011D and +0125. That is `mov byte [cs:bp], 0C3h` and its restore, one
  pair a post. The weapon is drawn as posts too, so every shot pays it.

The other two lines of the boot are the kernel's own `mega_strcal` (14.30's
calibration), before the game starts.

#### 14.50.2 What was built

All of it is in `apps/pixelstein/`. Nothing in `kernel/` changed.

- **`px_compose` stores through ES, which it sets to our DS.** That is
  `push ds / pop es`, and a `cld`, once a frame.
  - All 32 stores into our words are `[es:` instead of `[cs:`. It is the
    same prefix byte and the same clocks, so on the 8088 the Flat and Wire
    composes are unchanged.
  - The READS stay `[cs:`, which is legal on a code selector.
  - A queued wall's four words go into part 1 through four `stosw`, with
    ES swapped in for them: `mov dx, es / mov es, [cs:px_hand + PXH_GEN]
    / ... / mov es, dx`. DX is free there. `stosw` is 1 byte against the
    3 to 6 of the store it replaces, which pays for the swap on the 8088.
    The first spelling, `push es`/`pop es`, read 0.1 ms dearer on most
    Textured cells; the two moves are 22 cycles cheaper.
  - `.done` takes ES = part 1 for `PXG_QEND` and the far call.
    `px_fade_draw` sets ES the same way for its one `px_cdi` store.
- **`px_drvpm_post`, 29 bytes: the driver's `.post` for a part whose code
  is not its data.**
  - It makes the same two patch stores through part 1's DATA segment, with
    DS borrowed for each one. The scaler wants DS = the destination and
    ES = the sprite set.
  - The segment is `PXG_QDS`, the last word of `PXG_DRVMAX`'s 96 bytes, so
    nothing after `PXG_QTEX` moves. `px_hsegs` writes it, which is what
    keeps it right after `px_reloc`.
  - `px_gen_init` copies `px_drv_tpl` as before. Then, only when
    `[px_gencs]` differs from part 1's data segment, it lays the tail over
    `.post`. It is a tail and not a second driver, for the bytes: its one
    jump is assembled for the offset it lands at.
  - No real-mode kernel's code segment differs from its data, so the XT
    runs `px_drv_tpl` byte for byte. `tests/pxsscale.py` holds that.
- **The bytes are paid back in part 0** (below), so the program still
  lands inside 114 sectors:
  - `px_force_all` is one `lodsb`/`rep stosb` loop over its eight arrays,
    -42 net with its table. It now fills `px_lside` too, with 0FFh, which
    no skip reads once `px_lmat` mismatches. pxgame.asm asserts that the
    eight are one run.
  - The five `px_r0`/`px_r1` byte pairs are one word store each, -4 each.
    pxgame.asm asserts that the two are adjacent.
- **The ratchet.** `tests/unit/t_cswrite.py` now walks `apps/pixelstein/`
  as well. `tests/cswrites.txt` registers what is left:
  - `pxgen.inc`, 3 writes: `px_drv_tpl`'s two patch stores, which run only
    where CS is the data, and PXPROBE's ladder counter, an instrument build
    nothing ships;
  - `pxwin.inc`, 2 pairs: `ES = KERNEL_SEG` to READ the window record.
- SPEC.md 97.3 and 97.13 say the same in the contract.

#### 14.50.3 What it cost

Measured from the two maps of `pxgame.asm`, at fff52479 and at this change,
routine by routine.

| routine (part 0) | bytes | what |
|---|---:|---|
| `px_drvpm_post` | +29 | the protected-mode tail |
| `px_gen_init` | +18 | the test and the tail's copy |
| `px_hsegs` | +5 | `PXG_QDS` |
| `px_fa_fill` | +8 | `px_force_all`'s fills |
| `px_force_all` | -50 | seven blocks into one loop |
| `px_compose` | -5 | ES once a frame, the four `stosw` and the swap, `.done`'s ES, the dirty-row word |
| `px_fade_draw` | -2 | ES (+2), the dirty-row word (-4) |
| `px_frame_begin`, `px_card_draw`, `px_map_draw` | -4 each | the dirty-row word |
| **part 0** | **-9** | 58,368 -> 58,359 bytes, image + bss; 114 sectors either way |
| kern_big, kern_small, kern_mega | +0 | no kernel file touched |

#### 14.50.4 What was measured, and on what

- **Bochs 2.7, kern_mega with MEGATRAP=1, 16MB** (Pixelstein HIGH, 14.48),
  `tests/pxsmega.py`, the steps above. **There are 0 trap lines in all
  twelve**, where the same play took 173,932. The shots show the
  timedemo's card (81 frames, 61 fps), the game in the window, the
  textured Mode X view with the weapon and ammo 8 -> 2 after the two
  volleys, and the window again.
  - With one `[es:` store put back to `[cs:` (`.tchg`'s `px_ltop`) and the
    tail never laid, the same run is **red in ten of its twelve steps**:
    22,937 lines, on the compose store and the driver's +011D/+0125.
- **86Box `286-mega`** (mr286, a 16MHz 286, 4MB: the 286 reset path),
  with PXSTEIN.O88 on a 1.44MB floppy in B:.
  - It played in the window and in the Mode X bracket: textured walls, a
    door, three shots (ammo 8 -> 5), and Esc back to the window.
  - `faults --all` shows **two `[Segment can't write]`, both
    `mega_strcal` at boot**. 14.48 counted ~33,000 in `px_compose` and
    ~770 in the driver over a similar play. The rest are the expected
    `mega_svc_lo.r286` resets.
- **MartyPC, kern_big, the CGA 5150** (`tests/pixelstein.py`), before and
  after on the same tree. Frame time / draw time, in ms:

| cell | before | after |
|---|---|---|
| Textured Low res 64 (the default), A full repaint | 113.9 / 111.3 | 113.8 / 111.3 |
| Textured Low res 64, A turn | 103.9 / 101.3 | 103.8 / 101.3 |
| Textured Low res 64, C full repaint | 133.0 / 130.4 | 132.9 / 130.4 |
| Textured Low res 48, B turn | 81.1 / 78.4 | 81.2 / 78.6 |
| Textured Full 64, A full repaint | 180.5 / 177.4 | 180.3 / 177.3 |
| Flat Low res 64, A full repaint | 104.9 / 102.3 | 104.9 / 102.3 |
| Wire Low res 64, A turn | 96.3 / 93.7 | 96.3 / 93.7 |
| Textured Low res 64, A3 finished (three sprites) | 151.9 / 140.2 | 151.8 / 140.2 |
| Flat Low res 64, C finished | 126.8 / 119.7 | 128.1 / 119.7 |

  - All 48 cells are within 5% of SPEC.md 97.15. The worst is 1.0%, the
    last row, which is SPEC.md 97.15's own value for that cell.
  - That row's DRAW is identical. The 1.3 ms is outside the draw: the sim
    runs in a "finished" cell, and its phase against the tick moves the
    frame by about that much either way. Textured Low res 48 A3 finished
    moved the other way, 125.4 -> 124.1, with its draw 115.6 -> 115.5.

#### 14.50.5 `pxs256`'s check (c): the test counted the wrong frame

The check pokes a dog 1.5 tiles ahead while the guest is paused, marks
`px_frames`, runs, waits for ONE frame and reads the candidate list.

- **The frame it waits for can be the one in flight at the poke.** Paused
  after that frame's `px_spr_gather`, the frame ends with the candidates
  gathered BEFORE the dog stood there. The frame the poke's force owes
  follows at once, and that one has the dog.
- **It was shown on purpose.** A breakpoint at `px_compose`, which is past
  the gather, made the pause. Then at a breakpoint on `px_frame_end` the
  candidate list is `[]`, and one frame later it is `[(31, 16, 3)]`.
- **So it is intermittent, and load makes it likelier.** The host polls
  `px_frames` every 0.2 s, and the check only fails when that poll lands
  between the two frames. That is likelier when the guest runs slower
  against the host, which is a loaded box, and 14.48 saw it fail in its
  soak. Alone it passed 5 of 5 here at the base. "It fails identically
  with the old package" was the race both times.
- **The fix is in the test.** After the frame, it waits FOR the dog among
  the candidates, in guest time with a 60 s budget. A dog that never
  becomes a candidate still fails the check, at the budget.

#### 14.50.6 Rows

- **`pxsmega`** (new, soak, Bochs) is the play census above. It reports
  each step and fails on any trap line or FAULT. It took 163 s with the
  private tree already built, and declares 200. `--pkg F.O88` censuses another package on the same kernel
  and only reports; that is how the "before" column was taken.
- On this tree, MartyPC, QEMU and the host:
  `os88test.py soak -k 'pxs*' -k 'pixelstein*' -k 't_pxs*'` is **31 of 31
  ok** (1,232 s, two MartyPC lanes): `pixelstein-herc`, `-vga`, `-win`,
  `-c160`, `pxssim`, `pxssim-herc`, `pxsauto`, `pxsact`, `pxsmove`,
  `pxsstate`, `pxshud`, `pxshud-vga`, `pxswin`, `pxswin-qemu`,
  `pxswin-price`, `pxsmd`, **`pxs256`**, `pxsscale`, `pxs160`, `pxsperf`,
  `pxsfsx`, `pxsshots`, `pxsbench`, `pxs-gen`, `pxs-level`, `t_pxsmap`,
  `pxsdisk`, `t_pxsart`, `t_pxsscale`, and PiXEL's `pxsave`/`pxsavehost`,
  which the glob also matches. `pixelstein` itself is the before/after
  pair in 14.50.4, ok both times.
- Pixelstein is not added to `tests/megatrap0.py`. Its LAUNCH was already
  0 traps; its cost was in the frames, which is what `pxsmega` plays.

#### 14.50.7 What is left, and for the owner

- **The LOW path is still untested on kern_mega** (14.48.5): the pool
  full, and the region a conventional claim that the compactor moves. The
  tail's `PXG_QDS` follows a move through `px_hsegs`, as `PXG_QTEX` does,
  but nothing here has moved a low Pixelstein on kern_mega.
- **What the traps cost a 286 is not measured.** Bochs and 86Box are not
  cycle-accurate, so "33,000 in 20 s" is a count, not a time. The Victory
  has not run this.
- **`mega_strcal`'s two boot lines** are the kernel's calibration, which
  `megatrap0` allows as "(boot)". They are not Pixelstein's.

### 14.51 The owner's answers to the round's questions (2026-10-21)

The owner answered the questions the 14.46-14.50 round ended on. Recorded
here as decisions, so the next round does not ask them again.

- **The pool table's 768 bytes** (14.49, `mx_tab` at 96 entries in
  `.lowbss`) - it is kern_mega's low segment only, and that is accepted.
  **Moving it above 1MB is DEFERRED**; 14.49's costing of that move (a new
  selector, ~28 routines through ES, the hibernate's first run, the park's
  checksum) stands as the starting point.
- **Disable the mouse** should not claim a mouse exists and then hold it
  still - fix it if easy. It was: SPEC.md 96.36.8, `kdentry.inc` points 33h at
  the core's `iret` whenever `kern_dos` has no serial mouse to serve, so a
  reset answers AX = 0. Six instructions of `kern_dos`, no core byte (the core
  and its bss are both full to the byte). Soak row `kdnomouse`, red without.
  A PS/2 machine on the shut-down arm now hears "no mouse" too, which is the
  truth: that arm does not drive one.
- **A Video Player forced to move mid-play may pause or stutter.** That is
  acceptable. What is not negotiable is the trigger: **a move happens only
  when pressure forces it** - a claim that finds no run (14.15) or a posted
  request (SPEC.md 66.4.3) - never as housekeeping. 14.49's held rate hook is
  therefore the right trade, and a stutter during one is not a defect.
- **Hibernate's ~22 file calls** (14.46.5) are fine as long as the
  difference is not noticeable; the owner will test it on the iron.
- **`OS88_HIGH` / `OS88_HIGHMEM` stay opt-in, but only behind a gate.** It
  is in the FULL tier and must run quickly. Every package declares both, or
  EXCEPTS itself in a registry with a reason - and *"a damn good reason"*: a
  reason that is only "nobody looked" is not one. 14.52 builds it.
- **Claims that stay in conventional memory** are acceptable when all three
  hold: the claim exists **only during active use**, it is **claimed low**
  (bottom-up), and it is **movable while not in active use**, so idle ones
  compact to the bottom and leave the top of 640K as one free run. Each such
  claim is to be noted. 14.52 audits them.

### 14.52 The OS88_HIGH / OS88_HIGHMEM gate, and the claims that stay conventional (2026-10-21)

14.51's two answers, built. Part A is the gate the owner asked for; part B
is the audit of what still lands in conventional memory on kern_mega.

#### 14.52.1 The gate: `tests/unit/t_megahigh.py`, the full tier

**Every package under `apps/` declares both `OS88_HIGH` and `OS88_HIGHMEM`
in its class byte (+15), or carries a line in `tests/megahigh.txt` per bit
it does not declare, with a reason.** It is `t_movable.py`'s shape and
shares its discovery (a file under `apps/` that emits a header: 67, 60
assembly and 7 C). Two checks, `high` and `highmem`, each needing its own
line. The list turns ONE WAY: a line for a package that declares is a
failure. Registered as the `megahigh` row in the FULL tier, at the owner's
word; host-side, 1.8 s measured.

- **Decided from the source, cross-checked against the artefact.** The
  verdict is OS88_HEADER's fourth argument (or a C shim's `%define
  CC_STACK_CLASS`, which `apps/cc/crt0.asm` writes at +15), evaluated
  against `apps/os88api.inc`'s own equs through a small model of nasm's
  `%ifdef`/`%ifndef`/`%else`/`%endif`, with the defines the SHIPPED recipe
  passes read off the Makefile. Then every current built image is checked
  against it: a `.bin` whose byte 15 disagrees fails the row. The source
  carries the verdict because the C packages are not built by `make`, and a
  gate that skipped what is unbuilt would report on less than the tree; the
  images keep the parse honest (39 were cross-checked on this tree). The
  class byte is in the clear prefix `os88pkg.py` never compresses, so the
  `.bin` and the `.o88` agree on it and nothing is decompressed.
- **Re-homed programs** are checked in the PROGRAM part's header: each
  program part (`word.asm`, `scribe.asm`, `skies.asm`, `dos.asm`,
  `pxgame.asm`) emits its own OS88_HEADER and is a package to the row. A
  **loader** (its own source invokes `OS88_REHOME`) is not asked for
  `OS88_HIGH` - it is freed inside the re-home - and is asked for
  `OS88_HIGHMEM` only when its own source makes a plain claim, which is
  PIXELSTEIN's loader's 60KB sprite set (14.48 S7). The other four
  loaders' claims are all the parts library's DMA door, which the bit does
  not reach. That is a fact about the code, so it lives in the code and not
  in the registry.
- **DOS is the one header that depends on the build**: kern_mega's DOS.O88
  is `kdos/DOS.O88`, part 0 `dosp.bin`, built `-DDOSKPART -DDOS_EXTCORE`
  and `-DDOS_MEGA` under KERN_MEGA (`dos.bin` is the icon pipeline's and
  ships nowhere). `SHIPPED_STEM` names that stem, the row asserts the
  `kdos/DOS.O88` recipe, and a recipe with a KERN_MEGA arm is
  cross-checked against `build/megak/` alone.
- **Broken on purpose, all three red**: OS88_HIGHMEM taken out of HELLO
  (`does not declare OS88_HIGHMEM`); a `highmem` line for CALC (`exempt
  ... and no longer needs to be`); byte 15 of a copy of `paccman.bin`
  cleared under `$OS88_TREE` (`built image disagrees with its source`).

#### 14.52.2 What declares now, and the one line left

At the base: OS88_HIGH 62 of 67 (the five missing are the five loaders),
OS88_HIGHMEM 38. **After: OS88_HIGHMEM 62 of 67** - the four loaders the
row does not ask aside, every package but PiXEL.

- **OS88_HIGHMEM opted in: 23 assembly, 1 C** - Arkanoid, Bounce,
  Calculator, Cyclone, Desk Calendar, FPTEST, Gorillas, Hello, Mines,
  Missile, Pac-Man, Piano, RLAPPLY, Solitaire, TameGram, the Task Manager,
  Timer, WIRE, WireBall, Dr Marco, Font Viewer, Clear Skies, kern_mega's
  DOS.O88 (under `DOS_MEGA` only), and PaccMan.
  - **For twenty-one of them (PaccMan included) the bit is provably INERT.** Bit 6 is read in one
    place, `mega_ownerof`, whose callers are the claim, claim-hi, huge,
    free and claim-find paths, and these packages call none of those cells.
    Their only claims are the parts library's DMA door (Dr Marco's front
    art, PaccMan's overlay), which the bit does not reach, or os88ui's
    save-unders, which are assembled only under `OS88UI_DROP` /
    `OS88UI_MENU` - and none of the twenty-one defines either.
  - **Font Viewer** - `os88type.inc`'s prestyled table (`ty_psseg`, a plain
    claim, loaded into ES only) moves; its face cache is the DMA door and
    stays. `os88type.inc` was walked against the contract for Word and
    Scribe in 14.44.3.
  - **Clear Skies** - the world read buffer (`cs_wgseg`: a READ_AT target,
    then DS for `cs_xpand`'s source, read by offset), the flight shadow
    (`cs_shseg`, 16KB, 8KB in C160, none on Mode X: DS/ES only) and
    os88ui's drop/menu save-unders move. `cs_xpand`'s one sum is on its
    OWN segment, which SUBSEG already covers (14.44 S3).
  - **DOS** - on kern_mega every launch is the whole machine
    (`dos_mem_fix` forces `DOS_MEM_WHOLE`), so `dos_run`'s arena claim is
    never reached. What the bit moves - the packet driver's 3KB
    (`dos_pkt_bufs`, claimed and zeroed before the handoff and unused on
    this kernel), the core's host-heap hook (`dos_hk_claim`: the prompt's
    DIR sort, `DSH_SKB` 7KB, and its COPY buffer) and os88ui's drop
    save-under - is only ever loaded into ES or handed to a kernel slot.
    kern_big's `dosp.bin` is built without `DOS_MEGA` and is unchanged.
- **The one line: PiXEL `highmem`** - paragraph arithmetic on every claim
  it holds (14.40 item 4). Retired by 14.12's flat arm at every site.
- **What was checked and turned out not to be a reason**: the first draft
  of DOS's line named the arena (`OSAPI_MEM_CLAIM_HI` after the user's
  cap, which may be 64KB, so a pool selector under the bit, walked as an
  MCB chain). It would break - but kern_mega's program part never reaches
  that claim.

#### 14.52.3 Measured, on what

`protected-mode` 04896793 plus this branch. `make megatrap`'s census tree
(`MEGATRAP=1`, a private `build/trees/` tree), Bochs 2.7, 16MB.

- **A MEGARUN sweep of the 22 assembly opt-ins but DOS** (three boots, 20 s linger
  each): every one started (`RLAPPLY` answers `no 04` by design: it has no
  window and ends through the abort path), **no FAULT, no CLOSED, 0 trap
  lines charged to any of them** (the six in the logs are the three boots'
  `mega_strcal` calibration).
- **PaccMan** (the C toolchain's build, byte 15 = C3h): 30 s of attract
  mode, 0 traps.
- **Clear Skies**: the Location drop-down opened, LONDON-LCY picked (a
  world read into the buffer), Fly, throttle, London on the horizon, Esc,
  back: **1 trap**, `0060:04ED` - `api_x.done`'s `pop es` with the
  drop-list's freed save-under still in ES, 14.46.1's `.stale` line, once
  a free (14.52.5 item 2).
- **Font Viewer**: four faces stepped through and text typed, 0 traps.
- **DOS, an A/B in one boot**: a DOS.O88 whose program part says
  `OS88_HIGH` alone (`DOSOLD.O88`, its part 0 assembled from 04896793),
  then the new one; at each prompt `CD \RUN`, `DIR`, `COPY KDHELLO.COM
  X.COM`, `DIR`, `KDHELLO` (the park and back):

  | step | HIGH alone | HIGH + HIGHMEM |
  |---|---:|---:|
  | `CD \RUN` | 0 | 0 |
  | `DIR` (4 / 5 files) | 133 | 156 |
  | `COPY` | 18 | 20 |
  | `DIR` (5 / 6 files) | 156 | 179 |
  | `KDHELLO`, exit 042 | 0 | 0 |

  The only difference the bit makes is **two `.stale` fixups a COPY**: the
  core frees its copy buffer (a pool selector now) while ES holds it, so
  `api_x.done`'s `pop es` and `dsh_bufput`'s own (`282C:33B5`) each load
  NULL instead. The rest is the same ~23 a listed row in both builds -
  14.52.5 item 1. The sorted DIR and the copied file came out right.
- **kern_big and kern_small**: no kernel byte. The class byte is read
  there by `inst_pkg_spawn` alone, masked `and bx, 0x3F`; `ld_hisi`'s
  `test 0x80` and `mega_ownerof`'s `0x40` are kern_mega's. Twenty opted-in
  assembly images were compared with their build at the base: **the only
  differing byte is +15**, same length. (`fptest` and `pacman` are
  on-demand builds and were not compared; PaccMan's change is the same one
  define.)

#### 14.52.4 Part B: the claims that stay in conventional memory

The owner's three tests: held **only during active use**, claimed **low**
(bottom-up), **movable while not in use**. What reaches conventional memory
on kern_mega is narrow: the DMA door (`OSAPI_MEM_CLAIM_DMA`/`_HI`, the parts
library's carve and fetch included), a plain claim over 64KB, and every
claim of a package without `OS88_HIGHMEM` (PiXEL alone now). Read off the
code at this commit; sizes are the `equ`s.

| package | claim | size | held | low? | movable idle? | verdict |
|---|---|---|---|---|---|---|
| DOS, Clear Skies, Font Viewer | the claims 14.52.2 names | 3-16KB | a command / a flight / a style | - | - | **fixed**: above 1MB now |
| Word, Scribe, Clear Skies, DOS, PIXELSTEIN 3D | the parts carve at launch (`op_carve`, DMA_HI) | the keep | `op_load` to the re-home's KEEP lift | no (top-down: it becomes a CS) | n/a | **OK** - transient; 14.38.6 D4 |
| PIXELSTEIN 3D | LEV and ART (`op_fetch`, then `op_hilift`) | 10KB, 37KB | one copy's length | yes | n/a | **OK** - lifted above 1MB (14.48) |
| Apple II, C64 | the eager carve holding the ROM (and C64's overlay) | the ROM; C64's 20KB + its overlay | the session | **no** (`op_carve` is top-down) | **no** (`os88_part_seg(0)` banked; SPEC.md 73.14.2.1) | **open** - fails 2 and 3 |
| CWORD, RunCPM | the eager carve that is only the overlay | the overlay | the session | **no** (top-down, a CS) | yes (`cc_ovreloc`, 73.14.2.1) | **open on test 2**, by SPEC.md 20.12.10.8's CS rule |
| Apple II, LOOM, WEAVE | the lazy overlay part (`op_fetch`) | the overlay | first call to the session's end | yes | yes (`cc_ovreloc`) | **OK** |
| Dr Marco | the front screen's art (`op_fetch`) | per class | the front screens | yes | yes (`dm_artreloc`) | **OK** |
| 1942 | the tile banks (`op_fetch`, `n_readbank`) | per bank | the fullscreen game; the latch bank dropped at once | yes | no | **OK** - held only in play |
| Excitebike | sprites (`xb_spr_load`, DMA: the poses are far-called) | 44 / 28 / 17KB | a race (`xb_spr_free`) | yes | no | **OK** - held only in play |
| Excitebike | the front splash (`xb_partget`) | per class | the front screens | yes | yes (`xb_artreloc`) | **OK** |
| 1942 | the PCM rings (`n_pcm_open`, DMA_HI) | 13KB | the fullscreen game (`n_audio_open` to `n_exit`) | **no** | no (MC_DMA, the card reads it) | **open on test 2**: SPEC.md 34.5.3 puts an external ring at `OSAPI_MEM_CLAIM_DMA_HI` |
| Video Player | the card ring (`vp_aseg`, DMA_HI) | `VP_RL` + 1KB | a play with sound | **no** | n/a (the card reads it) | **open on test 2**, the same rule |
| Video Player | the 32KB bounce (`vp_dbnc`, `vp_mclaim`) | 32KB | a play whose ring is above 1MB | yes | no | **OK** |
| Video Player | keeper, shadow, poster, palettes, text save, layer (`vp_mclaim` / `vp_mclhi`) | per play / file | a play or an open file | mostly; VGA8's palette and the text save from the top | yes (`vp_smove`, HEAP-CLAIMS) | **OK**, the two top-down ones aside |
| Video Player | `os88xgap`'s scratch (DMA_HI) | 8KB | one measurement | no | n/a | **OK** - transient |
| MIDIRack | the card's external ring (`mrb_seg`, DMA_HI) | 17KB | **the first synth play to the WINDOW'S CLOSE** (`mro_free`) | **no** | **no** | **open - fails all three** |
| Word, Scribe, Font Viewer, CWORD | `os88type.inc`'s face cache (DMA, for 512-byte alignment) | `TY_FACE_KB` a face | while the face is open | yes | **no** (undeclared) | **open on test 3** |
| Frotz | a story over 64KB | to 508KB | the session | yes | yes (`zf_reloc`) | **OK** |
| Tracker, ModPlug | a module of 63KB and more (14.13's 65KB re-claim included) | to 116KB | the session | yes | yes (`trk_reloc`, `mpp_reloc`) | **OK** |
| PiXEL | every claim: master, work, view, the pump's DMA buffer, the decoder parts | per picture | a picture open | yes | the master and view yes (`pxview`/`pxmaster` declare) | **OK** on tests 2-3; conventional by 14.52.2's one line |

**Nothing was fixed in part B beyond the three moved high**, and the reason
is the same for each open row: none is both small and local and verifiable
in this round.

- **MIDIRack's ring** is the one that fails all three. The shape is to
  free it in `mrb_close` (`mrb_start` already re-claims when `[mrb_seg]` is
  0) - but `mrb_tc` loads ES from `[mrb_seg]` with no zero test, so a feed
  landing after the close would read and write through segment 0. That
  ordering (`mro_drain` runs before `mrb_close`) has to be proved on the
  `midirack*` rows, with a card, before it ships.
- **The external rings' top-down door** (1942, the Video Player, MIDIRack)
  is SPEC.md 34.5.3's contract, not an oversight. `OSAPI_MEM_CLAIM_DMA` with
  the same head is page-safe from the bottom too, so which door is a
  decision (below).
- **The face cache** needs a relocation proc in `os88type.inc` re-pointing
  the face table's segment words (the 512-byte alignment survives a move:
  every claim base is a multiple of 32 paragraphs, guard 6b). It is shared
  by four packages, so it is four packages' rows.
- **The ROM carves** (Apple II, C64) are the parts library's `op_carve`,
  top-down by SPEC.md 20.12.10.8 because a carve may hold code. An
  asset-only carve could come from the bottom and declare a proc that
  re-banks `os88_part_seg(0)`, or be lifted above 1MB the way `op_hilift`
  lifts PIXELSTEIN's parts. Either is a parts-library change.

#### 14.52.5 Left, and for the owner

1. **The DOS prompt takes a `mega_gpw` fixup per CHARACTER it prints**,
   with or without this round (the A/B: ~23 a DIR row). The sites are
   `con_markc`'s `or [con_drb + bx], al` and `con_write`'s `mov word
   [con_cx], 0`, reached with DS = the box's CODE selector. 14.46.1 counted
   the DOS trip (0), not the prompt. A census item, not this one's: find
   the path into `con_putc` that sets DS from CS.
2. **The `.stale` fixup once a free** (`api_x.done`'s `pop es`) now reaches
   Clear Skies' drop-lists and DOS's COPY. `os88ui_mnback` and
   `os88ui_drback` free the bank with ES still holding it; a `push ds /
   pop es` in front of each `OSAPI_MEM_FREE` (2 bytes a site) would end it
   for every `OS88UI_DROP`/`OS88UI_MENU` package (DOS, PiXEL, Redline,
   Skies, Word). Left: it touches five packages' images for one trap a
   close.
3. **Not run here**: Clear Skies' shadow above 1MB, which only a 1bpp
   adapter takes (Mode X uses none) and neither Bochs nor `286-mega` has;
   and the 286 reset path for any opt-in (Bochs is a 386).
4. **Owner questions.**
   - **The external DMA rings**: keep SPEC.md 34.5.3's top-down door, or
     move them to `OSAPI_MEM_CLAIM_DMA` with the same head so they are
     claimed low? A ring is MC_DMA and pinned while the card reads it
     either way, so the question is only where it sits.
   - **MIDIRack's ring**: free it at each stop, so it is held only while
     playing, at the price of a page-safe claim per play that a fragmented
     heap can refuse mid-session?

#### 14.52.6 Cost

| what | bytes |
|---|---:|
| kern_big, kern_small, kern_mega | **0** - no kernel source changed |
| every opted-in package's image and file | **0** - one header byte's value |
| `apps/os88api.inc` | a comment: OS88_HIGHMEM's note names the gate (`abilock`: *nothing changed*) |

**Rows that cover it**: `megahigh` (full, the gate); `movable` (fast, whose
discovery it shares); `megatrap0` (soak, Bochs: HELLO, CALC, MINES,
FONTVIEW, SOLITAIR, PIANO, BOUNCE, DESKCAL and TIMER are OS88_HIGHMEM now);
`megarehome` (soak, Bochs: DOS and SKIES are OS88_HIGHMEM now, and its
comment says why neither is asked to hold a pool claim at rest).

### 14.53 After 14.52: the owner's calls, the menu save-under, and the claim issues left for after the upstream PR (2026-10-21)

**The owner's answers to 14.52's two questions.**

1. **External DMA rings claimed bottom-up - on kern_mega only.** A ring
   (`OSAPI_MEM_CLAIM_DMA_HI`, SPEC.md 34.5.3) is claimed top-down on every
   kernel today; on kern_mega it moves to the bottom-up claim with the same
   64KB-page safety. kern_big and kern_small keep SPEC.md 34.5.3 as written.
   14.55 builds it.
2. **MIDIRack frees its external ring at every stop, and handles a refusal**
   at the next play: a fragmented heap that cannot give a page-safe 17KB is
   a play that says so, not a crash. `mrb_tc`'s unguarded ES load is the
   hazard 14.52 named, and the fix has to close it. 14.55 builds it.

**The menu and drop-list save-under, fixed (owner: "sounds easy so lets do
it").** `os88ui_drback` and `os88ui_mnback` freed the banked pixels while ES
still held the claim, so the free's own API return reloaded a freed selector
- one `api_x.done` stale-ES fixup per close in every package that carries
`OS88UI_DROP`/`OS88UI_MENU` (DOS, PiXEL, Redline, Skies, Word). The two
restores now `pop es` BEFORE the free, at no byte cost, and the two refusal
paths (a bank that straddles two displays, freed unrestored) load ES from DS
first, two bytes each. Every package that carries the controls is rebuilt by
it; on kern_big and kern_small the bytes move and the behaviour does not.

**The DOS prompt's trap** - one write fixup per character the windowed box
prints, because the core and the box enter with `push cs / pop ds` and on
kern_mega CS is a read-only code selector - is measured, and fixed if it
costs anything a user can notice, by 14.54. The owner wants it settled
before the upstream PR.

**THE CLAIM ISSUES LEFT FOR AFTER THE UPSTREAM PR.** The owner's rule (14.51)
is that a claim may stay in conventional memory when it exists only during
active use, is claimed bottom-up, and is movable while idle. These fail it
and are NOT fixed now - "we will get to them, but likely not before we first
PR this to upstream":

| package | claim | what fails | fix shape (14.52.4) |
|---|---|---|---|
| Apple II, C64 | the ROM carve (C64 20KB) | top-down and pinned for the program's life | claim bottom-up; a relocation proc for the one segment word each holds |
| CWORD, RunCPM | the overlay carve | top-down by the rule that a block holding code goes high (SPEC.md 50.3.2.1) | bottom-up with `cc_ovbind` as its proc, which HEAP-UNPIN-PLAN names as already being one |
| Word, Scribe, Font Viewer, CWORD | `os88type.inc`'s face cache | never declared movable | declare it; the cache is rebuilt on a miss, so it may also be purgeable |
| 1942, Video Player | the external DMA ring | top-down by SPEC.md 34.5.3 | **taken by 14.55 on kern_mega**; kern_big/kern_small stay as specified |
| MIDIRack | the 17KB external ring | all three tests | **taken by 14.55** (freed at stop, a refusal handled) |
| the DOS prompt | (not a claim) one #GP fixup per character | performance | **taken by 14.54** |

Two that are fine as they are and recorded so nobody re-audits them: the
re-home carve at launch (14.38.6, brief), and PiXEL's claims (not HIGHMEM,
14.40 - the gate's one exemption).

### 14.54 The DOS prompt's #GP fixups: measured, priced and taken out (2026-10-21)

The owner's question: *"Does the dos prompt trap cause a performance issue?
If so we might want to fix that before we PR."* **Yes - 28% of a long TYPE
on the mr286 - and it is fixed**, on kern_mega's DOS.O88 alone, for 15 bytes.

#### 14.54.1 What traps, counted (Bochs 2.7, 16MB, `make megatrap`)

The box in its window, started through MEGARUN; each command typed at the
prompt and the E9 census read until it went quiet for 10 s. Base 72fba2b3.

| operation | #GP fixups | sites |
|---|---:|---|
| 20 s idle at the prompt | 0 | |
| typing a 65-character command line (the editor's echo) | 0 | |
| `ECHO` + 60 characters | **62** | `con_markc` 61, `con_write`'s CR 1 |
| `ECHO` + 10 characters | **12** | the same |
| `DIR \RUN` (two files) | **87** | `con_markc` 83, CR 4 |
| `TYPE` of 800 bytes | **802** | `con_markc` 790, CR 10, and the two `.stale` below |
| `TYPE` of 20,000 bytes | **20,002** | `con_markc` 19,750, CR 250, the two `.stale` |

**Exactly one per byte printed**, plus the two `.stale` fixups a TYPE's
buffer free costs once a COMMAND (`api_x.done`'s `pop es` and the core's
`dsh_bufput` `pop es` at 282C:33B5, 14.52.3 - left, below). Every one of the
per-byte traps comes from ONE entry: `dos_hk_tty` (DHK_TTY, the box's console
hook) did `push cs / pop ds` before `call con_write`, so con_write's first
store - `con_markc`'s `or [con_drb + bx], al`, or `.cr`'s `mov word
[con_cx], 0` - went through the box's read-only CODE selector, and
`mega_gpw` swapped DS for the data selector in the frame and ran it again.
The swap lasts only to dos_hk_tty's `pop ds`, so the next byte pays again.

**INT 21h AH=02h/09h/3Fh, INT 33h polls and "a windowed DOS program printing
a lot" do not exist on kern_mega**: `dos_mem_fix` forces `DOS_MEM_WHOLE`
(KERN-MEGA-DOS-PLAN 12.1 item 1), so every program runs under `kern_dos` in
REAL mode, where there is no #GP to complete. The window runs the prompt and
nothing else, and the prompt's built-ins reach the console through
dos_tty -> DHK_TTY. The other `push cs` sites in `dos.asm` are under
`%ifndef DOS_EXTCORE` (kern_dos's copy, real mode) or cold (`dos_snap`,
`dos_pkt_*`, which the idle census showed at 0).

#### 14.54.2 Priced, two ways

**PREDICTED, 80286 clocks** (Intel's protected-mode table; `m` taken as 3,
no wait states), the path a `con_markc` fixup takes with DS = the box's pool
code selector and ES a GDT selector:

| step | clocks |
|---|---:|
| the store's first attempt + #GP through the IDT's interrupt gate, same privilege (40+m) | 48 |
| `mega_xs` stub: `push imm8`, `jmp near` | 13 |
| `mega_fault`: `cli`, `pusha`, two `push sreg`, `mov ds` (17), the vector compare and `je` | 63 |
| `mega_gpfix`'s error-code test | 17 |
| `mega_gpw`'s head and `call mega_cs2ds` | 26 |
| `mega_cs2ds` -> `mega_ldt2ds` (the pair's twin checked: `mov es` 17, `pop es` 20, eight compares) | 201 |
| the prefix scan: `mov es,[ss:bp+26]` (19), seven compares | 84 |
| DS: `mega_cs2dsw` -> `mega_cs2ds` -> `mega_ldt2ds` again, and the frame store | 241 |
| ES: `mega_cs2dsw`, `mega_cs2ds` refuses a GDT selector | 82 |
| `.fixed`, `jmp mega_gpfix.resume` | 14 |
| resume: `pop es`/`pop ds` (20 each), `popa` (19), `add sp`, `iret` same privilege (31) | 95 |
| **one fixup** | **~884** |

The replacement `mov ds, [cs:dos_dsel]` costs what `push cs / pop ds` did
(19 + the prefix against 3 + 20), so the saving is the whole ~884 clocks a
byte: **55 us at 16 MHz, 74 us at 12 MHz, 110 us at 8 MHz**. 20,000 bytes is
~1.1 s on the mr286's 16 MHz; a 40-byte DIR row ~2.2 ms.

**MEASURED-ish, 86Box `286-mega`** (the mr286: MR BIOS 286, 16 MHz, 4MB;
not cycle-accurate, so the ratio is the finding and not the microseconds).
DOS.O88 and the text files on a 1.44MB B:, the box launched by
double-click, each command timed in EMULATED ms (held machine, 50 ms steps)
from the Return to the first frame of its finished console picture - the
console paints once the verb has returned. Three rounds each, the same
command sequence:

| command | before (ms) | after (ms) | difference |
|---|---|---|---:|
| `TYPE` 20,000 bytes | 5,650 / 5,500 / 5,500 | 4,150 / 3,950 / 3,950 | **-1,533 ms, -28%** |
| `TYPE` 800 bytes | 1,650 / 1,850 / 1,800 | 1,900 / 1,750 / 1,750 | in the noise (predicted -45 ms) |
| `DIR` (three files) | 1,600 / 1,600 / 1,650 | 1,600 / 1,600 / 1,650 | in the noise (predicted -5 ms) |

1,533 ms over 20,002 fixups is **77 us a fixup on 86Box**, ~1,230 of its
clocks against the 884 predicted - its 286 is not Intel's table, and its
memory and the prefetch refill after an exception are its own; the two
agree on the order. What the byte costs ANYWAY is the marginal of the long
TYPE over the short one: **197 us a byte before and 115 after**, so the
fixup was **41% of every byte the prompt prints**; a short command is
dominated by the floppy read and the one 25-row paint (~1.6-1.8 s either
way here), which is why DIR on three files does not show it. **It is a
user-visible cost on a long listing or a TYPE of any length**, and the
86Box fault log agrees: a session of the old box logged more #GP than the
patch's 100,000-line cap, the same session on the new box 112 (the BIOS's
and `mega_svc_lo.r286`'s own).

#### 14.54.3 The fix

`dos_hk_tty`, under `%ifdef DOS_MEGA` only: **`mov ds, [cs:dos_dsel]`** in
place of `push cs / pop ds`. `dos_dsel` is a word in the box's image,
written THROUGH DS by `dos_hk_bind` (which runs once, before the first
door, with DS the box's data selector) and read through CS, which is
readable. A pool region keeps its selectors when it moves (`mx_compactf`,
"a block MOVES WITH NO RELOCATION PROC"), so the word stays right on the
case that ships; for the conventional fallback, whose segment IS its data
selector and changes when it moves, `OS88_REGION_MOVABLE` names
`dos_reloc`, which writes DX (where the region is now) into the word
through DX itself. Nothing else in the box changed: the per-byte path is
the only hot one the census found.

- **Cost**: kern_mega's `dosp.bin` (part 0 of `kdos/DOS.O88`) **51,220 ->
  51,235, +15 bytes** - `mov ds, [cs:x]` is 5 against 2 (+3), the word (+2),
  `dos_reloc` (+9), the store in `dos_hk_bind` (+4), and the named
  `OS88_REGION_MOVABLE` form is 3 shorter than the bare one (-3).
  `kdos/DOS.O88` is 65,314 bytes either way (the part is padded to its
  alignment). `CORE_MAX` is untouched: the hook is the HOST's, not the
  core's.
- **Identical, checked by md5 at one commit**: kern_big's `kdos/DOS.O88`,
  `dosp.bin`, `doscore.bin`, `kerndos.bin` and the icon pipeline's
  `dos.o88`; and on kern_mega `doscore.bin`, `kerndos.bin` and
  `kerndosl.bin`, assembled with the old `dos.asm` and the new. No kernel
  source changed, so no kernel byte on any build.

**Measured after**: the Bochs census above re-run on the new DOS.O88 -
ECHO 60 and 10 characters **0**, DIR **0**, TYPE 800 bytes **2**, TYPE
20,000 bytes **2** (the `.stale` pair each, which does not grow with the
file). The 86Box column above is the same build.

#### 14.54.4 The menu save-under fix of 72fba2b3, verified

Clear Skies (OS88_HIGHMEM, OS88UI_DROP) under Bochs on `make megatrap`'s
kernel, started through MEGARUN: the Location drop-list opened and an entry
picked four times, the Plane drop-list once - five closes.

| SKIES.O88 built from | `api_x.done` (0060:04ED) stale-ES fixups |
|---|---:|
| 433bf4ed (72fba2b3's parent) | **5** - one a close |
| 72fba2b3 | **0** |

The only other trap lines in either boot are `mega_strcal`'s two at boot.

#### 14.54.5 Left

- **The `.stale` pair a TYPE or COPY** (14.52.3): ~1,800 clocks a COMMAND,
  not a byte, so not a cost anybody can see. The host half could be taken
  in `dos_hk_free` (ES off the claim before `OSAPI_MEM_FREE`) but the
  core's own `pop es` in `dsh_bufput` would still reload the freed
  selector, and the core has 37 bytes of kern_mega slack for everything
  else. Left.
- **The 286 reset path** (`mega_svc_lo.r286`'s deliberate #GPs, one a
  reflected BIOS call) is what the 86Box log's other lines are, and is not
  this item.

**Rows**: `megadosp` (soak, new - Bochs, the census at the prompt: the
launch, ECHO and DIR at 0, TYPE at most the `.stale` pair; red with the
`push cs / pop ds` put back, `--pkg` with the base's DOS.O88 reading ECHO
62, DIR 87, TYPE 802); `megatrap0`, `megarehome` (soak, Bochs: DOS.O88 still
launches high); and on kern_big, where DOS.O88 is byte-identical, nothing
can see it - `dosmouse`, `dosmem`, `dosram` and the other `dos*` rows run
the same image they ran before.

### 14.55 The external DMA rings: bottom-up on kern_mega, and MIDIRack's freed at every stop (2026-10-21)

14.53's two owner decisions, built. SPEC.md 50.3.2, 34.5.3 and 105.8.4 are
the contract; this is the record.

#### 14.55.1 A ring is claimed bottom-up on kern_mega - in the kernel

**The switch is in the kernel, inside `%ifdef KERN_MEGA`**, so no package
changed for it and an old package benefits. `mem_claim_dma_hi_x` (the body
behind `OSAPI_MEM_CLAIM_DMA_HI`, 0x03B2) clears the direction when CX - the
page-safe HEAD - is not 0, and the claim then runs `OSAPI_MEM_CLAIM_DMA`'s
own scan and page bump with the same guarantee: no 64KB physical page
crossed by the head. The record carries no `MC_DMA_HI` bit, so a movable
one compacts downwards.

**Why the head and not the caller decides.** The cell's callers are two
populations, and the head tells them apart exactly:

| caller | CX | what it is | where it goes on kern_mega |
|---|---|---|---|
| 1942 `n_pcm_open` | 13 | three PCM rings the DSP reads | **bottom** |
| Video Player `vp_aseg` | `VP_RL/1024+1` | the card's external ring | **bottom** |
| MIDIRack `mrb_start` | 17 | the card's external ring | **bottom** |
| `SOUND.DRV` `sbl_pool_get` / `sbl_dma_get` | the whole block | its staging pool (a ring plays in place out of it) and its double buffer | **bottom** |
| the parts library's `op_carve` | 0 | code you far-call, and the region on a re-home | top (a CS, SPEC.md 20.12.10.8) |
| `apps/cc/crt0.asm`'s overlay | 0 | a CS | top |
| the Video Player's `vp_mclhi` | 0 | VGA8's palette, the text save | top |
| `os88xgap.inc`'s scratch | 0 | 8KB for one measurement | top |

A head is a chip's page, so every claim with one is a DMA buffer, and in
this tree every one of them is held only while it plays (34.5.2's pool is
claimed at the first grant and is movable whenever no direct stream plays
out of it; the double buffer lives one stream). That is exactly 14.51's
test. Nothing relies on a ring being top-down: HEAP-UNPIN-PLAN's
`sbl_halt`/`sbl_go_on` re-arm a ring wherever it is, `mem_claim_1`'s `.hi`
arm only matters to a top-down claim, and an MC_DMA ring a chip holds is
pinned whichever end it sits at.

**`SOUND.DRV`'s own two are in the set, and that is a decision to look
back over**: the owner said *external* rings. Keying on the head catches the
driver's pool and double buffer too; they pass 14.51's three tests and are
the same kind of block, so they were left in. Excluding them needs a signal
the cell does not have today (a flag in the call, so an SDK change).

**Cost: 4 bytes of kern_mega `.cold`** (42,272 -> 42,276; `jcxz` + `xor
di, di`), resident, no `.text` or `.bss`, `os88megaseg`'s overhead unchanged
at 4,323. **kern_big and kern_small: 0** - built at one commit with and
without the change, `kernel.bin` and `kernel.sys` of both kernels and every
`.o88`/`.drv` but the two packages of 14.55.2-3 are md5-identical.

#### 14.55.2 MIDIRack claims at Play, frees at every stop, and says so when refused

On every kernel (it is package behaviour). `mrb_close` now gives the ring
back after verb 2, and `mro_close` is every road out of a play - Stop, a
song's end and the next one's start, Eject, Remove, Clear, the window's
close - so all of them free it. `mrb_start` frees it on any refusal (it used
to keep a ring a refused open had claimed) and the moment a grant ring opens
instead, since that one never reads it. `mro_free`'s copy of the free is now
`mrb_rfree`.

- **The hazard 14.52 named is closed by order and by one test.** The order:
  `mro_close` runs `mro_drain` first (`[mr_seqon]` cleared, `[mr_inpass]`
  waited out - the worker sets `[mr_inpass]` BEFORE it reads `[mr_seqon]`,
  so either it is seen and waited for or the pass sees no song), verb 2
  then stops the DSP, and only then is the ring freed. So `mrb_span1`, the
  only WRITER through `[mrb_seg]`, is unreachable after the free and carries
  no test. The test: `mrb_tc` has a reader the drain does not cover -
  `mr_elapsed` calls it from the worker's `mrw_draw`, which runs OUTSIDE
  `[mr_inpass]`, on an `[mr_out]` it may have read before `mro_close`
  cleared it. It reads `[mrb_seg]` once into a register and answers 0, 0
  when it is 0, and `mrb_rfree` zeroes the word (`xchg`) BEFORE the free, so
  that reader sees no ring or a freed conventional block (a wrong second on
  the bar for one frame) and never ES = 0, which on kern_mega is a null
  selector and #GP.
- **A refusal is a refused Play**: nothing open, nothing held, `[mr_state]`
  stopped and the status line *"No memory for the card - close a program
  and retry"* (`mr_s_ringmem`, in `mr_s_notask`'s phrasing; it was the bare
  `mr_s_nomem`). The next Play asks again.
- **Cost**: `MIDIRACK.O88` image 37,588 -> **37,658 (+70: 51 the message,
  19 code)**, bss 10,920 unchanged, the file 30,980 unchanged (packed
  29,806 -> 29,837, the same sectors). Price at run time: a page-safe claim
  per song, a playlist re-claiming between songs.

#### 14.55.3 1942's PCM was refused by its own room check, on a heap in two pieces

Found by this round's kern_mega run, and **a package fix on every kernel**,
so it moves `1942.O88` on kern_big and kern_small too (2 bytes in place,
same length). `n_pcm_open` asked `op_lazykb` for the bank's KB, `xchg`'d it
into BX and called `OSAPI_MEM_AVAIL` - which answers **BX = the total free
KB** - then compared AX (the largest run) against BX. So the check was
*largest run >= total*, true only for a heap in one run. With the ring
top-down it usually is (the ring sits in the ceiling stack); with the ring at
the bottom on kern_mega, what lies under it - the purgeable FAT window, which
both answers count as free, and a 1 KB hole - is a second run: 553 KB
against 558, and PCM was refused every time: the ring freed and the game on
FM effects. The need goes in BP now (`n_pushall` banks it). On kern_big it
was the same latent refusal on any fragmented heap. Found with Bochs linear
breakpoints at the cell and at the four `jc .bad` sites (1942's image at
linear 140000h, `LDT[507h]`).

#### 14.55.4 Measured, on what

**kern_mega, Bochs 2.7, 16MB, SB16** (`--sb --cfg 1`), a 1.44MB B: of
VIDEO.O88, a PCM8 clip (`testsrc2` 320x240 15 fps 12 s + 440 Hz, `--preset
vga --profile floppy --audio pcm8`, 294,912 bytes), 1942.O88, MIDIRACK.O88
and two songs. `mem_tab` read with the debugger (linear 100BFh on this
build), five words a record:

| | the ring's record during the play | after |
|---|---|---|
| Video Player, PCM8 on the card | `1240` 17KB, MC_DMA `0440` (bottom-up, head 17KB) - the player's top-down `vp_mclhi` claim beside it reads `9D40`, MC_DMA `8000` | freed |
| MIDIRack, the DSP synth at 22,050 Hz | `1240` 17KB, MC_DMA `0440` | freed at Stop; BATTLE1's end advanced to INTRO and re-claimed |
| 1942 in play, **base kern_mega** (A/B) | `9C80` 13KB, MC_DMA `8340` (top-down) | - |
| 1942 in play, this build | `1240` 13KB, MC_DMA `0340` | freed at Esc (`n_exit`) |

- **The sound is intact.** The clip's 440 Hz is continuous in `sb.wav` from
  0.0 to 11.5 s, and the card's line reads **Drew 180/180, stalls 0, pauses
  0, Late 89, 27 ticks of 218** - 14.41's Bochs row in proportion (300/300,
  pauses 0, Late 147, 45 of 363), which is Bochs's SB16 and not this.
  1942's twelve shots are PCM bursts in `sb.wav` (RMS ~96 idle, 650-1,134
  in a burst) and its stream never fell back (the ring held throughout).
  MIDIRack played two songs on the DSP. No `FAULT` in `e9.out`.
- **kern_big, MartyPC `os8088_5150_herc_sb_720_gla`**: `midiracksb` PASS
  (the EXTERNAL ring - this DSP is 2.1, so SPEC.md 105.8.4's "grant ring on
  MartyPC" was stale and is corrected - 8,000 Hz, 73,728 samples in 9.23 s,
  0 underruns, pitch 10/10, the transposed 0), `midirackwt` PASS, and the
  new **`midiracksbmem`** PASS: the ring at 8BC0 in the claim map while it
  plays and gone at Stop; `tests/filler` takes the heap to 16 KB reachable
  (holes plus purgeable claims) and Play is refused with the message and
  nothing held; the filler closed, the next Play opens the card and CONS
  moves. **Red with the ring kept at Stop** (`mrb_close` ending in `ret`):
  `FAIL sbmem: the ring is still held after Stop ([mrb_seg] 8BC0)`. 32.6 s
  wall, declared 45.

#### 14.55.5 Rows, what is left, and for the owner

**Rows that cover it**: `midiracksbmem` (new, soak), `midiracksb`,
`midiracksbrate`, `midirackwt`, `mrdraw` and `mrdrawvga` (the draw that
reaches `mrb_tc`), `1942sound` (`n_pcm_open`, 905 s), and the fast tier
(`movable`, `megahigh`). Nothing registered runs a kern_mega sound play;
14.55.4's Bochs runs are by hand.

**Not run**: 86Box `286-mega` (the placement is the same scan on the 286
reset path, and nothing here touches `mega_r286`); a real SB on a 286.

**For the owner**:
1. `SOUND.DRV`'s pool and double buffer go bottom-up on kern_mega with the
   rings (14.55.1). Keep, or narrow to package rings (an SDK flag)?
2. MIDIRack on a DSP WITHOUT auto-init (SB 1.x) still claims the 17KB
   external ring at each Play only to have verb 0 answer 7, then frees it
   for the grant ring - so a fragmented heap now refuses a play the grant
   ring could have made. A ~20-byte option: on a refused claim, try the
   grant ring before refusing (it degrades by tier, 16 -> 8 -> 4 KB of the
   driver's pool).
3. MIDIRack's speaker/Covox ring (`mrk_seg`, 4 KB) still lives from the
   first bracket play to the window's close - the same shape as the ring
   this round freed, and not asked about.

#### 14.55.6 1942's sampled effects on kern_big, after the refusal fix

14.55.3 made `1942sound`'s `sb` leg run its PCM section for the first time
on a tree with no cartridge: the row had `sampled = sb and rom_build()`, so
with no ROM it expected no PCM and the refusal satisfied it. With the leg
taking SPEC.md 101 at its word (DSP 2+ plays the bundled part on any build)
it failed at `'PCM stopped FM music'` - and **that was the fixture, not
1942**. `g.fixture()` runs `n_stageinit`, which starts music slot 4, and on a
no-cartridge build slot 4 is `tools/1942data.py`'s placeholder jingle (440,
660, 880 Hz, 24 ticks, no loop): read at the second sample it was over -
`musicptr 0, musicfreq 0, musicwait 1`, `opl_b0` key bit clear with nothing
to key. The first sample passed (musicfreq 440, wait 2) because the jingle
was still sounding. The leg now restarts slot 14 before each sample - the
generated gameplay score, the same on both builds, looping, first note a
held 440 Hz - and asserts it is keyed on BEFORE the sample, so a quiet
fixture reads as `fixture: no music held` and never as a PCM defect again.
**Broken on purpose**: `n_effect`'s PCM branch zeroing `[n_musicptr]` turns
the leg red at `PCM stopped FM music`. A second stale step sat behind the
first: the missing-bank fallback poked `n_pcmfile`, the sidecar name that
went when the bank became part N_P_SFX, so it is now the graphics leg's
shape - the row's offset moved past the file's end, op_fetch refuses,
`pcmseg` 0 and effect 18 goes to FM at 180 Hz (`tests/n1942.py`'s symbol
list gains `N_P_SFX`). No package or kernel byte moved. MartyPC
`os8088_xt_vga_sb`: `sb` leg PCM start 4.98 / 4.11 / 4.74 ms (under the
15 ms bound), capture peaks Sound Blaster 30,975, AdLib 4,074. `soak -k
1942sound -k 1942`: both ok, `1942sound` **751.5 s** in a lane beside `1942`
(declared 905, corrected to the measurement), `1942` 265.5 s.

### 14.56 Field results on the owner's 286, kern_mega at 942137bc (2026-10-21)

The owner's first run of this round's disks on the Victory 286 (BIOS-served
hard disk, PS/2 mouse).

- **Hibernate** takes several seconds, which the owner reads as probably fine
  (14.46.5's ~22 file calls; not timed).
- **After the resume the PS/2 mouse is DEAD.** NumLock still toggles its LED,
  so IRQ1 is alive and the machine is not frozen - it is the pointer alone.
  **Confirmed by the owner a moment later: the system is FULLY alive - the
  screen saver came on by itself after the resume** - so IRQ0, the scheduler,
  `ui_task` and drawing all run; it is ONLY the mouse.
  **OPEN, being investigated as 14.57.** *(2026-10-22: FIXED by 14.57 and
  confirmed on the Victory, 14.60.)* The serial resume is `mouresume`'s
  (MartyPC, kern_big); no row resumes a PS/2 mouse on kern_mega.
- **T69 plays better but still stutters, and runs behind real time.** The
  owner defers it: *"another round on video player, likely on its own, later,
  for performance."* 14.36's clock fix is in and helped; what is left is
  throughput on a BIOS-served disk (14.35's real-mode fallback question is
  still open beside it).
- **MECH2 (`apps/video/demo/vga/04-MECH2.V88`) plays until the first heavy
  frames, many seconds in, then CORRUPTS THE SCREEN AND FREEZES THE WHOLE
  MACHINE.** A defect rather than a speed question. **FIXED in 14.58**: a
  BIGSP record read in place under protected mode ran off its 64 KB window.
- **The owner on 14.55's three questions.** SB 1.x fallback: *"Low priority
  but if you have the fix, sure"* - taken, 14.59. MIDIRack's speaker/Covox
  ring: *"Should only be holding that speaker ring if it's actually in
  speaker mode, to start with, but then sure have it freed when not in use"*
  - taken, 14.59. `SOUND.DRV`'s own pool and double buffer going bottom-up on
  kern_mega with the rings: not answered, so they stay as 14.55 built them
  (they pass 14.51's three tests).

### 14.57 The PS/2 mouse dead after a hibernate's resume: IRQ12 open through the re-knock (2026-10-21)

14.56's field report: on the Victory 286, kern_mega at 942137bc, a hibernate
resumed with the whole machine alive (the screen saver came on by itself
later) and the PS/2 mouse doing nothing. **Both defects found are in
`hbm_wake`'s step 3d, in HIBER.DRV, and are SHARED with kern_big.** Neither is
kern_mega's. SPEC.md 96.45.2.4 is the contract.

**1. The re-knock races its own ISR (the dead mouse).** SPEC.md 9.9.8's wake
re-knock (`0xF5`, then `F3 200 / F3 100 / F3 80 / F2` through `0xD4`, then
`0xF4`) reads every reply by POLLING 60h. Its comment says *"IRQ12 is still
masked at the slave"*. That is true of the LIVE route only: `mou_p2_off`
masked it before `kd_resume` sampled the mask. On the REBOOT route the
restored mask has IRQ12 OPEN. kern_big's `hbm_handover` samples the FRESH
boot's masks, whose PS/2 mouse is live, and kern_mega's `mega_hbwake` restores
`mega_dimr2`, taken at `hbm_xplan` with the session's mouse live. Bochs reads
slave `8Fh` at `.nowz`. IF has been on since step 1, so `mou_p2_isr` and the
poll take turns at the replies. On a real 8042 the IRQ pin is raised by the
controller's firmware a few of its own instructions after OBF, so either side
can win any one byte. The sequence that kills the mouse is: the poll takes
`0xF5`'s ACK, the ISR takes an `0xF3`'s, and the knock gives up with the
DEVICE still waiting for its sample-rate argument. The trailing `0xF4` is then
consumed as that argument. Reporting stays off (`0xF5` took), the keyboard is
untouched, and nothing re-enables the device for the rest of the session. The
mouse is dead. This needs a WHEEL mouse (`[mou_p2wz]` 3). The Victory's has
not been confirmed as one, but almost every PS/2 mouse is. **What it is NOT**:
the slave's or the cascade's mask, the IDT (run 0 brings the image's back, and
IRQ12's gate is built from the image's IVT), or the 8042's command byte (the
block writes it).

The two emulators do not show the field symptom, and they differ from each
other. Bochs puts the reply in the output buffer and raises IRQ12 only on its
next timer tick, so the poll wins every byte and the knock succeeds: no symptom
at all. QEMU raises IRQ12 as the byte is queued, so the ISR wins every byte and
the knock fails at its first ACK, before any `0xF3`. The wheel comes home 0 on
a device in four-byte mode. The phase decoder drops each zero Z byte for its
missing bit 3, so the pointer still works. Only a machine whose 8042 lets both
sides win sometimes, the field machine, gets the half-done knock.

**The fix**: IRQ12 masked at the slave at the head of the PS/2 block
(`pushf / cli / in 0xA1 / or MOU_P2SL / out / popf`, 9 bytes). It is
`mou_p2_init`'s own rule (*"IRQ12 MASKED BEFORE ANYTHING IS ASKED"*), which
the wake had copied everything of except this. The block's last step unmasks
it, as before.

**2. The serial unmask opened the whole MASTER 8259.** Step 3d unmasks
`not [mou_line]` at 21h for the serial mouse and calls 0 a safe no-op. But a
PS/2 mouse, and a pointing driver, settle with `MOU_P2LINE` = 0xFF, and
`not 0xFF` = 0. Every resume on such a machine therefore came back with IRQ 3,
4, 5 and 7 unmasked on lines the session had masked. Measured: Bochs on
kern_mega `B8h` before, `00h` after. QEMU on kern_big `A8h`, `00h`. It does
not kill the mouse (IRQ 3-7 are below the cascade's priority), but a
PicoMEM's or an LPT's line opening behind the kernel's back is its own fault
waiting to happen. **The fix**: `cmp cl, MOU_P2LINE / je` around the unmask
(5 bytes, `%ifdef KERN_BIG`: kern_small has neither pointer).

**Cost, measured at one commit (A/B trees `build/p2base`, `build/p2fix`)**:
`hbm_wake` +14 bytes on kern_big and on kern_mega (`.nop2` 0x1091 -> 0x109F,
0x18FB -> 0x1909), inside HIBER.DRV's padded image, which stays 7,169 bytes
(kern_big) and 10,241 (kern_mega). The packed file grows 5,874 -> 5,889 and
8,301 -> 8,312. **No resident byte on any kernel.** kern_big `kernel.bin` and
`kernel.sys` are byte-identical. kern_small has no HIBER.DRV, and its
`kernel.bin`/`kernel.sys` are byte-identical. kern_mega's `kernel.bin` differs
in ONE byte at the same length: `mega_dstubup`'s `add ax, mpk_stub`, the park
stub's offset inside the module, moved by the 14. `os88megaseg` overhead is
unchanged at 4,323.

**Measured on**: Bochs 2.7 (a 386, `type=imps2`) on kern_mega, booted off C:,
for a hibernate, a power-off and Resume, with and without each half. QEMU 9.2.4
on kern_big (`tools/setup-qemu9.sh`; 8.2 aborts in the resume stub, as
hmahiber's note says) with the serial mouse removed, the same three ways. The
Victory itself was not available. That the half-done knock is what it hit is
the one reading that fits a mouse dead with the keyboard fine. It is inferred,
and no emulator here can show it. A second field run after this is the
confirmation.

**Rows**: `megap2hiber` (new, soak, Bochs, 160 s) stops at a breakpoint at
`.nowz` in the hibernated HIBER.DRV, at the base its E9 line printed, and asserts
IRQ12 masked at the slave there. After the resume it asserts the master's IMR
as it hibernated, `mou_p2wz` 3, and the pointer moving. Red with the mask out
(slave `8Fh`) and with the guard out (master `00h`). `p2hiber` (new, soak,
QEMU 9, 30 s) is kern_big's: the master's IMR, the wheel and the pointer. Red
on the old HIBER.DRV (`00h`, wz 0). Also covering: `mouresume` and
`hibernate`/`hibernatem`/`hibernatedrv` (kern_big's resume, serial, MartyPC),
`hmahiber`, `megahiber`, and the `kdreturn*` family (the live route runs the
same block).

**Left for the owner**: (a) a field run of the Victory after this. If the
mouse is still dead, the next instrument is `MOUDIAG=1`'s panel after the
resume. (b) Bochs's mouse is `type=ps2` in `tools/os88bochs.py`, so no other
Bochs row exercises a wheel. `megap2hiber` sets `imps2` for itself only.

**CONFIRMED ON THE IRON (2026-10-21):** the owner reports the PS/2 mouse
works after a hibernate's resume on the Victory 286 with this fix.

### 14.58 MECH2's freeze: a BIGSP record read in place ran off its window (2026-10-21)

14.56's field report: on the Victory 286, kern_mega at 942137bc, Mechwarrior
2 (`apps/video/demo/vga/04-MECH2.V88`) plays until its first heavy frames,
then corrupts the screen and freezes the machine. **It is the Video
Player's, kern_mega's protected-mode arm of it, and it is fixed in the
player.** No kernel byte changed.

**The cause.** MECH2 is a BIGSP file (SPEC.md 98.1.4.1): super-packets up to
127 sectors, records up to 58,879 bytes. Its first record past 32 KB is
frame 170, 11.3 s in; everything before is under 32 KB. Under protected mode
a held file and an XMS bank are read IN PLACE through `OSAPI_XMEM_RUN`'s
windows (14.16, SPEC.md 98.3.18.16): one data selector every 32 KB of the
block, each 64 KB long, the record at window (offset >> 15), offset & 7FFFh.
That holds any 32 KB from any start, and a record of length L only when it
starts no more than 64 KB - L into its 32 KB. A 58 KB record mostly does
not. The decoder ran off the window's end: `lodsb` wrapped SI to 0 and read
the window's START, the frame was drawn from the wrong bytes, and the first
`rep movsw` with SI = FFFFh, or with DI driven past the screen, was a #GP
inside the fullscreen bracket. In real mode none of this can happen: the
handle `vp_xmk` makes is a paragraph's, so SI < 16 and 64 KB less 15 follow
any start. **SPEC.md 98.3.18.16 already said "BIGSP is refused" where in
place is taken. Nothing did it**: `vp_xipq` and `vp_xbipq` had no test, and
14.41's high ring (`vp_rhiq`) was the one place that refused BIGSP, for the
same reason.

**Reproduced, under Bochs** (16-bit protected mode's segment limit checks
are the 286's), kern_mega of this tree:

| run | arm | result |
|---|---|---|
| MECH2 on C:, booted OFF C: with a throwaway `HDD.DRV` that skips rung 1, so every C: read is a reflected `int 13h` as on the field (`a fixed-disk transfer went to the BIOS`), 4MB, SB16 | bank in place (PM), 2,656KB | `kern_mega FAULT vector 0D` **10 s after Space**: CS:IP 2834:99A8, `vd_native`'s span `rep movsw`, DS 28D4 (an LDT run selector), **SI FFFFh** |
| tests/vidplay.py's `--big` lin80 clip (38,691-byte records), 16MB | held, in place | `#GP` at 282C:9A68, the same `rep movsw` one list on, ES A000 **DI FFFFh**, CX 3FC0h: a span of garbage length. The screen: noise to the edges and the fault text over the top line |
| the same clip, 2MB | bank in place | the same fault, with and without an SB16 |
| **kern_big**, the same Bochs machine, A: = `os8088.img` with the same BIOS-only `HDD.DRV`, MECH2 on C:, the shipped player | `XMS bank in place (386), 3008 KB` | **Drew 1147/1147, stalls 0**, no fault |

So kern_big plays it and kern_mega does not, on the same machine and disk
path, and the difference is the window. (Late and the tick count are Bochs's
SB16 clock: kern_big and the fixed kern_mega both read `Late 573` and ~175
ticks for the whole play, 14.41's observation again.)

**The fix: `vp_pmbigq`**, asked at the head of the protected-mode tests in
`vp_xipq` and `vp_xbipq`. Under `[vp_pm]` with `[vp_msl]` = 2 (a BIGSP
file), CF = 1 and the session COPIES: the hold's copy down, or the copying
bank, both through the conventional ring that a BIGSP file keeps anyway.
Real mode returns CF = 0 at once and decides exactly what it did.

| run (fixed) | arm | result |
|---|---|---|
| MECH2, the BIOS-served C: as above, 4MB, SB16 | `Banked in XMS, 2656 KB` | **Drew 1147/1147, stalls 0**, no fault; the same Late 573 as kern_big's |
| the `--big` clip, 16MB | `Held whole in XMS` | 150/150, stalls 0, Late 0, 91 ticks of 91 |
| the `--big` clip, 2MB, SB16 | `Banked in XMS, 608 KB` | 150/150, stalls 0 |
| the `--big` clip, 2MB, speaker | `XMS bank idle: the speaker` | 150/150, Late 0 (the copying bank's speaker refusal, 98.3.18.5, streams it off the floppy) |

**Not reproduced on 86Box.** `286-video` with a kern_mega C: built by
`os88hdd.py` sat in a VGA BIOS loop at boot with the copied `hd` run's CMOS
(SS 0E60, mega_on 0, DX 0378h), and booting A: on the same profile did the
same; it was not taken apart, because the defect is a segment-limit
property and Bochs's check is the 286's. The mr286 has not played MECH2 on
either player.

**What it costs a play.** A BIGSP session on kern_mega copies where it read
in place: the bank's copy DOWN per chunk comes back (one `rep movsw` of
32KB each, ~5 ms on a 12 MHz 286 by 14.41's figure, eight a second at
MECH2's 252 KB/s, so ~4% of the machine), and a held file's drain. kern_big
on the same 286 still reads in place, by LOADALL. Whether that is a stutter
on the Victory is the owner's reading to take.

**Cost.** `video.bin` 58,973 -> **58,997 (+24)**, `VPDIAG=1` 60,537 ->
60,561 (+24), `NOLIVESND=1` 58,740 -> 58,764 (+24); bss unchanged; the
shipped `VIDEO.O88` 45,043 -> 45,065 bytes on disk. `vp_pmbigq` is 14 bytes,
the two calls 5 each. `tools/os88rdiff.py --hot-file
apps/video/rdiffhot.txt`, all three arms: **every hot routine identical, at
an even shift** (only `vp_xipend` and `vp_xbipq`, per-session, moved odd).
**Kernels: no source changed**, so kern_big, kern_small and kern_mega are
byte-identical to protected-mode's.

**The row: `megavidbig`** (soak, Bochs, 330 s declared, 301 measured).
`tests/vidplay.py`'s `--big` clip beside VIDEO.O88 on a 1.44MB B:, played
with Space at 2MB (the bank, Play at once) and 16MB (held whole, after
110 s for the hold to fill): neither may log a kern_mega FAULT. **Red with
the player before this** (`--pkg`): both legs fault at the `rep movsw`.
Green with it.

**Rows that cover it**: `megavidbig`; the kern_big video rows see a change
of no decision (real mode returns at once) and an image +24 - `vidplay
vidplayvga vidxip vidxipbank vidbank vidplayshd vidrepeat` are the ones that
run the two changed routines. The orchestrator's soak union should take
`megavidbig vidplay vidxip vidxipbank vidbank`.

**For the owner to look back over:**

1. **The fix copies; it does not read a BIGSP record in place.** Reading in
   place needs a window that starts within 64 KB less the record of the
   record: a run with a 2 KB step (63.5 KB of span, 1,536 selectors for a
   3MB bank - more LDT than kern_mega has), or a selector the player
   re-aims per record, which `OSAPI_XMEM_SEL` does only from the UI task
   and not under the hook (14.16 decision 1). Either is a kernel and SDK
   change; the copy is ~4% of a 286 at this clip's rate. If MECH2 stutters
   on the Victory where kern_big plays it clean, that is the next step.
2. **The contract was right and the code was not**: SPEC.md 98.3.18.16 has
   said "BIGSP is refused" since 14.16. Any other sentence of that shape in
   the in-place sections is worth a grep for the test it promises.

### 14.59 MIDIRack's SB 1.x fallback and its speaker ring, both the owner's calls (2026-10-21)

14.56's two answers on 14.55.5's questions 2 and 3, built. PACKAGE work: it
changes `MIDIRACK.O88` on every kernel and no kernel or driver byte. SPEC.md
105.8.3 and 105.8.4 are the contract; this is the record.

#### 14.59.1 A refused external ring is not a refused Play

The owner offered two shapes - try the grant ring when the claim is refused,
or skip the claim when the caps say no auto-init - and the second is not
available: **no `SND_CAP_` bit says "auto-init"** (the caps word has
`EXTBLK`, which is the block size, and nothing about the DMA mode), so
skipping would need an SDK change. The first is what is built, and it covers
the SB 1.x case anyway: that machine makes the claim, and whether the claim
is granted (verb 0 then answers 7) or refused, it reaches the grant ring.

`mrb_start`'s `OSAPI_MEM_CLAIM_DMA_HI` refusal now leaves `[mrb_seg]` at 0
and carries on: rate, lead and the synth's open as before, then - with no
ring to pre-roll into - straight to `mrb_grantopen` (16, 8, 4 KB of
`SOUND.DRV`'s pool). Only a grant refused with no external ring is
`mr_s_ringmem`; a grant refused after verb 0's 7 is still `mr_s_sberr`.

**Found on the way, and fixed with it**: `mro_play`'s worker refusal (no
task slot) went to `.no`, which sets `[mr_out]` to NONE with the output
still OPEN - the card's stream and its ring, or the FM chip, or the MPU -
so no later `mro_close` could reach them. The FM/tone/MPU road and the card's
road now share one `.wk` that calls `mro_close` before the refusal (it
saved bytes rather than costing them).

#### 14.59.2 The speaker ring lives for one session

It was already claimed only by a speaker or Covox play on the ISR's road
(`mrk_start`'s `.isr`; a polled Covox plays out of the image), so *"only in
speaker mode, to start with"* was true - what was not is that it was held
from the first such play to the window's close, through any amount of Sound
Blaster or FM after it. Now:

- **A pause keeps it** (the queue is the resume, `mrk_resume`).
- **`mrk_bracket` frees it on every other exit** (Esc, the song's end, N/P,
  a refusal inside the bracket) through `mrk_rdone`, which first takes what
  the ring holds unplayed off the synth's sample count, so `mr_elapsed` -
  which finds no ring afterwards - still reads where the sound stopped.
- **`mro_close` frees it** on every road out of a paused session - Stop,
  Eject, Remove, Clear, Settings, the next song, the window's close.
- **`mrk_start` frees it on a refusal after the claim** (`os88spk_init`'s).
- **A refused claim is a refused Play**: `mr_s_spkmem`, *"No memory to play
  - close a program and retry"* (14.55's phrasing; it was the bare *"Out of
  memory"*), nothing held, and the next Play asks again.

**The ordering argument, since 14.55's `mrb_tc` lesson binds it.** The
readers of `[mrk_seg]` are the ISR (inside the bracket only: `os88spk_stop`
closes the door in `mrk_main` before `OSAPI_FSX_RUN` returns, and a refused
`os88spk_go` never opened it), the bracket's own body (the UI task), and
`mr_elapsed`. `mr_elapsed` is reached from `mru_np_dyn` - the UI task's
paint or the worker's `mru_dyn`, both under the gfx lock - and from
`mru_changed`, **the worker's one lock-free door**. Every free is the UI
task's with the gfx lock held. So the one reader that could see the ring go
between its test and its `mov es` is `mru_changed`, and it no longer asks
for a bracket output: that clock moves only inside the bracket, where the
worker is frozen (SPEC.md 53.2), and the bracket draws itself (`mrk_draw`).
The rest is `mrb_rfree`'s shape: `mrk_rfree` zeroes the word with `xchg`
before the free, and `mrk_queued` reads it once and answers 0 with ES
untouched for no ring, so ES is never loaded with 0. On kern_mega this ring
is a plain claim of a HIGHMEM package, so an LDT selector above 1MB - a
FREED one would fault on a `mov es`, which is why the lock argument is made
rather than leaning on "freed memory is still readable". And `mrk_start`
now puts ES back on DS before entering the bracket, since the free's API
return reloads the ES it was called with and that had been the ring - on
kern_mega one stale-ES fixup per stop otherwise (14.53's case).

#### 14.59.3 Bytes

`MIDIRACK.O88` **image 37,658 -> 37,768 (+110: 46 the new message, 64
code)**, bss 10,920 unchanged, the file 30,980 unchanged (packed 29,837 ->
29,897, the same sectors). No kernel or driver source is touched, so
kern_big, kern_small and kern_mega are byte-identical by construction (the
diff is `apps/midirack/`, `tests/`, and documents). `tests/filler`, which
ships nowhere, grew 370 -> 423 bytes for a `T` key (below).

#### 14.59.4 Measured, on what

**kern_big, MartyPC `os8088_5150_herc_sb_720_gla`** (DSP 2.1, auto-init),
`midiracksbmem` extended and PASS, 50 s alone: the card's external ring
held while it plays and gone at Stop; the speaker's ring at `21E0` while it
plays, still held through a pause, gone at the Stop after it; gone at Esc in
the bracket; not held while the card plays; `tests/filler` under 17 KB and
the card's Play is on the **grant ring, 8,192 bytes, consuming**; the
filler's new `T` (take every run, ask nothing - FL_LEAVE's 8 KB is more than
a 5 KB ring or a 4 KB pool) to 0 KB reachable and both Plays refused with
their messages, nothing open or held; the filler closed, the card's
external ring at `8BC0` and the speaker's at `18A0` play again. **Red three
ways, each on purpose**: the grant fallback taken out (*"want the GRANT
ring ... 'No memory for the card ...'"*), `mrk_bracket`'s free taken out
(*"still held after Esc in the bracket"*), and `mro_close`'s taken out
(*"still held after Stop after a pause"*).

**NOT verified**: a real DSP without auto-init. Every MartyPC profile here
is DSP 2.1; MartyPC's `dsp_version` is a setting, but the machine configs
are concatenated into the SHARED staged tree at `make marty` time, so a 1.5
profile is a change to everybody's emulator, not a row. What was verified
is the half of 14.59.1 that is new code - the claim refused on the DSP we
have, and the grant ring taken instead of a refusal; the SB 1.x road itself
(verb 0's 7, then the grant) is 14.55's and unchanged. Nothing ran on
kern_mega: the change is package code whose PM-specific reasoning (the
selector, ES) is argued above and not measured, and no registered row plays
MIDIRack on kern_mega.

#### 14.59.5 Rows and what is left

**Run, all PASS on kern_big under MartyPC**: `midiracksbmem` 51.7 s,
`midirackspk`, `midirackcovox`, `midirackcovoxpoll`, `midirackwt`,
`midirackend`, `midirackmpu`, `mrdraw`, `mrdrawvga` on the final build, and
`midirackfm`, `midiracksb`, `midiracksbrate`, `midiracktone` on the build
two bytes before it (the ES line of 14.59.2, which none of those four
reaches). A first pass of the last seven failed at *"the autoload did not
happen"* - an artefact, and one worth knowing: `tests/mrprobe.py` resolves
the package's variables by re-assembling the SOURCE, so an edit to
`apps/midirack/` while a row runs makes it read `[mr_loaded]` at the wrong
address. Do not touch the source during a MIDIRack run.

**Rows that cover it**: `midiracksbmem` (extended, declared 70 s),
`midiracksb`, `midiracksbrate`, `midirackwt` (the card and its `.wk`),
`midirackspk`, `midirackcovox`, `midirackcovoxpoll` (the speaker bracket and
its ring), `midiracktone`, `midirackfm`, `midirackend`, `midirackmpu` (the
shared `.wk`, `mro_close`), `mrdraw` and `mrdrawvga` (`mru_changed`).

**For the owner**: the speaker's message is the generic *"No memory to play
- close a program and retry"*, because the ring is the Covox's too and
*"for the speaker"* would be wrong there.

### 14.60 The owner's calls after 14.57-14.59, and what kern_mega still puts low (2026-10-21)

**The PS/2 mouse after a resume is fixed on the iron** (14.57, confirmed by
the owner on the Victory 286).

**MECH2 no longer freezes, and now STALLS** (14.58): with its BIGSP records
copied through the conventional ring instead of read in place, the machine
does not have the capacity to copy to the ring AND load the data - *"we're
running at the edge of the hardware's capabilities, and the ring copy means
this file won't play."* So reading a BIGSP file IN PLACE on kern_mega is a
work item, and it **goes with the deferred Video Player round** (T69's
throughput and timing, 14.56), not before it:

- **The item: can kern_mega read in place at every superpacket size the
  format supports?** Today a record of length L fits an `OSAPI_XMEM_RUN`
  window (one every 32KB, each 64KB long) only when it starts at most
  64KB - L into its 32KB, so a BIGSP record past ~32KB can run off the end -
  which is the freeze 14.58 refused rather than fixed. The answer has to hold
  for the LARGEST superpacket the encoder may write, not for MECH2's 58,879.
- **The pattern to look at first is kern_big's**: on a 286 it reads BIGSP in
  place through LOADALL, which loads a segment's descriptor cache with any
  24-bit base - i.e. it re-aims ONE window at each record's start, so no
  record ever crosses its end. kern_mega's equivalent is the same idea with a
  real descriptor: re-aim one LDT data descriptor's base per record (a
  descriptor write is a few stores; the selector is then reloaded). 14.58's
  agent named the two shapes and their obstacles - a run of windows at a 2KB
  step (~1,536 selectors for a 3MB bank, more than the LDT holds), or a
  selector the player re-aims per record, which `OSAPI_XMEM_SEL` allows only
  from the UI task and not from the rate hook - so the work is a kernel cell
  the hook may call (or a re-aim done by the kernel inside `OSAPI_XMEM_RUN`'s
  own record step) and its SDK contract.
- **What to log while doing it**: the info card (`VPDIAG=1`, SPEC.md 98.3)
  should say which read path a play took (in place / ring copy) and why, and
  the largest record against the window it had - so a field run shows
  whether in-place was refused and by how much.

**Everything else the owner lists for a later round** - *"We'll fix all of
these later; note anything we're still throwing low as usual, and we'll work
on it in another round."* The register of what kern_mega STILL places in
conventional memory, at e94a733d. Sources: 14.52.4's table, 14.41, 14.45,
14.49, 14.55, 14.59.

| what | who | size | why it is low today | note for the round |
|---|---|---|---|---|
| external DMA rings (bottom-up since 14.55) | 1942 PCM, Video Player card ring, MIDIRack's ring | 13KB / `VP_RL`+1KB / 17KB | the DMA door | **lead, unverified**: an AT's 8237 page registers are 8 bits, so 8-bit ISA DMA addresses 16MB, not 1MB - a ring above 1MB may be possible on every kern_mega machine if `SOUND.DRV` programs the page from a LINEAR address. Check the SB's channel, the page register (83h for channel 1) and what each driver computes before believing it |
| `SOUND.DRV`'s own pool and double buffer | the driver | pool + double buffer | the DMA door, bottom-up with the rings (14.55; owner's question 1 unanswered) | same lead |
| MIDIRack's speaker/Covox ring | MIDIRack | 5KB | an ISR reads it | bottom-up and freed at stop since 14.59 - could go high if the ISR may load a selector |
| the parts carve at launch | Word, Scribe, Skies, DOS, PIXELSTEIN 3D | the keep | top-down, it becomes a CS | transient (14.38.6 D4) |
| the eager ROM carve | Apple II, C64 | the ROM (C64 20KB + overlay) | top-down and pinned | fails tests 2 and 3 (14.53's table) |
| the overlay-only carve | CWORD, RunCPM | the overlay | top-down by the CS rule | fails test 2 (14.53) |
| the face cache | Word, Scribe, Font Viewer, CWORD | `TY_FACE_KB` a face | DMA door for 512-byte alignment | fails test 3 (14.53) |
| the 32KB bounce | Video Player | 32KB | a ring above 1MB needs a conventional bounce for the disk | OK by the tests; goes away with in-place reading |
| the keeper, shadow, poster, palettes, text save, layer | Video Player | per play | conventional by design (14.41); VGA8's palette and the text save top-down | the two top-down ones fail test 2 |
| layered / BIGSP / EMS streams | Video Player | the ring | 14.41 left them conventional; BIGSP is the in-place item above | |
| `os88xgap`'s scratch | Video Player | 8KB | DMA_HI | transient |
| 1942's tile banks, Excitebike's sprites, Dr Marco/Excitebike art | the games | per bank / race / screen | parts fetched low | OK by the tests (held only in play) |
| a story / module over 64KB | Frotz, Tracker, ModPlug | to 508KB / 116KB | a plain claim over 64KB is conventional | candidates for `OSAPI_MEM_HUGE` above 1MB (14.37) |
| every claim | PiXEL | per picture | not `OS88_HIGHMEM` (paragraph arithmetic, 14.40) | the gate's one exemption (`tests/megahigh.txt`) |
| `mx_tab`, the pool's table | the kernel | 768 bytes | `.lowbss`, beside `mem_tab` (14.49) | moving it high deferred by the owner (14.51) |
| the descriptor tables | the kernel | GDT/LDT/IDT | conventional (14.2 item 12) | |
| the redirected-volume bounce | the kernel | 512 bytes a call | a buffer above 1MB reaching a RAM disk or a cable share (10.3 item 81) | |
| the launch's brief conventional room | the loader | the image | the re-home reads low before it lifts (14.38.6) | deferred by the owner's decision |

### 14.61 Field: after several DOS round trips the keyboard AND the mouse are dead (2026-10-21)

The owner, on the Victory 286 at kern_mega 78e04d0d or near it: Test Drive III
launched through the DOS box and came back to the desktop **about three
times**; on the fourth return **the keyboard and the mouse were both
dead**. The machine was NOT frozen - the DOS box's end-of-program toast came
up and went away - and **NumLock did not change the keyboard's LEDs**. The
owner's read: *"I doubt this is 'exactly the 4th try' - it's probably some
state or race."*

What the symptom says before anything is measured: both devices sit behind
the 8042, and both dying together while IRQ0 and drawing run points at the
CONTROLLER or at the PIC lines they share a path through, not at either
device. The first suspect is a byte left in the 8042's output buffer with
its interrupt edge missed - ISA IRQs are edge-triggered, so a byte that
arrives while IRQ1/IRQ12 is masked, unhooked or mid-swap, and is never read
from port 60h, keeps OBF set and the line high with no new edge, and the
controller then sends nothing from EITHER port. That is a race by nature,
which fits "about the fourth". Others to rule out: the 8042 command byte or
port enables (AD/A7) left as the game set them; the master IMR's IRQ1 or the
cascade bit; 14.57's re-knock, which the DOS return also runs. **Answered in
14.62**: the first suspect, a stranded byte - an AUX one, left by our own
handoff.

**The owner's discriminator, a reproduction later:** it is something Test
Drive III DOES. Exited at once, before its title screen, the keyboard and
mouse come back; let run to its title screen with Enter and then exited,
they come back dead. So the state TD3's running code leaves the 8042, the
keyboard or the PIC in is what the return does not undo. **It is not the
earlier defect**: 10.3 item 54.3 (the keyboard dead after a return because
`hbm_wake` left `MOU_P2KOFF` set and never sent `0xAE`) is fixed and still in
the tree - SPEC.md 96.45.2.3's keyboard guard in `hbm_wake`, and
`mega_d8042get`/`mega_d8042put` banking the command byte across a park - and
`elendilon` has nothing newer on it (its one commit not in `protected-mode`
is the video-compression squash).

**...and it is intermittent, and it crosses the trip both ways.** The
title-screen exit reproduced it twice and then not on the next three
attempts; and on the NEXT LAUNCH of Test Drive III after those trips, the
**keyboard was dead inside the game**, the game otherwise running. So the
bad state survives the handoff outbound as well as the return - which
points at controller or PIC state both directions carry (an 8042 output
buffer stuck full with its edge lost, the keyboard interface or scanning
left off, IRQ1 masked or an in-service bit stuck) rather than at the
desktop's own keyboard handler.

**...and it is NOT kern_mega's, and probably not Test Drive III's.** The
owner: it happens on **kern_big as well**, and it happened on the **very
first entry to TD3 in a session** - one in which FTPD, the Control Panel and
then **RealBoy** (the Game Boy emulator, branch `gameboy`, not merged here)
had been used, RealBoy last, before TD3 was launched from a desktop shortcut
to a DOS `.LNK` on the whole-machine arm. The keyboard did not work inside
TD3. So the lead is a package that takes over the keyboard and leaves the
8042, the keyboard or the PIC changed at exit, with the DOS handoff carrying
that state into the DOS program instead of putting the controller into a
known one. The owner supplied the exact `REALBOY.O88` and the ROM it ran;
both are kept in the session scratchpad and, like TD3, never enter the
repository. **But RealBoy is not needed for it**: on kern_mega the owner saw the
same failure with nothing else in the session - boot, launch TD3, return,
repeat. So the kernel's own round trip produces it, intermittently, on both
kernels; RealBoy is at most a second way in.

### 14.62 The 8042 across the DOS trip: a stranded byte silences both devices (2026-10-21)

14.61's field report, and the owner's three follow-ups: the keyboard dead
INSIDE Test Drive III on a later launch, the same on **kern_big**, and on
kern_mega with nothing else run in the session - boot, launch, return,
repeat. SPEC.md 96.45.2.5 is the contract.

**Reproduced under Bochs, on kern_mega, on the first try that moved the
pointer.** Plain trips never showed it: 12 Test Drive III round trips, then 10
with the pointer moved inside the game and a key held across the exit, all
clean. Moving the pointer ACROSS THE LAUNCH did, at once: Test Drive III's
second launch reached its Main Select screen with Esc doing nothing - the
keyboard dead inside the game, the game running on. A purpose-built program
then made it a reading: `tests/dostrap/kbtrip.asm` (KBTRIP.COM) reads port 64h
first thing, says `ready` on E9, and waits 30 s for a key. With the pointer
moving across each launch, **6 trips of 12 came in with `64=31` - OBF set, the
byte the AUXILIARY device's - and no key ever arrived** (`nokey 64=31`). Slave
IMR 9Fh: IRQ12 masked. One trip read `entry 64=18` and `nokey 64=31`: the
stranded byte arrived after the program had started.

**The cause is ours and is on both kernels.** The 8042 has one output buffer
for the two devices and holds IRQ1/IRQ12 high while a byte waits, sending
nothing more from either until 60h is read. `hbm_dosrun`'s step 6 calls
`mouse_unhook`, whose `mou_p2_off` masks IRQ12 and then sends 0xA7 - which
holds the device's CLOCK and leaves it REPORTING - and the handover then runs
the program with IRQ12 masked. A packet byte sent between the mask and the
0xA7, or one the device had in flight, sat in the buffer for the whole
program. Test Drive III, RealBoy and the title screen are incidental: what
the field runs share is a hand on the mouse at the launch (a double-click, a
shortcut). RealBoy's source (origin/gameboy, apps/realboy/) touches no port
60h, 64h, 20h/21h or A0h/A1h and hooks no vector - its keys come through the
OS - so it cannot leave the controller in a state; **no package-side fix is
indicated.**

**The dead DESKTOP after a return (14.61's own symptom) is the same fact one
step on, and the Victory's 286 is where it bites.** Two places on the way
home could strand a byte: (a) with the device left reporting through the
whole program, the return's 0xA8 releases a queued packet that trickles in
after `mega_dwake`'s no-wait drains; (b) hbm_wake sent the device 0xF4 and
left its 0xFA "for the ISR", and step 4's int 10h follows at once. On a 286
every reflection is a CPU reset, and the Victory's BIOS re-initialises the
8259 on that path (`mega_r286` re-initialises it again when it sees the masks
moved). ICW1 erases every line's edge sense, so a byte sitting in the buffer
at that moment is never delivered. On the Victory `mega_dprime`'s reflection
(the PicoMEM's line, W6) comes straight after the drains. Neither Bochs (a
386: no reset per reflection) nor 86Box (its ICW1 rebuilds IRR from line
LEVELS, so no edge is ever lost) can show this half. It is inferred, and the
field run is its confirmation. The old tree did show a third face of it in
Bochs: an AUX byte read by `int 09h` as a scan code, a stray `2` (`key 0332`)
typed into the DOS window after a return.

**Built** - put the controller in a known state on both sides, at the
layer that owns each moment:

| where | what | kernel |
|---|---|---|
| `hbm_dosrun` step 6 | with a PS/2 pointer live: IRQ12 masked, `D4 F5` (reporting off, as POST leaves it), its 0xFA read behind any queued packet (64 bytes at most), then `mouse_unhook`, then every AUX byte still in the buffer dropped (a keyboard byte is left for int 09h) | both (HIBER.DRV) |
| `hbm_wake` `.nowz` | the device's 0xFA to its 0xF4 collected with IRQ12 still masked | both (HIBER.DRV) |
| `hbm_kstrand`, end of step 3d | a byte in the buffer whose line has no request latched (OCW3 IRR, after 64 polls for the controller's own lag) read and dropped, up to four - whatever the program or a package left | both (HIBER.DRV) |
| `mega_kdrain` | the buffer emptied straight after an ICW1: in `mega_r286` when the BIOS re-initialised the 8259 on a reset, and in `mega_dropr` after the drop's own | kern_mega (`.text`, real mode) |

Dropping a byte read straight after an ICW1 is exact: no request stands
behind it to go spurious, so one key or one packet byte is lost where leaving
it lost both devices for the session.

**Cost, measured at one commit (build 869ba7ce's tree with and without the
change, artefacts compared byte for byte)**: HIBER.DRV +139 bytes of code
on both kernels (`hdl_call` 0x1A2C -> 0x1AB7 on kern_big, 0x25E3 -> 0x266E on
kern_mega), inside its padded image (7,169 / 10,241 bytes, unchanged), the
packed file 5,889 -> 5,998 and 8,312 -> 8,421. **No resident byte on
kern_big**: `kernel.bin` and `kernel.sys` byte-identical. kern_small has no
HIBER.DRV and every hunk is inside `KERN_BIG` or `KERN_MEGA` (by the diff).
kern_mega `.text` +22 (`mega_kdrain` 15 and two calls) - real-mode code, so
it cannot go to `.mega` - and `tests/megaseg.txt` 4,323 -> 4,345 with that
reason; `kernel.bin` the same length.

**Measured on**: Bochs 2.7 (a 386, imps2 mouse) on kern_mega. Base: KBTRIP
6/12 trips no key; Test Drive III dead inside the game on trip 2 of a run.
Fixed: KBTRIP 12/12, then the row three times. Its first run, with the 0xF5
wait bounded at 16 bytes, read a key with ASCII 00 ahead of the `k` on two
trips of ten (the scan code was not printed yet; plausibly Bochs's controller,
which QUEUES aux bytes where a real 8042 has none, still holding more than 16
of them); at 64 bytes, 20 trips of 20 read `k` first; Test Drive III 6/6 round
trips with the pointer moving across each launch, Esc and Y reaching the
game every time and the pointer moving after each return. kern_big rows
under QEMU 9 green: `p2hiber`, `kdreturn`, `kdkbd`, `hmadosret`.

**Row**: `megakbtrip` (new, soak, Bochs, 360 s; 300 measured): ten round trips
of KBTRIP with the pointer moving across each launch; each asserts that `k`
(scan 25h) was the first key the program read, the return, and the pointer
moving after it, and the next trip's typed command is the desktop's keyboard.
Red on the base tree (`--build` on a copy of it): 3 of 6 trips no key, a
stray `2` on the seventh. Also covering: `megap2hiber`, `megahiber`,
`p2hiber`, `kdreturn*`, `kdkbd`, `hmadosret`, `mouresume`,
`hibernate`/`hibernatedrv`, `kdmouse`/`kdnomouse`.

**For the owner**:

- **The 286 half is the field's to confirm.** If a return still comes home
  dead on the Victory, the screen to photograph is the menu bar: nothing
  draws a reading of 64h today. The cheapest instrument would be `KBDDIAG=1`'s
  ring on kern_mega (it records 64h and the PIC around IRQ1), which has never
  been built there.
- **A dropped byte is a lost key or a desynchronised packet**, at the four
  places above and only where the alternative was both devices dead.
- **The device is switched off for the DOS program** (0xF5). A DOS program
  that drives a PS/2 mouse itself (int 15h C2xx) initialises it anyway; one
  that expected it reporting with nobody having said so would see nothing,
  which is what POST would have given it.

### 14.63 Field: the mouse dies when it MOVES DURING THE LIVE RESUME (2026-10-21)

The owner, on the Victory 286 at 343124b2 (14.62's fix): the mouse froze on
the very first return - so 14.62 did not catch the return half - and after
half a dozen more tries, **a solid repro: moving the mouse WHILE THE LIVE
RESUME IS HAPPENING is what kills it on the desktop.** It is 14.62's
finding (the pointer moving across the LAUNCH) in the other direction. **The
keyboard dies with it** (the owner): NumLock does not change the LEDs and
nothing reaches the system - a reset each time. Both devices silent together
is the shared output buffer's signature, not a mouse fault.

The reading: the return re-enables the mouse's reporting (`hbm_wake`'s
`0xF4`) and then makes more BIOS calls, and on a 286 every one is a CPU
reset through the reflection - whose BIOS re-initialises the 8259 on that
path (14.62's inference), erasing a pending edge. A packet byte that lands in
that window is stranded in the 8042's one buffer and both devices go silent.
14.62's one-shot drains (`hbm_kstrand`, `mega_kdrain`) run at fixed moments;
a hand on the mouse keeps bytes coming after them, so a fixed-moment drain
cannot win this. **Being built as 14.64**: a watchdog on the tick - a byte
that has sat in the 8042 for two ticks with its line UNMASKED at the PIC has
lost its edge, and reading it out frees the controller - which covers every
lost edge on the 286's reset path, not only the resume's.

### 14.64 The tick's lost-edge watchdog, and a knob that loses edges (2026-10-21)

14.63's repro - the mouse moved while the live resume runs, and the desktop
comes home with the mouse AND the keyboard dead (the owner confirmed both:
NumLock does not light, only a reset gets out) - is the shared-buffer
signature: a byte left in the 8042 whose edge was erased. 14.62's drains run at
fixed moments, and every BIOS call after them is, on a 286, a CPU reset whose
BIOS can initialise the 8259 under a byte that is waiting. SPEC.md 96.45.2.5
(its fifth place) and 9.9.9 are the contract.

**Built: `mega_kwd`, at the end of `mega_t08`** (IRQ0's ROM half, after its
EOI; `.mega`, kern_mega only). One `in al, 0x64` on an ordinary tick. When a
byte IS waiting it is STRANDED if its line is unmasked (IRQ1 at the master; for
an AUX byte IRQ12 at the slave and the cascade at the master), the master has
no request latched for it (OCW3's IRR - bit 1, or bit 2 for the cascade, which
is also where an edge lost at the MASTER alone shows while the slave still
asserts), nothing is in service at the master (OCW3's ISR, then IRR selected
again), and the same was true on the tick before (`[ticks]` banked in
`.lowbss`). Two ticks, because the controller sets OBF before it raises its
pin; and a request that IS latched is delivered the moment the tick's iret
puts IF back, so it cannot still be there a tick later.

**Re-delivered, not dropped.** The byte goes through its line's own gate, as
the lost edge would have sent it: `int 21h` is `kbm_isr` on this kernel
always (`mega_ivt_sync` rebuilds from 22h; IVT 09h holds the BIOS's half for
reflections), and `int 74h` is whoever owns IRQ12 - `mou_p2_isr`, or a package
that hooked it. Their EOIs are no-ops, the tick's own having gone and the ISR
having read zero. An AUX byte whose IVT 74h vector is the ROM's (its gate only
EOIs, or reflects) is read and dropped. So no key is lost and no packet
desyncs, and a tick that misjudges a byte - one in the lag window on both
ticks - only delivers it early: `mou_p2_isr` then finds the buffer empty when
the late edge arrives. The keyboard's is the one case with a cost
(`mega_k09` does not test OBF, so a late IRQ1 would read the stale data
register again: a doubled scan code); on real hardware the lag is
microseconds and the two-tick rule makes it a product of two such windows.
Each re-delivery writes `kern_mega: 8042 edge lost, 64=ss` to E9.

**What it must not touch, checked**: every polled 8042 sequence that runs in
protected mode holds its own line MASKED - `hbm_wake`'s keyboard enable (IRQ1
masked), its knock and `0xF4` (IRQ12 masked at the slave), `hbm_dosrun`'s
`0xF5` (IRQ12 masked), `mega_dwake`'s `mega_d8042put` (every line masked) - or
runs inside an ISR at IF = 0 where no tick can land (`mega_leds`,
`mega_kack`, inside IRQ1). `mouse_init`'s probe and `mou_p2_init` are boot
code, before protected mode, when `mega_t08` is not the chain. kern_dos owns
the machine on the park arm and our tick is not installed there; a
reflection runs the BIOS's own int 08h, not `mega_t08`. No package or driver
in the tree reads port 60h (grep), and in protected mode IRQ1 is `kbm_isr`
whatever a package writes into IVT 09h, so an fsx bracket that "owns the
keyboard" owns it through the kernel's ISR anyway.

**kern_big and kern_small do NOT get it**, by reasoning rather than
measurement: neither resets the CPU on a BIOS call (kern_big's 286 path is real
mode with LOADALL), so the only 8259 initialisations they meet are POST's and
the DOS trip's, both at fixed moments that 14.62's `hbm_kstrand` already
covers. If the owner's kern_big ever shows this face, the same code fits
`sch_isr` behind `[cpu_tier]` (an XT's 64h is no 8042) - ESTIMATED at about
100 resident `.text` bytes - and that is the decision to bring back.
Both kernels are byte-identical: `kernel.bin` and `kernel.sys` compared at one
commit with and without the change; every hunk is inside `%ifdef KERN_MEGA`
or the knob.

**Cost (measured, kern_mega, this commit with and without)**: `.mega` 15,204
-> 15,368, **+164** (`mega_kwd` 130, its E9 string 31, the call 3); `.lowbss`
+2 (`mega_kwdt`); `.text` and `.bss` +0, so `tests/megaseg.txt` stays at
4,345 and nothing moves in the 64KB segment. The scratch line after `.mega`
in os88megaseg's report moved 1,490 -> 1,486, alignment.

**The knob, `PICLOSE=1|2`** (Makefile, stamp-tracked, in `$(KNOBS)`;
`t_buildmatrix` rows `piclose` and `piclose-nowd`, which also assemble
`KERN_MEGA` itself for the first time there). In `mega_svc_lo`'s real-mode
half, which both CPU paths reach (`.rm`: the 386's far jump and `mega_r286`),
it waits up to 65,536 polls for a byte to be in the 8042 and 8,192 more for
its line to rise (Bochs raises it on the controller's next timer, not with
OBF), then sends ICW1-4 to BOTH 8259s at the protected-mode bases and puts the
masks back - so `mega_r286` would see nothing moved and no `mega_kdrain`
follows. `2` is the same with `mega_kwd` compiled out (`-DNO_KBDWD`). **The
window is the half that matters**: the first build of the knob sent the ICWs
the instant the reflection reached real mode, and six trips of the red leg
all survived - a byte has to be WAITING at the initialisation, and on the
Victory the reset path is long and at IF = 0, where the knob's first cut was
a few instructions.

**Measured on Bochs 2.7** (a 386, `type=imps2`), `tests/megapiclose.py`:

| tree | trips | result |
|---|---|---|
| PICLOSE=2 (no watchdog) | 1 of 6 | **dead on trip 1**: the pointer did not move after the return, and the next command typed never reached the DOS window - both devices, as on the Victory (three runs, each dead on trip 1) |
| PICLOSE=1 (watchdog) | 6 of 6 | every return clean; **5 and 7 bytes re-delivered** in two runs, the first run's five all `64=19` - a KEYBOARD byte (AUXB clear), plausibly the `k`'s break arriving across the return, and not a mouse byte (the second run's log was not kept) |
| shipping (no knob) | 10 of 10 | `megakbtrip` green, **0** re-deliveries with the pointer moving across every launch: no false positive in 300 s of motion |

Recovery is by construction the second tick after the strand: 55 to 110 ms.
`megap2hiber` and `megahiber` (Bochs) green on the shipping tree. **86Box
`286-mega`** (the MR BIOS 286, the real reset path, serial mouse): boots to
the desktop, the pointer and the keyboard work (a click and two arrow keys
in a Disk window), and `faults` shows only the reflections' own deliberate
resets - so the watchdog's `int` from a 286's protected-mode tick is sound
there. 86Box cannot show the defect (its ICW1 rebuilds IRR from levels), so
the knob was not taken there.

**Not reproduced: a BIOS-served disk.** The other place the knob would strand
a byte is any reflection, and the watchdog covers them all; but Bochs's C: is
HDD.DRV's native IDE rung, so opening it with the pointer moving makes no
`int 13h` reflection at all (E9 says none) and the red tree survived it. A
machine whose C: is BIOS-served (rung 0) is where that face would show.

**Rows**: `megapiclose` (new, soak, Bochs, 450 s; 383 measured with both trees
built), and covering: `megakbtrip`, `megap2hiber`, `megahiber`. The kern_big
rows (`kdreturn*`, `kdkbd`, `hmadosret`, `p2hiber`, `mouresume`, `kdmouse`,
`ps2mouse`) cannot see this change: their kernels are byte-identical.

**For the owner**:

- **The Victory is the confirmation.** If a return still comes home dead
  with the pointer moving, the watchdog did not see the byte as stranded - a
  photograph of the menu bar is no help (nothing draws a reading), so the
  cheapest next instrument is the E9 line on a machine with a port E9 card,
  or a `mega_tell` line (one more byte of `.bss`: `mega_tellq` has no bit
  free).
- **The serial mouse has the same class of failure and this does not cover
  it.** A UART that holds IRQ4 high with a byte unread after an ICW1 is
  dead the same way (the comment above `mega_svc`'s `.imrkeep` records it).
  The 286-mega profile and the owner's pc5150 are serial; a serial arm would
  be the same shape (LSR's data-ready, IRR bit 4 or 3) on the same tick.
- **`mega_kdrain` and `hbm_kstrand` still DROP what they find.** They could
  now be left to the watchdog (nothing lost rather than one byte lost), but a
  drop at a fixed moment is the conservative half of the pair and was left as
  it was.

### 14.65 The debug toasts out of the shipping build: `MEGADIAG=1` (2026-10-21)

The owner, preparing an early-access release: *"The debug toasts that are all
over mega need to go; keep a 'Entered Protected Mode' toast on startup, but
none of the added disk status debug toasts, speaker toasts, etc."* Built: every
field reading kern_mega put on the glass is behind one knob, **`MEGADIAG=1`**
(`-DMEGA_DIAG`, stamp-tracked, in `$(KNOBS)`; `make megadiag` builds the
disks into `build/megadiag/`; `t_buildmatrix` rows `megadiag` and
`megadiag-hb`). Nothing was deleted outright: every reading below is how the
Victory 286 was debugged off photographs (items 14-25, 14.34), so the field
keeps a disk that has them.

#### 14.65.1 The inventory

Searched: `kernel/mega.inc`'s `mega_tell` and its eight `[mega_tellq]` bits,
every `cw_toast_show`/`toast_say` inside `%ifdef KERN_MEGA` or in
`mega.inc`/`megados.inc` (a nesting-aware scan of `kernel/`,
`drivers/`, `kerndos/` and `apps/dos/`), every string with a hex placeholder,
and the drivers. **The drivers have no kern_mega toast at all** - they are
the one build shared by every kernel, and HDD.DRV's *"a fixed-disk transfer
went to the BIOS (int 13h reflected)"* is `mega_mhd13`, an E9 line from the
kernel. No speaker or Sound Blaster toast exists either: the "sound" lines
were `RFL` and `Q`, which fire while a PicoMEM's Sound Blaster plays.

| message on the glass | raised in | when | verdict |
|---|---|---|---|
| `protected, 286 reset` / `protected, 386 CR0` | `mega_enter`'s tail (`mega_mtoast`, 8 s) | once, at boot, entering protected mode | **KEEP** - the owner's startup toast, wording as it was |
| `real mode: 8086-class CPU`, `... no 82KB for kern_mega's tables`, `... A20 will not open`, `... a task predates it` | `mega_gatef` (`.cold`) | at boot, when kern_mega declines protected mode and runs as kern_big | **KEEP** - the machine is not what the disk says, and this is the only place it is said |
| `NAME vv at cccc:iiii` | `mega_fkdo` (14.42) | a package was closed because it faulted | **KEEP** - a program vanished; kern_big's equivalent is the stop screen |
| ` kern_mega FAULT vv err eeee at cccc:iiii sp ssss - reboot ` | `mega_report` | a KERNEL fault: the machine stops | **KEEP** - the stop screen |
| `Use this disk's DOS.O88` | `mega_pkgok` | kern_big's DOS.O88 started on kern_mega | **KEEP** - a refusal |
| `DOS.O88 is kern_mega's` | `apps/dos/dosload.asm` (`DOS_MEGA`) | kern_mega's DOS.O88 on any other kernel | **KEEP** - a refusal |
| `No room above 1MB`, `Could not save to disk`, `Hibernation: other mode` | `kernel/hiber.inc` (`KERN_MEGA`) | the DOS park's and hibernate's refusals (14.43) | **KEEP** - refusals |
| `The machine was restarted while it ran.` | `apps/dos/dos.asm` (`DOS_MEGA`, W7) | a park recovered after a warm reset | **KEEP** - says why the program's work is gone |
| `NMI ss xnn cccc:iiii` / `NMI ss W llllll nn` | `mega_nmi`, drawn by `mega_nmitell` | a latched parity or channel-check NMI (item 14) | **MEGADIAG** - see 14.65.5, the one to look at |
| `RST 3E aa>bb 3F cc>dd` | `mega_r286` | the first 286 reset of a boot that moved the floppy's BDA bytes | **MEGADIAG** |
| `FDC RESET R nn F nn` | `mega_fdrprobe` | the 765 found reset behind our back | **MEGADIAG** |
| `RFL rrrr/s uuuu` | `mega_rfsec`, once a second from the tick | 20+ option-ROM IRQ reflections in a second - a PicoMEM SB playing - every 15 s | **MEGADIAG** ("speaker toasts") |
| `Q cc ss oo bb vv rr` | `mega_rfsec`, after RFL | the PicoMEM's request queue, the same second | **MEGADIAG** |
| `DOSP kkkkK o tttt b tttt` | `mega_dwake` (`megados.inc`) | every return from a DOS park | **MEGADIAG** |
| `FD BIOS ss s0s1s2 Dmm` | `mega_fdjudge` | the first native floppy fallback of a boot | **MEGADIAG** ("disk status") |
| `native floppy off: BIOS from now` | `mega_fdjudge` | eight fallbacks in a row switched the native path off | **MEGADIAG** - its E9 line stays |
| `H ssss:oooo bbbbbbbb rrr` | `hbm_d1why` (`kernel/hiber.inc`) | the DOS handoff record read back wrong | **MEGADIAG** - the shipped kern_mega says kern_big's `Not our handoff record` instead, so the refusal stays and only its hex goes |

E9 (`out 0xE9`) is invisible on real hardware and is NOT a toast, so every
E9-only line stays on both arms: `kern_mega: protected mode`, `mega_mhd13`,
`above 1MB`, `native floppy ...`, `floppy fell back ...`, `string restart`,
`8042 edge lost`, `parked`/`unparked`/`resumed`, MEGATRAP's `T` lines. The
copies of the menu-bar lines that `mega_nmitell.say` also wrote to E9 (with
`[mega_rstn]`, the reset count) go with the lines. No row read them (a grep of
`tests/` and `tools/` for every string and for `mega_tellq`, `mega_rf*`,
`mega_pmq`, `mega_fd*`, `mega_nmi*`: nothing).

#### 14.65.2 What moved under the knob

Not only the drawing: the bookkeeping that feeds nothing else. `mega_tell`
keeps bit 6 (the fault close) and nothing more, so `[mega_tellq]` has seven
bits free on the shipped build (14.64's "no bit free" is MEGADIAG's now).
Under `%ifdef MEGA_DIAG`: `mega_nmitell`, `mega_hexb` and the six line
strings; `mega_rfsec`, `mega_pmpre`/`mega_pmpost` and `mega_pit`, with the
tick's once-a-second divider and `mega_svc`'s PIT timing and PicoMEM sampling
around an IRQ reflection; `mega_replay`'s replay count; `mega_r286`'s BDA
comparison and `[mega_rstn]`, and the two `[mega_fdpre]` stores before a
reset (`mega_svc_lo`'s and `mega_dosgo_lo`'s); `mega_fdrprobe`'s counter and its
bit (the probe and its recalibrate are behaviour and stay); the NMI's CS:IP
and DMA snapshot (the latch-clear and `MEGA_NMIMAX`'s shut-off are behaviour
and stay); `mega_dtell`, `mega_dec4` and the DOSP string; the two floppy
toasts and their strings; `hbm_d1why` and its three hex printers; and the
42 `.bss` bytes only those read (every one of the `.bss` figure below). `kernel.asm` refuses `MEGA_DIAG`
without `KERN_MEGA`, and the Makefile passes it only with `KERN_MEGA`, because
`make megadiag`'s tree builds a kern_big beside its kern_mega as every
`mega.img` tree does.

#### 14.65.3 Bytes, measured

kern_mega, this tree with and without the change, one commit (nasm maps):

| section | before | after | |
|---|---|---|---|
| `.text` | 47,168 | 46,888 | **-280** |
| `.mega` | 15,368 | 14,448 | **-920** |
| `.bss` | 5,336 | 5,294 | **-42** |
| `.cold`, `.lowbss`, `.ovl`, `.boot2`, `.vgabuf` | | | +0 |
| HIBER.DRV (`.modh`) | | | **-109** of code, image still 10,241: it sits in `MODH_UNPAD`'s dead zone, so the claim is the same 11 KB |

The 64KB segment's kern_mega overhead falls **4,345 -> 4,023** (-322, the
`.text` and `.bss` above), and `tests/megaseg.txt` is lowered to it with the
reason. The `MEGADIAG=1` kernel's sections are byte-for-byte the sizes of the
kernel before this change (same `.text`, `.mega`, `.bss`, `.modh`), so the
knob puts back exactly what was there.

**kern_big and kern_small are BYTE-IDENTICAL**, measured: both built from an
export of HEAD and of this tree (no `.git`, so both at build 0), md5
`90cb1a0e...` (kern_big) and `41bb478e...` (kern_small) on both sides; and the
in-tree `build/kernel.bin` is `46fbeb79...` before and after. Every hunk is in
`mega.inc`/`megados.inc` (kern_mega only), inside `%ifdef MEGA_DIAG`, or
`hiber.inc`'s `.b1`, whose non-MEGA_DIAG arm is the same two instructions it
was.

#### 14.65.4 Measured, on what

- **Bochs**, the rows on this tree: `megadosp` PASS (0 / 0 / 0 / 2 traps,
  as before), `megaflt` PASS (six closed; its shot shows the KEPT fault toast,
  `FLTF 06 at 27D4:0096`), `megarehome` PASS, `megatrap0` PASS (13 packages,
  4 trap lines, the calibration's), `megahiber` PASS, `megavidbig` PASS (2MB
  bank and 16MB held). **`megakbtrip` FAILS 1 trip in 10, and so does the
  base**: three runs here read 9 of 10 (trips 6, 10, 1), and two runs of an
  export of 2222752e - the commit this is cut from, nothing of 14.65 in it -
  read 9 of 10 as well (trips 1 and 3), every miss the same `KBTRIP key 4300`
  (scan 43h, F9, where `k` was sent). It is not this change's: the key is
  read by the DOS program while kern_mega is PARKED, and nothing 14.65 gates
  runs between the handoff and the return. It is on the books for whoever
  owns 14.61-14.64 (14.64 recorded 10 of 10; a6a3ab5a, the commit before the
  base, changed the trip's mouse ACK wait).
- **The DOS trip, both arms, on the glass** (Bochs, a scratch driver of
  `megakbtrip`'s shape: boot, open B:, KBTRIP.COM, a key, home, then the bar
  every 1.5 s): the shipped kern_mega shows `protected, 386 CR0` for its
  eight seconds and then nothing but the menus and the clock, before and after
  the trip; the `MEGADIAG=1` disk shows the same boot toast and then
  **`DOSP 0076K o 0009 b 0002`** for ten seconds after the return. That is the
  A/B: the knob puts the line back and the default has none.
- **86Box `286-mega`** (MR BIOS 286, the 286 RESET path Bochs never takes):
  boot watched every 2 s for 80 s - `protected, 286 reset` for its eight
  seconds and then a bare bar; then B: opened (a floppy read on the native
  path), MEDIA opened, and BEVERLY.MOD played on its SB16 for 40 s watched
  every 2 s: **nothing on the bar but the menus and the clock**. The
  `MEGADIAG=1` disk booted the same way also shows a bare bar - its readings
  fire on events 86Box does not make (a PicoMEM's option-ROM IRQ, a BIOS that
  moves the floppy's BDA bytes across a reset), which is why they were the
  Victory's, and why the DOS trip is the A/B.

#### 14.65.5 For the owner

- **The KEEP list is above, and the one to veto is the NMI line.** A parity
  or channel-check NMI is a hardware fault, and on kern_big the AT BIOS's own
  handler reports it (and halts). kern_mega handles it - clears the latch,
  shuts the sources off after eight - and the shipped build now says nothing.
  It went with the knob because its text is a hex reading; if a machine with
  bad RAM should be told, the cheap form is one fixed sentence on bit 0
  (`mega_tell` already serialises it) for ~40 bytes of `.mega` and a string.
- **The startup toast keeps its words**, `protected, 286 reset` / `protected,
  386 CR0`: the way back it names is what a field photograph is for. Reworded
  to the owner's *Entered protected mode* it is a string edit (TOAST_MAX is
  24) if that is preferred.
- **A field run that needs a reading gets `make megadiag`'s disk**
  (`build/megadiag/mega.img`, or `mega120.img`; `megarun.o88` and `mbr.bin`
  are built beside it so `tools/os88bochs.py --build build/megadiag` boots
  it). The handoff document (A.4) and `docs/BOCHS.md` say so now.

#### 14.65.6 Rows

Covering: `megadosp`, `megaflt`, `megarehome`, `megatrap0`, `megahiber`,
`megakbtrip`, `megavidbig`, `megapiclose`, `megap2hiber`; `buildmatrix`
(`megadiag`, `megadiag-hb`) for the knob's arm. No row asserted on a removed
notice, so none changed. `megakbtrip`'s 1-in-10 is the base's (14.65.4). kern_big
and kern_small rows cannot see this change: their kernels are byte-identical.

### 14.66 kern_mega's package ARMS: what a shipped package may carry for it, and the gate (2026-10-21)

The owner: *"Adding 5kb to all shipping video.o88s won't work; we can replace
the video.o88 on the mega apps disk as needed, special app builds are ok.
That is true of all mega changes and I thought we had a gate for it - mega
specific work that more than trivially increases the disk size is gated to
mega builds only. Can you do a survey and make sure that is right?"* **There
was no such gate and no kern_mega apps disk** - `make mega` said "pair them
with the SHIPPED build/apps.img" - and DOS.O88's `DOS_MEGA` was the one
package with a kern_mega-only build. Both exist now; SPEC.md 20.18 is the
contract.

#### 14.66.1 The survey

Every shipped `.o88`/`.drv` was built at 98266265 (protected-mode's branch
point from elendilon), at 7feb6f7f (elendilon's version 2 player, the first
parent of the 2e77a97e merge) and on this branch, and measured as file bytes
/ unpacked image (`os88pkg`/`os88drv.image_unwrap`); the growth is
attributed from `git log -p` on each package's sources: (a) kern_mega-
specific, (b) a shared fix or feature, (c) trivial. "Before" is 2e77a97e's
build against 98266265; "after" is this branch's SHIPPED build against it.

| package | before | after | attribution |
|---|---|---|---|
| VIDEO.O88 | +6,486 file / +3,295 image (+3,110 / +1,789 against 7f), with part 1 `vblkpm.bin`: 5,023 bytes, 1,798 packed | **47,328 / 58,730: the file 7feb6f7f shipped, to the byte; the image 62 smaller**; part 0 `vblk.bin` md5 `d8097675ab7a284c94a47570dfd37af5`, 7f's | (a) part 1; `[vp_pm]`'s in-place reads through `OSAPI_XMEM_RUN`'s selectors; the ring above 1MB (14.41); `vp_pmbigq` (14.58); the 8-paragraph grid; `OSAPI_MEM_CODESEL`; the DMA-door claims; the speaker library's PM ISRs - **all moved to the arm**. (b) kept: 14.16's rewrites that write nothing through CS (`vd_walk`'s colour in its frame, `vdec.inc`'s list map in DX - by its own count ~50 cycles a frame FASTER on an 8088, and self-aligned by `VD_PAR`; `vp_lrget`, `vp_khdr`, `vp_rbnd`, `vp_bcb`), and five `call vp_mclaim` sites, 2 bytes smaller each |
| AUDIO.O88 | +365 / +912 | +12 / +15 | (a) the speaker library's PM copies (os88spk.inc's second ISR family, os88spkfx.inc's level and emit copies, ~900 bytes) - moved. (c) the 15 left are the fail-safe: `os88spk_go` refusing under `CPU_F_PM` |
| TRACKER.O88 | +506 / +994 | +22 / +18 | (a) the speaker library as Audio's, `trk_claimsel`, `mp_patseg`'s flat arm, the selector tests - moved. (b) the 128-byte granule addressing of samples and patterns, which any kern_mega run needs, low or high - kept |
| MIDIRACK.O88 | +512 / +852 | +0 / +238 | (a) the speaker library, `mwt_voicep`/`mwt_pmset` (the wavetable loop's PM copy), the selector limit and flat placement - moved. (b) 14.55/14.59's ring claimed at Play and freed at every stop, its refusal sentences, and `mwt_place`'s offset-kept placement - kept |
| PIXEL.O88 | +2,048 / +135 (parts +3,028 unpacked) | +1,536 / +135 (parts +357) | (a) 14.40's 290 `PXSW` stores and 70 `PXSWON` runs through ES = `PXP_SELF`, ~2.5KB of the parts' image: the macros spell `[cs:` in the shipped build now (a `%define es cs` inside them) and the arm rebuilds the seven parts that use them. (b) the parts' grid fixes and `PXP_SELF`/`PXP_PKGD` in the part header - kept. The file's +1,536 is three 512-byte part sectors |
| TASKMGR.O88 | +747 / +823 | same | (b) SPEC.md 28.4.8's IVT+BIOS (EBDA) accounting, 28.4.9's 640K-1MB map, `OSAPI_XMEM_WALK`'s above-1MB rows (kern_big's store has them too). (c) `tm_pm`, ~80 bytes, kept on purpose: the copy on kern_mega's system disk reports kern_mega's memory truthfully (tests/megapkg.txt) |
| SKIES.O88 | +522 / +522 | same | (c) the code grew 108 bytes (cs_xpand: `OSAPI_MEM_SUBSEG`'s ~25 and the granule-aligned sum every kernel takes); the 522 is a part crossing a sector |
| HIBER.DRV (kern_big's module) | +423 / +838 | same | (b) every addition live on kern_big is a shared fix - `hbm_kstrand` (14.62's stranded 8042 byte; kern_big's DOS trip has it too) and `hbm_psum` (SPEC.md 87.4.1's sum over the programs) - checked by walking hiber.inc's live lines with `KERN_BIG` against 98266265's. Its kern_mega code is `%ifdef KERN_MEGA` already, and kern_mega's HIBER.DRV is a build of its own |
| HDDTOOL.DRV | +309 / +512 | same | (b) the installer's refusal saying what it tried (10.3 items 27 and 54), a field diagnostic for every kernel's installer |
| PAINT.O88 | +120 / +116 | same | (b) the BMP rows and the move on the 8-paragraph grid; (c) the flat arm for a selector canvas, tens of bytes |
| 1942.O88 | +0 / +99 | same | (c) two `OSAPI_MEM_CODESEL` calls (DX itself in real mode) and the scroll's grid |
| XMEM.DRV | +48 / +57 | same | (b) `xm_walk`, the pool walk the Task Manager's rows read on kern_big |
| DOS.O88, SOUND, HDD, RAMDISK, DESK, TANK | +38, +35, +14, +18, +12, +27 file | same | (c) DOS has had its arm since KERN-MEGA-DOS-PLAN 12.1; the drivers' `OS88_DRV_HIGHOK` bit, `SBL_DSCS`, `rd_fold`'s selector test and HDD.DRV's `hd_xa_arm` refusal are tens of bytes each |
| REALDMG.O88 (new, merged during this work) | - | - | no kern_mega code: no marker, no `PKG_MEGA`, and it exempts itself from both class bits (tests/megahigh.txt, 199124f1) |

#### 14.66.2 The mechanism

- **`PKG_MEGA`** (`apps/os88mega.inc`): a package assembles its
  kern_mega-only code under `%ifdef PKG_MEGA`, which no shipped recipe
  passes. `$(MEGAPKGS)` (Makefile, beside `MEGADIR`) is the list - VIDEO,
  AUDIO, TRACKER, MIDIRACK, PIXEL - each built by a `$(MEGAAPPDIR)` rule into
  `build/megak/apps/`. Not `build/megak/` itself: the kern_mega kernel's
  sub-make builds `build/megak/tracker.o88` there for its icon pipeline,
  from the plain recipe.
- **The disks**: `build/megaapps.img` (1.44MB) and `build/megaapps120.img`
  (1.2MB) are `$(APPSARGS)` with `MEGASUB` substituting every arm, and `make
  mega` builds them. `mega.img`/`mega120.img` take the same substitution
  over `SYSTEM/` and the core set (no arm there today, so the same bytes).
  `make megapkgs` prints each arm against its shipped build.
- **The fail-safe, per package**, because nothing stops somebody putting the
  ordinary apps disk in a kern_mega machine:
  - VIDEO refuses to START (`OS88_MEGA_REFUSE`: `Use the MEGA apps disk`):
    its every borrow is LOADALL or a loaded base. Its shipped class byte
    declares neither bit.
  - AUDIO, TRACKER and MIDIRACK refuse the speaker PCM TIER (`os88spk_go`,
    CF=1 under `CPU_F_PM`) and play another way. TRACKER's and MIDIRACK's
    shipped class bytes drop `OS88_HIGHMEM` - what reads a selector claim is
    the arm's - and keep `OS88_HIGH`; AUDIO keeps both (none of its own
    code moved).
  - PIXEL runs, its parts' stores through CS completed by `mega_gpw` -
    right, and slow, which is the arm's reason.
- **`VP_GRAN`/`VP_GOFF`** keep the Video Player's 20 granule sites one line
  each; `VP_OWNB` is the CS store again in the shipped build; one spare byte
  where `vp_m3c` was keeps `vp_ring` and `vp_rdseg` at 7f's parity, and
  `tools/os88rdiff.py` against 7f reports nothing hot moved by an odd amount
  outside the decoder's own self-aligned rewrite.

#### 14.66.3 The gate

`tests/unit/t_megapkg.py`, FAST (1.6s): every shipped build's live source -
the shipped recipe's defines, through every `%include`, a macro's body where
it is invoked - may name a kern_mega marker (`CPU_F_PM`,
`OSAPI_MEM_CODESEL`, `OSAPI_MEM_SUBSEG`, `OSAPI_XMEM_RUN`, `OS88_CODESEL`)
per file exactly as often as `tests/megapkg.txt` says, with a reason, the
registry turning one way; no shipped `.o88` may take a part assembled with a
kern_mega define; a build whose live graph names `PKG_MEGA` (or invokes a
macro that does) has its arm, whose rule passes `-DPKG_MEGA`; and a current
`build/megaapps*.img` carries the arms. **It is by NAME**, so the coming
renumbering of kern_mega's cells after main's merge cannot move it. Red on
2e77a97e six ways (VIDEO's, VIDEOD's and VIDEOR's vblkpm part; the live
`CPU_F_PM` sites of video.asm, os88spkfx.inc and midirack.asm); red with one
`%ifdef PKG_MEGA` pair taken from `os88spkfx_init`; red with AUDIO out of
`$(MEGAPKGS)`. Blind to an arm's code that names no marker (os88spk.inc's
second ISR family is only data-selector stores): `make megapkgs`' sizes are
where that shows.

Also changed: `t_megahigh.py` reads an armed package's class byte off the
ARM and holds its shipped build to declaring no more; `t_megagrid.py` and
`t_cswrite.py` exempt the `%ifndef PKG_MEGA` arm as they exempt `%ifndef
KERN_MEGA`'s.

#### 14.66.4 Measured, on what

- **The arms are BYTE-IDENTICAL to 2e77a97e's shipped packages** -
  video.bin, audio/tracker/midirack.o88, PiXEL's seven parts and pixel.o88 -
  so every kern_mega row tests what it tested before, from a new path.
- kern_big on MartyPC: `vidplay`, `vidv2`, `vidxip` ok; `vblkemu` and
  `vblkemu --pm` (unicorn) ok.
- kern_mega under Bochs 2.7: `megavidv2` with the arm, 0 traps on every leg
  of all three layouts, the tails identical, 338s; `pxmega` with the arm, 0
  traps on every sample, 189s; the SHIPPED `VIDEO.O88` swept on kern_mega:
  `MEGARUN: no 04` - refused, no fault.
- 360KB disks, from the file sizes at 1KB clusters: apps360 5 clusters freed
  (VIDEO 50 -> 47, TRACKER 27 -> 26, AUDIO 10 -> 9), 334 of 354 in use;
  media360 1 (MIDIRACK 31 -> 30); office360 none (PIXEL stays 110).
- Kernels untouched: nothing under `kernel/` changed.

#### 14.66.5 Rows, and for the owner

Rows: `megapkg` (fast, new), `megahigh`, `megagrid`, `cswrite`, `pkgdeps`;
kern_big `vidplay`, `vidv2*`, `vblkemu`, `vblkemupm`, `vidxip`, `vidbank`,
`vidfskeys`, and the Tracker, Audio and MIDIRack speaker rows for the
shipped side; kern_mega `megavidv2`, `megavidbig`, `pxmega` (these three
take `build/megak/apps/` now), `megahigh`, `megatrap0`, `megarehome` for the
arm.

For the owner: (1) the shipped Video Player REFUSING on kern_mega is a
choice - a shipped player that ran there would need the very PM paths this
removes; (2) 14.16's decoder rewrites stayed shared, because reverting them
means two copies of the hottest code in the player, and they are faster on
an 8088 too; (3) what is left in PiXEL's parts (+357 unpacked) and Paint
(+116) is the 8-paragraph grid, which any kern_mega run needs, low or high,
and the Task Manager's `tm_pm` (~80 bytes) is kept so its system-disk copy
tells the truth there - each is a line in tests/megapkg.txt that a later
call can lower.

### 14.67 main into elendilon: the API tail renumbered, and four new packages through the kern_mega gates (2026-10-21)

`origin/main`'s sixteen commits (INSTALL 117, System Check 118, Lemmings 116,
WireChat 113.x, WireFleet 114.x and SERIAL.DRV 115, Gorillas' opponent, The
Wire's cell fence 92.2, vidsel, checkdocs, the section citations, the guide)
merged into `elendilon` at 2e77a97e, and the moved `elendilon` (RealDMG,
14.65) merged on top. What was decided, so it can be looked back over:

**The API tail.** Both sides had appended cells at the SAME offsets. main's
keep theirs - `OSAPI_SERIAL` 0x049F, `OSAPI_DRV_USE` 0x04A5 - because main is
upstream and this branch goes there as a PR. kern_mega's six move up after
them in their own order: `OSAPI_XMEM_SEL` **0x04AB**, `OSAPI_MEM_CODESEL`
**0x04B1**, `OSAPI_XMEM_WALK` **0x04B7**, `OSAPI_MEM_HUGE` **0x04BD**,
`OSAPI_XMEM_RUN` **0x04C3**, `OSAPI_MEM_SUBSEG` **0x04C9**; `osapi_table_end`
0x04CF, 113 rare cells, 1,215 bytes (SPEC.md 2.6's paragraph and kernel.asm's
guard). **Moved again at the second main merge (2026-10-22, the
PR #299 review):** main's `OSAPI_GFX_BLITD` (SPEC.md 120.7) had taken 0x04AB,
so the six follow it by the same rule - 0x04B3, 0x04B9, 0x04BF, 0x04C5,
0x04CB, 0x04D1, `osapi_table_end` 0x04D7, 1,223 bytes - and `PKG_FMT` stays
for the same reason. That merge also leaves `VID_EXT` off kern_mega
(kernel.asm, SPEC.md 120.3): VESA.DRV is a real-mode driver writing through
CS, and `[vx_ep]` a seg:off protected mode cannot far call. Every offset this document quotes for those six above this section is
the number before the merge. **`PKG_FMT` is not bumped**: nothing main or a
release ever built calls a moved cell, so all of it still runs; what breaks is
a package built from `elendilon` BEFORE this merge that calls one of the six -
test disks in development, which are rebuilt. `tools/abilock.py` recorded it
without a bump, and **the owner confirmed it** (2026-10-22: *"No format bump
is fine. We're not released yet."*). The Wire's
`WIRE_TABLE_END` stays 0x04A5: it fences the released level, and the six are
past it, so a Wire record whose package calls one is refused at `--verify`,
which is the fence doing its job. **kern_big's bytes**: built at one commit
against the same tree with the two groups in the other order, the image
differs at `.text` 0x04A1-0x04CE (the cells, whose `call` displacements are
position-relative) and nowhere else outside the boot blob; footprint +0
against the baseline on both kernels.

**What the union broke.**

1. **`apps720.img` needed 722 of 713 clusters** - main built it at 710 with
   WireFleet's 47 on it, and this branch had grown six packages by 12
   (VIDEO.O88 +7, the version 2 decoder above 1MB; AUDIO, MIDIRACK, SKIES,
   TASKMGR and TRACKER one each). WireFleet leaves the 720KB apps disk, a
   dated SPEC.md 24.6.1 curation: it has a 720KB disk of its own (`make
   wirefleetdisk`), its rows boot that disk, and every other apps disk and
   `games360.img` still carry it - DrMarco's shape. An owner call. 14.66's
   arms, merged after, took VIDEO.O88 down 3 clusters; the disk is 669 of
   713 without WireFleet and would be 716 with it, so the call stands -
   put to the owner with the disk's listing. **The owner's pick (2026-10-22):
   WireFleet goes back and `OS8088.V88` comes off** - 101 clusters of data
   the 720KB SYSTEM disk already carries in its own `MEDIA/`, so the pair
   loses nothing. 615 of 713. A third primary 720KB disk, for what the 720KB
   pair leaves off, is the owner's next call.
2. **SERIAL.DRV wrote its own image through CS thirteen times** (`.mods`,
   `srl_open` and `srl_close`), which faults in protected mode on a real 286.
   All thirteen are `CSDO` now: byte-identical off kern_mega, +7 bytes each
   on it (91 bytes of the module image, nothing resident). **Verified under
   Bochs**: with WireFleet started above 1MB, COM2 answered absent and COM1
   OPENED - every one of the converted stores, the UART programmed, IRQ4
   unmasked - with zero traps (markers in `srl_open`'s exits, taken out
   again). What could not be checked: a second machine on the cable, so no
   byte crossed the link and `srl_pump`'s receive path never ran.
3. **`drivers/hdd/ipkg.inc`** points ES at `KERNEL_SEG` for INSTALL's log
   proc. On kern_mega 0x0060 is the kernel's CODE selector - readable, and
   `in_onlog` only reads through it - so it is registered in
   `tests/cswrites.txt` with that reason rather than converted. A driver has
   no way to name `MEGA_SEL_KDATA`, which is `inst.inc`'s os88drv.inc
   decision again.
4. **`t_install` failed every first `make` of a clean tree**, refusing when
   no `build/small*.img` exists - which a plain `make` never builds. That
   half is skipped with a note now and runs wherever a kern_small disk does.

**A kern_mega defect, found by WireFleet.** `mega_imghif` is handed
`[ld_need]`, which `ld_alloc` SATURATES to FFFFh for a region of a full 64KB
(image + bss rounding past 63KB: 64 << 10 is 0 in a word). Its `inc cx` made
the block, the limit and the copy all ZERO bytes, so such a package ran from a
pair that held none of its image and faulted before its entry proc's first
instruction (`CLOSED 0D at 2854:0000`, the launch answered 04). WireFleet's
44,128 + 20,896 is exactly that, and `tests/csprobe` (41,085 + 23,924, already
OS88_HIGH) was the same case unnoticed. 0 after the round-up now means
10000h: a 64KB block, limit FFFFh, a 65,534-byte copy (`mega_lincopy`'s
ceiling; the last two bytes are past image + bss <= FE00h). +12 bytes of
`.mega`, above 1MB; `.text` and `.bss` +0, kern_big and kern_small untouched.

**The new packages and `t_megahigh`.** Each was read for paragraph arithmetic
on its own segments or claims, CS writes, IVT stamps and BIOS or DMA on a
claim, then launched on a `MEGATRAP=1` kern_mega under Bochs (16MB) through
MEGARUN with everything else running:

| package | verdict | under Bochs |
|---|---|---|
| INSTALL | OS88_HIGH \| OS88_HIGHMEM: no claims, no segment arithmetic; the engine is HDDTOOL.DRV's | window up, 0 traps. The device scan (Next) was NOT driven |
| System Check | OS88_HIGH \| OS88_HIGHMEM: two claims, both under 64KB, filled through `OSAPI_FILE_READ` | window up, 0 traps |
| WireFleet | OS88_HIGH \| OS88_HIGHMEM: its DMA claim is conventional by construction (the DMA door), its pin passes CS and `mem_movable` translates the alias | 0 traps once `mega_imghif` was fixed; HIGHMEM alone was clean before it |
| WireChat, Gorillas | already declared; main's changes add no segment work | 0 traps |
| Lemmings | `highmem` and `high` lines in tests/megahigh.txt | not run: its data is DMA Design's and not in this tree |

Lemmings' `highmem` line is concrete (`ovl_far`'s `base + (hi << 12) + (lo >>
4)`, `lem_far_add`, the part reads at `seg + p * 4032`); its `high` line is
"what would settle it", one MEGATRAP run with the data present, since
nothing in the code computes on its own region. RealDMG arrived with its two
lines and calls none of the eight renumbered cells (the C SDK reaches
`OSAPI_MEM_HUGE` through its `%define`, so a rebuild is all a C package
needs).

**Rows.** `megahigh`, `megatrap0`, `megarehome`, `megavidv2` for kern_mega;
`install`, `syscheck`, `lemmings`, `wirechat`, `thewire`, `wirefleet*` for
main's packages on this tree - all 13 green (`os88soak.py -k`, three lanes;
`megavidv2` failed there on "a Bochs is already running" and passed alone,
0 traps and both tails identical, because `os88test.py`'s Bochs display pool
(:110-:117) is per PROCESS, so two sessions' runners hand out the same
display and refuse each other). Gorillas' seven rows and RealDMG's were not
re-run: neither package's source is changed by the merge.

### 14.68 main's CD-ROM on kern_mega: CDROM.DRV, CDRack and the Mixer, above 1MB (2026-10-22)

The merge at 1bdec716 brought upstream's CD-ROM (SPEC.md 119: `CDROM.DRV`
and CDRack) and the Mixer (34.15: `MIXER.O88` and SOUND.DRV's menu-bar
volume), none of it written for protected mode. The owner: *"Kern mega is
going to run on systems that might actually have a cd drive. And verify they
work high and set them. Make machines to test as needed."*

**What was broken, and how badly.** Measured on 86Box's mr286 with the
pre-fix drivers (`tests/megacdrom.py --cdrom` / `--sound`):

1. **The CD-ROM was a kern_mega FAULT.** `cd_packet` folded DI's paragraphs
   into ES after every DRQ block (`es += di >> 4`, `and di, 15`). Off the
   8-paragraph grid that is no selector (3.1, 14.19): the first data phase
   in protected mode loaded ES = 9CF4h, `#GP 9CF4 at 26B4:02F5` - `[loadseg():
   Bigger than LDT limit]` - and `kern_mega FAULT 0D ... reboot`. Under
   Bochs the same fold happened to land on a GDT index and did not fault,
   which is the silent half of 14.19's description: it read into whatever
   that descriptor named. `cd_bnorm` (cdiso.inc) did the same to the FSV
   read's destination.
2. **Every wait trapped.** `cd_tmo_set`/`cd_tmo_up` read the BIOS tick
   through `xor ax,ax / mov es,ax / [es:046Ch]` - segment 0, the NULL
   selector, on every poll of every command. mega_null0 COMPLETES that
   (a #GP and a #DB each, and `mega_ivt_sync`), so it ran and looked like it
   worked; the fault log shows the train (`26B4:00E5`, `26B4:0100`).
   tests/seg0.txt carried it as an M1 gap.
3. **The menu bar's quick volume opened nothing.** `volume_popup` accepts
   only the kernel (`cmp ax, KERNEL_SEG / jne .out` on ES), and on kern_mega
   `ui_task` hands it ES = its DS, the data alias `MEGA_SEL_KDATA` = 10h.
4. **Neither package was on kern_mega's system disks**: the merge put
   `$(SYSOPTIONAL)` on kern_big's disk recipes only.

**The fixes, zero kernel bytes on every build.**

- The tick is **`OSAPI_GET_TICKS`** - an inline cell, `mov ax,[cs:ticks]`,
  every register but AX and the flags kept - and not 0:046C. Ten bytes a
  site became five, and no segment register is touched. 0040:006C would
  also have been right (mega_t08 advances the BDA's tick in protected mode)
  but costs a byte a site; `[ticks]` is the same clock on every kernel.
- `cd_packet` **walks the offset only**. Every caller already keeps
  offset + capacity <= 65,536 - `cd_bytes` hands it a destination folded to
  under 128 bytes and asks for at most `CD_RUN` x 2,048 = 32 KB, and the
  rest read into the driver's own image - so the fold was doing nothing a
  real-mode caller needed either. Deleted.
- `cd_bnorm` folds in **whole 128-byte granules** (`and al, 0F8h`, `and
  [cd_boff], 127`), `dskw_norm`'s kern_mega arm. A buffer above 1MB never
  reaches a redirector (`drv_fs_call` bounces a pool selector through
  `dsk_secbuf`, `drv_fs_hi`), so a granule fold of an identity segment is
  the whole case.
- `volume_popup` takes ES **at or below 60h** (`ja` for `jne`): 10h and
  0060h, never a package's segment or a pool selector. Zero bytes.
- CDRack and the Mixer declare **`OS88_HIGH | OS88_HIGHMEM`**: read for
  arithmetic on their own segments (none - CDRack's bss is `os88_image_end +
  n` offsets, the Mixer's `mx_*` cells), claims (neither makes one, so
  HIGHMEM is INERT for both, 14.52.2's twenty-one), CS writes, IVT, BIOS
  calls and DMA (none: every port is the driver's). `t_megahigh` passes,
  74 packages, both bits on 67.
- `$(MEGASYSOPTARGS)` puts both on `build/mega.img` and `build/mega120.img`
  in `SYSTEM\`, beside the drivers they need.
- tests/seg0.txt loses `drivers/cdrom/atapi.inc`'s line.

**CDROM.DRV is NOT declared `OS88_DRV_HIGHOK`**: its sector buffer and
packet are in its own image and handed to `cd_packet` as DS, which above
1MB is a pool selector - fine for the offset walk, but nobody ran it there,
and the driver is 9 KB of conventional memory the owner can weigh. It is the
obvious next step and the row below is how to take it.

**Bytes.** kern_big, kern_small, kern_emu, kern_mega: **0** - no kernel
source changed. CDROM.DRV: image 9,059 -> **9,033** (-26), file 5,404 ->
5,381. SOUND.DRV, CDRACK.O88, MIXER.O88: 0 (one opcode byte; the class
byte). kern_mega's system disks: +14,458 bytes, 858 -> 887 clusters (of
2,847 and 2,371).

**Measured, on what.** Every step on BOTH emulators and three CPUs:

| machine | what ran | result |
|---|---|---|
| Bochs 2.7, 16MB, `build/megacdsim.img` + `build/cdfix.iso`, SB16 | CDRack from `A:\SYSTEM` and from `D:\TRACK03.CDA` (the file read through `drv_fs_hi`'s bounce into the granule fold); its buttons, list and toggles; the D: listing; `BIG.BIN` (100,001 bytes, partial sectors both ends) copied D: -> B: | CDRack **LDT 2844h, base 140000h**; BIG.BIN byte-identical to `os88cdiso.pattern()`; MINES.O88 launched off the CD; the quick volume opened and **Open Mixer...** started the Mixer at **base 140800h**, its CD channel's level and balance set; no `kern_mega FAULT` |
| 86Box mr286, 16 MHz 286, 4MB, SB16, `vm/286-mega-cdrom` | CDRack relaunched in protected mode; Play, Next, Pause, Resume, the volume slider (CDV_VOLUME, the MODE SELECT out-block); the quick volume, Open Mixer..., the CD channel | CDRack **282Ch, base 140000h**, the disc PLAYING and counting; Mixer **2844h, base 144800h**, `mx_mask` 01FFh; the fault log: the two reflections' own resets and **nothing else** |
| 86Box micronics386, 25 MHz 386DX, 2MB, `vm/386-mega-cdrom` | the same | CDRack 282Ch, Mixer 2844h, playing; no exception outside the BIOS |

The autostarted CDRack is LOW (`9640h`): `\AUTOSTRT`'s walk (`as_boot`)
runs before `mega_enter`, so every autostarted package is real mode and
conventional on kern_mega. Not a CDRack property - written down because it
reads like `OS88_HIGH` being ignored.

**The machines.** `vm/286-mega-cdrom` is `vm/286-mega`'s mr286 with
`ide_isa_2ch`, an ATAPI CD-ROM on 1:0 holding `build/cdaudio.cue`, and
`build/megacdtest.img` / `build/megaapps.img` in A: and B:.
`vm/386-mega-cdrom` is `vm/386-cdrom` with the same two disks. `make
megacdtest` builds `build/megacdtest.img` (kern_mega's system disk, a
SYSTEM.CFG wanting SOUND.DRV and CDROM.DRV, and `cdaudiotest.img`'s two
autostart links) and `build/megacdsim.img` (the same with the
`-DCDSIM_AUDIO` driver, for Bochs). Two harness fixes came with them:
**`tools/os88box.py` resolves a CD-ROM's image path** against the profile
(it rewrote only floppies and hard disks, so 86Box looked for
`../../build/cdaudio.cue` in the run directory and silently dropped the
drive - `386-cdrom`'s disc never reached a container run) and takes `--cd`;
**`tools/os88bochs.py` takes `--cd ISO`** (ata1-master, 170h) and **`--a
IMG`**. The MR BIOS wants its floppies set once from a fresh CMOS (docs/86BOX.md);
the micronics386's IBM-style setup takes up/down between fields and
PgUp/PgDn within one, `Set Diskette Unit 0` and `1` to 3.5".

**Row.** `megacdrom` (soak, 86Box, 34 s measured): a fresh CMOS set up by
the row, the boot, CDRack relaunched above 1MB and playing (`cr_play`,
`cr_secs` read through its pool base), the quick volume's Mixer above 1MB
reading the card, and no exception charged to a loaded image. Red with the
pre-fix CDROM.DRV (nine image exceptions, the machine stopped) and with the
pre-fix SOUND.DRV (no Mixer).

**Found and not fixed.** RAMDISK.DRV's `rd_fold` and NET.DRV's `net_norm`
fold a CONVENTIONAL buffer's offset into its segment by paragraphs, the
shape item 1 was - and on kern_mega the kernel hands a redirector the
`dskw_norm`'d buffer, offset 0..127, so any offset from 16 up builds an
off-grid ES. `drv_fs_call`'s comment (*"where a redirector's paragraph
arithmetic is right"*) holds for `dsk_secbuf` only, which is 512-aligned.
Not run here; tests/unit/t_megagrid.py scopes to `apps/` and could take
`drivers/` too.

### 14.69 Open after the main merge, for the next round (2026-10-22)

Three items 14.68 found and did not take, written down as work rather than
left in its prose. The owner asked for each to be carried here.

- [ ] **RAMDISK.DRV and NET.DRV fold paragraphs into a segment.**
  `rd_fold` (drivers/ramdisk/) and `net_norm` (drivers/net/) fold a
  conventional buffer's offset into its segment by paragraphs - the shape
  14.68's item 1 was in `CDROM.DRV`, which faulted the machine on its first
  read. On kern_mega the kernel hands a redirector the `dskw_norm`'d buffer
  at offset 0..127, so any offset from 16 up builds an off-grid ES: `#GP`
  with RPL bits, or SILENTLY an LDT alias of other memory (3.1, 14.19).
  `drv_fs_call`'s *"where a redirector's paragraph arithmetic is right"*
  holds for `dsk_secbuf` alone. Neither driver has been run on kern_mega.
  The fix is 14.68's: walk the offset, or fold in whole 128-byte granules
  (`dskw_norm`'s kern_mega arm). `tests/unit/t_megagrid.py` scans `apps/`
  only; taking `drivers/` too is what keeps the next one out.
- [ ] **`CDROM.DRV` above 1MB.** It runs on kern_mega now but loads LOW -
  9 KB of conventional memory - because nobody has run it high. To take it
  there: declare `OS88_DRV_HIGHOK` (drivers/os88drv.inc, item 82); check its
  sector buffer and packet, which live in its own image and reach
  `cd_packet` as DS - a pool selector above 1MB, which the offset-only walk
  should already tolerate; check that nothing hands its own segment to the
  BIOS or a DMA engine (it does PIO, `rep insw`); then extend `megacdrom` to
  assert the driver's image base is above 1MB while CDRack plays and copies.
  `OS88_DRV_HICLAIM` is not wanted: it makes no claims.
- [ ] **Autostarted packages run low.** `\AUTOSTRT`'s walk (`as_boot`,
  SPEC.md 26.10) runs before `mega_enter`, so every autostarted package
  starts in real mode, in conventional memory, whatever `OS88_HIGH` says -
  `megacdtest`'s autostarted CDRack sits at `9640h` while the same package
  relaunched by hand runs at base 140000h. It reads like the class bit being
  ignored. The fix is an ORDERING one: walk `\AUTOSTRT` after the switch on
  kern_mega (a re-homed package already moves up after it, 14.15), or post
  the walk to `ui_task` the way a re-home is serviced. Whichever is taken,
  check that a link which launches a real-mode-only package still works, and
  that the boot's own driver mounts are not reordered under it.

### 14.70 RAM actually used, and the Task Manager that shows it (2026-10-22)

The owner's screenshot of the Victory 286's Task Manager (4MB, WireChat and
the Task Manager open) read `RAM 0.4/4.0M`, `HEAP 0.0/3.5M`, a second
`HIGH 421/3456K` bar, and a System row of 439K of which `Above 1MB` was 374K.
It reconciles to the KB: the Victory's chipset REMAPS the 384K behind
640K-1MB to the top, so its BIOS states 3,456K above 1MB where an 86Box
machine with the same 4MB states 3,072K and the 384K is simply lost; System
is 57K code and data + 3K stacks + 5K FAT snapshot + 374K; HEAP's size is
4,096 - 3 - 439 = 3,654K, shown truncated as 3.5M. The owner's verdict is that
the figures are right and the PICTURE is wrong, and that the 374K is mostly
not memory the kernel needs. Two threads, recorded separately.

#### 14.70.1 RAM actually used - for a later round

What `Above 1MB` holds on that kernel: the HMA (64K), the GDT (64K), the LDT
and IDT (18K), `.cold` and `.mega` (about 58K), about 50K of padding up to
the pool's 64KB-aligned base (`MEGA_XPOOL`, 0x140000), and about 118K of
pool blocks owned C0h and up.

- [ ] **Purgeable is not implemented on kern_mega.** Item 84 moved the
  kernel's caches into the pool (the window save-unders, up to 63K each, the
  menu save-under, the icon store, the file list, the clipboard) and item 86
  the FAT windows, and its own words are *"A pool block has no `mem_tab`
  record, so it is never shed, moved or counted."* On kern_big those claims
  carry a purge tier: the Task Manager leaves them out of System (SPEC.md
  28.4.1) and `mem_claim` sheds them before it refuses. On kern_mega
  neither happens - they are billed to System and NEVER dropped, so a
  package's claim can fail with caches standing that kern_big would have
  shed. The owner's read of the 118K (ETHER.DRV's ~34K with WireChat
  online, HDD 4K, SOUND 8K, the rest caches) is this. The shape: a purge
  flag in `MX_FL` (the byte has room), set by `mem_hitag`'s placements;
  `mx_alloc` sheds flagged kernel blocks before it refuses; the Task Manager
  excludes them from System as it does on kern_big.
- [ ] **The GDT is a 1:1 table sized for every possible segment, and almost
  none of it is used.** Selector = segment value, so the GDT's size is set
  by the HIGHEST segment anything loads (the scratch selectors at FFD0-FFF8,
  the ROM font at F000), not by how many are used. **Measured** under Bochs,
  kern_mega at this tree with SOUND and HDD up and Calculator, Mines, Note
  Pad, Paint, Sheet, WireChat, the Task Manager and Tracker all started: of
  8,192 entries, **106 have the Accessed bit set - 848 bytes of a 65,536-byte
  table** (the kernel writes 0x92/0x9A, so the CPU's A bit is a true record
  of a load). Of those 106: 23 in conventional memory, 1 at A000, **78 in
  C000-EFFF, every one of them the Task Manager's own ROM scan** (`tm_romscan`
  steps 2K and loads ES at each), and 4 kernel selectors at FFD0-FFF8. The
  owner's suspicion of "4-8K max" holds with room to spare. Dropping the 1:1
  design means a selector is ALLOCATED for a segment a package or the kernel
  names, the way the LDT's dynamic entries already are - the size of that
  change is the work to cost: every place that turns a segment value into a
  selector by identity (and `mega_oddsegf`'s window, which already handles a
  segment that is not a multiple of 8 dynamically, is the model).
- [ ] **The LDT can start small and grow.** 2,048 entries (16K): 1,280 are
  a STATIC code alias for every 512 bytes of conventional memory (index =
  segment / 32, which is how a CS with bit 2 set names its image), the rest
  dynamic; **31 accessed** in the same run. Regrowable by claiming a bigger
  block and reloading LDTR; the static aliases go dynamic the same way the
  GDT's identity entries would. The owner: *"LDT regrowable: seems good."*
- [x] **The IDT stays 2K.** Every vector can be reflected to the BIOS or to
  a DOS program. *"IDT is fine."*
- [ ] **The HMA is RAM.** It has to be free at 1MB only while DOS uses it,
  so it is pool like any other until then, and the trip into DOS saves what
  is in it and the return restores it to the same linear addresses (a pool
  block is named by its linear base, 13.4) - what the park already does for
  conventional memory. The owner prefers this to parking droppable caches
  there, which would cost a program 64K, and on a 1MB machine that matters.
- [ ] **The padding.** About 50K between `.mega`'s end (~0x133600) and
  `MEGA_XPOOL`, there only because the pool's arithmetic wants a base whose
  low word is 0. Pack the fixed area and start the pool on a finer
  boundary, or put a fixed structure in the gap.
- [ ] **"DOS box" is the wrong name and keeps coming back.** DOSBox is an
  open-source emulator; os8088's DOS is a clean implementation of DOS, not an
  emulator, and is unrelated to it. A round to purge the word from code
  comments, strings and documents.

#### 14.70.2 The Task Manager - this round, both kernels

The owner's design, on kern_big and kern_mega alike (the small build has no
memory page and is unchanged; check afterwards that the BIG Task Manager
copied onto a small disk still runs, which it does today). **BUILT on branch
`taskmgr-merge`** (SPEC.md 28.4.10, 28.4.11, and the corrections in 28,
28.4, 28.4.8, 28.4.9); the boxes say what each line came to.

- [x] **One bar on kern_mega.** RAM is RAM, so the second bar and the `HIGH`
  line belong to kern_big's separate XMS store only. `HEAP 0.0 of 3.5M`
  beside a 400-odd K bar of almost the same thing was the confusing part.
  BUILT: `tm_init` sets `[tm_xoff]` under protected mode exactly as on a
  machine with no store, so the XMS line and bar are not drawn and the
  header, the rows and the frame rise into their room.
- [x] ~~**The summary line is `R` and not `RAM`**, which frees the two
  characters that let every figure on it carry a SECOND decimal in MB -
  `0.47/4.00M`.~~ **SUPERSEDED by the owner mid-round**: on kern_mega the RAM
  and HEAP pairs are ONE pair labelled `Ram`, used/total in MB to two
  decimals (`Ram 0.37/15.62M`, Bochs), no `R`, no `H`. The HEAP pair was
  RAM's own less the system's share on both sides; the memory page's `AVAIL`
  line is where the heap is read now. Conventional-only machines keep the
  split `RAM`/`HEAP` pairs in whole KB, and kern_big with its XMS store keeps
  its split view (RAM in MB to one decimal, HEAP the conventional heap) -
  *"the store is not RAM a program runs in."* Still no mixing of K and M on
  the line: *"mixing and matching k and m is too confusing."*
- [x] **One page for RAM and heap, with collapsible groups.** Each owner -
  System, each program, each driver, the purgeable caches - is one row with
  its total; a click expands it into its claims. An **Expand all** button
  shows every claim at once. The heap page's scrolling (SPEC.md 28.4.4)
  carries the length. Purgeable claims are their own group, shown as
  reclaimable and not as System. The owner: *"I love the expandable claims
  idea."* BUILT: `[tm_view]` 0/1 (the heap page is gone, `TM_NVIEW` 2); one
  row per owner with its total and legend square, opening into
  TYPE/ADDR/SIZE/TIER rows; `+ALL`/`-ALL` at the header's right end; the
  list scrolls; `Purgeable` is a group of every FBh-FEh claim with its tier,
  a raise cache included (SPEC.md 28.4.5's filing under the window's package
  is retired for it). The heap page's two caption lines are one `AVAIL nnnK
  MAX RUN nnnK` line.
- [x] **Two inner views with a toggle: Programs and System.** Programs bills
  each package with System as one row; System itemises the kernel below 640K
  and above 1MB - the HMA (`HMA (DOS)`), the tables, the kernel's code, the
  padding, the kernel's caches and **each driver by name** (*"which driver
  holds what is very useful"*). The HMA and padding rows go away when
  14.70.1 makes them RAM. The kernel names its fixed areas through
  `OSAPI_XMEM_WALK`, as marker records the Task Manager already knows how to
  read. BUILT: the toggle is the header's left end, lettered inverted
  (`PROGRAMS`/`SYSTEM`); SYSTEM lists `IVT+BIOS`, `Code+data`, `Stacks`,
  `FAT snap`, then above 1MB `HMA (DOS)`, `GDT`, `LDT+IDT`, `Kernel code`,
  `Padding`, then `Kernel`, one group per driver by file name and one per
  built-in, then `Programs` and `Purgeable`. Bochs, kern_mega with SOUND and
  HDD: HMA 64K, GDT 64K, LDT+IDT 20K, Kernel code 58K, Padding 50K, Kernel
  32K, SOUND.DRV 12K, HDD.DRV 4K. The kernel half is `OSAPI_XMEM_WALK` with
  BX = `XMW_FIXED` (five negative-offset marker records, `mega_xmfix`) and
  the class byte `XMW_CLS` on a driver's block (`mega_xmcls`): **+149 bytes
  of `.mega`** (14,460 -> 14,609), `.text` +0, kern_big and kern_small
  byte-identical but for the build number; additive, so `abi.lock.json` was
  refreshed with no `PKG_FMT` bump (SPEC.md 28.4.11).
- [x] **640K-1MB rows that add up.** The screenshot's read 128 + 32 + 32 + 144 +
  64 = 400K of a 384K window: `Unrep.` read 144K where the code computes 128.
  Not found by reading `tm_romscan` and the row loop; to reproduce on
  `vm/286-victory` (the Paradise PVGA1A's ROM at C000). **NOT REPRODUCED,
  and one real defect fixed.** `vm/286-victory` under 86Box reads Video 128,
  ROM C000 24K, ROM C680 6K, Unrep. 162, BIOS 64 = 384, and Bochs 128 + 38 +
  154 + 64 = 384. `Unrep.` is 192 less the very ROM figures printed beside
  it, so the code cannot print 128 + 32 + 32 + 144 from one scan - the
  photograph's ROMs are not 86Box's (a 32K PVGA1A image and a second 32K
  ROM) and stays UNEXPLAINED until it is retaken on this build. The defect:
  `tm_romscan` trusted a ROM's length byte, so a ROM reaching past F000 was
  billed into the BIOS's 64K and the scan's segment could WRAP past FFFF
  into conventional memory and list ROMs again. Its KB are clamped at F000
  and the scan stops there (SPEC.md 28.4.9).
- [ ] **Maybe: dump to file**, only if it is cheap in bytes. ESTIMATED, not
  built: the rows are composed into `tm_str` and painted by `tm_mrow_close`,
  so a dump is a switch there that appends the row to a buffer instead of
  painting, a pass over every group expanded and unclipped, one
  `OSAPI_FILE_WRITE` of a fixed name into `SYSTEM/APPDATA/`, and a menu
  item: about 150-200 bytes of image, plus the buffer (claimed for the
  dump, not bss).

**What it cost the package** (measured, `make` at this branch):
`taskmgr.o88` image **8,554 -> 9,418** bytes (+864) and bss **1,935 ->
2,385** (+450) - heap while the window is open, nothing resident. The
`APP_SMALL` build is **byte-identical** (`build/smallapp/taskmgr.bin`, image
3,808, bss 1,325).

#### 14.70.3 Left for the second round (2026-10-22)

The merged page landed (`b6c4a1ed`, `2cd6ddc7`). Open:

- [ ] **AVAIL / MAX RUN on kern_mega count the conventional heap only**
  (`AVAIL 573K` under `Ram 0.37/15.62M`). Designed, not built, in
  `taskmgr.asm` alone: keep `OSAPI_XMEM_CAPS`'s free KB as `[tm_hifree]`;
  compute `[tm_himax]`, the largest run one claim gets after `mx_alloc`'s
  compaction, by `mx_compactf`'s own walk over `tm_xw` (movable owners
  80h-DFh and FDh per `mx_canmove`, pinned FEh/FFh/slots, the F0h-F4h
  markers skipped); under `[tm_pm]` AVAIL = `[tm_avl] + [tm_hifree]` and MAX
  RUN the larger run, in KB to 9,999 and otherwise BOTH in MB to two places
  (`AVAIL 15.04M MAX RUN 14.98M`, 27 cells). MAX RUN quotes what an
  `OS88_HIGHMEM` package can get - every package declares it or is in
  `tests/megahigh.txt`. ~80-120 bytes, estimated.
- [ ] **Dump to file is built and NOT merged**: branch `taskmgr-merge-dump`
  (`c276423c`), List > Save to file writing both views with every group open
  to `APPDATA\MEMDUMP.TXT`, verified on MartyPC. **+380 bytes of image**
  against the 150-200 estimate - the owner's call. It conflicts with the
  AVAIL fix at the bss tail.
- [ ] **The Victory photograph's 640K-1MB rows** (400K of 384K) did not
  reproduce: 86Box `286-victory` and Bochs both sum to 384, and the code
  computes `Unrep.` from the same ROM figures it prints. One real defect was
  fixed on the way (`tm_romscan` trusted a length byte reaching past F000).
  Retake it on this build.
- [ ] Purgeable on kern_mega (14.70.1): until then the kernel's caches above
  1MB show under `Kernel`, not `Purgeable`.

### 14.71 The upstream PR's review: four kern_mega defects, fixed (2026-10-22)

The review of the upstream PR read the protected-mode hibernate and the
fault handler again and a second agent confirmed four defects. None of them
was seen in the field; each is a path nobody had driven. All four are
`%ifdef KERN_MEGA` code, so kern_big and kern_small are BYTE-IDENTICAL
(`build/kernel.bin`, `build/smallk/kernel.bin`).

- [x] **The section's head overflowed past 62 runs.** `hbm_xwrite` writes the
  run list at byte 12 of a 512-byte head, 8 bytes a run, and run 0's bytes
  (the GDT) from byte 512 of the same bounce - so `12 + n x 8 <= 512` holds
  for 62 runs and no more, and nothing counted them: `mx_tab` holds 96
  blocks (`MX_NBLK`) and a full INST_MAX of twelve is ~60. At 63 runs the GDT
  overwrote the list's tail, the hibernate said *safe to switch off*, and the
  resume refused it as `Hibernation: unreadable`. `hbm_xplan` now refuses
  above **`HBX_HMAX` = 62** (`Hibernate failed`, the same CF as no room for
  the staging block), BEFORE the picture, and an `%error` ties the constant
  to the one sector. The head was not grown: that is mega_hbf's 512,
  `hbm_xres`'s `MPK_HDR + 512`, `HB_XMIN_KB` and `[mega_hbxl]`'s start moved
  together, which can follow if 62 is ever met.
- [x] **Chunk 0 on a volume of 512-byte clusters.** `hbm_xwconv` puts chunk 0
  right from segment 0 for one cluster and from 40h:(cluster - 400h) for the
  rest - right from 1KB clusters up. At 512 bytes that offset is FE00h:
  linear **10200h** instead of 200h, and FE00h + 7E00h ran past the
  selector's 64KB. Old os8088 formats of small partitions made such volumes
  (drivers/hdd/fmt.inc), and so do NT-style FAT16 tools at 32MB and under. A
  protected-mode hibernate onto `[dsk_spc]` = 1 now refuses first, **`Disk
  clusters too small`** (`hbm_wrimg`, before `hbm_xplan`). The refusal over
  the split at max(cluster, 400h): the case is rare, and a refusal is one
  compare where the split is a second LOW0 limit and a third write-at.
- [x] **A reported `#GP` left `mega_tcs` set**, and **a reported fault left
  `mega_n0` set.** `.copied` was the CS: prefix index's only reset, so a
  package's CS: read past its limit - reported, the package closed, the
  machine going on - sent the NEXT `#GP(0)` without a prefix to the
  trampoline instead of the DS/ES register fix. `mega_gpw` clears it on entry
  now. `mega_n0` the same way: a stepped instruction that itself faulted was
  reported with it set, and every later null-segment fixup then refused as
  *already stepping one*. `mega_fkill` clears it after its own DS load -
  `mega_report`'s first call and its only caller, which arrives with whatever
  DS the reporter had.
- [x] **NMI on across both of a reflection's transitions.** The way back
  loaded the protected-mode IDTR in real mode and then ran
  `smsw`/`or`/`lmsw` before PE; on a 386 the way down cleared PE and then
  far-jumped before `lidt [mega_rmidtr]`. An NMI in either window reads the
  IDT's vector-1 gate as a far pointer (FFD8:xxxx) and runs ROM bytes.
  `mega_enter` and `mega_pmtail` already mask NMI over their switches; the
  reflection now does too, and over the transitions ONLY - NMI held off for
  a whole int 13h would be 400 ms of it. Port 70h's `0x8D` before the 386's
  CR0 clear and `0x0D` after `.rm`'s `lidt` (the 286's way passes there too,
  already on after `mega_r286`: one redundant write), `0x8D` before the
  `lgdt`/`lidt` back and `0x0D` once DS, ES and SS are selectors again. And
  `mega_gpfix`'s emulated far CALL moved the fault frame four bytes down with
  SP still at the old frame, so an NMI there pushed over the moved ES and DS:
  `sub sp, 4` first.

**What it cost** (`make mega`, os88megaseg): kern_mega's `.text` + `.bss`
**52,874 -> 52,890** (+16, the four port writes), `.mega` **14,609 ->
14,623** (+14), overhead over kern_big **3,440 -> 3,456** of the 4,029
`tests/megaseg.txt` allows. HIBER.DRV gains the compare, the refusal and
its string. The rows are `megahiber` and `megap2hiber` (the hibernate),
`megaflt` (the fault handler) and `megapiclose` (the reflection); none of the
four defects has a row of its own, the first two needing 63 pool blocks or a
512-byte-cluster disk and the last an NMI in a window of a few clocks.
