# MEGA-VIDEO-REALMODE-PLAN - the Video Player's bracket in real mode under kern_mega

**Status: PLAN. W0 (the census) is DONE (2026-10-29); W1 (the drop and
the return) and W2 (the table and the fallback) are BUILT and their gates
pass (2026-10-30, section 4's entries); W3 (the extent reader, the low
drivers, the XMEM twins) and W4 (SOUND.DRV low, the stream and the speaker
door) are BUILT and their gates pass (2026-10-30); W5 (the player) is
BUILT and its gates pass (2026-10-30) - its arm's `VPDIAG` builds no
longer fit `APP_MAX_SIZE`, which W6 needs (W5's entry); W6 is BUILT AND
MEASURED on the Victory: parity (2026-10-30, its entry and
docs/reports/VIDPROF-W6-2026-10-30.md); W7 is BUILT and its gates pass
(2026-10-30, its entry).** The plan was first written against
`4d3d0943` from source and documents only. **W0 measured it again on
`df2e1fbf`**, and section 7 is that census: every `PKG_MEGA` block of the
player classified and assembled, the bracket's real API whitelist walked off
the shipped build, every kern_mega body on it audited against the kern_mega
map, and the claims, the token and the three drivers the bracket touches.
Where W0 disagreed with the first draft, the first draft is corrected in
place and section 7 says why. **Section 5.2 is what W0 owes the owner before
W1 starts.** No emulator, no row and no field run was taken for W0; its
figures are assembler output (`nasm -l`, the kern_mega map) and static call
graphs over the listings, and a figure for code that does not exist yet is
still an ESTIMATE and says so. A bare "14.nn" is a section of
docs/plans/KERN-MEGA-PLAN.md, and a bare "§" is SPEC.md.

**The owner's ask.** *"fullscreen/in-window video player needs a way to 'fall
back' to real mode, so it's not in-and-out and underperforming Big. It needs
to have parity. That may mean it removes 90% of the mega specific video player
code, and it may mean OS changes."* Their standing decisions: kern_mega gets
**parity** with kern_big for video; **no second set of videos** is encoded for
it; and the more capable kernel **must not underperform** the less capable
one.

**Scope, as corrected by the owner.** The unit is the **fsx bracket**
(`OSAPI_FSX_RUN`, §53). Full-screen play and in-window play are both brackets:
the in-window play is a same-mode bracket that leaves the desktop's
framebuffer as it is and decodes into it, so it only *looks* windowed
(§98.3.7). **Live play (§98.3.10) stays as it is**, in protected mode. It is
the one play that runs on a live desktop, through a worker, and it reads the
whole file into RAM before it plays, so it has no disk to be slow on.

---

## 0. The answer, in nine lines

1. **kern_mega already runs a real-mode bracket. It lasts one BIOS call.**
   `mega_svc` / `mega_svc_lo` / `mega_svc_back` (`kernel/mega.inc:2711`,
   `:2834`, `:2994`) leave protected mode, run one `int`, and come back. The
   bracket keeps the 8259 at `MEGA_PIC_BASE` with no ICW1, lets the mouse ISR
   run in real mode, and credits the ROM's ticks to the rate hook. On a 286
   it takes the reset path through `mega_r286` (`:3219`), with the Victory's
   PIC and 8042 fixes. **The proposal is to stay down for the whole fsx
   bracket**: one drop at the bracket's entry and one return at its exit,
   instead of one round trip per `int 13h` and per PicoMEM IRQ.
2. **This is the owner's own deferred fallback**: *"FSX on a BIOS-type disk
   running the player in real mode for the bracket"* (14.35, 14.56).
   KERN-MEGA-PLAN 2.7 first proposed dropping for the bracket, for DOS. The
   DOS trip (`kernel/megados.inc`) built the whole-machine version of it.
3. **The bracket is the right unit because the kernel is already parked
   under it.** §53.1 makes *"the kernel never runs while the mode is
   foreign"* true by construction. Only the UI task is eligible (§53.2).
   What the app may call is a closed list (§53.7). So a real-mode bracket
   has to supply that list and the ISRs, and nothing else.
4. **What does not survive the drop is everything above 1MB**: `.cold`
   (which holds the file layer, `dwf_dskw_read_seq`, `kernel/diskw.inc:3246`),
   `.mega`, the LDT, driver images, on-demand modules, the XMEM pool's own
   copy code - **and, W0 found, the package's own REGION**: the arm says
   `OS88_HIGH`, so `ld_hi` (`kernel/loader.inc:1109`) copies it into a
   code/data pair above 1MB (`mega_pkghif`, `kernel/mega.inc:10216`) whenever
   the pool has room. `mega_coldup` (`kernel/mega.inc:6906`) is what puts
   `.cold` and `.mega` up there. Every selector the package holds is also
   lost. kern_mega's `.text` is in conventional memory at `KERNEL_SEG`, is
   8086 code, and partly runs in real mode already: the `KDS` macro reads
   `[k_dsel]` (`kernel/kernel.asm:262`), and `mega_svc_lo.rmc` sets it to
   `KERNEL_SEG`. **But `.text` also holds SELECTORS in data words** that the
   drop must turn back into segments: `[k_seg0]` (`kernel/kernel.asm:4083`)
   and every video and font segment `mega_vfixall` made a selector
   (`kernel/mega.inc:11766`), which `fsx_mode` hands to the package as
   `FSI_SEG` (section 7.3).
5. **So the kernel must do five things for the bracket** (section 7.3, 7.5):
   * provide a real-mode SERVICE MODULE for the services that live above
     1MB: a pre-walked extent reader behind `OSAPI_FILE_READ_SEQ`, real-mode
     `OSAPI_XMEM_*`, `OSAPI_SND_STREAM`, `OSAPI_FSX_SPK`, `OSAPI_DRV_CALL`,
     `OSAPI_SEG` and `OSAPI_MOUSE` bodies, the IRQ0 rate arm, and an
     API-table swap that also enforces the whitelist - **11 twins and a tick
     for the 38 cells the shipped bracket reaches** (section 7.2);
   * COPY THE REGION LOW for the duration (63,309 bytes for the arm today)
     and call its region proc, as a compaction would;
   * copy SOUND.DRV low, and HDD.DRV and EMS.DRV when the play reads
     through them;
   * turn kern_mega's selector-holding `.text` words back into segments at
     the drop and back again at the return (`[k_seg0]`, `mega_vfixall`'s
     list, `[cpu_feat]`'s `CPU_F_PM`);
   * keep one real-mode IVT arrangement for the whole play.
   **The package renames its OWN claims, at its own door, with
   `OSAPI_SEG`'s `SEG_REAL`** - not through its relocation procs, which W0
   found compute real-mode MOVE arithmetic that is wrong for a
   selector-to-paragraph rename (section 7.4).
6. **The package then runs its SHIPPED (kern_big) bracket code** - its
   `[vp_pm]` cleared at the door, so every run-time dual arm takes its
   real-mode branch:
   * its conventional ring, with no ring above 1MB and no bounce copy up;
   * its LOADALL or `ub_set` borrows for a bank in place - given the block's
     CURRENT linear address at the door, because a token is not one once its
     block has moved (section 7.4);
   * its hook inside the `int 13h` that interrupts it, so the two-frames-a-call
     patch (§98.3.22.6) is no longer needed;
   * the PicoMEM's IRQs served natively.
   That is kern_big's mechanism over kern_big's code, which is what parity
   means here. **It is not literally unchanged**: 15 of the 38 cells the
   shipped bracket can reach cannot be offered in real mode (the heap, the
   decompressor, the drawing slots on a stop path) and their call sites
   leave the bracket in W5 (section 7.2).
7. **Cost, re-estimated on W0's measured comparables**: ~4.8-6.0 KB for the
   module (the API table alone is **1,241 bytes**, not the ~720 first
   assumed, and it is banked as well as replaced), read on demand and kept
   in a conventional claim for the play; ~150-400 resident bytes of kern_mega
   for the PM halves of the drop and return; during a play, conventional
   room for the region (63,309), SOUND.DRV (8 KB) and HDD.DRV (4 KB) as
   well as the module; one reset per bracket on a 286 (14.31 puts the
   Victory's reset path at 3.3 ms). kern_big and kern_small accept the
   flag and ignore it, which changes the door's flag test on both - an
   estimated 0-6 bytes, and neither stays byte-identical (section 3.1.1).
8. **What it deletes from the player's kern_mega arm, MEASURED**: **368 of
   the arm's 1,771 kern_mega-only lines in `video.asm` (630 of its 3,849
   bytes)** - the ring above 1MB, the bracket-only procs' arms
   (§98.3.22.6 among them) and the bank-in-place gates - plus the speaker
   library's protected-mode ISR family (32 lines, 433 bytes of `video.bin`)
   and the shaper's protected-mode copies (49 lines, 476 bytes of part 2).
   Un-parting the shaper (`vfx.asm`, `vfx.inc` and 126 more lines) is
   OPTIONAL: it fits the plain arm and not its `VPDIAG=time` build. That is
   21%, not 55-70%: **1,525 of the 1,771 lines are reached from the
   desktop** - the session's start, the timer's pre-fills, the keys and the
   poster, the info card and Live - and runtime-dual code reached there
   stays (section 7.1).
9. **The gate** is the owner's two files on the Victory: Catch My Heart and
   Sonic 2, encoded for kern_big, with `VPDIAG=time` profiles on both
   kernels. Parity means the bank runs dry no earlier than kern_big's
   (frames 1312 and 733), the picture's median lag is 0, and the
   `MEGADIAG` reflection count inside the bracket is about 0. Re-measured
   that way, `mega_disk` (§98.2.3.8) should come out at 1.00, and the
   `--kernel mega` factor can be retired.

**W0's verdict on 5.1.1 (section 5.2)**: always real STANDS for the loss the
owner named - rung 1's native PIO survives the drop, and the bracket gains
`HDV_ARM` - but W0 found three others, all of them kern_big's behaviour and
not today's kern_mega's: in-place reads under the speaker's clock (MEASURED
in §98.3.18.16: 182 ticks against 375 held, 195 against 403 banked), streams
from FS-driver and redirected volumes, and the desktop's conventional
memory. Two want a ruling before W1 starts.

---

## 1. The problem, as measured

These are field and 86Box figures. None was re-taken for this plan.

* **The disk.** Replayed through the encoder's curve, kern_mega's plays run
  dry where the machine did only at **0.915 and 0.877** of kern_big's disk
  rate: Catch My Heart and Sonic 2, `286-picomem1`, §98.2.3.8. The profile
  carries `mega_disk` = 0.88.
* **The in-place reader's own cost**: another **0.159 ms a KB** on every
  chunk, on top of the copy up's 0.186 (`xrd`, §98.2.3.9).
* **The reflections in one Victory play** with the PicoMEM: **2,161 IRQs in
  77.3 s at 1.0 ms each (2.8%)**, and the services at **38.7% of the play,
  27.1 ms each** (§98.3.22.5). One reflected 32 KB read is the same length as
  kern_big's own call (28.7 against 30.5 ms, §98.3.22.6). **The read is not
  slower. What the read stops is the problem.**
* **What a reflection stops.** Under kern_big the rate hook runs inside the
  `int 13h`. Under kern_mega it is shut out, and the periods come back as one
  call (`mega_rfclk`, `kernel/mega.inc:3170`). Before §98.3.22.6 the picture
  was a median 14-35 frames behind, against kern_big's 0. 14.124 has the
  Victory's timed profiles: Catch spends 65% of the play in reads and 48%
  drawing. On kern_big those two overlap; here they **add**.
* **Four causes were found and fixed in 14.73**: the card's lost block
  count, landings straddling a 64 KB page, the hook starved, and the option
  ROM's line masked. 14.36 fixed the hook's lost periods. Each one was a
  real fix, and each one patches a symptom of being out of real mode during
  the call.
* **The reset.** A 286 leaves protected mode only by resetting the CPU.
  The figures disagree: 412 µs (`docs/reports/LOADALL-VICTORY286-2026-10-14.md`,
  via KERN-MEGA-PLAN section 0 line 2), **1.2 ms a round trip** (`RFL`,
  KERN-MEGA-DOS-PLAN 2.1), and **3.3 ms a PicoMEM IRQ reflection** (14.31, via
  14.124). None of the three is the cost of the reflection's own copy work.
  Whichever is right, it is paid on every read and every card block.

**Machines it does NOT affect**: a 286 whose disk HDD.DRV's rung 1 drives
natively. On `vm/286-video` the mr286's on-board IDE makes 0 resets in a
whole play, and T69 plays at 25 fps on kern_mega (14.73). That machine is
already at parity, and the plan must not make it worse (section 5.2 item 1:
it takes the real-mode bracket too, and keeps its rung-1 PIO).

---

## 2. What already exists to build on

### 2.1 The one-call real-mode bracket (the reflection)

The reflection is in `kernel/mega.inc:2711-3216`. It already solves most of
what a bracket-long drop needs:

* **The PIC stays at 20h, with no ICW1.** An ICW1 loses latched edges, and
  a serial mouse then stays dead for the whole session (the comment at
  `:2814-2821`). In real mode the BIOS's handlers are reached at IVT
  20h-27h, which `mega_enter` copies from 08h-0Fh (`:487`, `:534`, `:12156`).
  The slave stays at 70h.
* **The mouse ISR is bimodal** (14.1 answer 18). In real mode the line is
  left open (`mega_rmaskf`, `:5000`), and `[k_dsel]` holds `KERNEL_SEG`
  (`:2875`).
* **Kernel lines are captured while an option ROM's IRQ runs**
  (`mega_capon` / `mega_capoff`, `:715`, `:764`). What they catch is
  replayed in protected mode (`mega_replay`, `:829`). The PicoMEM's
  multiplexer is the reason: the card raises its Sound Blaster's IRQ as a
  software `int 8+n` from its option ROM's handler (`mega_irqrom`, `:631`).
* **The 286 reset path** is `mega_svc_lo.r286` (`:2858`) out, through CMOS
  shutdown code 0Ah, and `mega_r286` (`:3219`) back. That path carries the
  Victory's quirks: a BIOS that re-initialises the 8259 on the reset path
  (masks compared, `mega_pic_pm`, `mega_kdrain`, 14.62 and 14.64,
  `PICLOSE=1` as the A/B), and a floppy controller reset with nobody told
  (`mega_fdrprobe`). **`mega_r286` already has a second landing**: the DOS
  park's, `cmp byte [mega_drop], 0 / jne mega_dropr` (`:3283`).
* **The return to protected mode is `lmsw` on both CPUs**, followed by
  `lgdt`, `lidt`, `lldt`, the data selectors, and NMI back on
  (`mega_svc_lo` `.go` onward, `:2938-2992`).
* **The speaker** puts channel 0 back to the ROM's rate for the call
  (`:2826`, `:3010`).

### 2.2 The long drop: the DOS trip

docs/plans/completed/KERN-MEGA-DOS-PLAN.md is BUILT. `kernel/megados.inc`
implements it:

* `mega_dosgo` (`:1252`) leaves protected mode through the same reset and
  lands in `mega_dropr` (`:1586`), which puts the 8259 back at 08h and
  enters kern_dos.
* The return is `kd_unpark` (`kerndos/kdpark.inc:23`), then the park stub
  (`kernel/megapark.inc`), then `mega_dwake` (`:1664`). `mega_dwake` does
  `lldt`, `mega_pic_pm`, the parked masks, a forced `mega_ivt_sync`, the 8042
  command byte, and the option ROM's prime.
* It parks all of conventional memory above 1MB (~165 ms each way, an
  estimate in its 2.4) and sums it, because a DOS program may write
  anywhere.

**What the trip teaches this plan, and what it does not:**

* The reset-and-land machinery, the PIC and 8042 sequence for the way back,
  and `mega_dwake`'s list are reusable as they are.
* The park and the sum are NOT needed. Our own package runs, and no hostile
  program does.
* KERN-MEGA-DOS-PLAN 2.2 argued *"not literally the fsx bracket"* for DOS,
  because the trip detaches drivers and takes the screen. Neither applies to
  a video play. The play wants exactly the bracket's frozen desktop, and one
  driver kept.

### 2.3 Extent lists and real-mode readers

The hibernate resume walks its file into an extent list while the file
layer is alive. It then reads that list from a stub with no file system at
all (§87.5). kern_dos is handed such a list too (§96.40.1, §96.40.2).
kern_dos is also the precedent for the kernel's disk layer assembled for
real mode, as a part (`kerndos/`). It costs ~33 KB, which this plan refuses
to pay (section 3.2, A2).

### 2.4 What the player already has

* **The session's claims are movable, with relocation procs** (§98.3.19.2:
  `vp_smov` / `vp_smove`, `vp_rmov`, `vp_pmov`). Every word naming a claim
  holds its base, and every other segment is derived where it is used.
  ~~Those procs are exactly what a selector-to-paragraph rename needs.~~
  **W0: they are not.** They do a real-mode MOVE's arithmetic - `[vp_ring]`
  and `[vp_lyseg]` slid by the base's delta (`video.asm:12349-12366`), a
  resident block's words renamed when they fall inside it
  (`video.asm:10278-10306`) - and under KERN-MEGA-MEMORY-PLAN MC7 the
  protected-mode forms of those words are SEG_SUB selectors or paragraph
  OFFSETS (`video.asm:10649-10653`), so a delta between a selector and a
  paragraph is garbage. Section 7.4 says what replaces it.
* **Every claim the player makes is conventional but two**: `vp_mclaim`,
  `vp_mclhi`, `vp_rclaim` and `vp_rclk1` go through the DMA door
  (`video.asm:23459-23546`, §98.3.18.17), and the parts do too
  (`apps/os88partsbody.inc:668`, `:1722`, `:1726`; the player does not ask
  for `OP_WANT_HIFETCH`). The two that are not: the ring above 1MB
  (`OSAPI_MEM_HUGE`, `video.asm:14047`), which W5 deletes, and the REGION
  (`OS88_HIGH`, section 0 item 4).
* **The bracket already pins its blocks** at the call
  (`apps/video/video.asm:14615-14621`: `vp_rmov`, `vp_smov`, `vp_bset`
  just before `OSAPI_FSX_RUN` at `:14635`).
* **The XMEM token equals the physical address until the block first
  moves**: tokens are chosen where the block is (`mx_tokfit`,
  `kernel/mega.inc:7356`; §50.6.10). The shipped player's LOADALL path reads
  a token as a linear base (§98.3.18.13, `apps/os88la.inc`). **W0: that is
  wrong on kern_mega as soon as the block has moved** - the pool's
  compactor moves token blocks and `mx_tophys` (`kernel/mega.inc:7335`) is
  what translates a token to where the block is NOW; the kernel's own note
  above it says no package reads a token in place on kern_mega. So every
  real-mode reader of the bank needs the translation, not only
  `os88xgap.inc`'s guess (section 7.4).
* **The SHIPPED build refuses at its entry on a protected-mode kernel**
  (`OS88_MEGA_REFUSE`, `apps/os88mega.inc:42`; the header comment at
  `video.asm:50-63`). Every refusal reason it names is a real-mode technique,
  which is why it would be correct inside a real-mode bracket.

---

## 3. The options

### 3.1 A: a REAL-MODE BRACKET (`FSXF_REAL`) - RECOMMENDED

`fsx_run` (`kernel/fsx.inc:180`) gains a flag: CX bit 3, `FSXF_REAL`, and
only with `FSXF_RATE`. On kern_mega the bracket is armed as today, and
then, before the entry proc is called, the kernel leaves protected mode. It
returns to protected mode after the entry proc returns and before
`fsx_restore`. kern_big and kern_small accept the bit and ignore it: they
are already in real mode. §53.1's rule that undefined bits refuse means the
bit has to be defined everywhere in one change. The cell does not move, so
this is a clarified contract with no `PKG_FMT` bump
(`tools/abilock.py --update`).

**What the kernel does at the drop**, in protected mode, in this order.
**Always, for every bracket that asks (5.1.1)**: there is no "only when it
pays" test any more. What is left are STRUCTURAL refusals, and because the
owner's rule is one code path (5.1.1, 5.1.2), ~~**a refused real-mode bracket
is a refused PLAY with its reason (§47), not a protected-mode bracket**: the
deletions of section 7.1 are dead only if nothing falls back to the code
they serve.~~ **Superseded by 5.3 (the bracket is DUAL) and built so by W2:
no reason refuses - each FALLS BACK to the protected-mode bracket and SI
says which (SPEC.md 53.2.3).**

1. **Refuse what the real-mode bracket structurally cannot run**: a
   `TF_SERVICE` task alive (a driver's worker - `kernel/driver.inc:4434` -
   would never be scheduled) or `FSXF_KEEPWORKER` (one task runs); a stream
   on a volume the extent reader cannot serve (section 7.5: an FS-driver or
   redirected volume); an extent list past its cap; and no conventional room
   for the region, the drivers and the module (section 7.4). The player has
   none of the first two (its card stream is DIRECT and EXTERNAL, no driver
   task: `video.asm:16898`, `drivers/sound/sb.inc:1672-1681`).
2. **Load the service module** (section 3.1.1) into a CONVENTIONAL claim.
   On kern_mega `mod_need` moves a module above 1MB (`mod_hi`, `mega_modhif`),
   so this one either opts out of that or is copied low; it runs in real
   mode.
3. **Walk the stream into an extent list**. The package names the file: DX
   is a NUL 8.3 name in its image, or ES:DI the `READ_SEQ` cursor it already
   holds, with `RSQ_VOL` and `RSQ_CUR`. Each cluster run becomes an absolute
   sector run on its unit - a BIOS unit for a `DVK_BIOS` volume, or the
   volume's block driver and handle for a `DVK_DRV` one (`kernel/disk.inc:308`)
   - in a conventional scratch. Fragmented past a cap (an estimated 256
   runs, 1.5 KB) refuses.
4. **Copy the drivers the play reads through low**: SOUND.DRV when the
   session holds a stream, HDD.DRV when the stream's volume is one of its
   rungs, EMS.DRV when the session maps a frame. Each is an `MX_KDRV` pool
   block above 1MB (`drv_hi`, `kernel/driver.inc:4231`). Copy it into a
   conventional claim (512-aligned, so `SBL_DSCS`, `drivers/sound/sb.inc:840`,
   yields DS = CS) and rename every holder of its segment with the walk
   `drv_hi_move` already makes the other way (`cw_mem_disp` /
   `mem_region_reloc`, `kernel/driver.inc:4266-4268`). SOUND.DRV is also told
   with a new verb (`DSV_RMODE`) to swap the segments it stored for real ones
   (section 7.5). The card is halted between brackets (§98.3.7), so no DMA
   is armed against the move.
5. **Copy the REGION low** when it is above 1MB (`OS88_HIGH`), into a
   conventional claim, and call its `OS88_REGION_MOVABLE` proc with DS = the
   new paragraph, as a compaction would - the player's sets `[vp_self]`
   (`vp_reloc`, `video.asm:21654`). **The kernel renames nothing else of the
   package's**: the package has already put real segments in its own words
   at its own door, with `SEG_REAL` (section 3.1.2, 7.4). XMEM blocks are
   not renamed either: a block cannot move while no protected-mode code runs
   (section 7.4).
6. **Turn kern_mega's selector-holding `.text` words back into segments**:
   `[k_seg0]` = 0 (`kernel/kernel.asm:4083`; `vid_setmode` and
   `osapi_mouse` load ES from it), every word `mega_vfixall` made a selector
   (`kernel/mega.inc:11766`: `k_vsa`..`k_vsf`, `vid_tab`'s column,
   `vid_seg`/`vid_rseg`/`vid_tseg`, each display context's three,
   `fsx_mtab`'s rows, the glyph table's) - `mega_vrm`
   (`kernel/mega.inc:5089`) already does two of them for one reflection - and
   `CPU_F_PM` cleared in `[cpu_feat]` (`kernel/cpudet.inc:70`), so the
   inline `OSAPI_CPU_INFO` cell and every driver asking it get the real-mode
   answer with no twin.
7. **Swap the API table** at **`KERNEL_SEG:0x0010`, 1,241 bytes** to
   `osapi_table_end` at 0x04E9 (`kernel/kernel.asm:2281`, `:4027`, the size
   asserted at `:4070`: 44 SLOT, 11 XCELL, 116 rare, 6 JCELL, 1 FCELL and 17
   ICELL cells). It is `.text`, so conventional and writable. Bank it into
   the module and write a real-mode table of the same cell boundaries:
   * the 12 cells that run as they are (section 7.2) left as they are;
   * the 11 twins as `jmp far` into the module - every one of them is a
     cell of 5 bytes or more, and none of the six 3-byte JCELLs is on the
     whitelist;
   * every other cell `stc` / `retf` at its first byte (two bytes: it fits a
     JCELL), or `mov ax, reason` before it where the cell has room.
   This is the whitelist enforced for free.
8. **Drop**:
   * On a 386, clear PE in CR0 and far-jump (`mega_svc_lo`, the CR0 path,
     `kernel/mega.inc:2834-2853`).
   * On a 286, take `mega_svc_lo.r286`'s reset (`:2854`) with a new
     `[mega_drop]` value (`kernel/megados.inc:166`): `mega_r286`
     (`kernel/mega.inc:3219`) already branches on that byte at `:3275`, after
     its mask compare, `mega_pic_pm` and `mega_kdrain`.
   * Land in real mode with `[k_dsel]` = `KERNEL_SEG`, `lidt` the IVT, A20
     left OPEN, and the PIC still at 20h.
   * IVT 20h: the module's tick. IVT 21h: the BIOS's keyboard handler, as
     copied (it fills the same buffer kern_mega's native `mega_i16` reads,
     `kernel/mega.inc:1587`). IVT 24h and 74h: `mou_isr`, which is bimodal
     already. The SB's line: the option ROM's handler where there is one,
     and its software vector (0Dh or 0Fh) to the low copy's `sbl_isr`;
     otherwise IVT 25h or 27h straight to `sbl_isr`. The disks' lines: the
     BIOS's.
   * **The PIC stays at 20h, so a package must never write IVT 08h-0Fh
     itself**: IRQ0 is vector 20h there. The shipped speaker library does
     exactly that (`apps/os88spk.inc:308-311`, `:880-883`), so the arm keeps
     its other path - the DOOR writes the vector, `OSAPI_FSX_SPK` AL = 2
     (`apps/os88spk.inc:341-358`) - and the module's `FSX_SPK` twin writes
     IVT 20h. This is a rule of `FSXF_REAL` for every package, and §53 says
     it. (The alternative is to re-base the PIC to 08h for the bracket, as
     `mega_dropr` does for DOS, `kernel/megados.inc:1586-1612` - which costs
     the ICW1 edge loss `kernel/mega.inc:2814-2821` exists to avoid.)
   * Call the entry proc far, at the region's paragraph, through its
     dispatcher (`PKG_DISP`), with DS = CS = that paragraph, ES =
     `KERNEL_SEG`, SI = the window.

**During the bracket**, the module's tick does `sch_isr`'s rate arm and
nothing else (`kernel/sched.inc:2150-2226`, §53.2.2):

* add the divisor to `[sch_racc]`;
* on a carry, chain the ROM's IRQ0 handler, bump `[ticks]`, and run
  SOUND.DRV's `DSV_TICK` through the low copy - **`snd_tick` itself is
  `.text` but is NOT mode-clean**: it reaches the driver through
  `drv_svc_call`, whose body `drv_svc_call_x` is `.cold`
  (`kernel/driver.inc:2400`), so the module carries that call;
* otherwise send its own EOI;
* call the hook under `sch_rhook`'s three skip rules
  (`kernel/sched.inc:595`), with IF = 1 under the speaker's sample ISR as
  there.

There is no task switch, because there is only one task. Because this
handler is IRQ0 inside the BIOS's `int 13h` as well, **the hook decodes
inside the read, as on kern_big**. `mega_rfclk` and the catch-up after a
reflection are not involved.

**At exit**, in reverse:

* A20 proven open. `mega_a20`'s sequence (`kernel/mega.inc:8788`) is
  `.cold` and so unreachable before `lmsw`: the module carries a copy.
* `lmsw`, then `lgdt`, `lidt`, `lldt`, the data selectors, and `[k_dsel]` =
  `KDATA` - `mega_svc_lo`'s own way back (`kernel/mega.inc:2965-2992`).
* `mega_dwake`'s tail (`kernel/megados.inc:1664`): a forced
  `mega_ivt_sync` and the option ROM's prime. The 8042 is the BIOS's
  throughout, so `mega_d8042put` is not needed.
* The video words made selectors again (`mega_vfixall`), `[k_seg0]` and
  `CPU_F_PM` back, the table put back, the drivers renamed back and copied
  up, the region copied back UP (it was written to all play: its bss, and
  code the play patches through its data alias) and its proc called again,
  the module's claim freed.
* Then `fsx_run`'s `.restore` (`kernel/fsx.inc:390`) as today. Its mode set
  reflects once, as it does now.

#### 3.1.1 The real-mode service module (re-costed on W0's measured comparables)

It is an on-demand module (§2.8), kern_mega only, and is never resident
while no real-mode bracket runs. **Every byte figure below is still an
ESTIMATE for code that does not exist**, but each now stands against a
MEASURED comparable: a kern_mega body's span to the next global label in
`build/megak/kernel.map` (built at `df2e1fbf`), or a package include's
assembled bytes in the player's listing.

| piece | what | measured comparable | est. bytes |
|---|---|---|---|
| the API table: its bank and its real-mode image | 174 cells at their ABI lengths (section 7.2) | the table is **1,241** | **2,482** |
| real-mode halves of the drop and return | landing, IVT arrangement, entry far call, A20 proof, way back to `lmsw` | `mega_svc_lo` 207, `mega_r286` 96, `mega_dropr` 103, `mega_a20` 53 | 300-450 |
| tick | the rate arm, the ROM chain, `[ticks]`, `DSV_TICK`, the hook | `sch_isr` 285 (all of it), `sch_rhook` 102, `snd_tick` 35, `drv_svc_call_x` 74 | 200-300 |
| `READ_SEQ` | cursor offset to extent to `int 13h` (CHS, or AH=42h where the unit takes it) or to the low block driver's `DSV_BLK`, cut at 64 KB pages (`dsk_runcap`'s rule) and the unit's track or cylinder bound (§18.91.5), with retries; `READ_AT` refused (it is reached only on `FERR_NAME`, `apps/os88rseq.inc:63-66`, which kern_mega never answers) | the hibernate stub's extent reader, `HS_CODE_MAX` 1,024 (`kernel/hbstage.inc:22`); `drv_blk_call_x` 87 | 500-800 |
| `XMEM_COPY`, `_ALLOC`, `_FREE`, `_CAPS` | token to physical through `mx_tab` (`.lowbss`), first fit with NO compaction, the copy by LOADALL (286) or a loaded base (386), A20 probed per call | XMEM.DRV whole is **2,041** (pool, LOADALL and unreal transports, `int 15h`); `mx_alloc` 234, `mx_free` 74, `mx_tophys` 30; the player's own `os88la.inc` 366 and `os88ub.inc` 83 | 700-1,000 |
| `SND_STREAM`, `FSX_SPK` | the driver's far call to the low copy; the speaker's door writing IVT 20h | `osapi_snd_stream` 96, `osapi_fsx_spk` 108, `spk_off` 78 | 200-300 |
| `DRV_CALL` | class to the low copy's far pointer (DRVC_EMS, DRVC_DISK) | `drv_svc_call_x` 74 | 60-100 |
| `SEG`, `MOUSE`, `FSX_WAIT` | `osapi_seg`'s real-mode arm (`kernel/kernel.asm:5906-5950`); the pointer's state words without `kbm_poll`'s `.cold` menu path; `FSXW_FRAME` by `hlt` instead of `task_yield` | `osapi_seg` 53, `osapi_mouse` 121, `fsx_wait` 90 | 100-200 |
| driver and region copies, the video unfix | the `mem_region_reloc` walk both ways, `mega_vfixall`'s inverse | `drv_hi_move` 69, `mega_vfixall` 137, `mega_vrm` 56 | 250-400 |
| | | **total** | **~4.8-6.0 KB** |

The table dominates: two images of 1,241 bytes. A table generated at the
drop (each cell start out of a 348-byte offset list, refusal bytes written
by a loop) is the alternative that trades ~800 bytes of module for a loop
and a list; W2 decides by measuring both.

**What is NOT in the module, and why it need not be**: section 7.3 audits
every kern_mega body the whitelist reaches. **Twelve cells run as they are**
once the drop has done its step 6 (`FSX_MODE`, `FSX_SURF`, `FSX_PAGE`,
`VIDEO`, `FONT_GLYPHS`, `KEY_DOWN`, `SND_CAPS`, `WM_CONTENT`, `WM_WAKE`
and the inline `GET_TICKS`, `CPU_INFO` and `SET_COLOR`), and in real mode
their `int 10h` goes to the IVT, which is the BIOS, not to `mega_svc`.

The resident cost in kern_mega is `fsx_run`'s bit and a branch at its entry
call (`kernel/fsx.inc:388`), the PM halves of the drop and return (which
can live in the module, called through its thunks, leaving only the branch
and `mega_r286`'s new landing resident), and SOUND.DRV's verb: an estimated
**150-400 bytes** (§1: *design for bytes*). kern_big and kern_small take the
bit's accept and ignore: the door's test (`cmp cl, FSXF_FASTTICK +
FSXF_RATE` / `jae .fail`, `kernel/fsx.inc:192-193`) refuses bit 3 today, so
accepting it changes that test on every kernel - an estimated 0-6 bytes,
and neither is byte-identical.

#### 3.1.2 What the package does

* **The arm passes `FSXF_REAL` with DX or ES:DI naming the stream**, at the
  one call site (`video.asm:14635`). ~~A refusal is a refused play, said with
  its reason (`[vp_msg]`, as the CF = 1 arm before `.ran` already does at
  `video.asm:14668-14673`), and NOT a second call without the bit.~~ **(5.3,
  W2: the kernel falls back by itself and answers in SI; the card says
  which bracket and why.)**
* **At its door, still in protected mode, it puts REAL SEGMENTS in its own
  words** and keeps the protected-mode ones to put back after (section 7.4):
  `SEG_REAL` (§50.6.11) for each claim base, its own real-mode derivations
  (the shipped `%else` arms) for every derived word, `[vp_pm]` = 0, and
  `vp_rhpat` with AL = 0 so `vp_addr` is its own again (`video.asm:14140`).
  `vp_bset` then takes its real-mode arm: no `OSAPI_MEM_CODESEL`.
* **It hands its real-mode bank readers a LINEAR address, not the token**:
  the block's current base is `SEG_LIN` of `[vp_xrun]`, the run
  `vp_xrunmk` already takes in protected mode (`video.asm:5958-5982`).
* **Every call the real-mode table does not serve leaves the bracket**:
  15 cells and the sites that reach them (section 7.2).
* **The ring is CONVENTIONAL for every session**: §98.3.18.17's ring above
  1MB is deleted, because any session can enter a bracket (Play, or F from
  Live), and the session's `vp_sstart` sizing leaves room for the region's
  low copy.
* **`vp_xbipq` decides the bank in place by the REAL-MODE rules**, because
  the bank is read only inside a bracket. `vp_xipq` (the hold) cannot do
  that: the hold is read in place by Live in protected mode as well, so its
  decision is per play context (section 5.2, loss 1).

#### 3.1.3 What can go wrong

* **The 286 has only the reset way out.** It is the reflection's own path,
  taken once per bracket instead of on every read, so the Victory's reset
  quirks are already handled (14.62, 14.64). A bracket-long drop makes them
  *rarer*, not new.
* **A BIOS that shuts A20 inside `int 13h` or `int 15h`.** The way back to
  protected mode must prove A20 open before `lmsw`, as `kd_unpark` does
  (KERN-MEGA-DOS-PLAN 2.5 item 3). Each LOADALL borrow probes it, as
  XMEM.DRV does.
* **A fault inside the bracket is a real-mode fault**: no `kern_mega FAULT`
  line and no `mega_fkill`. A bad record in a real-mode bracket hangs the
  machine where kern_big's would hang it too. The owner has accepted it
  (5.1.3). `MEGATRAP` does not see it either.
* **Edges across the two mode changes.** No ICW1 is ever issued, so the
  only exposure is the 286 reset path's own BIOS behaviour, which
  `mega_r286`'s mask compare already catches.
* **A selector left in a word the real-mode bracket reads** is a wild
  segment, silently: W0 found three kinds (`[k_seg0]` and the video words in
  the kernel, the package's derived words, SOUND.DRV's stored segments) and
  there may be more. A gate must break each on purpose (section 4, W1, W2).
* **The extent list is stale if the file changes mid-play.** It cannot:
  nothing runs that could write.
* **SOUND.DRV's state**: a verb the driver gets wrong is silence or a stuck
  card. The Victory has already seen exactly that (§98.3.22.7). Its gate
  must play the card's clock through several brackets (section 4, W4). A
  stream OPENED inside a real-mode bracket (`vp_sopen`, `video.asm:15457`)
  stores real segments; `DSV_TICK` runs every protected-mode tick between
  brackets and must not dereference them there (W4's audit).
* **The first fill.** ~~The prefill before the bracket stays a
  protected-mode read.~~ **W0: the ring's first fill is ALREADY inside the
  bracket** (`vp_main`'s `.fill`, `video.asm:15353-15365`, with the bank's
  and the layer's prefills after it, `:15376-15378`). What precedes the
  bracket on the desktop is the key's one read (`vp_spos` and `vp_rdat`,
  one call of 64 KB or less) and the session's claims. 5.1.4's placement is
  therefore: the fill stays where it is, inside the real-mode bracket, and
  the key's read stays on the desktop - one call, shared with the poster's
  and a seek's, where a protected-mode read is the only kind there is.

### 3.2 A2: the same bracket over a full real-mode kernel - REFUSED

Assemble the kernel's file layer for real mode, as kern_dos is assembled,
instead of an extent reader. It costs ~33 KB of image (`KD_IMG_KB`) to
serve one sequential stream whose clusters are known before the bracket
starts. It would also need the FAT window and the mount state, both of
which kern_mega keeps in claims that may be above 1MB. The extent reader is
the hibernate stub's proven shape at about 2% of the size.

### 3.3 B: windowed play with the desktop live - DISSOLVED

The owner's correction makes this the same as A. The in-window play is a
same-mode fsx bracket, so A covers both kinds of play. The one play on a
live desktop is Live, which reads no disk and stays in protected mode.

### 3.4 C: the package's whole session in real mode ("real-mode residency") - REFUSED

That means keeping the kernel in real mode whenever the player's window is
open. The desktop, the window manager and every other task are
protected-mode code, so nothing else could run. That is the windowed DOS's
problem, which KERN-MEGA-DOS-PLAN 2.1 refused for the same reason.

~~**Rule 1 for A**, from this option's failure: the real-mode bracket is
taken only when it pays (section 3.1, step 1), so `vm/286-video`'s
native-disk machine keeps the protected-mode bracket, which is already at
parity.~~ **Superseded by 5.1.1 (always) and W0's finding that rung 1's
PIO survives the drop (section 5.2 item 1)**: `vm/286-video` takes the
real-mode bracket too, and what it must not lose is checked by W3's rung-1
row at 14.73's 25 fps.

### 3.5 D: native protected-mode drivers for what is reflected - NOT INSTEAD, MAYBE AS WELL

14.124 had this queued: *"Before building a protected-mode handler for the
card's multiplexer..."*. A protected-mode PicoMEM multiplexer handler would
take the 2.8% of IRQs back, and a PicoMEM disk rung in HDD.DRV would take
the reads (the card's own firmware is in the tree, `tools/picomem/`).

**It is narrower**: it covers one card, while an XT-IDE, SCSI or any other
option ROM's disk still reflects (14.73 reproduced the problem on an
XT-IDE). It also depends on the card's protocol, and the hook is still not
inside the read unless the disk path is interrupt-driven. **A fixes every
ROM-served disk at once**, so D is a later, separate question.

### 3.6 E: park kern_mega and run kern_big for the play - REFUSED

E reuses the DOS trip's park to bring up a kern_big. A session is not
portable between kernels, the park costs ~0.2 s each way plus the sum, and
a bracket toggle (F, a seek) would pay that every time.

### 3.7 How much of the kern_mega arm A deletes (MEASURED by W0)

~~An ESTIMATE until W0 classifies every block: ~1,000-1,250 lines, 55-70%.~~
**W0 measured it (section 7.1): 368 lines of the 1,771, 630 of 3,849
bytes, 21%** in `video.asm`, plus 433 bytes of `os88spk.inc` assembled into
`video.bin` and 476 of `os88spkfx.inc` assembled into part 2. The first
draft's figure assumed that what is protected-mode-only is bracket-only, and
it is not: the session's start (`vp_sstart`, `vp_spos`, the ring's and the
keeper's selectors), the timer's pre-fills on the desktop (`vp_pstep` and
`vp_xstep` read the ring, the bank and the hold through the very arms the
hook uses), the keys and the poster (`vp_loadkey`, `vp_kpic`,
`vp_sesspic`), the info card and Live all run in protected mode and reach
**1,525 of the 1,771 lines**. Those arms are runtime-dual (`[vp_pm]`) or
ask `OSAPI_SEG`, which the real-mode table answers as every real-mode
kernel does, so inside the bracket they take the shipped arithmetic and
outside it they are still needed. They are not dead and are not deleted
(5.1.2).

**What goes with A** (each measured, section 7.1):

* **the ring above 1MB** (§98.3.18.17, 14.41): `vp_rhiq`, `vp_rhkb`,
  `vp_rhtry`, `vp_rhfit`, `vp_rhoff`, `vp_rhcp`, `vp_bnc` and the arms that
  call them - 286 lines, 458 bytes. Its own reason went: any session can
  enter a bracket, and a real-mode bracket can address no ring above 1MB;
* **the bracket-only procs' arms**: `vp_main`, `vp_hook` (§98.3.22.6's two
  frames a call), `vp_skdue`, `vp_fseek`, `vp_lstage`, `vp_lybench`,
  `vp_bnsel`, and `vp_bset`'s code alias - 69 lines, 138 bytes;
* **the bank in place's protected-mode gates** (`vp_xbipq`) - 13 lines, 34
  bytes, because the bank is read only inside a bracket;
* **the speaker library's protected-mode ISR family** (`os88spk.inc`'s
  `OS88SPK_ISRS es, p, 1` and its stores) - 32 lines, 433 bytes - and **the
  shaper's protected-mode copies** (`os88spkfx.inc:584-627`) - 49 lines, 476
  bytes of part 2. The speaker plays only inside a bracket
  (`OSAPI_FSX_SPK`).

The shipped `%else` arms that come back are 35 lines, 93 bytes. Projected
from those parts (an arithmetic projection, not a build): the plain arm's
image 60,688 -> **~59,718**, image + bss 63,309 -> ~62,339, before W5's own
door code (estimated 150-300 bytes).

**OPTIONAL, and a choice rather than dead code**: un-parting the shaper
(`vfx.asm` 69 lines, `vfx.inc` 41, the 126 lines of `vp_fxget`,
`vp_fxinit` and the thunks, 243 bytes) puts `os88spkfx.inc`'s 1,891 bytes
back in the image. Measured headroom: the plain arm would be ~64,000 of
APP_MAX_SIZE's 65,024; **the `VPDIAG=time` arm would be ~65,678, 654 over**
(it is 64,987 today, 37 under). So one shape for both arms keeps the part.

**What stays**, and why (section 7.1's table):

* MC7's segment derivation for every claim of the session (`vp_at`,
  `vp_atn`, `vp_sub`, `vp_subtab`, `vp_rsmk`, `vp_rslot`, `vp_plmk`,
  `vp_plset`, `vp_lymk`): runtime-dual, used on the desktop and by Live;
* the hold read in place through selector runs (`vp_xrunmk`, `vp_xmk`,
  `vp_xok`, `vp_pmbig`'s in-place arm): Live (§98.3.18.1);
* a resident block's windows (`vp_rui`, `vp_rres`, `vp_rwmk`): Live's
  resident files;
* `vp_paddr` and `vp_rhpat`: the desktop's pre-fill and Live address the
  ring through them - unpatched at the bracket's door instead;
* the page-safe claims (`vp_rclaim`, `vp_rclk1`, `vp_mclaim`, `vp_mclhi`,
  `vp_mfree`), the header, `vp_self` / `VP_OWNB` / `vp_reloc`, the block
  decoder's part 1 (`VB_PM`, 80 lines of `vblk.asm`, part 1 is 282 bytes
  bigger than part 0: 5,028 against 4,746) while Live may play a version 2
  file (section 6), the info card's words, and the `VP_RQ` diagnostic,
  which is W6's instrument and goes after W6.

**90% would need** the session's desktop half to be protected-mode-free
too: every claim made inside the bracket and dropped at its end, so that
nothing the player holds on the desktop is a claim at all except Live's.
That is a player redesign and is section 6, question 2. The owner has said
the percentage is not a target (5.1.2).

---

## 4. Recommendation and waves

**Take A, always (5.1.1) - subject to section 5.2.** Every wave verifies on the host (`make`, `make mega`,
`tools/kernsize.py` on all three kernels) and names its rows. The rows run
once, after the last wave, as the Testing rule says. The final pass is the
deduplicated union below, plus `test-full`.

* **W0. The census - DONE 2026-10-29** (section 7). It built and ran
  nothing that ships: two assemblies of the player (`-DPKG_MEGA` and not),
  one of kern_mega with its map, and static call graphs over the listings.
  It replaced this file's 3.1.1 and 3.7 with measured tables, answered 5.1.4
  (section 3.1.3), and found what section 5.2 asks the owner before W1.
* **W1. The drop and return with an empty bracket - BUILT 2026-10-30, its
  two gates green.** SPEC.md 53.2.3 is the contract (written before the
  code); `kernel/fsx.inc`'s `%ifdef KERN_MEGA` tail is the code, with
  `mega_down` / `mega_rland` / `mega_up` made routines both drops share
  (`kernel/megados.inc`, `kernel/mega.inc`).

  **What was built.** `FSXF_REAL` = 8, CX bit 3 of `OSAPI_FSX_RUN`, in both
  SDK and kernel (`apps/os88api.inc`, `kernel/fsx.inc`), the ABI lock
  updated with no bump (a new bit every older kernel refuses as undefined).
  Every kernel strips it at the door (`and cl, ~FSXF_REAL`); kern_mega banks
  the asked flags in SI first and, in protected mode, acts:
  * **`fsx_rprepf`** (`.mega`, at the door after every other test, so
    nothing can refuse after the claim): refuses with `FSXF_KEEPWORKER`,
    with a `TF_SERVICE` task alive, with EXTD.DRV loaded, and when an
    `OS88_HIGH` region (base >= 1MB off its descriptor) gets no
    conventional claim of `I_SIZE` - owned by the instance's slot, so a
    teardown frees what an exit did not. A conventional region runs where
    it is.
  * **`fsx_rcopyf`** (`.mega`): the whole region DOWN at the freeze's arm,
    with IF = 1 - frozen, no other task can write it, and the rate not yet
    armed, so no hook can - and back UP at the exit, IF = 1 again, after
    the rate is off.
  * **`fsx_rcall`** (`.text`) in place of `wm_pkgcall`: `fsx_rdownf`
    (`.mega`: the masks a service reflection's through `mega_rmaskf`'s new
    `.nv` entry, `mega_vrm`'s two words undone, IVT 20h = `fsx_rtick`,
    EXTD.DRV's 16 slots pointed at `fsx_rgone`, `[k_seg0]` = 0, `CPU_F_PM`
    cleared, and every word `mega_vfixall` made a selector banked and made
    a segment by `mega_vwalk`), then `mega_down` with `[mega_drop]` = 2 -
    CR0 on a 386, the shutdown-0Ah reset on a 286, `mega_r286` doing its
    mask compare and `mega_pic_pm` / `mega_kdrain` as for a reflection -
    landing at **`fsx_rland`**, the entry far-called at the real paragraph's
    `PKG_DISP`, then `mega_a20`, `mega_up`, `sch_fast_off` and
    **`fsx_rupf`** (`.mega`: the FORCED `mega_ivt_sync`, the masks, every
    word back from the bank, the copy up, the claim freed).
  * **`fsx_rtick`** (`.text`): `sch_isr`'s three arms with no scheduler -
    `[sch_subs]`; under `FSXF_RATE` the accumulator and the hook with
    `sch_rhook`'s busy and SS rules; under `FSXF_FASTTICK` the countdown; a
    tick chains `[mega_old08]`, the ROM's own handler, and bumps `[ticks]`.

  **Decisions, and why** (where the plan above was ambiguous or W1 found it
  wrong):
  1. **The real-mode half is RESIDENT in `.text`, not a module.** A module
     is read from the SYSTEM disk; read at every bracket it is a disk swap at
     every Play on a one-floppy machine, which fails docs/plans/completed/
     ONDEMAND-PLAN.md 1's test, and kept loaded for the session it is
     conventional memory either way. W1's real-mode code is ~240 bytes,
     smaller than a module's row, thunks and conventional load arm. **W2
     decides the table separately**: its two 1,241-byte images are the part
     that might want one - or none, generated at the drop into a heap claim
     (3.1.1's alternative), which needs no disk at all.
  2. **`FSXF_REAL` combines with ANY flag**, not "only with `FSXF_RATE`"
     (3.1): the tick does the plain and the sub-tick arms too for ~10 bytes,
     and a generic flag (5.1.5) should not dictate the clock.
  3. **No relocation proc is called for the region's copy** (3.1 step 5
     said call it). A region above 1MB HAS none recorded -
     `osapi_mem_movable_x` refuses the declaration there - and the copy is
     not a move: the window's copy is the one that stands again at the
     exit. So the contract says it (SPEC.md 53.2.3): inside, take your own
     segment from CS; a word banked before the call holds a selector. The
     player's `[vp_self]` is W5's to set inside its entry proc.
  4. **IVT 20h is restored by the forced `mega_ivt_sync`, not by hand**: the
     sync rebuilds the gates AND writes IVT 20h-27h back as the copy of
     08h-0Fh, so the tick's vector is gone the moment the gates are right -
     and skipping the sync is the break that leaves it there.
  5. **`mega_dprime` is not called** (3.1's "mega_dwake's tail" named it):
     it primes the lines the DOS trip's two ICW1s cost. The bracket issues
     no ICW1 and keeps a reflection's masks, so there is nothing to prime -
     and a 286's reset that does re-initialise the 8259 is `mega_r286`'s
     mask compare, as on every reflection.
  6. **The A20 proof is `mega_a20` itself, MOVED from `.cold` to `.text`**
     (118 bytes with a far entry for `mega_gatef`, -112 in `.cold`): the GDT
     is in extended memory, so the line must be proved open in real mode,
     before `lgdt`.
  7. **`snd_tick` does not run in the bracket** (`SOUND.DRV`'s image is
     above 1MB): W4's, with the low copy.
  8. **The protected-mode state is `.lowbss`**, not `.bss`: only `.mega`
     reads it (through SS), so it stays out of the kernel segment the
     `tests/megaseg.txt` ratchet guards.
  9. **The hook may run in protected mode BEFORE the entry proc**, on the
     window's copy, between the rate's arm and the drop: §53.2.2's "a no-op
     until ready" already covers it, and the contract says so.

  **What W0's census missed** (7.2 says `FSX_MODE` is AS-IS once the video
  words are segments): `fsx_mode`, `fsx_caps` and `wm_content` far-call
  EXTD.DRV's entry block (`EXTCALL`) whatever the machine, and unloaded
  every slot names `.cold`'s `mod_gone` through a PROTECTED-MODE selector -
  a wild segment in real mode. The first build HUNG inside leg B's
  `OSAPI_FSX_MODE`, at 10B8:2D64. Hence `fsx_rgone` and the fourth refusal.
  **W2 must look for this shape in every run-as-is cell: a far call through
  a kernel slot table** (module blocks, `dkv`, `thv`) is as much a selector
  as a data word is.

  **Bytes** (`tools/kernsize.py` on all three, against `df2e1fbf`+W0's
  `1ca43598`): kern_big **+3** (`.text`), kern_small **+3** (`.text`),
  nothing else on either. kern_mega `.text` **+422**, `.bss` +0, `.cold`
  **-112**, `.lowbss` **+74**, `.mega` **+518** (its scratch +10); the
  kernel segment's ratchet **+419** to 4,800 (`tests/megaseg.txt` says what
  each part is). The plan's 150-400 resident estimate is exceeded because
  the tick, the landing and the A20 proof are resident rather than in a
  module (decision 1) - 422 conventional bytes for W1's whole real-mode half.

  **Gates, and what they showed** (each run twice or more, green):
  * **`megarmb`** (Bochs, a 386, 36-38 s): `tests/megarmb`'s MEGARMB
    (`OS88_HIGH`, region at FD8400h, its copy at 12A0) and MEGARMBL (a
    conventional region, run in place at 9F40). Per package: R refused, A
    and B taken; stopped INSIDE A, CR0.PE 0, `[k_seg0]` 0, `[cpu_feat]` 09
    (no `CPU_F_PM`), VSEG's words A000/B000/B800/F000, vid_tab's VGA row
    A000, IVT 20h `0060:fsx_rtick`; the hook handed **1,200 periods in 75
    ticks, 16.0000 exactly against 65536/4096**, 40:6C 75 with `[ticks]` 75
    (and 36/36 in B), 1,068 calls with up to 3 periods handed to one (the
    skip path); the bss mark read back; B's `FSI_SEG` A000; `[ticks]` -
    40:6C equal before and after; after, IVT 20h-27h the copy of 08h-0Fh,
    `[mega_ivtsh]` the IVT's, the IDT's IRQ gates byte for byte as before,
    the pointer moving.
  * **`megarmb286`** (86Box `vm/286-victory`, the reset path, ~8 s): the
    same assertions off the packages' bss, plus **exactly one CPU reset at
    `mega_down`'s `int 3` per taken bracket (4 for 4) and none for the
    return**; nothing charged to a loaded image after the boot (the boot's
    own two completed #GPs at `1118:140F`/`:1428`, a write through ES =
    0060h, are there on the base tree too and are not this wave's). On the
    286 the same 1,200 periods came as 836 calls, up to 8 to one.
  * **Broken on purpose**, each red and each put back:
    * the forced `mega_ivt_sync` skipped: red on "IVT 20h-27h is the copy of
      08h-0Fh" (`fsx_rtick` left on IRQ0's real-mode vector, which the next
      reflection would take);
    * `[k_seg0]` left `MEGA_SEL_LOW0`: red on the inside look (`0008`).
      **The plan's predicted symptom does not show**: on VGA mode 13h
      `fsx_mode` reaches `vid_cga_equip`, which writes only when 40:10 says
      mono, so the wrong-segment read lands on 40:90 and changes nothing.
      The row reads the word itself;
    * no copy back: MEGARMB's counts, mark and `FSI_SEG` all read 0 (red on
      4, 5 and 6), MEGARMBL untouched;
    * EXTD.DRV's slots left on `mod_gone`'s selector: MEGARMB hangs in leg
      B (the row reports the hang);
    * on the 286 row, `[mega_drop]` left 0: the reset lands as a
      reflection's, a #GP at `0010:0514` and no bracket - red.
  * **Existing rows W1 can reach**, for the final pass (not run here):
    `megapiclose` (its reflections' way back is `mega_up` now, and the DOS
    trip's way down `mega_down` / `mega_rland`), `megaxmove`, `vidkern`
    (kern_big's `fsx_run` door), `megatbl` and `megasweep` (`mega_vfixall`
    is a walk now), `megadosp` / `megakbtrip` / `megahiber` and the hibernate
    rows (`mega_dosgo_lo`, `mega_hbgor`), `megacurdisk` (`mega_rmaskf`), and
    the megaport `vid*` rows, whose bracket door changed by the strip.
* **W2. The table, and the fallback - BUILT 2026-10-30, its gate green.**
  SPEC.md 53.2.3 is the contract (rewritten before the code: the fallback,
  SI's answer, the table, the twin and the package's half);
  `kernel/fsx.inc` (`fsx_rprepf`, `fsx_rdownf`, `fsx_rupf`, `fsx_rkeep`,
  the foot of `fsx_run`), `kernel/kernel.asm` (`rmk_*`, `osapi_segr`),
  `kernel/mouse.inc` and `apps/os88real.inc` are the code.

  **What was built.**
  * **The DUAL bracket (5.3).** `FSXF_REAL` never refuses on kern_mega in
    protected mode: `fsx_rprepf` tests the reasons in order, and the first
    that holds goes in `[fsx_rwhy]` with NOTHING taken, so the same bracket
    goes on in protected mode, exactly today's. `fsx_run`'s foot puts the
    answer in the caller's SI: `FSXR_REAL` (0), `FSXR_KEEP`, `FSXR_SVC`,
    `FSXR_EXTD`, `FSXR_SERIAL`, `FSXR_ROOM`. A kernel in real mode keeps SI,
    so a package passes SI = 0 and reads one answer on every kernel; inside,
    `OSAPI_CPU_INFO`'s `CPU_F_PM` says the same. Each reason is ONE test of
    three lines, so lifting one (the owed list below) deletes it and touches
    nothing else.
  * **The table.** At the drop `fsx_rdownf` banks the 1,241-byte table in the
    DOS park's scratch (`MEGA_SEL_PSCR`, 1,490 bytes above 1MB with `.mega`,
    asserted to hold it), writes every byte `stc`, puts a `retf` before each
    kept run and at the end, copies the kept runs back off `fsx_rkeep`'s
    list, and makes `OSAPI_SEG` a far jump to its twin; `fsx_rupf` copies
    the bank back before IF = 1. A refused cell - any shape, entered at any
    byte - slides down `stc`s to a `retf`: CF = 1, every other register and
    flag kept. The list is 11 rows of {first cell, bytes, twin} whose cells
    are label pairs in the table itself (`rmk_1`..`rmk_10`, `rmk_seg`), so a
    run is exactly its cells and cannot drift.
  * **The twins W1 left.** `OSAPI_SEG`: its real-mode arm moved from `.cold`
    to `.text` as `osapi_segr`, which the `.cold` body (on a kern_mega that
    never switched) far-jumps to and the table jumps to - kern_big's
    answers out of the one body. `OSAPI_FSX_WAIT` and `OSAPI_MOUSE` turned
    out cheaper as ARMS than as twins: `FSXW_FRAME` waits on `hlt` while
    `[fsx_rmode]` is set (no scheduler), and `osapi_mouse` skips `kbm_poll`
    (its menu path is `.cold`) - 7 bytes each where a twin body is 30-100.
    No other cell of W0's table is cheap enough for W2: `DRV_CALL` needs the
    low drivers (W3), the XMEM cells and `READ_SEQ` are W3's, `SND_STREAM`
    and `FSX_SPK` W4's.
  * **The package's half: `apps/os88real.inc`** (W0's "SEG_REAL at its door",
    7.4): `OS88REAL_LIST` names a package's claim words, `os88real_prep`
    asks `SEG_REAL` for each at the door and banks the answers (CF = 1 = a
    claim with no real segment - above 1MB - and the package does not ask
    for the bit: ITS OWN fallback), and `os88real_swap`, first and last in
    the entry proc, exchanges list and bank only when the bracket IS real.
    The exchange is symmetric, so the second call puts the selectors back
    before the copy is written up over the window's.

  **The audit W1 asked for** (a far call through a kernel slot table in a
  run-as-is cell). Walked off a kern_mega listing from every kept cell's
  body, the twins' and `mou_isr`/`mou_p2_isr`, flagging every far or
  indirect call, `int`, descriptor instruction and selector word:
  * `EXFP` (EXTD.DRV) from `fsx_mode` - W1's, still `fsx_rgone`'s;
  * **`SERFP` (SERIAL.DRV) from the POINTER'S ISR** - `mou_isr_ser` far-calls
    the module for a leased UART on every interrupt, a wild selector in real
    mode. Not in W0's census, which walked the cells and not the ISRs. A
    lease open is a state the user is in (§115's link games), so it is a
    FALLBACK reason, `FSXR_SERIAL`, owed;
  * **`[vx_ep]` (VESA.DRV's hook)** from `vid_setmode`, the cursor and the
    glyph renderers: reachable only on a VESA desktop and then only by
    drawing (not offered) and the cursor (not drawn under the held lock), so
    no kept cell reaches it - but the drop points it at `vx_null` for the
    bracket anyway, the far slot's insurance (`.lowbss` +4 for its bank);
  * `dkv` (DOCK.DRV) from `gfx_hole_arm`, and `thv`: drawing only, not
    offered;
  * `osapi_mouse`'s port re-arm (§9.13) in real mode compares IVT 0Ch's
    segment with CS - 0060h in both modes - and is harmless; `int 10h` in
    `fsx_setbios`/`vid_text` and `int 16h` are the BIOS's in real mode.

  **Decisions, and why**:
  1. **The table is GENERATED AT THE DROP and banked in the park's scratch**:
     not a module, not a resident second image. A module is a system-disk
     read at every Play (W1's decision 1); a resident real-mode image is
     1,241 bytes of every kern_mega machine's conventional memory for ever;
     a heap claim for the bank is conventional room at every Play and
     another reason to fall back. The park's scratch is 1,490 bytes above
     1MB that nothing else can use while a bracket runs (a trip, a hibernate
     and a bracket exclude each other), so the bank costs NO byte anywhere.
     The generator is a `.mega` loop over a 66-byte list (in extended
     memory), against 3.1.1's estimate of 2,482 for two images.
  2. **The sled, not per-cell refusal bytes**: writing `stc` over every byte
     and a `retf` before each kept run refuses every cell shape at once with
     no cell-length table at all (W0's 348-byte offset list), and refuses an
     entry mid-cell too.
  3. **A fallback is the kernel's, a package refusal the package's.** The
     kernel cannot know a package holds a claim above 1MB, and the package
     cannot know a driver's worker is alive - so the kernel falls back for
     what it sees and says why, and `os88real_prep` lets the package decline
     to ask. Neither refuses the play.
  4. **The owner's rule binds the reasons** (2026-10-30: "We need to not
     refuse states the user is going to be in"): nothing in W2 is a refusal.
     The reasons a user will be in are owed, below.
  5. **SI, not AX, carries the answer**: SI is no input of `OSAPI_FSX_RUN`,
     so a kernel that keeps it answers `FSXR_REAL` for nothing - kern_big
     and kern_small stay byte-identical.

  **Owed - reasons for a state the user WILL be in, each to be lifted by
  deleting its one test once the wave that owes it is built**:
  | reason | the state | owed by |
  |---|---|---|
  | `FSXR_EXTD` | the extended desktop (two displays): EXTD.DRV's entry slots far-called by `OSAPI_FSX_MODE`, `OSAPI_FSX_CAPS`, `OSAPI_WM_CONTENT` | **W7** - real-mode targets (twins of the EXTF verbs the three cells reach, or a low copy of the module's image for the bracket), and a fullscreen play on EITHER display (the owner, 2026-10-30: the player supports both) |
  | `FSXR_SVC` | a driver's worker alive (a network driver's, a sound stream's task) | **W7** - the bracket would starve it; either the driver is told (a verb, as `DSV_RMODE`) or its worker's work is run from the real-mode tick |
  | `FSXR_SERIAL` | a SERIAL.DRV lease open (§115) | **W7** - the leased UART's capture (`SERFP+SER_IRQ`) from a low copy of the module, or the ISR skipping the leased row in a real-mode bracket with the module told |
  | `FSXR_ROOM` | a full conventional heap and an `OS88_HIGH` region - or no room for W3's service area | **W5 in part** (the player's ring goes conventional, so its own sizing leaves the room, 5.2 item 4), and the HMA placement of 5.3.3 later |
  | `FSXR_DRV` | a listed driver the bracket does not carry: ~~`DRVC_SOUND`~~ - **lifted by W4**; now only a class no play reaches (`DRVC_NET`, ...) or a SOUND.DRV word above 1MB | done |
  | `FSXR_DROOM` | no room for a driver's low copy | **W5 in part**, with `FSXR_ROOM` |
  | `FSXR_FRAG` | a file past `FSXL_RUNS` runs | - (a copy plays; protected mode serves it meanwhile) |
  | `FSXR_VOL` | an FS-driver, redirected or image volume | **PERMANENT** (5.3 item 2): the info card's line |
  | `FSXR_KEEP` | none: the package's own combination | - |

  **Bytes** (the kern_mega map against W1's `321199e6`, both at one commit
  count; `tools/kernsize.py` on kern_big and kern_small): **kern_big and
  kern_small BYTE-IDENTICAL** (`cmp` of both `kernel.bin`s). kern_mega
  `.text` **+69** (`osapi_segr` 40; the two arms 7 each; the foot 15 and its
  byte; less the door's `jc`), `.cold` **-35**, `.mega` **+252** (the
  reasons, the swap, the list), `.lowbss` **+4**, `.bss` +0; the
  kernel-segment ratchet `tests/megaseg.txt` +69 to 4,869, itemised there.
  `apps/os88real.inc` is ~75 bytes in a package that includes it; no
  shipped package does yet (the player is W5's).

  **Gate: `megarmbref`** (Bochs, a 386; `tests/megarmb/megarmbf.asm`'s
  MEGARMBF, `OS88_HIGH` and `OS88_HIGHMEM`) - green, 26 s, run three times.
  Each FALLBACK arm runs and is asserted to have RUN in protected mode with
  its reason: K (`FSXF_KEEPWORKER`, SI 1), S (a `TF_SERVICE` task: the row
  sets the bit on task 0 while the package waits, SI 2), E (EXTD.DRV's row
  poked, SI 3), L (`[ser_row]` poked to a row no port has, SI 4), M (the
  heap filled by the package's own conventional claims, SI 5); H, a
  `OSAPI_MEM_HUGE` claim above 1MB (linear FD0000h) in os88real's list, so
  prep says CF = 1 and the package runs without the bit; P, a conventional
  claim renamed inside to the paragraph prep banked (1160h), `5AA5h`
  written through it in real mode reading back through the selector after;
  N, a real-mode bracket in which `OSAPI_MEM_CLAIM` (a rare cell) and
  `OSAPI_FILE_RENAME` (a 3-byte JCELL) answer CF = 1 with every register
  kept, the SEG twin answers `SEG_LIN` = CS x 16 and refuses `SEG_AT`,
  `FSX_WAIT` and `MOUSE` answer, and the table read INSIDE is the real-mode
  image to the byte; then n, the package's own re-run without the bit,
  where the claim is had. After: the table byte for byte, every poked word
  back, `[vx_ep]` back, `[fsx_rwhy]` FFh, the pointer moving. **W1's
  `megarmb` and `megarmb286` were updated** for R's new meaning (CF 0,
  SI = `FSXR_KEEP`, `CPU_F_PM` inside; no reset on the 286) and both are
  green with the table swap in every taken bracket.

  **Broken on purpose**, each red and each put back:
  * the W1 refusal put back (the door `jmp .fail` on any reason): K, S, E,
    L and M answer CF = 1 and their entries never run (5 red);
  * a kept run dropped from `fsx_rkeep` (`rmk_7`, FSX_MODE and FSX_WAIT):
    `OSAPI_FSX_WAIT` answers CF = 1 inside N, and the table read inside
    differs from the image in 15 bytes, the first a missing `retf` at 023Ah;
  * the SEG twin left a refusal (`rmk_seg` with no target): `SEG_LIN`
    answers CF = 1, 5 bytes off at 04E3h;
  * the table NOT copied back at the return: the first real-mode bracket
    (P) leaves the sled in place, so N's own `OSAPI_FSX_RUN` is refused,
    nothing after it runs and the table after differs (7 red);
  * SI's answer not written at the foot: K-M come back SI 0 (5 red);
  * `os88real_swap` never swapping (the package's half): inside P the word
    is still the selector (1204h against the banked 1160h) and the pattern
    written through it is lost (2 red).

  **Existing rows W2 can reach**, for the final pass (not run here):
  `megarmb` and `megarmb286` (run here, green), every row that calls
  `OSAPI_SEG` on kern_mega in real mode or on a never-switched kern_mega
  (the `.cold` body is a far jump now) - `megaxmove`, `megatbl`,
  `megasweep`, `megahdd*`, `megasb*` and the megaport `vid*` rows, whose
  packages ask `OSAPI_SEG` constantly; `megacurdisk` and the mouse rows on
  kern_mega (`osapi_mouse`'s new test); the megaport rows using
  `OSAPI_FSX_WAIT` (`vidvga4`, `vidmodex`, `vidplayvga`, `vidpage`, the
  game ports); `t_api_abi` (fast, already green: the table's bytes are
  unchanged); and `vidclose` and `vidxipbanktier`, which the plan named.
* **W3. The extent reader, the low drivers and the XMEM twins - BUILT
  2026-10-30, its three gates green.** SPEC.md 53.2.3 is the contract
  (rewritten before the code: the LIST, four reasons, the twins); the code
  is `kernel/fsx.inc`'s KERN_MEGA tail (`fsx_rlist`, `fsx_rwalkf`,
  `fsx_rsvcf`, `fsx_rdrvf`, `fsx_rcopyf`, `fsx_rfree`, the `rms_*` twins).

  **What was built.**
  * **THE LIST.** With `FSXF_REAL` the caller's **BP** names `FSXL_FILE`
    (a NUL 8.3 name its `READ_SEQ` will read) and `FSXL_DRV` (a class its
    `DRV_CALL` will reach) entries; BP = 0 is no list, and every real-mode
    kernel keeps BP. BP because every other register is spoken for by
    `FSXF_RATE` and the window; read off fsx_run's frame under the X stub,
    so `.text` pays nothing.
  * **THE WALK** (5.1.4's switch point, decided): at the door, in protected
    mode, in the caller's own folder (`inst_vol_enter`, through
    `mega_tcall`, only when there is a list), each file stat'ed and its chain
    walked (`fsx_rwalkf`, `.cold`: `dskw_stat_x`, `dsk_clus2lba_x`,
    `dsk_next_clus_x`) into RUNS - a `DVK_BIOS` unit's ABSOLUTE sectors (the
    partition's base added, every run's end checked against CHS's reach) or
    a `DRVC_DISK` volume's own. Into the PARK'S SCRATCH, so a fallback takes
    nothing; 224 runs and 4 files (`FSXL_RUNS`, `FSXL_NMAX`). A stat or chain
    that fails is the file's `FERR_*`, answered by the twin - what protected
    mode would say - not a fallback. The first fill stays inside the bracket,
    as W0 found it.
  * **THE SERVICE AREA**: the twins' code (2,016 bytes) is assembled in
    `.mega` between `; ovlchk: real mode` markers and COPIED, with the rows
    and runs behind it, into a conventional claim of the instance's for the
    bracket - 3 KB for a contiguous file, 4 KB at the cap - run at a
    paragraph whose x 16 is the claim's base less `rms_lo`, so `.mega`'s own
    offsets are the twins'. No conventional byte at rest and no disk read.
    `fsx_rkeepm` points five cells at it.
  * **THE TWINS.** `OSAPI_FILE_READ_SEQ` (`rms_rseq`): the runs by `int 13h`
    with `dsk_xfer`'s bounds (track or cylinder as the kernel's own reads,
    the 64KB page with a sector bounced through `dsk_secbuf`, the floppy's
    EOT, retries, then a sector at a time), or a `DRVC_DISK` volume through
    the LOW copy's `DSV_BLK`; the cursor comes back a seek (`FSEQ_OFF` on,
    the rest zero). `OSAPI_XMEM_COPY` (`rms_xcopy`): the token through
    `mx_tab` (W0's finding: nothing moves a block while no protected-mode
    code runs, so no pin), A20 proved by `mega_a20f`, the bytes by LOADALL on
    a 286 (`apps/os88la.inc`, 16 KB a borrow) and a loaded base on a 386
    (`apps/os88ub.inc`, 2 KB a chunk) - kern_big XMEM.DRV's own transports,
    included rather than copied. `OSAPI_XMEM_CAPS`. `OSAPI_DRV_CALL` and
    `_AT` (`rms_drvx`, `rms_drva`): a class carried low, at its paragraph,
    through `DSV_PKGCALL`.
  * **THE LOW DRIVERS** (`fsx_rdrvf`): `DRVC_DISK` (asked by a stream on its
    volume, or listed) and `DRVC_EMS` (listed) - a conventional claim, image
    and bss copied down at the freeze's arm and up at the exit with the
    region; a conventional image runs where it is. HDD.DRV's rung 1 keeps its
    native PIO, and **`HDV_ARM` works in the bracket** (refused under
    `CPU_F_PM`): vidrmbhdd reads 32 KB straight into XMS through the twin.
  * Four reasons, one test each: `FSXR_VOL` (6), `FSXR_FRAG` (7), `FSXR_DRV`
    (8), `FSXR_DROOM` (9); `FSXR_ROOM` covers the service area too.

  **Decisions, and why**:
  1. **The twins are `.mega` copied per bracket, not resident `.text`**
     (W1's choice for its 422 bytes) - these are 2,016 and only a real-mode
     bracket wants them; `.text` is conventional memory on every kern_mega
     machine for ever and is the kernel segment `tests/megaseg.txt` guards.
     A module would be a system-disk read at every Play (W1's decision 1).
     The copy costs a claim, which FSXR_ROOM already covers.
  2. **`READ_AT`, `XMEM_ALLOC` and `XMEM_FREE` stay refused** (7.2 offered
     "TWIN without compaction, or the split moves out"): an allocator in real
     mode is a second one to keep in step with `mx_alloc`'s tokens and
     floor, for the player's one call site at the first fill. **W5 moves
     `vp_lysplit`'s and `vp_xfree`'s claims to the door.**
  3. **`FSXR_VOL` is PERMANENT** (5.3 item 2: FS-driver and redirected
     volumes play in protected mode, with the info card's line), and so is
     a mounted image's; RAMDISK.DRV is a FILE class (`DRVC_FILE2`), not the
     block class 5.2 item 3 assumed, so it is among them.
  4. **`DRVC_SOUND` falls back (`FSXR_DRV`)** until W4 carries it; EMS is
     carried by the same code as HDD.DRV. It was untested at W3 - no
     emulator in the tree gave kern_mega an EMS board - and is gated since
     by `vidrmbems` (SPEC.md 107.8) on `vm/286-mega-ems`, 86Box's Lo-tech
     board on the owner's mr286: green on its first run, with no defect in
     the EMS path, and red with the copy back up taken out.
  5. **No new XMS twin translation pin**: the copy reads `mx_tab` live, so a
     token is right however far its block moved before the door.

  **Defects found on the way** (two of them W1's, latent until W3 stood a
  second claim next to the region's copy):
  * W1's copy LENGTH rounded with `and cl, 0FEh / jnz`: a region of 7FFh
    bytes - any size ending FFh - copied 64 KB, over the next claim;
  * W1's copy CLAIM rounded `(I_SIZE + 1023) >> 10, jnc` - the shift's carry,
    not the add's - so MEGARMBF's 2 KB region claimed 64 KB, and
    megarmbref's leg M was green ON that defect (the filled heap could not
    give 64 KB). Fixed in `fsx_rlowclaim`; M now gives MEGARMBF a ~61 KB
    region, since a kernel claim still reaches the ~51 KB under a package's
    floor that the package's own fill cannot;
  * `mega_lincopy` spends AX..DI: the per-driver copy loop kept them.

  **Bytes** (the kern_mega map against W2; `cmp` of kern_big's and
  kern_small's `kernel.bin` and `kernel.sys` at one commit): **kern_big and
  kern_small BYTE-IDENTICAL**; kern_mega `.text` **+0**, `.bss` +0, `.cold`
  **+444**, `.mega` **+2,723** (2,016 the twins), `.lowbss` +4; the
  kernel-segment ratchet unchanged at 4,869. Per real-mode bracket: the
  service area's claim (3-4 KB) and HDD.DRV's copy (4 KB) when its volume is
  read. **`tools/kernsize.py` measures kern_mega**: `--build build/megak`
  (it detects the tree and adds `-DKERN_BIG -DKERN_MEGA -DKERN_KNOB`) or
  those three defines by hand; a plain run measures `build/`, which is
  kern_big. No failure to re-assemble reproduced on this tree.

  **Gates** (`tests/vidrmb.py` with MEGARMBX, `tests/megarmb/megarmbx.asm`;
  each run three times or more, green): **`vidrmbfrag`** (Bochs booted off
  C:, no driver: C: the BIOS's volume, 37-41 s), **`vidrmbhdd`** (Bochs with
  HDD.DRV: C: its DRVC_DISK volume, rung 1, 41-43 s), **`vidrmb286`**
  (86Box's 286-victory: the reset way down, XMEM by LOADALL, 6-7 s). Every
  leg checks every dword in the buffer, back through the COPY twin, and once
  more in protected mode after the bracket: RMBXC.DAT (197,608 bytes) in
  its own folder, RMBXB.DAT (100,000) on a B: floppy, FRAGA.DAT walked to the
  cap (224 single-sector runs, real mode) and FRAGB.DAT one past it
  (`FSXR_FRAG`, protected, bytes right); `FSXR_DRV` for DRVC_SOUND; HDV_ARM
  (with HDD.DRV: the whole map, every dword in XMS; without: no driver);
  inside, FERR_NAME for an unlisted name and FERR_NOENT for a listed
  missing one, READ_AT and XMEM_ALLOC refused, XMEM_CAPS equal, the COPY
  twin reading what protected mode wrote; after, the table, every claim and
  HDD.DRV's image back. W1's `megarmb` and `megarmb286` and W2's
  `megarmbref` re-run green (their packages pass BP = 0; megarmbref expects
  the five new far jumps).

  **Broken on purpose**, each red and put back:
  * every extent one sector late (`fsx_rwalkf`): C, B and F wrong in every
    dword (49,402 / 25,000 / 28,648), G right (protected mode);
  * the COPY twin's token translation a dword off: the round trip through
    the twin stayed GREEN - it is self-consistent - which is why the
    protected-mode re-read and N's read of what protected mode wrote were
    added; with them C, B, F and N are red.

  **Existing rows W3 can reach**, for the final pass (not run here):
  `megarmb`, `megarmb286`, `megarmbref` (run here, green); every row that
  boots kern_mega and opens an fsx bracket or touches `fsx_rprepf` /
  `fsx_rcopyf` (the megaport `vid*` rows - `vidvga4`, `vidmodex`,
  `vidplayvga`, `vidpage`, `vidclose`, `vidxipbank`, `vidxiphd` - whose
  packages do not ask `FSXF_REAL` yet, so they reach only the door's
  unchanged path); `megahddtick` and `megainst` (HDD.DRV's image is copied
  low and up again only in a real-mode bracket, but its row is read);
  `megaxmove` (`mx_tab` is read by the twins); `cswrite`, `stkbalance` and
  the ovlchk gate (fast, green).

  **What W4 and W5 inherit**: the list is how SOUND.DRV is asked for
  (FSXL_DRV DRVC_SOUND lifts `FSXR_DRV` once carried, with `DSV_RMODE`); the
  player passes its .V88 (and a layer file) as FSXL_FILE, its buffers'
  words through `apps/os88real.inc`, `FSXL_DRV DRVC_DISK` for HDV_ARM, and
  moves its XMEM_ALLOC/FREE out of the bracket.
* **W4. SOUND.DRV low.** The `SND_STREAM` and `FSX_SPK` twins (the latter
  writing IVT 20h), `DSV_RMODE` for a stream opened in protected mode and
  carried in (Live's F), and the audit that `DSV_TICK` between brackets
  never reads a stream's real-mode segments. Gates:
  * existing `vidsound` (86Box, SB2.0 the clock, 1,800 frames, capture
    byte-exact), `vidxipsnd`, `vidsnd`;
  * a new `vidrmbswap`: F toggles the bracket five times mid-play with the
    card's clock exact, starting from Live as well as from Play;
  * `pmbox` / `pmbox2` with the PicoMEM's own firmware: its multiplexer
    served natively in the bracket, and `MEGADIAG`'s IRQ count at 0 inside
    it.
  **W4 BUILT (2026-10-30)**, SPEC.md 53.2.3's *THE SOUND DRIVER (W4)* the
  contract. What was built, and where it differs from the text above:
  * **`DSV_RMODE` is `SNDV_RMODE` (13)**, a `DSV_PKGCALL` verb and not a
    service slot: a new `DSV_*` slot grows `DSV_SIZE`, which is in every
    kernel's copied driver tables, so kern_big and kern_small would have
    moved. The driver answers a LIST of its segment words (`sbl_rseg`, the
    external ring's control-word segment, the pool, the double buffer) and
    the KERNEL renames them - selector to paragraph at the drop, and at the
    return either the banked selector (a word the bracket left alone) or
    the selector of the claim based at that paragraph (`mega_p2d`, a word
    the bracket WROTE: a stream opened inside). So a stream opened in
    either mode plays on in the other, both directions, which is what
    Play-then-F needs as well as Live's F. The verb also flips the 8237
    rule (§34.5.2.2 reads the DMA count only where the line is masked
    across BIOS calls) and, at the DOOR (AL = 2), runs the IRQ discovery a
    Sound Blaster defers to its first stream: its probe stubs are IVT
    08h-0Fh, where the bracket's 8259 does not deliver, and its hook would
    name the low copy and outlive it. The first build had the first stream
    of the session open inside a bracket and every stream after it, in
    both modes, was silent.
  * **The copy is at the DROP and the RETURN, at IF = 0** (`fsx_rsnddn`,
    `fsx_rsndup`), not at the freeze's arm: the card plays across both
    switches, and a block IRQ between an IF = 1 copy and the drop is
    counted into the image that stops running.
  * **The card's line**: every IVT cell 08h-0Fh / 70h-77h naming the
    driver's code half is banked and pointed at the copy, a master line's
    twin at 20h + n too, the line taken out of the drop's masks - AND out
    of `[mega_rmask]`, which `mega_r286` compares a 286's reset against:
    left as `mega_rmaskf` said, the compare missed on `vm/286-hma`, the
    8259 was re-initialised and every real-mode leg played nothing, where a
    386 (no reset) was green.
  * **The tick** far-calls `rms_stick` (`DSV_TICK` through the low copy,
    DS = it, ES = `KERNEL_SEG`), and the hook runs at IF = 1 under the
    speaker's sample ISR, `sch_rhook`'s rule. **The audit**: `sbl_tick`
    reads the watchdog's counter and the PicoMEM's port and no segment
    word, so the four renamed words are everything a real-mode segment
    could reach between brackets.
  * **The twins**: `OSAPI_SND_STREAM` (`rms_sstr`, `osapi_snd_stream`'s
    register contract to the low `DSV_STREAM`, AX = 4 with no sink
    carried); `OSAPI_FSX_SPK` (`rms_spk`: AL = 2 writes IVT 20h and answers
    the chain `KERNEL_SEG:fsx_rtick`, AL = 1 closes, AL = 0 refused AX = 1
    as in protected mode, a tone sounding AX = 3). **The owner's rule is
    untouched**: the speaker's fast in-place play keeps the protected-mode
    bracket (5.3 ruling 1); the shipped speaker library still asks AL = 0
    in a real bracket and is refused - teaching `os88spk_go` AL = 2 there
    is W5's, with the player that chooses the bracket.
  * **A defect W2 shipped and W4's gate found**: `apps/os88real.inc`'s
    `OS88REAL_LIST` took its words as one greedy parameter (`2+`), so a
    list of TWO had a one-word bank and `os88real_prep` wrote the first
    word's real segment over the second word. `2-*` now; every list until
    MEGARMBS had one word.

  **Gates** (all green, each run twice or more; `tests/vidrmbsnd.py`,
  MEGARMBS - one 32 KB direct external ring at 8,000 Hz, a protected-mode
  CONTROL leg first, a stream opened in a bracket carried out, in, out and
  in to its close on a SECOND ring, a stream opened in protected mode
  carried in and out, a watchdog only `DSV_TICK` can fire, the speaker's
  twin with its own real-mode sample ISR):
  * **`vidrmbsnd`** (Bochs, SB16, 42 s): every leg's bytes a tick within a
    block and 10% of the control's, PLAYING at each end; the watchdog
    ENDS the masked stream in 11 ticks; S: K = 40, 23,600 samples = K x
    590 periods, 590 of 590 hook calls at IF = 1, IVT 20h `fsx_rtick`
    after the close; SOUND.DRV's ring words selectors of the ring's claim
    after the brackets; and the card's capture (`sb.wav`) one unbroken
    sawtooth for 21.2 s - 664 periods, 0 phase breaks, across ten
    switches. Bochs's SB16 runs its DMA on its own timer (~680 bytes a
    tick for 8,000 Hz), which is why the claim is against the control.
  * **`vidrmbsnd286`** (86Box `286-hma`, the mr286 with an SB 2.0, 10 s):
    the same legs by the 286's RESET way down, 14,336-16,384 bytes in 36
    ticks (15,818 due at 8,000 Hz: block-granular), read off the bss.
  * **`vidrmbsndpm`** (86Box `at286-picomem1`, a PicoMEM 1.x running its
    own firmware, 26 s with the CMOS cache warm) - **the plan's `pmbox`
    gate**: the card's Sound Blaster plays every real-mode leg at the
    control's rate, its interrupts served natively in the bracket where
    protected mode reflects each one. `MEGADIAG`'s IRQ count was not read
    (a knob kernel); that the stream counts at all inside is the answer,
    there being no reflection in real mode to count.
  * **Broken on purpose, each red**: the drop's IVT 20h + n twin left out
    (Bochs: the machine hangs - IRQ 5 reaches a selector); the return's
    `mega_p2d` left out (Bochs: `kern_mega FAULT 0D` in protected mode,
    and the words check); `rms_stick` a bare `retf` (W: not ENDED in 90
    ticks); `rms_spk` writing IVT 08h (S: 0 samples); `[mega_rmask]` not
    given the opened line (286: every real-mode leg 0 bytes); the IVT
    redirect taken out (PicoMEM: every real-mode leg 0 bytes). The 20h + n
    twin alone left out stays GREEN on the PicoMEM, which raises its SB's
    `int 0Dh` in software - so the twin and the 08h + n cell are both
    needed, one per kind of card.
  * W1-W3's rows re-run on W4: `megarmb`, `megarmb286`, `megarmbref`
    (taught the two new twins), `vidrmbfrag` (its D leg now `DRVC_NET`:
    `DRVC_SOUND` is carried), `vidrmbhdd`, `vidrmb286`, and `vidrmbems`
    merged in from `mega-rmb-ems` - all green.
  * **Not run, and why**: the plan's `vidsound`, `vidxipsnd`, `vidsnd`
    and `vidrmbswap` are the PLAYER's, and the player does not ask
    `FSXF_REAL` until W5, so they reach only the unchanged path;
    `vidrmbsnd`'s carried legs are `vidrmbswap`'s instrument-level
    equivalent and `vidrmbswap` itself is W5's.

  **Bytes** (`tools/kernsize.py`, kern_mega against W3's `def83132`;
  kern_big and kern_small `cmp`'d byte-identical - `kernel.bin` and
  `kernel.sys` of both - against W3 built at the same build number):
  kern_mega `.text` **+18** (`fsx_rtick`: the tick's far call 10, the
  hook's IF rule 8; `tests/megaseg.txt` 4,869 -> 4,887), `.bss` +0, `.cold`
  +0, `.lowbss` +2 (`fsx_rstko`), `.mega` **+848** - the service area's
  twins 2,016 -> **2,368** (+352: `rms_sstr`, `rms_spk`, `rms_stick` and
  the sound state), the protected-mode door, drop and return the rest -
  which crosses one `.mega` rung (23,552 -> 24,064: KERN_SIZE at boot
  +512, resident after the switch unchanged at 67,584, `.mega` going above
  1MB). SOUND.DRV **+56 bytes** of image and of file (`snd_rmode` and its
  list, inside what a machine keeps), on every disk that carries it - a
  verb every kernel's driver answers, priced here so the owner can call
  it trivial or not (KERN-MEGA-PLAN 14.66).

  **What W5 inherits**: the player lists `FSXL_DRV DRVC_SOUND`, opens or
  carries its stream as it does today and gets the card's clock in either
  mode; `os88spk_go` must ask AL = 2 when CPU_INFO answers real mode
  inside a kern_mega bracket (an AL = 0 refusal with AX = 1 is the sign);
  and a multi-word `OS88REAL_LIST` now works.

* **W5. The player.** The arm takes the real-mode bracket at its door
  (section 3.1.2), moves the 15 non-offered cells' call sites out of the
  bracket (section 7.2's table: a pre-claimed scratch for `vp_skdue`, the
  mute and unmute's claims, the layer split and the gap's claims, the
  poster's claim and the session stop at the exit), and deletes what section
  3.7 measured as dead: 368 lines of `video.asm`, the speaker library's and
  the shaper's protected-mode copies - ~~and does not keep the protected-mode
  bracket as a fallback~~ **(superseded by 5.3: the protected-mode bracket
  STAYS for the three plays 5.3 names and for every `FSXR_*` reason W2's
  door answers; the arm passes SI = 0, reads SI's answer onto the info
  card's line (5.3.2), and takes `apps/os88real.inc` for its claim words,
  whose `os88real_prep` CF = 1 is the player's own fallback; only what NO
  play can reach is deleted)**. `t_megapkg` must still pass (fast tier), and
  `tests/megapkg.txt`'s allowance for `video.asm` only turns one way.
  Gates: the megaport `vid*` rows (`vidvga4`, `vidmodex`, `vidplayvga`,
  `vidvga8`, `vidresidentvga`, `vidlivevga`, `vidpage`, `vidfskeysvga4`,
  `vidclose`, `vidwin`, `vidpreview`, `vidsound`, `vidcard`) and the
  `vidxip*` family. `vidlivevga` proves Live untouched.
  **W5 BUILT (2026-10-30)**, SPEC.md 98.3.24 the contract. What was built,
  and where it differs from the text above:
  * **The door** (`vp_rdoor`, at `vp_srun`'s `OSAPI_FSX_RUN`) asks
    `FSXF_REAL` unless one of the player's reasons holds - LIVE, SPEAKER
    IN PLACE, VERSION 2 IN PLACE, SHADOW IN PLACE (5.3 ruling 1; "in
    place" is the bank's OR THE HOLD's, as protected mode decided it),
    RESIDENT, SPLIT, MOVED, HIGH CLAIM - and passes the list (`FSXL_FILE`,
    `DRVC_DISK`, `DRVC_SOUND` for a card, `DRVC_EMS` for an EMS bank). The
    kernel's `FSXR_*` comes back in SI; both land in `[vp_rwhy]` and on the
    info card's ninth line (5.3 ruling 2).
  * **The XMS claims did NOT move to the door, and this is why**:
    `vp_lysplit`'s split frees and re-claims on verdicts taken by the
    session's FIRST FRAME (the CPU timed, the disk's verdict), so there is
    nothing to decide at the door. That one session - a layered file's
    first, with an XMS layer bank - takes protected mode (SPLIT) and every
    later one real mode. `vp_xfree` and `vp_lyxfree` inside a real bracket
    (a refused copy, a refused map) drop the bank as ever and OWE the
    `OSAPI_XMEM_FREE` (`vp_rxfree`), made after the bracket (`vp_rxgive`).
    The seek's scratch and an Unmute's ring ARE the door's (`vp_rclaims`).
  * **The ring above 1MB is DELETED** (98.3.18.17, 456 bytes): it was the
    one PKG_MEGA piece that existed only for protected-mode streaming, and
    keeping it would have sent every streamed play on a machine with XMS
    to protected mode - the first build of the door had it as a reason and
    `vidrmbplay` would have read no real bracket on any XMS machine. 5.2
    item 4 and 5.3 ruling 3 accepted the conventional ring.
  * **Nothing else became dead.** The dual bracket keeps every
    protected-mode streaming path reachable: the fallback (SPLIT, the
    kernel's reasons, HIGH CLAIM) streams exactly as before, Live is
    protected mode by ruling, and the speaker's, version 2's and the
    shadow's in-place reads stay there by ruling. Section 3.7's other DEAD
    rows were written for a bracket that always took real mode, and 5.3
    reversed that.
  * **Three defects the gates found**:
    1. **the speaker in a real bracket hung** (`megavidbig`'s held leg,
       `megavidring`'s lin80): the library's ring segment (`os88spk_rseg`)
       and the shaper part's far vectors and data segment were set up on
       the desktop as SELECTORS. `vp_rin` re-binds both (`vp_spkbind`,
       `vp_fxbind`, the part's new `VFXV_BIND`, VFX_ABIVER 2) and
       `vp_rpost` binds them back;
    2. **a 286's first real-mode borrow reset the machine**
       (`mega-vidwin`, vm/at286-port): `vp_xok` never ran `la_capture`
       because protected mode decided the hold in place, so LOADALL loaded
       an IDT of limit 0. `vp_rin` captures it, every bracket;
    3. **the megaport harness read the region, not the copy that runs**
       (`mega-vidplayvga`, `-vidpage`, `-vidwin`, `-vidpreview`): every
       row takes the region's base before the play. Under the deleted ring
       those plays were protected mode and nothing showed it.
       `tools/os88megacommon.py` maps a read, a write or an exec
       breakpoint inside the bracket window's region to `[fsx_rseg]` while
       `[fsx_rmode]` is 1 (a breakpoint on `fsx_rland` re-arms the set for
       each copy and is never seen by the row). And `tests/vidwin.py`'s
       hold at 100 is armed off a 0.02 s poll: at 86Box's 6x a 0.3 s poll
       was 54 frames and the faster real-mode swap ran past it (2 in 3).

  **Gates** (all green): the plan's megaport `vid*` rows above (`vidwin`
  4 of 4 after its poll, and the base `vidwin` on MartyPC), `vidsound`,
  `vidcard`, and a new **`vidrmbplay`** - WHICH bracket a play took, read
  two ways that do not lean on each other ([fsx_rmode] polled during it,
  [vp_rwhy] after): a speaker play at 1MB REAL with its chain the real-mode
  door's, a hold under the speaker at 16MB PROTECTED (SPEAKER IN PLACE),
  the same hold through an SB16 REAL, a Live clip full screen PROTECTED
  (LIVE). **Broken on purpose**: the player of `02a5f114` (before the
  rebind) hangs `spk` and `held`; `vp_rdoor` without `FSXF_REAL` turns
  `spk` and `card` red on [fsx_rmode] while [vp_rwhy] reads 00h either
  way; without the `la_capture`, `mega-vidwin` resets the machine; without
  the harness's copy, `mega-vidplayvga` times out at its first hold.
  Re-run on the final tree: `megavidbig`, `megavidring`, `megavidtier`,
  `megavidv2`, `vidrmbfrag`, `vidrmbhdd`, `vidrmb286`, `vidrmbems`,
  `vidrmbsnd`, `vidrmbsnd286`, `vidrmbsndpm`, `megarmb`, `megarmb286`,
  `megarmbref`. **Not run**: the `vidxip*` family - the shipped player is
  byte-identical and kern_big with it, so they reach nothing W5 changed.

  **Bytes**: kern_big, kern_small and kern_mega `kernel.bin`/`kernel.sys`
  `cmp`'d byte-identical against W4's sources built at the same build
  number; the shipped `video.bin` (59,926) and `video.o88` (48,357) too.
  The arm 63,308 -> **64,430** of `APP_MAX_SIZE`'s 65,024 (image 60,687
  -> 61,773, bss 2,621 -> 2,657), `video.o88` 52,628 -> 53,665; part 2
  (the shaper) 2,421 -> 2,436. The other arms the speaker library reaches
  (`os88spk_go`'s AL = 2 retry, PKG_MEGA only): AUDIO 9,593 -> 9,607 and
  TRACKER 27,018 -> 27,049 file bytes; MIDIRACK and XTMP3 the same size.
  **OPEN, AND W6 NEEDS IT**: the arm's `VPDIAG=1` build is 65,726 and
  `VPDIAG=time`'s 65,913 - over by 702 and 889 bytes, so neither
  assembles (W4's were 64,800 and 64,987: 224 and 37 to spare). The arm's
  diag card was trimmed to nine lines (144 bytes of bss; it carried four
  lines it never wrote). What else is weighable (`VP_NOLIVESND` MEASURED,
  the rest ESTIMATED from the routines' spans): `VP_NOLIVESND` 233; the reason NAMES in the diag arm as numbers ~250; the shaper's glue
  (`vp_fxget`, `vp_fxinit`, `vp_fxbind`) ~270 if the diag arm honoured
  `VP_NOSPKFX` again; the speaker library's protected-mode ISR family ~430
  if the diag arm played the speaker in real mode only. Any two are not
  enough; the choice is the owner's.
  **RESOLVED the same day without dropping a feature** (SPEC.md 98.3.24.1):
  the door, its afterword, the card's ninth line and, in a diag build, the
  profile's two ends and the reflection counts moved out of the image into
  a fourth PART, `apps/video/vcold.asm` - a LINKED part, PiXEL's shape,
  reading the player's words by the names `tools/os88plink.py` takes from
  the player's own map and calling its routines back through a trampoline.
  The arm 63,756 (1,268 to spare), `VPDIAG=1` 64,544 (480), `VPDIAG=time`
  64,723 (301); every shipped build byte-identical. The part was first
  fetched at launch and PINNED: fetched at the first door, `megavidv2`'s
  lin320 drew 72 wrong pixels, and movable, its modex leg had the player
  CLOSED by a #GP in the next stop's key picture (`vp_after` -> `vp_kpic`
  -> `vd_n_p3`). **Both were ONE KERNEL DEFECT, now FIXED** (KERN-MEGA-PLAN
  14.128, SPEC.md 50.6.11): `SEG_SUB` answered a key shadow's plane with the
  player's own `SEG_AT` window, which the next record's read re-aimed. With
  `mega_dfind` refusing windows (+16 bytes of `.mega`, kern_big and
  kern_small byte-identical) the part is MOVABLE again, the shaper's shape,
  at +16 bytes of each arm: 63,772 (1,252), 64,560 (464), 64,739 (285).
  `mega-vidgrey` mirrored the card at eight lines and is taught nine.
* **W6. Parity, on the Victory** - **BUILT, 2026-10-30: PARITY**
  (docs/reports/VIDPROF-W6-2026-10-30.md). Under the `MEGADIAG` kern_mega
  with the `VPDIAG=time` player, Catch My Heart drew every frame with **5
  stalls, 70 late and 0 pauses** against kern_big's 24 stalls and 222 late;
  Sonic 2 with **0 stalls, 8 pauses and 79 late** against kern_big's 6
  pauses and 296 late - the pauses on both kernels being the stream running
  dry near frames 690-720, the file's and not the kernel's. The reads run at
  503 and 499 KB/s against 490 and 494, the frames-behind median is 0 on
  all four, and the bracket made **4-6 BIOS reflections and one option-ROM
  IRQ in a whole play**. So `mega_disk` came out at parity and the
  encoder's `--kernel mega` is RETIRED with it (SPEC.md 98.2.3.8, options
  version 51: a record that said mega reads as kern_big's); `xrd` stays,
  being the reader's own cost on both kernels. The same evening's field
  defects - the text's glyphs, a short name's silence and the lead's
  static - are KERN-MEGA-PLAN 14.131-14.133, gated by `vidrmbtwice`.
  The story as it was set (field, docs/FIELD-MACHINES.md). Send
  `make VPDIAG=time` and `make megadiag` builds of both kernels' players.
  * **The measurement**: Catch My Heart and Sonic 2, the owner's
    kern_big-encoded files, played on kern_big and on kern_mega with the
    real-mode bracket. Profiles `VPPROFn.DAT`, read by
    `tools/os88vfit.py FILE.V88 VPPROFn.DAT --reads`.
  * **Parity**:
    * the bank runs dry no earlier than kern_big's (Catch 1312, Sonic 733),
      or not at all;
    * the frames behind median is 0, as kern_big's is;
    * the reads' rate is within 2% of kern_big's;
    * the `MEGADIAG` service and IRQ reflections inside the bracket are 0
      (the trailer's `vp_rq` words);
    * the share in reads and in drawing OVERLAPS as on kern_big, where
      14.124 shows them adding.
  * Then re-take `mega_disk` from those plays. At 1.00, `--kernel mega`
    keeps its meaning and changes nothing, and no second encode is ever
    needed (the owner's rule).
  * Repeat on `vm/286-victory` with the XT-IDE disk (14.73's reproduction)
    as the in-house twin.
* **W7. The states a user is in that W2 sends to protected mode** (the
  owner, 2026-10-30: *"We need to not refuse states the user is going to
  be in, and the Video Player supports playing on either screen in
  fullscreen"* - it may come as the last wave). W2's owed table names each
  reason; this wave lifts them one at a time, each by deleting its one test
  in `fsx_rprepf` once its state works in real mode:
  * **`FSXR_EXTD` - the extended desktop.** EXTD.DRV's entry block (`EXFP`)
    is far-called by `OSAPI_FSX_MODE` (`EXTF_KIND`, the collapse),
    `OSAPI_FSX_CAPS` and `OSAPI_WM_CONTENT` (and `fsx_run`'s restore, in
    protected mode). Serve the EXTF verbs those cells reach in real mode -
    real-mode targets in `.text`, or a LOW COPY of the module's image for
    the bracket on W4's SOUND.DRV pattern - and gate it with a fullscreen
    play on EITHER display of `xt-multimon`-class hardware (a mode set on the
    secondary through `OSAPI_FSX_RUN` CH = n), with the window on the other.
    The census must re-walk the module's own code for selectors.
  * **`FSXR_SVC` - a driver's worker alive** (ETHER's, a sound stream task):
    the real-mode bracket has one task. Either tell the driver (a verb, as
    W4's `DSV_RMODE`) so its worker parks for the bracket, or run its work
    from the real-mode tick through its low copy.
  * **`FSXR_SERIAL` - a SERIAL.DRV lease open** (§115): the pointer's ISR
    far-calls `SERFP + SER_IRQ` for the leased UART on every interrupt. A
    low copy of the module, or the ISR leaving the leased row to a
    real-mode capture the module is told about.
  * **`FSXR_ROOM`** is W5's in part (the conventional ring sized to leave
    the region's copy room) and 5.3.3's HMA idea after.
  Gates: `megarmbref`'s S, E and L arms turn from FALLBACK to TAKEN as each
  lifts (the row's asserts change in the same diff), plus the two-display
  play.

  **BUILT (2026-10-30): all three tests are gone from `fsx_rprepf`** (SPEC.md
  53.2.3's W7 paragraph is the contract). The extended desktop: EXTD.DRV
  takes a fourth row of the service area's low drivers (`RMD_EXT`, its
  whole claim by `lsl`, copied with the region's) and the drop aims every
  `EXFP` slot's SEGMENT at it, offsets kept; the census of the module's
  bracket path is `EXTF_KIND` and `EXTF_ENTER` alone (`OSAPI_FSX_CAPS` is
  not a kept cell and `wm_content`'s arm is resident) and it is real-mode
  code as written. A driver's worker: parked, nothing to tell - SB's are
  covered by 53.3's release and §34.5's underrun contract, USBMOUSE's poll
  simply stops. A lease: `SERFP`'s segments the module's paragraph (its
  claim is conventional, an `%error` keeps it so), the ring served where
  the module can reach it in real mode - the REGION's own copy when the
  lease is the bracket package's (a service-area copy there was overwritten
  by the region's copy-up at the return: the first build's L leg sent the
  byte and the owner's ring said it had not), else a 517-byte copy in the
  service area - and its line left open when its vector is the kernel's.
  **Bytes**: kern_big and kern_small BYTE-IDENTICAL (`cmp`, both
  `kernel.bin`/`kernel.sys`, one commit count); kern_mega `.mega` **+336**
  (22,560 -> 22,896, crossing a rung), `.lowbss` +8, `.text` +0, `.cold` +0,
  the kernel-segment ratchet unchanged. **`megarmbref`** (Bochs): S and E
  TAKEN, and L a REAL lease on COM1 whose byte, queued by hand, is sent
  from inside the bracket by the pointer's ISR at IVT 24h - the owner's
  ring in an `OS88_HIGH` region shows it gone after (x = 0101). Broken on
  purpose - `srl_seg` left the owner's SELECTOR for the bracket - the byte
  never reaches the owner's ring (0100), red.

  **The two-display gate is `megarmbext`** (86Box, a new profile
  `vm/286-mega-multimon`: `vm/286-victory`'s AMI 286 with a Paradise VGA and
  a HERCULES, a monitor each, Extend in SYSTEM.CFG). Its first boot is
  `tests/megarmb/megarmbd.asm`'s MEGARMBD: an `FSXF_REAL` bracket on CH = 1
  and on CH = 2, each SI 0 with no `CPU_F_PM`, every `EXFP` slot on the low
  copy's paragraph inside and one display, mode 13h's fill on the VGA and
  FFh on the Hercules ON THE GLASS with the other monitor dark (the patched
  86Box's `screenshot1`), and after it two displays with the slots byte for
  byte. Its second boot is the VIDEO PLAYER's arm itself, a streamed VGA8
  file and a one-bit Hercules one - the player picks the display its file
  is for wherever its window is - each played full screen REAL on its own
  display. Broken on purpose - the `FSXR_EXTD` test put back - 8 FAILS, SI
  3 on every bracket and play; EXTD.DRV's row left empty instead, the HERC
  mode set is refused on the VGA's kind and nothing collapses.

  **The player's legs found a W5 defect that W7 had to fix to be gated at
  all** (SPEC.md 98.3.24.2): on a 4 MB 286 the door answered `FSXR_ROOM` to
  EVERY streamed play - with one display too, and on the tree before W7 -
  because the ring took the conventional heap (480 KB, 16.5 KB left) and,
  being on the 64 KB page grid, left even an 80 KB-smaller ring's slack in
  two runs (46.5 + 29 KB) where the region's copy needs 63 in one. The
  plan's own W5 row had owed "the ring's sizing leaves the room" and it had
  not been built. `vp_rbhold` claims 80 KB top-down before the ring sizes
  itself and `vp_rbfree` hands it back after: +60 bytes of the arm, the
  shipped player byte-identical. **This bears on W6**: the Victory's field
  run, on the W6 disks, would have read protected mode on every streamed
  play.

---

## 5. Open questions for the owner

1. **Only when it pays, or always?** The real-mode bracket for every
   streamed play, or (recommended) only on a BIOS-served disk or an option
   ROM's card. Always means one code path, but it gives up rung 1's native
   PIO on machines already at parity.
2. **Is 55-70% of the arm enough**, or should the session's desktop half be
   redesigned so the player holds no claim on the desktop (section 3.7's
   route to 90%)?
3. **A real-mode fault inside the bracket hangs without a `kern_mega FAULT`
   line.** That is kern_big's behaviour. Acceptable?
4. **The first fill.** Leave it in protected mode, which costs start
   latency only, or take it inside the bracket too?
5. **Other fsx programs.** `FSXF_REAL` is a general bracket flag. Open it to
   other packages now (games with a fullscreen disk loader), or keep it to
   the Video Player until the field says it holds?

### 5.1 The owner's answers (2026-10-29)

1. **Whichever is simpler** - to the owner's mind "always real", unless
   there is a benefit to the dual-mode bracket or it is actually the simpler
   and more reliable one. DECIDED: **always**, the one code path, for every
   streamed fsx play that asks - unless W0's census finds a concrete loss
   (a native-disk 286 whose rung-1 PIO the real-mode bracket cannot keep),
   in which case W0 says so with the measurement before W1 starts.
2. **The percentage is not a target.** What is: no dead code left behind -
   whatever the bracket makes unreachable in the kern_mega arm is deleted in
   the wave that makes it so, however little that is.
3. **A fault inside the bracket hanging like kern_big's: acceptable.**
4. **The first fill: wherever the switch point is cleanest and works** -
   the implementer's call, written down with the reason.
5. **The flag is GENERIC.** `FSXF_REAL` is open to every fsx package from
   the day it ships, documented in SPEC.md 53 as a bracket flag like
   `FSXF_RATE`: other programs are expected to hit the same wall, if not as
   hard as the Video Player.

### 5.2 What W0 owes the owner before W1 starts (2026-10-29)

**The always-real verdict: it STANDS for the loss 5.1.1 named, and W0
found three others.** None of the four is a loss against kern_big; three
are losses against TODAY's kern_mega, and one of those is measured. 5.1.1
asked to be told before W1 starts, so W1 waits on rulings 2 and 3 below.

1. **The named loss does not exist: rung 1's native PIO survives the
   drop.** HDD.DRV's `DSV_BLK` (`hd_blk`, `drivers/hdd/hdd.asm:176`) goes
   straight to `hd_xfer` (`:316`) and rung 1's `hd_xfer_ide` (`:389`): port
   I/O, the buffer walked by paragraph arithmetic (`hd_buf_step`,
   `:652-659`, `add word [hd_bseg], 0x1000`), and no kernel call at all on
   rung 1's path; rung 0's one call is `OSAPI_SEG`'s `SEG_LIN`
   (`:586-589`), which the real-mode table answers. HDD.DRV is
   `OS88_DRV_HIGHOK` (`:46`) and so is above 1MB, which is why step 4 copies
   it low (≤ 4 KB). **The real-mode bracket also GAINS rung 1's read
   straight into XMS** (`HDV_ARM`, §98.3.18.15), which HDD.DRV refuses
   under `CPU_F_PM` (`drivers/hdd/hdxms.inc:75`) and the player calls inside
   the bracket (`vp_harm`, `video.asm:7445`). What the native-disk machine
   pays instead is per BRACKET and not per frame: two mode changes, the
   region's two 63,309-byte copies, and the drivers' - start latency, not
   play speed. Not measured; W3's `vm/286-video` row is the measurement.
2. **LOSS - in-place reads under the speaker's clock, the shadow and a
   version 2 file** (MEASURED for the speaker, §98.3.18.16 / 14.16.1, 86Box
   `vm/286` at 12.5 MHz, 240 frames at 24 fps): a held file with an 11,025
   Hz PCM8 sound on the speaker plays **182 ticks of 181, Late 0** read in
   place under protected mode, against **375 ticks, Late 477** with the
   refusal real mode needs; banked, **195 / 27 against 403 / 546**. The
   shadow measured no difference (182 against 183); version 2 was not
   measured. Real mode refuses these in place because its borrow holds
   interrupts off (LOADALL on a 286; on a 386 an ISR's DS reload loses the
   loaded base) - the shipped rules at `video.asm:7070-7076` and
   `:7179-7181` - so the play copies or reads the disk, which is
   kern_big's behaviour on the same files. **Ruling asked**: accept it as
   kern_big parity (recommended - it is the stated target), or keep these
   plays in a protected-mode bracket (a second code path, which 5.1.1
   declines), or recover it in real mode later on a 386 with a base in a
   register no ISR reloads (FS/GS), a design nobody has costed.
3. **LOSS - streams from a volume whose driver cannot run low** (FUNCTIONAL,
   unmeasured how often anyone does it): the extent reader serves
   `DVK_BIOS` volumes by `int 13h` and `DVK_DRV` block volumes through a low
   `DSV_BLK` (HDD.DRV; RAMDISK.DRV too, whose blocks are `OSAPI_XMEM_COPY`,
   a twin). It cannot serve an FS driver or a redirected volume
   (`DVK_FILE`, `kernel/disk.inc:310`): NET.DRV's cable share, HTTPFS.DRV
   (over ETHER.DRV and the network), CDROM.DRV (an FS driver). Today's
   protected-mode bracket reads them through the kernel. **Ruling asked**:
   refuse such a play with its reason (recommended for W3: a one-line
   greying, §47, and a copy to a disk plays), or add a protected-mode
   UP-CALL for `READ_SEQ` on those volumes - an inverted reflection, which
   needs the region's low copy and the hook visible to protected mode
   while it runs and the pool pinned (`MX_FPIN`, `kernel/mega.inc:6991`)
   across it, none of which exists.
4. **LOSS - conventional memory** (FACT, arithmetic): the ring above 1MB
   (§98.3.18.17, 14.41) goes, so a session's ring is sized from and held in
   conventional memory on the desktop, as on kern_big; and a play needs
   conventional room for the region's low copy (63,309 bytes for the arm
   today), SOUND.DRV (8 KB), HDD.DRV (4 KB) and the module (~5-6 KB) on top
   of the ring. Kern_big parity again; accepted unless the owner says
   otherwise. (Dropping `OS88_HIGH` from the arm instead would skip the
   copy but hold the region low all session, and `tests/megahigh.txt` only
   turns one way.)

---

### 5.3 The owner's rulings on 5.2 (2026-10-30) - the bracket is DUAL

5.1.1's "always real" gives way where W0 found a real loss, because only
~21% of the arm becomes dead anyway: **the protected-mode bracket STAYS, as
the fallback**, and the real-mode bracket is the default.

1. **In-place reads that real mode must refuse** (5.2 item 2): keep them in
   the PROTECTED-MODE bracket where that is faster - "if it is easy enough
   to keep the extra code and keep these faster, that is optimal".
   * **The PC speaker's clock: keep it fast** (the protected-mode bracket,
     182 ticks against real mode's 375 on `vm/286`).
   * **The shadow: not a concern** - it is always slow, the fallback for "we
     cannot play that here"; it may take whichever bracket is simpler.
   * **Version 2**: the owner asked why it would be slower. Not the block
     decoding itself: in REAL mode an in-place read of the bank borrows a
     segment by LOADALL (a 386's loaded base) with interrupts off, and the
     decoders let interrupts back in only at their YIELD sites (vdec.inc's
     VD_YCK, `vd_ypatch`); the block decoder part (`vblk.asm`, 98.3.23) has
     none, so the shipped real-mode rules refuse version 2 in place
     (`vp_xbipq`'s `vp_v2` test) and the play copies each chunk down out of
     the bank instead - one 32 KB copy a chunk more. In protected mode the
     bank is read through selectors with interrupts on, so the question
     does not arise. So: version 2 in place takes the protected-mode bracket
     too, until (if ever) `vblk.asm` gains yield sites.
2. **Streams from FS-driver or redirected volumes** (5.2 item 3): the
   PROTECTED-MODE bracket, as today, with a line on the INFO CARD saying why
   the play is slower (the card already says which bank a play used; this
   says which bracket and the reason).
3. **Conventional memory** (5.2 item 4): accepted. If the HMA is usable from
   the bracket, the region's low copy and the drivers could go there later;
   62 KB is close to what fits, so something may still have to move lower.

**So W1-W5 build the real-mode bracket BESIDE today's, chosen per play**:
real mode unless the play is one of the three above, and the kern_mega
arm's protected-mode bracket code stays for them - only what NO play can
reach any more (the ring above 1MB where a real-mode play no longer needs
it, and anything else W5 proves unreachable) is deleted.

## 6. What this plan could not determine

* ~~Which kern_mega `.text` bodies on the bracket's whitelist are
  mode-clean.~~ **W0 answered it** (section 7.3): twelve run as they are
  once the drop has made four kinds of word real segments again; eleven
  need twins; fifteen cannot be offered.
* Whether the PicoMEM's option ROM IRQ handler behaves identically with the
  PIC based at 20h for a whole play. The reflections already run that way,
  one call at a time, so it is likely. W4's `pmbox` rows are the answer.
* The true reset cost on the Victory: 412 µs, 1.2 ms and 3.3 ms are all
  quoted (section 1). It does not decide anything here, being paid once a
  bracket.
* Whether Live needs `VB_PM` (version 2 block files under Live), which
  decides whether `vblk.asm`'s protected-mode assembly survives. W0 did not
  settle it: `vp_canlive` (`video.asm:9432`) tests the format and the
  layout and never `[vp_v2]`. Part 1 runs correctly in real mode too (it
  writes nothing through CS), so the bracket may keep it; whether part 0's
  CS stores are worth fetching it as well is W6's to measure.
* ~~Whether `apps/os88xgap.inc`'s placement guess ... needs `mx_tophys`
  once the bank is read in real mode.~~ **W0: every real-mode reader of a
  token needs it**, not only the guess, and correctness IS affected once a
  block has moved: the door hands the readers the block's current linear
  base (section 7.4).
* Whether any shipped Live file is `BIGSP` (super-packets past 32 KB):
  `vp_canlive` does not refuse one, so `vp_pmbig`'s in-place arm is kept as
  Live's.
* **Whether the static reach is tight.** Section 7's classes come from
  call graphs over the listings, which over-approximate (a path the code
  guards at run time is still a path). They can only make a block look MORE
  needed than it is, so the deletions of section 3.7 are a floor: W5 may
  find more dead once the door exists, and must delete it (5.1.2).


---

## 7. The W0 census (2026-10-29, on `df2e1fbf`)

**How it was taken**, so that it can be taken again: the player assembled
twice into a scratch directory, plain and `-DPKG_MEGA`, each with a NASM
listing (`nasm -f bin -w+error -I apps/ [-DPKG_MEGA] -l ... apps/video/video.asm`);
kern_mega built into this tree's own `build/megak/` (`make BUILD=build/megak
KERN_MEGA=1 build/megak/kernel.bin`) for its map, and assembled once more
with a listing under the same defines. Every `%if` naming `PKG_MEGA` was
enumerated with its arms, and each arm's bytes were summed from the listing
(a macro's or an include's output charged to the line that invoked it, a
nested block counted once). Call graphs were built from the listings'
`call`/`jmp`/`j*` operands and fall-throughs, and REACH means reachable
from a set of roots through them: the BRACKET is `vp_main` (the entry proc)
and `vp_hook` (the rate hook); LIVE is `vp_lplay`, `vp_worker` and the
other `vp_l*` entry points of §98.3.10; the DESKTOP is the entry proc and
every callback the window registers, with the bracket's and Live's roots as
stops. A static graph over-approximates, so every class below errs towards
NEEDED (section 6). The scripts were scratch and are not in the tree.

### 7.1 The `PKG_MEGA` arms of the player, classified

`apps/video/video.asm` has **164 conditional blocks** naming `PKG_MEGA`
(160 `%ifdef PKG_MEGA` and four `%if`s of an expression that names it). Their kern_mega-only arms hold **1,771 lines** (1,511 that are not
comments or blank) and assemble to **3,849 bytes**; their shipped-only
`%else`/`%elif` arms hold 190 lines and 3,393 bytes, of which 1,891 are
`os88spkfx.inc`'s include (`video.asm:21432-21434`). Outside `video.asm`:
`apps/os88spk.inc`'s arms hold 32 lines and assemble to 433 bytes in the
arm's `video.bin` (362 of them the protected-mode ISR family,
`os88spk.inc:634`); `apps/os88spkfx.inc`'s hold 49 lines and 476 bytes, all
in part 2 (`apps/video/vfx.asm`, 69 lines, a 2,421-byte part);
`apps/os88xgap.inc`'s one is 2 bytes; `vdec.inc` and `vosd.inc` have none;
`vblk.asm` has 21 `VB_PM` sites in 80 lines (part 1 is 5,028 bytes against
part 0's 4,746).

**By reach** (the class of the routine each line is in):

| class | lines | not comment | bytes | what |
|---|---|---|---|---|
| BRACKET only | 65 | 51 | 120 | `vp_bnsel`, `vp_fseek`, `vp_hook`, `vp_lstage`, `vp_lybench`, `vp_lysplit`, `vp_main`, `vp_skdue` |
| BRACKET and LIVE | 17 | 16 | 36 | `vp_kmove`, `vp_lydec`, `vp_rptr` |
| reached from the DESKTOP | 1,525 | 1,331 | 2,489 | 100 routines: the session's start and claims, the timer's pre-fills, keys, poster, info card, Live's entry paths |
| data, strings and diagnostics | 164 | 113 | 1,204 | `vp_s_buf`'s 968-byte buffer, the `VP_DIAG` text, `vp_prend`, the arm's state words |
| **total** | **1,771** | **1,511** | **3,849** | |

**By mechanism** - what the real-mode bracket makes unreachable once every
bracket takes it and nothing falls back (section 3.1):

| mechanism | where | lines | bytes | verdict |
|---|---|---|---|---|
| the ring above 1MB (14.41) | `vp_rhiq`, `vp_rhkb`, `vp_rhtry` (with its nested block), `vp_rhfit`, `vp_rhoff`, `vp_rhcp`, `vp_bnc`; their calls in `vp_sstart` (`video.asm:11607`, `:11672`), `vp_kroom`, `vp_sfree`, `vp_fill` (`:18143`, `:18173`), `vp_bbnc`, `vp_xbread`, `vp_xbup`, `vp_xfillt`, `vp_prend`; `vp_rhi`, `vp_dbnc`, `vp_dbclm` | 286 | 458 | DEAD |
| bracket-only routines' arms | `vp_main` (`:15287`), `vp_hook` (`:18619`, `:18641`: §98.3.22.6), `vp_skdue`, `vp_fseek`, `vp_lstage`, `vp_lybench`, `vp_bnsel`, `vp_bset` (`:24928`, called only at the door) | 69 | 138 | DEAD |
| the bank in place under protected mode | `vp_xbipq` (`:7186-7241`) | 13 | 34 | DEAD (the bank is read only inside a bracket) |
| the speaker's protected-mode ISRs | `os88spk.inc` | 32 | 433 | DEAD (the speaker plays only in a bracket) |
| the shaper's protected-mode copies | `os88spkfx.inc:75-83`, `:584-627` (part 2) | 49 | 476 | DEAD |
| the shaper as a part | `vp_fxget`, `vp_fxinit`, the `vp_b1v` thunks, `vp_smov`/`vp_smtab`'s word, `vp_sndprep`, `vp_aput`; `vfx.asm`, `vfx.inc` | 126 + 110 | 243 | OPTIONAL (fits the plain arm, not `VPDIAG=time`'s, section 3.7) |
| MC7's segments for the session | `vp_at`, `vp_atn`, `vp_sub`, `vp_subtab`, `vp_rsmk`, `vp_rslot`, `vp_plmk`, `vp_plset`, `vp_lymk`, and the 60-odd sites that call them | | | STAYS: desktop and Live; runtime-dual |
| the hold in place through selector runs | `vp_xrunmk`, `vp_xmk`, `vp_xok`, `vp_xipq`, `vp_pmbig` | | | STAYS: Live (§98.3.18.1) |
| a resident block's windows | `vp_rui`, `vp_rres`, `vp_rwmk`, `vp_raud` | | | STAYS: Live's resident files |
| `vp_addr`'s protected-mode body | `vp_paddr`, `vp_rhpat` | | | STAYS: desktop pre-fill, Live; unpatched at the door |
| page-safe conventional claims | `vp_rclaim`, `vp_rclk1`, `vp_mclaim`, `vp_mclhi`, `vp_mfree` | | | STAYS |
| the region's identity | the header, `vp_self`, `VP_OWNB`, `vp_reloc` | | | STAYS (`vp_reloc` is what the region's low copy calls) |
| the block decoder's part 1 | `vp_bget`'s row choice, `VB_PM` | | | STAYS while Live may play version 2 (section 6) |
| the reflection counts | `VP_RQ` (`video.asm:132`, `vp_srun`) | | | STAYS until W6 has read them |

**Sizes against `APP_MAX_SIZE` (65,024)**, measured from the headers the
two assemblies wrote (image + bss):

| build | image | bss | total | headroom |
|---|---|---|---|---|
| shipped | 59,794 | 2,621 | 62,415 | 2,609 |
| kern_mega arm | 60,688 | 2,621 | 63,309 | 1,715 |
| shipped `VP_DIAG` | 61,379 | 2,801 | 64,180 | 844 |
| arm `VP_DIAG` | 61,999 | 2,801 | 64,800 | 224 |
| shipped `VP_DIAG VP_RDTIME` | 61,566 | 2,801 | 64,367 | 657 |
| arm `VP_DIAG VP_RDTIME` (`VPDIAG=time`) | 62,186 | 2,801 | 64,987 | **37** |

### 7.2 The real whitelist: what the shipped bracket calls

Walked off the SHIPPED build (the one 5.1.1 says the bracket runs), from
`vp_main` and `vp_hook`, through every include the player assembles:
**38 API cells**. Two more the graph named (`OSAPI_PKG_REHOME`,
`OSAPI_TOAST`) are a fall-through artefact past `os88_rseq_new`, which is
a `rep stosw` and a `ret` (`apps/os88rseq.inc:75-91`). **The rate hook
calls no cell at all** in the shipped build; the arm's calls `OSAPI_SEG`'s
`SEG_AIM` from it (`vp_pmbig`, `vp_raud`, `vp_rres`), which is protected
mode's alone.

| cell | kern_mega body | where the bracket calls it | real mode |
|---|---|---|---|
| `FSX_MODE` | `fsx_mode`, `.text` | `vp_main` (`video.asm:15143`) | AS-IS once the video words are segments: it reads `fsx_mtab`'s rows and `vid_setmode` loads `[k_seg0]` - **and (W1) once EXTD.DRV's slots are a real-mode `mod_gone`: it far-calls them (`EXTCALL EXT_FSX`), as `fsx_caps` and `wm_content` do, and hung there in W1's first build** |
| `FSX_SURF` | `fsx_surf`, `.text` | `vp_main` (`:15126`), in-window | AS-IS |
| `FSX_PAGE` | `fsx_page`, `.text` | `vp_hpg2` (`:20084`) | AS-IS (its wait is ports) |
| `VIDEO` | `osapi_video`, `.text` | `vp_main`, `vp_dinfo`, `vp_video` | AS-IS |
| `FONT_GLYPHS` | `osapi_font_glyphs`, `.text` | `vo_setup` (`vosd.inc:78`); `vp_tqbuild` on the exit path | AS-IS once the glyph table's segment is real |
| `KEY_DOWN` | `kbd_down`, `.text` | `os88alt_edge` (`os88alt.inc:72`) | AS-IS |
| `SND_CAPS` | `osapi_snd_caps`, `.text` | `vp_ringkb`, `vp_scaps`, `os88spk_lptq` | AS-IS |
| `WM_CONTENT` | `wm_content`, `.text` | `vp_track` (`:21696`), in-window | AS-IS |
| `WM_WAKE` | `wm_wake`, `.text` | `vp_wakew`, on the stop path | AS-IS |
| `GET_TICKS`, `CPU_INFO`, `SET_COLOR` | inline cells | everywhere | AS-IS (`CPU_INFO` answers real mode once `CPU_F_PM` is cleared) |
| `FSX_WAIT` | `fsx_wait`, `.text` | `vp_main` (`:15530`), `vp_mwait` (`:15680`) - always `FSXW_FRAME` | TWIN: `FSXW_FRAME` yields (`kernel/fsx.inc:867`), and there is no scheduler; `hlt` on `[sch_subs]` as under a sample ISR |
| `MOUSE` | `osapi_mouse`, `.text` | `vp_main` (`:15232`), `vp_poll` (`:15719`) | TWIN: `kbm_poll` reaches `.cold` (`menu_kbnav`) and `drv_svc_call` (a beep) |
| `SEG` | `osapi_seg`, `.cold` (`kernel/kernel.asm:5906`) | `xg_gap` (`os88xgap.inc:69`), `vp_at` and kin; HDD.DRV, SOUND.DRV, EMS.DRV | TWIN: its own real-mode arm, `SEG_LIN` / `SEG_SUB` / `SEG_WIN` as sums, `SEG_AT` / `SEG_AIM` refused |
| `FILE_READ_SEQ` | `dwf_dskw_read_seq`, `.cold` | `vp_fill` (`:18158`), `vp_bstep` (`:8638`), `vp_eread`, `vp_tfill`, `vp_xbread`, `vp_lyget`, `vp_xstep`, `os88_rseq` | TWIN: the extent reader |
| `DRV_CALL` | `drv_pkg_call_x`, `.text`, into `.cold` | `vp_emap`/`vp_ems`/`vp_eread` (DRVC_EMS: map, free, unframe), `vp_harm`/`vp_hdis` (DRVC_DISK: `HDV_ARM`, `HDV_DISARM`), `os88spk_lptq` (DRVC_SOUND: `SNDV_DACINFO`) | TWIN: class to the low copy |
| `SND_STREAM` | `osapi_snd_stream`, `.text`, into `.cold` and `.mega` | `vp_skeep` (`:17396`, every pass), `vp_sstrm` (the open, inside the bracket) | TWIN |
| `FSX_SPK` | `osapi_fsx_spk`, `.text` (`kernel/snd.inc:1095`) | `os88spk_go` (`os88spk.inc:301`), `os88spk_stop` | TWIN: writes IVT 20h (section 3.1, step 8) |
| `XMEM_COPY` | `xm_copy`, `.text`, into `.mega` (`xm_dsp`, `kernel/xmem.inc:418`) | `vp_xcopy`, `vp_xupm` (the bank's copy up), `vp_tseam`, `vp_lyxcp`, `xg_time` | TWIN |
| `XMEM_ALLOC`, `XMEM_FREE`, `XMEM_CAPS` | `xm_*`, `.text`, into `.mega` | `vp_lysplit`'s split at the first fill (`vp_balloc`, `vp_lyxopen`, `vp_lyxfree`, `vp_xcaps`), `vp_xfree` on a bank's failure | TWIN without compaction, or the split moves out |
| `FILE_READ_AT` | `dwf_dskw_read_at`, `.cold` | `os88_rseq`'s fallback on `FERR_NAME` (`os88rseq.inc:66`) | REFUSE: never reached on kern_mega |
| `MEM_CLAIM`, `MEM_FREE` | `.cold` | `vp_skdue` (a seek's key-entry scratch, `:16152`, `:16178`), `vp_xstep` (`:6492`), `vp_sndoff`, `xg_gap` | CANNOT: the heap makes LDT selectors; the sites leave the bracket (W5) |
| `MEM_CLAIM_DMA_HI` | `.cold` | `vp_sndprep` (an unmute's ring, `:17126`), `xg_gap` (`os88xgap.inc:60`) | CANNOT; W5 |
| `MEM_CLAIM_HI`, `MEM_MOVABLE` | `.cold` | `vp_sesspic` at the exit (the poster, `vp_mclhi`, `vp_pmov`) | CANNOT; W5 makes the poster after `OSAPI_FSX_RUN` returns |
| `DECOMP` | `lzf_decomp`, `.cold` | `vp_thpic` (`:2727`), on the stop path | CANNOT (it could be a twin; W5 moves the stop instead) |
| `GFX_BLIT1`, `GFX_BLIT4`, `GFX_BLITP`, `GFX_FILL`, `GFX_FILL_GRAY`, `GFX_FRAME`, `FONT_RUN`, `WM_CLIP_SET` | `.text`, and `.cold` walks for the blits; `wm_clip_set` frees through `.cold` | `vp_sesspic` -> `vp_stopfor` -> `vp_sstop` -> `vp_repaint` at the exit (`video.asm:3486`) | CANNOT; W5 defers the stop past the bracket |

**12 run as they are, 11 are twins (and the tick, which is no cell), and 15
cannot be offered.** Driver verbs inside the bracket: `DRVC_EMS`'s
`EMSV_MAP`, `EMSV_FREE`, `EMSV_UNFRAME`; `DRVC_DISK`'s `HDV_ARM` and
`HDV_DISARM`; `DRVC_SOUND`'s `SNDV_DACINFO`; SOUND.DRV's stream verbs and
its `DSV_TICK` from the tick. Cells the DRIVERS call on those paths: HDD.DRV
`OSAPI_SEG` (`drivers/hdd/hdd.asm:588`) and `OSAPI_CPU_INFO`
(`hdxms.inc:74`); SOUND.DRV `OSAPI_CPU_INFO` (`sb.inc:1711`), `OSAPI_SEG`
(`:3158`, `:3208`) and `OSAPI_GET_TICKS`; EMS.DRV `OSAPI_SEG`'s `SEG_WIN`
(`ems.asm:505`) - all on the list above. **`int` instructions** in the
shipped bracket: `int 10h` AH=0Bh in `vp_cgaset` (`video.asm:4146-4212`)
and `int 16h` in `vp_bpre` (`:8907`, `:8910`) and `vp_poll` (`:15760`,
`:15763`); both go straight to the BIOS in real mode (`int 16h` is native
under protected mode, `mega_i16`, `kernel/mega.inc:1587`; `int 10h` is a
reflection there today). The kernel's own: `int 10h` in `vid_setmode`,
`int 13h` in the extent reader and HDD.DRV's rung 0.

### 7.3 The kern_mega bodies, audited for real mode

**`.cold` and `.mega` are above 1MB, verified**: `mega_coldup`
(`kernel/mega.inc:6906`) copies both there from `mega_enter` when the
machine has the extended memory (`[mega_codehi]`) and the kernel gives
their rungs to the heap (`kernel/kernel.asm:8790-8792`); `COLD_SEG` is then
a selector whose descriptor `mega_coldup` re-bases. So no `.cold` or
`.mega` body - nor any cell shape that reaches one, `api_far`, `RCXCELL`,
`RNCELL`, `RCSLOT`, `FCELL` - can run in the real-mode bracket. In
`build/megak/kernel.map`: `.text` 47,510 bytes at `KERNEL_SEG`, `.cold`
44,849 and `.mega` 18,203.

**`.text` holds selectors in DATA words**, and each one the bracket's cells
read must be a segment again at the drop:
* `[k_dsel]` (`kernel/kernel.asm:4082`) - `mega_svc_lo.rmc` already does it;
* `[k_seg0]` (`:4083`) - NOT done by the one-call drop today; `vid_setmode`,
  `vid_cga_equip`, `osapi_mouse`, `kbm_shf` and `spk_off` load ES from it;
* the video and font layer's segments `mega_vfixall` (`kernel/mega.inc:11766`)
  makes selectors at the switch: `k_vsa`, `k_vsb`, `k_vsc`, `k_vsf`,
  `vid_tab`'s column, `vid_seg`, `vid_rseg`, `vid_tseg`, each display
  context's three, `fsx_mtab`'s rows (what `fsx_mode` answers as `FSI_SEG`,
  `kernel/fsx.inc:657-660`) and the glyph table's - `mega_vrm`
  (`kernel/mega.inc:5089`) does two of them for one reflection today;
* `[cpu_feat]`'s `CPU_F_PM` (`kernel/cpudet.inc:70`), which the inline
  `OSAPI_CPU_INFO` cell returns in AH.

**Bimodal bodies are not enough on their own**: several `.text` bodies test
`[mega_on]` or `[ss:mega_pm]` and have a real-mode arm (`osapi_seg`,
`xm_copy`'s `.rmes`, `osapi_fsx_spk`'s `.rm`), but a real-mode arm on
kern_mega is not kern_big's - `xm_dsp` has ONE arm on kern_mega, the
protected-mode call (`kernel/xmem.inc:418-426`), and `osapi_fsx_spk`'s
real-mode arm leaves the vector to a caller that would write IVT 08h. So
the drop leaves `[mega_on]` and `[mega_pm]` as they are, and the table
decides which bodies run: the twelve AS-IS cells above were walked to the
end of their reach in the kern_mega listing (stopping only at `task_yield`
and `sch_isr`) and reach no `.cold`, no `.mega`, no selector constant and
no descriptor instruction once the four kinds of word above are segments.

**The table**: `KERNEL_SEG:0x0010` to `0x04E9`, 1,241 bytes, `.text`
(`kernel/kernel.asm:2281`, `:4027`, asserted at `:4067` and `:4070`). The
first draft's `0x0200` and ~0x2D0 bytes were wrong. Every cell on the
whitelist is five bytes or longer; the six 3-byte JCELLs (file rename,
the file dialog, the two `_SYS` writers, the two finds) are all refusals.

### 7.4 The player's claims, its region and its XMS token

**During a bracket the instance holds**: the claims of `vp_smtab`
(`video.asm:12383-12391`: the ring's `[vp_rclm]`, the keeper, the page-flip
copy, the shadow, the poster, a key's shadow, the palette events, the VGA8
palettes, the text's second save, the block decoder's part, the shaper's
part on the arm, the VPDIAG profile) and `[vp_lyseg]`; a resident play's
`[vp_rblk]` and `[vp_rablk]` (`vp_rmtab`, `vp_ramtab`, `:10311-10312`) with
the words derived from them; the card's ring `[vp_aseg]` (`MC_DMA`, never
movable); transient read claims (`[vp_rdseg]`, a seek's); and XMS blocks
named by token (`[vp_xbase]`, the bank or the hold; the layer's bank). All
of them are pinned for the bracket at its door (`vp_rmov` / `vp_smov` with
AX = 0, `video.asm:14615-14619`) and re-declared after (`:14660-14665`),
with the relocation procs `vp_smove` (`:12349`), `vp_rmove` (`:10278`),
`vp_pmov` (`:3186`) and the region's `vp_reloc` (`:21654`).

**Every one is conventional** but the ring above 1MB, which goes (section
3.7), and the REGION: the arm is `OS88_HIGH | OS88_HIGHMEM`
(`video.asm:34`), so `ld_hi` (`kernel/loader.inc:1109-1132`) gives it a
code/data pair above 1MB whenever the pool has room. **The region's low
copy is the kernel's job and the first thing W1 needs**: 63,309 bytes of
image and bss, copied down and copied back UP after (the play writes its
bss, and patches its own code through its data alias, `vp_rhpat`,
`vd_ypatch`), its region proc called each time with DS = the new base.

**The rename is the package's, not the kernel's.** Under MC7 the
protected-mode form of a derived word is not a paragraph: `[vp_ring]` is a
`SEG_SUB` selector where the ring starts inside its claim (THE GAP), a
resident block's positions are paragraph OFFSETS into it
(`video.asm:10649-10653`), and the plane and slot tables (`vp_rsel`,
`vp_lysel`, `vp_kpl`, `vp_spl`, `vp_mxseg`, `vp_plc`) are selectors per
plane or slot. `vp_smove` and `vp_rmove` compute a real-mode MOVE's delta
(`video.asm:12349-12366`, `:10278-10306`), which between a selector and a
paragraph means nothing. So at its door, in protected mode, the arm asks
`SEG_REAL` (§50.6.11: "a REAL-MODE segment with the same base, for a
vector or a ROM that runs in real mode; CF=1 there is none") for each claim
base, derives the rest by the shipped `%else` arithmetic, clears `[vp_pm]`,
and puts the protected-mode words back after. The kernel then has nothing
of the package's to rename but the region, and `FSXF_REAL`'s contract for
every package is the same sentence: *inside the bracket every segment you
hold is a real one; ask `SEG_REAL` for each before the call, and a claim
that has none cannot be used there.*

**The XMS token** (task 4's last half): tokens are KB ranges chosen where
the block is (`mx_tokfit`, `kernel/mega.inc:7356`) and translated to where
it is NOW by `mx_tophys` (`:7335`), because the pool's compactor moves
token blocks; the kernel's own note says no package reads a token in place
on kern_mega, LOADALL and unreal mode being gated off under `CPU_F_PM`
(`:7300-7312`). In a real-mode bracket the shipped readers DO read in place
(`apps/os88la.inc`, 366 bytes; `apps/os88ub.inc`, 83), so they need the
block's current linear address, and the arm already has the means:
`SEG_LIN` of `[vp_xrun]`, the run selector `vp_xrunmk` takes for the block
(`video.asm:5958-5982`). **What pins the pool during the bracket: nothing
needs to** - `mx_tab` (`kernel/mega.inc:12380`, `.lowbss`) is only changed
by protected-mode code, and none runs inside a real-mode bracket, so a
translation made at the door holds to the exit by construction. If W3
instead offers `XMEM_ALLOC` inside the bracket it must not compact, and
anything that ever lets protected-mode code run mid-bracket (an up-call,
section 5.2) must pin the instance's token blocks with `MX_FPIN`
(`:6991`), the pin the DOS trip's park already uses.

### 7.5 The drivers the bracket reads through

**How kern_mega loads a driver image**: `drv_hi` (`kernel/driver.inc:4231`)
moves an `OS88_DRV_HIGHOK` image into a pool block owned `MX_KDRV`
(`kernel/mega.inc:7956`) above 1MB and renames every holder of its segment
through `cw_mem_disp` / `mem_region_reloc` (`kernel/driver.inc:4266-4268`);
the compactor may move it again later (14.37). SOUND.DRV
(`drivers/sound/sound.asm:48`), HDD.DRV (`drivers/hdd/hdd.asm:46`), EMS.DRV
(`drivers/ems/ems.asm:33`), RAMDISK.DRV, CDROM.DRV, NET.DRV and ETHER.DRV
all say `OS88_DRV_HIGHOK`. **"Copy it low for the bracket"** is therefore:
a 512-aligned conventional claim, the image and bss copied, the same
rename walk with (pair selector -> paragraph) and back at the exit, the IVT
entry for its line where it has one, and the driver's own stored segments.

**SOUND.DRV** (7,213 bytes with the PicoMEM tier, an 8 KB claim): its DS
is DERIVED from CS (`SBL_DSCS`, `drivers/sound/sb.inc:840`), so a
512-aligned copy gives DS = CS with nothing told. It stores
`[sbl_rseg]` (`:3954`), `[sbl_seg]` (`:3964`), `[sbl_poolseg]` (`:3970`) and
an external ring's control-word pointer `[sbl_extctl]` (segment = the
package's ring claim, `:1676-1680`) - the last is what the block ISR
dereferences during a play. `[sbl_dmaoff]` / `[sbl_page]` are physical and
mode-free, and `[sbl_dpm]` (`:3898`) need not be renamed: counting by the
8237, which it picks under `CPU_F_PM`, is correct in real mode as well. The
player's stream is DIRECT and EXTERNAL (`video.asm:16898`,
`sb.inc:1672-1681`), so no driver task; it is OPENED inside the bracket
(`vp_sopen`, `video.asm:15457`), so in a real-mode bracket it opens with
real-mode answers and needs no rename - except a Live session's stream,
opened on the desktop and carried in by F, which is what `DSV_RMODE` is
for. SOUND.DRV's kernel calls on these paths are `OSAPI_CPU_INFO`,
`OSAPI_SEG`'s `SEG_LIN` and `OSAPI_GET_TICKS`, all served.

**HDD.DRV** (≤ 4 KB): mode-clean on `DSV_BLK`, rung 0 and rung 1 (section
5.2 item 1); its `HDV_ARM` is refused under `CPU_F_PM` and works in real
mode. **EMS.DRV** (1 KB): port writes, and `OSAPI_SEG`'s `SEG_WIN`
(`drivers/ems/ems.asm:505`), which the twin answers with the paragraph.
**RAMDISK.DRV**'s blocks are `OSAPI_XMEM_COPY`, a twin, so it can be served
the same way. **CDROM.DRV, NET.DRV, HTTPFS.DRV** are FS drivers or a
redirection, not block drivers, and HTTPFS rides ETHER.DRV: not served
(section 5.2 item 3).

### 7.6 Getting to real mode and back: what exists, what W1 adds

**What exists, and what the bracket reuses**:
* **The one-call drop** (`mega_svc`, `kernel/mega.inc:2711`, `.mega`, into
  `mega_svc_lo`, `.text`, `:2834`): the 386's CR0 path (`:2834-2853`) and
  the 286's reset path (`.r286`, `:2854-2875`: 40:67, CMOS shutdown code
  0Ah, a null IDT); `mega_r286` (`:3219`) with its mask compare,
  `mega_pic_pm`, `mega_kdrain` and `mega_fdrprobe`, and its landing switch on
  `[mega_drop]` (`:3275`); `.rm` / `.rmc` (`:2876`, `:2880`: `lidt` the
  IVT, SS, DS, `[k_dsel]`); the way back (`:2965-2992`: NMI off, `lgdt`,
  `lidt`, `lmsw`, `lldt`, the data selectors, `[k_dsel]`, NMI on) and
  `mega_svc_back` (`:2994`, `.mega`: `mega_vsw`'s words back, the masks,
  `mega_rfclk`). **Reused**: both ways down, `mega_r286` with a new
  `[mega_drop]` value, the way back's sequence, `mega_vrm`'s shape.
* **The long drop** (the DOS trip, `kernel/megados.inc`): `mega_dosgo`
  (`:1252`), `mega_dropr` (`:1586`: the PIC re-based to 08h, which the
  bracket does NOT do), and the return's `mega_dwake` (`:1664`: `lldt`,
  `mega_pic_pmf`, the 8042, a forced `mega_ivt_sync`, `mega_dprime`).
  **Reused**: the forced `mega_ivt_sync` and `mega_dprime`; not the park,
  the sum, the PIC re-base or the 8042, none of which the bracket changes.
* `mega_a20` (`kernel/mega.inc:8788`) is `.cold`: the module carries the
  A20 proof the way back needs before `lmsw`.

**The entry and exit points W1 adds** (names are proposals):
* **E1** `fsx_run`, at the entry call (`kernel/fsx.inc:383-388`: after
  `wm_clip_clear`, in place of `call wm_pkgcall`): `FSXF_REAL` set ->
  `fsx_rdrop`.
* **E2** `fsx_rdrop`, protected mode (the module's, through its thunk):
  the region low, the drivers low, the video words / `[k_seg0]` /
  `CPU_F_PM`, the table, then the drop with `[mega_drop]` = 2 (or the 386's
  CR0 path).
* **E3** the real-mode landing: `mega_r286`'s branch at `:3275` (286) or the
  CR0 path's far jump (386) to the module's landing; IVT 20h = the module's
  tick; the entry proc far-called at the low region's `PKG_DISP`.
* **X1** the entry proc returns: A20 proved, then `mega_svc_lo`'s way back
  (`:2965-2992`) to a protected-mode tail.
* **X2** the tail (`mega_dwake`'s two calls, then the module's undo of E2 in
  reverse), then `fsx_run`'s `.restore` (`kernel/fsx.inc:390`).
