# kern_mega M6: the DOS box beside protected mode, and hibernate

**Status: W0-W7 BUILT (2026-10-18, branch `protected-mode-7`), with 10.4's
review answered at the second merge and the park cut to the runs in use
(10.6); W2, W6's prime, W5 on a BIOS-only C:, the runs park on a real 1MB AT
and whether the Victory's ROM keeps extended memory over a warm boot (W7's
premise) are untested off the Victory 286.** Section 1 now records what really happened when the box was run
under kern_mega, sections 10.1-10.3 what each wave measured. **W5 departs
from section 8.3**: the resume never leaves protected mode (10.3). Written 2026-10-18 on branch
`protected-mode-5`, cut from `protected-mode` at `62dfe2c0`, as Track 5 of
docs/plans/KERN-MEGA-HANDOFF.md. It is the "own plan first" that
docs/plans/KERN-MEGA-PLAN.md section 10's M6 row asks for. Section 2.7 of that
file is the feasibility verdict this plan turns into steps, and section 13 is
the memory direction it has to fit.

**What is measured and what is not.** Every byte figure for kern_mega's layout
is read off `build/megak/kernel.map` after `make mega` at `62dfe2c0`. Every
field figure (copy rates, reset costs, the PicoMEM's behaviour) comes from
docs/plans/KERN-MEGA-PLAN.md section 10.3 or from
docs/reports/LOADALL-VICTORY286-2026-10-14.md, and is named where it is used.
**Every cost of new code is an ESTIMATE** at this tree's density, and is
labelled so. Nothing here was run: no Bochs, QEMU or MartyPC session was
started for this plan. Where the plan says the tree does something wrong
today, that is a prediction from reading the source, and section 1 says so.

---

## 0. The answer, in ten lines

1. **On kern_mega the DOS box has exactly one route: the whole-machine handoff
   (arm 3, SPEC.md 96.40), with EXTENDED MEMORY standing in for the hard
   disk.** The kernel copies conventional memory above 1MB (the "park"),
   leaves protected mode once, runs `kern_dos` as it does today, and comes
   back by copying the park down and re-entering protected mode. No
   hibernation file, no reboot, and no hard disk needed.
2. **The windowed arms cannot run under 286 protected mode at all**, and
   section 1 predicts that today they fault (a DOS program's `int 21h` lands
   on IRQ1's gate). Wave 0 greys them, with the reason.
3. **Out is one CPU reset on a 286 and a CR0 write on a 386. Back is `lmsw`
   on both** - the 286's way into protected mode needs no reset.
4. **None of the kern_mega kernel has to stay below 1MB during the bracket**,
   because none of it runs: it is parked whole, and the park is a copy, so
   the code that does the drop is still where it was when it runs. What must
   be below 1MB is `kern_dos`'s own low part and a ~450-byte return stub,
   which goes in the text framebuffer as SPEC.md 87.5's does.
5. **The high claims survive because nothing of ours touches them and a
   checksum proves the DOS program did not.** `int 15h AH=88h` reads 0,
   `AH=87h` is filtered to the box's own arena, and a mismatch on the way
   back refuses to resume and says so, rather than running a damaged kernel.
6. **The XMS door becomes `kern_dos`'s own**: one arena block the kernel
   carves out of its pool before the drop, handed over in the launch block,
   with `int 15h AH=87h` as the transport. Today `kern_dos` refuses XMS
   outright (`kerndos/kdback.inc:273`).
7. **The program gets what it gets today (586 KB, SPEC.md 96.40) at first, and
   ~615-620 KB once `kern_dos` moves into the HMA** - estimated, wave 4.
8. **Hibernate under kern_mega is two sections**: the conventional image as
   today, plus a second section holding the state above 1MB that a fresh boot
   cannot rebuild (the XMEM pool's blocks, and later the LDT claims). The
   fresh kernel restores that section itself, in protected mode, before the
   stub runs; the stub gains a short protected-mode tail.
9. **Option-ROM IRQs need nothing during the bracket** - real mode with the
   PIC at 08h is the PicoMEM's home - and two things at its edges: its line
   must be left open for `kern_dos`, and the 8259 re-initialisation each way
   can strand a pending request, so the line is primed once on the way back.
10. **Bochs can test everything here except the 286 reset and the PicoMEM**,
    which are the Victory 286's. The waves are cut so each field run asks one
    question.

---

## 1. What kern_mega does with the box today

Read off the source at `62dfe2c0`; **nothing in this section was run**.

* **Hibernate is greyed in protected mode.** `hb_ok` points the item at
  `Hibernate (PM)` when `CPU_F_PM` is set (`kernel/hiber.inc:508`,
  `kernel/menu.inc:2678`; KERN-MEGA-PLAN 10.3 item 13). *(W5 has since
  built it, and the string is gone: section 10.3.)*
* **The DOS handoff is not greyed.** `osapi_dos_handoff_x`
  (`kernel/hiber.inc:416`) posts on any kernel, and nothing in `apps/dos/` or
  `kerndos/` reads `CPU_F_PM` (a grep for it, `KERN_MEGA` and `mega` over both
  directories finds nothing). `hbm_handover` (`kernel/hiber.inc:2688`) then
  ends in `push es / push HS_CODE / retf` with ES = B800h or B000h. In
  protected mode that loads an identity DATA selector into CS. `mega_gpfix`
  completes a far transfer only to a 512-aligned identity selector below
  A000h (KERN-MEGA-PLAN 10.1 item 14), so this one goes to the fault reporter.
  **Predicted: arm 3 on kern_mega ends in `kern_mega FAULT` at that `retf`.**
* **The windowed arms are worse.** The box far-jumps into the program with
  CS = the program's segment, which is a data selector in protected mode. And
  a DOS program's `int 21h` goes through IDT vector 21h, which is IRQ1's gate
  (`MEGA_PIC_BASE` = 20h, `kernel/mega.inc:91`): the keyboard ISR. The IVT
  writes the box makes at `dos_hook_vectors` (`apps/dos/dos.asm:2254`) do not
  reach the IDT, because `mega_ivt_sync` follows only IVT 08h-0Fh and 70h-77h
  (`kernel/mega.inc:2731`). **Predicted: any DOS program on kern_mega faults
  or vectors into the keyboard ISR on its first instruction or call.**

So wave 0 (section 10) is a refusal, and it should go first: a feature that
greys itself with a reason is a normal path (SPEC.md 47), and one that faults
is a defect. It is also the cheapest measurement in the plan: break-on-purpose
it under Bochs and the fault line confirms or refutes this section.

### 1.1 What actually happened (W0, Bochs, 2026-10-18)

Run before any change, with kern_big's `DOS.O88` on `mega.img` and
`KDHELLO.COM` (`tests/dostrap/kdhello.asm`, `-DKDHOLD`) on a Bochs C:.
**Both predictions were right about the outcome and wrong about the
instruction.**

* **The windowed arm faults inside the BOX, before the program's first
  instruction**: `kern_mega FAULT 0D err 1A70 at 2464:0106`. 2464h is the LDT
  code alias of segment 9180h, which is `DOS.O88`'s own image (its `'O8'`
  header is at 9180:0000), and offset 0106h is `mov ax, [4149h] / add ax,
  10h / mov es, ax`: the box computes the program's segment as an arena base
  plus 10h, and 1A72h is not on the 8-paragraph grid, so it is no selector
  (#GP with the selector as the error code). IDT 21h IS IRQ1's gate
  (`0060:3307`, `kbm_isr`), as predicted - the program never got as far as
  calling it.
* **Arm 3 faults earlier than `hbm_handover`'s `retf`**: Bochs's C: is a fixed
  disk, so `hbm_dosrun` writes `HIBERNAT.IMG` first (SPEC.md 96.41), and that
  write faults in HDD.DRV's PIO loop (`mov ax, es:[di] / out dx, ax` at
  `27B4:099A`, `ES` = 0008h): the image is read from linear 0 through
  `MEGA_SEL_LOW0`, whose limit is 4FFh on purpose (KERN-MEGA-PLAN 10.1 item
  11) - section 8.2 step 2's hazard, met in the arm 3 path before it was met
  in hibernate's.
* **And a third finding the plan did not have: Bochs will not EXECUTE out of
  video memory at all.** The first W1 build reached `mega_dropr` and jumped
  into the loader stub at B800:0000, and Bochs stopped with `>>PANIC<<
  prefetch: getHostMemAddr vetoed direct read, pAddr=0x0000000b8000`. So no
  stub that runs in the text framebuffer - this plan's park stub, the DOS
  loader stub, the hibernate resume stub - can be tested under Bochs. W1's
  answer is in section 2.5: neither half of the park executes there.

---

## 2. The shape: arm 3, with extended memory as the disk

### 2.1 Why not the windowed box

The windowed box (arms 1 and 2, SPEC.md 96.36) runs the program inside the
kernel's session: `int 21h` enters `dos_int21` in `DOS.O88`, which calls the
kernel's file slots through `dos_be_*` (SPEC.md 96.4.1), and the kernel's
tick, mouse and keyboard ISRs keep running under the program.

Under kern_mega those kernel services are protected-mode code and the program
is real-mode code. Keeping the windowed box means switching mode on every
`int 21h` and on every kernel IRQ that arrives while the program runs. Going
up costs little (`lmsw`). Coming back down costs a CPU reset on a 286, **~1.2
ms a round trip on the Victory** (`RFL 002B/s 04CA`, KERN-MEGA-PLAN 10.3 item 25).
The tick alone (18.2 Hz) plus a mouse at packet rate would be several percent
of the CPU before the program made a single call, and every file call would
add a reset. It would also need a second, bimodal entry for every ISR the
kernel owns, which KERN-MEGA-PLAN 2.2 already refuses ("a lifetime
discipline").

**Refused for M6.** The windowed DOS box on a protected-mode kernel is the
386 tier's job: a V86 task (KERN-MEGA-PLAN 8, the "later" row of its section
10). On kern_mega the radio's first two arms grey with their reason.

### 2.2 Not literally "the fsx bracket"

KERN-MEGA-PLAN 2.7 says "leave protected mode for the length of the fsx
bracket". The drop cannot happen inside `OSAPI_FSX_RUN`: that runs inside a
package callback with the gfx lock held (SPEC.md 96.2), and the drop detaches
drivers and takes the screen. Arm 3 already solved this. The package POSTS
(`OSAPI_DRV_SUSPEND` AL = 2, SPEC.md 96.40.3) and returns, and `ui_task`'s
step 0 spends the post in `hbm_dosrun` with nothing held. M6 keeps that shape
exactly. Only what `hbm_dosrun` does with the conventional image changes.

### 2.3 The sequence

Today's arm 3 on kern_big writes `HIBERNAT.IMG` first when there is a fixed
disk (SPEC.md 96.41), hands over, and comes back through SPEC.md 96.49's live
resume (or `int 19h`). On kern_mega in protected mode:

**Out** (all in `hbm_dosrun`, HIBER.DRV, protected mode, until step 6):

1. The record, the folder, `DOS.O88` and its part's extents: SPEC.md 96.40.1
   steps 1-3, unchanged.
2. Detach the hardware drivers (`drv_unload` per row, `[hb_drvmask]`), take
   the gfx lock, raise `[sch_lock]`: SPEC.md 87.4 steps 1-2, unchanged.
3. **Claim the park**: one block of `[mem_top]` x 16 bytes from the XMEM pool
   (`mx_alloc`, owner the kernel). On a 640 KB machine that is 655,360
   bytes. If it cannot be had, the arm was greyed already (section 5.1); a
   race refuses with a toast.
4. **Bank `SS:SP`** (`[hb_sp]`, SPEC.md 87.4 step 4's shape), then **copy
   linear 0 .. `[mem_top]` x 16 into the park** with `mega_lincopy`
   (`kernel/mega.inc:4000`) in 64 KB chunks, and **sum** the park and the
   kernel's protected high set (section 5.4). The sum goes into the launch
   block, not into kernel memory, because kernel memory is what it covers.
5. The launch block (SPEC.md 96.40.1, `kerndos/kdlaunch.inc`) gains the
   park's base and length, the sum, the XMS arena (section 6), the GDTR and
   IDTR values, and the wake address. Stage the existing stub, the part's
   extents and the block in the text framebuffer: SPEC.md 96.40.1 steps 4-5.
6. **Leave protected mode** (section 3). On a 286 this is the reset through
   CMOS shutdown code 0Ah into `mega_r286`, which a new flag sends to a
   real-mode landing (`mega_dropr`, `.text`) instead of to `mega_svc.rm`. On
   a 386 it is CR0, then a far jump to the same landing.
7. `mega_dropr`, in real mode: mask everything, re-initialise the 8259 to base
   08h, set the masks `kern_dos` is handed (section 9.2), put PIT channel 0
   back in the ROM's mode (SPEC.md 96.40.1 step 6), NMI on, then jump into the
   staged stub, which reads, expands and enters `kern_dos` exactly as it does
   today.

**During**: `kern_dos` and the program. Nothing of kern_mega runs.

**Back**:

8. `kd_leave` (`kerndos/kdentry.inc:638`), when the launch block names a
   park, fetches the **park stub** from above 1MB into its staging segment
   with one `int 15h AH=87h`, fills its cells (exit code, arena KB, the tick
   at hand-over: SPEC.md 96.49.5's `HS_TICK0`), and jumps to it. The stub
   comes from the PARKED KERNEL's own build (section 4.2), so a `kern_dos`
   and a kernel from different commits cannot disagree about its layout.
9. The park stub, in the framebuffer: mask the 8259, open A20 and prove it
   (`mega_a20`'s sequence), load a bootstrap GDT of its own, `lmsw`, verify
   the sum. On a mismatch it writes one sentence into the visible text page,
   waits for a key on port 60h, and resets through the 8042. Otherwise it
   copies the park back over 0..3FFh and 500h..`[mem_top]` x 16 (never the
   BDA: SPEC.md 87.5's rule), loads the kernel's real GDTR and IDTR, and far
   jumps to `KERNEL_SEG:mega_dwake`.
10. `mega_dwake` (`.text`, protected mode): `lldt`, the data selectors,
    `mega_pic_pm` (back to 20h) and the parked masks, NMI on, a forced
    `mega_ivt_sync`, the 8042 command byte, the option-ROM prime (section
    9.3), then a far jump into `hbm_wake` through the module's LDT code alias.
11. `hbm_wake` (`kernel/hiber.inc:2777`) runs as it does for the live resume:
    the PIT back to mode 2 and `[sch_pit_last]` re-seeded, the speaker off,
    the clock caught up from 40:6C (SPEC.md 96.49.5), the caches discarded,
    the drivers reloaded from `[hb_drvmask]`, the exit code to the box, the
    locks down. One addition: it frees the park and the arena blocks.

### 2.4 What it buys, and what it gives up

**Buys**, against kern_big's arm 3:

* **No hard disk needed.** SPEC.md 96.42's destructive arm (a machine with no
  fixed disk reboots and loses the session) does not arise on a kern_mega
  machine that can park.
* **Time.** The park is 640 KB at the Victory's 0.257 ms a KB, measured for a
  copy above 1MB (docs/reports/LOADALL-VICTORY286-2026-10-14.md): **~165 ms
  each way**, against ~2 s each way for the hibernate image on iron
  (docs/plans/KERN-DOS-PLAN.md §2.2). The sum adds an estimated ~0.4 s each
  way at 16 MHz (section 5.4). Both are estimates for this code, which is
  not written.
* **Nothing is lost.** Every window and package comes back, as with
  hibernate-first.

**Gives up: durability.** A hibernation image survives a reset. A park lives in
RAM, so a DOS program that hangs the machine, or a user who presses Ctrl-Alt-
Del inside it, loses the session. Today's kern_big arm 3 on a machine with a
hard disk would bring it back on the next boot. Two answers are possible: also
write the image when a BIOS fixed disk exists (the ~2 s comes back), or
recover a park after a WARM reset (wave 7, which depends on the BIOS not
clearing extended memory on a 1234h boot, unmeasured). That is open question 2.

---

### 2.5 As built (W1, W2): five departures from 2.3

`kernel/megados.inc` is the kernel's half, `kernel/megapark.inc` the park stub
(assembled into HIBER.DRV's image), `kerndos/kdpark.inc` kern_dos's way back.
Where it differs from 2.3, and why:

1. **The snapshot is taken where kern_big writes the image, not at the
   handover.** 2.3 step 4 parks at the drop, after `hbm_down` and the
   unhooks; but then the parked session is one with its drivers shut, its
   mouse unhooked and its IVT changed, and `hbm_wake` - which a kern_big image
   enters as of `hbm_wrimg` - would wake the wrong machine. So
   `mega_dosparkf` runs at `hbm_wrimg`'s place with `hbm_wrimg`'s own first
   half before it (the hardware drivers swept into `[hb_drvmask]`, the lock,
   `[sch_lock]`), and returns TWICE: AL = 0 on to the drop, AL = 1 when the
   session comes back, from where `hbm_dosrun` frees the claims and jumps to
   `hbm_wake`, exactly as the image's wake address would have. The claim
   (`mega_dosclaimf`) comes before all of it, so "no room" refuses with
   nothing torn down (`No room above 1MB`).
2. **The stage runs below the top of the park, not in the framebuffer.**
   `mega_dropr` copies the 16KB stage from B800/B000 to `P - 16KB` and enters
   the loader stub there: conventional memory is all parked by then, the stub
   is position-independent, and the 16KB under P is clear of both places it
   writes (kern_dos at 0600h, the packed stream at `KDS_TMPSEG`) for any park
   of `MPK_MIN_KB` = 128 or more. Bochs cannot fetch from video memory (1.1),
   and the park has no reason to want to. `DOSRMARK=1` keeps the old place so
   the loader stub's CS-relative trace stays on the glass.
3. **The park stub runs IN PLACE, above 1MB, and kern_dos enters it.** 2.3
   steps 8-9 fetch the stub into the framebuffer and let it open A20 and
   switch. Instead `kd_unpark` stages what `hbm_wake` reads (HS_CLK,
   HS_TICK0, HS_DOSCODE, HS_DOSAKB - `kd_resume`'s four), sets a text mode if
   the program left a graphics one, opens and proves A20 (`mega_a20`'s
   sequence, 388 bytes of kern_dos with the rest), `lgdt`s the six-entry GDT
   the kernel left in the park's header and far-jumps to `08h:0`. Nothing of
   the way back executes in memory the restore overwrites, and kern_dos needs
   to know only two numbers (`KDP_GDT`, the selector).
4. **The header is not summed.** It holds the stub, its GDT and cells and the
   region list, written partly after the sum; a header a program damaged
   fails as a stub that cannot run (a reset) or a list that sums the wrong
   memory (a mismatch). What IS summed is the parked image, the part of
   conventional memory left in place by a partial park, the GDT less entries
   0-7 and 1FFEh-1FFFh (they move), the LDT, the IDT less the gates
   `mega_ivt_sync` rebuilds (22h-27h, 70h-77h - the detach rewrites them
   after the snapshot, and `mega_dwake` rebuilds them from the restored
   IVT), `.cold`, and every pool block but the park and the arena.
5. **The sum is one word per 64-byte line, its place in the line stepping 22
   bytes (mod 64) each line** - every even offset sampled within 32 lines,
   Fletcher's two sums mod 65535 (`MPK_SUMLINES`, one macro for both takers).
   Sparse and distributed, as 12.1 item 3 asks; it misses a write that lands
   on no sampled word, and the break-on-purpose runs (10.1) show both.

## 3. Leaving and re-entering real mode

### 3.1 On a 286

**Out is a reset**, the mechanism every reflection already uses
(`mega_svc.r286`, `kernel/mega.inc:1878`): bank `SS:SP` in `[mega_rss]` /
`[mega_rsp]`, point 40:67 at `KERNEL_SEG:mega_r286`, CMOS 0Fh = 0Ah with NMI
off, `lidt` an empty IDT and `int 3`. The CPU shuts down and the AT's logic
resets it. The BIOS jumps through 40:67 with RAM, A20 and every device as they
were, and the CPU's registers, GDTR, IDTR and LDTR lost.

`mega_r286` (`kernel/mega.inc:2000`) already does the things a reset owes: it
restores the stack, counts the reset, compares 40:3E/3F (10.3 item 15), probes
the 765 for a controller reset (`mega_fdrprobe`), clears the shutdown code,
and re-initialises the 8259 only if the masks show the BIOS did (10.1 item 9 and 10.3 item
24). The drop adds one byte, `[mega_drop]`, and one branch at its foot:
`jmp mega_dropr` instead of `jmp mega_svc.rm`. Every probe stays, so a field
run reads the same `RST` and `FDC RESET` lines across a drop as across a
reflection.

**Back is `lmsw`**, which can SET PE on a 286 (KERN-MEGA-PLAN 2.1). No reset,
no BIOS. This is why the return can run from a stub with nothing underneath
it.

### 3.2 On a 386

Out clears CR0.PE and far-jumps to `mega_dropr`, as `mega_svc`'s 386 arm does.
Back is the same `lmsw` (it sets PE on a 386 too). Bochs runs only this path
(docs/plans/KERN-MEGA-HANDOFF.md Part A.3, limit 2).

`mega_dropr` banks the low four bits of the MSW before the drop, and the stub's
`lmsw` restores them, so MP, EM and TS come back as they were whatever the
program did with an FPU. A DOS program on a 386 may enter protected mode
itself (a DPMI or VCPI extender); it has left it by the time it exits through
`int 21h AH=4Ch`, which is real mode. Paging cannot be on in real mode.

### 3.3 The ledger

What has to be saved and put back, and where it lives across the bracket.
SPEC.md 96.5 is the windowed box's ledger and this is its counterpart. Every
row marked "built" is existing code reused.

| item | out | during | back |
|---|---|---|---|
| GDT, LDT, IDT | stay where they are, 0x110000-0x124800 (`kernel/mega.inc:65-67`) | nobody's; covered by the sum | `lgdt`/`lidt` with the parked values (carried as stub cells), `lldt MEGA_SEL_LDT` in `mega_dwake` |
| GDTR, IDTR, LDTR, MSW | lost (286 reset) or left (386) | the program's | stub cells; MSW low bits banked |
| the GDT's moving entries (WIN, ODD, the copy's XA/XB) | not saved: each is rewritten per use | - | the sum is taken after the last `mega_lincopy`, or skips those four entries |
| PIC base | 20h -> **08h**, one ICW sequence with every line masked, in `mega_dropr` | 08h | `mega_pic_pm` (`kernel/mega.inc:715`), 08h -> 20h |
| PIC masks | banked in the park (they are kernel `.text` bytes); `kern_dos` gets section 9.2's | the program's | `mega_dwake` writes the banked ones back |
| 8259 edge latches | reset by ICW1 | - | reset by ICW1 again: section 9.3 re-arms the two lines known to stall (the serial mouse, 10.1 item 9; the PicoMEM) |
| A20 | open | the program may shut it | **the stub opens it and proves it before it touches anything above 1MB** (`mega_a20`'s sequence, 10.1 item 10). If it will not open the session cannot come back, and the stub says so |
| NMI mask (70h bit 7) | off across each switch | on (kern_dos) | off across the copy, on in `mega_dwake` |
| IVT | parked whole: 08h/09h hold the BIOS's halves, 20h-27h the IRQ copies (`mega_enter`, `kernel/mega.inc:319-360`) | rewritten by `kern_dos` and the program | **restored by the copy**, then `mega_ivt_sync` forced, so IDT 22h-27h and 70h-77h follow it |
| BDA | not parked | the ROM's and the program's | **not restored** (SPEC.md 87.5, 96.5). Named fixes: keyboard buffer drained and 40:17/18/96/97 zeroed (SPEC.md 96.5's rule), LEDs from KB_FLAG (`mega_leds`), 40:F0's exit code read (SPEC.md 96.41, built) |
| 40:67 and CMOS 0Fh | ours for the reset | the program's (a 286 extender and `int 15h AH=87h` both use them) | rewritten before every later reset, so nothing to do |
| the tick, 40:6C | `mega_t08` counts it natively in protected mode | the BIOS counts it | the same counter on both sides, so SPEC.md 96.49.5's catch-up applies unchanged (built) |
| `[ticks]`, `[sch_pit_last]` | parked | - | `hbm_wake`'s clock catch-up and re-seed (built) |
| PIT channel 0 | ROM mode 3, divisor 65536 (SPEC.md 96.40.1 step 6) | the program's | mode 2, divisor 0: `hbm_wake` step 1 (built, for exactly the game that leaves its own rate behind) |
| speaker, port 61h | off | the program's | off: `hbm_wake` (built) |
| keyboard | IVT 09h is the BIOS's (`mega_old09`), plus `kern_dos`'s buffer guard (SPEC.md 96.50) | the BIOS's | IDT 21h was always `kbm_isr` -> `mega_k09`, so only the BDA fixes above, plus the 8042 command byte |
| 8042 command byte | banked (command 20h) in `mega_dropr` | the program may turn off translation or the aux port | written back (command 60h) in `mega_dwake` |
| mouse | `mouse_unhook` as today | `kern_dos`'s INT 33h (SPEC.md 96.45) | the live route's re-arm (SPEC.md 96.45, `tests/mouresume.py`'s subject), plus a UART register read to drop a held line |
| floppy | BIOS state at 40:3E-40 is shared with `mega_fd13` | the BIOS's | `mega_fdrprobe` at the next native transfer finds a reset controller (10.3 item 15, built) |
| 8237 DMA | SOUND.DRV detached | a Sound Blaster program may leave a channel armed | SOUND.DRV's reload programs its channel again. Masking channels 1 and 5 in `mega_dwake` is cheap insurance (an estimate, not a finding) |
| drivers | `drv_unload` per hardware row (SPEC.md 87.4 step 1) | gone | reloaded from `[hb_drvmask]` (built). Their IRQ hooks reach the IDT through `mega_null0` and `mega_ivt_sync` (10.1 item 17, built) |
| tasks, locks | frozen in the park, `[sch_lock]` up, gfx lock held | - | `hbm_wake` step 7 (built) |

### 3.4 What the BIOS may do to the machine on the 286's way out

The Victory's BIOS has run the shutdown-0Ah path thousands of times by now
(every C: call and every PicoMEM IRQ is one). What is known from 10.3:

* **It may re-initialise the 8259** on the way to 40:67. Whether this BIOS
  does is not recorded: item 24 fixed `mega_r286`'s test for it and nobody
  has read the answer off this machine. For the drop it does not matter,
  because `mega_dropr` re-initialises to 08h unconditionally. What does
  matter is that a re-initialisation **clears the masks**, so for the
  instructions between 40:67 and `mega_dropr`'s first `out` every line is
  open. `mega_r286` runs with IF off throughout, and so must the landing,
  until the masks are set.
* **It may touch the floppy**: 40:3E's IRQ6 bit and the motor bits, and
  possibly a controller reset (item 15). `mega_r286` already reports and
  repairs it.
* **It sends no EOI and runs no POST** with code 0Ah. A reset taken while an
  IRQ is in service would leave that line's in-service bit set across the
  re-initialisation. The drop is taken from `ui_task` step 0, not from an ISR,
  so this does not arise.

The way back needs no reset, so the BIOS does nothing there.

---

## 4. What must stay below 1MB

### 4.1 During the bracket: none of the kernel

The parked kernel does not run during the bracket, so **no byte of it has to
be below 1MB while the program runs**. Everything it had in conventional
memory is in the park and is copied back over whatever the program left.
This is the important difference from KERN-MEGA-PLAN 13.2's low region, which
exists for the REFLECTION: code that runs in real mode while the desktop is
live.

What is below 1MB during the bracket is `kern_dos`'s, and it is `kern_dos`'s
layout (`kerndos/kdlayout.inc`):

| | bytes | why it is low |
|---|---:|---|
| IVT and BDA | 1,536 | the CPU's and the ROM's |
| `kern_dos`'s image, at 0x0600 (`KD_IMG_KB` = 33) | 33,792 | it is real-mode code; wave 4 moves it to the HMA |
| the FAT snapshot (`DSK_FAT_SECS` = 9) | 4,608 | read by `int 13h`, which takes a 20-bit address |
| the mount buffers and stack (`KD_LOW_KB` = 3) | 3,072 | the same, and SS |
| the read-ahead it claims from the top | not sized here | `int 13h` again |

That is today's arm 3, unchanged, and the program gets **586 KB** above its
PSP (MEASURED on a 640 KB 5150, SPEC.md 96.40). Wave 4 takes the 33 KB image
out of that column.

### 4.2 The transitions

Three pieces of new code run in real mode or without the kernel under them:

| piece | where it runs | why there | size |
|---|---|---|---|
| `mega_dropr` (out) | kernel `.text` at `0060:` | 40:67 has to name it, and at that moment conventional memory is still the kernel's: the park is a COPY | ~120 bytes, ESTIMATE |
| the existing `kern_dos` loader stub | the text framebuffer | unchanged (SPEC.md 96.40.1) | built |
| the park stub (back) | the text framebuffer | the copy writes over all of conventional memory, `kern_dos` included, so the code doing it must be outside it. The framebuffer is the one RAM no rung owns (SPEC.md 87.5), and by the time `kd_leave` runs the program has exited | ~450 bytes, ESTIMATE: A20 ~115 (`mega_a20`, `mega_a20s`, `mega_a20t`, `mega_kbw` span 0x4826-0x48D7 less the LED routines), a 3-entry bootstrap GDT 24, the sum and the copy ~150, the tail ~40, the failure sentence ~80 |

**The park stub lives above 1MB while the program runs**, not in `kern_dos`'s
image. The kernel copies it from HIBER.DRV's image into the park's header
before the drop, and `kd_leave` fetches it with one `int 15h AH=87h`. That keeps
it out of the 33 KB the program pays for, and makes it the parked kernel's own
build.

**When section 4 item 1 of KERN-MEGA-PLAN moves the kernel above 1MB,
`mega_dropr` joins the low region** beside `mega_r286`, because 40:67 must name
real-mode code. `mega_dwake` does not: it runs in protected mode and is entered
by a far jump through `KERNEL_SEG`, which can have any base.

### 4.3 The reflection's low set, sized from the map

KERN-MEGA-PLAN 13.2 lists the reflection's real-mode half without a size. From
`build/megak/kernel.map` at `62dfe2c0` (virtual offsets in `.text`):

| what | span | bytes |
|---|---|---:|
| `mega_svc.rm` .. `mega_svc.pm` (the real-mode half) | 52EFh-534Dh | 94 |
| `mega_r286` | 53DDh-545Fh | 130 |
| `mega_capon`, `mega_capoff`, `mega_cap2`/`mega_capc` and `mega_capsave` | 46FDh-47E0h | 227 |
| `mega_pic_pm` (called from `mega_r286`) | 480Fh-4826h | 23 |
| `mega_fdrprobe` and the 765 helpers it calls | 6239h-62DAh | 161 |
| **code** | | **635** |
| GDTR/IDTR images, `mega_rss`..`mega_imr2`, `mega_fdpre`/`mega_fdpost` | 4365h-4396h, 50FAh-50FEh | 53 |
| `.bss` bytes they touch (`mega_irqr`, `mega_rstn`, `mega_tellq`, `mega_fdbusy`, `mega_fdrst`) | | ~7 |
| **total** | | **~695** |

Plus the caller's LOW_SEG task stack, the IVT and the BDA. For scale: the
kernel's conventional footprint after the switch is **70,656 bytes**,
0600h-11A00h on VGA: the 58,368-byte image rung (`KIMG_PARA` E40h), FAT 4,608,
LOW 7,168 and the 512-byte VGA buffer. `mega_coldup` gave `.cold`'s 37,888-byte
rung to the heap (`COLD_RAM` 1180h). `mega.inc` is 9,947 bytes of `.text`
(4332h-6A0Dh). So the real-mode set is about 1% of the conventional kernel,
and moving the rest high leaves ~0.7 KB plus stacks low. That is what section
13.3 step 1 of KERN-MEGA-PLAN has to reserve, and it is the M3 tracks'
business more than this plan's.

`tools/kernsize.py --build build/megak` was tried and reports kern_big's
sections (`text 42,706`, against the map's 53,033), so it does not read a
kern_mega build. The map is the instrument here.

---

## 5. How the high claims survive the bracket

### 5.1 The extended-memory map during the bracket

| linear | what | whose | protected by |
|---|---|---|---|
| 100000h-10FFEFh | the HMA | free on kern_mega (`kernel/kernel.asm:698`; `MEGA_GDT_LIN` sits above it on purpose). `kern_dos`'s from wave 4 | - |
| 110000h-11FFFFh | the GDT, 8,192 entries | kernel | the sum |
| 120000h-123FFFh | the LDT, 2,048 entries | kernel (track 1's allocator writes 1280-2047) | the sum |
| 124000h-1247FFh | the IDT | kernel | the sum (or rebuilt) |
| 125000h-12E31Eh | `.cold`, 37,663 bytes (`COLD_SIZE`) | kernel | the sum |
| 130000h- | the XMEM pool (`MEGA_XPOOL`) | `mx_tab` | per block: below |
| (pool) | **the park**, `[mem_top]` x 16 | kernel, for the bracket | the sum |
| (pool) | blocks packages hold (gap buffers, Pixel's undo, a RAM disk) | packages | the sum: they come back with the session |
| (pool) | track 1's high claims: caches, modules, high packages | kernel | **caches are dropped before the park** (they are rebuilt anyway, SPEC.md 87.6 step 3); the rest the sum |
| (pool) | **the XMS arena** (section 6) | the DOS program, for the bracket | nothing: it is theirs |

**The minimum machine.** `mega_enter` needs 192 KB above 1MB
(`kernel/mega.inc:164`). The whole-image park needs `[mem_top]` x 16 more. On
a 640 KB machine that is 640 KB, so about 832 KB of extended memory, which in
practice means a 2 MB AT. The Victory has 4 MB. A 1 MB AT (384 KB extended)
runs kern_mega and cannot park. Arm 3 greys there with the reason, a fact
rather than a guess (SPEC.md 47): `needs 640K above 1MB`.

**Section 13 is what brings that machine in.** High-first leaves less in
conventional memory, so a park that copies only the runs in use (the claims
in `mem_tab` plus the kernel's low region) shrinks as packages move up. That
is a list of runs instead of one block, the shape SPEC.md 87.5's extents
already have. It is a lever for after wave 1, not part of it.

### 5.2 What the DOS program may and may not touch

| | may | how |
|---|---|---|
| conventional memory above `kern_dos` | yes | it is the arena, as on DOS |
| the XMS arena | yes | through the box's XMS door (section 6), or `int 15h AH=87h` with that end inside it |
| the HMA | no | XMS reports none (SPEC.md 96.15.2), unchanged. From wave 4 it is `kern_dos` |
| everything else above 1MB | **no** | `int 15h AH=88h` answers 0. `AH=87h` with an extended end outside the arena is refused (CF = 1, AH = 03h, the BIOS's own "A20 failed" code, which programs treat as no memory) |

This makes KERN-MEGA-PLAN 2.7's three defences concrete. A program that uses
XMS, or honours `AH=88h`, is safe. That covers everything that ran under
HIMEM, including the 286 DOS extenders, which take their memory from XMS when
an XMS driver is present.

Not covered: a program that reads CMOS 17h/18h or 30h/31h for the memory size
and addresses it itself (a VDISK-style or raw extender), or one that goes to
protected mode, LOADALL or unreal mode on its own. Real mode cannot stop any
of those. The sum catches them.

**Placement.** A raw allocator takes memory either top-down from the CMOS size
or bottom-up from 1MB. With the tables fixed at 110000h, the kernel's state is
at the bottom whatever is decided. Putting the park directly above `.cold` and
the arena at the top means a top-down raw allocator eats the arena (the
program's own XMS clients suffer, the session survives) and a bottom-up one
eats the kernel (the sum catches it). Neither can be ruled out from the
software side. This is why the sum is not optional.

### 5.3 What else writes above 1MB during the bracket

Only the BIOS, inside `int 15h AH=87h` on the box's behalf and the program's.
The BIOS builds its own GDT in the caller's buffer and its own empty IDT, and
on a 286 it returns by shutdown code 09h, so it touches neither our tables
nor 40:67 in a way that outlasts the call. Nothing of ours runs: no ISR of the
kernel is reachable from the real-mode IVT, which is `kern_dos`'s and the
program's.

### 5.4 The sum

**What**: the park, the GDT less its four moving entries, the LDT, the IDT,
`.cold`, and every `mx_tab` block the kernel or a package holds (track 1's
claims when they exist), less the arena and the dropped caches. On the Victory
with a 640 KB park and nothing else held, that is ~760 KB.

**How**: a 32-bit two-sum (Fletcher-shaped: a running sum and a sum of sums),
because a plain additive word is blind to a zeroed run that happens to sum to
zero. It is taken in protected mode after the copy, and verified by the stub
before anything is written back. The stub has no ROM to print through in
protected mode, so it writes its sentence straight into the text page it
stands in, below its own code. `hbstub.inc`'s failure path already writes
over the staged cells it no longer needs (SPEC.md 87.5), and this is the
same move.

**Cost, ESTIMATED from Intel's clocks**: a `lodsw` / `add` / `adc` / `add` /
`adc` loop is about 17 clocks a word on a 286. 760 KB is ~389,000 words, so
~6.6 M clocks: **~0.4 s at 16 MHz each way**, before wait states.
KERN-MEGA-PLAN 2.7 estimated 0.3 s per 512 KB, which agrees. A sparse sum (one
word in 16) would be ~25 ms and would miss a small write. Which to ship is
open question 3.

**On a mismatch**: the session cannot be trusted, so it is not resumed. The
sentence names the program (from the launch block, which the stub carries)
and says the machine will restart. Then the 8042 resets it. **It is the
second deliberately destructive path** after SPEC.md 96.42's, and it should
read like one. With a hibernation image also written (open question 2) it
would fall back to that instead.

---

## 6. The XMS door (KERN-MEGA-PLAN 12.2)

12.2's table leaves one row open: "DOS box | the box's XMS door | the cells -
and M6's question". On kern_mega the windowed box does not exist (section
2.1), so the door is only ever `kern_dos`'s, and the cells are gone for the
length of the bracket with the kernel that serves them.

**What it is today.** In the window, the box's `int 2Fh AX=4300h/4310h`
(`apps/dos/dos.asm:14022`) answers over `OSAPI_XMEM_*`: a handle table of
`XMS_NH` = 8, a move needing one conventional end, no HMA (SPEC.md 96.15).
Under `kern_dos` the four back-end doors are `stc`/`ret`
(`kerndos/kdback.inc:273`), because an 8088 has no store and the probe would
cost the 39 KB budget bytes. So `dos_xms_kb` reads 0, and `AH=4300h` answers
"no driver" (SPEC.md 96.15.1).

**What it becomes on kern_mega:**

1. **The arena.** After claiming the park, `hbm_dosrun` claims ONE more
   block, by default all that is left of the pool. It passes its base and KB
   in the launch block (`KDL_XBASE`, `KDL_XKB`, new). `hbm_wake` frees it. The
   program's XMS allocations die with it, which is XMS's own rule for a
   program that exits without freeing.
2. **The allocator.** `kern_dos`'s `dos_k_xcaps`/`_xalloc`/`_xfree` become a
   block table over the arena. `mx_alloc`, `mx_free` and `mx_caps`
   (`kernel/mega.inc:4097-4229`) are already XMEM.DRV's allocator, 1 KB
   granular. They would move into a file of their own (`kernel/mxpool.inc`)
   that both roots `%include`, the way `kernel/hbstub.inc` is shared (SPEC.md
   96.49). Then `kern_dos` and the kernel cannot differ in how a block is cut.
3. **The transport.** `dos_k_xcopy` is `int 15h AH=87h`: every AT BIOS has it.
   On the Victory it costs **412 us a call plus 256.9 us a KB**, measured, and
   **this BIOS leaves A20 open after it**, also measured
   (docs/reports/LOADALL-VICTORY286-2026-10-14.md). A call moves at most 64
   KB, so `AH=0Bh` chunks, as it does today. Because `AH=87h` moves extended
   to extended directly, SPEC.md 96.15's refusal of a move with two extended
   ends can be lifted here.
4. **`int 15h` is hooked by `kern_dos` on this route**: `AH=88h` -> 0 and
   `AH=87h` filtered (section 5.2). Every other function chains to the ROM.
5. **A20 and the HMA** stay as SPEC.md 96.15.2 says: the calls succeed and
   change nothing, and there is no HMA. On a BIOS that closes A20 after
   `AH=87h` (SPEC.md 41.2's known hazard), wave 4's `kern_dos` re-proves A20
   after each call, because it then lives above 1MB.
6. **Later, LOADALL.** In real mode under `kern_dos` the 286's LOADALL is legal
   (it is our machine during the bracket), and it is 139 us a call against
   87h's 412 on the Victory. `apps/os88la.inc` already exists. It is a lever,
   not a wave.

**Cost**: ~500 bytes of `kern_dos` image, ESTIMATED (allocator ~250, `int 15h`
hook and filter ~100, transport and chunking ~100, the `int 2Fh` answer
already there). `KD_IMG_KB` had 133 bytes left when its comment was last
updated (`kerndos/kdlayout.inc`), so this crosses a kilobyte, and **a kilobyte
of `kern_dos` is a kilobyte off the DOS program**
(docs/plans/KERN-DOS-PLAN.md §1). If `kern_dos` is one binary for both kernels,
kern_big's program pays it for a door that cannot open there. That is open
question 4.

**BUILT (`kern_dos`'s side), and the estimate was low by half**: 12.1 item 4
made it a kern_mega-only `DOS.O88`, so kern_big's is byte-identical and pays
nothing, and the measured cost is **1,026 bytes** of `kern_dos` image plus bss
(33,585 -> 34,611: the door 842, its bss 98, two calls 6, and 80 of `CORE_MAX`
hole for the core's 67-byte extended-to-extended move). `KD_IMG_KB` is 34 on
kern_mega, so its DOS program pays one kilobyte. The allocator is a private
copy of `mx_alloc`'s shape and not a shared `kernel/mxpool.inc`: the arena has
one claimant and no owner byte, and `kernel/mega.inc` is another lane's. SPEC.md
96.15.4 is the contract, and KERN-MEGA-PLAN 10.3 item 68 the record.

---

## 7. `kern_dos` in the HMA (the +30 KB)

KERN-MEGA-PLAN 2.7's own proposal. Wave 1 does not need it, and it is worth a
wave of its own.

* **What moves**: the 33 KB image. The HMA holds 65,520 bytes, so it fits with
  ~31 KB to spare. `.cold` already runs from the HMA at `vstart=0x10` on
  kern_big (SPEC.md 2.10.7), which is the precedent for assembling a body
  there.
* **What stays low**: the FAT snapshot, the mount buffers and stack (all
  `int 13h` targets: `dsk_runcap` keeps HMA buffers out of `int 13h` already,
  because a BIOS computes a 20-bit address), the read-ahead, and ~200-300
  bytes of entry trampolines (ESTIMATE). Each vector `kern_dos` hooks (20h-24h,
  2Fh, 33h, 09h's guard, 15h) gets a low stub that proves A20 (~2 us,
  `mega_a20t`'s compare-and-restore) and reopens it if a program shut it, then
  far-jumps to `FFFF:`. That is DOS=HIGH's design.
* **How it gets there**: the kernel reads and expands the part into the HMA in
  protected mode, before the drop, with its own file layer. The real-mode
  loader stub and its `int 13h` reads are then not needed on this route at
  all, so the way out gets faster as well as smaller.
* **What it costs**: a third assembly of `kern_dos` (`KD_HMA`: `KERNEL_SEG` =
  FFFFh, the image at offset 10h, `FAT_SEG`/`LOW_SEG` at 0060h). Probably a
  part of its own in `DOS.O88`, `OP_LAZY`, carried only where kern_mega is
  (mega.img has 2,077 free clusters of 2,847, per `os88disk`'s line from `make
  mega`). And the shared disk layer's segment arithmetic must hold with
  CS = DS = FFFFh, which nobody has checked.
* **What it buys**: the program's floor moves down by ~33 KB less the
  trampolines: **586 -> ~615-620 KB, ESTIMATED.** KERN-MEGA-PLAN 2.7's ~630
  counted the read-ahead out too, and that stays low.
* **Its risk** is the classic DOS=HIGH one. A program that relies on the 1MB
  wrap with A20 open (EXEPACK's "Packed file is corrupt" class) addresses
  `kern_dos` instead. DOS answered with LOADFIX, and so could the box (load
  the program 64 KB up). Section 12 asks whether that is wanted.

---

## 8. Hibernate under kern_mega (KERN-MEGA-PLAN 2.6)

### 8.1 What is above 1MB, and which of it is state

A hibernation image is only useful for what a fresh boot of the SAME build
cannot rebuild. SPEC.md 87.3 makes that requirement, and it is what lets the
restore write over the running kernel.

| above 1MB | rebuilt by a fresh boot? |
|---|---|
| the GDT | yes: `mega_enter` builds the identity map and `mega_coldup` re-bases `.cold`, identically |
| the IDT | yes: built from the IVT, and the restored IVT plus a forced `mega_ivt_sync` gives the image's gates |
| `.cold`'s copy | yes: it is a CODE selector, read-only, so its bytes are `KERNEL.SYS`'s |
| the LDT entries 0-1279 (the fixed alias table) | yes: computed |
| **the LDT entries 1280-2047** (track 1's allocator) | **no** |
| **every `mx_tab` block in use** | **no**: it is package data |
| track 1's caches above 1MB | not wanted: SPEC.md 87.6 step 3 discards every cache on resume anyway |
| track 1's modules above 1MB | their code yes, their `.bss` no (modules write their own image, KERN-MEGA-PLAN 2.4) |

So the image grows **one section**, which SPEC.md 87.7 already names as owed:
*"extended memory in the image (the store's blocks would have to be copied
down and up through `xm_copy`, a second section of the file)"*. With that
section, `Hibernate (XMS)`'s refusal (SPEC.md 87.2's third row) can go on
kern_mega.

### 8.2 The write, on both CPUs

All of it is in protected mode, so the 286 and the 386 do the same thing.

1. SPEC.md 87.4 steps 1-4, unchanged. Plus: drop track 1's high caches.
2. **The conventional image**, as ONE `dskw_write` from linear 0. In
   protected mode linear 0 needs a selector. Selector 0 is NULL, and
   `MEGA_SEL_LOW0` stops at 4FFh on purpose (10.1 item 11). So the writer
   takes a scratch data selector with base 0 and a 64 KB limit
   (`mega_oddsegf` special-cases segment 0). The segment walk of SPEC.md
   18.4.1 then steps to 1000h, an identity selector. Whether `dskw_norm`'s
   kern_mega granule rule (10.1 item 4) walks that correctly needs checking,
   not assuming.
3. **The extended section**, appended: a header (`'HIBX'`, `[mega_extkb]`,
   the run count) and then each run. A run is a block of `mx_tab`, or the LDT's
   upper 6 KB (entries 1280-2047), or a module's `.bss`. Each run is copied
   down through a low bounce claim (`MEM_K_HIB`, like the extent list) and
   written. A reflected `int 13h` takes a real-mode ES:BX, so the buffer has
   to be below 1MB (KERN-MEGA-PLAN 13.2's bounce row). The bounce is in the
   conventional image as a HELD claim, so `hbm_wake` frees it. That is SPEC.md
   87.6.1's trap, and its answer, one claim along.
4. **The pointer**: byte +11, today `db 0` (SPEC.md 87.3), becomes flags. Bit
   0 means a kern_mega image and bit 1 means an extended section follows. Do
   not rely on `MOD_STAMP` alone to keep kern_big from resuming a kern_mega
   image: both builds have one `BUILD_NUM` at a commit, and whether their
   stamps differ was not checked.

The transport rule stands: SPEC.md 87.2 refuses a volume whose driver answers
`DSV_GEOM` with CF = 1 (HDD.DRV's rung 1, native IDE), because the resume stub
speaks only `int 13h`. On the Victory that is the right answer: its PicoMEM
C: is BIOS-only. On Bochs, HDD.DRV takes rung 1, so a Bochs hibernate has to
use the kernel's own boot partition (`os88bochs.py boot --c keep --bootc
--noa`), which is BIOS `int 13h` reflected (10.3 item 11).

### 8.3 The resume, on both CPUs

KERN-MEGA-PLAN 2.6 proposed that the stub read the extended part into
conventional memory and copy it up. **This plan does it the other way round.**
The fresh kernel is in protected mode with a working file layer and
descriptors. The stub has neither. So the fresh kernel restores the extended
section itself, before it stages the stub:

1. `hb_probe` runs in real mode, in `kmain`, before `mega_enter`, as today.
   It only posts `UI_RBQ_ASK`. `hb_ask` runs at `ui_task`'s first pass, after
   `mega_enter`. There it refuses a kern_mega image when `CPU_F_PM` is clear
   (the switch refused this boot, so the image's kernel state cannot be
   entered), and it refuses a kern_big image on kern_mega, and the reverse.
2. `hbm_res`, in protected mode: transport facts and the conventional
   image's extents (SPEC.md 87.5 steps 1-2), unchanged. **Then the extended
   section**, read with the kernel's own file layer through a low bounce and
   `mega_lincopy`'d to each run's linear base. Before it, `drv_shutdown` (moved
   up from step 3, so no fresh driver holds a pool block) and a refusal if
   `mx_tab` is not empty. **The LDT's upper entries go LAST**, because after
   them the fresh kernel's own allocator descriptors are the image's. That is
   a rule for track 1: nothing the resume path uses may live behind LDT
   entries 1280-2047.
3. Stage the stub as today (SPEC.md 87.5 step 4). The cells gain the GDTR and
   IDTR values and the wake address as an LDT alias.
4. **Leave protected mode** to run the stub. On a 286: CMOS 0Ah and 40:67 =
   the stub's own address in the framebuffer, then the reset. **The BIOS jumps
   straight into the stub**, with no landing in between. On a 386: CR0, then a
   far jump to it.
5. **The stub's first instructions set the 8259 masks** (section 3.4: a 286
   BIOS that re-initialised the PIC on the way here has cleared them, and the
   IVT the stub runs under holds kern_mega's kernel ISRs at `0060:` offsets,
   which must never run in real mode). Then the body runs as it does today:
   every sector to its linear address with `int 13h`, the IVT last.
6. **The stub's protected-mode tail**, `%ifdef KERN_MEGA` in
   `kernel/hbstub.inc`: `lgdt` and `lidt` from the cells, `lmsw`, then a far
   jump to `KERNEL_SEG:mega_hbwake`, which is `mega_dwake` (section 2.3 step
   10) entered with a different flag. About 40 bytes and 12 bytes of cells,
   ESTIMATE. Today's stub ends by jumping to `hbm_wake` in real mode. That
   cannot work here: the image's kernel holds selectors everywhere (`[k_dsel]`
   = 10h, task frames, the module slots), so real-mode code would read them
   as segments.
7. `hbm_wake` as today.

The two CPUs differ only in step 4's way down, and the way up is the same
`lmsw`.

`kern_dos`'s live resume (`kd_resume`, `kerndos/kdresume.inc:53`) includes the
same `hbstub.inc` and does not define `KERN_MEGA`, so its stub is unchanged.
It must refuse an image whose pointer says kern_mega. Its fallback (`int 19h`,
then the fresh kern_mega boot's own resume) then does the right thing. That is
~8 bytes of `kern_dos`, ESTIMATE. It only matters if kern_mega's DOS handoff
ever writes an image (open question 2).

### 8.4 Cost

ESTIMATES. HIBER.DRV is built per kernel (`build/megak/hiber.drv`, 5,061
bytes on disk at this commit, against SPEC.md 87.7's 5,499-byte kern_big
image), so none of this reaches kern_big. The extended-section writer and
reader are ~800-1,200 bytes of HIBER.DRV, plus ~60 of kernel `.text` for
`mega_hbwake` beside `mega_dwake`, plus ~40 of `hbstub.inc` under `%ifdef
KERN_MEGA`. HIBER.DRV is an on-demand module, so none of it is resident.

---

## 9. Option-ROM IRQs: the PicoMEM's multiplexer

### 9.1 During the bracket: nothing to do

KERN-MEGA-PLAN 10.3 items 18-25 are about running an option ROM's IRQ handler
while the kernel is in protected mode: reflect it to real mode
(`mega_irqrom`, `kernel/mega.inc:518`), catch the kernel's own lines meanwhile
(`mega_capon`/`mega_capoff`) and replay them through the IDT afterwards
(`mega_replay`). None of that is used during the bracket. The machine is in
real mode with the PIC at 08h, and the IVT's 08h+n entry for the PicoMEM's
line still holds its ROM's handler. `mega_enter` never rewrites IVT 0Ah-0Fh,
and SOUND.DRV restores what it hooked when it is detached. So the multiplexer
runs exactly as it does under DOS. It copies each 128-byte "DMA" block and
raises its Sound Blaster's IRQ as a software `int 8+n` into the DOS program's
own handler (SPEC.md 34.10.1). The PicoMEM's BIOS C: is served natively too:
**on the Victory a DOS program's disk call under `kern_dos` costs no reset at
all**, where the desktop's costs one per call.

Whether any DOS Sound Blaster program has been run under `kern_dos` on the
Victory is not recorded here. It is untested, not known to work.

### 9.2 At the way out: the line must be open

SPEC.md 34.10.1's rule: the multiplexer raises an audio or NE2000 request only
when line n's bit in the 8259 mask is clear. Its OWN line has to be open for
it to run at all. A DOS program unmasks its Sound Blaster's line, which is not
the PicoMEM's own (the card refuses to share it). Nothing in a DOS program
knows to open the PicoMEM's line. On a real DOS machine its BIOS left it open
at POST.

Today's handover masks IRQ 1, 3, 4 and 12 on top of the current IMR
(`hbm_handover`). On kern_mega, `mega_dropr` additionally **opens every line
whose IVT handler is an option ROM's**, using `mega_irqrom`'s own test
(segment C000h-EFFFh, and IRQ 9's redirect to one). That is what POST would
have left. It costs a loop over 14 lines, ~25 bytes, ESTIMATE.

### 9.3 At the way back: ICW1 can strand a request

KERN-MEGA-PLAN 10.1 item 9 found that re-initialising the 8259 resets its edge latches, so a
device that raised its line while it was masked and is still holding it high
never makes another edge. The serial mouse died that way, and the fix was to
stop re-initialising per reflection. **The bracket cannot avoid it**: the
base has to go 20h -> 08h -> 20h, and a base moves only by ICW1. So the two
re-initialisations are taken with every line masked, and afterwards the lines
known to stall are re-armed:

* **The serial mouse**: read the UART's IIR, LSR, RBR and MSR, which drops a
  held INTR. This is what `mouse_init` does at boot.
* **The PicoMEM's line**: the multiplexer EOIs at once, walks its request
  list and acknowledges the card with `out 2A0h, 1` (SPEC.md 34.10.1). A
  request whose edge was lost leaves the card waiting for that
  acknowledgement. So `mega_dwake` **primes** each option-ROM line once by
  reflecting it (`int 22h+n` in protected mode reaches `mega_irv` and
  `mega_svc` as an IRQ). The handler then runs, finds the list empty or not,
  and acknowledges.

**Unverified**: that a multiplexer pass with nothing pending is harmless,
including the acknowledgement. The firmware in `firmware/picomem1/` is a
`.uf2` and a patch, not source this plan could read. If it is not harmless,
the alternative is to prime only when the request list at ROM + 4356h
(`mega_pmpre`'s reading, 10.3 item 23) shows something pending. That is
another ~20 bytes.

The same ICW1 at the way OUT can strand a request that was pending when the
kernel dropped. `mega_dropr` primes in real mode with a plain `int 8+n`, which
is what the hardware IRQ would have done.

### 9.4 After the bracket

The restored IVT names the same ROM handlers, `mega_ivt_sync` rebuilds the
gates from it, and the reflection path of items 18-25 carries on as before.
SOUND.DRV's reload re-runs `pm_init`. Its `int 13h AX=6000h` detect (reflected)
re-hooks the multiplexer if a program left the vector somewhere else (SPEC.md
34.10.1 rule 1), and the restored IVT will not have. So the Tracker case of
item 25 should come back without new code beyond the prime. That is the field
run's to confirm.

---

## 10. Waves

Bochs is a 386. It cannot run the 286 reset (its triple fault resets the
devices too), has no PicoMEM, and gives a C: through HDD.DRV's rung 1 rather
than the BIOS (docs/plans/KERN-MEGA-HANDOFF.md Part A.3). So every wave below
is cut into a Bochs half and, where it has one, a Victory half. kern_big stays
byte-identical in every wave. The kernel's changes are all `%ifdef
KERN_MEGA`, and HIBER.DRV is built per kernel. `DOS.O88` is the exception:
`mega.img` carries the same `build/kdos/DOS.O88` kern_big ships, so a
`kern_dos` change either stays byte-identical on kern_big or is argued right
on both (open question 4).

| wave | content | cost (ESTIMATE) | Bochs | Victory 286 |
|---|---|---|---|---|
| **W0** | refuse the DOS box in protected mode: arms 1-2 greyed `(PM)` in `DOS.O88` on `CPU_F_PM`, arm 3 refused by `osapi_dos_handoff_x` under `%ifdef KERN_MEGA` until W1 | ~10 B kernel `.text`; ~50 B `DOS.O88` (right on both kernels: the bit is never set on kern_big) | before: double-click a `tests/dostrap` probe, expect section 1's FAULT on E9 (the break-on-purpose). After: the refusal sentence, screenshotted | one photograph of the greyed radio |
| **W1** | the park on the 386 path: section 2.3 steps 3-11, `mega_dropr`/`mega_dwake`, the park stub in HIBER.DRV, `kd_leave`'s fetch, the sum | kernel `.text` ~300 B; HIBER.DRV ~700 B; `kern_dos` ~100 B | see below | - |
| **W2** | the 286 drop: `[mega_drop]` and the branch in `mega_r286`; section 3.4's masks-first landing; a `mega_tell` line for the trip | ~30 B | not testable (A.3 limit 2) | see below |
| **W3** | the XMS door (section 6) | `kern_dos` ~500 B; kernel ~40 B for the arena claim | a `tests/dosxmsq.py`-shaped probe: allocate, move out, wipe conventional memory with a third value, move back, compare, free. Negative control: a raw `AH=87h` aimed at the park is refused | the same probe, for 87h's real cost |
| **W4** | `kern_dos` in the HMA (section 7) | a third `kern_dos` assembly, ~250 B of trampolines, a `DOS.O88` part | the arena figure (`kdhand`'s `([es:2] - CS) >> 6`) reads >= 610 against 586; a probe that shuts A20 through port 92h and then calls `int 21h` | the arena figure |
| **W5** | hibernate on kern_mega, both CPUs (section 8) | HIBER.DRV ~1,000 B; kernel ~60 B; `hbstub.inc` ~40 B mega-only | boot partition (`--c keep --bootc --noa`): `tests/hibernate.py`'s witness (an About box) plus a `tests/megaxm`-style XMS witness (64 KB of patterns), hibernate, `stop`, boot, Resume, patterns compared, both files gone. Negative controls: a kern_big boot refuses the image; a byte of the extended section changed on the host image is refused | the same on the PicoMEM C:, which is the case that motivates it |
| **W6** | option-ROM hygiene at both edges (section 9.2-9.3) | ~60 B | an SB16 DOS program plays under `kern_dos`, and SOUND.DRV plays after the return (a plain card: no multiplexer) | **the real test**: Tracker plays BEVERLY.MOD after a DOS round trip; a DOS Sound Blaster program plays through the PicoMEM under `kern_dos` |
| W7 (option) | recover a park after a warm reset: the fresh boot looks for a valid park header before claiming the pool, and offers Resume | ~300 B, plus a fresh `mega_enter` that must not disturb the park | press Ctrl-Alt-Del inside a DOS program, expect the offer | whether this BIOS keeps extended memory over a 1234h boot, which decides whether W7 exists |

**W1 in Bochs**, which is where most of M6's correctness is decided:

* An About box open as the witness (SPEC.md 87.8's idea). Launch a probe
  through arm 3. It prints its arena and waits for a key, and exits with code
  7. Expect the desktop back with the About box live, `7` in the box's
  window, and on E9 `kern_mega: parked 640K sum xxxxxxxx` and `unparked`.
* **Break it on purpose** (docs/WRITING-TESTS.md §1): while the probe waits,
  `os88bochs.py dbg "setpmem 0x140000 4 0"` corrupts the park. Expect the
  stub's sentence on the glass and a restart, not a resumed desktop. Then the
  same against the GDT (`0x110100`) and `.cold` (`0x125010`).
* Ten round trips in a row with no growth in `mx_tab` (the park and arena
  freed every time: SPEC.md 87.6.1's leak, which this route could repeat).
* A WAV played through Bochs's SB16 before and after (SOUND.DRV unloaded and
  reloaded, its IDT gate rebuilt).
* kern_big's `kernel.bin` and `hiber.drv` byte-identical at one build number
  (KERN-MEGA-HANDOFF Part A.2's recipe). `DOS.O88` either identical or argued.
* The MartyPC rows that cover `kern_dos` on kern_big, because `DOS.O88`
  ships there: `kdhand`, `kdreturn`, `kdreturnm`, `kdpart`, `dosmem` and
  `dosxms` (`tests/suite.py`). Run once, after the last batch (CLAUDE.md's
  Testing rule).

**W2 on the Victory**, one question: does the 286 get out and back? The owner
boots `mega.img` and runs a `.COM` from B: through the Memory page's arm 3,
presses a key, and photographs:

* the program's own screen (its arena line);
* the desktop after it. A new `mega_tell` line, `DOS park 640K out ooo back
  bbb ms`, should be on the bar, with the earlier `RST` and `FDC RESET` lines
  if the reset path touched the floppy;
* the same ten times, then Restart from the System menu (10.3 item 17's
  path, unchanged).

A machine that stops on a blank text screen after the program exits is the
stub. `DOSRMARK=1` (SPEC.md 96.49.3) should then stamp the park stub's stages
on row 7 as it does the hibernate stub's. That is the photograph to ask for.

**Order.** W0 now. W1 then W2 deliver the feature, and they are the critical
path. W3 is compatibility. W4 is memory. W5 is independent of W1-W4 and the
largest; it should wait for track 1's LDT allocator, so its extended section
is designed once against the real claim table rather than against `mx_tab`
alone. W6 can fold into W2's field run if the first trip shows a silent
PicoMEM afterwards.

---

### 10.1 What the waves measured (Bochs, 2026-10-18, branch `protected-mode-7`)

Every figure below is MEASURED, off `build/megak/kernel.map`, the kern_dos map
and Bochs 2.7 (a 386, `megs: 16`, 639KB conventional, VGA, a 20MB C: through
HDD.DRV's rung 1). Bochs is a 386, so the 286's way down (W2) is not in it.

**Bytes.** kern_big's `kernel.bin`, `hiber.drv` and `DOS.O88` (and its
`kerndos.bin`, `dosp.bin`, `doscore.bin`, `dosload.bin`) are byte-identical at
build 1600 against `4460155c`. On kern_mega:

| what | before -> after | the estimate it replaces |
|---|---|---|
| kernel `.text` (`kernel/megados.inc`) | 53,033 -> 55,127, **+2,094**: 1,684 of code, 383 of data (the region list is 336 of it), and `mega_r286`'s branch | ~300 (W1) + ~30 (W2) |
| kernel `.cold` | **+88** (`mega_pkgok` and the loader's call) | - |
| HIBER.DRV's image | **+642** of content (the park stub 524, `hbm_dosrun`'s kern_mega arm 100, the string 18), all of it inside the 415 bytes of padding the image already carried: 6,145 -> 6,381 | ~700 |
| kern_dos (`-DKD_MEGA`) | 34,611 -> **35,002** of 35,840 (`kd_unpark`, A20, the switch: 391); `KD_IMG_KB` 34 -> 35, and `DOSRMARK=1` (35,554) fits the same rung | ~100 |
| kern_mega's `DOS.O88` on disk | 47,610 -> 48,577 (W0's mark and refusal, W3, the way back) | - |

The `.text` figure is the one to argue with: it is conventional memory on
kern_mega, and the PM routines (`mega_dosparkf`, `mega_dsumall`,
`mega_dosgo`, `mega_dwake` - 1,594 of the 1,684) could live in `.cold`, above
1MB, behind far thunks to the `.text` helpers they call. Only `mega_dropr`
(90 bytes) has to be below 1MB. Not done in this round.

**W0.** Section 1.1: both arms fault where 1.1 says, before the refusal; after
it the windowed arm cannot be picked (the Memory page draws one line), a
kern_mega `DOS.O88` on a kernel without `CPU_F_PM` toasts *DOS.O88 is
kern_mega's* (MartyPC, a 5150 on kern_big: `toast_buf` read back, `ld_status`
LD_EABORT, no window; the shipped `DOS.O88` on the same disk geometry opens its
window as the control), and kern_big's on kern_mega toasts **`Use this disk's
DOS.O88`** (photographed, Bochs) and launches nothing.

**W1, the round trip.** `KDHELLO.COM` (`-DKDHOLD`) from C:, through the park:
the program reports **577 KB** above its PSP and the desktop's DOS window
reports **`ended, exit code 042 (Arena: 586KB)`** - the same 586 KB as kern_big's
arm 3 on a 640KB 5150 (SPEC.md 96.40) - with every window back. E9 says
`kern_mega: parked 027FK at 130000 sum ...` and `kern_mega: unparked`; the
menu bar says **`DOSP 0639K o 0018 b 0004`** to `o 0050 b 0008` across runs
(Bochs times: its IPS is not a 286's). The first build parked 608K, because it
rounded the park to 32KB and gave the program 555 KB; the copies are exact to
the KB now.

**W1, break it on purpose** (`os88bochs.py dbg "setpmem ..."` while the program
waits for its key):

| written | what came back |
|---|---|
| `0x138800`, the park image (sampled word) | the sentence - `os8088: KDHELLO.COM  wrote over the parked session. Press a key to restart.` - photographed, and a key restarts the machine |
| `0x110100`, the GDT | restart |
| `0x125000`, `.cold` (sampled word) | restart |
| `0x125010`, `.cold` (NOT a sampled word) | **resumed**: the sparse sum's miss, exactly the trade 12.1 item 3 accepted. The plan's own test address was on a word the sampling skips |

**W1, ten round trips** with SOUND.DRV loaded (`--sb --cfg 1`): 10 parked, 10
unparked, no fault, and `mx_tab` after them holds the park's and the arena's
two rows freed (KB 0) and nothing else - no growth.

**W1, a WAV before and after**: the Audio Player plays `FURELISE.WAV` through
Bochs's SB16 (`sb.wav` 0 -> 1,728,512 bytes), a trip is taken with the player's
window open, and Play after it plays again (`sb.wav` -> 3,457,024): SOUND.DRV
was swept, reloaded and its IRQ gate rebuilt.

**W1, the 1MB AT** (12.1 item 5), by capping the park: `setpmem` of
`mega_parkcap` (linear `0x600 +` its `.text` offset, `0x704D` in this build)
to 192: E9 `parked 00C0K`, the program reports **130 KB**, and the desktop
comes back with the 447 KB left in place untouched (it is in the sum).

**W3** in Bochs: `XMSPROBE.COM` through the park comes home with **exit code
048** (30h): steps 1-5 over the arena passed and the raw `AH=87h` at the park
(130000h) was refused. The round trip and the probe were run again after
`protected-mode`'s track 1 merge (modules above 1MB, the kernel's own blocks
from the pool's top): the same answers. The park is claimed as owner FFh and
so still comes from the pool's BOTTOM, which is what the probe's control
address relies on.

**W2 is built and not run** (Bochs cannot reset a CPU alone). What the Victory
286 run should show is in section 10's W2 paragraph; the trip line is
`DOSP kkkkK o tttt b tttt` (decimal), and a `DOSRMARK=1` build traces the way
back on row 7: `U` (kern_dos reached `kd_unpark`) at column 40, then the park
stub's `P` (protected mode, above 1MB), `S` (sum agrees; `X` if not), `R`
(restored), `K` (into the kernel's tables) at 41-44, and `hbm_wake`'s `W` at
78. On that build the stage stays in the framebuffer on the way out, so the
loader stub's own trace is on the glass too, after a `D` at column 0 from
`mega_dropr`.

### 10.2 W4 and W6 as built, and what they measured

**W4, kern_dos in the HMA** (section 7's design, two departures). kern_mega's
`DOS.O88` carries ONE kern_dos and it is the HMA one: `-DKD_MEGA` implies
`-DKD_HMA`, which assembles the image at `org 10h` in `KD_SEG` = FFFFh and puts
only `FAT_SEG` and `LOW_SEG` at 0060h. So the off switch is not a second
kern_dos: it is **LOADFIX's answer itself** - the thing a program "hates"
about DOS=HIGH is being loaded below 64KB with A20 open (EXEPACK's *Packed file
is corrupt*), and `LOADFIX <program>` at the box's prompt (a window-side verb,
OPEN's room, so the core pays nothing) sets `KDLF_LOADFIX` and kern_dos starts
the arena at 64KB. The trampolines are `kerndos/kdhma.inc`: after
`dos_hook_vectors`, every IVT vector whose segment is FFFFh gets an 8-byte low
slot (`call kdh_tchk` / `jmp far`), the check proving A20 at 0000:FFE0
against FFFF:FFF0 - not 0500/0510, which on this arm is kern_dos's own code -
and reopening it through port 92h and then the 8042. int 1Eh's table is copied
low. HIBER.DRV's loader stub expands the two streams to FFFF:0010 and
FFFF:CORE_ORG (`kds_expand_at`), because a segment cannot rise past FFFFh.
`KERNEL_SEG` is a macro on this arm, undefined across `dos.asm` so the SDK
inside it keeps its own 0060h.

| measured (Bochs) | |
|---|---|
| the arena | **621 KB**, against 586 low - **+35 KB**, the estimate was 615-620 |
| KDHELLO above its PSP | 612 KB, against 577 |
| the image | 35,198 bytes at 100000h (of 65,520); low: FAT 4,608 + LOW 3,072 (`.lowbss` 790, of which the trampolines and the DPT 278) |
| `A20SHUT.COM` (`make build/A20SHUT.COM`) | exit **020h**: A20 shut through port 92h, the next INT 21h served, the gate open again. **Red** with the trampoline pass taken out: it hangs after its banner |
| `LOADFIX KDHELLO` | 557 KB above the PSP and an arena of 566 KB: loaded above 64KB |
| `XMSPROBE.COM` | 048, unchanged |

A vector kern_dos takes AFTER the pass (a child's terminate restoring
22h-24h from its PSP, which was itself made from trampolines) is direct, and
works while A20 is open. A slot table of 24 is full on no program measured.

**W6, option-ROM hygiene** (9.2, 9.3). `mega_drom_scan` reads, before the
snapshot, which IRQ lines have an option ROM's handler (`mega_irqrom`'s own
test); the drop clears them, and the cascade, in both mask pairs kern_dos runs
under (the loader stub's `HS_MASKM/S` and `mega_dropr`'s), and `mega_dwake`
reflects one pass of each such line's handler after the 8259 is re-based.
Bochs has no such line, so the scan finds nothing and the trip is unchanged;
**the prime is unverified** (9.3's own caveat) and is the Victory's.

**And the keyboard, found by typing after a trip.** No key reached the desktop
after the way home: `hbm_wake` re-arms a PS/2 mouse with the 8042 command
byte's keyboard-disable bit (`MOU_P2KOFF`) set, as every write inside
`mouse_init`'s probe window is, and `mouse_init` ends that window with `0xAE`
- which `hbm_wake` never sends. kern_mega sends it now, and `mega_dwake`
drains the 8042 after the re-base besides (a byte the controller holds keeps
IRQ1 high with no new edge). **kern_big's live resume (SPEC.md 96.49) had the
same sequence**, and the Victory 286 then hit it from Test Drive III: the
other track's b793f294 sends `0xAE` in `hbm_wake` on every kernel and banks
and restores the 8042's command byte across a park (`mega_d8042get`/`put`,
KERN-MEGA-PLAN 54.3), so the kern_mega-only `0xAE` this wave added was
removed at the merge and the drain now runs after `mega_d8042put`.

### 10.3 W5 as built: hibernate, and a resume that stays in protected mode

SPEC.md 87.9 is the contract; this is why it reads that way.

**The departure from 8.3.** Section 8.3 planned the fresh kernel restoring
the extended section in place and then DROPPING to real mode to run
SPEC.md 87.5's resume stub in the text framebuffer. Two facts sent it the
other way. Bochs will not fetch an instruction from video memory at all
(W1 found it, 2.5), so that stub cannot run where the plan is tested; and
W1 had already built a stub that does exactly a resume's job - put
conventional memory back from a block above 1MB and enter the kernel
through its own tables - running in place on a GDT of its own. So the fresh
kernel reads the WHOLE FILE into one staging block in the pool with its own
file layer, and enters the park stub over it, which gained one step: a
list of copies (`MPK_NCPY`, 0 on a park) made before the conventional
restore, and a wake address in a cell (`MPK_C_WAKE`) in place of the
assembled `mega_dwake`. Three things fall out, and each was a risk in 8.3:

* **Neither CPU leaves protected mode**, so the 286 spends no reset on a
  resume and the 8259 is never re-based: the Victory and Bochs run the same
  instructions. (The WRITE is protected mode too, and a 286's reflected
  int 13h is the one reset it costs - as for any disk write on kern_mega.)
* **The LDT's upper entries need no ordering rule** (8.3 step 2's "last"):
  the stub writes the tables as data while it runs on its own GDT, so the
  resume path may use anything it likes until then.
* **The transport rule relaxes on kern_mega**: the fresh kernel reads the
  image with whatever the volume's driver speaks, so a native-IDE volume
  resumes - the Bochs runs below went through HDD.DRV's PIO path both ways.

The price is room: the staging block holds the image AND the section, so a
machine needs ~(conventional + section + 2KB) free in the pool beside the
image's own blocks. `hbm_xplan` refuses a hibernate a resume could not
place; the Victory's 3MB pool holds it with megabytes to spare, and a 1MB AT
(a ~190KB pool) cannot hibernate in protected mode at all - it was never
going to fit a 640KB picture above 1MB.

**What the runs found** (Bochs, a 386 with 16MB, `os88bochs.py boot --bootc
--noa --csys`: the kernel's own boot partition on a 20MB C:, the system
floppy's root files beside it so HIBER.DRV loads from C:):

1. **Linear 0 through `MEGA_SEL_LOW0`.** The first attempt faulted in
   HDD.DRV's PIO write at `mov ax, [es:di]`, ES = 0008h, DI = 0500h: the
   write starts from segment 0, `mega_null0` runs each instruction that
   touches it with LOW0 in place of the null selector, and LOW0 stops at
   4FFh on purpose (10.1 item 11). `hbm_xlow` stretches it to 64KB across
   the one `dskw_write` and puts 4FFh back, so the image's GDT holds 4FFh.
2. **A code alias in ES across a reflection** (a kernel bug, not W5's, and
   fixed in `mega_svc`): after the pointer write ES held HIBER.DRV's CODE
   selector (`push cs / pop es`), the `int 16h` at "Press any key" reflected
   to real mode with it, and a 386 keeps a segment's ATTRIBUTES across the
   switch - real mode reloads only the base - so every BIOS store through ES
   faulted, in real mode, for ever: 3GB of Bochs log a minute and a garbled
   text screen. Intel's own exit sequence (SDM 9.9.2) loads writable data
   descriptors first; `mega_svc` now copies DS (KDATA) into ES before it
   clears PE. Any caller with a code alias in ES and a 386 under it had
   this, hibernate or not.
3. **The second round died where the first had not**, on the same PIO
   instruction with ES = 0, reported instead of fixed up: `mega_n0`, the
   "one null-segment instruction is being stepped" flag, was SET in the
   image, because the picture of conventional memory is read one stepped
   word at a time and the word holding the flag is read while it is up. So
   every later null fixup refused ("never nests"). `mega_hbwake` clears it,
   and `mega_fdx` and `mega_irqpend` with it, the other two cells that are
   mid-transfer values in any image - SPEC.md 87.6's class, three new
   members.

| measured (Bochs) | |
|---|---|
| a bare desktop with an About box | a 768KB file: 640KB and a 128KB section (the tables, the 32KB read-ahead store at the pool's top) |
| with MEGAHIB holding 64KB of XMS | **832KB**: 640KB and 192KB, three runs; the 64KB compares equal after the resume, and the instrument goes red on one byte changed from the debugger |
| three rounds in a row | resumed 3, the 64KB equal 3 times out of 3, 0 faults - the second round being the one item 3 had killed |
| a byte of the section flipped on the host | `Hibernation: unreadable`, the question still offered, nothing resumed |
| kern_big | **all 186 built artefacts byte-identical** at build 1600 against `8e162dd1` (the last `protected-mode` merged in) - every driver, module, package, the kernel and every disk image, so kern_big carries no byte of M6 on disk or in RAM |
| cost (kern_mega) | `.text` +180 bytes (the wake, the way out, the helpers factored out of the park, the reflection fix); HIBER.DRV `.modh` +1,815 and `.modhb` +10 - an on-demand module, so none of it resident. The estimate was ~1,000 + ~60 + ~40 |

**Still open.** The Victory's BIOS-only C: (the case that motivates W5:
every write a reflected int 13h, a 286 reset each), and the cross-kernel
refusal, which is argued from `dskw_read`'s FERR_BIG and was not booted.
### 10.3.1 W7 as built: a park survives Ctrl-Alt-Del

SPEC.md 96.40.9's last bullet is the contract. What decided the shape is
that **kern_mega's boot is real mode from start to finish** and `mega_enter`
is the last thing `kmain` does, so a warm boot leaves everything above 1MB
alone until the desktop is up - the "fresh `mega_enter` that must not
disturb the park" of section 10's W7 row is simply not reached. So the
recovery is a probe in the boot blob before `drv_boot` (whose drivers it
would replace anyway), and it re-uses kd_unpark's way in and the stub
unchanged: no question is put, because a DOS handoff's image on kern_big
resumes without one too (SPEC.md 87.5) and the user's own act was only to
end a program. The anchor is in GDT entry 0, outside the sum and never read
by the CPU, and it is consumed on sight.

| measured (Bochs) | |
|---|---|
| KDHELLO waiting in kern_dos, Ctrl-Alt-Del | E9: `a park survived the reset`, `unparked`; the DOS window back, its line without an exit code - and since the second merge with *C:\RUN\KDHELLO.COM: The machine was restarted while it ran.* and its prompt: the probe stages `MPK_RESETCODE`, a refusal code with DOS_MEGA's `DER_RESET`, where it staged "no code" and the box was never woken |
| the same with 64 bytes of the park overwritten from the debugger first | the stub's *KDHELLO.COM wrote over the parked session*; a key; an ORDINARY boot, the anchor gone. (Four zero bytes were not caught: the sum samples one word per line, the trade 12.1 item 3 chose) |
| cost | kern_mega `.text` **+60** (the anchor written and cleared), `.boot2` **+494** - the blob, freed when the desktop is up. Section 10 estimated ~300 |

**Open: the Victory.** Bochs's warm boot keeps RAM; whether the Victory's ROM
does over a 1234h boot is the question section 10 put, and it decides only
whether W7 ever fires there - a ROM that clears it leaves no anchor and the
boot is an ordinary one. The clock after a recovery is this boot's RTC
reading, as a hibernate's resume stages it, and **Bochs cannot check it**:
read from the debugger (`info device cmos`) while KDHELLO waited, its RTC was
02:43 the next day against a host 15:31, and over the next 80 host seconds
it read 03:31, 05:10 and 20:05. It races while the guest is in `hlt`, and an
idle desktop mostly is, so a recovery shows a clock hours ahead of the
session it recovers and the session's own PIT-kept clock is the sane one.
That number is the Victory's to check.

### 10.4 Review findings (2026-10-18, read and not run, at `ff9dc63d`)

*(Written as 10.2 on a parallel track and renumbered at the merge; commit
389becef's "10.2's review" is this section. What became of each finding is
at its foot.)*

A read-only review of W0-W3 (b08f8b87, dc870218, 8be42ccd, 576fafaf,
f5b00c29, d4f5e873). **The gating is right** - every `kernel/hiber.inc` and
`kernel/loader.inc` change is inside `%ifdef KERN_MEGA`, `megados.inc` is
inside mega.inc's own guard, the ordinary hibernate is untouched - **and so
are the park and the return**: the 286's way out (CMOS 0Ah, 40:67) and back
(`lmsw`, no reset), `kd_unpark` proving A20 before touching anything above
1MB, the 8259 back to 20h with the parked masks, the IVT and its gates
rebuilt, NMI on, and a mismatch that really restarts (answer 3). A cache the
DOS program overwrote cannot be served: `hbm_wake` still flushes the
read-ahead and drops the FAT windows, and the kernel's own pool blocks
(`MX_KOWN`: the read-ahead store, the modules above 1MB) are in the sum.

**Four defects to fix before M6 is called done**, in the order they bite:

1. **An overlapping forward XMS move corrupts.** XMS 0Bh guarantees a move
   whose source is below an overlapping destination; `apps/dos/dos.asm`'s
   `.bothx`/`.chunk` walks forward in 32KB chunks of `AH=87h`, each a forward
   `rep movsw`, so 64KB from offset 0 to offset 2 of one handle fills the
   destination with the first word and still answers AX = 1. Copy the chunks
   in reverse when src < dst and the ranges overlap (or refuse).
2. **A20 is open for the whole DOS run.** Nothing closes it at the drop
   (`mega_dosgo`, `mega_dropr`); the program loads at PSP ~0F80h, which is
   EXEPACK's "Packed file is corrupt" condition and breaks anything relying on
   the 1MB wrap, and the XMS A20 calls answer yes and change nothing. Section
   7 lists the risk for W4 only; W1 already has it. Close it in `mega_dropr`
   (`kd_unpark` already opens and proves it on the way back) and make the
   XMS local/global enable calls real.
3. **The "No room above 1MB" refusal leaks.** `hbm_dosrun`'s `.b7` (kernel/
   hiber.inc) is reached after the `MEM_K_HIB` extent claim in `[hb_xseg]`
   and after `hbm_vbn`'s chdir, and `.bad` frees neither - each retry leaks a
   block of conventional heap and leaves the user in DOS.O88's folder. On a
   1MB AT `.b7` is the refusal that actually fires. Claim the park before the
   extent list, or free it and `hbm_vbk` on `.b7`. (kern_big's `.b6` has the
   same shape.)
4. **Owner answer 5 is half built.** `mega_dosclaimf` shrinks the park 16KB at
   a time and leaves the rest in place, but "park only the runs in use" is
   not there, and 5.1's "caches dropped before the park" is not done - the
   read-ahead store (up to ~32KB at the pool's top) stays claimed and summed.
   On a real 1MB AT (~192KB of pool less the kernel's high blocks) that can
   put the park under `MPK_MIN_KB` and refuse outright; the Bochs "1MB" run
   only capped `mega_parkcap` and never had a short pool. Shed the
   read-ahead store first (`osapi_dsk_cache` AL = 0; `hbm_wake` re-solves it
   anyway) and then decide on the runs-in-use step.

**Lesser, for the same round:**
* The sum misses three things that a mismatch should catch: IDT gates
  20h-21h (`sch_isr`, `kbm_isr`; `mega_ivt_sync` rebuilds from 22h, so they
  are neither checked nor rebuilt), the GDT's first line (LOW0, KDATA, LDT,
  BCODE, IDT) and entries 1FF8h-1FFDh including `MEGA_SEL_LDTD` (1022.75
  lines rounded down).
* The park header's own cells (`MPK_C_PBASE`, `MPK_C_PKB`, `MPK_C_PSUM`, the
  GDTR/IDTR copies) are not summed, and the stub compares against the
  header's copy of the sum rather than the launch block's `KDL_PSUM`, which
  kern_dos holds below 1MB. A stray write that shrinks the KB count restores
  too little and still matches. `kd_unpark` passing `KDL_PSUM`/`KDL_PKB`/
  `KDL_PBASE` in registers for the stub to compare closes it.
* The XMS door: lock/unlock (0Ch/0Dh) and 0Eh/0Fh answer 80h, which the 286
  extenders 5.2 names need; 08h reports total free as the largest block; 0Bh
  does not check offset + length against the HANDLE's size, so a move can
  reach the next handle's block.
* `int 15h` `E801h`, `E820h` and `C7h` pass to the ROM and report the real
  extended size.
* 3.3/9.3 ledger items not built: the 8042 command byte, the MSW low bits
  (`kd_unpark` keeps what the program left), the serial mouse's line after
  ICW1.
* `mx_tab` has 8 rows (`XM_MAX_BLKS`): with the table full, `mega_dosclaimf`
  walks down to 128KB and says "No room above 1MB" whatever the free KB.
* kern_mega without PM runs no DOS box at all (kern_mega's DOS.O88 refuses
  and `mega_pkgok` refuses kern_big's), so `hbm_dosrun`'s `.kbimg`/`.kbhand`
  arms are unreachable there - answer 4 may intend that; say so if so.

**What became of them** (Track 6, at the merge):

1. **Fixed.** `dos_xms_move`'s extended-to-extended arm copies an overlap
   with the source first TAIL FIRST, in chunks no longer than the distance
   between the ends (`.ovl`, `DOS_MEGA` only: kern_mega's core +141 bytes,
   `CORE_MAX` 15,424 -> 15,552 on that arm alone). `XMSPROBE.COM` gained the
   case as step 8 - 2KB moved two bytes up inside one handle - and reads
   **exit 048 with the fix and 072 (step 8) without it**, both under Bochs.
2. **Superseded by W4.** kern_dos lives in the HMA now (10.2), so A20 has to
   be open across the run, and the trampolines re-open it on every vector.
   EXEPACK's condition is what `LOADFIX <program>` answers, and a program
   that wants the wrap is LOADFIX's too.
3. **Fixed** on kern_mega: every refusal after `hbm_vbn` (`.b2`-`.b7`) frees
   the extent claim if it was taken and puts the user's volume back
   (`.badv`); kern_big keeps its bytes, so its `.b6` shape stands there.
4. **Built, both halves.** The read-ahead store is given back before the park
   is sized (389becef, the other track), and **the park is the RUNS IN USE**
   (10.6): 196KB parked where it was 639KB, with the program's arena still
   the whole 621KB.

**The lesser items, at the second merge:**

* **The sum's gaps are closed by RESTORING them**, not only checking:
  `mega_dsave` banks GDT entries 1-5 and 7, 1FF8h-1FFDh and IDT gates
  20h-21h in the park's header (`MPK_SAVE`) and four entries of the stub's
  copy list put them back before it loads the kernel's tables. Entry 0 is
  W7's anchor and 6 is ODD's, written per use. Bochs: KDATA's base, LDTD and
  gate 20h overwritten while KDHELLO waited, and the session came back with
  exit code 042.
* **The header is checked against what is not in it.** `kd_unpark` passes
  the sum, the KB and the base in registers from its launch block below 1MB;
  the stub compares them with its cells, the GDTR and IDTR cells with the
  build's constants and the wake with its two legal targets. The list, the
  counts and the bank are in the sum now (one region, `MPK_MAGIC` to the end
  of the bank), and the bank's unused entry-0 slot holds a second copy of the
  KB and the base, so W7 - which after a reset has only the header to pass -
  is covered too. Bochs: the KB cell shrunk to 256 while KDHELLO waited is
  refused. **The first run of that test found a defect**: the refusal's reset
  left the anchor standing, the next boot's probe took the shrunk park back
  with its own self-consistent cells, restored 256KB and faulted. The stub
  clears the anchor before it says anything now, and the same test boots
  ordinarily after the key. Its GDT moved to +380 to make room
  (`KDP_GDT`, one build, one number).
* **The XMS door** - 0Ch-0Fh, 08h's largest block, 0Bh's handle bounds - and
  **`int 15h` E801h/E881h/E820h/C7h/8Ah**: SPEC.md 96.15.4.
* **The MSW** is banked at the snapshot (`mega_dmsw`, kernel data, so it is
  in the copy) and `lmsw`'d back in `mega_dwake`: kern_dos needs no byte.
* **The 8042 command byte** is the other track's `mega_d8042get`/`put`.
* **The serial mouse's line needs nothing**: `kd_mou_stop` leaves IER = 0,
  so the UART's INTR is low across `mega_dwake`'s ICW1, and `hbm_wake`'s
  step 3d writes IER last, after the drain is armed - the rising edge comes
  then. Every Bochs round trip here clicks after the return.
* **`mx_tab` is 24 rows** (`MX_NBLK = XM_MAX_BLKS + 16`, the kernel's own
  blocks having rows of their own since the region-list merge), so the "8
  rows" finding no longer holds; a package can hold at most XMEM.DRV's eight.
* **kern_mega without protected mode runs no DOS box, and that is the
  intent**: kern_mega's DOS.O88 refuses there (it is built for the park) and
  `mega_pkgok` refuses kern_big's on kern_mega, so `hbm_dosrun`'s
  `.kbimg`/`.kbhand` arms are unreachable on that build. A machine that
  cannot enter protected mode wants kern_big's disks.

### 10.5 Review findings for W4-W7 (2026-10-18, read and not run, at `59bbfb6c`)

A read-only review of 76e225b2, 0444dff2, c1243768, af133b46, f9f5e4a7,
a3c8deda and the merges, beside a Bochs run at 4MB (`OS88_BOCHS_MEGS=4`:
KDHELLO.COM parks, runs, comes home with `Arena: 621KB` - W4's +35 - and the
keyboard types after it). **kern_big is untouched**: KERNEL.SYS and all six
360KB images byte-identical at build 1600 against `b793f294`, every change
inside `%ifdef KERN_MEGA`/`DOS_MEGA`/`KD_HMA` or byte-equal. Both earlier
fixes survived the merge (b793f294's `0xAE` once in `hbm_wake`, the
`mega_d8042get`/`put` pair; df2db099's `PUSH_CSDATA` record copy), and W4's
keyboard work does not conflict with them. Of 10.4's four: the overlapping
move is fixed (but see below), the `.b7` leak and chdir are fixed on
kern_mega, the read-ahead shed is built, "park only the runs in use" is open,
and A20-open is now ACCEPTED BY DESIGN because kern_dos lives in the HMA -
which is what the first four defects below cost.

**Five defects**, in the order they bite:

1. **The HMA arm floppies with a STALE EOT, and can write the wrong sectors.**
   `dsk_xfer` (kernel/disk.inc, before every int 13h) stores the mounted
   volume's sectors-per-track into `dsk_dpt+4` - SPEC.md 18.92's whole
   reason the table is ours. Under `KD_HMA`, `kerndos/kdentry.inc` points
   int 1Eh at `LOW_SEG:kdh_dpt`, a copy taken ONCE at entry, so every later
   per-call store lands in the HMA original and the BIOS never sees it. The
   copy holds whichever volume was touched last before the handoff (C:'s 17
   or 63, a 720KB disk's 9); with an EOT below the real count a multi-track
   transfer carries on at head 1 sector 1 and answers CF = 0. A DOS program
   writing to a 1.44MB B: after a C: boot writes onto the wrong sectors.
   Bochs's BIOS ignores the table's EOT, which is why no run showed it.
   CONFIRMED by reading. Fix: `dsk_dpt` itself in `.lowbss` on `KD_HMA`, or
   the EOT store into `kdh_dpt` as well.
2. **`kdx_87` calls the ROM's int 15h AH=87h from INSIDE the HMA**
   (`kerndos/kdxms.inc`, `call far [kdx_old15]`). An IBM-AT-style ROM turns
   A20 OFF at the end of a block move, and its `iret` then lands at
   FFFF:next with the gate shut - wrapped low memory. That is every XMS 0Bh
   move on such a ROM. Bochs's ROM restores the old A20 state. Fix: a low
   stub that re-proves A20 before returning into the HMA. The Victory's ROM
   behaviour is unknown.
3. **A CP/M-style `CALL 5` jumps into kern_dos** (`apps/dos/dos.asm`'s PSP
   build): the segment PSP + 5 - 0FEFh wraps for any PSP below FEAh, so
   with A20 open the target is 100000h + (PSP+5)x16 - inside kern_dos's
   HMA image for every program loaded low, which is the default. It was
   broken before W4 too, more quietly (empty memory). DOS 5's shape is a
   fixed F01D:FEF0 target with a far JMP at FFFF:00D0, or a non-wrapping
   segment on the HMA arm.
4. **The A20 trampolines do not cover what `kdhma.inc`'s header claims.**
   `KDH_NSLOT` is 24 against "~10" - `dos_ivtab` alone is 29 vectors, so
   38h-3Eh are ALWAYS direct; `dos_m33_arm` hooks int 08h directly at FFFF
   on the first int 33h, so any shut A20 afterwards dies on the next tick;
   `dos_psp_make` writes int 22h-24h as FFFF (direct after any child exits);
   int 2Fh AX=4310h hands out FFFF:`dos_xms_ent` direct. The 8042 reopen
   writes DFh and jumps without waiting for the gate - on an AT with no port
   92h (probably the Victory) the first instruction after the trampoline
   fetches with A20 still shut - and IF stays on through the check, so an IRQ
   between the two `not`s leaves 0:FFE0 inverted, and one between `out
   64h,D1` and `out 60h,DF` sends DFh to the KEYBOARD as a command. A20SHUT
   tests port 92h with int 21h only and reaches none of these.
5. **A W5 resume does not bound what it restores against THIS machine**
   (`hbm_xres`): the run count is capped at `MPK_CMAX`, but each run's
   destination and length come from the image and are never checked against
   this boot's `[xm_kb]` or the pool, and run 0 not against the table range.
   Resume with less extended memory (a SIMM out, a BIOS setting) and the
   stub copies into memory that is not there and the kernel believes in the
   old pool. Also: the conventional image has no checksum (kern_big's has
   none either), and `hbm_xisz` rounding `mem_top` to 32KB loosens kern_big's
   exact size match to "within a chunk".

**Lesser:** the stub's `.bad` leaves W7's anchor set, so a failed ordinary
return shows "wrote over the parked session" twice before a clean boot; a
STALE anchor can resurrect an old session (Ctrl-Alt-Del, boot something
else that does not touch 110000h, work, warm-boot kern_mega) - the sum cannot
tell old from intact, a stamp or the warm-boot flag would; the overlap fix
copies forward at distance 1 (`and bl, 0FEh` gives 0); W6 primes EVERY
ROM-owned IRQ line, not only the PicoMEM's, where 9.3's "only if pending" was
the safer arm; the W5 staging block is placed around the pool's blocks but
never CLAIMED for the file read; "Hibernate..." is never greyed in PM, so a
1MB AT refuses only at the write (SPEC.md 47); stale comments (`mega_dropr`
and megados.inc's "kern_dos at 0x600", kdhma.inc's "~10" and "made from
trampolines"); no assertion ties the HMA image's end to the FFFF:FFF0 probe
word (there is room: 35KB). Not walked: that every int 13h buffer on the HMA
arm is low (`dsk_secbuf`, `disk_dir`, `FAT_SEG` appear to be).

**Verdict: the structure is sound and kern_big is untouched; W4 is not done
until 1-4 are, and W5 wants 5.** 1 is the one that loses data.

### 10.5.1 What has been fixed since (2026-10-18)

Each one is kern_mega only; kern_big's KERNEL.SYS, DOS core and box are
byte-identical across all of them.

1. **Defect 1, FIXED (`ac55f667`).** `kdh_dptsync` (kerndos/kdhma.inc)
   copies `dsk_dpt` into the low `kdh_dpt` immediately before the transfer
   `int 13h` in `dsk_xfer` and `dskw_fmt_low`, so the BIOS reads the EOT
   the per-call store just wrote. The copy, not a move of `dsk_dpt` into
   `.lowbss`, because the copy is two call sites while the move changes
   every reader in the shared disk layer.
2. **Defect 2, FIXED (`ac55f667`).** `kdx_87` reaches the ROM's AH=87h
   through `kdh_s87`, a low slot whose return lands in `kdh_tchk`, so the
   gate is re-proved before the HMA executes. Bochs at 4MB: XMSPROBE exit
   048, A20SHUT exit 032.
3. **Defect 3, FIXED.** The HMA arm builds PSP:5 to reach `dos_cpm`,
   which is paragraph-aligned IN THE CORE. It is not in kern_dos, because
   the core is assembled apart from both hosts, so a kern_dos label is not
   visible to it. The PSP builder picks it at RUN time, when `CS = FFFFh`,
   because the core is shared with the windowed box, which is not in the
   HMA. `dos_cpm` takes CL rather than AH and returns to the program's own
   near return; PSP:0050's gate did neither, so CALL 5 had never served
   CP/M on any kernel. `tests/dostrap/call5.asm` prints through function 2
   twice and exits with function 19h's answer: **exit 002 from C:** in
   Bochs at 4MB. kern_mega's `CORE_MAX` +64 (15,616; 15,599 used).
   **kern_big's CALL 5 was still the PSP:0050 gate**, and the owner's
   answer to that question was to fix it too: `dos_cpm` is in every build of
   the core now and PSP:5 is `CS + dos_cpm/16 - size/16` wherever the core
   runs (SPEC.md 96.21.4) - a word in the windowed box, a wrap under
   kern_dos low in memory, the old field only for an HMA block too small to
   reach. **48 bytes of the core, on both hosts** (`CORE_MAX` 15,344 ->
   15,392, 15,391 used; kern_mega's 16,064 unchanged). `tests/call5.py`
   runs CALL5.COM on kern_big with B: current, windowed and under kern_dos:
   **exit 1 on both legs**, and **0 on both** with PSP:5 aimed back at the
   gate (AH is 0 at entry, so its first CALL 5 is a terminate).
4. **Defect 4, FIXED.** The reopen first (`ac55f667`): it runs under
   `pushf`/`cli` and banks every register. It tries port 92h first, then
   the 8042 with an input-buffer wait before each write, then waits up to
   64K re-tests for the gate.
   Then the direct addresses. Every far address the core publishes after
   entry now goes through the core's `dos_hvec`: the PSP's 0Ah-14h, the
   mouse's IRQ0 hook and int 2Fh AX=4310h's XMS entry. On the HMA run that
   calls the host's `kdh_vslot` through `dos_hslot`, a `DOS_MEGA` core cell
   placed after every other DBSS row, so no cell before it moves.
   `kdh_vslot` hands back the slot that already names a target, so
   republishing costs nothing, and `KDH_NSLOT` is 48 for ~40 distinct
   targets. A full pool falls back to the direct address.
   The packet driver's vector is deliberately NOT a slot: a client finds
   the driver by reading `PKT DRVR` at the target + 3. It is also never
   published on this host, because `dos_pkt_start` is the windowed box's.
   `tests/dostrap/a20pub.asm` checks each address's segment, shuts port
   92h, lets three ticks pass and calls the XMS entry. It reads **exit 071**
   in Bochs at 4MB and **hangs without the fix**. A20SHUT 032, XMSPROBE
   048, CALL5 002 and KDHELLO 042 still pass. kern_mega's `CORE_MAX` is
   15,680 (15,645 used) and `CORE_BSS_SIZE` 3,509.
5. **Defect 5, the BOUND FIXED; the conventional image's sum DEFERRED.**
   The writer records the pool's `[xm_kb]` at offset 6 of the section
   head, which was zero. A resume compares it first: an image cut from a
   bigger pool than this boot's refuses with "No room above 1MB" and the
   question stays offered, because the restored kernel would believe the
   image's `[xm_kb]`. Then `hbm_xruns` checks every run. Run 0 must be
   exactly the tables' range. Every other run must be whole KB, not empty,
   and inside `MEGA_XPOOL` + this boot's `[xm_kb]`. Anything else is
   "Hibernation: unreadable". An image from before this change has 0 at
   offset 6 and is refused as unreadable.
   Bochs, booted off C:'s boot partition: hibernate at 16MB with an About
   box up, then boot at 4MB, which **refuses**, then at 16MB, which
   **resumes, About box and all**. On the old code the 4MB boot would have
   placed its staging block and copied a run to 0FF8000h, memory the
   machine does not have. The first build bounded the high word at 3Fh,
   which is 4MB and not 64MB, and refused a good image. A kept C: carries
   the OLD HIBER.DRV, so it cannot test a rebuilt one.
   **Deferred, and why:** a sum over the conventional image would mean
   summing during kern_big's shared image write, or re-reading ~640KB
   after it. kern_big has no such sum either, so the conventional image is
   still exactly as trusted as kern_big's. `hbm_xisz`'s 32KB rounding of
   `mem_top` is left as it is. [DECISION FOR THE OWNER: a whole-file sum
   on both kernels?]
6. **The lesser items, as of now:**
   * FIXED: "Hibernate..." greys in protected mode as
     **"Hibernate (No room)"** when the pool is smaller than conventional
     memory plus the tables (`HB_XMIN_KB`, 85KB). That refusal is certain,
     so the grey is a fact. Bochs: offered at 4MB and 16MB; seen greyed on
     a build with the threshold raised 16MB. A 1MB machine stays in real
     mode and keeps kern_big's rule.
   * FIXED: the resume's staging block is CLAIMED in `mx_tab` as the
     kernel's (`hbm_xclaim`) for the file read. A refused resume frees it
     by its offset (`hbm_xunclm`). `mega_dpbase` is zeroed first, so a
     stale base cannot free a block somebody else has since taken. The
     4MB-refuse / 16MB-resume round passes with it.
   * FIXED: the stale "kern_dos at 0x600" comments in megados.inc. An
     assertion in `kerndos/kdos.asm` now says the HMA image and its bss
     stay below FFFF:FFF0, the word the A20 proof flips.
   * **KEPT AS BUILT: W6 primes every ROM-owned line.** The 8259 cannot
     say which line is held, because ICW1 clears the edge latch and IRR
     with it. So "only if pending" means reading the PicoMEM's own request
     list (ROM + 4356h), which only covers the PicoMEM. No field run has
     shown a stray pass. [DECISION FOR THE OWNER: switch to the
     PicoMEM-gated prime if a Victory run shows one.]
   * LEFT TO THE DOS SESSION, whose areas these are: the stub's `.bad`
     with W7's anchor set, a stale anchor, and the distance-1 overlap copy.

5. **Lesser: the stub's `.bad` leaves W7's anchor set, FIXED** on the other
   track at the same time (10.4's lesser items): the stub clears it before
   it says anything, found by a test whose refusal's reset let the next boot
   take a shrunk park back and fault.
6. **Lesser: the overlap at distance 1, FIXED at the merge.** One byte apart
   cannot be split into word moves at all, so it answers A8h, XMS's own
   "invalid overlap"; distance 0 moves nothing, and every other odd distance
   takes word chunks one byte shorter than it, which cannot overlap.
7. **Lesser: the stale comments' "kern_dos at 0x600"** went with the runs
   park's stage (`mega_dropr` puts it under the top of conventional memory).

### 10.6 The park is the runs in use (owner answer 5, second half)

`mega_dmapb` marks conventional memory a bit a KB: `[0, mem_base)` - the
IVT, the kernel, the stacks - and every live `mem_tab` record, rounded out
to whole KB. **Free heap is nobody's**: the claim map is out of band, so a
free run holds no state, and a freed region already hands its old bytes to
the next claimant, which is the same promise. The first run (from 0) goes
back through the stub's own restore with `MPK_C_PKB` = its KB; every later
one is one more entry of the copy list W5 built, packed into the image after
it, so the stub gained no code for it.

The claim is the marked KB plus `MPK_SLACK` (16KB) for whatever is claimed
between the claim and the snapshot; the snapshot counts again, and a park
that outgrew its claim - or more runs than the header has room for, which
`MPK_RUNMAX` = `MEM_MAX` + 1 makes impossible by construction - is the
PREFIX park of the claim instead, as before. So is a count within 16KB of a
full park, or one over `[mega_parkcap]`. A runs park leaves nothing in place,
so the arena is not capped and the loader's stage goes under the top of
conventional memory, which is either parked or free.

| measured (Bochs, the 16MB machine) | prefix | runs |
|---|---|---|
| parked | 639KB (`027F`) | **196KB** (`00C4`) |
| XMSPROBE's arena | 621KB | **621KB**, exit 048 |
| KDHELLO through Ctrl-Alt-Del (W7) | back | back |

On a 1MB AT the pool is ~190KB less the kernel's own blocks, so a desktop
whose claims are under ~180KB now parks whole and gives the program all of
conventional memory where the prefix park gave it what the pool could hold.
**Not run on a 1MB machine**: Bochs is 16MB, and `[mega_parkcap]` caps the
prefix park, which the runs park only falls back to.

### 10.7 The park after packages-high: nothing torn down past the snapshot

Packages-high waves P4 and P5 (KERN-MEGA-PLAN items 81-86) put driver images,
their claims and the kernel's caches above 1MB, and **every trip came back
refused** - *"NAME wrote over the parked session"* - for every program,
before it had run a line. Found in Bochs by dumping extended memory at a
breakpoint just after the park's sum and again at the refusal, and diffing
inside each summed region:

| region | what changed | written by |
|---|---|---|
| GDT entry 1700h, byte 5 | the B800 text page's ACCESSED bit | the CPU, when `hbm_down` loads the stage's segment after the snapshot |
| LDT entries 501h-503h | zeroed | `mega_ldt_free`, from `drv_shutdown_x` -> `drv_release` -> `mega_drvhifree` -> `mega_imghifree` |
| HDD.DRV's image, +B05h and +E00h-E21h | its state | the same `drv_shutdown_x` |

Before P4 a driver's image was conventional memory, which the restore puts
back, so `hbm_down` unloading it after the snapshot was undone on the way
home. Above 1MB it is not, and the checksum is right to refuse: the restored
kernel believes in a driver whose selectors are zeroed and whose state is
"unloaded". **So on the park route nothing is torn down after the
snapshot**: `hbm_dosrun` raises `[mega_dparked]` when `mega_dosparkf` returns
into the drop, and `hbm_down` then skips `cp_flush_close` and
`drv_shutdown_x`. The sweep before the snapshot has already taken every chip
owner, and what it leaves loaded (disk, cable, RAM disk, EMS: SPEC.md 87.4's
skip list) hooks nothing kern_dos runs into, so those ride across the trip
as they were. The byte is kernel data, so the restore itself puts it back to
0 on every way home, W7's included. **The accessed bit** is set on every
present code or data descriptor in both tables just before the sum
(`mega_dacc`), since the CPU sets it on any first load and the kernel never
reads it.

Bochs, on the tree with all of P1-P5 and the second census: XMSPROBE 048,
CALL5 002 (the HMA run), two trips in one session, a Ctrl-Alt-Del recovery,
a hibernate round; and the refusal on every one of them without the fix.

## 11. What this plan could not determine

* **Whether section 1's predicted faults are real.** They come from reading,
  not from a run. W0's first step is the run.
* **Whether HIBER.DRV runs at all in protected mode.** Hibernate has been
  greyed since it could be reached, and `hbm_dosrun` has never run on
  kern_mega. Its CS writes (45 by KERN-MEGA-PLAN 2.4's count) would be
  completed one #GP each by `mega_gpw`. That should be slow but correct on a
  path that runs once. It is unmeasured.
* **Whether the Victory's BIOS re-initialises the 8259 on the shutdown-0Ah
  path**, and whether it keeps extended memory over a warm (1234h) boot. The
  second decides W7.
* **The PicoMEM firmware's response to a spurious multiplexer pass**
  (section 9.3). Only a binary is in the tree.
* **`kern_dos`'s real headroom.** `kerndos/kdlayout.inc`'s comment says 133
  bytes left in the 33 KB rung. `build/kdos/DOS.O88` is 47,610 bytes at this
  commit against SPEC.md 96.40.3's 44,337, so that comment may be stale in
  either direction. W1 and W3 should measure the assertion at the foot of
  `kerndos/kdos.asm` before deciding where their bytes go.
* **Whether kern_big's and kern_mega's `MOD_STAMP` differ at one commit**
  (section 8.2, step 4).
* **Whether the shared disk layer survives `CS = DS = FFFFh`** (section 7).
  kern_big's `.cold` runs there, but `.cold` holds no `.bss`.
* **Every byte cost of new code and the sum's time.** All are estimates.
  The copy rate is the one measured input.

---

## 12. Open questions only the owner can settle

1. **Should kern_mega's DOS box be the park and nothing else?** That is,
   grey the windowed arms for good on this kernel, with the 386 V86 tier as
   the only way back to a windowed box, and drop the Memory page's radio to
   one arm here.
2. **Durability against speed.** On a machine with a BIOS fixed disk, should
   the handoff ALSO write a hibernation image (~2 s each way on iron, SPEC.md
   96.41), so that a DOS program that hangs the machine, or a Ctrl-Alt-Del,
   does not lose the session? Or is the park alone right, with W7 as the
   answer to a warm reset?
3. **The sum: full (~0.4 s each way at 16 MHz, estimated) or sparse (~25 ms,
   misses small writes)?** And is a mismatch a restart with a sentence
   (section 5.4), or a question offering to try anyway?
4. **One `DOS.O88` or two.** If the park, the XMS door and the HMA arm live
   in the one `kern_dos` both kernels ship, kern_big's DOS program pays ~1 KB
   for doors that cannot open there (section 6). A kern_mega-built `DOS.O88`
   on `mega.img` costs kern_big nothing, but it breaks SPEC.md 96.40.3's
   "one file carries the whole function from disk A to disk B": a copy taken
   to the other kernel would refuse arm 3.
5. **The minimum machine.** Is "a 2 MB AT" (section 5.1) acceptable for arm 3
   on kern_mega? Or should W1 park only the runs in use, so a 1 MB AT
   qualifies whenever its desktop is small enough, at the cost of a run list
   in the stub?
6. **Is the HMA worth a third `kern_dos`** (section 7: ~30 KB for the program,
   one more assembly arm to keep correct, and DOS=HIGH's A20 and EXEPACK
   hazards)? If it is, should the box offer LOADFIX's answer?
7. **The XMS arena's size**: everything the pool has left (the default here),
   or a figure on the Memory page so a program that grabs all XMS cannot
   starve one launched from it?
8. **Is W5 (hibernate) wanted before track 1's allocator lands**, against
   `mx_tab` alone, or after it, once?

### 12.1 The owner's answers (2026-10-18)

1. **The park and nothing else on kern_mega.** "Windowed" today is the fsx
   bracket, which to the user looks the same as handing the machine over and
   taking it back; a TopView-style box is a later idea, not this plan. The
   Memory page shows one choice on kern_mega, or is not drawn.
2. **Durability is an OPTION, in place of the Memory page**, if the time it
   costs is worth offering: "also save the session to the hard disk before
   running DOS", shown only where a BIOS fixed disk exists. It rides on W5
   (hibernate on kern_mega), so it arrives with W5 and not before.
   **BUILT** (KERN-MEGA-PLAN 14.43, SPEC.md 96.40.9.3): *Save the session
   to disk*, unticked by default, greyed with no fixed disk; the image is
   written before the park's claim, and every wake deletes it.
3. **A mismatch restarts**, with the sentence (section 5.4). Something went
   wrong, and resuming could look as if it worked while something unknown
   is broken. **The sum is SPARSE, provided it is DISTRIBUTED** across every
   region it covers: a DOS program that ignores DOS's own rules and writes
   only a few bytes into memory it was never given is unlikely enough.
4. **Two `DOS.O88`s.** There are kern_small programs and there will be
   kern_mega programs, and DOS ships on each system disk. `mega.img` carries a
   kern_mega build of it; a kern_big copy brought to kern_mega (or the reverse)
   refuses. kern_big's `DOS.O88` stays byte-identical; kern_mega's does not
   have to be.
5. **A 1MB AT must work.** Park what fits; if the high pool cannot hold all
   of conventional memory, park only the runs in use, and if even that does
   not fit, leave the remainder in place and give the DOS program less - its
   arena ends below the lowest byte left in place, as kern_big's arms limit
   the arena today.
6. **The HMA is wanted, as its own later wave** unless it turns out easy
   to build: the most stable arrangement, with an option to turn it off for
   programs that hate it (LOADFIX's answer).
7. **The XMS arena is everything left in the pool.** The kernel is not
   running and nothing else can use it while DOS runs.
8. **Left to the plan**: W5 waits for track 1's LDT allocator, as section 10
   already recommends, so the extended section of the image is designed once
   against the real claim table.
