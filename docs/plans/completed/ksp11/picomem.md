# Kernel size pass 11 - agent "picomem": notes

Concept: the PicoMEM work - merges `eb1be371` (the card's NE2000 on its real
memory map, SPEC.md 72.2; SOUND.DRV's PicoMEM IRQ handling, 34.10.1) and
`a3818217` (the PicoMEM tier in SOUND.DRV by default, the USB mouse 9.12.7,
B: from the card 18.97.6, Restart warm-resets 18.100.1). Files:
`kernel/disk.inc` (my hunks only: `dsk_fdd_park_x`, `dsk_fdd_pmemu`, its call
in `dsk_fdd_probe`), `drivers/sound/{picomem,sb}.inc`, `drivers/sound/sound.asm`,
`drivers/ether/ne2000.inc`, `drivers/usbmouse/usbmouse.asm`.

Branch `ksp11-picomem`, cut from `elendilon` at `2a05e31f`.

## Base `2a05e31f`

| | kern_big | kern_small |
|---|---:|---:|
| `.text` / `.bss` / `.cold` | 43,428 / 5,190 / 37,943 | 32,022 / 3,073 / 23,479 |
| `.lowbss` / `.vgabuf` | 5,598 / 336 | 2,868 / 0 |
| **resident** | **92,495** | **61,442** |
| `.ovl` / `.ovlw` | 2,471 / 5,086 | 2,076 / 1,480 |

`kern_dos` (`build/kerndos.bin`, a part of `DOS.O88`): 33,243 bytes.

Drivers - UNPACKED image + bss, which is what `drv_load` claims (rounded up to
whole KB, `mem_bytes_kb_x`):

| driver | image | bss | image+bss | claim |
|---|---:|---:|---:|---:|
| SOUND.DRV | 6,946 | 176 | 7,122 | 7 KB |
| ETHER.DRV | 16,400 | 496 | 16,896 | 17 KB |
| USBMOUSE.DRV | 1,410 | 0 | 1,410 | 2 KB |

What the concept cost them (assembled, `nasm` on the trees named): SOUND.DRV
6,637 without the PicoMEM tier (`NOPICOMEM=1`) -> 7,122 with it, **+485**, all
of it attach code and its state, resident for as long as the driver is
mounted; ETHER.DRV 16,778 (`e002a0e9`) -> 16,896, +118; USBMOUSE.DRV 1,151 ->
1,410, +259.

The kernel half: `.cold` +40 on both kernels is the warm reset at the head of
`dsk_fdd_park_x` (this is where "Restart warm-resets" lives - kernel, not a
driver); `.ovlw` +85 on kern_big is `dsk_fdd_pmemu` and its call. **On
kern_small the PicoMEM costs `.ovlw` nothing**: `dsk_fdd_pmemu` is
`%ifdef KERN_BIG`, so KERN-SMALL-CUT-PLAN 7's FAT-window floor (the cap that
binds kern_small's `.ovlw`) is untouched by this concept. On kern_big the
`.ovlw` ceiling is the FAT window plus `dsk_secbuf`, 5,120 bytes by sector:
the +85 took kern_big's payload spare from 119 to 34.

## TAKEN

### Batch 1 - the kernel half (`kernel/disk.inc`, `tools/stkbalance.py`)

kern_big `.cold -5`, `.ovlw -11` (resident 92,495 -> 92,490); kern_small
`.cold -5` (61,442 -> 61,437); kern_dos 33,243 -> 33,183 (-60).

* **`dsk_fdd_park_x`'s warm reset, -5 on both kernels.** CX is 0 when the
  ramp's `loop` runs out, which is the only way onto the warm-reset path, so
  `mov es, cx` with `[es:0x472]` replaces `mov ax, 0x40 / mov es, ax` with
  `[es:0x72]` (-3) - the same byte, 0040:0072h. The `popf / cli` pair that
  only existed to balance the stack for the walker before a jump that never
  comes back is gone (-2): IF is still 0 from the ramp's `cli` to the reset
  vector, and the machine state at `FFFF:0000` is identical but for SP, which
  POST does not read. 40 -> 35 bytes.
* **`tools/stkbalance.py`: a far jump to a LITERAL address ends the path**
  (`FARLIT`). `jmp 0xFFFF:0x0000` leaves every routine the walk can see, so
  nothing in the corpus will ever pop what it leaves; a far jump to a LABEL
  (`jmp KERNEL_SEG:sched_unhook`) is still a tail call and still walked.
  `tests/unit/t_stkbalance.py` carries both halves - the QUIET case (the warm
  reset's shape, which the old walker reported) and a LOUD one (a far jump to
  a label at depth must still be caught). It is the only literal far jump in
  `kernel/`, `apps/`, `drivers/`, `boot/` and `kerndos/`.
* **`dsk_fdd_park_x` is not assembled into kern_dos** (`%ifndef KD_BUILD`),
  -60 bytes of the DOS program. kern_dos includes `disk.inc` whole, and
  `kd_leave` parks with its own loop (`kerndos/kdentry.inc`); nothing there
  calls this one - `sched_unhook`, its only caller, is not in that root. NOT
  the concept's bytes alone: 20 of the 60 are the park's, the other 40 the
  PicoMEM's.
* **`dsk_fdd_pmemu` saves nothing; its caller already has, -11 `.ovlw` on
  kern_big.** The call moves in `dsk_fdd_probe` from before the probe's five
  pushes to after them, the yes path leaving through a new `.pops` (the
  probe's own pop tail, after the DOR restore, which the PicoMEM path never
  touched and still does not). So BX, CX, DX and ES are banked once instead of
  twice; AL comes back = the unit on every no path, which is all `.fdc` reads
  (BX, CX, DX, ES are each written before use below it - checked line by
  line). `test byte [es:bx+0x40B2]` reaches the attribute 16KB up without
  `add bx, 0x400`, and the `test` leaves CF = 0 for the yes exit. 77 + 8 = 85
  -> 64 + 10 = 74. The published state is unchanged on both paths: the
  PicoMEM path still sets no `fdd_dbg_ran` bit (the call sits above that
  store) and now writes `[fdd_unit]`, which only the probe reads. Returns
  kern_big's window-half spare from 34 to 45 bytes.

Proved: `tests/picomem.py` (the `kern` leg cuts `dsk_fdd_pmemu` and
`dsk_fdd_park_x` out of `disk.inc` as they ship and runs them under unicorn
against a model of the card - unit 0/1 served or not, Restart warm-resets with
1234h at 0040:0072 and FFFF:0000, the no-card machine parks both units and
int 19h's) all ok. No emulator here has a PicoMEM, so the probe's new call
site is proved by reading plus `fddpark` on MartyPC (the no-card path).

### Batch 2 - the drivers (`drivers/sound/{picomem,sb}.inc`, `ne2000.inc`, `usbmouse.asm`)

No kernel byte. Unpacked image + bss (what `drv_load` claims), and the
shipped LZ4 file:

| driver | base | tip | delta | claim | file (lz4) |
|---|---:|---:|---:|---:|---:|
| SOUND.DRV | 7,122 | 6,972 | **-150** | 7 KB -> 7 KB | 6,064 -> 5,950 |
| ETHER.DRV | 16,896 | 16,889 | -7 | 17 KB | 11,084 -> 11,083 |
| USBMOUSE.DRV | 1,410 | 1,406 | -4 | 2 KB | 1,366 -> 1,362 |

(Batch 3 below takes ETHER.DRV to 16,888 and USBMOUSE.DRV to 1,396.)

No claim moves a KB (SOUND.DRV is 6,972 of 7,168), so what these buy today is
disk and headroom, not heap; the bytes are real and the rung is not the point.
The PicoMEM tier in SOUND.DRV goes from +485 to **+341** (6,637 without it).

SOUND.DRV, the PicoMEM tier (`picomem.inc`), -137:
* **pm_init's four zero stores and pm_sbport's** (27 bytes). The cells are 0
  because the image was loaded a moment ago and ATTACH is the first verb it
  gets - the guarantee `snd_entry`'s `[drv_up]` already rests on (os88drv.inc
  OS88_STATE: a load zeroes, a re-arm does not, and a tier change re-runs
  `sbl_attach`, never `pm_init`).
* **`[pm_sbport]` deleted** (6 bytes with its store): written, never read by
  any code. SPEC.md 34.10.2 kept it "for the reader of a dump", and
  `[sbl_base]` is the same port - whatever base the card took is the one
  `sbl_f_probe` finds by its own scan. `tests/picomem.py` reads the port off
  the model's accepted `78h` command instead.
* **`pm_porttest`, `pm_bios` and `pm_snd_on` written into `pm_init`**, their
  one caller: one register save instead of four, three `call`/`ret` pairs
  gone. `pm_sb_off` written into `pm_undo`, its one caller; `pm_undo` no
  longer clears `[pm_up]` (the refusal frees the image).
* **`pm_ticks` deleted**, `pm_wait` calls `OSAPI_GET_TICKS` (the cell is
  `mov ax, [cs:ticks]` / `retf`, IRQ0-driven, and `sbl_f_irqdisc` already
  times its F2h wait on it in the same attach): one far call per 256 status
  reads, ~1% of a batch, attach only.
* `pm_wait`'s status dispatch is three compares, not four (READY leaves on the
  equal compare's CF = 0, BUSY polls, `ja` past NOCMD refuses, ERR/NOCMD
  reset); `pm_cmd` keeps DX from `pm_wait` and builds the answer with
  `mov ah, al` / `xchg al, ah` instead of through BX; the card's own line is
  checked as `shr` of 54h (bits 2/4/6) after a `test al, F8h` (whose CF = 0
  is what n = 0 reads), the cell is `[es:si+22h]` with no `add al, 8`, and the
  unmask is `mov ah, FEh` / `rol ah, cl`; `lodsb`/`lodsw` walk the two tables.

  **Proved by A/B under unicorn**, the old `picomem.inc` (`2a05e31f`) against
  the new, both `%include`d as they are into one harness against a model of
  the card: 4,322 scenarios - card or not; the card's own line CH = 0, 1, 2,
  3, 4, 5, 6, 7, 8, 15, 23h, 83h; ten status scripts (READY, BUSY then READY,
  ERR then READY, the reset budget spent, NOCMD, INIT, WAITCOM, an undefined
  9, NOCMD for ever, and BUSY for ever against a clock that ticks, which
  reaches the deadline); IRQ answers ok / refuse 7 / refuse all; port answers
  ok / CMS on 220h-22Fh and 240h / an error; the multiplexer's vector the
  BIOS's or not; and `pm_init` alone and followed by `pm_undo`. Compared: the
  ORDERED trace of every port read and write, the final 8259 mask, all eight
  registers on return (both routines preserve every one), `[pm_base]`,
  `[pm_pmirq]`, `[pm_irq]`, `[pm_up]` and the int 13h call count. **0
  differing**, `[pm_up]` after `pm_undo` excepted (no longer cleared, above).
  The harness is not committed (it is scratch, the shipped row is
  `tests/picomem.py`).

SOUND.DRV, `sb.inc`, -13:
* **`sbl_f_irqdisc`'s PicoMEM skip** (the concept's own hunk, -7): the
  candidate index is `(7 - [pm_irq]) / 2` - `[pm_irq]` is 7, 5 or 3, taken
  only from `pm_irqs` - instead of a search of `sbl_dsc_irqn`. Commented at
  both ends.
* **`sbl_isr` re-laid for the common path, a SPEED change (-6)** - pass 10's
  `ksp10/covox.md` REFUSED entry, taken here because `sb.inc`'s PicoMEM hunk
  is this concept's. `.input` sits straight after the output body and falls
  into `.eoi`, `.direct` and `.under` follow `.done` and jump back up, so
  `jne .input` is short; `mov [sbl_valid+bx], bh` and two `cmp [sbl_valid+bx],
  bh` (BH = 0 from the `xor` above each) replace the immediates. 8088 clocks
  per block IRQ: **output and direct playback -14 / -12** (the relaxed
  `jne .input`'s taken skip, 16 -> 4, and a clock per register operand), an
  underrun -12, auto-init capture -4, single-cycle capture -4; no path takes a
  jump it did not take before. **`je .spur` stays relaxed**: every layout
  that makes it short as well puts `.direct` or `.eoi` 2-5 bytes out of short
  range (four were assembled), which would cost the common path what it gains.

ETHER.DRV, `ne_memok`, -7: `ne_dma_write` and `ne_dma_read` hand SI and DI
back advanced past the AX bytes they moved and keep AX, so the compare walks
down from the ends with `loope` instead of reloading both pointers and the
count. Attach only; the per-packet copy is untouched.

USBMOUSE.DRV, `um_pmattach`, -4: `[es:si+22h]` with no `add al, 8`, and
`mov ah, FEh` / `rol ah, cl` for the unmask. Attach only; `um_pm33` (the
per-report path) is untouched.

Rows: `soak -k picomem -k sndplay -k covoxdrv -k covoxauto -k covoxnolpt -k
usbmouse` 6/6 ok. No row opens a capture stream, so the input arm of
`sbl_isr` is proved by the listing: the same instructions in the same order
(the `cmp` against BH, which the `xor bh, bh` four instructions up made 0).

### Batch 3 - USBMOUSE.DRV and ETHER.DRV again

* **`um_pmattach`, -10** (USBMOUSE.DRV 1,406 -> 1,396; file 1,362 -> 1,353):
  `um_rx`/`um_ry` cleared from the `xor ax, ax` that zeroes ES (the second
  `xor` gone, and AH = 0 is what the cell arithmetic needs); the line's unmask
  and the int 33h hook in ONE `pushf`/`cli` window instead of two - the line is
  open before the vector either way, as it was, and the window is strictly
  wider; the hook is `mov ax, um_pm33` / `xchg ax, [es:CCh]` / `mov
  [um_old33], ax` and the same for CS, each word swapped by one instruction
  under that `cli`, where it was a load, a store and an immediate store per
  word. Attach only.
* **`ne_probe`, -1** (ETHER.DRV 16,889 -> 16,888): `inc byte [eth_word]`
  where the store of 1 is reached only past `cmp byte [eth_word], 0 / jne`.

`soak -k picomem -k usbmouse` ok (the picomem `mouse` leg drives the whole
PicoMEM backend: attach, reports through int 33h, chaining, detach).

## Tip

| | base | tip | delta |
|---|---:|---:|---:|
| kern_big `.cold` | 37,943 | 37,938 | -5 |
| **kern_big resident** | 92,495 | **92,490** | **-5** |
| kern_big `.ovlw` (boot only) | 5,086 | 5,075 | -11 (window spare 34 -> 45) |
| kern_small `.cold` | 23,479 | 23,474 | -5 |
| **kern_small resident** | 61,442 | **61,437** | **-5** |
| kern_small `.ovl` / `.ovlw` | 2,076 / 1,480 | same | 0 |
| kern_emu `.cold` / `.ovlw` | 38,061 / 5,086 | 38,056 / 5,075 | -5 / -11 |
| kern_dos (`kerndos.bin`) | 33,243 | 33,183 | -60 |
| SOUND.DRV image+bss | 7,122 | 6,972 | -150 (7 KB claim either way) |
| ETHER.DRV image+bss | 16,896 | 16,888 | -8 (17 KB) |
| USBMOUSE.DRV image+bss | 1,410 | 1,396 | -14 (2 KB) |

`KERN_SIZE` does not move (98,304 / 63,488) and no rung moves: the cold rung
stays 50 bytes into its 75th step on kern_big. The concept's kernel cost is
now `.cold` +35 on both kernels (from +40) and `.ovlw` +74 on kern_big (from
+85); its driver cost +341 in SOUND.DRV (from +485), +110 in ETHER.DRV (from
+118) and +245 in USBMOUSE.DRV (from +259).

## REFUSED

* **Caching "is there a PicoMEM" at boot in a `.bss` byte**, so the park reads
  one byte instead of running the ramp (~-10 resident a kernel, net of the
  byte). It needs a detection in the boot overlay on BOTH kernels, ~25 bytes
  each (kern_big's blob is full, so its `.ovlw` - 45 spare - and kern_small's
  window half, 56), a new unconditional 2A3h read at every kern_small boot
  where today there is none, and the answer goes stale across a hibernate
  image resumed on another machine - stale "no card" being exactly the
  `int 19h` hang 18.100.1 exists to avoid.
* **Warm-resetting EVERY machine** (the park's `int 13h` loop and `int 19h`
  gone, ~-50 resident a kernel): a behaviour change on every machine, POST on
  every Restart. The owner's call, not a size pass's.
* **One ramp for the park (`.cold`) and `dsk_fdd_pmemu` (`.ovlw`)**: a far
  call each way; resident grows ~+5 to save ~20 boot-only bytes.
* **`ne_probe`'s CR test through a direct `in al, dx`** (~-10 of ETHER.DRV):
  the test's whole argument is the stale byte an EMPTY 8088 slot answers,
  which is the last instruction byte the prefetcher fetched - through `ne_in`
  its `ret` (C3h); inline, the following bytes include a compare's immediate
  equal to the very value tested for. Only the field can say the inline form
  still refuses an empty slot.
* **The park's ramp as `mov cx, dx`** (675 reads, -1): a longer `cli`, and a
  claim about the card's counter past 256 reads nobody has checked.
* **The card's line checked by `shr` alone** (-3 more): on a 286 the count is
  masked to five bits, so a garbage CH of 23h/25h/27h... would pass as 3/5/7.
  The `test al, F8h` stays.
* **`je .spur` short in `sbl_isr`**: four layouts assembled, every one puts
  `.direct` or `.eoi` 2-5 bytes past a short jump on the common path.
* **`sbl_isr`'s output and input half-swaps as one body** (~-20): a rewrite of
  the block-IRQ path resting on `sbl_valid` holding only 0 and 1. Not the
  concept's, and not a hunk to change without a capture row.
* **`um_pmdetach`'s vector restore by `movsw`** (-2 net once SI/DI are saved
  for the detach contract): not worth DF and two saves.

## DEFECTS

None fixed. **One finding for the owner, by reading, NOT fixed**:
`kerndos/kdentry.inc`'s `kd_leave` ends in its own park and `int 19h` with no
PicoMEM check - the path SPEC.md 18.100.1 fixed for the desktop's Restart
(`int 19h` into a card whose devices are as the session left them never came
back on the field 5150). Leaving a DOS program under kern_dos on a PicoMEM
machine is plausibly the same hang. Not fixed: no field report, no emulator
has the card, and the check costs bytes of the DOS program (~35, the park's
own shape).

## CROSS-FILE

* `kerndos/kdentry.inc`'s `kd_leave` on a PicoMEM (DEFECTS, above).
* The 2A3h ramp is now written four times - `dsk_fdd_park_x` (`.cold`),
  `dsk_fdd_pmemu` (`.ovlw`, kern_big), SOUND.DRV's `pm_init` and
  USBMOUSE.DRV's `um_pmattach` - and cannot be shared across the
  kernel/driver boundary without a published cell (six bytes of ABI for ~20).

## ROWS

* `make -j2`, `make -j2 small`, `make emu`, the fast tier 61/61 after every
  batch; `kerndos.bin` assembles in every `make`.
* `tools/stkbalance.py` over the kernel (the fast tier's file list): 0
  unbalanced, base and tip; over SOUND.DRV's six files and USBMOUSE.DRV: 0
  both. ETHER.DRV walked alone reports `eth_dropall`'s tail into `inet.inc`
  at base and tip alike (a file-list artefact). `tools/stkdepth.py
  drivers/ether/ether.asm --check`: all 12 dropped saves still hold.
* `tests/unit/t_stkbalance.py` 25/25, with the two new fixtures.
* Soak rows, all ok: `picomem` (x3), `sndplay`, `covoxdrv`, `covoxauto`,
  `covoxnolpt`, `usbmouse` (x2), `midiracksb`, `fddpark` (the probe's moved
  call and the park on a machine without the card, MartyPC).
* Not run: `ethernet` (QEMU) - QEMU's ne2k_isa answers a doubled PROM, so
  `eth_word` is 1 and `ne_memok` is never reached there; the CR test it does
  reach is unchanged. `ne_memok` is driven by `tests/picomem.py`'s `ne` leg
  against the PicoMEM's NE2000 and a genuine NE1000 model, frames byte-exact.
* No emulator in the tree has a PicoMEM. Every success path above is proved
  by `tests/picomem.py` (unicorn, the shipped source, a model of the card) and,
  for SOUND.DRV's attach, the 4,322-scenario A/B described in batch 2.
