# ksp14 agent "serial" - the serial lease, WireFleet's link and the mouse contest

Kernel size pass 14 (`docs/plans/completed/ksp14/BRIEF.md`). Concept: SPEC.md
115's desktop serial lease and what it put in the mouse code - `kernel/serial.inc`
(`osapi_serial`, `ser_api_x`, `ser_release_inst`, `ser_drop`, `SERFP`),
`mod.inc`'s `mod_f_serial` row, the `.mods` image (`SERIAL.DRV`), and
`mouse.inc`'s non-cursor, non-wheel hunks: the leased row in `mou_pout`,
`mou_isr_body` and `mouse_unhook`, SPEC.md 9.5.6's line-error gate
(`mou_errt`, `mou_claim`, the ISR's stamp) and KERN-MEGA-PLAN 14.39's
`MOUPRIV_ENTER`/`LEAVE` growth and its three parity pads. Commits `2edd2c29`
(#278), `901516f2` (#282), `60c46f08`, `f9ed8cc4`, `61b4054d`; `c704b42f` is
kern_mega's alone (`kbm_isr` under `KERN_MEGA`, HIBER.DRV) and was not touched.
Branch `ksp14-serial`, base `364d6f82` (kernel-size-14 after the menubar and
vesa merges). Figures are ASSEMBLED bytes, `tools/kernsize.py --json`
absolutes; resident = `.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf`.

## BASE AND TIP

| | base `364d6f82` | tip | delta |
|---|---:|---:|---:|
| **kern_big resident** | 92,645 | **92,602** | **-43** |
| kern_big `.text` / `.bss` / `.cold` / `.lowbss` / `.vgabuf` | 43,543 / 5,388 / 38,020 / 5,358 / 336 | 43,507 / 5,388 / 38,013 / 5,358 / 336 | -36 / 0 / -7 / 0 / 0 |
| kern_big `.boot2` / `.ovl` / `KERN_SIZE` | 6,107 / 5,118 / 98,816 | 6,107 / 5,118 / 98,816 | 0 |
| **kern_small resident** | 59,050 | **59,009** | **-41** (`.text` 31,137 -> 31,103, `.cold` 22,054 -> 22,047; no PS/2 ISR there) |
| kern_small `.boot2` / `.ovl` / `KERN_SIZE` | 4,381 / 1,486 / 61,440 | 4,381 / 1,486 / 61,440 | 0 |
| **kern_emu resident** | 92,916 | **92,873** | **-43** (`.text` 43,814 -> 43,778, `.cold` -7) |
| kern_emu `.boot2` / `KERN_SIZE` | 6,207 / 99,328 | 6,207 / 99,328 | 0 |
| **kern_mega resident** | 102,952 | **102,911** | **-41** (`.text` 47,225 -> 47,191, `.cold` 42,748 -> 42,741; `.mega` 14,698 unchanged) |
| `SERIAL.DRV` image (unpacked, both kernels) | 847 | 857 | +10 (1 KB claim either way, 167 to spare) |

Against the concept's ~+260 on kern_big that is 17%. No rung moved on any
kernel; `.bss`, `.lowbss`, `.boot2` and `.ovl` are untouched, and the stack
sizes with them (`mou_pstack` is vesa's 128 and nothing here deepens the ISR:
the leased path now pushes nothing at all).

## TAKEN

Bytes are kern_big / kern_small / kern_mega, from `tools/os88rdiff.py kernel
364d6f82 .` (`-D KERN_SMALL`; kern_mega from its own map). Clocks are 8088
(EA 6 direct, 9 `[bx+disp16]`; `jcc` 4 not taken, 16 taken).

### 1. The lease's resident half - `.text` -26, `.cold` -7 (all four kernels)

* **`osapi_serial` keeps only `inst_caller`** (31 -> 11). It refused a
  non-instance caller (`DH >= INST_MAX`, AX = 5) and turned DH into the record
  with `inst_ptr`, banking AX and DI round it - but DI is read only by
  `SERIAL.DRV`'s `srl_open` (the region checks), which runs in the module and
  can take the record itself. So the refusal moves into `ser_api_x`, first,
  where it was first before; `srl_open` computes `inst_tab + DH * I_RECSZ`
  (`mov al, dh / mov ah, I_RECSZ / mul ah / add ax, inst_tab / xchg ax, di`,
  +10 of module) just before the first `[di+...]`; and the kernel never touches
  DI, which `srl_api` already banks. `-20` of `.text`, `+10` of `.cold`.
* **`ser_api_x` restructured** (58 -> 46 with the refusal it gained). A held
  lease means the module is loaded - `ser_row` is set only by `srl_open` and
  cleared only by `srl_close`, and the module is dropped only by the call that
  ends a lease (no shed, no compaction drop: `mod_drop` is the only path and
  `MOD_SER` has no other caller) - so a held lease goes straight to the module,
  and only Open with none calls `mod_need` (whose `.already` arm is pure, so
  the old call on a loaded module did nothing). The no-lease answers read the
  `cmp al, 1` they share: Close CF = 0 AX = 0, any other verb `inc ax / stc`,
  and `mod_need`'s failure keeps its own CF = 1 under `mov ax, 8`.
* **`ser_tidy`, one far body for "drop SERIAL.DRV unless a lease is held"**,
  every register and the flags kept (17). `ser_api_x` falls into it after the
  module call, so the verb's AX/CF come back through it; `ser_release_inst`
  far-calls it after the module's release (and no longer needs the release's
  CF: a closed lease is `ser_row = FFFFh`, which `ser_tidy` reads).
  `ser_drop_x` and `ser_drop` (12) are gone.
* **`ser_release_inst` no longer banks AX or the flags** (23 -> 17). Both
  callers read neither: `inst_rel_rec` pops AX next and tail-jumps to
  `xm_release_rec`, whose contract is DI; `mouse_unhook` loads AX next and
  `mou_pall` writes the flags. (AX survives anyway: `srl_release` and
  `ser_tidy` keep it.)

### 2. The mouse ISR's leased row out of line, and `srl_pump` through SI - `.text` -2 (big, small, mega)

`cmp bx, [ser_row] / jne .mouse / push bx / call far [SERFP+SER_IRQ] / pop bx /
jmp short .next` is `cmp bx, [ser_row] / je mou_isr_ser`, the call and its
`jmp short` below the iret. `srl_pump` addresses the ring through SI instead of
BX (a rename: `srl_body` reloads BX after both of its calls and `srl_api` banks
SI), so `srl_irq` gives the scan index back untouched and the ISR stops banking
it. **Every live port that is not leased is 12 clocks faster per IRQ**
(`jne` taken -> `je` not taken); **the leased port is 15 faster** (`jne` not
taken + `push` + `pop` = 31 -> `je` taken 16).

### 3. `mou_eoi` under `mou_isr_body` - `.text` -4 / -4 / -2

`mou_isr_body` ended `jmp mou_eoi`, a near jump ~450 bytes on past `mou_byte`
and `mou_apply`. The epilogue now sits directly under the scan loop, which
falls into it: **15 clocks off every IRQ3/IRQ4 exit**, -3, and 14.39's two
parity pads (after the scan's `jmp` and after the `iret`) go for one pad where
`mou_eoi` stood. Against the base, `mou_byte`/`mou_apply` sit +14 (kern_mega
+16) and everything from `cursor_hide` on -10 (kern_mega -8): even on all
three arms, and `mou_p2_isr`'s `jmp mou_eoi` is the same near jump.

### 4. `mou_claim`'s line-error gate is one store - `.text` -1, and `mouse_unhook`'s pad -1 (all)

The gate zeroed the run and jumped out (`mov [bx+mou_run], ah / jmp short
.out`, 6). It stores -1 instead (5) and falls into `.count`, whose `inc` makes
the run 0 and whose compare against `[mou_need]` - 1 or `MOU_LOCKN`, never 0
(`mouse_init` writes one of the two to both rows, `mou_need` is born 1,0,1,0) -
leaves by `.out`. That makes `mouse_unhook`'s `db 0` pad (serial's five-byte
call, SPEC.md 115) redundant; nothing hot lies between the two
(`mou_newround`..`mouse_unhook` sit odd, as nothing pins them). **Cycles**: the
ungated path is the same instructions; the GATED path - a port with a framing
error in the last 36 ticks completing a packet while the contest is unsettled,
never a settled machine and never a real mouse at its own rate - is ~61 clocks
longer (33 -> 94).

### 5. `mou_p2_isr` asks OBF and AUX in one compare - `.text` -2 (big, mega) *(outside the concept: the PS/2 ISR, read on the way)*

`test al, 1 / jz .eoi / test al, 20h / jz .eoi` is `and al, 21h / cmp al, 21h /
jne .eoi`, kern_mega's own `kbm_isr` test. **4 clocks faster when an AUX byte is
there** (16 -> 12) and when a keyboard byte is (28 -> 24); 4 slower only on an
IRQ12 with an empty buffer, which SPEC.md 9.9.6 counts as a fault.

### Parity

`hotpar` green on kern_big and kern_small at every commit. Against the base
(`os88rdiff --all`, kern_big): `gfx_blit1_x`, `gfx_blit4`, the `font_run`
family and the `dsk_*` hot routines +0; `mou_p2_isr` -2; `kbm_isr`, `mou_isr`,
`mou_isr_body` -4; `mou_byte`/`mou_apply` +14; `cur_move`, `cur_move_1`,
`sch_isr`, `sch_switch`, `wm_paint_all`, `ui_task` -10. All even, every one
`same` but the routines changed above. `mou_eoi` is now at an EVEN address (it was odd) - an
epilogue with no branch in it, so nothing inside it moves badly.

## EQUIVALENCE

**A unicorn harness** (scratch: `serh.py`, not committed) loads each kernel's
`.text` and `.cold` from the base's and the tip's own assembly
(`os88rdiff --keep`) at `KERNEL_SEG` and `COLD_SEG`, and runs the real code from
a far call into the CELL at 0x049F (so `api_rx` too), stubbing only
`inst_caller` (DH = the case's), `mod_need` (pure `.already` while a lease is
held - the invariant above - else the case's CF), `mod_drop`, and the module's
three entries (the API's AX/CF and whether it closes the lease; the release
closing for FFh or its owner):

* **5,040 cell cases** - AL 0, 1, 2, 3, 4, 7Fh, FFh; DH 0, 5, 11, 12, FFh;
  `ser_row` FFFFh, 0, 2; `mod_need` CF 0/1; module AX 0/1/6, CF 0/1, closes or
  not; caller CF 0/1. AX, CF, BX CX DX SI DI BP DS ES SS SP, the final
  `ser_row`, and the ORDERED events (`mod_need` with its AL; the module call
  with AL, BX, CX, DH, SI, ES, DS; `mod_drop` with its AL): **identical**. The
  module's DI is the base's record and the tip's caller DI by design; the
  harness asserts the base's equals `inst_tab + DH * 32` on every call, which
  is exactly what `srl_open` now computes. ZF/SF/PF/AF differ in 576 cases and
  are no part of the contract (SPEC.md 115.1: AX/CF out).
* **90 release cases** - AL 0, 1, 5, 11, FFh; `ser_row` FFFFh, 0, 2; owner 0,
  1, 5; caller CF 0/1: every register, `ser_row` and the events identical; the
  flags differ (24 cases) - the change argued above.
* **Negative control**: the tip's `mov ax, 8` patched to 9 in the loaded image
  -> 72 cases differ.

`srl_pump`'s BX -> SI is a rename checked mechanically (no `bx` left in the
routine); the ISR and `mou_eoi` moves and `mou_p2_isr`'s fold are argued above
and run by the rows. `tools/stkbalance.py` over `serial.inc` and `mouse.inc`,
base against tip: the same six known tail-`jmp kret_*` paths, none new.

## REFUSED

* **Zeroing the leased row's `mou_bases` for the lease** (the ISR's `jz .next`
  arm would then carry the serial test, so a non-leased port pays nothing, and
  `mou_pout`'s six-byte test would go): ~-6 resident and ~19 clocks per port per
  IRQ, ESTIMATED - but `mou_bases` is published (`mou_dbg_blk`, SPEC.md 9.4.2)
  and `mou_lockon` would skip the row, so what the machine reports and does
  changes; see DEFECTS for the second.
* **`ser_row` as a byte** (-1): three rows read it as a word
  (`wirefleetlink`, `wirefleetnomouse`, `wirefleetsystem`), and an odd byte in
  `serial.inc` needs a pad.
* **`OSAPI_SERIAL` as an `RCXCELL` straight to a cold body**: `inst_caller` is
  `.text` with no shim (+4 for `cw_inst_caller`, +5 a far call, and every exit
  of the body then owes `pop dx`) - more than the 11 bytes it deletes.
* **SER_RELEASE folded into SER_API** as a verb: `SERFP` -4 and a header word,
  but `ser_release_inst` would have to bank DX and move AL into DH (+6).
* **Sharing `mou_p2_isr`'s 30-byte prologue with `mou_isr_body`**: any dispatch
  after the pushes is a test on the serial ISR's path.
* **`push ss` instead of `[mou_pss]`** in `MOUPRIV_ENTER`: two bytes on every
  interrupted task slice, against STACK-SLOTS-PLAN's whole point.
* **`mou_errt` as a byte table**: a 256-tick alias would gate a port ~14 s after
  an error.
* **`mou_p2_isr` beside `mou_eoi`** for a short jump: -1 net, for moving a
  pinned hot entry and two pads.
* **`push ax / mov al, MOD_x / call mod_need|drop / pop ax` as a helper with
  the id inline**: `.cold` may carry no data directive (`os88ovlchk`).
* **Module file names without `.DRV`**, composed by `mod_need`: ~40 bytes of
  strings against ~20 of code and a 13-byte buffer, ESTIMATED ~-7, and
  `tools/os88rom.py`/`os88mod.py` read those strings.
* `instance.inc`'s parity pad (serial's 3-byte call in `inst_rel_rec`) stays:
  no cycle-neutral odd byte was found between it and `ui_task`.

## WANTS THE OWNER

1. **A lease taken before the mouse contest settles loses its IRQ when the
   mouse then settles on the other line** (DEFECTS, by reading): the fix is
   `cmp bx, [ser_row] / je .next` in `mou_lockon`, **+6 resident on all four
   kernels**, ESTIMATED - and with it the question of what `srl_close` should
   restore on a port the contest retired while it was leased. **FIXED at the
   owner's word** - see the last section.

## WANTS AN ABI BREAK

None. The kernel <-> `SERIAL.DRV` interface is an on-demand module's (built
from the same assembly, checked by `os88mod.py`): `srl_api` now takes DH alone
and computes the record, and `srl_irq` keeps BX.

## CROSS-FILE

None found outside the concept's files that was not taken or refused above.

## ROWS RUN

Once, at the tip (`adf352a3`), after the last code commit, `--marty-jobs 1`:

| row | emulator | result |
|---|---|---|
| `wirefleetlink` | QEMU (two UART peers) | ok 105.0s |
| `wirefleetsystem` | MartyPC (original 8250 lease, forced cleanup) | ok 64.9s |
| `wirefleetnomouse` | MartyPC (SPEC.md 115.1.1: lease before the contest settles) | ok 14.7s |
| `mouerrq` | QEMU (one port, a BREAK; the closed list's awkward-mouse entry) + MartyPC | ok 10.8s |
| `mousess` | MartyPC (SPEC.md 9.10.1, the ISR on LOW_SEG) | ok 11.7s |
| `mouwheel` | MartyPC | ok 3.7s |
| `wheel` | MartyPC (a serial IntelliMouse) | ok 10.9s |
| `wheelps2` | QEMU (the PS/2 aux port) | ok 7.0s |
| `ps2mouse` (full-tier row, run alone) | QEMU | ok 9.8s |
| `buildmatrix` | host (the knob kernels - `NOMOUPRIV`, `MOUDIAG`, `KERN_SMALL` arms of the moved ISR code) | ok 264.7s |

Plus the fast tier at every commit (61/61, `hotpar` among them), `make small`,
`make emu` and `make mega` at the tip.

**Broken on purpose**: `srl_open`'s new record computation pointed one record
on (`add ax, inst_tab + I_RECSZ`) - `wirefleetnomouse` went red
(*"automatic search leased COM2 with the mouse settled did not happen"*), and
green again on the restored tree. No row reaches the IRQ-scan case where the
lease is COM1 and the mouse COM2 (the only order in which `srl_irq` keeping BX
matters); that is covered by construction - `srl_pump` names no BX at all.

## DEFECTS FOUND

* **`mou_lockon` masks a LEASED port's IRQ line** (by reading; not reproduced).
  SPEC.md 115.1.1 lets Open take a port before the contest settles. If a mouse
  then settles on the other port, `mou_lockon` retires every port but the
  mouse's: its `mou_pout` (IER = 0) skips the leased row, but the PIC mask
  below it does not - so when the two ports are on different lines the lease's
  IRQ is masked for the rest of the lease, and `SERIAL.DRV` is serviced only
  by `srl_pump` inside each Read/Write (16 bytes a call; an original 8250 holds
  one). `srl_close` then puts back the PIC bit and IER it saved at OPEN - the
  contest's unmasked, RX-armed state - re-arming a port the contest retired
  (harmless: `mou_byte` drops a settled machine's other ports, but each byte the
  far end sends is an IRQ and a scan). WANTS THE OWNER 1.

## FIXED AT THE OWNER'S WORD - a lease taken before the settle keeps its line (DEFECT / WANTS THE OWNER 1)

The owner's constraint: *"we just can't break mice, or serial"* - smallest
change, and nothing may change on a machine where no lease is held when the
mouse settles. Done on `ksp14-serial` after a fast-forward to
`kernel-size-14` at `e03cfb79`; the bytes below are against that tip.

**The repro row first: `tests/serlease.py`** (soak, `serlease`, 12 s
measured, MartyPC `os8088_5150_cga_gla`). MartyPC because it is the 8088
with both UARTs, a serial mouse on COM1 (IRQ4) and COM2 on its own line
(IRQ3), and its debug server's `inb` reads the 8259's mask and the UARTs'
IER/MCR without side effects. Nothing here is on the closed QEMU list: no
cross-wired card and no modem, and COM2 is empty. The row opens WireFleet,
puts the machine back into "no mouse has spoken" the way `wirefleetnomouse`
does, and sets COM2's IER and IRQ3 to what `mouse_init` leaves on a no-mouse
boot, because opening the package with the mouse has already retired it. On
the keyboard mouse it then:
- **A.** takes Link > Connect COM2 while the contest is unsettled;
- **B.** catches `mou_hotplug` in state 1 and checks that COM1's DTR/RTS are
  down (the positive control) and COM2's are up;
- **C.** moves the real mouse until the contest settles on COM1 and
  `mou_lockon` has run, then asserts: IRQ3 unmasked, COM2's IER armed, IRQ4
  unmasked, the lease still held, and the pointer moving;
- **D.** closes WireFleet and asserts COM2 is retired (IER 0, IRQ3 masked),
  IRQ4 is unmasked and the pointer still moves.

**Red on the base exactly as reported**: *C: the LEASED COM2 line IRQ3 masked
by the settle (IMR ac)*, *D: COM2 re-armed at close (IER 01)*, *D: IRQ3
unmasked at close (IMR a4)*.

**Owner's item 2 - the hot-plug reset cycle: it already skipped the leased
port, and nothing was changed.** `mou_hotplug` reaches the UARTs only through
`mou_pall`, which loops `mou_pout`, and `mou_pout` has tested
`cmp bx, [ser_row] / je .out` since #278. Leg B is the proof: on the base it
was already green, with COM1's MCR caught at DTR/RTS low and COM2's at 0Bh.
The `.have` path's raise uses the same `mou_pall`.

**The fix:**

| | kern_big | kern_small | kern_emu | kern_mega | `SERIAL.DRV` image |
|---|---:|---:|---:|---:|---:|
| `mou_lockon` skips `[ser_row]` | `.text` +6 (43,010 -> 43,016; resident 91,591) | +6 (30,790 -> 30,796; resident 58,137) | +6 (`.text` 43,272) | +6 (`.text` 46,773; `mou_lockon` 65 -> 71 in its map) | - |
| `srl_open` banks `[mou_seen]` as `srl_seen`; `srl_close` retires a port retired during the lease | - | - | - | - | 857 -> 909 (+52, 1 KB claim either way, 115 to spare) |

`.bss`, `.cold`, `.lowbss`, `.boot2`, `.ovl` and `KERN_SIZE` do not move on
any kernel. Every hot routine stays at its measured parity (`hotpar`), because
`mou_lockon`'s +6 is even and nothing hot sits between it and `mou_hotplug`.

* **`mou_lockon`**: `cmp bx, [ser_row] / je .next` after its `[mou_port]`
  test. With no lease `[ser_row]` is FFFFh and never equals a row, so a
  machine without a lease runs the same retirement with one more
  not-taken compare. That is once per session, on the UI task, not on a hot
  path.
* **`srl_close`** (module): if the lease was taken unsettled
  (`srl_seen = 0`) and `[mou_seen]` is set now, the contest retired this
  port while it was leased. Close then leaves it as `mou_lockon` leaves a
  loser: IER 0, and the PIC bit masked unless the line is `[mou_line]` or
  unknown. It does this by overwriting the banked `srl_ier` and `srl_pic`
  before the usual restore. A lease opened after the settle, or still
  unsettled at Close, restores exactly as before. `mou_lockon` and the lease
  verbs both run on the UI task, so nothing races.
* SPEC.md 9.5 (the loser bullet), 115.1.1 (a new paragraph: the settle
  DURING a lease) and 115.2 (Close's exception) say so.

**Broken on purpose**: `srl_close`'s `jne .put` made a `jmp` - `serlease`
red on D (*COM2 re-armed at close (IER 01)*, *IRQ3 unmasked at close*), green
again restored. The kernel half's red arm is the base run above.

**Rows run once, after the fix** (`--marty-jobs 1`), all ok:
`serlease` 11.8s (MartyPC), `mousess` 10.1s (MartyPC), `mouerrq` 8.6s (QEMU +
MartyPC), `mouwheel` 3.3s (MartyPC), `wheel` 14.4s (MartyPC), `wheelps2`
6.8s (QEMU), `ps2mouse` 10.2s (QEMU, the full-tier row alone),
`wirefleetlink` 105.3s (QEMU), `wirefleetsystem` 64.4s (MartyPC),
`wirefleetnomouse` 12.9s (MartyPC), `buildmatrix` 306.7s (host: the knob
arms of `mouse.inc`). Fast tier 61/61 with `hotpar`, `make small`, `make
emu`, `make mega`; `os88ovlchk` clean; `stkbalance` over `serial.inc` and
`mouse.inc` the same six known paths, none new.

