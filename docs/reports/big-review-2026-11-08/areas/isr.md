# LENS: interrupts, critical sections, reentrancy

Reviewer key `isr`. Tree: `the-big-review` at 4f0da91. This lens runs across the
scheduler, input, sound, memory and kern_mega areas, so it overlaps their reports.
Where another area already holds a finding, it is cited by id (`sched-7`, `mem-2`,
`input-3`, `mc-6`, ...) and only new evidence is added here.

## Scope read

Read line by line, including the code on both sides of each `pushf/cli`:

| file | lines | what |
|---|---|---|
| `kernel/sched.inc` (3,092) | 1-200, 470-1000, 1180-1340, 1600-1660, 1780-2580, 2900-3092 | `sch_isr`, `task_yield`, `sch_switch`/`sch_resume`, `sch_rhook`, `sched_stop`, idle task, `task_spawn`/`task_exit`, `sch_account`, `sch_pit_now`, the `.lowbss`/`.bss` layout |
| `kernel/mouse.inc` (6,699) | 395-470, 895-905, 1200-1520, 1580-1600, 1840-1870, 2060-2125, 2185-2300, 2860-3390, 3600-3800, 3950-4070, 4180-4300 | MOUPRIV, `mou_lockon`, `mouse_unhook`, `mou_vset`, `mou_p2_isr`, `kbm_isr`, `kbm_i15`, `kbd_track`, `kbd_ovflow`, `mou_isr`, `osapi_mouse_feed`, the cursor gate in `mou_apply` |
| `kernel/events.inc` (~360) | 40-300 | `evq_push`, `evq_pop`, `evq_mup`, `evq_drain` |
| `kernel/wm.inc` | 2260-2420, 4540-4565, 11660-11800 | `wm_wake`, `wm_wake_disp`, `wm_nest_pushd/pop` |
| `kernel/ui.inc` | 268-290, 600-690, 730-760, 1160-1215, 3118-3175 | wake redo, `ui_post` producers and consumer, the idle guard, `ui_cmd_reboot` |
| `kernel/serial.inc` | 90-140, 260-300, 395-520 | `srl_api`, `srl_open`/`srl_close`, `srl_pump` |
| `kernel/snd.inc` | 150-560, 990-1100 | the speaker tone, `snd_tone_req`, `snd_tick`, `snd_release_inst`, `spk_off`, `osapi_fsx_spk` |
| `kernel/splash.inc` | 990-1290 | `spl_spin`, `spl_anim`, `spl_isr`, `spl_hook/unhook/swap` |
| `kernel/fsx.inc` | 1290-1460, 2240-2310, 2820-2880 | `fsx_rcall`, `fsx_rtick`, `fsx_rdownf`/`fsx_rupf` (kern_mega) |
| `kernel/vga12.inc` | 5040-5180 | `gfx_lock`/`gfx_unlock` |
| `kernel/disk.inc` | 1-40, 870-895, 1440-1470, 1960-2080 | the `int 13h` bracket and its `sch_lock` |
| `kernel/memory.inc` | 1820-1880, 3075-3092, 4555-4668 | the backward chunk copy, the driver-image IF=0 move, `mem_regrow`'s window |
| `kernel/instance.inc` | 1965-1990, 2290-2360 | `inst_wchk` ("int 0, no handler"), `osapi_sys_snapshot_x` |
| `kernel/clock.inc`, `clockw.inc` | 55-150, 1105-1160, 1395-1440; 200-215 | RTC IF=0 bursts |
| `kernel/shutdown.inc` | 1-120 | the Shut Down tail |
| `kernel/hiber.inc` | 860-890, 4590-4640, 4825-4960 | the wake's IMR and 8042 re-arm |
| `kernel/kernel.asm` | 5290-5335, 7705-7760 | BOOT_MARK's fault-vector hooks |
| `kernel/driver.inc` | 2537-2600, 5679-5700 | `drv_svc_call_x`, `drvp_wr` |
| short IF=0 windows | `toast.inc` 203, 589; `dock.inc` 577; `dockmod.inc` 596; `menu.inc` 2257; `stikmenu.inc` 1679; `vidsel.inc` 1269; `font.inc` 160-240; `filecp.inc` 534; `diskw.inc` 5909; `compress.inc` 2915-2960; `stkdiag.inc` 205-305 | |
| drivers | `sound/sb.inc` 735-950, 2540-2680, 3194-3320; `sound/picomem.inc` 586-630; `sound/sound.asm` 468-510; `usbmouse/usbmouse.asm` 380-470; `os88drv.inc` 255-320 | `sbl_isr`, `sbl_tick`, `sbl_hook`/`sbl_cell`, the discovery stubs, `pm_tick`, `opl_wrp`, `um_pm33` |
| packages | `apps/os88spk.inc` 1-140, 420-580; `apps/wireball/wblink.inc` 700-800; `apps/dos/dosw.inc` 1-120, 265-350; `apps/dos/dos.asm` 4470-4530 | the sample ISRs, WireBall's rate hook, `dvw_irq0`, AH=25h |
| contracts | SPEC.md §1 (52-140), §8 (12351-12486), 8.5 (13079-13180), 9.8.3 (16211-16260), 28 (46295-46330), 53.2.2 (106819-106920), 115 (225397-225520); `apps/os88api.inc` 2340-2350, 3965-3990, 6595-6612 | |

I also built an index of all 617 `cli`/`sti`/`pushf`/`popf`/`hlt` sites in `kernel/` outside
the mega files, grouped by enclosing procedure, and an index of every IMR read-modify-write
(`in al,21h/A1h` … `out`) in `kernel/` and `drivers/`. Both are behind the findings below.

## Verdict

The interrupt layer is careful and mostly correct. Its typical bug is no longer a
missing `cli`. It is now a **precondition that one ISR meets and another context does
not**. Most of the code assumes `SS = LOW_SEG`, `DF = 0`, a free mouse stack, or that
"nothing else runs during `int 13h`". The kernel's own ISRs establish those things or
test for them; the driver-facing and package-facing entry points do not always do so.
The real defects found are small and cheap to fix:

- the Pause key stops the clock on an XT (isr-1);
- the USB-mouse feed swaps SP but not SS (isr-2);
- DF is never cleared at the IRQ0 callback boundary (isr-4);
- a two-instruction tail update races WireBall's own rate hook (isr-5);
- a handful of IMR read-modify-writes run at IF=1 (isr-6).

The systemic gaps are bigger than those bugs:

- **There is no written interrupt-latency budget.** The longest IF=0 windows are now
  the compactor's copies (mem-2, mem-8, mem-11), not the "longest" one SPEC names.
- **No one owns the IRQ/IVT/IMR.** Six owners each write them their own way, and
  restores are blind.
- **The CPU exception vectors are left to the BIOS dummy handler.** A divide fault
  means silent garbage on an XT and a hang on a 286.

All three can be fixed without a large byte cost, and none needs an ABI break.

## Strengths

1. **Publication by a single store, applied consistently.** `task_spawn` publishes
   `T_STATE` last (sched.inc 915-1000). `sch_wake_ui` writes both wake bytes with one
   word store and says why tearing is impossible (sched.inc 696-724). `ui_post` is
   tested and cleared with one `xchg` (ui.inc 746). `gfx_lock` stores the flag and the
   owner as one word inside the window (vga12.inc 5118-5124). This is the right
   discipline for an 8086, where an interrupt can arrive only between instructions.
   It is worth naming as a §1 rule, because isr-5 is the same pattern broken in a
   package.
2. **Private ISR stacks that do not trust SS.** `MOUPRIV_ENTER` *loads* SS rather than
   testing it (mouse.inc 395-422). `sch_isr` moves the ROM chain onto `sch_chstack`
   only when SS is `LOW_SEG` (sched.inc 2202-2214). `sch_switch` refuses to park a
   foreign SP (2412-2425). The nested-tick resume (8.5.1) is handled correctly. Moving
   the chain off every task slice is real resident savings. OS/2 1.x had a kernel
   interrupt stack for the same reason; DOS and Windows 3.0 standard mode did not.
3. **The wake protocol survives drains.** The three-state `W_WKQ` (NONE/RING/EATEN,
   wm.inc 2337-2341) and coalescing make `OSAPI_WM_WAKE` safe from ISRs and workers
   and impossible to deafen. It is the Windows 3.x "PostMessage is the interrupt-safe
   call" model, plus a lost-record recovery that Windows never had.
4. **The ROM is treated as hostile but necessary.** The self-chain trap
   (`kbm_isr .selfchain`, mouse.inc 2955-2975, 3039-3046) is one example. Another is
   calling the keyboard ROM under an IRQ1-only mask, a fix for a race measured in the
   field (9.8.3, mouse.inc 3001-3031). A third is the per-arm EOI ownership comments
   (`sch_isr`'s sub-tick and skip EOIs, `mou_p2_isr` slave-then-master at 2245-2250,
   `sbl_isr`'s ISR-bit test for spurious IRQ 7).
5. **The halt guards are textbook.** `sch_idle_body` does `cli`/test/`sti`/`hlt`
   inside the `sti` shadow (sched.inc 754-791). `ui_task .idle` tests and blocks under
   one IF=0 window (ui.inc 1169-1185). Both are correct, and both say why.
6. **The sample ISR is genuinely minimal.** `os88spk_isr`'s per-sample path is about
   ten instructions with its own `cld` and its state reached through CS
   (os88spk.inc 459-480). The kernel's half is a thin door, and its teardown
   (`spk_off`) runs on every way out.
7. **Claims are instrumented.** `sch_chskip`, KFZ's heartbeat with the PIC
   IMR/ISR/IRR readings, and STKDIAG's chain water each turn an argument about an ISR
   into a reading. Few hobby kernels of the period had anything comparable.

## Findings (ranked)

### isr-1: Pause stops the clock on an XT (bug, low, now, about +14 bytes resident)

**Evidence.** `kbm_isr` calls the ROM's int 09h with only IRQ1 unmasked
(`or al, 0xFD`, mouse.inc 3022-3031). SPEC.md 9.8.3 keeps IRQ1 open on purpose
because "Ctrl+NumLock waits INSIDE int 09h for the next key to nest a second handler"
(16254-16257). For that whole wait, IRQ0 is masked.

The 8259 latches one IRQ0 edge, so a 10-second pause delivers one tick and loses
about 180:

- `[ticks]` loses them;
- the BIOS count at 40:6C loses them (it is chained from `sch_isr`);
- the time of day loses them (`clk_tick` runs off `[ticks]`).

A Sound Blaster stream's watchdog does not run either. SPEC's sentence "A masked edge
is latched in the IRR and taken at the restore, so nothing is lost" (16250) is true
for a 2,500-cycle handler and false for Pause.

Only the int-09h arm is affected, which on these builds is every XT. On a ROM with
int 15h 4Fh (`KBD_I15`), int 09h stays the ROM's and nothing is masked.

**Recommendation.** On the int-09h arm, before the mask, take one extra decision.
When Ctrl is down in 0040:0017 and port 60h holds 45h (the trigger byte for Pause on
both XT and AT ROMs), mask with `0xFC`, which leaves IRQ0 open. Peeking 60h is free
on the 8255 this arm runs on. The 9.8.3 race needs a handler that *returns*, and the
Pause handler does not return until the next key, so leaving the tick open cannot
reopen it. If the owner judges Pause not worth the bytes, change the SPEC sentence
instead (see the mismatches).

**Bytes.** About +14 `.text`, resident, on kern_big and kern_small (`t_hotpar` parity
to re-check).

### isr-2: `osapi_mouse_feed` is reached from an IRQ handler but swaps SP and not SS (hazard, medium-low, now, +8 resident)

**Evidence.** The feed's header reasons that "this is not an ISR, it is a plain task
call, and SS is LOW_SEG in every os8088 task context" (mouse.inc 3986-3990). It then
moves SP alone onto `mou_pstack` (4013-4016).

Its USB-mouse caller is the PicoMEM path. That path is "Reached from inside the
card's IRQ handler, IF = 0 and its EOI still to come" (usbmouse.asm 416-426), so the
interrupted context can be anything the card's IRQ lands on with IF=1:

- a ROM service on its own stack, which is SPEC.md 7.4's designed case for
  `MOUPRIV_ENTER`;
- the card's own option-ROM `int 13h`;
- a program's stack.

In those contexts the swap writes the feed's frames at `foreignSS:mou_pstack`, and
`mou_apply` then reads and writes `.lowbss` through SS. That covers `evq_push`'s ring
(events.inc 100-128) and the cursor's row table (MOUPRIV's own note, mouse.inc
416-418). The result is a lost event, a garbage arrow, and corrupted memory in the
segment the CPU happened to be standing in.

The same header is also stale on its own terms. It says "IT SWAPS UNCONDITIONALLY
where MOUPRIV_ENTER tests SS first", but MOUPRIV now *sets* SS without a test
(419-421).

**Recommendation.** Bank and load SS exactly as `MOUPRIV_ENTER` does:
`mov [mou_pss], ss` / `mov ss, [cs:mou_lowss]`, and `mov ss, [mou_pss]` on the way
out, all inside the existing `cli`. Then the feed is correct from any context, and
the header becomes true. The alternative is to make the driver switch first, as
`dvw_irq0` does (dosw.inc 267-276). That moves the rule into every future pointer
driver, so the kernel fix is better.

**Bytes.** About +8 `.text`, resident (kern_big, `OS88_DRIVERS`).

### isr-3: "ISR-safe" `OSAPI_WM_WAKE` has an unstated `SS = LOW_SEG` precondition (hazard, low, now, 0 or +4)

**Evidence.** `evq_push` addresses the ring as `push ss / pop es` and
`[ss:di+EV_TYPE]` (events.inc 100-128). `wm_wake` calls it (wm.inc 2343-2360).

The SDK says only "Any context - ISR-safe and worker-safe, no lock needed"
(os88api.inc 6605). A driver or package ISR that interrupts a ROM service running on
its own stack therefore writes the event into that ROM's segment. The one in-tree ISR
poster, `dvw_irq0`, moves onto a `LOW_SEG` slice *before* it posts (dosw.inc
267-276, 326-347), so the authors knew. The contract does not say it.

**Recommendation.** Say it in os88api.inc and SPEC.md 74.1: "from an ISR, only with
SS = LOW_SEG - an ISR that may have interrupted a ROM service's own stack switches to
a task stack first, as DOS.O88's `dvw_irq0` does". That costs 0 bytes. Optionally,
make `evq_push` independent of SS by loading ES from a `LOW_SEG` constant (the
`MOUPRIV` cell `mou_lowss` already exists) and dropping the `ss:` prefix. That is
about +4 resident bytes, and it removes the precondition for every future caller.

### isr-4: DF is never cleared at the IRQ0 callback boundary (hazard, low, now, +1 resident or 0)

**Evidence.** `sch_isr` saves everything and never executes `cld` (sched.inc
1892-2400; the only `cld`s in the file are at 988, 1634 (KFZ's `khb_paint`) and
2602). It then calls three things:

- the splash animation, `splf_anim` → `spl_anim` → `spl_spin` (sched.inc 2351-2353,
  splash.inc 1024-1029), whose row copy is `rep movsb`;
- the loaded driver's `DSV_TICK` (snd.inc 438-445);
- the package's `FSXF_RATE` hook (sched.inc 618-622).

`spl_isr` does `cld` for exactly this reason, and its comment claims that "sched.inc
says the same thing above its own in-ISR blit" (splash.inc 1208-1212). Only the KFZ
knob's `khb_paint` does.

Packages run with `std` at IF=1. `osapi_sys_snapshot_x` says so in as many words:
"apps/word, apps/sheet, apps/notepad and a dozen more" (instance.inc 2307-2310). The
kernel's own compactor copies backwards at IF=1 (memory.inc 1853). Neither the
`DSV_TICK` contract (os88drv.inc 270-271) nor §53.2.2's hook contract (106845-106873,
which pins IF, registers and the stack) mentions DF.

The splash case is close to theoretical, because no package code runs during the
load. The driver and hook cases are not: Tracker, Video and MIDIRack all run a rate
hook while their own foreground code is free to `std`.

**Recommendation.** Add one `cld` in `sch_isr` after `KDS`. `iret` gives the
interrupted code its own DF back, so the cost is 1 byte and 2 clocks per IRQ0, and
it covers every callee present and future. If `t_hotpar`'s parity makes that byte
dear, put the `cld` in `splf_anim` (the boot blob: 0 resident) and write
"DF is the interrupted code's: `cld` before any string op" into both contracts.
`fsx_rtick` (kern_mega's real-mode bracket, fsx.inc 1364-1446) has the same gap
and the same 1-byte fix.

### isr-5: WireBall's queue tail is updated in two instructions its own rate hook reads between (bug, low, now, +2 package)

**Evidence.** The foreground consumer does `inc byte [si+WP_QTAIL]` followed by
`and byte [si+WP_QTAIL], 63` (wblink.inc 779-780). The 582 Hz hook tests fullness
against that byte (`cmp ch, [si+WP_QTAIL]`, 733). Between the two instructions the
tail reads 64, which no head can equal.

If the ring is full at that instant (head 63, true tail 0), the hook judges it not
full and stores into the head slot. It then advances the head to 0, which equals the
tail, and 63 queued bytes now read as an empty ring.

The window is one instruction at 582 Hz with a full 63-entry ring, so it is rare,
and the symptom is a dropped link frame that the framer resynchronises. It is in the
reference package for fast games, though (CLAUDE.md names WireBall's queue as the
pattern to copy), so the pattern should be right.

**Recommendation.** `mov bl, [si+WP_QTAIL] / inc bl / and bl, 63 /
mov [si+WP_QTAIL], bl`, which publishes with one store, the house rule of
strength 1. That is about +2 bytes in WIREBALL.O88. Write the rule into SPEC §1 as
"an index an ISR reads is written by ONE store", and it becomes a review item rather
than a rediscovery.

### isr-6: IMR read-modify-writes at IF=1 on task context (hazard, low, now, +3 a site)

**Evidence.** Most IMR writes sit under `pushf/cli`: `srl_api` wraps all of
`srl_open`/`srl_close`, and `hbm_wake`, `usbmouse` and `mou_vset` are guarded. These
five are not:

- `mou_lockon` (mouse.inc 1253-1255), UI task, every contest settle;
- the post-DOS re-arm (1590-1594);
- `mou_p2_off`'s slave mask (2194-2196);
- `mouse_unhook`'s per-port mask (1417-1419), on the reboot path;
- SOUND.DRV's `sbl_cell` and `sbl_hook` (sb.inc 3235-3237, 3211-3213), at attach and
  tier change.

A task switch between the `in` and the `out` writes back a stale mask, undoing
whatever another task did to the IMR meanwhile. The only other-task IMR writer
plausible today is a DOS program in its window, which programs the 8259 freely on a
pre-empted worker. DOS.O88 exchanges the vectors (dvw_runs, dosw.inc 98-106) but not
the IMR. The result is a line masked or unmasked behind somebody's back, with no
error.

**Recommendation.** `pushf/cli … popf` round each. That is about +3 bytes a site:
roughly +9 resident (the three non-boot `.text` sites), +3 in SOUND.DRV's two, and
`mouse_unhook` optional. The structural answer is isr-13's mask shadow.

### isr-7: The cursor is drawn at IF=0 inside the serial ISR, which caps SERIAL.DRV's usable rate on an 8250 (hazard, medium, now: measure first)

**Evidence.** `mou_isr_body` scans the ports, routes the leased row to `srl_irq`, and
hands a mouse byte to `mou_byte` → `mou_apply`. On a completed packet, `mou_apply`
moves the arrow with `cur_move` at IF=0, before the EOI and the `iret`
(mouse.inc 3720-3774, 4212-4300).

The measured cost of a hide-and-show over the 8x12 arrow is about 1.94 ms on a 1bpp
adapter (PERFORMANCE.md Part 5, the `gfx_lock + gfx_unlock` row). VGA is not
measured.

The lease's default is 9600 baud (SPEC.md 115.1: "0 = 12, 9600 baud"), which is
1.04 ms a byte. An 8250 holds one byte in RBR while the next one shifts in, so an
IF=0 stretch of more than about two byte-times overruns it.

That stretch arises whichever row is scanned first:

- if the lease's row comes after the mouse's, its byte waits behind the draw
  (`input-3` is the unleased form of this);
- if it comes before, the *next* lease byte lands during the draw.

SPEC.md 115.2's "keeps mouse movement from stealing serial data" (225490) is true of
the row routing. It is not established for timing. This is very likely the longest
IF=0 window on a running desktop that is not compacting.

**Recommendation.** First measure. Run `tests/serlease.py` at divisor 12 with the
mouse moving, on `os8088_xt_vga` and a CGA twin, and count LSR overrun bits (byte 2's
sticky bits already report them). Then pick one of two:

- the cheap answer is a documented ceiling. Default the lease to 2400 (divisor 48,
  as WireFleet already asks) on a UART the boot probe identifies as an 8250 and not
  a 16550, and state it;
- the structural answer is the rethink below: send the EOI before the draw, and draw
  with IF=1 under `sch_lock` with a busy byte.

**Bytes.** 0 to measure; about +30 to +40 resident for the rethink.

### isr-8: CPU exceptions are left to the BIOS dummy handler (weakness, medium, now, about +30 resident, +12 `.ovl`)

**Evidence.** The shipping kernel installs no handler for int 0 (divide) or, on a
286+, int 6 (invalid opcode). `inst_wchk`'s note says so: "`div cl` FAULTS (int 0, no
handler installed)" (instance.inc 1976). Only the `BOOT_MARK` knob hooks
0, 1, 3, 4, 5, 6 and 7 (kernel.asm 7727-7758).

The IBM ROM's dummy handler returns, with different results by CPU:

- an 8088 pushes the address *after* the `DIV`, so the code carries on with AX/DX
  unchanged: silent garbage;
- a 286 or later pushes the faulting instruction, so the dummy `iret` re-executes it
  forever: a hard hang, with no report.

The kernel spends a great deal of care keeping divisors safe from hostile media
(SPEC.md 18.2's table, disk.inc 5026, 5155, 5269). Packages, including every C
package (cc8086 emits `div`), have no such net. `sch_stkdie`'s banner states the
project's own principle: "A halted machine is a bug report; a machine that carries on
is a bug that gets blamed on something else three subsystems away" (sched.inc
2601-2612).

**Recommendation.** Ship two 4-byte stubs, for int 0 and (when `cpu_tier` is 286 or
above) int 6, that set a code and jump into `sch_diepanel`'s machinery with the
faulting CS:IP from the frame. The text would read "DIVIDE FAULT 1234:5678". Never
touch int 5: on a PC it is Print Screen. Install them in `.ovl` at `sched_init`.

Nothing needs restoring: DOS.O88 banks IVT 00h for its programs (dosw.inc 98-100),
kern_dos's handoff takes the IVT whole, and int 19h's next kernel re-installs. Killing
only the offender (Windows 3.x's UAE) would need an unwind point that ui_task does
not have, so the panel is the right first step. It is what Mac System 5-7's bomb box
was.

### isr-9: `SCH_CHSTK` is sized without int 1Ch, and the mechanism SPEC gives is the wrong way round (hazard and doc, low, now; listed as sched-7)

**Evidence.** SPEC.md 8.5.1 and sched.inc 2187-2188 say the ROM's handler "sends its
EOI and then `sti`s before `int 1Ch`". The IBM BIOS listing does the opposite:
`TIMER_INT` begins with `STI`, calls `INT 1CH`, and sends the EOI after it. SeaBIOS's
`handle_08` likewise calls int 1Ch before `pic_eoi1`. GLaBIOS's order is not
verified here.

Two consequences follow:

1. IRQ1-7 cannot land in the chain until the ROM's EOI, because the PIC holds IRQ0 in
   service. The keystrokes and mouse gate frames that SPEC sizes the stack for can
   only arrive in the few instructions between that EOI and the ROM's `iret`.
2. Whatever POST left on int 1Ch, and on int 08h in front of the ROM, runs on
   `sch_chstack`. That covers option ROMs and anything chained there. It gets the
   128 − 56 = 72 bytes SPEC leaves, with no canary (sched-7), and below it in
   `.lowbss` sits `mou_pstack`'s top (mouse.inc is included before sched.inc,
   kernel.asm's include order).

`stkdiag.inc` 216 and 300 state the order correctly ("a real BIOS `sti`s before
int 1Ch"). The A/B measurements (36 to 56 bytes) were taken on bare BIOSes.

**Recommendation.** Correct the two texts (see the mismatches). Take sched-7's canary
on `sch_chstack`. Add a STKDIAG reading on a machine with option ROMs (an XT-IDE or
PicoMEM field machine) before anyone shrinks `SCH_CHSTK`.

### isr-10: SPEC names the wrong "longest IF=0 window" (doc, low, now, 0)

**Evidence.** SPEC.md 46310-46313 says `OSAPI_SYS_SNAPSHOT`'s 1.8-2.2 ms is "still
the longest IF=0 window in the system". Longer windows exist:

- `mem_regrow`'s path 3 copies the whole block inside its own `pushf/cli`
  (memory.inc 4559-4664), about 170 ms for a 64 KB canvas (mem-2);
- the driver-image move runs at IF=0 by design, "~50 ms" for ETHER's 18 KB
  (memory.inc 3080-3083; mem-11);
- `mem_avail_lvl_p`'s model is ESTIMATED at 8-15 ms (mem-8);
- `cur_move` inside `mou_isr` is about 1.94 ms (isr-7);
- `task_spawn`'s fill is about 0.7 ms (sched-1).

The snapshot's own "safe by about 3.5×" argument is also wrong for a leased 9600-baud
port: the next byte arrives 1.04 ms behind, not 8.3.

**Recommendation.** Correct the text, and give the system the budget isr-14 proposes.

### isr-11: DSV_TICK's environment is documented as the kernel's, and it is not (doc, low, now, 0)

**Evidence.** `sbl_tick`'s header says "IF = 0, DS = KERNEL_SEG" (sb.inc 2555).
`drv_svc_call_x`, which is how `snd_tick` reaches it, loads DS with the driver's
segment and ES with the kernel's (driver.inc 2542-2546), and `sbl_tick` reads its
own `[sbl_str_act]` DS-relative, correctly.

`os88drv.inc`'s `DSV_TICK` line (270-271) states only "INSIDE IRQ0, at IF=0". It says
nothing about DS, ES or DF (isr-4), that BP is clobbered (the selector rides in it,
driver.inc 2532-2536), or that a nested tick skips the call (sched.inc 2269-2276). A
third-party driver writer cannot write a correct tick from that line.

**Recommendation.** See the mismatches for the proposed text.

### isr-12: `gfx_lock`'s bare `cli`/`sti` is an unlisted exception to §1 rule 3 (consistency, low, now, 0)

**Evidence.** CLAUDE.md and SPEC §1 rule 3 say critical sections are
"`pushf`/`cli` … `popf`, never `cli` … `sti`". `gfx_lock` uses `cli` … `sti` and
documents "IF = 1 on return" (vga12.inc 5084-5127). `task_exit`'s unmatched `cli` is
documented the same way (sched.inc 1190), and so are `sch_rhook`'s `sti`
(sched.inc 598) and `sch_idle_body`'s `sti` inside the loop. Each exception is
argued in place. None is listed where the rule is stated, so the rule reads as
absolute and the tree visibly breaks it.

**Recommendation.** Add a short list to §1 rule 3 of the sanctioned bare `sti` sites
and why, and have `tests/unit/t_asmrules.py` fail a new bare `sti` in `kernel/` not in
a `tests/stisites.txt` registry. That is the `textsites.txt` ratchet's shape, at
0 bytes.

### isr-13 (rethink): one owner for IRQ lines, the IVT and the IMR (rethink, medium, now as an appended cell, flagday to make it mandatory; about +100 resident net; listed in part as ABI-BREAK-LIST L3)

**Today.** At least six owners write vectors and masks, each its own way:

- the kernel: `sched_init`/`sched_stop`, `mouse_init`/`mouse_unhook`, `kbm_isr`'s
  transient mask, `spk_off`;
- SOUND.DRV: `sbl_hook`/`sbl_unhook` and four discovery stubs, each with its own
  spurious-IRQ-7 test;
- USBMOUSE.DRV: int 33h plus a line;
- XMEM.DRV: int 15h, with an IMR bracket at xmem.asm 1036-1048;
- packages in brackets: IRQ0 through `os88spk`;
- DOS.O88: the whole table, exchanged.

The kernel's restores are blind. `mouse_unhook`, `sched_stop` and `spk_off` put back
what they saved without asking whether the vector is still theirs; only USBMOUSE
checks "if it is still ours". Hibernate has had three separate "the wake unmasked
the whole master" class bugs (hiber.inc 4825-4848, 4870-4950). EOI routing
(master-only vs slave-then-master), the spurious-line test and DS derivation are
re-implemented in every driver ISR.

**Proposal.** `OSAPI_IRQ` (append a cell, so no ABI break), with three verbs:

- HOOK: line, handler, owner instance or driver;
- UNHOOK;
- MASK and UNMASK through a kernel **IMR shadow** with per-owner bits. The kernel
  writes the port from the shadow and never does a read-modify-write.

The kernel owns the 16-row table (old vector, owner, slave flag), restores in LIFO
order, refuses an UNHOOK that is not the top of its line's chain, and emits the
correct EOI in a common stub. That stub can also do the spurious IRQ 7/15 check once,
for everyone. Drivers keep working unchanged. A flag day can then make the cell
mandatory, which is the "an IRQ is hooked through a cell" half of ABI-BREAK-LIST
**L3**: kern_mega's `mega_null0`/`mega_db` fix-ups exist because drivers hook through
segment 0.

**Bytes.** Roughly +80 `.bss` and about +120 `.text` for the table, verbs and shadow,
against the sites it retires: the kernel's own IMR sites (isr-6) and `mouse_unhook`'s
loop. Net about +100 resident on kern_big. Drivers shrink (SOUND.DRV's hook, cell and
stubs are about 60 bytes, not resident). kern_small needs only the shadow (about +25).

**Prior art.** OS/2 1.x `DevHlp_SetIRQ`/`UnSetIRQ`/`EOI` (the kernel owns the 8259);
Windows 3.x VPICD (`VPICD_Virtualize_IRQ`); AmigaOS `AddIntServer`/`SetIntVector`
with prioritised server chains. DOS had none, and its TSR-unload folklore ("unhook
only if the vector is still yours") is what USBMOUSE re-derived.

### isr-14 (rethink): a written interrupt-latency budget, and a ratchet that enforces it (rethink, medium, now, 0 shipped bytes)

**Today.** There is no number anywhere for how long IF may stay 0. Each window argues
its own case against "one mouse byte at 1200 baud" (8.3 ms), and the arguments
disagree: SPEC 28 uses 8.3 ms, mem-11 found 7.5 ms, and the lease needs 1.04 ms.
Meanwhile the copies that matter (mem-2, mem-11, mc-5) grew past 50 ms unremarked.

**Proposal.** Put one line in SPEC §1: "IF=0 for at most N µs, and never around a
copy whose length a caller chooses: a copy holds `[sch_lock]` (no switch) and runs at
IF=1". N should be about 500 µs, which keeps a 9600-baud 8250 safe with margin and
fits `sch_switch` (about 460 µs). The exceptions would be named: the ROM keyboard
handler, and the cursor until isr-7 is decided.

Add `tests/unit/t_ifwin.py` (fast, host-only). It statically walks every
`pushf/cli` … `popf` region in `kernel/` and `drivers/`, flags any `rep movs`/`stos`,
`loop` or call to a `mem_b*` copy inside one, and fails unless the site is in
`tests/ifwin.txt` with a reason. The list can only shrink.

**Prior art.** AmigaOS's guidance was to keep `Disable()` brief and use `Forbid()`
(a scheduler lock) for anything long. That is exactly os8088's `cli` vs `sch_lock`
pair, which exists here without the rule that tells you which to use. Mac's Inside
Macintosh carried a list of calls not to make at interrupt time, which is the same
idea for callers (see isr-16).

### isr-15 (rethink): `sch_lock` across `int 13h` is a whole-machine BIOS monitor (rethink, low-medium, record, about +40 to +80)

**Today.** Every `int 13h` holds `[sch_lock]`, so for one to two revolutions per call
(about 400 ms) no task switches: not the Sound Blaster's `TF_SERVICE` refill worker,
not a game's worker (disk.inc 1-14, 1462). The lock buys three things:

1. BIOS non-reentrancy, which a mutex would also give;
2. the pinned DMA buffer, which `mem_pinseg` already protects;
3. several invariants that lean on "no other task is running", for example the
   cursor-in-transfer gate's "dsk_xfer holds [sch_lock] throughout, so no other task
   is running to be in [an fprog fill]" (mouse.inc 4214-4224).

**Alternative.** A BIOS mutex taken by `dsk_xfer` and anything else that calls a
non-reentrant BIOS service. On AT-class ROMs, hook int 15h AH=90h ("device busy")
and AH=91h ("interrupt complete") to yield while the ROM waits for IRQ6.

**Prior art.** PC/GEOS on the 8088 had `SysLockBIOS`/`SysUnlockBIOS` for exactly this.
The AT BIOS's AH=90h/91h hooks were provided for multitasking OSes (OS/2 1.x used
them).

**Cost.** Each "nothing else runs" invariant must be re-proved. That migration is
the real cost, more than the bytes. Record it; take it only if SB underruns during
disk reads become a reported problem.

### isr-16: no single "callable at interrupt time" list (weakness, low, now, 0)

**Evidence.** ISR-safety is stated slot by slot (`OSAPI_WM_WAKE`'s "Any context"),
rule by rule (the rate hook may call "nothing in the kernel - but one verb",
§53.2.2), and in a worker's list (os88api.inc 3969-3978). There is no table a driver
or package author can check: which slots may be called from an ISR, from a rate
hook, or from `DSV_TICK`, and with what SS, DS, ES and DF.

**Recommendation.** Add a generated section to `docs/INDEX.md`, built from a
`; CTX:` tag on each slot in os88api.inc and kept by `tools/os88index.py`, the
existing generator. Prior art: Inside Macintosh's list of routines that "may move or
purge memory" and so cannot be called at interrupt time; Windows 3.1's short list of
interrupt-safe calls (`PostMessage`, `timeGetTime`, ...).

### isr-17: the SB driver leaves the master PIC reading its ISR (consistency, low, now, +4 SOUND.DRV per site)

**Evidence.** `sbl_isr`'s spurious-IRQ-7 test (sb.inc 939-943) and `sbl_stub7`
(3296-3299) select OCW3 0Bh and never select 0Ah again. The KFZ heartbeat calls RIS
"STICKY" and restores it (sched.inc 1947-1952), and every in-tree IRR reader selects
0Ah first (sched.inc 1297, bootprof.inc 96, hiber.inc 872), so nothing in-tree is
hurt. A DOS program or TSR under DOS.O88 that reads port 20h expecting the IRR is.

**Recommendation.** Two bytes of `mov al, 0x0A / out 0x20, al` at each site. Or write
"every IRR read selects 0Ah first" into SPEC §1 and leave the SB sites as they are.

## Rethinks (summary)

| id | what | phase | bytes |
|---|---|---|---|
| isr-13 | kernel-owned IRQ/IVT/IMR registry with an IMR shadow and LIFO-checked restore | now (append cell); flagday to mandate (ABI-BREAK-LIST L3) | about +100 resident net |
| isr-14 | written IF=0 budget plus a `t_ifwin.py` ratchet; long copies under `sch_lock` at IF=1 | now | 0 shipped |
| isr-7 R | cursor draw after the EOI: decode at IF=0, send the EOI, then `inc [sch_lock]`, a busy byte, `sti`, the redraw loop (repeat while the position moved), `cli`. A nested `mou_isr` while busy only decodes and records; it must not re-enter `MOUPRIV`'s fixed top (test the busy byte before the swap) | now, after measuring | +30 to +40 resident |
| isr-15 | BIOS mutex instead of `sch_lock` across `int 13h`; int 15h 90h/91h yield on ATs | record | +40 to +80 |
| isr-8 | exception stubs into the death panel | now | about +30 resident |

The Mac comparison is what makes isr-7 R worth having. System 1-7 drew the cursor in
the VBL task at 68000 interrupt level 1, and the SCC serial chip interrupted at
level 4, so cursor drawing could never cost a serial byte. The 8259 has priorities
but the kernel's own ISRs never re-open IF, so os8088 has one level. Sending the EOI
and doing `sti` before the draw recovers the Mac's ordering for the one ISR whose
body is long.

## Prior art (systemic)

| system | model | better than os8088 at | os8088 better at |
|---|---|---|---|
| **DOS 2-6** | no scheduler; InDOS and the critical-error flag; int 28h idle; ISRs chain vectors | nothing structural: it simply has no concurrency | everything here; DOS's TSR re-entry rules are what os8088's sch_lock-around-BIOS reinvents |
| **Windows 3.0/3.1** | VPICD owns the PIC and virtualises IRQs (enhanced mode); a short list of interrupt-safe calls (`PostMessage`); UAE on faults | a single IRQ owner (isr-13); a published interrupt-safe list (isr-16); fault reporting (isr-8) | wake coalescing with lost-record recovery; private ISR stacks on a 4.77 MHz part |
| **OS/2 1.x** | the kernel owns the 8259 (`DevHlp_SetIRQ`/`EOI`); interrupt handlers run after the kernel's EOI with interrupts on; context hooks defer work to task time; ProcBlock/ProcRun | EOI and nesting written once; priority re-enabling inside long handlers (isr-7 R); `ArmCtxHook` as a primitive where os8088 has ad-hoc `ui_post` bytes | the resident cost: OS/2 1.x needed a 286 and megabytes |
| **Mac System 5-7** | VBL tasks, the Deferred Task Manager, the Time Manager; 68000 interrupt priority levels; cursor drawn at interrupt time; the bomb box | cursor at a *low* interrupt level beneath serial (isr-7); the "do not call at interrupt time" list (isr-16); every fault produces a visible report (isr-8) | os8088's cursor gate (`cur_inxfer`, the FPG guard) is better reasoned than the Mac's, which corrupted drawing in known cases ("ObscureCursor" folklore) |
| **PC/GEOS (8088)** | pre-emptive threads; semaphores; `SysLockBIOS` | a BIOS lock that blocks only BIOS users (isr-15) | os8088 runs on 128 KB; GEOS needed 512 KB |
| **AmigaOS** | `Disable()`/`Enable()` and `Forbid()`/`Permit()`, both nestable counters; interrupt server chains; `Cause()` soft interrupts | the *rule* that long sections use `Forbid` (isr-14); prioritised IRQ chains (isr-13) | nothing missing in mechanism: `cli`/`sch_lock` is the same pair |

What can be adopted cheaply, in order: isr-14's rule and ratchet (0 bytes); isr-16's
list (0); isr-8's fault stubs (about 30); the IMR shadow alone from isr-13 (about 25)
before the full registry.

## Doc/code mismatches

1. **kernel/sched.inc:2187-2188.** Current: "The ROM sends its EOI" / "and then stis,
   so a second IRQ0 can re-enter". The IBM ROM does `STI` at its head and sends the
   EOI after `int 1Ch`; a nested IRQ0 can only land between that EOI and the ROM's
   `iret`. Proposed: "The ROM `sti`s at its head and sends its EOI at its end, after
   int 1Ch, so a second IRQ0 can re-enter this ISR between that EOI and the ROM's
   iret".
2. **SPEC.md:13125-13127 (§8.5.1).** Current: "inside the ROM's handler, which sends
   its EOI and then `sti`s before `int 1Ch`". Proposed: "inside the ROM's handler,
   which `sti`s at its head and sends its EOI at its end, after `int 1Ch`. Until
   that EOI the PIC holds every lower line off, so a keystroke's `int 09h`, a mouse
   gate frame or the floppy's completion can land only between the EOI and the
   ROM's `iret` - and `int 1Ch`'s handler, whatever POST left there, runs on this
   stack in full". The same paragraph's "exactly one window" also ignores
   `sch_rhook`'s `sti` under a sample ISR (sched.inc 596-598); add "and, on kern_big
   under a sample ISR, inside `sch_rhook`".
3. **SPEC.md:16250-16252 (§9.8.3).** Current: "A masked edge is latched in the IRR
   and taken at the restore" … "nothing is lost". It is false for Pause: IRQ0 stays
   masked for the whole pause (isr-1). Proposed, if isr-1 is not taken: "…so nothing
   is lost for a handler of its own length - except Pause, whose wait runs inside the
   handler with IRQ0 masked: the BIOS clock and `[ticks]` stop for as long as the
   machine is paused".
4. **SPEC.md:46310-46313 (§28).** Current: "still the longest IF=0 window in the".
   Code: `mem_regrow`'s path 3 copy (memory.inc 4559-4664), the driver-image move
   (memory.inc 3080-3083), `cur_move` in `mou_isr`. Proposed: "one of the longer
   routine IF=0 windows - the compactor's driver-image move (§66) and `mem_regrow`'s
   path 3 are far longer, and a mouse packet's arrow move is comparable - and safe
   against a 1200-baud mouse by about 3.5x, though not against a 9600-baud lease
   (§115)".
5. **kernel/splash.inc:1210-1212.** Current: "(SPEC.md 1 - sched.inc says the same
   thing above its own in-ISR blit)". `sch_isr`'s spinner call has no `cld`
   (sched.inc 2351-2353); only the KFZ `khb_paint` does (1634). Proposed: "(SPEC.md
   1. sch_isr's own call of this animation does NOT `cld` - isr-4 - so the `cld`
   belongs in splf_anim too)", or delete the parenthesis once isr-4's `cld` lands.
6. **kernel/mouse.inc:3986-3990.** Current: "IT SWAPS UNCONDITIONALLY where
   MOUPRIV_ENTER tests SS first" … "this is not an ISR, it is a plain task call, and
   SS is". `MOUPRIV_ENTER` sets SS without a test (407-408, 419-420), and the feed's
   PicoMEM caller is inside the card's IRQ handler (usbmouse.asm 416-426). Proposed,
   with isr-2's fix: "IT SWAPS SS AND SP, as MOUPRIV_ENTER does: USBMOUSE.DRV's
   PicoMEM path calls this from inside the card's IRQ handler, which may have
   interrupted a ROM service on a stack of its own".
7. **drivers/sound/sb.inc:2555.** Current: "in:       IF = 0, DS = KERNEL_SEG, behind
   the snd_live gate; AX/DX free". `drv_svc_call_x` sets DS to the driver's segment
   and ES to the kernel's (driver.inc 2542-2546). Proposed: "in:       IF = 0, DS =
   this image, ES = KERNEL_SEG, behind the snd_live gate; AX/DX/BP free; DF is the
   interrupted code's".
8. **drivers/os88drv.inc:270-271.** Current: "DSV_TICK    dw  near proc called from
   snd_tick - INSIDE IRQ0, at IF=0." Proposed: "near proc called from snd_tick -
   INSIDE IRQ0, at IF=0, DS = your segment, ES = KERNEL_SEG, BP clobbered, on the
   interrupted task's stack (or a ROM service's, which has room for little); DF is the
   interrupted code's, so `cld` before any string op. Skipped on a tick nested inside
   the ROM's chain. Keep it short…".
9. **SPEC.md:106845-106873 (§53.2.2, the hook's environment list).** It lists IF,
   registers, the stack and the kernel calls, but not DF. Add: "- **DF is the
   interrupted code's.** A hook that uses a string instruction issues `cld` first;
   `iret` gives the interrupted code its own back."
10. **apps/os88api.inc:6605-6606.** Current: "Any context - ISR-safe and worker-" /
    "safe, no lock needed". `evq_push` writes the ring through SS (events.inc
    100-128). Proposed: "Any context - worker-safe, and ISR-safe with SS = LOW_SEG
    (an ISR that may have interrupted a ROM service's own stack switches to a task
    stack first, as DOS.O88's dvw_irq0 does) - no lock needed".
11. **kernel/events.inc:80-81.** Current: "ISR-safe: the copy + index update sit
    inside pushf/cli ... popf, so a" … "caller running with IF=0 (the mouse ISR) stays
    that way throughout". Add: "- given SS = LOW_SEG, which the ring is addressed
    through: the mouse ISRs and the feed load it; any other ISR caller must too".

## Open questions

1. GLaBIOS's int 08h: does it EOI before or after int 1Ch? If before, isr-9's
   "lower IRQs land only after the EOI" is GLaBIOS-dependent. MartyPC can answer this
   in one breakpoint on the field BIOS.
2. Which POST-time hooks are actually on int 08h and int 1Ch on the field machines
   (pc5150 with PicoMEM, the Victory 286, the Compaq)? A one-line STKDIAG read of
   `sch_old08` and the IVT 1Ch cell on each would size isr-9.
3. isr-7: what does `cur_move` cost on VGA (four planes, save-under)? If it is well
   over 2 ms, the lease's 9600 default is unsafe on every 8250 machine with a VGA.
4. isr-2: does the PicoMEM's option-ROM `int 13h` run on a private stack? If it does,
   the USB mouse corrupts PicoMEM RAM during PicoMEM disk reads on the owner's own
   setup, and isr-2 rises to medium-high.
5. DOS.O88 windowed mode (outside this lens): a program's int 1Ch hook is in the
   exchanged range (dosw.inc 98-106, IVT 10h-3Fh), but `dvw_irq0` exchanges the world
   OUT before calling the kernel's int 08h (dosw.inc 276, 334). So the ROM calls the
   *desktop's* int 1Ch, and a windowed program's 1Ch handler never runs. Is that
   intended (SPEC.md 96.54)? Many DOS games time off 1Ch.
