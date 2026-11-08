# Mouse, keyboard, serial lease, VMware pointer

Area key `input`. A blue-sky review of the tree as of branch `the-big-review`
(2026-11-08). Byte figures are from `build/kernel.map` (kern_big) unless they
are marked ESTIMATED.

## Scope read

| file | lines | what was read |
|---|---|---|
| `kernel/mouse.inc` | 6,699 | 1-4,420 (the whole input half: constants, port table, MOUPRIV, the boot-overlay probe, settle/lockon/hotplug/unhook, `osapi_mouse`, the PS/2 half, the keyboard mouse, `kbm_isr`/`kbm_i15`, the key-state map, the overrun guard, the serial ISR, `mou_byte`, `osapi_mouse_feed`, `mou_apply`) and 6,280-6,699 (state, the `'MO'` block and its asserts, `.bss`/`.lowbss`). The cursor half (4,420-6,280) was read only where it is the ISR's callee, because the gfx and video reviews cover it. |
| `kernel/mouproto.inc` | 91 | all |
| `kernel/moudiag.inc` | 759 | all |
| `kernel/kbddiag.inc` | 634 | all |
| `kernel/serial.inc` | 511 | all |
| `kernel/vmmouse.inc` | 374 | all |
| `kernel/vmmabi.inc` | 51 | all |
| `SPEC.md` §9 | 13,632-17,990 | the intro, 9.4, 9.4.1-9.4.4, 9.4.8, 9.5-9.5.3, 9.5.6, 9.6.4, 9.6.6, 9.6.1.1, 9.7.2, 9.8.2-9.8.3, 9.9.4, 9.10-9.10.1, 9.11 |
| `SPEC.md` §115 | 225,397-225,500 | all |
| context | - | `sched.inc` `task_yield` and `sch_isr` (1,805-2,420), `ui.inc` key poll (125-260), `events.inc` `evq_push`, `memory.inc` `mem_movable_x`, `apps/wirefleet/wflink.inc` (the one lease caller), `apps/os88api.inc` OSAPI_MOUSE / KEY_DOWN / SERIAL prose, the two-cycle review, ABI-BREAK-LIST, KERNEL-BYTE-QUEUE, MOUSE-BOOT-FREEZE-PLAN |

## Verdict

The input subsystem is one of the strongest parts of the kernel. It is
field-hardened, and almost every rule in it is there because of a named
machine. Every serial port decodes on its own, and the contest that picks
the mouse port takes evidence from several sources: the identify burst, a
run of clean packets, line errors and DSR/DCD. That is more robust than any
DOS-era mouse driver. Four backends end in one `mou_apply` and one
`mou_settle`. The ISRs run on a private stack that also survives a ROM's
stack hop. Hardware waits are timed in ticks rather than spin counts. The
32-bit backdoor stays out of the shipped kernels.

The weak spots are at the edges of the hardware model:

- **A 16550 whose FIFO is already on** is assumed to be an 8250 (input-1).
- **The tick can park a ROM keyboard handler with IRQ1 still in service** on
  the AT path that §9.8.3 left unmasked (input-2).
- **The serial ISR keeps reading ports it has retired**, so it takes bytes
  from foreign devices after the contest is settled (input-3).
- **The serial lease trusts its caller to pin the ring.** The ring has no
  reason to be in the caller's memory at all (input-4, R1).

Each of these fixes costs a handful of bytes, and none needs an ABI break.
The biggest byte lever is the keyboard mouse, about 456 resident bytes on
every machine (R2). It is a textbook on-demand module and is still resident.

The comments and SPEC have drifted in about thirty places. Mostly these are
names that were folded away (`mou_claim`), constants that moved
(`MOU_DRAINT` 9 → 18) and the §9 intro, which still describes the
pre-§9.5.2 ISR and the pre-kern_emu VMware arrangement.

## Strengths

1. **Per-port decoders and an evidence-weighted contest** (`mouse.inc`
   3,689-3,753, 3,789-3,935, 942-1,107). Phase, b0/b1 and run are kept per
   row. A modem's resync cannot break the mouse's run, and a lowercase
   letter's bit 5 cannot click while its device is losing. The identify
   burst is read in the one window where the kernel owns the port outright,
   with IER 0 and the IRQs masked. Line errors bar a port from the run
   (§9.5.6). No DOS driver weighed this much evidence. Keep it.
2. **One back end, one settle** (`mou_apply` 4,139; `MOU_SETTLE_STORES`
   1,162). The binding both-buttons fall-through and the
   `[blk_act]`/`[cur_shchk]`/`ui_post` stores exist exactly once, for serial,
   PS/2, driver and VMware alike. This is the pattern other subsystems
   should copy wherever two producers feed one state machine.
3. **Ticks, never spin counts, and every elapsed time is wrap-safe**
   (`MOU_P2TMO` 196-200, `mou_p2wr` 1,654-1,675, every `sub ax,[stamp]`).
   One constant means the same thing on a 4.77 MHz XT and on a 486.
4. **The private ISR stack sets SS as well as SP** (395-425, §9.10.1), and
   needs no register to do it. It took a SeaBIOS stack hop to find this,
   and the fix is general.
5. **The overrun guard predicts nothing** (`kbd_ovflow` 3,315-3,383). It
   winds the tail back on a full buffer whatever byte arrives, and its
   header says why every filter was a hole. The nested-beep hang is now
   impossible, not merely unlikely.
6. **The vectors are handed back carefully.** `kbm_isr`'s self-chain
   refusal (3,042-3,046), `mouse_unhook` restoring int 09h or int 15h by
   `[kbm_ivec]`, and the "8 = int 74h is ours" step byte together close
   §9.6.5's freeze class for good.
7. **`KBD_I15`, one vector decided once** (664-688, 3,079-3,107). It gets
   the scancode from the ROM's own read where an 8042 would lose a byte to
   a peek, and costs an XT nothing.
8. **The lifetime split of the probe** (504-583). The probe goes to
   `.ovl`/`.boot2`. Everything `mou_hotplug` and `mouse_unhook` can reach
   stays resident, each edge is named in `tests/ovlrefs.txt`, and the far
   doors go through one shared stub.
9. **The serial lease costs nothing resident beyond routing**
   (`serial.inc`). The module is loaded on Open and dropped on Close, and a
   leased row is routed through the existing ISR scan, so the cross-wired
   IRQ handling comes for free. Open's rules for an unsettled contest
   (§115.1.1) are well reasoned.
10. **`kern_emu` isolates the 386 protocol completely** (`vmmouse.inc`
    39-60). The shipped kernels assemble `VMM_POLL` to zero bytes.
11. **The published `'MO'` block is asserted by offset**
    (6,520-6,560). It is read on the one machine with no debugger and is
    treated as an ABI.
12. **`mouproto.inc` is shared as source with kern_dos.** It is the
    arithmetic and nothing else, which is CLAUDE.md's rule applied
    correctly.

## Findings

Ranked by value. Phase: **now** = no ABI break; **flagday** = needs one;
**record** = worth writing down, not recommended.

### input-1 - A 16550 with its FIFO already on is treated as an 8250 (bug, medium, now)

**Evidence.** `mouse_init` programs LCR, DLL, DLM, MCR and IER
(`mouse.inc` 767-800, 880) and flushes by reading RX, LSR and IIR (888-896).
It never writes FCR (base+2). The ISR takes **one** RX byte per port per
interrupt (3,720-3,753). §9.13's re-arm in `osapi_mouse` rewrites MCR and
IER only (1,585-1,590). SPEC.md 213,126 confirms that nothing in the tree
touches the FCR.

**Failure.** Take a warm boot after an OS or driver that enabled the FIFO.
Windows 9x's serial VxD does, and so can a DOS comm program run under
`DOS.O88` before §9.13 takes the port back. A cold reset clears FCR and a
BIOS POST usually does not.

- **Trigger level 14.** RDA fires only after 14 bytes, so the pointer lags
  about 117 ms at 1200 baud. After the hand stops, the backlog drains at one
  byte per four character times.
- **Trigger level 1.** Two bytes queued during any IF=0 hold longer than
  8.3 ms leave INTR asserted after the ISR's single read. An edge-triggered
  8259 never sees another edge, and the mouse is silent for the session.
  That is §9.5.2's stuck-line symptom by another road.

**Recommendation.**

1. Write FCR = 0 in `mouse_init` beside the LCR writes:
   `mov ax, 0x0200 / call mou_ov_pall`. The write is ignored on an 8250,
   whose base+2 is read-only IIR.
2. Write the same in §9.13's re-arm (`mov ax, 0x0200 / call mou_pout`).
3. Optionally, make the scan re-read the same row's LSR after `mou_byte`
   returns (`pop bx / jmp short .scan` with BX intact). Then no byte is ever
   left behind, whatever the UART.

The real case is the 286+ machines on the field list. The P4 of FIELD-NOTES
63 is exactly the kind of machine that warm-boots out of Windows.

**Bytes.** +6 `.ovl` (kern_big) and +6 `.boot2` (kern_small), 0 resident
for item 1; +6 `.text` on kern_big for item 2; +2 `.text` on both kernels
for item 3.

### input-2 - The tick can park a ROM int 09h with IRQ1 still in service, on the paths §9.8.3 left unmasked (hazard, medium, now)

**Evidence.** On a `KBD_I15` machine, kern_big on an AT ROM that advertises
4Fh, `kbm_isr` is not in the int 09h chain at all: the ROM's handler is
entered straight from IRQ1 (`mouse.inc` 664-688, 723-735). kern_mega keeps
a `jmp far` (3,034-3,039). `sch_isr` switches on **every** tick when the
lock, the chain flag and coop are clear (`sched.inc` 2,380-2,392,
"pre-emptive: switch every tick"). It does not ask whether the code it
interrupted is itself an interrupt handler.

The IBM AT int 09h does `sti` at entry and sends its EOI only at the end
(K26/K27). §9.8.3 (SPEC.md 16,220-16,258) exempts the int 15h path on the
grounds that an AT ROM disables the keyboard interface. That argument
covers a nested **second byte**. It does not cover a **park**, and §9.8.3
itself says IRQ1 "with the mouse's IRQ3/IRQ4 under it, stays in service for
as long as the handler is stretched or parked".

**Failure.** A key is pressed while the idle task runs, and `kbm_i15`'s
`sch_wake_ui` makes the UI task runnable. The next tick lands after the
ROM's `sti` and switches to the UI task. IRQ1 then stays in service for that
whole UI pass, which blocks IRQ2 through IRQ7:

- the serial mouse overruns;
- the PS/2 mouse (on the cascade) stalls;
- the key itself is stored late;
- a floppy `int 13h` in that pass waits for an IRQ6 the 8259 cannot deliver.
  The ROM times out after about 2 s, and with `[sch_lock]` held the parked
  task cannot run to end it. The user sees a disk error.

This is PREDICTED, not measured: Bochs and QEMU run SeaBIOS, which does not
`sti` in IRQ handlers. 86Box with an AMI or Phoenix AT BIOS is the
instrument.

**Recommendation.** Use one generic rule rather than a per-path mask:
`sch_isr` refuses a switch while any IRQ other than IRQ0 is in service.
Select the ISR with OCW3 `0Bh` at port 20h, read it, test `AL & 0FEh`, and
put the IRR selection back with `0Ah` (`khb_*` already does exactly this
sequence, at `sched.inc` 1,947-1,953). It covers the 4Fh path, kern_mega's
jump, and any driver ISR that does `sti`. Coordinate with the scheduler
review.

**Bytes.** About +14 `.text`, resident, on all kernels. Roughly 40 cycles
on the switch path only (18.2 Hz), ESTIMATED.

**Prior art.** AmigaOS Exec defers dispatch until the outermost interrupt
returns. OS/2 1.x dispatches only on the way back to task time. Every DOS
TSR that popped up first read the 8259's ISR for exactly this reason.

### input-3 - After the contest the ISR still reads retired and foreign UARTs and drops their bytes (bug, medium, now)

**Evidence.** `mou_isr_body` scans every row with a nonzero base on every
interrupt, forever (`mouse.inc` 3,720-3,753). `mou_lockon` deliberately
leaves the bases in place (1,215-1,260). `mou_byte` then drops any byte from
a row that is not `[mou_port]` (3,793-3,798, `jne .jbout`), after the RX
read has already consumed it.

**Failure.** Take a DOS program under `DOS.O88`, or any package, that drives
COM2 itself while the mouse is on COM1. A DOS null-modem game or a terminal
is the typical case: it hooked IRQ3 and polls or interrupts on COM2. Every
mouse IRQ4 scans row 2 too, and row 2 is scanned **after** row 0's
`mou_byte` → `cur_move`, which is the long part. So a COM2 byte that
arrives during the arrow move is read by the kernel and thrown away. At
9600 baud a byte lands roughly every millisecond, which is about one
`cur_move`, so moving the mouse costs that program bytes at a high rate.
The lease (§115) avoids this, but only for a program that uses the lease.

**Recommendation.** Once `[mou_seen]` is set, scan only `[mou_port]`'s row
and `[ser_row]`. The loser rows are IER 0 and cannot be why the line fired.
For example, after the `ser_row` test:
`cmp byte [mou_seen],0 / je .take / cmp bl,[mou_port] / jne .next`.
This also saves one `in` a row per IRQ on two-port machines. The
cross-wired case is unaffected, because the mouse's own row is still
scanned whichever line fired.

**Bytes.** About +11 `.text`, resident, on both kernels.

### input-4 - The serial lease trusts the caller to pin the region the ISR writes into (hazard, medium, now)

**Evidence.** `srl_open` checks that the caller's record is live, that
ES == `I_SPTR`, and that SI+517 ≤ `I_SIZE` (`serial.inc` 226-243). It never
looks at the region's `MC_RLOC`. It banks ES (272), and `srl_pump` writes
`[es:si+...]` from IRQ context (423-470). Every package must declare
`OS88_REGION_MOVABLE` (CLAUDE.md's ratchet). Pinning is the caller's
duty: WireFleet does it at `wflink.inc` 118-121, and SPEC.md 115 says
"must pin".

**Failure.** A caller that forgets the AX=0 pin, or opts back in while
leased, is moved by the next compaction. The ISR then keeps writing the old
paragraphs, which by then belong to another claim. That is silent
corruption, not "lost bytes".

**Listed as.** The two-cycle review (2026-11-01), its Video bullet "A
serial lease whose ring is inside the package's own region can lose bytes".
The new evidence is that the failure is a write into a foreign claim and
that nothing enforces the pin.

**Recommendation.** Either:

- (a) the module refuses Open with AX=5 when the region record's
  `MC_RLOC` ≠ 0 (kern_big only: kern_small has no compaction); or, better,
- (b) R1 below: move the ring out of the caller entirely.

**Bytes.** (a) about +20 in `SERIAL.DRV` (module) and 0 resident.

### input-5 - Shift+PrtSc runs the ROM's Print Screen inside `kbm_isr`'s mask, with IRQ0 off (hazard, low-medium, now)

**Evidence.** On the kern_big int 09h path and on kern_small, `kbm_isr`
calls the ROM with every IRQ but IRQ1 masked (`mouse.inc` 3,021-3,031,
§9.8.3). Nothing hooks int 05h; no code writes IVT 14h. SPEC.md 16,162
notes that the ROM's Print Screen path EOIs early. §9.8.3 lists only Pause
as a known long stay.

**Failure.** On an 83-key keyboard, PrtSc is Shift plus the keypad `*`, so
it is easy to press by accident. In a graphics mode the ROM walks 2,000
cells through int 10h AH=08h and int 17h. That is seconds with no printer,
or tens of seconds printing, all with the clock, the scheduler, the serial
mouse and the floppy masked. Ticks are lost, so the clock drifts, and the
mouse overruns.

**Recommendation.** Set the BIOS print-screen status byte at linear 0500h
to 1 in `mouse_init`. The IBM ROM's int 05h returns at once while that byte
reads "in progress", and DOS uses the same byte. SPEC.md 37,323 already
names 0500h as print-screen status, and the kernel's own 0580h table sits
beside it. The hibernate stage overwrites 0500h (`HS_IVT`), so the resume
must re-set it, or accept the gap.

An alternative is to hook int 05h to an existing `iret` and restore it in
`mouse_unhook`.

**Bytes.** 5 bytes of `.ovl`/`.boot2`, 0 resident. The int 05h route is
about +12, partly resident.

**Related, record.** While a `FSXF_RATE` sample ISR plays, each keystroke
now also delays IRQ0 by up to about 1,725 cycles (§9.8.3's measurement).
That is a few samples of jitter per key.

### input-6 - The `'MO'` block publishes the doctored 8042 command byte as "the BIOS's" (bug, low, now)

**Evidence.** `mou_p2cmd0` is the BIOS byte with bit 4 cleared and **bit 5
forced set** (`mouse.inc` 1,903-1,912). The raw byte, `mou_p2cmdr` (the BIOS
byte with bit 4 cleared), lies outside the span (6,464-6,467). The comment
(6,459) and SPEC.md 14,127-14,130 say the span holds the byte "as the
machine's own BIOS left it". MOUDIAG's `cmd` column has the same mismatch
(`moudiag.inc` `mdg_t7`).

**Failure.** Bit 5 is exactly §9.9.7's field defect, PC MODE on an AT
controller. A `tests/sysbench` reading from that kind of machine cannot show
how its BIOS left the bit.

**Recommendation.** Swap the two declarations, so the raw byte sits at +6
inside the asserted span and the doctored one below it. Rename them so that
`mou_p2_off` keeps writing the doctored one and `.fail` the raw one. The
layout asserts keep their shape.

**Bytes.** 0.

### input-7 - The server-side "settled" test in `srl_open` lags the ISR by one UI pass (bug, low, now)

**Evidence.** `srl_open` treats the contest as settled only at
`[mou_hpst] == 2` (`serial.inc` 265-268). `[mou_seen]`, `[mou_port]` and
`[mou_ptr]` are set in the ISR (`mouse.inc` 3,911-3,916). `hpst = 2` only
arrives at `mou_hotplug`'s `.have` on the next pass (1,371-1,379).

**Failure.** Inside that window, a user-named Open (BL bit 7), or one where
the other port identified, takes the port the mouse has just won. The ISR
then routes that row to `SERIAL.DRV`, and the mouse is dead until Close.
This contradicts §115.1, "a winning serial mouse's port is never eligible".

**Recommendation.** Test `[mou_seen]` instead
(`cmp byte [mou_seen],0 / je .unsettled`). The settled arm already compares
`[mou_port]`.

**Bytes.** 0, module only.

### input-8 - `KBDDIAG=1` drops §9.8.3's IMR bracket, so the diagnostic build re-creates the defect it is for (bug, low, now)

**Evidence.** The `KBD_DIAG` arm calls the ROM with every IRQ open
(`mouse.inc` 2,978-2,996). The shipped arm masks everything but IRQ1
(3,021-3,031). `kbddiag.inc` 24-25 still says "the one thing that changes
is that kbm_isr CALLS the ROM rather than jumping to it". That stopped
being true when the shipped kernel started calling the ROM too.

**Failure.** A field kernel built to diagnose keyboard faults lets a tick
run inside the ROM's handler again. That is §9.8.3's split modifier, on
exactly the machine it is aimed at.

**Recommendation.** Put the same `in 21h / push / or FDh / out ... pop /
out` bracket around the diagnostic call, then fix the header.

**Bytes.** About +8 on the knob build, 0 shipped.

### input-9 - `mou_p2_off` writes the 8042 on the UI task with nothing excluding int 09h (hazard, low, now)

**Evidence.** `mou_p2_off` sends 0A7h, then the command-byte pair (60h, then
the value) (`mouse.inc` 2,189-2,203; `mou_p2wcmd` 1,753-1,765). It runs with
IF=1 and IRQ1 live. `mou_p2_init` masks IRQ1 for exactly this class of race
(1,848-1,863).

**Failure.** An AT ROM's int 09h writes 0ADh and 0AEh to port 64h. If IRQ1
lands between the two halves of `mou_p2wcmd`, the controller command is
replaced, and the value byte then goes to port 60h with nothing pending.
The 8042 forwards it to the **keyboard**, and the command byte is never
restored. This is reached only when a serial mouse beats a live PS/2 mouse
(`mou_lockon` 1,261-1,268), and at `mouse_unhook`.

**Recommendation.** Bank and mask IRQ1 at the master around the two writes,
as the probe does. Do not use `pushf/cli`: `mou_p2wr`'s timeout counts
ticks.

**Bytes.** About +12 `.text`, resident, kern_big.

### input-10 - PS/2 sync rests on byte 0's bit 3 alone (weakness, low, now)

**Evidence.** `mou_p2_byte` re-syncs only by refusing a header without bit 3
(`mouse.inc` 2,283-2,293). The probe flushes the buffer after F4 has turned
reporting on (2,066-2,069), so a packet that is half in flight is cut.

**Failure.** A misaligned stream takes b1 or b2 as a header whenever bit 3
happens to be set. In the meantime the low bits read as buttons (phantom
clicks) and the deltas are garbage. A hot-replugged device's `AA 00` is
taken as a header.

**Recommendation.** Reset `[mou_p2ph]` to 0 when `[ticks]` has moved by 2 or
more since the last aux byte. A packet's bytes arrive within a few
milliseconds; packets are at least 10 ms apart. This needs one stamp word.

**Bytes.** About +12 `.text` and +2 `.bss`, kern_big.

**Prior art.** Linux psmouse's lost-sync timeout; Windows i8042prt's
per-packet timer.

### input-11 - The hot-plug poller drops DTR on a modem port every 3 s for the whole session (hazard, low, now)

**Evidence.** While `[mou_seen]` is 0, `mou_hotplug` power-cycles every
unleased live port through `mou_pall` (`mouse.inc` 1,335-1,342 and
1,354-1,370), stopping only on an identify or a PS/2 answer. §115.1.1 rule 4
already treats DSR/DCD as "not a mouse" (`serial.inc` 342-352).

**Failure.** On a mouseless XT with a modem, each drop is a hang-up under
`&D2`. On an os8088 null-modem peer, DSR and DCD flicker every 3 s.

**Recommendation.** Skip the drop on a port whose MSR shows DSR or DCD, the
lease's own rule. That makes `mou_pall`'s drop per port in `mou_hotplug`
only.

**Bytes.** About +14 `.text`, resident.

### input-12 - The keyboard mouse is about 456 resident bytes on every machine and qualifies as a module (size, medium, now)

Rethink R2 has the detail. Its own header calls it "a FIRST CANDIDATE TO
REMOVE". A module is the middle road between keeping it resident and
removing it.

### input-13 - Keys are dispatched before the clicks queued in the same pass (rethink, medium, record)

Rethink R3 has the detail. This is the only systemic ordering gap in the
area.

### input-14 - A settled serial mouse never gets a reset edge again (weakness, low, record)

**Evidence.** `[mou_hpst] = 2` is terminal (`mouse.inc` 1,298-1,299), and
9.6.1.1 accepts "unplugging the mouse does not bring the keyboard mouse
back".

**Failure.** §9.4 opens with the wedge: a mouse hot-plugged onto live
DTR/RTS stays wedged until it is power-cycled. After a settle that is now
true again, for the rest of the session, after a bumped cable.

**Recommendation.** Record it. If it is ever wanted, add a Control Panel
verb or a key combination that runs one drop-hold-raise on `[mou_port]`
alone. The machinery exists; only the trigger is missing.

**Bytes.** About +20, in the panel module.

### input-15 - `mou_b0`/`mou_b1` live in the high bytes of other tables' words (weakness, low, record)

**Evidence.** `mou_b0` is `mou_phase+1` and `mou_b1` is `mou_masks+1`
(`mouse.inc` 6,291-6,302), with "READ AS BYTES ONLY" as the only guard
(316-317). A future `mov cx,[bx+mou_masks]` reads a packet byte and
assembles cleanly. This is the kind of invariant CLAUDE.md wants held by a
gate rather than by prose.

**Recommendation.** Keep the 4 bytes, but add a `tests/unit` grep that
refuses word reads of `mou_masks` and `mou_phase`, or spend the 4 bytes.

**Bytes.** 0, or +4.

### input-16 - `mou_claim` is gone, and twenty-two references remain (consistency, low, now)

**Evidence.** It was folded into `mou_byte`'s `.pkt` arm in kernel size
pass 17 (`mouse.inc` 1,184-1,186). Ten kernel comments (`mouse.inc` 40,
1,115, 1,153, 1,250, 1,385, 1,491, 3,738, 6,408, 6,430; `hiber.inc` 3,233)
and twelve SPEC.md lines (14,710, 14,751, 14,758, 14,829, 14,865, 14,889,
14,903, 14,927, 15,335, 15,568, 17,050, 17,602) still name it as live code.

**Recommendation.** Write "`mou_byte`'s `.pkt` arm (`mou_claim` until size
pass 17)" once in §9.5, and name `MOU_SETTLE_STORES`/`mou_settle` where a
line means the stores. A `checkdocs` rule for retired labels named in
backticks would stop the next one.

**Bytes.** 0.

### input-17 - The two diagnostic knobs carry two copies of one formatter (consistency, low, record)

**Evidence.** `mdg_tpl`/`mdg_line`/`mdg_h16`/`mdg_h8`/`mdg_h1`
(`moudiag.inc`) and `kbg_tpl`/`kbg_line`/`kbg_h*` (`kbddiag.inc`) are the
same code. The knobs are mutually exclusive (`kbddiag.inc` 31-33), so
shipped bytes are 0. The cost is maintenance only: `mdg_*` learned the
"ES is not ours" hand loops, while `kbg_show` sets ES itself.

**Recommendation.** Move one copy into a `diagfmt.inc` the next time either
knob is touched.

**Bytes.** 0 shipped.

### input-18 - The serial ISR's frame is a full nine-register save for every byte (perf, low, record)

**Evidence.** `mouse.inc` 3,689-3,701 and 3,758-3,774. The frame costs
roughly 650 cycles per byte, about 136 µs, which is about 1.6 % of an XT
while the hand moves at 1,200 baud (ESTIMATED from Part 2's tables). Phase-1
and phase-2 bytes need only AX, BX, DX and DS.

**Recommendation.** A light path for those two phases would save about
0.25 % of the machine. That does not pay for its bytes; record it so nobody
re-derives the number.

## Rethinks

### R1 - The serial ring belongs to `SERIAL.DRV`, not to the caller (now, no ABI break)

The only caller never reads the ring directly: WireFleet touches it only
through verbs 2 and 3 (`wf_serialbuf` is named once, at `wflink.inc` 127).
Every buffer rule in §115.1 exists only because the 517 bytes are in the
caller's region:

- the pin;
- the region-size validation;
- "the kernel and module own these bytes while leased";
- input-4's hazard.

Put the 517 bytes in the module's own image instead. A module claim is
pinned for its life, and it lives exactly as long as the lease.

- Open ignores SI. Old binaries still pass it, so the change is compatible.
- `srl_open` loses its region checks (about −30 module bytes).
- The package region gets 517 bytes smaller.
- The pin clause in §115 and in the `os88api.inc` prose becomes advice.

**Bytes.** 0 resident. Module RAM +517 while leased, −517 in the caller.
The file grows by a few bytes, since the zeros compress under LZ4.

Prior art: Windows 3.x `OpenComm(cbInQueue, cbOutQueue)` allocates the
queues in the driver's own fixed memory.

### R2 - The keyboard mouse as an on-demand module (now)

**Measured** on `build/kernel.map`:

| part | bytes |
|---|---|
| `kbm_tab`..`kbm_isr`, less the shared `mou_clamp` | 306 |
| `kbm_p5spend` | 16 |
| `kbm_pollm`..`kbm_ui` | 108 |
| `kbm_post` | 9 |
| state, `kbm_p5`..`kbm_ev` | 17 |
| **total** | **about 456** |

These bytes are resident on kern_big and kern_small alike, and on most
machines a mouse retires them seconds after boot (§9.6.1.1).

The feature meets CLAUDE.md's module test. It is needed only when the user
presses an arrow on a pointerless desktop, and the system disk is in the
drive at that moment on every machine that has no mouse.

- **What stays resident:** `kbm_key`'s first two gates (`[mou_ptr]`,
  ScrollLock), the keypad-5 latch in `KBM_SCAN`, `kbm_shf`, and thunks for
  `kbm_key`'s claim path, `kbm_poll`/`kbm_pollm`, `kbm_ui` and
  `kbm_p5spend`.
- **What moves:** the table, `kbm_btn`, `kbm_rclick`, `kbm_move` (which
  already far-calls `menu_kbnav`), `kbm_ui`'s body and the state.
- **When it drops:** when `[mou_ptr]` is set. `t_moddrop` wants that drop on
  the day the module lands.

**Bytes.** About −300 to −380 resident on both kernels, against about +450
module bytes (ESTIMATED; thunks and the far calls into `cur_move`/`menu_kbnav`
need costing). The first press costs a disk read, which is about 400 ms on
an XT.

This matters most on the 128 KB machine.

### R3 - One ordered input stream (record; a partial fix is now)

Today keys come from the BIOS buffer at the top of each `ui_task` pass and
go to `wm_top` (`ui.inc` 134-136 and 205-227). Clicks come from the event
ring afterwards (254-256).

**Failure.** "Click a window, then type" that lands in one pass sends the
keys to the window that was in front before the click. On an XT a pass that
repaints lasts hundreds of milliseconds, so this is a real window, not a
theoretical one.

Every peer keeps keyboard and mouse in **one time-ordered stream**:

- Windows 3.x's system queue;
- the Mac Event Manager;
- GEOS's Input Manager;
- AmigaOS `input.device`.

That is why type-ahead follows focus changes on all of them.

**A cheap partial fix.** `kbm_isr` already calls the ROM and gets control
back on most paths. It could compare the BIOS tail before and after, and
push an ordering marker into the event ring when a key was stored. `ui_task`
would then take a key from int 16h only when it pops a marker. The 4Fh path
and kern_mega do not return, so they would keep today's behaviour.

**Cost.** About +30 resident, plus an `EVT_*` code. An event code is an ABI
addition, not a break, if it is appended. Measure how often this bites
before building it: modal loops already eat keys.

### R4 - PS/2 through the BIOS's pointing-device services (record)

DOS mouse drivers drove the aux port through int 15h AH=C2h on any BIOS
that offered it: Microsoft's for the PS/2 port, Logitech's, and later
CuteMouse. The BIOS knows its own 8042, its §9.9.7-style quirks and its SMM
USB legacy emulation.

os8088 talks to the 8042 directly, and §9.9.1-9.9.7 is the record of what
that has cost. With C2h:

- the resident 251 bytes (`mou_p2wr`..`mou_p2_byte`) would shrink to a far
  callback that decodes three stacked words, plus C200h to retire;
- the probe would become C205h and C200h.

The trade-off is that the BIOS's int 74h handler runs on the interrupted
stack, which §9.10 moved away from, and there is one far call per packet.
It is worth a measured A/B on the P4/USB-converter machine of FIELD-NOTES 63
before anything else is changed in §9.9.

**Bytes.** Perhaps −100 to −150 resident (ESTIMATED).

## Prior art (systemic)

- **Mouse detection.**
  - Microsoft's MOUSE.COM and Windows 3.x's mouse drivers probe once at
    load. Their test is a DTR toggle and an `'M'` within about 200 ms.
  - The 1995 Plug and Play COM spec adds a PnP string after `'('`.
  - os8088 goes further than any of them: an identify plus a packet contest
    plus line-error gating plus a hot-plug poller. Its `MOU_IDMAX` of 128
    accommodates PnP strings.
  - What they did better: none of them power-cycled a port they could not
    identify, ever (input-11).
- **Input ordering.** See R3. Every peer has one ordered queue; os8088 has
  three sources: the BIOS key buffer, the event ring, and the position and
  wheel levels.
- **Key state.**
  - Mac `GetKeys` is a 128-bit KeyMap, the same shape as `kbd_dnmap`.
  - Windows has `GetAsyncKeyState`.
  - Both are always maintained. os8088 arms on the first question, which
    keeps a mouse machine from reading port 60h at all. That is better.
- **Keyboard mouse.** Mac System 6's Easy Access "Mouse Keys" used the
  keypad, with 5 to click, 0 to lock the button and `.` to release it.
  §9.6.1's latch converges on the same design. Windows had nothing built in
  until the Access Pack.
- **Interrupt time versus task time.** See input-2. AmigaOS, OS/2 and
  well-behaved DOS TSRs never reschedule out of an un-EOI'd hardware
  interrupt. os8088 masks one entry path instead of stating the rule.
- **Serial ownership.**
  - Windows 3.x `COMM.DRV` and OS/2 `COM.SYS` own the port exclusively and
    keep their queues in driver memory.
  - os8088's lease is leaner: nothing resident, and the module lives only
    while leased. Its buffer placement is the one thing worse (R1).
  - Neither OS let the mouse driver read a UART it had not claimed
    (input-3).

## Doc/code mismatches

Each entry gives the current text exactly, then what the code does, then the
proposed text.

1. **`kernel/mouse.inc:24`**
   - **Text:** "mouse_x/mouse_y themselves range over the full 0..639 / 0..479."
   - **Code:** `mou_clamp` (2,737-2,779) bounds them to
     `[vid_wm1]`/`[vid_hm1]`, or to the union of both displays on a
     two-display kern_big.
   - **Proposed:** "mouse_x/mouse_y range over the live screen,
     0..[vid_wm1] / 0..[vid_hm1] - or both displays' union on a two-display
     kern_big (mou_clamp, SPEC.md 39.15.4)."

2. **`kernel/mouse.inc:315-316`**
   - **Text:** "disagrees with its base address is out of scope here, as it was before."
   - **Code:** every hooked line services every live port (3,720-3,753), and
     `mou_lockon` masks by the line in `[mou_line]` (1,245-1,256). Only a
     lone card jumpered to the other line is unhandled (SPEC.md 9.5.2).
   - **Proposed:** "A card whose jumper disagrees with its base is handled
     where both rows are live - any hooked line services every port (SPEC.md
     9.5.2, 9.5.2.1). A lone card jumpered to the other line is not: that
     line is never hooked."

3. **`kernel/mouse.inc:1294`**
   - **Text:** "- up to half a second, self-healing on the next packet -"
   - **Code:** `MOU_DRAINT equ 18`, about 1.0 s (line 95).
   - **Proposed:** "- up to MOU_DRAINT (~1 s), self-healing on the next packet -"

4. **`kernel/mouse.inc:1250`**
   - **Text:** "mou_claim sets it with [mou_seen]"
   - **Code:** `MOU_SETTLE_STORES` (1,162-1,173) sets both.
   - **Proposed:** "MOU_SETTLE_STORES sets it with [mou_seen]"

5. **`kernel/mouse.inc:2407`**, together with **2425-2426**
   - **Text:** "520 bytes of .text, which is two 512-byte steps of
     KERN_BUDGET" and "docs/KERNEL-MEMORY.md carries the same note beside the
     numbers."
   - **Code:** `build/kernel.map` gives about 456 bytes (R2's figures), and
     `docs/KERNEL-MEMORY.md` has no such note (grep).
   - **Proposed:** "About 456 bytes of .text on kern_big (build/kernel.map,
     2026-11-08: kbm_tab..kbm_isr less mou_clamp, kbm_p5spend, kbm_pollm..kbm_ui,
     kbm_post and 17 bytes of state). Bytes, not rungs: the step history that
     stood here is in git." Drop the KERNEL-MEMORY.md sentence, or add the note
     there.

6. **`kernel/mouse.inc:2716-2717`**
   - **Text:** "CX and DX SURVIVE - mou_isr still has the staged button byte in DX"
   - **Code:** the buttons travel in CL (`mouproto.inc` out; `mouse.inc`
     4,174 `mov bl, cl`). DX is spent by `MOU_DECODE_MS`.
   - **Proposed:** "CX and DX SURVIVE - mou_apply still has the buttons in
     CL, and kbm_move its step in DL."

7. **`kernel/mouse.inc:2798-2803`**, and the same at **SPEC.md:15154-15160**
   - **Text:** "This is the only place os8088 touches the keyboard hardware"
     ... "Nothing here writes a port, queues a key or ends an interrupt."
   - **Code:**
     - `kbm_isr` writes the master IMR around the ROM call (3,021-3,031);
     - it sends an EOI on `.selfchain` (3,042-3,046);
     - the PS/2 half writes the same 8042 (1,872 and following, 2,196-2,200);
     - on `KBD_I15` machines `kbm_isr` is not hooked at all.
   - **Proposed:** "kbm_isr is the kernel's only int 09h hook (the PS/2 half
     reaches the same 8042 through 64h, SPEC.md 9.9). It reads port 60h
     without acknowledging it and chains; what it writes is the master
     8259's mask around the ROM's handler (SPEC.md 9.8.3) and, on the
     self-chain refusal alone, an EOI. It queues no key."

8. **`kernel/mouse.inc:3986-3988`**
   - **Text:** "IT SWAPS UNCONDITIONALLY where MOUPRIV_ENTER tests SS first, and that is ten bytes of the difference."
   - **Code:** `MOUPRIV_ENTER` banks and loads SS unconditionally, with no
     compare (405-425; SPEC.md 9.10.1 "There is no compare any more").
   - **Proposed:** "IT SWAPS SP ALONE where MOUPRIV_ENTER banks and loads SS
     as well (SPEC.md 9.10.1). That half exists because a ROM service the
     kernel calls with IF=1 may have switched stacks under an ISR; this is a
     plain task call, and SS is LOW_SEG in every os8088 task (SPEC.md 1)."

9. **`kernel/mouse.inc:2199-2200`**
   - **Text:** "...and the command byte back as the BIOS had it, IRQ12 bit and all"
   - **Code:** `mou_p2cmd0` has bit 5 forced set and bit 4 cleared (1,909-1,912).
   - **Proposed:** "...and the command byte as the BIOS had it, IRQ12 bit and
     all, with bit 5 (the aux clock) held off"

10. **`kernel/mouse.inc:6282-6283`**
    - **Text:** "the odd byte of each pair is padding and costs 4 bytes in all"
    - **Code:** `mou_b0 equ mou_phase + 1` (6,291) lives in those odd bytes.
    - **Proposed:** "the odd byte of each mou_phase pair holds that row's
      mou_b0 (below); mou_run's and mou_need's odd bytes are idle"

11. **`kernel/mouse.inc:6408`**
    - **Text:** "came in on, banked by mou_claim beside"
    - **Code:** `MOU_SETTLE_STORES`.
    - **Proposed:** "came in on, banked by MOU_SETTLE_STORES beside"

12. **`kernel/mouse.inc:6430-6431`**
    - **Text:** "Set beside [mou_seen] in mou_claim and in mou_p2_byte at its settle"
    - **Code:** `MOU_SETTLE_STORES`, reached from `mou_byte.pkt`,
      `mou_p2_byte` and `osapi_mouse_feed`; `vmm_boot_x` keeps its own copy.
    - **Proposed:** "Set beside [mou_seen] by MOU_SETTLE_STORES (every
      backend's settle) and by vmm_boot_x's copy"

13. **`kernel/mouse.inc:6456`**
    - **Text:** "packet state: 0 idle, 1 got b0, 2 got b1"
    - **Code:** phase 3 means a wheel Z byte is owed (2,316-2,327).
    - **Proposed:** "packet state: 0 idle, 1 got b0, 2 got b1, 3 a wheel
      mouse's Z byte owed (SPEC.md 9.9.8)"

14. **`kernel/vmmouse.inc:255-257`**, and **`kernel/sched.inc:1819-1820`**
    - **Text:** "Used by ui_task's pass and by menu_drop," and "is for ui_task and menu_drop"
    - **Code:** the only `VMM_POLL` site is `ui.inc:256`. `menu.inc` has none.
    - **Proposed:** "Used by ui_task's pass, which already holds DS =
      KERNEL_SEG" and "is for ui_task, which already holds the kernel's"

15. **`kernel/vmmouse.inc:160`**
    - **Text:** "It is safe to borrow because it is PROVABLY IDLE while [vmm_on] is set:"
    - **Code:** a §115 lease can re-arm a UART line on kern_emu too, and then
      `mou_isr` runs. What makes the borrow safe is IF=0 from ENTER to LEAVE
      (205-219), as in both ISRs.
    - **Proposed:** "It is safe to borrow because nothing can be on it at the
      same time: vmm_poll holds IF = 0 from MOUPRIV_ENTER to MOUPRIV_LEAVE,
      and both mouse ISRs run IF = 0 from gate to iret (SPEC.md 9.10).
      After vmm_boot_x's mou_lockon neither ISR normally fires at all, but a
      serial lease (SPEC.md 115) can re-arm a line, and the IF = 0 argument
      covers that too:"

16. **`kernel/kbddiag.inc:24-25`**
    - **Text:** "the one thing that changes is that kbm_isr CALLS the ROM rather than jumping to it."
    - **Code:** the shipped `kbm_isr` calls the ROM too (`mouse.inc`
      3,021-3,031). The diagnostic arm differs by NOT masking (2,978-2,996).
      input-8 fixes the code.
    - **Proposed:** "the one thing that changes is that kbg_pre and kbg_post
      bracket the call."

17. **`SPEC.md:13650-13653`**
    - **Text:** "(§9.10)" and "`kern_big` only, behind `[cpu_tier] == CPU_386`"
    - **Code:** the backdoor is §9.11, and it is `kern_emu` only
      (`vmmouse.inc` 39-60, `%ifdef KERN_EMU`), in `VMMOUSE.DRV`.
    - **Proposed:** "... on I/O port `0x5658` (§9.11), which is how the
      browser build (v86) gets a grabless absolute pointer. `kern_emu` only
      (§9.11.7), and in `VMMOUSE.DRV` rather than the kernel: the protocol is
      32-bit, behind `[cpu_tier] == CPU_386` in `vmm_boot_x`."

18. **`SPEC.md:13660-13668`**
    - **Text:** "`mou_isr` (IRQ4) and `mou_isr3` (IRQ3) differ only in the port row each hands the shared body in BX"
    - **Code:** the two doors differ in the line bit they stamp into
      `[mou_lfired]` (3,667-3,686). The body scans every live port and reads
      RX only where LSR says a byte is ready (3,720-3,753).
    - **Proposed:** "`mou_isr` (IRQ4) and `mou_isr3` (IRQ3) differ only in
      the 8259 bit each stamps into `[mou_lfired]` (§9.5.2.1). The shared
      body switches to its own stack (§9.10), saves the registers, loads DS
      and scans EVERY live port: an LSR read each, and the RX read - which is
      what clears that UART's interrupt - wherever a byte is waiting, handed
      to `mou_byte` with the port row in BX (§9.5.2). A leased row goes to
      `SERIAL.DRV` first (§115.2)."

19. **`SPEC.md:13706-13708`**
    - **Text:** "Two 16-row × 16-bit tables" and "The tables are 16×16"
    - **Code:** one byte per row, `CUR_GH` rows (`mouse.inc` 239-244,
      `CUR_TABSZ`).
    - **Proposed:** "Two tables of `CUR_GH` = 12 one-byte rows (the pictures
      are written as 16-bit words and `CUR_ROW` keeps the top byte); the
      arrow is `CUR_GW`×`CUR_GH` = 8×12, and that is what every walk is
      bounded by"

20. **`SPEC.md:13728`**
    - **Text:** "guard their critical sections with cli/sti"
    - **Code:** the guard is `pushf/cli` … `popf` (`mouse.inc` 17-20; a hard
      rule in CLAUDE.md).
    - **Proposed:** "guard their critical sections with pushf/cli … popf"

21. **`SPEC.md:13734-13736`**
    - **Text:** "(`kern_big` only, §9.10)"
    - **Code:** `kern_emu` only, and the section is §9.11.3.
    - **Proposed:** "(`kern_emu` only, §9.11.3)"

22. **`SPEC.md:13754`**
    - **Text:** "(`[mou_seen]`, set by the ISR at every third byte and never cleared)"
    - **Code:** it is set when a port completes `[mou_need]` clean packets in
      a row, or when another backend settles (3,902-3,916; `mou_settle`).
    - **Proposed:** "(`[mou_seen]`, set when a port completes `[mou_need]`
      clean packets in a row (§9.5) or another backend settles the contest
      (§9.5.1.1), and never cleared)"

23. **`SPEC.md:14545-14549`**, together with **14563-14566**
    - **Text:** "`MOU_DRAINT` = 9" / "(≥46 ticks" / "`MOU_DRAINT` ticks (~0.5s)" / "stays 9 as the ceiling"
    - **Code:** `MOU_DRAINT equ 18`, about 1.0 s (`mouse.inc` 95), raised
      with `MOU_IDMAX` = 128 for FIELD-NOTES 44's 69-byte PnP burst.
    - **Proposed:** "inside `MOU_DRAINT` = 18 of the raise, so the quiet test
      (≥37 ticks against `MOU_DRAINQ` = 3)", "`MOU_DRAINT` ticks (~1s)", and
      "the fixed drain ate 505 ms when `MOU_DRAINT` was 9; it is 18 (~1 s)
      since FIELD-NOTES 44's 69-byte PnP burst"

24. **`SPEC.md:14839-14841`**
    - **Text:** "**`[mou_lfired]` is stamped after `mov ds, ax` and not before.**"
    - **Code:** it is stamped first, through CS and with no register, before
      `MOUPRIV_ENTER` (3,667-3,686).
    - **Proposed:** "**`[mou_lfired]` is stamped FIRST, through CS and with no
      register** (`mov byte [cs:mou_lfired], MOU_L4`), so the private-stack
      swap after it is one copy for both doors (§9.10)."

25. **`SPEC.md:14829`**, and the other §9 lines naming `mou_claim` (14,710,
    14,751, 14,758, 14,865, 14,889, 14,903, 14,927, 15,335, 15,568, 17,050)
    - **Text:** "`mou_claim` banks that into `[mou_line]`"
    - **Code:** `mou_claim` was folded into `mou_byte`'s `.pkt` arm in size
      pass 17 (`mouse.inc` 1,184); the stores are `MOU_SETTLE_STORES`.
    - **Proposed:** "`MOU_SETTLE_STORES` banks that into `[mou_line]`", and
      elsewhere "`mou_byte`'s `.pkt` arm (`mou_claim` until kernel size
      pass 17)".

26. **`SPEC.md:14861`**
    - **Text:** "unchanged at two compares"
    - **Code:** one compare: state 2 is terminal (`mouse.inc` 1,298-1,299).
    - **Proposed:** "one compare (`[mou_hpst]` = 2 is terminal)"

27. **`SPEC.md:15602-15604`**
    - **Text:** "proves itself — the serial mouse's identify, or the PS/2 aux port"
    - **Code:** the identify sets only `[mou_ident]` and `[mou_idany]`
      (1,083-1,086). `[mou_ptr]` is set by `MOU_SETTLE_STORES` (1,171) and
      by `vmm_boot_x`.
    - **Proposed:** "proves itself — a serial port settling the contest
      (§9.5), the PS/2 aux port's first packet, a pointer driver's first
      report (§9.12) or the VMware backdoor (§9.11)"

28. **`SPEC.md:16891-16892`**
    - **Text:** "`mou_eoi`'s single `iret` and `mou_p2_isr`'s carry the restore."
    - **Code:** `mou_p2_isr` sends the slave's EOI and then
      `jmp mou_eoi` (2,256-2,262).
    - **Proposed:** "`mou_eoi` carries the one restore: `mou_p2_isr` sends
      the slave's EOI and joins it."

29. **`SPEC.md:14873, 14910, 14962, 15600`**
    - **Text:** "### 9.5.6", "### 9.5.4", "### 9.5.5", "### 9.6.1.1"
    - **Code:** their siblings are `####`, and the order runs 9.5.6 before
      9.5.4, and 9.6.4/9.6.5/9.6.6 before 9.6.1.1.
    - **Proposed:** demote them to `####` and put them in numeric order, or
      renumber them.

30. **`docs/plans/MOUSE-BOOT-FREEZE-PLAN.md:11-17`**
    - **Text:** "base word `[mou_hpbase]` at the first frame" and "`MOU_DRAINT` = 9 ticks after the raise"
    - **Code:** there is one stamp, `[mou_hpt]` (SPEC.md 9.4.8 "One stamp,
      not three"; no `mou_hpbase` exists), and `MOU_DRAINT` = 18.
    - **Proposed:** "`kmain` stamps `[mou_hpt]` at the first frame (the one
      word the poller measures from, SPEC.md 9.4.8; `[mou_hpst]` = 3 carries
      the 'poller fired' reading)" and "`MOU_DRAINT` = 18 ticks (9 when
      measured)".

## Open questions

1. **input-2.** Does an AMI or Phoenix AT BIOS under 86Box reproduce the
   park? The recipe: hold IRQ1 in service across a UI pass with a floppy
   read in it. `tools/os88box.py faults` and a stopwatch on `[ticks]` would
   tell.
2. **input-1.** Does any field machine leave the FIFO on across a warm
   boot? A `tests/comscan`-style probe can read IIR bits 7:6 (FIFO enabled)
   without writing FCR.
3. **R3.** How often does click-then-type actually land in one pass on an
   XT? A counter in `ui_task` would say so in a session.
4. **R4.** Does BIOS int 15h C2h work on the field P4 with the USB→PS/2
   converter where the direct 8042 path is marginal?
