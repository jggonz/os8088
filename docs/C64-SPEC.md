# C64 — a Commodore 64 emulator, written in C (`apps/c64/`)

**The binding contract for the C64 package.** It stands to `apps/c64` as
SPEC.md §74 stands to `apps/runcpm`: every symbol, constant, string, register
answer and layout the port depends on is pinned here, and a change goes in
here *before* it goes in the code. The design record — the waves, the
decisions, the review that reshaped them and the measurements each wave took —
is `docs/plans/completed/C64-PORT-PLAN.md`; this file says what the code does
today and cites the plan for how it got there.

It lives outside SPEC.md by the user's instruction. **A bare `§N.M` in this
file is a section of THIS file**; SPEC.md's sections are always cited in full
as `SPEC.md §N`, and other documents by name (`CONTRIBUTING.md §6`,
`C64-SPEC §4.3` from outside). `tools/checkdocs.py` resolves all three.

**What is NOT built, stated once here so no section below has to hedge:**
the plan's wave 4 (`docs/plans/completed/C64-PORT-PLAN.md`, *Wave 4*) never
landed. There is no autostart, no bitmap or multicolour composer, no sprite
drawing and no `tools/c64prg.py`. File > Smart attach lands a `.PRG` in RAM
and stops (§11.3); the composer draws standard text and fills every other
mode with the background level (§5.1, §9.5).

---

## 1. What it is, and the attribution

### 1.1 The port

`apps/c64/`, package name **`C64`**, is a native reimplementation of **VICE
3.10's `x64`** — the fast, non-cycle-exact C64 machine — as a **windowed
Commodore 64**: a 6510 running in a 64KB heap claim, the KERNAL, BASIC and
character ROMs carried as part 0 of the package and claimed at launch (§1.4),
a VIC-II and two CIAs written in the C this toolchain compiles, and the
320×200 screen composed into 1bpp bands and blitted into a window.

It is a port in SPEC.md §73.12's sense, and in SPEC.md §74's: the behaviour
is VICE's, taken from its source and not from memory; the code is
reimplemented in the C subset of SPEC.md §73 plus the hot loops that are
hand-written 8086; what cannot carry is present and greyed with the fact that
greys it (SPEC.md §47). Nothing of VICE's *source* is vendored — every file
carrying derived tables, strings or behaviour names the VICE file in its
header and carries the GPL-2-or-later attribution.

The structural shape is RUNCPM's (SPEC.md §74.1): **no worker task and
nothing blocking.** The machine runs on the UI task in wake-driven wall
slices (`OSAPI_WM_WAKE` / `OSAPI_WM_ONWAKE`), and a C64 sitting at `READY.`
costs nothing until a key or a tick arrives. The package holds no task slot
at all — File > Exit emulator goes through `os88_wm_close` (§15.2).

### 1.2 Licence and attribution

VICE is **GPL-2-or-later**, © 1996–2025 the VICE team. Therefore:

- `apps/c64/` **is GPL-2-or-later**. The rest of the tree is not, and the file
  headers, the About panel (§12), `apps/c64/COPYING` and the PR body all say
  so.
- `apps/c64/COPYING` is VICE's GPL-2 text, verbatim (17,989 bytes). It is in
  the repo **and on every C64 floppy** (§14.2).
- The disk's `README.TXT` names the licence and points at `COPYING` on the
  disk and in the source tree.
- The About panel carries `Copyright 1996-2025, VICE team`, `GPL-2 or later`
  and the ROM copyright line, from VICE's `src/arch/gtk3/uiabout.c` and
  `README` lines 186–290.

### 1.3 The ROMs — committed, a stated departure from `CONTRIBUTING.md` §6

**The three Commodore ROM binaries are committed under `apps/c64/rom/`.**
This is a **user-decided departure from `CONTRIBUTING.md` §6** ("nothing
third-party is committed"), taken for those three files only, so that the
build does not depend on a VICE checkout being present beside this repo:

| file | bytes | SHA-256 |
|---|---|---|
| `apps/c64/rom/kernal-901227-03.bin` | 8,192 | `83c60d47047d7beab8e5b7bf6f67f80daa088b7a6a27de0d7e016f6484042721` |
| `apps/c64/rom/basic-901226-01.bin` | 8,192 | `89878cea0a268734696de11c4bae593eaaa506465d2029d619c0e0cbccdfa62d` |
| `apps/c64/rom/chargen-901225-01.bin` | 4,096 | `fd0d53b8480e86163ac98998976c72cc58d5dd8eb824ed7b829774e74213b420` |

They are the C64 defaults VICE 3.10 itself picks: `src/c64/c64-resources.c`
sets `KernalName` to `C64_KERNAL_REV3_NAME`, `BasicName` to `C64_BASIC_NAME`
and `ChargenName` to `C64_CHARGEN_NAME`, the three names in `src/c64/c64rom.h`.

`apps/c64/rom/README.md` states, and must keep stating: **the ROM images are
Copyright © Commodore Business Machines**, they are neither GPL nor ours, and
they are distributed here exactly as VICE distributes them in `data/C64/`.
Nothing else third-party is committed.

### 1.4 The ROM — a PART of the package

`tools/c64rom.py` checks each input's SHA-256 against §1.3's table and
concatenates the three into `build/c64-rom/C64.ROM` with a **fixed layout**:

| offset | length | contents |
|---|---|---|
| `0x0000` | 8,192 | KERNAL (`kernal-901227-03.bin`) |
| `0x2000` | 8,192 | BASIC (`basic-901226-01.bin`) |
| `0x4000` | 4,096 | CHARGEN (`chargen-901225-01.bin`) |
| | **20,480** | total — exactly 20KB, 512-aligned by construction |

**That file is PART 0 of `C64.O88`** (SPEC.md §20.12): `os88pkg.py` appends
it to the package (the `CC_PACKAGE` call in the Makefile passes it as the
part argument). `apps/c64/c64.asm` declares it as
`OS88_PART OP_ASSET, OP_COMP` — an ASSET because nothing in it is far-called
(the core reaches it by segment arithmetic off its base, §4.3), REQUIRED so
that a machine that cannot spare 20KB refuses the launch, and **compressed**
(SPEC.md §20.12.7): the 20,480 bytes ship as **17,361** under LZ4 and `len`
stays 20,480, so the refusal still needs no sector read.

`apps/cc/crt0.asm` calls `op_load` before any C runs, so by the time
`os88_main` executes the 20KB is claimed and the ROM is in it —
`os88_part_seg(0)` is its base, kept in `c64_m.romseg` — or the launch was
refused with a toast, before a sector was read, because the part table
`op_load` sizes from is already inside the image the kernel had to read
anyway. `os88_main` additionally refuses if that segment is below `0x0E00`,
because the KERNAL's fetch bias (`romseg − $0E00`, §4.3) would wrap; the heap
never starts that low (docs/KERNEL-MEMORY.md) and the check says so out loud.

There is therefore **no missing-ROM state anywhere in the package**: no halted
machine, no notice on the glass, no `C64.ROM missing` row and nothing greyed
on it. `tests/c64part.py` asserts the part is not on the disk, that the
package declares parts, that it launched, that `os88_part_seg(0)` is the
segment the C uses, and that five windows of the claim equal
`build/c64-rom/C64.ROM` — the last sixteen bytes among them, so a carve one
sector short cannot pass.

`APP_MAX_SIZE` bounds the primary SEGMENT's image plus bss, not the FILE, so a
part costs nothing against SPEC.md §73's cap: image 41,426 + bss 13,190 =
54,616 of 61,440, in a file of 58,833 bytes (§13.0).

---

## 2. Where the behaviour comes from — the authority table

Every user-visible surface names ONE VICE file. Paths are relative to the
**VICE 3.10 source tree**; the tree is the *reference*, never a build
dependency (§1.3).

| surface | source |
|---|---|
| menu bar: File / Edit / Snapshot / Preferences / Help (Debug is `#ifdef DEBUG` in VICE and absent here by VICE's own rule) and every item string under them, submenus folded into their head item; Help > About VICE... is the live item and the kernel's name pull-down About opens the same panel; the menu-set `AM_NAME` is `VICE` | `src/arch/gtk3/uimachinemenu.c` (`ui_machine_menu_bar_create`, the `.label` fields) |
| hotkey captions on every item, live or greyed — every binding in `hotkeys.vhk` and its `!include`s, transcribed, so a greyed item is never captioned from memory | `data/hotkeys/hotkeys.vhk` + every `!include`'d `hotkeys-*.vhk` |
| keyboard map: PC key → (row, col, shiftflag) of the 8×8 matrix, the `!LSHIFT`/`!RSHIFT`/`!LCBM`/`!LCTRL` positions, RESTORE on Page_Up, Tab = C=, Escape = RUN/STOP | `data/C64/gtk3_sym.vkm` (152 entries) |
| the keyboard's LEVEL model: a key is in the matrix while it is down | `src/keyboard.c` (`keyboard_key_pressed`/`keyboard_key_released`, `keyboard_latch_modifier_states`); os8088 side: SPEC.md §9.7 `OSAPI_KEY_DOWN`, `kbd_track` in `kernel/mouse.inc` |
| the 16 colours, their order and their LUMINANCE ladder | `src/vicii/vicii-color.c` — `vicii_colors_6569r5`, which is what VICE 3.10 as shipped compiles for a PAL C64 (`TOBIAS_COLORS` is defined; `PEPTO_COLORS`/`COLODORE_COLORS`/`MARKO_LUMAS` are not). **NOT `data/C64/vice.vpl`**: an external palette is loaded only when `${CHIP}ExternalPalette` is set, and it factory-defaults to 0 (`src/video/video-resources.c`) — §9.6 |
| the JAM line, `Main CPU: JAM at $E5CF` — VICE's format string with the `D'OH!` dialog's padding spaces dropped; 22 glyphs | `src/maincpu.c` (`"   " CPU_STR ": JAM at $%04X   "`) + `src/6510core.c` (`CPU_STR` = `Main CPU`) |
| window title `VICE (C64)` | `src/arch/gtk3/ui.c` (`"VICE (%s)"`, `machine_get_name()`) + `src/c64/c64.c` (`machine_name = "C64"`) |
| status bar: message area, `Joysticks:` with two indicators, drive 8's number, the speed widget's two labels `%7.0f%% cpu` and `%8.1f fps` (`CPU_DECIMAL_PLACES` 0, `FPS_DECIMAL_PLACES` 1) folded onto one row, the warp and pause LEDs as two labelled lamps `W` and `P`; what is dropped is in §10.3 | `src/arch/gtk3/uistatusbar.c` (`draw_joyport_cb`, `statusbar_append_led`, `statusbar_led_widget_create("warp:", …)` / `("pause:", …)`), `src/arch/gtk3/widgets/statusbarspeedwidget.c` |
| About box: `About VICE`, `The Commodore 64 Emulator`, version 3.10, `Copyright 1996-2025, VICE team`, GPL-2-or-later, the ROM copyright line | `src/arch/gtk3/uiabout.c`, `configure.ac` (`vice_version` 3.10), `README` lines 186–290, `COPYING` |
| machine model: C64 PAL, 985248 Hz, 63 cycles/line, 312 lines, 50.12 Hz (the default; NTSC greyed) | `src/c64/c64.h`, `src/c64/c64model.c` |
| 6510 opcode semantics incl. illegal opcodes, BCD, per-opcode cycle costs with the page-cross and taken-branch penalties, IRQ/NMI entry, the I-flag's one-instruction timing | `src/6510core.c`, `src/c64/c64pla.c`, `src/interrupt.c`, `src/c64/mainc64cpu.c`; the oracles are Klaus Dormann's `6502_functional_test` (fetched, pinned SHA, never committed) and `tools/c64dec.py` (§4.6) |
| the `$00`/`$01` processor port: DDR semantics, the read-back, which write re-banks | `src/c64/c64pla.c`, `src/c64/c64mem.c` (`zero_read`/`zero_store`, `pport`) |
| bank maps (which of RAM/BASIC/KERNAL/CHARGEN/IO each region shows for each `$01` value with `!exrom=!game=1`) | `src/c64/c64meminit.c`, `src/c64/c64mem.c` |
| the event/alarm model: "run to the next device event, then service it" | `src/alarm.c`, `src/maincpu.c` |
| CIA register file, timer A/B modes, ICR semantics, TOD, CIA1 PRA/PRB keyboard+joystick read, CIA2 PRA VIC bank bits and serial-bus read-back | `src/core/ciacore.c`, `src/core/ciatimer.h`, `src/c64/c64cia1.c`, `src/c64/c64cia2.c`, `src/iecbus/iecbus.c` |
| VIC-II registers `$D000-$D02E`, the screen/char base from `$D018` and the CIA2 bank, raster compare, IRQ flag/mask | `src/vicii/vicii-mem.c`, `src/vicii/vicii-irq.c`, `src/vicii/viciitypes.h`, `src/vicii/vicii-timing.h` |
| Copy and Paste: screen code → PETSCII → ASCII, ASCII → PETSCII, the line-ending fold, the KERNAL buffer's address and size | `src/charset.c`, `src/clipboard.c`, `src/arch/gtk3/actions-clipboard.c`, `src/kbdbuf.c`, `src/c64/c64.c` (`kbdbuf_init(631, 198, 10, …)`) |
| joystick bits: `$DC00`/`$DC01` bits 0–4 active low (up, down, left, right, fire); a keyset key is consumed by the stick and not typed; "Swap joysticks" | `src/joyport/joystick.c`, `src/keyboard.c`, `src/c64/c64cia1.c` |
| SID register file (`$D400-$D41C`, voice frequency/control/gate) | `src/sid/sid.c` (register semantics only; no synthesis carries) |
| warp: the slice cap lifted AND rendering capped at 10 fps | `src/vsync.c` (`warp_render_tick_interval`, the skip under `warp_enabled`), `src/sound.c` (`sound_suspend`) |
| power-on RAM pattern | `src/ram.c` (`RAMInitStartValue` 0, `RAMInitValueInvert` 4, `RAMInitValueOffset` 2, `RAMInitPatternInvert` 16384, `RAMInitPatternInvertValue` 255) |
| icon: a 16×16 1-bit breadbin drawn for this port, not copied | `data/common/vice-x64_16.png` is the reference look only; `apps/runcpm/icon.inc` is the format precedent |
| the platform precedent every non-VICE surface follows (slice loop, wake, fullscreen chord exception, About panel rows, toast-under-fullscreen, the keyboard-mouse sentence) | SPEC.md §74, §74.1, §74.2, §74.4, §9.6, §9.7, §5.4.1, §5.4.2; `apps/runcpm/runcpm.c`, `rcterm.c`, `rcz80.inc`, `rcband.inc`, `rcmem.inc`, `rcabout.c` |

---

## 3. The memory model

### 3.1 The two claims

Neither the C64's RAM nor its ROMs live in the package. Both are heap claims,
their own segments:

| claim | size | contents |
|---|---|---|
| RAM | `os88_mem_claim(64)` — 65,536 bytes, `c64_m.ramseg` | the C64's RAM, `$0000-$FFFF`, flat, one segment |
| ROM | `op_load`'s carve — 20,480 bytes, `c64_m.romseg` = `os88_part_seg(0)` | the ROM part, §1.4's layout, claimed and decoded by the parts standard before `os88_main` runs |

Launch is **defined by the claims succeeding**, not by a free-KB figure: the
64KB claim must succeed (its refusal toasts `C64: 64KB? <n> KB`, with
`os88_mem_largest_kb()`'s answer) and `op_load` must have had its 20KB. Three
more claims appear later and are refused politely if they cannot be had: the
package's own `C64.OVL` module (§13.3) at the first wake, a transient claim
the size of the `.PRG` during Smart attach (§11.3), and the 2KB clipboard
staging claims of Copy and Paste (§7.7).

Colour RAM (1,024 nibbles), the VIC/CIA/SID register files, the 1bpp frame
shadow and the keyboard matrix are in the package's **bss** (§13.0). The
core's own hot scratch is in neither — §3.5.

### 3.2 `$0000` and `$0001` are NOT RAM

**Every read and every write of `$0000` and `$0001` is a special case**, on
the fast path and the slow path alike. They are the 6510's processor port
(`c64pla.c`, `c64mem.c`'s `zero_read` / `zero_store`), not memory:

- **`$00`** is the data-direction register. Reset value `$2F`.
- **`$01`** is the data port. Reset value `$37`.
- **A write to `$01` (or to `$00`, which can change which bits `$01` drives)
  re-evaluates the bank map at once**, together with the fetch segment and
  the boundary words (§4.3). The core never executes one instruction under a
  stale map.
- **Read-back of `$01` is ONE formula**: VICE's
  `data_read = (data | ~dir) & (data_out | pullup)` with the C64's `pullup` =
  `$17`, which on a machine with no datasette reduces to
  **`(data & dir) | (~dir & $17)`** (`c64io.c`). An OUTPUT bit reads what was
  written to it; an INPUT bit reads the pull-up where the board has one and 0
  where it has none: bits 0–2 read 1, **bit 3** (cassette write) reads 0,
  bit 4 (cassette sense) reads 1, **bit 5** (the motor) reads 0, bits 6–7
  read 0. A program that reads `$01` to discover the bank sees this table and
  not a real 6510's decay behaviour.

Underneath the port, RAM `$0000`/`$0001` still exists and is what the VIC and
any DMA-less read of those bytes would see; this port keeps the two RAM bytes
and the two port registers separately, as VICE does.

### 3.3 The seven bank maps

With no cartridge, `!exrom = !game = 1`, VICE's 32 bank configurations
collapse to **eight `$01` values and seven distinct maps**
(`c64meminit.c`), which is the whole table this port carries:

| `$01` & 7 | `$A000-$BFFF` | `$D000-$DFFF` | `$E000-$FFFF` |
|---|---|---|---|
| 0 | RAM | RAM | RAM |
| 1 | RAM | CHARGEN | RAM |
| 2 | RAM | CHARGEN | KERNAL |
| 3 | BASIC | CHARGEN | KERNAL |
| 4 | RAM | RAM | RAM |
| 5 | RAM | I/O | RAM |
| 6 | RAM | I/O | KERNAL |
| 7 | BASIC | I/O | KERNAL |

Rows 0 and 4 are the same map. `$01 = $37` at reset selects row 7, which is
the machine BASIC runs in. The three bytes the core reads to decide a region
(`C64_SCR_MAPA/D/E`) live in its scratch (§3.5).

### 3.4 The fast path, the slow path, and the slow FETCH path

The core's memory access is priced in instructions, not in calls:

- **Reads below `$A000`, except `$0000`/`$0001`** (§3.2), and **writes
  outside `$D000-$DFFF`, except `$0000`/`$0001` and the core's scratch
  (§3.5)**, are the two-instruction fast path against `DS` = the RAM claim.
  This is the overwhelming majority of every program's traffic.
- **Every write additionally sets one bit in the 256-page dirty bitmap**
  (§9.2) and widens the write window when the address is inside the watch
  range. That is the stated per-write cost of this design, in exchange for a
  screen model that can see a RAM character set change.
- **Reads at or above `$A000`** consult the map (§3.3): RAM (the fast path
  again), ROM (one segment load into the ROM claim, then the read —
  `c64_rom_rd`), or I/O.
- **Every `$D000-$DFFF` access in an I/O bank is a direct cdecl call into
  `c64io.c`** (`_c64_io_rd` / `_c64_io_wr`) made from inside the core's
  handler: the shim swaps `DS` to the package segment, saves the core's live
  registers **including the cached fetch `ES`** (§4.3 — `ES` is
  caller-clobbered by the C ABI), calls, restores. **The core never exits
  mid-instruction** (§4.5).
- **There is a true slow FETCH path.** An opcode or operand byte fetched from
  `$D000-$DFFF` while I/O is mapped goes through `_c64_io_rd`, not through
  the biased `ES`. Fetching from I/O is a thing real programs do only by
  accident, and this port executes what the accident really produces instead
  of silently executing RAM.

### 3.5 The core's scratch — in the emulated machine, never in bss

**A hot counter in the package's bss is a TCG slow path under QEMU** when that
page also holds translated code (`.claude/skills/port-to-os8088/LESSONS.md`
13; it cost RUNCPM 5× on the whole Z80 core). The 6510 core's hot words
therefore live in the RAM claim, at **`$FFC0-$FFF9` — 58 bytes at the top of
the C64's own memory, BELOW the six vector bytes**: `$FFFA-$FFFF` (NMI,
RESET, IRQ) stay real RAM, because a program that banks the KERNAL out
(`$01 = $35`) puts its own vectors there and the core fetches them from RAM
in that map. The offsets are the `C64_SCR_*` constants in `c64.c`:

| offset | what | § |
|---|---|---|
| `$00` (32 bytes) | the page dirty bitmap | §9.2 |
| `$20` | `BLO` — the low edge of the region `ES` is biased for | §4.3 |
| `$22` | `CARRY` — cycles `c64_cut()` took out of the countdown | §4.4 |
| `$24` | `DEAD` — **the countdown**, the one hot counter | §4.2 |
| `$26` | `BOUND` — the high edge of that region | §4.3 |
| `$28` | the cached fetch `ES` | §4.3 |
| `$2A` | bit 0 the IRQ level, bit 1 the NMI edge | §4.4 |
| `$2C`, `$2E` | the write window | §9.2 |
| `$30` | "the core wrote something" | §9.2 |
| `$32`, `$34` | the watch range | §9.2 |
| `$36`–`$38` | the three bank-map bytes the core reads above `$A000` | §3.3 |

The 32-bit total of emulated cycles the status bar divides (§10.2) is NOT
here: it is a two-word counter in the package's C, folded once per `c64_run`
call, and not hot.

That address is chosen because the KERNAL's vectors sit there **in ROM** in
every map that runs KERNAL code, and nothing in the KERNAL or BASIC uses the
RAM under them. Two stated deviations follow, and the CPU harness (§4.6)
holds a case for each:

- **A read of `$FFC0-$FFF9` in an all-RAM map reads the scratch**, not the
  RAM the emulated machine wrote.
- **A write to `$FFC0-$FFF9` is dropped.** The write path already tests the
  high byte; only the `$E0-$FF` branch pays the extra compare.

**Anything that loads a whole 64KB image into the claim lands ON TOP of the
scratch** and has to clear it afterwards: `os88_main` does
(`c64_scratch_clear`), and so does `hosttest/c64cputest.asm` after it reads
Dormann's 64KB fixture in — without it the fixture's own bytes at `$FFCA`
become the pending-interrupt flags and the core takes an NMI nobody raised.

The mask table the dirty bitmap needs is `cs:`-resident and **read-only** —
reads of a translated page are not the TCG hazard, only writes are.

### 3.6 The movers, and every cross-segment routine

`apps/c64/c64mem.inc` is `rcmem.inc`'s shape for these two claims: `c64_rd` /
`c64_wr` / `c64_rd16` (near cdecl accessors), `c64_dirty`, `c64_scr_rd` /
`c64_scr_wr` (the scratch, one word), `c64_dirty_take` (the whole flush read
in one call, §9.2), `c64_rom_rd`, `c64_div32` / `c64_muldiv` (§10.2),
`c64_zcopy_in` / `c64_zcopy_out` (package ↔ RAM), `c64_zzcopy_in` (claim →
RAM, far-to-far, the `.PRG` load), `c64_copy_row` (Edit > Copy's per-cell
loop, §7.7) and `c64_zfill` (the power-cycle pattern).

**The rule is wider than the movers, and it is a rule:**

- **Data in the RAM or ROM claim is passed as an explicit `(segment, offset)`
  pair, never as a C pointer.** A C pointer is a package-DS offset and
  nothing else; a "pointer to a screen row" is a defect that assembles
  cleanly. Every `c64band.inc` and `c64mem.inc` entry point takes the pair.
- **Every `movs`/`cmps`/`stos` loads `DS`/`ES` on purpose, restores both,
  leaves `DF` clear, and carries the `cc8086:allow` marker** (SPEC.md §73's
  second C rule is suspended only inside these).
- **All of them are tested under `SS ≠ DS` with an `ES` sentinel**, not only
  the movers: `hosttest/c64memtest.sh` covers `c64mem.inc` *and*
  `c64band.inc`, with four negative controls — ES, DF, BP and DS — that must
  fail (§14.5).

---

## 4. The 6510 core

`apps/c64/c64cpu.inc`, hand-written 8086 in `rcz80.inc`'s shape, inside the
shim (§13.1).

### 4.1 The register plan

| 6510 | 8086 | note |
|---|---|---|
| A | `AL` | |
| P (N, Z, C) | `AH` | `lahf` layout, so the host flags carry them |
| P (V, D, I, B) | `CH` | never `cs:` statics — a write into the package's own code page is the TCG slow path of §3.5, and V moves on every ADC/SBC/BIT |
| X | `CL` | indexed addressing is `add bl,cl / adc bh,0`, whose carry IS the page-cross penalty §4.2 charges — so `CH` is free for the flags at no cost |
| Y | `DL` | `DH` is the memory-data byte and a free scratch: Y is indexed with `add bl,dl / adc bh,0`, so nothing ever needs `DX` as a word |
| PC | `SI` | |
| S | `DI` | held as the FULL stack address `$0100 + S`, so a push is `mov [di],v / dec di / or di,0x0100` |
| dispatch scratch / effective address | `BX` | |
| the cdecl frame, and 12 bytes of scratch below it | `BP` | `[bp+disp]` addresses **SS**, deliberately: the decimal ADC/SBC needs more temporaries than this plan has registers, and a push inside a handler would put them where a fetch cannot reach them |
| RAM | `DS` | the 64KB claim |
| the fetch segment | `ES` | §4.3 |

The dispatch is a 256-entry table: `xor bh,bh / mov bl,[es:si] / inc si /
shl bx,1 / jmp [cs:bx+tab]`.

The countdown and the boundary words are **not** registers and **not** bss:
they are in the emulated machine's own scratch (§3.5), and because `DS` is
the C64's RAM for the whole of `_c64_run` each of them is a bare
`[disp16]` with no segment override.

**`P` is a real 6502 `P` byte in `c64_m`, in both directions.** The core
unpacks it into `AH`/`CH` on entry and packs it back on exit — once per call
— so the C and the harness read the register the machine has rather than
this file's layout. PHP, PLP, BRK, RTI and interrupt entry go through the
same two helpers.

**The stack wrap is `and di,0x00FF / or di,0x0100` and not `and di,0x01FF`.**
`0x0200 & 0x01FF` is `0x0000`, not `0x0100`, so the one-line form has a pull
with `S = $FF` read byte zero of the address space. BASIC boots straight
through that defect — the KERNAL never wraps the stack — and Dormann's
"proper stack wrap around" test is what catches it (§4.6).

### 4.2 Time is 6510 CYCLES

**The clock is the emulated 6510's cycle count and nothing else.** Every
opcode carries its real cost from `6510core.c`'s tables (`c64_cyc`, 256
read-only bytes), **including the page-cross penalty on the indexed
addressing modes and the taken-branch and branch-page-cross penalties**, and
the core decrements **one cycle counter** (§3.5's `DEAD`) by it.

That counter is **both** the wall-clock slice budget (§4.4) and the device
clock (§6.3). It is the only time in this machine.

**It is a SIGNED word, and that sets the cap on every budget in this port.**
The check between instructions is `cmp word [DEAD],0 / jle` — two
instructions, and no per-instruction test of anything else — so a budget
above 32,767 arrives negative and the core expires before its first fetch.
`C64_SLICE_MAX` is 16,384 and `C64_SLICE_WARP` 30,000 for that reason, and
the CPU harness runs Dormann in 30,000-cycle passes for the same one.

The countdown is checked **between** instructions and never inside one, so
`c64_run` may overrun what it was asked for by at most one instruction's
cost. What was NOT spent is left in `c64_m.cnt` — negative by up to seven —
and the caller's `ran = asked − cnt` is therefore exact. Nothing is lost or
double-counted at a slice boundary.

A count of *control transfers* is not a cycle count and is not used as one: a
branch-counted clock would advance the CIAs and the VIC at a rate that
depended on the program's shape. The cycle table is the price of CIA timer
modes, IRQ phase, `$D012` reads and raster interrupts meaning what they say.

### 4.3 The fetch segment, and the boundary word

`ES` is biased so that `[es:si]` is always PC's byte: for a PC in RAM it is
the RAM claim; for a PC in ROM it is the ROM claim **minus that bank's own
bias** (§1.4's layout gives one constant per bank — `romseg − $0E00` for the
KERNAL, which is why §1.4's `0x0E00` guard exists).

**It is re-evaluated whenever the fetch address leaves a mapping region —
not merely at control transfers.** Falling through from `$9FFF` to `$A000`,
`$BFFF` to `$C000`, `$CFFF` to `$D000` or `$DFFF` to `$E000` changes the
visible bank with no branch in sight, and an instruction *beginning* at
`$9FFE` fetches operand bytes from the other side. So:

- The core keeps the **region `ES` is currently biased for** in its scratch
  (§3.5) — a LOW edge and a HIGH edge, `BLO` and `BOUND`. The boundaries are
  `$A000`, `$C000`, `$D000`, `$E000` and the last byte of the address space.
- **Two `cmp`s per fetch, one against each edge.** A ceiling alone is only
  correct while PC increases: a `JMP` back from `$E000` to `$0400` leaves PC
  BELOW the biased region. Re-biasing on every control transfer instead would
  cost ~20 instructions on the most frequent path in the machine, where a
  range check costs two. When PC leaves the region, the map is consulted and
  `ES`, `BLO` and `BOUND` are recomputed. Operand fetches use the same
  guarded fetch.
- **A fetch from `$0000`/`$0001`, and from `$D000-$DFFF` with I/O mapped,
  takes the slow path** and leaves the region EMPTY, so every fetch there
  re-enters the same routine.
- **Every write to `$00` or `$01`** recomputes both (§3.2).
- **Every cdecl call out of the core saves and reloads the cached `ES`**
  (§3.4) — the C ABI clobbers it.

**Every map transition has a case in the CPU harness** (§4.6, row 4).

### 4.4 The alarm model, and the wall slice

VICE's own structure (`src/alarm.c`, `maincpu.c`): **run to the next device
event, service it, compute the next one.** There is no fixed quantum
anywhere in this machine.

**The alarm.** Before each run, `c64_alarm_next()` (`c64io.c`) computes
*cycles to the next event* as the minimum of: the end of the frame, the TOD's
50 Hz tick, CIA1 and CIA2 timer A and timer B underflow, and the VIC raster
compare. The core runs exactly that far and RETURNS; `c64_advance(ran)` then
moves every device phase by what actually ran, and the loop computes the next
deadline. `os88_onwake` is that loop (§13.1). Two points that are deliberate:

- *The service is a return, not a call out of the core.* The core keeps the
  whole 6510 in registers; a cdecl call from inside it would save and reload
  every one of them, which is the same work as the entry/exit shell.
- *The end of a raster LINE is not an alarm.* `$D012` is computed from the
  cycle counter when it is READ; a per-line alarm would end the run 312 times
  a frame for a register most programs never touch. The raster COMPARE and
  the frame end are alarms, so a raster interrupt still fires on the line it
  was armed for and in the right order against every CIA interrupt.

**Interrupts are taken between instructions, and the cost of that is zero
per instruction.** The core checks the pending flags at the top of `_c64_run`
and at the three instructions that can UNMASK a line — `CLI`, `PLP` and
`RTI`. Everything else that can raise one is an alarm, and an alarm is what
ended the run, so an alarm-raised IRQ is taken with no latency at all. The
one exception is an I/O write that unmasks a flag already set (`STA $DC0D`),
which happens inside a run: `c64_cut()` ends that run at once and moves the
unspent countdown into `CARRY`, so ending early costs nothing in the cycle
accounting. **The I flag's one-instruction timing is carried, in both
directions** (`6510core.c`'s CLI, PLP and SEI arms, `mainc64cpu.c`): an IRQ
*unmasked* by CLI or PLP is taken only after ONE more instruction, and an IRQ
that was visible under the OLD I value is still taken at the end of a SEI or
a masking PLP. RTI has neither delay. The delay is implemented by ENDING THE
RUN after that one instruction — the countdown is set to 1 and the unspent
budget moves into `CARRY` exactly as `c64_cut` does — and the slice loop's
next `c64_run` begins with the interrupt check. One corner: where the
countdown is already 1 or less the run is ending anyway and the IRQ is taken
at the top of the next run, one instruction early; the alternative is a
compare on the hottest path in the machine.

**BRK is seven cycles and it is charged once** — the dispatch charges the
table entry and the vector path charges nothing more. §4.6's cycle row holds
BRK and an IRQ entry so a double charge cannot come back.

**The wall slice.** Separately, the core is given a **raw cycle budget**
(`c64_budget`) for how long it may hold the UI task — RUNCPM's structure
(SPEC.md §74.1):

- seeded from `os88_cpu()` (`OSAPI_CPU_INFO`) — `512 << os88_cpu()`, clamped
  into the range below (`c64_tier_init`, `c64scr.c`);
- **`C64_SLICE_MIN` = 256 to `C64_SLICE_MAX` = 16,384 cycles**, doubled when
  four consecutive slices each finish inside one host tick, halved when one
  slice spans two tick boundaries;
- **the doubling is CLAMPED to the cap, not merely stopped below it** — with
  warp's ceiling of 30,000 the unclamped double lands on 32,768, which is
  −32,768 in a 16-bit `int`, and the core would expire before its first
  fetch;
- **only a genuinely EXHAUSTED slice adapts** — a wake that did not spend its
  budget (the machine paused, or jammed) leaves the estimate alone. Without
  the rule, typing into a paused machine walks the budget to its cap over a
  hundred wakes that ran nothing, and the first wake after the resume is a
  second of stalled UI task.

**Warp mode is this ceiling raised, and the flush rate capped — and nothing
else** (Alt+W, §11.1). Nothing in this port paces the machine against a wall
clock (SPEC.md §74.4's posture, and why §11.2 greys VICE's Emulation speed
section), so the only throttle between the 6510 and the host is how many
cycles a wake may run. Warp raises the ceiling from 16,384 to
**`C64_SLICE_WARP` = 30,000** — not 32,767, because `c64_m.cnt` is a signed
word and the alarm model may round a budget UP by the instruction in
progress. The other half is VICE's too: `vsync.c` limits rendering to 10 fps
under warp (*"makes warp faster"*), so **while warp is on the flush interval
is `max(c64_flush_every, C64_WARP_FLUSH = 2)` host ticks** — 18.2 Hz / 10 fps
is 1.82 ticks. Entering warp also calls `c64_sound_stop()`, as VICE's
`sound_suspend` does (§11.4).

**On `CPU_8086` neither half binds**, and the item says so: the slice cap is
never reached (a 16,384-cycle slice is far more than one host tick at
4.77 MHz, so the halving arm keeps the budget near its 256 floor) and that
tier already flushes every other tick, slower than 10 fps. The message there
is `Warp mode on - no change.` — exactly 25 cells, so it takes §10.1's short
erase path — and on every other tier `Warp mode on.`; both are this port's
wording after `Paused.` / `Running.`, not VICE's, which has a `warp:` LED and
no message. The item is not greyed: on a 286 or 386 the cap binds and warp
does what VICE's does. Measured under QEMU with a `FOR`/`PRINT` loop, warp is
worth about 7 % — the wake round trips and repaints saved, not emulation
getting faster (the plan's wave-3 record has the readings).

**The floor is never a whole jiffy.** Every device phase — the cycle counter,
each timer's remaining count, the raster position, the TOD accumulator — is
retained across slices and across wakes, so a slice may end anywhere.

**The wake is re-posted only while there is something to do** —
`c64_wants_wake()`: something dirty, or a running (`C64_ST_RUN`) machine that
is either un-paused or owes an Advance frame. **A message being up is not on
that list**: a stopped machine keeps its message until the next event
(§10.1). A JAM stops the wake too, and `os88_onkey` / `os88_onclick` /
`os88_oncmd` kick it so a wake the full event ring dropped cannot park a
running machine.

**Every 16-bit counter compared against a cycle count is compared UNSIGNED,
and a counter at or above `$7FFF` is skipped rather than cast.** `unsigned`
is 16 bits here and a CIA counter's whole range is legal — `$FFFF` is the
reset-default latch and the free-running-timer idiom. Cast to `int` that is
−1: `while (n > (int)c)` subtracts nothing for ever inside `c64_advance`, and
in the scheduler `(int)(c64_ta[k] + 1)` is 0 or negative, always wins the
minimum, and runs the core one cycle at a time. The frame end is never more
than 19,656 cycles away, so a counter above `$7FFF` can never be the nearest
alarm. Neither defect shows on the host (`int` is 32 bits there) or in the
core's gate; `hosttest/c64uitest.c` models the target's widths with
`short`/`unsigned short` as the negative control.

### 4.5 What `_c64_run` answers

Two values only:

| answer | meaning |
|---|---|
| `C64_RUN_SLICE` | the wall-slice cycle budget was spent between instructions |
| `C64_RUN_JAM` | a `KIL`/`JAM` opcode; the machine stops, the status row says so, the window stays up |

**The JAM line is VICE's and it is PERMANENT.** `Main CPU: JAM at $E5CF` —
the format string of `src/maincpu.c` with `CPU_STR` from `src/6510core.c`
and VICE's dialog padding dropped (§2). `C64_ST_JAM` is a permanent
status-row state and not a five-second message: `c64_jam` raises
`c64_dirty_any`, clears `c64_st_lok` and toasts, and never touches `c64_msg`
(§10.1).

**A frozen speed figure with no keystroke echoing is not a jam.** A wake
drained off the event ring by a kernel loop while the per-slot coalescing
flag stayed set used to park the slice driver for good; SPEC.md §74.1's
idle-arm sweep of that flag is the kernel fix, and the plan's wave-3 record
carries the reproduction.

Alarms and I/O are **calls out of the core, not exits from it** (§3.4, §4.4),
so there is no mid-instruction exit and no caller ever has to resume a
half-executed opcode.

### 4.6 The core's gate

`apps/c64/hosttest/c64cputest.asm` + `.sh`, run by **`make c64cputest`**
(minutes, like `make rcz80test`; deliberately *not* in `build.sh`): the
**shipping `c64cpu.inc`**, in a boot sector, in raw QEMU, under `SS ≠ DS`.
**Twelve rows**, each with a negative control the harness FAILS if it passes:

| row | what | the control |
|---|---|---|
| 1 | **Klaus Dormann's `6502_functional_test`** to its success trap at `$3469` — the 65,536-byte binary, fetched at a pinned SHA-256 and never committed, read into the C64's RAM and started at `$0400`; the judgement is where PC settles, and a failure prints the trap and the registers | `ADC #` dispatched to `ORA #` — it must not reach `$3469` |
| 2 | **each of the seven bank maps** (§3.3) | the map row is shifted |
| 3 | **`$0000` and `$0001`**: DDR-derived banking, §3.2's read-back, a re-bank taking effect on the very next fetch | the re-bank does not take effect |
| 4 | **reads, writes and FETCHES at `$9FFF`, `$A000`, `$BFFF`, `$C000`, `$CFFF`, `$D000`, `$DFFF`, `$E000`**, seven cases: two whole instructions either side of `$A000`, an immediate straddling `$9FFF`/`$A000`, one straddling `$BFFF`/`$C000`, a `JMP` whose HIGH byte comes from `$D000` (the slow I/O fetch), one straddling `$DFFF`/`$E000`, a backward `JMP` from the KERNAL into RAM (§4.3's LOW edge), and a `$01` remap in the middle of the instruction stream | the bank above it is made RAM |
| 5 | **real I/O stub returns**: the stubs answer values the test then checks, so the cdecl convention, the `DS` swap and the `ES` save/reload are exercised | the stub answers a constant |
| 6 | **`ES`/`DS` restoration** after every call out | the reload is NOPped out of the shipping text at runtime |
| 7 | **IRQ and NMI entry**, including entry while a bank switch is pending | nothing is pending |
| 8 | **the illegal opcodes** `6510core.c` implements, every family EXECUTED: LAX, SAX, DCP, ARR in both modes with its flags, ANE, ALR, ANC, SBX and row 11's four stores, each with an answer only that opcode produces | `LAX abs` dispatched to `NOP`; `ARR #` dispatched to `AND #` |
| 9 | **cycle totals**, table-driven — eighteen entries: every addressing mode of LDA, a store, three RMW forms, the stack pair, `JSR`/`RTS`, both `JMP`s, a branch taken, not taken and taken across a real page boundary at `$08FD`, BRK's seven cycles and an IRQ entry's seven | one opcode costs one cycle less |
| 10 | **decimal ADC and SBC over all 262,144 cases** — every accumulator, every operand, carry both ways, both instructions — against **`tools/c64dec.py`**, an independent Python implementation of the documented NMOS rules that hands the harness four 16-bit checksums (262,144 results will not fit a boot sector). It replaces Dormann's `6502_decimal_test`, which is published as SOURCE only; a reference written in this tree is weaker evidence than a fetched one, and the mitigation is that `c64dec.py` shares no line with `c64cpu.inc`'s decimal path | `SED` dispatched to `CLD` |
| 11 | **the four unstable stores** SHA, SHX, SHY and SHS: the mask is the **unindexed** base's high byte plus one and a page cross puts the VALUE in the target's high byte (`6510core.c`'s `STORE_ABS_SH_*`); five cases, one landing at `$0008` instead of `$2008` | `SHA abs,Y` dispatched to a plain `STA abs,Y` |
| 12 | **what an interrupt puts on the stack, and what RTI takes off**: `$01FD`, `$01FC`, `$01FB` by name — PC high, PC low, the status byte with `B` clear for an IRQ and an NMI and set for BRK — `S` landed at `$FA`, and a BRK brought back through `RTI` to its own `PC + 2` | `BRK` dispatched to `NOP` |

Every perturbation reaches the ENVIRONMENT or the core's own tables at
runtime, never the source: what is assembled is byte for byte what ships. The
harness CLEARS the bytes a row reads before it runs — a control that read
the value the positive run had left at the same address passed, which is a
control proving nothing.

---

## 5. The VIC-II

### 5.1 What is modelled

- **Standard text mode is composed** — glyphs from the CHARGEN ROM or the
  RAM character set the VIC is pointed at, resolved to 1bpp by luminance
  (§9.6). The mode is read from `$D011` bits 5–6 (ECM, BMM) and `$D016`
  bit 4 (MCM); **any mode other than standard text fills the span with the
  background's luminance level** (`c64_band1`'s `mode != 0` arm). There is
  no bitmap, multicolour or extended-background composer, and no sprite
  drawing: the sprite registers are stored and nothing reads them.
- Screen base and character base from `$D018` **and** the CIA2 `$DD00` bank
  bits (§6.2).
- Border and background `$D020`/`$D021`. A `$D020` write costs a fill only
  when it crosses the luminance threshold (§9.7).
- `$D012` raster read, `$D019` flag, `$D01A` mask, and the raster compare —
  all off the cycle clock (§5.2). **The STATUS latches whether or not the
  interrupt is unmasked, and only the LINE is gated**: `vicii_irq_raster_set`
  sets `$D019` bit 0 unconditionally and `vicii_irq_set_line` is the only
  thing that reads `$D01A` — and it both SETS and CLEARS `$D019` bit 7 from
  `irq_status & regs[0x1a]`. So `LDA $D019 / AND #$01` with interrupts
  disabled — the commonest raster wait there is — sees the bit, and bit 7
  drops when the flag is acknowledged. The compare is an alarm on every frame.
- **Not honoured, silently**: the 25/24-row and 40/38-column flags and the
  fine scroll bits of `$D011`/`$D016`. A program that uses them draws the
  same picture as one that does not.

### 5.2 Timing — the PAL frame, off the cycle clock

The PAL frame is **63 cycles × 312 lines = 19,656 cycles = 50.123 Hz**
(`C64_PAL_LINE`, `C64_PAL_LINES`, `C64_PAL_FRAME` in `c64io.c`; `c64.h`,
`vicii-timing.h`). It has nothing to do with the 60 Hz jiffy, which is a
thing the KERNAL programs a CIA to produce (§6.3).

The raster counter is **computed from the cycle counter when it is read** —
`$D012` is `frame_cycles / 63` — and the raster compare and the frame end are
alarms (§4.4). A program arming two raster interrupts in a frame gets both.

The VIC is serviced at LINE granularity: a register written part-way along a
line takes effect from that line.

### 5.3 What does not carry

- **Sprites** — not drawn at all (§5.1).
- **Sprite–sprite and sprite–background collision:** `$D01E` and `$D01F`
  **answer 0.**
- **Bitmap, multicolour and extended-background modes** — filled with the
  background level (§5.1).
- **Cycle-exact raster effects** — mid-line colour changes, bad-line timing,
  FLD, FLI, sprite stretching, opening the border, fine scroll.
- **Colour.** §9.6.

Text, BASIC, the KERNAL and text-mode programs are unaffected; games and
demos mostly are not text-mode programs.

---

## 6. The CIAs

### 6.1 CIA1 (`$DC00-$DCFF`)

Timers A and B (one-shot and continuous, underflow IRQ), the ICR with VICE's
mask/flag semantics (`ciacore.c`), TOD registers read and write, and **PRA/PRB
with the DDR**: PRA drives the keyboard matrix columns and PRB reads the rows,
against §7's cached matrix, with the joystick bits (§8) ORed in as
`c64cia1.c`'s `read_ciapa` / `read_ciapb` do. The **IRQ line** into the 6510
is CIA1's ICR ORed with the VIC's `$D019 & $D01A`.

**The 60 Hz jiffy is not a constant of this design — it emerges.** The KERNAL
writes `$4025` (16,421 cycles) into CIA1 timer A on a PAL machine, and the
timer underflows when the cycle clock says it does. `TI$`, the cursor blink
and the keyboard scanner run at the rate the emulated machine chose, which is
what makes a program that reprograms timer A behave.

Four things a CIA has that a pair of counters does not, each cited where VICE
has it in `c64io.c`:

- **The two timers are advanced INDEPENDENTLY from the same elapsed count**
  (`ciacore.c`). Only the CASCADE mode (CRB `INMODE` = 10) consumes timer A's
  underflows.
- **A raised interrupt is not cancelled by a MASK change.** VICE raises the
  output from the flags and drops it in the ICR **read** (*"pending
  interrupts and currently active interrupts are never cancelled or
  cleared"*), so the asserted output is a state of its own here. Recomputing
  the line from `flags & mask` on every touch drops the IRQ when a handler
  writes `STA $DC0D` before its own `LDA $DC0D`, and manufactures a second NMI
  edge when CIA2's mask is re-enabled.
- **The TOD is a clock, an ALARM, a read LATCH and a stop bit.** CRB bit 7
  selects which set of four registers a write lands in; a match raises ICR
  bit 2. Reading HOURS latches all four and reading TENTHS releases them, so
  a program cannot read 10:59:59.9 as 10:00:00.0. Writing HOURS **stops** the
  clock and writing TENTHS restarts it. The hours are **12-hour BCD with an
  AM/PM bit that toggles at 12**: `09 → 10` and `12 → 01` are the two carries
  out of the units digit.
- **CRA/CRB bit 4 is a STROBE and is stored as 0** (VICE stores
  `byte & 0xEF`), so a program that reads a control register back, ORs a
  start bit in and writes it does not force-load the timer a second time.

### 6.2 CIA2 (`$DD00-$DDFF`)

Timers, TOD, ICR, **PRA bits 0–1 = the VIC bank** (inverted, as the hardware
has them), and **NMI from timer underflow**. The NMI line is CIA2's ICR ORed
with RESTORE (§7.4).

**PRA is also the serial bus, and it is READ BACK, not stored.** Bits 3–5 are
ATN, CLK and DATA OUT; bits 6–7 are CLK and DATA IN. With no true drive VICE
answers `((PRA | ~DDRA) & 0x3F) | ((iec_fast_1541 & 0x30) << 2)` where
`iec_fast_1541` is `~(PRA | ~DDRA)` (`c64cia2.c`, `iecbus/iecbus.c`) — so
**CLK IN and DATA IN are the INVERSE of CLK OUT and DATA OUT**: an empty bus
reads back what this machine drives. Answering the raw register instead reads
DATA IN low, which is a device replying, and `LOAD"*",8` then waits for it
for ever instead of timing out (§11.3).

### 6.3 Three independent phase accumulators

One clock, three phases, none derived from another:

| accumulator | period | drives |
|---|---|---|
| the CIA timers | whatever their latches say (the KERNAL's PAL timer A is 16,421 cycles ≈ 60 Hz) | the jiffy IRQ, `TI$`, every timer interrupt |
| the VIC raster | 63 cycles a line, 19,656 a frame ≈ 50.123 Hz | `$D012`, raster IRQs, the frame counter the status bar prints (§10.2) |
| the CIA TOD | `C64_TOD_PERIOD` = 19,705 cycles — a **50 Hz** mains tick off the cycle clock | the TOD registers |

---

## 7. The keyboard — a level model

VICE's `keyboard.c` is a **level** model: a key is in the matrix while it is
down and leaves it when it comes up. So is this one. **There is no invented
hold time**, which is what makes RUN/STOP+RESTORE work, makes a game reading
`$DC01` work, and leaves the KERNAL's own repeat timing to the KERNAL.

### 7.1 The map

`apps/c64/c64kbd.c` carries `data/C64/gtk3_sym.vkm`'s 152 entries as two
static tables of `{row, col, flags}`: `c64_kasc`, keyed **by ASCII 32..126**
(95 entries; `{` and `}` are not in the `.vkm` and carry `C64K_NONE`), and
`c64_kscan`, keyed **by scan code for everything else**. From the `.vkm`,
verbatim: **Tab = C=**, **Escape = RUN/STOP**, **Page_Up = RESTORE**
(marked `C64K_RESTORE`, not a matrix position), and the `!LSHIFT` /
`!RSHIFT` / `!LCBM` / `!LCTRL` matrix positions. Home, Ins, Del, F1–F8, the
arrows, End (`←`) and Page_Down (`↑`) are all the `.vkm`'s.

### 7.2 The level model, on this OS — polled once per WAKE

`os88_onkey` delivers **presses only**. The state comes from the kernel's own
key-state map, `OSAPI_KEY_DOWN` (SPEC.md §9.7, wrapped as `os88_key_down`,
§15.1): `AL` = a make scancode, `CF` = down, every register kept.

Four rules, each of which exists because getting it wrong is silent:

1. **The map is armed ONCE, in `os88_main`.** `OSAPI_KEY_DOWN`'s first call
   clears and arms the map and always answers "up". Arming it from the first
   slice would erase the make `os88_onkey` had already seen — the first key
   of the session, lost.
2. **Host key state is polled ONCE PER WAKE and cached** (`c64_kbd_poll`,
   called from `os88_onwake` before the slice). Every emulated CIA1 read in
   that wake reads the cached 8×8 matrix. The bound is about twenty far calls
   at 46.7 µs — the down-list, the five joystick scancodes, Ctrl and both
   Shifts, and the ten digit scancodes only while Ctrl is down (§7.3) —
   under 1 ms, and it does not multiply by the number of slices in the wake.
3. **The matrix is REBUILT from the whole down-list every wake, never cleared
   incrementally.** Several `.vkm` mappings share the synthetic SHIFT, CTRL
   and C= bits, so clearing one key's bits can clear another key's.
4. **A fresh press is guaranteed one emulated KEYBOARD-SCAN INTERVAL in the
   matrix before release polling can clear it — and the unit is EMULATED
   CYCLES, not wakes.** `C64K_FRESH_CYC` is 20,000 6510 cycles: one CIA1
   timer-A period (16,421 at the KERNAL's `$4025`) plus margin, stamped from
   `c64_cyc_lo` when the press is added and checked at every poll. "One wake"
   is the wrong unit: a wake is one wall slice, 256..16,384 cycles, against a
   scan every ~16,421, so on a 4.77 MHz 8088 — where a 200 ms keypress buys a
   few thousand emulated cycles — a one-wake guarantee loses characters at
   random, and no emulator screendump can show it (PERFORMANCE.md's input
   overrun). The entry ages on the emulated clock and not on being read, so a
   program that never scans the matrix cannot make a key stick; the harness
   asserts a press whose host key is already up survives polls at 4,000
   cycles and is gone one poll after 20,001.

The **down-list is `C64K_DOWNMAX` = 16 entries** and its overflow path is
bounded: the 17th simultaneous key is dropped, not written past the end. The
key ring between `os88_onkey` and the poll is `C64K_RING` = 32.

**The modifier keys never arrive through `os88_onkey`**: a bare Shift or Ctrl
press produces no `int 16h` event, so the host's shift state is
`os88_key_down(KSC_LSHIFT) || os88_key_down(KSC_RSHIFT)` and nothing else.
**And a bare Shift and a bare Ctrl are matrix keys in their own right**:
`keyboard_latch_modifier_states` puts the left-shift and left-ctrl positions
into the matrix whenever the PHYSICAL key is down, with no other key needed —
a game polling `$DC01` for the shift key as a second fire button sees it. The
`.vkm`'s DESHIFT still wins, which is VICE's `!virtual_deshift` in the same
expression, and it is what makes SHIFT+letter the graphics character it is on
the machine: the shift comes from the host's own key, not from the ASCII the
BIOS folded it into.

**The map is advice, not an oracle** (SPEC.md §9.7). A key whose break code
the ISR missed stays in the matrix until its next press — a stuck key inside
a game is a possible symptom, and still strictly better than any invented
hold.

### 7.3 The folds the BIOS imposes, resolved on scan

An AT BIOS `int 16h AH=0` folds Ctrl+H, Ctrl+I and Ctrl+M onto BS, Tab and
CR. This port **routes those three on SCAN, not on ascii**:

| arrives as | scan | means |
|---|---|---|
| 8 | `0x23` | **CTRL+H** |
| 8 | `0x0E` | Backspace → INST/DEL |
| 9 | `0x17` | **CTRL+I** |
| 9 | `0x0F` | **Tab → C=** |
| 13 | `0x32` | **CTRL+M** |
| 13 | `0x1C` | Return |

`Ctrl+letter` otherwise arrives as 1..26 and is `CTRL+letter`.

**CTRL+digit — the colour and RVS codes — come from the poll**, not from a
chord: while `os88_key_down(KSC_CTRL)` is true, the ten digit scancodes are
polled in the same once-per-wake pass and their matrix bits set. So no VICE
Alt+digit chord is stolen to fake them (Alt+8/9/0/1 and Alt+3/4/5/6 stay
VICE's drive and printer captions, §11.2).

### 7.4 RESTORE, and RUN/STOP+RESTORE

**Page_Up = RESTORE** and raises an NMI. At that moment
`os88_key_down(KSC_ESC)` is read: Esc down means RUN/STOP is held, and the
KERNAL's own NMI handler warm-starts to `READY.` — the real machine's
behaviour arrived at the real machine's way, through the matrix.

### 7.5 The chords the target class cannot deliver

Three of VICE's bindings do not survive this hardware, and **the caption is
kept while the menu item is the guaranteed route**:

| chord | VICE's item | what happens here |
|---|---|---|
| Alt+F12 | Power cycle machine | an 83-key XT keyboard has no F12; the menu item is the route |
| Alt+Insert | Paste | an AT BIOS `int 16h AH=0` (the one in `ui_task`, `kernel/ui.inc`) drops the enhanced code `0xA2`; the menu item is the route |
| Alt+Delete | Copy | the same, code `0xA3` |

They work where the BIOS passes them and are captioned exactly as VICE
captions them either way.

Alt+D (Fullscreen) is not in that sense undeliverable, but it is the one
chord the port must dispatch itself whatever else it does: a `WF_FULL` window
has no menu bar, so it is the only way back (§9.8).

**Every other chord the menu advertises is dispatched by `os88_onkey`**: a
caption is not an accelerator in this kernel (SPEC.md §12.2's bar binds
none), so Alt+A, Alt+F9, Alt+Q, Alt+W, Alt+P, Alt+Shift+P, Alt+J, Alt+Delete
and Alt+Insert are recognised by scan code and routed to `os88_oncmd`, the
same helper a menu pick reaches, so a chord and a pick cannot drift apart.
Which of them a real AT or XT BIOS actually delivers has not been recorded
from the 86Box machines (§14.6); QEMU's SeaBIOS passes enhanced codes an AT
BIOS drops, so a QEMU reading says nothing about it.

### 7.6 The keyboard-mouse rule

SPEC.md §9.6 stands: **on a machine with no mouse the arrows, Space, keypad
0/5 and Del are the kernel's mouse; ScrollLock hands them back.** When that
is the case the status row's message area prints **`ScrollLock for joystick`**
— the fact, where the user is looking, instead of a joystick that silently
does nothing.

**No slot reports "no mouse has spoken"** (`osapi_mouse` tests `[mou_seen]`
to decide whether to poll the keyboard mouse and then answers x, y and the
button), and adding one spends kernel headroom. So the package asks a
question it CAN answer and that has the same answer: `kbm_key`
(`kernel/mouse.inc`) intercepts a cursor key only when no mouse has spoken
AND ScrollLock is off, and an intercepted key never reaches `os88_onkey`. So
*"the down-map says an arrow is held, and `os88_onkey` has never once
delivered an arrow"* **is** *"the kernel is eating them"*, observed rather
than inferred. Three consecutive polls are required before the row says
anything, because a wake posted before a press is dispatched ahead of the key
event queued behind it. `c64_arrow_typed` is a one-way latch. The message is
said **once a session** (SPEC.md §47), and `hosttest/c64uitest.c` carries
both the row and the negative control — a machine that DELIVERS its arrows
must never show it.

**The joystick itself works either way, which is why this is a hint and not a
refusal.** `kbd_track` runs inside the `int 09h` ISR, *above* everything the
UI task later decides, so `os88_key_down` reports a held cursor key on a
mouseless machine exactly as it does on one with a mouse. What the user loses
without ScrollLock is the desktop pointer, which every deflection drags
across the screen.

### 7.7 Copy and Paste — PETSCII in and out

**Edit > Copy (Alt+Delete)** and **Edit > Paste (Alt+Insert)** are VICE's two
clipboard actions (`src/arch/gtk3/actions-clipboard.c`), on os8088's system
clipboard (SPEC.md §55). Every conversion is transcribed from `src/charset.c`.

**Where the work runs, and why.** `ovl_cmd` (in `C64.OVL`) sets a latch —
`c64_copy_req` / `c64_paste_req` — and returns; `os88_oncmd` is dispatched
**under the gfx lock** (SPEC.md §12.8.3), and a thousand converted cells
there is the whole desktop stopped. The resident `c64_clip_service(base)`
(`c64kbd.c`) runs the whole body from the **top of the next wake**, with no
lock held. The 6510 advances only inside `os88_onwake`, so between the pick
and the top of the next wake not one emulated cycle has run: it is the same
screen, copied a wake later. A PAUSED or a JAMMED machine services the
request as well. Nothing per-byte crosses the overlay boundary — a loop that
crosses it pays a far call per iteration (§9.7's `C64COST_OVLCALL`) — so:

| the loop | where it runs | what it costs |
|---|---|---|
| the matrix out of the RAM claim | `c64_zcopy_out`, one call a ROW | 25 near calls |
| the row composed into the clipboard claim — table index, the reverse-video mask, the trailing-space trim, the `\n` and the store | **`c64_copy_row` (`c64mem.inc`), assembly, one call a ROW** | ~20 µs a cell + 24 µs a row |
| screen code → ASCII | `c64_sctab[128]`, a table | one indexed load a cell |
| host byte → PETSCII | `c64_pettab[256]`, a table | one indexed load a byte |
| the CRLF fold and the typing | `c64_paste_feed`, ten bytes a wake, no lock held | ~1.4 ms a wake |

`c64_copy_row` has its own case in `hosttest/c64memtest.asm` (section 4b):
the mask, the index, the store into ANOTHER SEGMENT, the trim, an all-spaces
row, a row whose last cell is not a space, and `n = 0`.

**Both tables are built once, in the overlay, on the first wake** —
`ovl_conv_init` (`c64cmd.c`), which is also §13.3's first `ovl_*` call. The
arrays are bss and stay resident and DS-relative, which is what makes the
move legal (SPEC.md §73.14: only code moves). A `c64_conv_ok` flag guards the
Edit arm of `ovl_cmd`, so a load refused for a transient reason and granted
later cannot reach the loops with 128 zero bytes of table.

**Copy is `clipboard_read_screen_output` (`src/clipboard.c`).** The 40×25
matrix is read a row at a time and each screen code goes through, in this
order: `charset_screencode_to_petscii` (`code & 0x7f`; `≤ $1F` +$40;
`$40..$5F` +$20), `petcii_fix_dupes` (`$60..$7F` → `$C0..$DF`),
`charset_p_toascii` with `CONVERT_WITH_CTRLCODES`, and unmappable PETSCII →
`.` (`ASCII_UNMAPPED`). Trailing spaces come off each row and each row ends
in one `\n`. Three consequences that a remembered version gets wrong:

- **An unmappable cell is `.`, not `?`.** `edit_copy_action`'s own pass
  replaces a byte only when it is neither a line ending nor printable, and
  `.` is printable — VICE's Copy output cannot contain a `?`. The cells that
  reach that arm are screen codes `$40`, `$5B-$5F`, `$60` and `$7B-$7F` and
  their reverse-video twins: the graphics cells, exactly what a PETSCII
  drawing is made of. The harness copies screen code `$40` and requires `.`.
- **The letters come back LOWER case**: `charset_p_toascii` maps PETSCII
  `$41-$5A` to `'a'-'z'` and `$C1-$DA` to `'A'-'Z'`, whatever character set
  the VIC is drawing, so a Copy of the boot screen puts
  `**** commodore 64 basic v2 ****` on the clipboard.
- **Copy says nothing on success**, because VICE says nothing.

A refused `os88_clip_put_seg` — over `CLIP_MAXKB`, or a heap that cannot fund
it — leaves the clipboard **empty rather than stale** (`kernel/clip.inc`) and
is said on the status row: `The clipboard refused the screen.` The claim
`clip_put` takes may compact the arena (`mem_compact`, then `mem_shed_one`),
and that term is not modelled in §9.7's figure; it is wake time, which is the
ordinary price of a heap claim, and not held-lock time.

**Paste is `paste_callback`**: the clipboard converted with
`charset_petconvstring(CONVERT_TO_PETSCII)` and handed to `kbdbuf_feed`. The
byte map is `charset_p_topetscii` — `'a'-'z'` → `$41+`, `'A'-'Z'` → `$C1+`
(deliberately not the `$61` duplicates), `` ` `` → `$27`, everything under
`$20` or at or above `$7B` → `?` (`PETSCII_UNMAPPED`). So a listing pasted in
lower case types as the BASIC keywords it looks like, and one in UPPER case
types the graphics characters `$C1-$DA` draw — a real machine's answer too.

- **Line endings are tested before the byte map, and CRLF is ONE line end**
  (`test_lineend`): CRLF, CR and LF each become a single PETSCII CR. The pair
  test is in the feeder and the single-byte answer is
  `c64_pettab[$0D] = c64_pettab[$0A] = $0D`, written over the byte map's
  `PETSCII_UNMAPPED` in `ovl_conv_init`.
- **The conversion happens ten bytes at a time in the feeder**, not in the
  command: ~145 µs a byte of SmallerC code, and the KERNAL's buffer holds ten,
  so that is the count the wake is bounded by.
- **The typing is the KERNAL's.** `kbdbuf_flush` puts nothing into the
  machine while its own buffer is not empty (`$C6 != 0`) and never more than
  ten bytes; the three numbers are `kbdbuf_init(631, 198, 10, …)` — the
  buffer at **`$0277`** (`C64_KB_BUF`), the count at **`$C6`** (`C64_KB_NDX`),
  ten (`C64_KB_SIZE`). VICE flushes once a frame; here it is once per wake,
  before the slice. A program that stops reading stops the paste.
- **A paste stops at an embedded NUL**, because that is what ends the string
  VICE converts (`while (*s)`); this kernel's clipboard is BYTES, so the test
  has to be explicit. What was produced before the NUL stands.
- **A reset empties the queue** (`kbdbuf_abort`, called from
  `machine_reset`): `c64_paste_stop` is the one place it happens here, and it
  is unconditional.

**Two staging areas, both transient heap claims of 2KB, separate on
purpose** — the queue outlives its command by however long the machine takes
to drink it, so one shared area would let a Copy taken during a long paste
rewrite what was still being typed:

| staging | bytes | what the number IS |
|---|---|---|
| Copy's claim (`C64_CLIPKB` = 2) | **1,026** = `40 × 25 + 25 + 1` produced into it (`C64_CLIPMAX`) — the screen, one line ending a row and the NUL, the largest thing `clipboard_read_screen_output` can produce. Taken and freed inside one wake |
| Paste's claim (`C64_PASTEKB` = 2) | **2,048** (`C64_PASTEMAX`) — what one `os88_clip_get_seg` can be handed; `OSAPI_CLIP_GET` has no offset, so the queue IS the staging. ~50 lines of a BASIC listing. Held from the command until the queue drains, and `c64_paste_stop` is the one place it goes back |

VICE's own queue is 16KB and this OS's clipboard ceiling is 32KB (SPEC.md
§55.2); 2,048 stands because a 16KB queue would be a 16KB claim a low-memory
machine can refuse for a paste of forty characters. **A clipboard bigger than
the queue is pasted as far as it fits and the truncation is said** —
`Pasting the first 2048 bytes.` — and `hosttest/c64uitest.c` compares the
message against `C64_PASTEMAX` so the two cannot drift. A claim that cannot
be had is said — `No memory for the copy.` / `No memory for the paste.` — and
the machine is untouched.

The feeder reads its queue with `os88_peek` over a **21-byte window** —
`C64_PFWIN = C64_KB_SIZE * 2 + 1`, because ten delivered bytes consume at most
twenty when every one is a CRLF pair, and the `+ 1` is the lookahead: without
it the pair test at the END of a window cannot see the `\n` after a `\r`, and
the stray `\n` becomes a second RETURN on the next wake.

**A full clipboard is not an empty one.** `os88_clip_size` answers `AX` and
the package's `int` is 16 bits; `clip_put` refuses only what is strictly above
`CLIP_MAXKB * 1024`, so a clipboard of exactly 32,768 bytes is legal and
arrives as `0x8000` = −32,768. The empty test is `sz == -1`, the only answer
that means empty and is not a length any put can have, and the size
comparison is unsigned. The harness drives a 32,768-byte clipboard, and its
stub casts through `short` so the host's 32-bit `int` cannot hide it.

**Both items are greyed by state** — §11.2's table.

---

## 8. The joystick

- **Port 2** is the cursor/numpad arrows with **Ctrl as fire**. This is
  **this port's choice**, and it is stated: VICE's `JoyDevice2` default is
  `JOYDEV_NONE`, but a keyset is the only joystick source this machine has,
  so `KeySetEnable=1` is the shipped state and Preferences > Allow keyset
  joysticks is shown **checked and disabled**.
- **Port 1** is empty.
- Read from the once-per-wake cached key state (§7.2) and presented as
  `$DC00`/`$DC01` bits 0–4 **active low** (up, down, left, right, fire).
- **Alt+J, Swap joysticks**, swaps the ports, as VICE's item does; the status
  row's two indicators swap with it (§10.1). The row says
  `Joysticks swapped.` / `Joysticks normal.`
- **A keyset key drives the stick and is not typed.** `keyboard_key_pressed`
  walks the ports mapped to a keyset, calls `joystick_check_set` and returns
  before `kbd_queue_pushkey` when one takes the key. Without it the four
  cursor keys drove port 2 AND entered the matrix: a game polling `$DC00` got
  phantom cursor presses out of `$DC01`, and moving the stick in BASIC walked
  the cursor. The consumption is applied where the matrix is built, so the
  entry stays in the down-list and its release is tracked by the ordinary
  poll. `c64_keyset` is VICE's `joykeys_enable`, the flag it tests first, and
  the harness turns it off to show the same key reaching the matrix again.
- **Consequently the four cursor keys do not move the BASIC cursor** — what a
  real machine with a keyset joystick on port 2 does. The C64's own two arrow
  KEYS, `←` on End and `↑` on Page_Down (§7.1), are unaffected.
- **Ctrl is both fire and the CTRL key, and it is the one departure from the
  consumption rule.** A BASIC user holding Ctrl to type a colour code also
  fires port 2. Stated, not fixed: §7.3's CTRL+digit and CTRL+letter paths
  need it in the matrix.

---

## 9. The screen

### 9.1 The window

Authored **`C64_W_W` = 338 × `C64_W_H` = `TITLE_H` + 226 + 1** at (7, 20)
for a **CONTENT box of 336 × 226**: 320×200 of C64 screen, an 8-pixel border
on every side, and a 10-pixel status row. The content width is `W_W − 2`
(`os88_wm_create` authors a FRAME and `os88_wm_geom` answers the CONTENT box,
two 1-pixel side borders narrower), and the content height is
`W_H − TITLE_H − 1`. `hosttest/c64uitest.c` asserts `C64_W_W − 2 == GW`. With
336 of content the status row is 42 whole cells (§10), the border is 8 on
every side, and the scroll rect's `x1` and `x2 + 1` are multiples of 8 by
construction. `os88_wm_snap` puts the content x on a cell boundary, which is
what lets `OSAPI_GFX_SCROLL` accept the rect (§9.4).

**226 of content is a 480-line number, and the window asks the adapter
before it authors one.** `os88_main` calls `os88_video()` and clamps the
height to `dock_top − 20`; a 200-line CGA desktop cannot give 226, and
authoring it anyway leaves `wm_fit` to clamp the window with the status row
— the row that carries §4.5's permanent jam line and every refusal — off the
bottom. The flush then reads the LIVE content box every time it runs
(`c64_geom`):

- the status row is at `content.y + content.h − 10`, whatever that is;
- the cell rows drawn are `(status_y − border − content.y − border) / 8`,
  clamped to 25;
- the bottom border fills between the last drawn row and the status row.

So a clamped window shows fewer C64 rows and keeps its status row.

### 9.2 Dirty tracking — a 256-page bitmap, set by the core

A shadow of the screen matrix and colour RAM cannot see a RAM character set
change (and could not see bitmap or sprite data, if those were composed), so
the core does the tracking:

- **A 32-byte, 256-page dirty bitmap** in the core's scratch (§3.5). Every
  RAM write ORs one bit into it — about five instructions, the stated
  per-write cost of this design (§3.4).
- The flush maps the dirty pages through the **frame registers as they
  currently stand** (`$D011`, `$D016`, `$D018`, the CIA2 bank) onto the cell
  rows those pages feed — screen matrix, character generator — and
  **composes only those rows**. A frame-register write dirties every row by
  itself. Colour RAM keeps its own window in the package, because colour RAM
  is not in the claim (§3.1).
- **The flush takes all of it in ONE call**: `c64_dirty_take`
  (`c64mem.inc`) is one `rep movsw` and one `rep stosw` over 36 bytes
  (`dst[0..31]` the bitmap, `dst[32..35]` the window), ~190 µs, and it resets
  the scratch in the same pass. Reading those bytes through `c64_scr_rd` one
  at a time was ~50 near thunks and ~1.8 ms a flush, before a pixel was
  decided. `hosttest/c64memtest.asm` case 3b runs it under `SS ≠ DS` and
  checks the 37th byte is a guard it never reaches.

**The core keeps a WRITE WINDOW beside the bitmap** — the lowest and highest
address written since the last flush, two scratch words. A 256-byte page is
6.4 character rows, so a page-granular bitmap alone can only ever say
*"recompose these seven rows, all forty cells"*, and §9.7's `one changed cell`
would be 28 ms instead of 4.2. **The window is taken over a WATCH RANGE**
(`C64_SCR_WATLO`/`WATHI`, `mbase .. mbase+999` — the screen matrix, written by
`c64_frame_regs` and re-written the moment `$D011`, `$D016`, `$D018` or the
`$DD00` bank bits move it): without the range, every `JSR` writes the stack
at `$01xx` and every BASIC statement writes zero page, so within one slice
the window spans every matrix row and buys nothing.

**The dirty rows of the matrix are the window INTERSECTED WITH the pages.**
The pages alone are 6.4 rows each; the window alone is one `lo/hi` pair, so
two pokes in distant rows — a score at row 0 and a status line at row 24 —
span every row between them (25 rows, ~299 ms). Row `i`'s forty bytes lie in
at most two pages, so row `i` is dirty when the window's row range covers it
**and** one of those pages has its bit set; the window then narrows the SPAN
inside each dirty row (`c64_span_of`). The two distant pokes cost the seven
rows of one page plus the six of the other — §9.7's `two pokes, rows 0 and
24`: 13 rows, 481 cells, 2 blits, and that row fails if the intersection is
dropped.

The counter the harness prints is **dirty pages per wake**. There is no
per-tick compare of a 2,000-byte shadow anywhere in this design.

### 9.3 The frame shadow, and the flush

**The shadow is the glass, not the model:** an **8,000-byte 1bpp frame
shadow** in bss (`C64_SHBYTES`) — 320 × 200 bits, exactly the pixels last
blitted.

The flush (`c64_flush`, `c64scr.c`) runs **at most once per
`c64_flush_every` host ticks** — 1, or 2 on `CPU_8086` (§9.8), or
`C64_WARP_FLUSH` = 2 under warp — never once per slice, and only when
something is dirty, a message is up, or the speed figures moved. It holds
the gfx lock **only** around itself, never around a slice, and under a clip
set by `os88_wm_clip_set`, because a wake is not a paint (SPEC.md §11.3).
Its order:

1. the message deadline, first thing, before any branch can return past it;
2. compose the dirty rows (§9.2) into 1bpp bands (§9.5);
3. test for a whole-frame shift first (§9.4);
4. otherwise compare each composed row against the frame shadow and **draw
   only the differing spans** — a span is `(first cell .. last cell)` and the
   composer and the blit both take it;
5. update the shadow with what was drawn;
6. border fills **only if the border's luminance level changed** or the
   shadow was invalid;
7. the status row, delta-drawn (§10).

Because the shadow is pixels, it validates the composer as well as the model:
if the composed row equals the shadow, nothing is drawn, whatever the model
believed.

**Two flags, meaning different things.** `c64_rowd` says THE SOURCES changed
— `0` clean, `1` a span inside the write window, `2` the whole row, which is
what a frame-register write sets. `c64_force` says THE GLASS is unknown, over
a **cell span** `c64_fc0..c64_fc1`: a scroll's vacated rows, a partial expose,
the rect an About panel has stopped covering. Setting both for a register
write makes every write to `$D011`, `$D016`, `$D018` or `$DD00` cost 25
forced full-width blits (~234 ms) with the compare switched off, and a raster
interrupt's `LDA #$1B / STA $D011` does that fifty times a second. So:

- **every one of those registers is guarded by VALUE** — a write that stores
  the byte already there returns without dirtying anything;
- **`$DD00` is guarded by its two BANK BITS**, because the KERNAL bit-bangs
  the serial bus through the other six and a `LOAD` from drive 8 was hundreds
  of full-screen repaints a second;
- a genuine change sets `c64_rowd = 2` and **not** `c64_force`: the row is
  recomposed and then compared, so a `$D016` write that draws the same
  picture composes 25 rows and draws nothing (§9.7's row).

`c64_force`'s cell span matters for the same reason: a menu closing over this
window is a damage rect about 190 px wide (`MENU_MAXCH` is 24 glyphs), and
forcing its rows full width composes 13 × 40 cells where 13 × 24 is the
answer. **The force path does not FILL first**: the composed band arrives in
final screen polarity, so filling the damage and blitting over it would be
PERFORMANCE.md rule 2's erase-then-letter pair.

### 9.4 The scroll test — a shift of **k = 1..24** rows

Flushing once per host tick means several rows can have scrolled since the
last one, so the shift test is not the one-row test:

- For `k = 1..24`, test whether cell row *i* matches shadow row *i + k*. The
  test is on a 16-bit **signature of the row's SOURCES** — its forty matrix
  bytes and forty colour nibbles, `c64_rowsig` (§9.5) — not on composed
  pixels, because composing 25 rows to find out is the cost the scroll exists
  to avoid.
- **The signature is a HINT and nothing rests on it.** After the scroll is
  emitted the shadow is moved with it and every row still goes through
  `c64_rowspan` against the moved shadow; a row that did not really shift
  compares unequal and is drawn. A collision is easy to build — `c64_rowsig`
  is an XOR under a per-cell rotate, so screen code 34 in column 5 against 36
  in column 6 over a blank row collide — and skipping the matched rows would
  leave a wrong row **that never repairs itself**, because after that flush
  nothing is dirty. `hosttest/c64uitest.c` drives that pair and checks the
  glass against the row's own sources. The signature saves the SCROLL, not
  the compare.
- **It is only asked when at least four fifths of the rows ON THE GLASS are
  dirty** (`C64_SHIFT_NUM`/`C64_SHIFT_DEN` = 4/5 — 20 of 25 on a full
  window). A FRACTION, because `nrows` is not 25 on a clamped 200-line window,
  where an absolute 20 is a threshold the screen can never reach. A scroll
  dirties the whole matrix by definition (the KERNAL moves 960 bytes and
  clears 40); a one-row change dirties the rows of two pages, fourteen of
  them, and at a threshold of eight the test found spurious matches on a
  screen with several blank rows.
- **The test, the compose loop and the re-signing all run over the rows ON
  THE GLASS, `0..nrows-1`.** The flush never writes `c64_shsig[]` past
  `nrows`, so a test over all 25 compares live signatures against power-on
  zeros and fails for every `k`.
- **Row signatures are updated only for rows the flush RECOMPOSED** (a row
  that was not recomposed did not change its sources), and **when the shift
  test has run, its signatures are READ rather than taken again** — the gfx
  lock is held for the whole flush, so nothing can have moved. Each of those
  is 25 × 1.08 ms saved on the path that recomposes everything.
- On a hit, emit **one `OSAPI_GFX_SCROLL` plus the `k` vacated rows** —
  `k + 1` calls, not 25. **The `dy` is POSITIVE, because positive moves the
  content up** (SPEC.md §5.5; `apps/runcpm/rcterm.c` passes `rc_scr_n << 3`).
  The harness's `gfx_scroll` stub implements both directions and all four
  refusals, and the `k = 9` step checks the pixels moved UP against a
  snapshot — a count cannot see a direction.
- `OSAPI_GFX_SCROLL` refuses when the clip does not contain the rect; on a −1
  the flush **falls back to spans and the shadow stays true**, as `rcterm.c`
  does.

**The gate asserts one scroll per FLUSH, not one per emulated line.** A
`FOR`-loop of `PRINT`s that scrolls nine lines between two ticks is one scroll
of nine rows.

### 9.5 The composers

`apps/c64/c64band.inc` — `rcband.inc`'s shape, **1bpp only**, every entry
point taking explicit `(segment, offset)` pairs for anything in a claim
(§3.6). `C64_BSTRIDE` = 40 and `C64_X2STRIDE` = 80 are assemble-time
constants and the eight rows are unrolled (PERFORMANCE.md Set 64):

| routine | does |
|---|---|
| `c64_band1(dst, first, last, mseg, moff, col, gseg, goff, mode, bg)` | composes cells `first..last` × 8 rows into a 1bpp band: text by glyph through the 16 `{and, xor}` pairs `c64_lum_update` builds (§9.6); **`mode != 0` fills the span with the background's level** (§5.1) |
| `c64_rowspan(a, b, n)` | compares a composed band against the frame shadow — eight pixel rows of `n` bytes at stride 40, both in the package's own segment — and answers `(first << 8) \| last`, the differing CELL columns, or −1 for "same". Both scans are `repe cmpsb` and the second sets `DF` on purpose, which is why §3.6's harness covers this file |
| `c64_rowcopy(dst, src, n)` | brings the shadow up to date with what was drawn |
| `c64_rowsig(mseg, moff, col, n)` | §9.4's shift test: a 16-bit signature of one row's SOURCES |
| `c64_x2init()` | builds `c64_x2tab`, the 512-byte byte → doubled-word table, once (`os88_main`) |
| `c64_band_x2(band, rows)` | pixel-doubles a band through that table, 8 or 16 rows deep, for fullscreen (§9.8) |

**The composer takes a span, never "always 40 cells."** A one-cell change
composes one cell — the difference between a keystroke costing ~4 ms and
~12 ms (§9.7).

The glyph bytes come from the **CHARGEN ROM in the claim** or from the RAM
character set the VIC is pointed at — never from `OSAPI_FONT_GLYPHS`. This is
a C64 face.

Each composed span goes down in **one `OSAPI_GFX_BLIT1`** (`os88_gfx_blit1`);
a −1 (a `kern_small` kernel, whose slot is a `stc`/`ret` stub, or a broken
argument) falls back to the font path — **and that is a DRAW, not a
deferral.** `c64_row_font` maps the row's screen codes to the kernel's own
face and emits the span as one `os88_font_run`, once, and the shadow is
updated with the composed band anyway: on this path the shadow becomes a
proxy for THE SOURCES, which is still what the compare needs. The two wrong
answers the harness gates against: discarding the −1 and updating the shadow
(a permanently blank screen the compare then refuses to repair) and keeping
the row OWED (recomposed and refused again, for ever). What the fallback
cannot carry is said rather than faked — it is not the C64's face, a
reverse-video cell is drawn plain, a graphics cell is drawn as a dot — so the
status row says `No bands here - text only.` the first time it happens.

### 9.6 Monochrome, by luminance, on every adapter — a fact, not a limitation being worked around

**The C64 screen is 1bpp through `OSAPI_GFX_BLIT1` on every adapter, VGA
included.** Every one of the C64's 16 colours is resolved to black or white by
a luminance threshold, relative to the background: `c64_lum_update`
(`c64scr.c`) builds 16 `{and, xor}` pairs — ink brighter than paper is the
glyph as it stands, paper brighter is the glyph inverted, and **equal
luminance is a uniform cell**, which is what the machine really shows and
what an XOR alone cannot express.

**The luminances are the VIC-II's own ladder** (`c64_lum[16]`), transcribed
from `vicii_colors_6569r5`'s Y column, the palette VICE 3.10 as shipped
compiles for a PAL C64 (§2). **It has NINE levels for sixteen colours, shared
in seven pairs**: blue = brown, red = dark grey, purple = orange, medium grey
= light blue, green = light red, cyan = light grey, yellow = light green. A
table derived from `data/C64/vice.vpl` — a file VICE does not read by default
— makes all sixteen distinct and shows contrast in seven places where a real
C64 shows a flat field. `tools/c64ref.py --lumcheck` checks the table over
all 256 ordered pairs.

The fact that decides 1bpp: **SPEC.md §5.4.1 — VGA keeps the span writer.**
`gfx_blit4` prices a band by its colour *runs*, at ~215 µs a run
(PERFORMANCE.md Set 44), and a C64 text band is ~1,000–1,600 runs — 0.2 to
0.35 s per band on the target and 0.3 to 0.5 s a frame even on a 386. A 4bpp
composer was priced out before it was written.

The kernel now publishes `OSAPI_GFX_BLIT1_PEN` (SPEC.md §5.4.2.2) — an ink
and a paper for the next `OSAPI_GFX_BLIT1`, on colour adapters only. This
port does not use it: colour stays greyed with this section's fact (§11.2),
and the EGA-16 map `c64_ega[16]` is kept in `c64scr.c` for the day a colour
composer is written against that slot (§15.3).

### 9.7 The cost — in milliseconds, not in calls

A call count hides the composer, which is where a C64 frame's cost actually
is: RUNCPM's measured band is ~860 µs a call + ~173 µs a cell, so 25 40-cell
bands are ~195 ms. **"25 calls" is therefore not an acceptance criterion.**

**The per-routine numbers come from `tests/c64band`** (§14.5, `make
c64bandbench`), under `-icount shift=3,sleep=off` where one PIT count is
0.359 ms of real 4.77 MHz XT (PERFORMANCE.md Part 4), N = 8 iterations a
row, **with a clip armed** on its rerun callbacks (they are `W_ONKEY` /
`W_ONCLICK`, not `W_PAINT`, and this package arms a clip in `os88_onwake`,
`os88_onclick` and `os88_about` for the same reason). The clip is 28 % of a
line of text: a 40-cell `font_run` is 718 counts unclipped and 922 clipped, a
320×8 `blit1` 40 and 47; the composer's own rows do not move, being package
code. The kernel arms NO clip for `W_PAINT` (SPEC.md §11.3 rule 3), so the
paint-path rows in the table below are an upper bound.

| the bench row | counts | per operation |
|---|---|---|
| `FONT_RUN` 40 aligned — the bar (41.4 ms against PERFORMANCE.md's model of 40 × 900 µs + 756 = 36.8; the 12 % is the clip) | 922 | 41.4 ms |
| `c64_band1` 40 cells | 168 | 7.54 ms |
| `c64_band1` 1 cell | 8 | 0.36 ms |
| `OSAPI_GFX_BLIT1` 320×8 stride 40 | 47 | 2.11 ms |
| `c64_rowspan` 40 equal | 35 | 1.57 ms |
| `c64_rowspan` 40 differing | 38 | 1.71 ms |
| `c64_rowcopy` 40 | 35 | 1.57 ms |
| `c64_rowsig` 40 | 24 | 1.08 ms |
| `c64_band_x2` 8 rows | 450 | 20.19 ms |

The bench saves ES around every blit (a callback is entered with
`ES = KERNEL_SEG` and must return it) and preflights `OSAPI_GFX_BLIT1`, so a
kernel that refuses bands prints `REFUSED (CF=1)` instead of timing a call
that draws nothing.

**The constants in `c64scr.c` that `hosttest/c64uitest.c` prices its table
from**, each with its arithmetic beside it in the source:

| constant | value | is |
|---|---|---|
| `C64BENCH_CALL` / `C64BENCH_CELL` | 175 / 184 µs | `c64_band1`'s call floor and one composed cell, solved from the two bench rows |
| `C64BENCH_SPAN` / `C64BENCH_SIG` | 1,571 / 1,077 µs | a 40-cell compare and one row signature |
| `C64BENCH_X2` | 20,190 µs | `c64_band_x2`, eight rows |
| a gfx call | PERFORMANCE.md's 756 µs floor + 3.4 µs a band byte | what the 320×8 row says a blit's pixels cost |
| a font call | 756 µs + 900 µs a glyph cell | PERFORMANCE.md's own arithmetic (756 + 78 × 900 = 70.9 ms against its ~71 ms line) |
| `C64COST_SCRACC` / `C64COST_TAKE` | 38 / 190 µs | a `c64_scr_rd`/`wr` thunk and the whole `c64_dirty_take` — **models**, from 11 µs a near call + the body's clocks at 0.21 µs on an 8-bit bus |
| `C64COST_OVLCALL` | 58 µs | one overlay bridge crossing: the 46.7 µs far call + the shim's 11 µs near call |
| `C64COST_ZCALL` / `C64COST_ZBYTE10` | 26 µs / 3.6 µs a byte | `c64_zcopy_out`'s shell and its bytes — the same per-byte figure prices `os88_clip_put`'s own `rep movsb` |
| `C64COST_CPCELL` / `C64COST_CPCALL` | 20 / 24 µs | one cell of `c64_copy_row` (eleven instructions, 22 bytes, fetch-bound at 4.34 clocks a byte) and that proc's shell once a row |
| `C64COST_PSBYTE` | 145 µs | one byte the paste feeder types — SmallerC's loop body, ~105 instruction bytes, counted off the generated assembly |

**The table the harness prints**, as it stands (`apps/c64/build.sh` prints it
on every build; a change that moves a row up is a regression against a
documented number, and this table, `c64scr.c`'s constants and the harness
change together or not at all):

| operation | measured, on the harness | gated on |
|---|---|---|
| one changed cell | **4.2 ms** | compose 1 cell; 1 blit call |
| one changed row | **12.5 ms** | compose `last − first + 1` cells; 1 blit call |
| a `k = 9` scroll | **258.4 ms** | 1 scroll + `k` drawn rows, **1 scroll per flush**; all 25 rows composed and compared (§9.4) |
| a `k = 1` shift the signature got WRONG | **256.9 ms** | 1 scroll, and the colliding row DRAWN: the glass matches the row's own sources |
| a `k = 3` scroll, `gfx_scroll` refusing | **302.0 ms** | spans, and the shadow stays true |
| two pokes, rows 0 and 24 | **127.2 ms** | 13 composed rows, **2 blits** — the window ∩ the pages (§9.2) |
| a full expose, 25 rows | **306.4 ms** | 25 composed rows + the border + the status row's 37 glyph cells |
| 25 rows changed, not a shift | **301.1 ms** | no scroll emitted |
| a `$D020`-only change | **3.5 ms** | fills only, no band composed — and the write must cross the background's level |
| a `$D020` change that keeps the LEVEL | **0.5 ms** | 0 fills, 0 composes — `c64_lum_update`'s flip test decides, not the write |
| a changed cell, `blit1` REFUSING | **33.3 ms** | §9.5's font path, ONCE, and the fact said |
| 8 × `$D011`, `$D016`, a `$DD00` serial edge | **0.1 ms** | 0 blits, 0 composes — a register write that changes nothing costs nothing (§9.3) |
| a `$D016` change that draws the same picture | **255.3 ms** | 25 composed rows, **0 blits, 0 fills** — recompose, then ask the shadow |
| an expose with the About panel up | **336.4 ms** | 11 composed rows: the rows the panel covers are not drawn under it. MORE than a full expose because the panel is 222 glyph cells, which is why the next row exists |
| an expose that misses the panel | **17.0 ms** | the panel is redrawn only when the damage rect reaches it |
| the About panel closing | **149.8 ms** | 14 composed rows — the panel's rect, not the screen |
| a `k = 1` scroll on a CLAMPED window | **154.5 ms** | 1 scroll + 1 drawn row, on 15 rows of glass |
| one joystick indicator changed | **1.4 ms** | one `blit1`, no fill — the status row's delta (§10.1) |
| a short message going up | **22.8 ms** | 1 fill of the row's first 25 cells only and 23 glyph cells — the joystick widget, drive number and lamps untouched (§10.1) |
| …and coming down again | **26.0 ms** | 1 fill of the same 25 cells and 24 glyph cells |
| a long message going up | **35.1 ms** | 1 fill of the whole row and 35 glyph cells — the negative control for the row above |
| the speed figures changed | **3.7 ms** | TWO `font_run` calls, one per changed NUMBER field, no fill (§10.2) |
| the same row with the delta switched off | **42.0 ms** | the negative control: 1 fill + 37 glyph cells |
| entering fullscreen | **306.4 ms** | one whole repaint, the kernel's own (§9.8) |
| the wake after entering fullscreen | **0.3 ms** | 0 blits, 0 composes — `OSAPI_FULLSCREEN` repaints synchronously, so the shadow already describes the new glass |
| entering fullscreen at 2× on VGA | **892.0 ms** | the same repaint with `c64_band_x2` under every band — the number that decides §9.8's tier table |
| the wake after entering fullscreen at 2× | **0.2 ms** | 0 blits, 0 composes |
| a `k = 3` shift at 2× | **126.6 ms** | 1 `gfx_scroll` with `dy = 48`, not 24, and 3 doubled rows |
| Edit > Copy of the whole screen | **28.6 ms** | 0 blits, 0 fills, **no lock held**: 25 row pulls, 25 `c64_copy_row` calls, 1,025 bytes through `clip_put`'s `rep movsb`, ONE bridge crossing. Not a total: the claim may compact the arena (§7.7) |
| Edit > Paste of 40 characters | **0.8 ms** | no lock held, and independent of the length: one `os88_clip_get_seg` and ONE bridge crossing |
| a wake typing ten pasted bytes | **1.4 ms** | `os88_onwake`, no lock: ten bytes converted and put in `$0277` |
| a wake with no tick boundary | **0.1 ms** | 0 (§9.3) |

Three decisions fall out of that table: **a changed cell is 4.2 ms and a
changed row 12.5**, which is the whole reason the composer takes a span and
the write window exists (§9.2); **a full repaint is ~300 ms, five host
ticks**, so the `CPU_8086` tier flushes every OTHER tick (§9.8); and
**`c64_band_x2` doubles the whole 40-byte row whatever the span**, so a
one-cell change in fullscreen costs the full 20.19 ms and the `CPU_8086` tier
does not magnify.

### 9.8 Fullscreen, and the tier table

`OSAPI_FULLSCREEN` (SPEC.md §11.2's latch) on **Alt+D**, both directions —
VICE's own binding. **This is a stated exception to SPEC.md §11.2.1**, taken
the way SPEC.md §74.2 takes Alt+F for a terminal: the C64 owns F and Esc, so
neither can carry the latch here. `os88_onkey` tests
`ascii == 0 && scan == C64_SCAN_ALT_D (0x20)` and calls the same resident
`c64_fullscreen_toggle` the menu item calls. It is resident and not in the
overlay: a `WF_FULL` window has no menu bar (SPEC.md §11.2), so the chord is
the only door back, and a door that had to load `C64.OVL` would refuse on a
disk without it with the refusal printed on a bar the user cannot see.

**2× ships, on every tier that can pay for it.** The frame shadow stays
320×200 and every compare, signature and span is in C64 pixels; **the
doubling happens at BLIT time and nowhere else.** `c64_scw` and `c64_sch` are
the GLASS pixels per C64 cell — 8 or 16 a side — decided in ONE place,
`c64_geom` (`c64scr.c`), and every cell↔pixel conversion divides by them.
**The two axes are decided separately, by "double it if the box can hold
it"**: width if the fullscreen box is at least 640 wide, height if it also
holds `25 × 16 + 2 × 8` lines above the status row. **One routine serves both
axes**: `c64_band_x2` always emits 2 × rows of `C64_X2STRIDE`, each a
duplicate of its neighbour, so X-only doubling is the same buffer read at
twice the stride. The doubled band is 1,280 bytes of bss, and bss rather than
a claim because **the flush cannot refuse** — a Copy that cannot get memory
says so; a flush has no such answer.

| adapter / tier | fullscreen |
|---|---|
| VGA 640×480 | **2× on both axes**, 640×400 centred, all 25 rows — the band composed 16 rows deep |
| CGA 640×200 | **2× horizontal only**, 640 wide exactly (a CGA pixel is 2:1, so 1:1 there is half as wide as it should be) — and §9.1's clamp gives **21 of the 25 rows** above the status row |
| Hercules 720×348 | 2× horizontal, 640×200 centred, all 25 rows, 1× vertical |
| the `CPU_8086` tier (`c64_can2x` = 0) | **1:1 centred**, whatever the adapter — §9.7's 892 ms against 306 |

- **The scroll still scrolls at 2×**: the rect and `dy` handed to
  `OSAPI_GFX_SCROLL` are glass pixels, `k × c64_sch` and `40 × c64_scw` wide;
  §9.4's multiple-of-8 rule holds at either scale because `c64_gsx` is snapped
  and `40 × 16` is 640. Falling back to bands whenever magnified would be 25
  doubled rows, about half a second, for what one scroll does.
- **The 1:1 border floor is applied at 1:1 only**: at 2× the margin is
  genuinely 0, and forcing 8 slides the picture and clips the last column.
- **2× has no font fallback, deliberately** — a doubled one would be a second
  renderer for the `kern_small` case. A refusal at 2× latches `c64_no2x`,
  throws the shadow away and **RETURNS from the flush** — not `break`, because
  the tail of `c64_flush` clears `c64_dirty_any` and sets `c64_sh_ok`, which
  must not stand for a screen that was not drawn — and the next wake lays the
  screen out at 1:1, where the font path is.
- **A refused `os88_fullscreen` owes a repaint, which the success arm does
  not**: `OSAPI_FULLSCREEN` repaints the window whole, synchronously, in both
  directions (`wm_fullscreen` raises with `AL = 1`, so `W_PAINT` runs nested
  inside the call), and a `c64_sh_inval()` on the success arm draws the
  identical picture again — 25 bands, ~300 ms, invisible in an emulator.
  On the refused arm nothing repaints, and `c64_fullscreen_toggle` has just
  taken the About panel down, so it sets `c64_dirty_any` and posts a wake.
- **The rest of the screen is a border fill**, and a toast raised while
  fullscreen goes to the status row as well: the bar a toast lands on is
  under a `WF_FULL` window, so every refusal in this port takes both routes.

**The pixel doubler is gated by the shipping text, because the host harness
MODELS it in C.** `c64_x2init` sat in the tree with nothing calling it and
answered 256 zeros (`shl bx,1` answers bit 15 in CF and the byte was in the
low half; and the pair was set between the two result shifts), and the C
transcription in `hosttest/c64uitest.c` was correct, so the first 2×
screendump was entirely black. `hosttest/c64memtest.asm` section 6b runs
`c64_x2init` and `c64_band_x2` under `SS ≠ DS`: five hand-computed entries
(`$00 → $0000`, `$FF → $FFFF`, `$80 → $C000`, `$01 → $0003`, `$55 → $3333`),
the doubled-nibble identity `(b & 0xAA) >> 1 == (b & 0x55)` over all 512
bytes (the identity alone passes an all-zero table; the entries alone miss a
pair at the wrong bit), the second scan line a copy of the first, and
`rows = 1` writing no third row.

The harness also gates the magnification itself: `c64_scw`/`c64_sch` read off
the computed geometry on a 640×480 box and a 640×200 one, every 2 × 2 block
of the magnified glass asserted uniform, the `k = 3` scroll's `dy` asserted
48, and two negative controls — the `CPU_8086` tier must not magnify, and a
refused `blit1` must come down to 1:1.

---

## 10. The status bar

One 10-pixel row under the screen, **336 pixels wide = 42 cells**, delta-drawn
— 336 being the CONTENT width, which is why the window is authored 338
(§9.1).

### 10.1 What is on it

**42 cells is the whole design constraint.** VICE's two speed strings are 12
cells each with their own field widths (§10.2), so 24 of the 42 are spoken
for before anything else is on the row. **The order is VICE's**:
`uistatusbar.c` appends the SPEED widget first, leftmost, then the joysticks,
then the drive units. In pixels from the content origin (`C64_ST_*` in
`c64scr.c`):

| x | field | width | |
|---|---|---|---|
| 0 | `%7.0f%% cpu` | 12 cells | the SPEED widget |
| 96 | `%8.1f fps` | 12 cells | |
| 192 | — | 1 cell | the one separator cell the row can afford |
| 200 | `Joysticks:` | 10 cells | `C64_ST_JOYX` — cell 25 |
| 280 | control port 1's `+` | 2 cells, one `blit1` | `draw_joyport_cb`'s five squares arranged in a `+` — up above, left and right beside, fire in the centre, down below; an OFF dot is its centre pixel rather than nothing, because a band carries no pen for SPEC.md §47's grey |
| 296 | control port 2's `+` | 2 cells, one `blit1` | |
| 312 | the drive number `8` | 1 cell | drawn plain, never lit (§10.3) |
| 320 | the warp lamp `W` | 1 cell, one `font_run` | §10.2 |
| 328 | the pause lamp `P` | 1 cell, one `font_run` | |

42 cells exactly — 41 of them fields. VICE's LEDs are on a different row from
the speed widget, so folding both onto one row is this port's decision.

**A dot is not a `gfx_fill`.** Ten dots drawn one fill each is 7.6 ms; each
five-dot group is composed into a band in the package's own RAM and goes down
in one call.

**The row delta-draws.** A full redraw — a fresh window, an expose, a message
going up or coming down — erases the row first, because those are the cases
where its pixels are unknown. Every other repaint draws only the field that
changed, over its own cells, with no erase: `font_run` and `blit1` both arrive
in final polarity. **`c64_status` is called from every flush and its own
compare is the gate**: it answers *"nothing moved"* in zero drawing calls.

**A message owns the row's 40 field cells — not the row** (`c64_say` clamps
to `C64_MSGCELLS` = 40; `hosttest/c64uitest.c` walks every `c64_say` literal
against it, and `apps/c64/build.sh` extracts the literals out of
`apps/c64/*.c` so a message this document does not know about is still
measured). **The two lamps at cells 40 and 41 are drawn under a message as
well**, because `P` is the indicator that reports the PAUSE state and
`Paused.` is a message. A message expires after about five seconds.

**A SHORT message owns only the cells it needs.** The joystick widget starts
at cell 25, so a message of **`C64_MSGSHORT` = 25** cells or fewer fills
`ox .. ox + 199` only, and the widgets right of it stay on the glass and keep
delta-drawing under it — which matters because `ScrollLock for joystick`
(§7.6) is raised BY the joystick, and a full-row erase blanked the two
indicators that report it. A longer message — `Pasting the first 2048 bytes.`
at 29 — owns all 40 field cells. `c64_slen` stops scanning at
`C64_MSGSHORT + 1`, because the only question asked of the length is
`≤ 25` and the scan runs on every flush while a message stands. Three flags
carry it: `c64_st_ok` (the row's pixels are ours — cleared by an expose, a
damage rect that reaches the row, and `c64_sh_inval`), `c64_st_lok` (the LEFT
field cells say what we last put there — what `c64_say` and `c64_jam` clear)
and `c64_st_blank` (a long message is standing where the widgets go, so the
flush after it comes down rebuilds them). The harness counts the lit pixels
in the widget's band before and after, with a 33-cell message as the negative
control.

**The messages**, each a fact: `The clipboard is empty.`, `Pasting the first
2048 bytes.`, `The clipboard refused the screen.`, `The clipboard refused to
be read.`, `No memory for the copy.`, `No memory for the paste.` (§7.7);
`No square voice here - no SID sound.`, `The speaker is busy - no SID sound.`
(§11.4); `Warp mode on.` / `Warp mode on - no change.` / `Warp mode off.`
(§4.4); `Paused.` / `Running.` / `Advanced one frame.`; `Joysticks swapped.`
/ `Joysticks normal.` (§8); `ScrollLock for joystick` (§7.6);
`No bands here - text only.` (§9.5); `Unable to load C64.OVL.` (§13.3);
`A file dialog is already open.`, `PRG too short.`, `PRG over 65533 bytes.`,
`PRG: no heap for it.`, `PRG: cannot read it.`, `PRG runs past $FFFF.`,
`Loaded <name>` (§11.3). Edit > Copy on success says nothing.

**A permanent row state is not a message.** `Main CPU: JAM at $XXXX` (§4.5)
is a LINE: it does not stop being true after five seconds, and a jam that
expired into the ordinary widget row — `0% cpu 0.0 fps` — is what an IDLE
machine looks like. The row's selector is: a message while one is up, then
the jam line, then the widgets. `c64_jam` raises `c64_dirty_any`, clears
`c64_st_lok` and toasts, and never touches `c64_msg`; routed through
`c64_say` the line went up as a message and was re-lettered identically when
the deadline cleared it — 1 fill + 22 cells that changed no pixel.

**The deadline is examined first thing in the flush**, before any branch can
return past it; there is exactly one writer of `c64_msg` (`c64_say`).

**A message is not a reason to ask for another wake.** A running machine
already asks, so its messages expire on the ordinary flush cadence. **A
machine the user stopped — paused or jammed — keeps its message until the
next event** (a keystroke, click, menu pick or expose, every one of which
flushes). With the message as a term of `c64_wants_wake`, a stopped machine
re-posted for the whole five seconds with nothing inside the wake — SPEC.md
§74.1's ~1,400 round trips a second at 693 µs each, ~4.8 s of the shared UI
task — and gating it on the flush's tick boundary instead answers 0 at the
moment the boundary has not arrived, so the message never comes down at all.

### 10.2 The speed widget — VICE's own strings, and what they count

`statusbarspeedwidget.c` prints `%7.0f%% cpu` (`CPU_DECIMAL_PLACES` 0) and
`%8.1f fps` (`FPS_DECIMAL_PLACES` 1). Both are folded onto this one row,
leftmost, e.g. `   100% cpu    50.1 fps`.

**The two literal tails are drawn once, and the numbers delta against the
glass.** `% cpu` and ` fps` are drawn only with the row itself; each numeric
field is compared against what was last drawn and re-run over the span
between the first and last differing cell (`c64_st_field`). A typical second
is one or two cells per field: §9.7's `the speed figures changed` is 3.7 ms —
TWO `font_run` calls, one per field, because both figures come from one fold
of one clock and the fixed `% cpu` tail at x = 56..95 sits between them —
against 42.0 ms for the whole-row path. This is the program's only redraw on
a TIMER, so it is the row that has to be gated.

**The widget's own delta is part of the flush gate**: `c64_pct`/`c64_fps10`
differing from what is on the glass is a reason to flush, because
`c64_dirty_any` comes from a RAM write and an ML poll loop on `$D012` writes
none — the figures froze exactly when somebody was looking at them.

- **cpu %** = emulated 6510 cycles delivered per second ÷ 985,248. 100 % is
  a real C64. Nothing throttles, so this number is the machine's honest
  output and the reason Preferences > Emulation speed is greyed (§11.2). On
  a 4.77 MHz 8088 (MartyPC, the 360KB disk on a CGA desktop) the row reads
  **`1% cpu`**, and the KERNAL takes about 1.3 billion host cycles — some
  280 s of guest time — to reach `READY.`
- **fps** = **emulated VIC frames** per second (§6.3's raster accumulator).
  50.1 is what a machine running at 100 % prints.
- **Flushes per second is a different number and is not on the bar** — it is
  a harness counter, capped at the host's 18.2 Hz tick by §9.3.

**Counters are two-word (lo/hi), folded once per `c64_run` call.** "No
`long`" is a C rule of SPEC.md §73, not a rule against 32-bit quantities: a
cycle count over a second overflows 16 bits on any machine faster than an XT,
and a 16-bit counter that laps produces small plausible numbers rather than
an error (CLAUDE.md's performance rule 3). The frame counter is ONE word,
deliberately: `fps` is the difference over a one-second window, taken masked
to 16 bits, exact for any window under 65,536 frames.

**The arithmetic**, because a percentage with no float in it is where a
plausible wrong number comes from. 985,248 cycles a second over 18.2 host
ticks is 5,413.45 cycles per hundredth of a tick, so

    raw = c64_div32(cycles_hi, cycles_lo, 5413)      one `div`
    % cpu     = c64_muldiv(raw, 10, elapsed_ticks)   one `mul`, one `div`
    fps × 10  = c64_muldiv(frames, 182, elapsed_ticks)

`c64_div32` and `c64_muldiv` are a few lines each in `c64mem.inc`; both
answer `0xFFFF` on overflow or a zero divisor rather than raising `#DE`. The
window is one second (`elapsed ≥ 18`) and is RESTARTED rather than published
if the wakes stopped for ten (`elapsed > 182`). Both figures are quantised by
the window — at 100 % over eighteen ticks `fps` reads 49.5 or 50.6 and only
settles on 50.1 over a longer one.

**The clamp is before the cast, on both fields.** `c64_div32` answers up to
`$FFFF` and `unsigned` is 16 bits: a quotient of 32,768 or more is already
negative by the time a `raw > 30000` test sees it, sails past the clamp, and
prints a flat `0% cpu` beside a live `fps`. Both clamps are applied while the
value is still unsigned, to 30,000 — not a small number: under QEMU the core
runs at some thousands of per cent, and a cap of 3,200 clipped an honest
figure into `1777% cpu` beside `1195.1 fps`. The two clamps saturate at
different emulated speeds and that is stated rather than hidden: `raw` at
30,000 caps `% cpu` at ≈ 16,666 %; `c64_fps10` at 30,000 is 3,000.0 fps, which
is 5,985 % on a PAL machine. The fps field is in tenths, so 16,666 % is 83,497
tenths and does not fit a 16-bit `int` at all — one field has to saturate
first. The harness drives 183,500,800 emulated cycles and the 9,335 VIC frames
that speed implies through both fields and asserts both read their cap.

**The warp and pause LEDs are two labelled lamps, `W` and `P`, inverted when
lit.** In VICE they are a separate row of the status bar, each with a text
label (`statusbar_led_widget_create("warp:", …)` / `("pause:", …)`). `warp:`
+ lamp + `pause:` + lamp is 13 cells and the row has 2 to give, so the lamp
and its label are folded into ONE GLYPH each: the letter is always drawn and
its cell is inverted, black on white, while the latch is on. It reads the
same on a 1bpp adapter as on VGA, which a grey would not (SPEC.md §39.4).

### 10.3 What is dropped rather than greyed

**Recording, Volume, CRT and Mixer** are not on the row at all, and neither
are the `Tape:` field and drive 8's track counter ` 18.5` — the drive NUMBER
stays, because that is the one of them a user looks for. The fact is the
width: 42 cells, minus 24 for the two speed strings, minus 10 for
`Joysticks:`, minus 6 for its two indicators and the separator, minus 1 for
the drive number, minus 2 for the lamps, leaves nothing. `Tape:` is 5 and
` 18.5` is 5.

**The drive number is drawn plain, not greyed.** `os88_gfx_pen(1)` buys a
checkerboard on a 1bpp adapter, and the checkerboard is laid in
`[gfx_disink]`, which `kernel/vga12.inc` sets to `CBLACK` for every content
pen because a window's content is white in both themes. **This row's paper is
black**, so a greyed glyph on it is a black stipple on black: gone on CGA and
Hercules, nearly gone on VGA. Inverting the cell is worse — on this row an
inverted cell means a LIT lamp, and a drive permanently lit says something
false. So the unit number is drawn, white, and never lights; that there is no
drive is carried where SPEC.md §47 wants it, on the greyed File > Attach items
(§11.2). VICE's `warp:` and `pause:` label TEXT is dropped by the same
arithmetic (§10.2).

---

## 11. The menus

### 11.1 The set, and what is live

**Exactly five** — the kernel's `MENU_APPMAX` — with `AM_NAME` = **`VICE`**:
**File (10 items), Edit (2), Snapshot (8), Preferences (11), Help (5)**.
VICE's Debug menu is `#ifdef DEBUG` in `uimachinemenu.c` and is **absent here
by VICE's own rule**, not greyed. Every item string is `uimachinemenu.c`'s
and every hotkey caption is a `hotkeys*.vhk` line, transcribed (§2); the VICE
line each string comes from is a comment beside it in `c64menu.c`.

**Two kernel limits shape every line of `c64menu.c`**: a pull-down is at most
`MENU_POPMAX` = **11 items** and each item is truncated to `MENU_MAXCH` =
**24 glyphs** (`kernel/menu.inc`; SPEC.md §39.2 — 11 is what a 200-line CGA
gives). Four rules follow:

1. **A section that is ENTIRELY unavailable folds into ONE item, and that
   item is the section's FIRST** — its submenu head label where it has one,
   otherwise the first item of the section, which is the action the section
   exists for. Never a word invented to summarise it, and never the section's
   last item (a greyed *Datasette controls* tells a reader looking for tape
   support nothing). A section with a LIVE item in it is not folded, which is
   why `reset_submenu` contributes three of File's ten slots. **The fold is
   per SECTION, taken to its end**: File's disk section (Attach disk image,
   Create and attach..., Detach disk image, Flip list) is ONE item, and
   Snapshot's six-item event section is ONE item. **The one exception, taken
   twice**: where the section's first item does not fit `MENU_MAXCH` the fold
   lands on the first that does — `Attach datasette image...` and `Attach
   cartridge image...` are 25 glyphs, so the tape section folds onto `Detach
   datasette image` and the cartridge section onto `Cartridge freeze`.
2. **The label is VICE's and is never shortened.** Where the label plus its
   `.vhk` caption passes 24 glyphs the CAPTION is dropped and the label
   stands alone; the chord is then in §11.2's table. `Power cycle machine`,
   `Load`/`Save snapshot image...`, `Quickload`/`Quicksave snapshot`,
   `Advance frame` and `Datasette controls` are the items this costs a
   caption. **One exception**: where dropping the caption would take away the
   only chord a LIVE item has, the SEPARATOR gives instead — `Reset machine
   CPU Alt+F9` is 24 glyphs with one space and 25 with two. **A caption is
   only taken from an item that has one**: a `UI_MENU_TYPE_SUBMENU` entry has
   no `.action`, so VICE prints no chord on it — `Attach disk image`, `Flip
   list` and `Printer/plotter` carry the label alone, not the Alt+8 / Alt+I /
   Alt+4 that belong to items inside them.
3. **A present-but-impossible item wears `OS88_MENU_DIS` and the fact that
   greys it is in a comment beside the string** (§11.2, SPEC.md §47) — and
   **nothing is LIVE that only toasts a refusal.** A greying is retired the
   moment its fact is.
4. **A CHECK item's state is a `*` in the label**, and the item pointer is
   swapped between the two spellings (`c64_menu_state`). Eight items are
   `UI_MENU_TYPE_ITEM_CHECK` in `uimachinemenu.c`: Fullscreen, Show
   menu/status in fullscreen, Warp mode, Pause emulation, Show status bar,
   Mouse grab, Swap joysticks, Allow keyset joysticks. This kernel's menu has
   no check mark and its face has no glyph for one; the `*` is
   `apps/tracker`'s idiom and not `apps/solitaire`'s `MENU_DIS` twin, because
   `MENU_DIS` is §47's *"you cannot have this"* and greying the item that is
   ON would make it impossible to turn off. A CHECK that is ON and cannot be
   turned off — `Show status bar`, `Allow keyset joysticks` — wears the `*`
   AND `MENU_DIS` together. The marker is two glyphs so both spellings are
   the same width; the longest, `* Pause emulation  Alt+P`, is exactly 24.
   The kernel reads the item strings through the set's `items` pointer at
   draw time, so swapping a pointer is enough and `os88_menu_set` is not
   called again.

**The order inside a menu is `ui_machine_menu_bar_create`'s.** Preferences is
`Fullscreen`, `Restore display state`, `Show status bar`, `Warp mode`,
`Pause emulation`, `Advance frame`, `Emulation speed`, `Mouse grab`, `Swap
joysticks`, `Allow keyset joysticks`, `Settings...` — 11, which is
`MENU_POPMAX` exactly, and why `Show menu/status in fullscreen` is folded onto
`Restore display state` even though that section has a live item in it: both
are greyed display-state items the kernel owns, and carrying it would make
12.

Live items:

| item | caption | note |
|---|---|---|
| File > Smart attach... | Alt+A | the Standard File dialog on `*.PRG` (§11.3) |
| File > Reset > Reset machine CPU | Alt+F9 | **and it CLEARS THE PAUSE, which is VICE's own order**: `machine_reset_action` calls `ui_pause_disable()` straight after `machine_trigger_reset()`. `c64_adv` is cleared with it and `c64_menu_state` re-runs. The body is a latch (`c64_reset_req` = 1) spent by `c64_reset_service()` at the top of the next wake, out of the desktop's lock |
| File > Reset > Power cycle machine | Alt+F12 | caption kept; the item is the route (§7.5). **RAM pattern fill: VICE's C64 factory pattern** (`src/ram.c`, `ram_init_with_pattern`) — the eight-byte period `00 00 FF FF FF FF 00 00` with every other 16K block inverted, **not zeros**; `Reset machine CPU` does not touch RAM. The latch is `c64_reset_req` = 2, because `c64_zfill` over 65,536 bytes is about a quarter of a second on the target and `os88_oncmd` runs under the gfx lock |
| File > Exit emulator | Alt+Q | **`os88_wm_close`, the kernel's own close path** (§15.2). Answered in the RESIDENT `os88_oncmd`, because it is the one command that must work on a disk whose `C64.OVL` is missing; `c64_exit_req` is set there and the top of `os88_onwake` spends it — sets `C64_ST_DEAD`, calls `os88_wm_close`, and returns with nothing after the call |
| Edit > Copy | Alt+Delete | §7.7. Where a BIOS passes the chord, `os88_onkey` dispatches scan `0xA3` — a code no unmodified key produces, so it cannot be confused with the C64's own Del (`0x53`) |
| Edit > Paste | Alt+Insert | §7.7. The chord is scan `0xA2`, against the C64's own Ins (`0x52`) |
| Preferences > Fullscreen | Alt+D | §9.8. CHECK: rule 4's `*` |
| Preferences > Warp mode | Alt+W | §4.4. CHECK: rule 4's `*`, and §10.2's `W` lamp |
| Preferences > Pause emulation | Alt+P | `c64_pause` toggles, `c64_sound_stop()` both ways, `Paused.` / `Running.`. CHECK: rule 4's `*`, and §10.2's `P` lamp |
| Preferences > Advance frame | Alt+Shift+P | **VICE's action, not this port's idea of one** (`src/arch/gtk3/actions-speed.c`): `if (ui_pause_active()) { vsyncarch_advance_frame(); } else { ui_pause_enable(); }` — from a RUNNING machine the item only PAUSES; from a paused one it runs to the next VIC frame end (§6.3) and stops, saying `Advanced one frame.`. The command raises a request; the slice driver serves it in the wall slices it already sizes and re-posts while it is outstanding. Greyed on a JAM (§11.2). **The chord reads the shift level**: the BIOS hands Alt+Shift+P the same ascii/scan pair as Alt+P, so `os88_onkey` asks `os88_key_down(KSC_LSHIFT/KSC_RSHIFT)` — without that the chord RESUMED a paused machine, the opposite of VICE in the only state VICE advances from |
| Preferences > Swap joysticks | Alt+J | §8. CHECK: rule 4's `*` |
| Help > About VICE... | | the kernel's name pull-down About opens the same panel (§12) |

### 11.2 Present and greyed — the fact that greys it (SPEC.md §47)

| item | the fact |
|---|---|
| File > Attach disk image (ONE item: the whole disk section folds into it, §11.1 rule 1; no caption — Alt+8/9/0/1 and Alt+I/K/N belong to items inside the submenus) | no 1541 in this build: a D64 needs the drive's directory walk and the KERNAL serial traps, and this port loads `.PRG` files only |
| File > Detach datasette image (the tape section; Alt+T captions kept) | no tape emulation: T64/TAP are not read |
| File > Cartridge freeze (the cartridge section; Alt+C / Alt+Z captions kept) | no cartridge port: the bank maps carry the cartridge-less 7 of VICE's 32 (§3.3) |
| File > Printer/plotter (no caption; Alt+3/4/5/6 are the `printer-formfeed-*` actions inside it) | no printer path in this OS |
| File > Activate monitor (Alt+H) | no monitor: VICE's is 30,000 lines of host C |
| File > Reset drive #8 | no drives |
| Snapshot > every item (load/save Alt+L/S, quick snapshots Alt+F10/F11, the event section folded onto `Start recording events` including the milestones Alt+E/U, media recording Alt+Shift+R/S, quicksave screenshot on Pause) | no snapshot format: a VSF carries every chip's state and this machine's chips are not VICE's |
| Preferences > Restore display state (Alt+R), Show menu/status in fullscreen | the window is os8088's: the kernel places it, and the bar is under a fullscreen window (SPEC.md §11.2) |
| Preferences > Emulation speed (200%..10%, Custom CPU speed), 50/60/Custom FPS | nothing throttles here: the machine delivers what the CPU can and the status bar prints it (§10.2; SPEC.md §74.4) |
| Preferences > Show status bar | the status row is the window's bottom row and is always drawn. A CHECK that is ON and cannot be turned off: `*` AND `MENU_DIS` (§11.1 rule 4) |
| Preferences > Mouse grab (Alt+M) | no 1351 mouse: the pointer is the desktop's |
| Preferences > Allow keyset joysticks (Alt+Shift+J) | the keyset **is** the joystick here; `*` AND `MENU_DIS` (§8) |
| Preferences > Settings... (Alt+O) | no resources file: every setting this port has is on the Preferences menu itself |
| Help > Browse manual, Command line options, Compile time features, Hotkeys | no manual on this floppy and no command line in this OS; the hotkeys are the menu captions |
| Machine model other than C64 PAL | one ROM set and one timing are carried: PAL 985248 Hz, 312 lines, 19,656 cycles a frame |
| SID voices 2–3, waveforms other than the gate, ADSR, filters, `$D41B`/`$D41C` | the PC speaker is one square wave, and the sound driver's FM path is not wrapped for C (§11.4) |
| Colour on the glass, VGA included | §9.6 — SPEC.md §5.4.1's span writer, ~215 µs a colour run, ~1,000 runs a text band |
| Bitmap and multicolour modes, sprites, collision registers | §5.1, §5.3 — the composer draws standard text; `$D01E`/`$D01F` answer 0 |
| Cycle-exact raster effects (fine scroll, mid-line colour changes, bad-line timing, FLD/FLI) | the VIC is serviced at raster-LINE granularity (§5.2) |
| Status bar: the `Tape:` field, drive 8's LED and track field | no tape, no drive (§10.3) |
| Power cycle (Alt+F12), Paste (Alt+Insert), Copy (Alt+Delete) **as chords** | §7.5 — the caption is VICE's and the menu item is the route |

**Two items are greyed by STATE rather than by the build**: the fact that
greys them is on the status row and it can stop being true, so
`c64_menu_state()` re-runs on every path that changes the state.

| item | greyed while | and the fact is |
|---|---|---|
| Preferences > Advance frame | `C64_ST_JAM` | §4.5's permanent line: `c64_advance_frame` answers a jam by doing nothing, and a live item that is a silent no-op is the shape SPEC.md §47 forbids |
| Edit > Paste | `C64_ST_JAM` **or `c64_pause`** | in either, nothing would ever drain the queue (§7.7). **The paused case is a deliberate departure from VICE**, which queues a paste on a paused machine and delivers it on resume: there the queue is drained by the vsync handler, here `c64_paste_feed` runs only in the RUNNING arm of `os88_onwake`. The pause's fact is not a message (`Paused.` expires) but §10.2's `P` lamp and the check beside Pause emulation |
| Edit > Copy | *nothing* | the frozen screen of a paused or jammed machine is real, and the body runs from the wake whatever the state |

**The chords are guarded with them.** `os88_onkey` dispatches Alt+Shift+P,
Alt+Delete and Alt+Insert itself (§7.5), and the kernel's *"a disabled item
is never dispatched"* does not reach a chord the package delivers: each tests
the item's own first byte for `OS88_MENU_DIS` before calling `os88_oncmd`.

### 11.3 Program loading

**File > Smart attach... (Alt+A)** opens the Standard File dialog on `*.PRG`.
`os88_file_dlg` answers −1 when another modal dialog already owns the screen,
and that is said — `A file dialog is already open.` — rather than swallowed.

`os88_onfile` — **resident**, because the runtime reaches a callback by a
near offset (§13.1) — refuses by size before touching the disk (`PRG over
65533 bytes.`; the ceiling is 65,533 because two bytes are the load address
and the rest must fit `$FFFF`), then calls the already-loaded `ovl_load_prg`
(`c64load.c`), which:

1. refuses a file under 3 bytes (`PRG too short.`);
2. takes a transient claim of `ceil(size / 1KB)` — computed as
   `(size >> 10) + ((size & 1023) != 0)`, because `(size + 1023) >> 10`
   wraps in 16 bits for a file over 64,512 bytes — or says
   `PRG: no heap for it.`;
3. `os88_file_read_seg` into it (`PRG: cannot read it.` on a short read) —
   a `.PRG` lands at any address, and `os88_file_read_seg` needs a
   512-aligned base (SPEC.md §2.1.1), which is why there is a claim;
4. reads the 2-byte load address and refuses a file whose LAST byte would
   pass `$FFFF` (`PRG runs past $FFFF.`) — the test is
   `n − 1 > $FFFF − load`, exact in 16 bits, and a file ending exactly at
   `$FFFF` is legal;
5. `c64_zzcopy_in` into the RAM claim at the load address, frees the claim,
   dirties every page (`c64_dirty_all`), and says `Loaded <name>`.

**That is all it does.** There is no autostart: no reset, no `READY.` wait,
no `mem_set_basic_text`, no `RUN`. The bytes are in the C64's RAM at the
address the file names, and the user types `RUN` (for a `$0801` BASIC
program) or `SYS`. The plan's wave 4 specified VICE's RAM-injection autostart
(`AutostartPrgMode=1`, `autostart-prg.c`) and it was never built.
(`OSAPI_FILE_READ_AT` exists unwrapped and offers nothing this needs — no new
slot, §15.2.)

**`LOAD"*",8` answers with the KERNAL's `?DEVICE NOT PRESENT  ERROR`.** That
is the honest machine with no drive, written here so nobody files it as a
defect. The ROM is the authority; transcribed from the machine, here and in
`README.TXT`:

```
LOAD"*",8

SEARCHING FOR *
?DEVICE NOT PRESENT  ERROR
READY.
```

**Two spaces before `ERROR`**, the KERNAL's own spacing. Getting there needs
CIA2 PRA modelled and not stored (§6.2): with the raw register answered, DATA
IN reads low — a device answering — and the KERNAL waits at `SEARCHING FOR *`
for ever.

### 11.4 Sound

**SID voice 1's frequency and gate** go to `os88_snd_tone` (`OSAPI_SND_TONE`)
once per wake, and only when they changed (`c64_sid_dirty`, raised by a write
to `$D400-$D41C`) — one far call.

**The hertz is one rounding, not two.** A 6581 at the PAL dot clock sounds
`Fn × 985248 / 2^24` Hz — 0.0587257 Hz a step. It is
`c64_muldiv(f, 3848, C64_SIDDIV = 0xFFFF)`: the product is kept in 32 bits
(§10.2's routine), and 3848/65535 = 0.0587166 is within 0.015 % of the real
constant across the whole range. `(raw >> 4) × 15 / 16` rounds twice and near
the bottom of the range is a whole hertz out — `F = 341` answered 19 where the
true figure is 20, which is the difference between the 20 Hz floor refusing
the note and playing it. The harness asserts both ends of the range and the
floor. (The divisor is spelled `0xFFFF` because SmallerC's `int` is 16-bit
signed and a decimal 65535 is *"Constant too big for 16-bit signed type"*.)

**A held note does not survive a stop.** The tone is played with duration 0,
which SPEC.md §34 holds until something takes it down, and the only thing
that ever did was the guest closing the gate. `c64_sound_stop()` is the one
place it comes down — on pause, a JAM, a reset, the About panel and entering
WARP (VICE's `sound_suspend` under warp) — and it raises `c64_sid_dirty`, so
the wake re-reads the CURRENT SID registers on the way out and plays what the
machine actually holds rather than remembering the note it took away.

**The capability is established before the slot is called**, once, in
`os88_main`: `os88_snd_caps() & SND_CAP_TONE` (SPEC.md §73.11). Every machine
this OS boots answers yes (`kernel/snd.inc` ORs `SND_CAP_TONE` in
unconditionally), so the guard is not a path a user will meet; the harness's
stub can clear the bit, and then the SID latch is dropped rather than left
raised and the fact is said once a session: `No square voice here - no SID
sound.`

**A refused grant is not permanent.** `os88_snd_tone` answers −1 when another
instance holds the speaker (SPEC.md §34.3). The latch is cleared only when the
grant takes; the retry is **bounded at `C64_SID_TRIES` = 8 wakes** and then
dropped, and the next SID register write re-arms it. The give-up is said once
a session, on the row and in a toast: `The speaker is busy - no SID sound.`
Voices 2 and 3, every waveform beyond the gate, ADSR and the filters are
greyed with §11.2's fact: `OSAPI_SND_FM` and the streaming path are driver
verb protocols not wrapped for C.

---

## 12. The About panel

`ovl_about_show` in `c64about.c` — **`C64_ABT_ROWS` = 9 rows**, modal, the
machine **paused while it is up**, its close drawn as damage and not as a
repaint (the `rcabout.c` shape; the close and hit test stay resident in
`c64.c`). Reached from **Help > About VICE...** and from the kernel's own
About item alike (`about_set`).

```
About VICE
The Commodore 64 Emulator
VICE 3.10
os8088 port: PAL
Copyright 1996-2025, VICE team
GPL-2 or later - see COPYING
ROMs Copyright Commodore
Business Machines
Ported by Jorge Gonzalez
                                    [ OK ]
```

The rows are `uiabout.c`'s title and model string, `configure.ac`'s
`vice_version`, and VICE's `README` lines 186–290. **The fourth row says what
the port is and nothing about how it renders**: PAL is a fact about the
emulated machine; `1bpp` and `no drive` belong in this document and in the
greyed items that name them (`.claude/skills/port-to-os8088/LESSONS.md` 8).

**The panel is `C64_ABT_W` = 336 wide — the C64 screen and its border —
snapped to the cell grid, and that is a redraw decision.** 336 makes *"the
rows the panel covers"* exact horizontally, so `os88_paint` skips them
entirely (`c64_hold_r0`/`c64_hold_r1`) and nothing under the panel is drawn;
narrower, an expose composed all forty cells of every covered row and then
painted the panel over them. `os88_paint` checks the horizontal cover rather
than assuming it, because a window narrower than 336 clamps the panel. The
height is rounded UP to a whole cell and the origin DOWN onto the grid
(`c64_scw`/`c64_sch`-sized, §9.8): `c64_hold_r0/r1` are
`(y − screen_y) / 8` and C truncates toward zero, so an unsnapped panel left
the partly covered row at each end held but uncovered, and `WF_OWNBG` means
nobody whitens the strip.

**It is redrawn only when the damage reaches it.** The panel is 1 fill + 2
frames + 10 `font_run` calls over 222 glyph cells; redrawing it on every
paint made an expose with the panel up cost more than the full expose the
hold rows exist to beat. §9.7: 336.4 ms for a whole expose with the panel up
(11 composed rows of 25), 17.0 ms for one that misses it.

**`ovl_about_show` answers a status and the latch is that answer.** 0 means a
refused overlay load, which `c64cmd.c`'s rule calls a normal path; latching
`c64_abt = 1` over it left `os88_onwake` returning early with no panel on the
glass and `os88_paint` holding rows for a panel that does not exist.

**Its close is `c64_blank_rect` over the panel's own rect**, not
`c64_sh_inval`: §9.7's 149.8 ms, 14 composed rows, against ~271 for all 25
rows, the border and the status row.

It must fit CGA's ~136-row framed content box with OK inside the panel and
the panel inside the content box (the constraint RUNCPM's panel is measured
against in SPEC.md §74.4): the OK sits at `6 + 9 × 10 + 11` = 107, and both
the panel's width and height are clamped to the live content box with the OK
button clamped inside the panel.

---

## 13. The budget

SPEC.md §73's cap is **61,440** for resident image + bss, and SPEC.md §73.9's
split trigger is **55,000 resident**. Five figures are reported: resident
image, bss, `C64.OVL`, resident shims, largest frame — the lines `make c64`
prints.

### 13.0 The measured line

From a clean `make c64` of the tree as it stands:

```
cc8086: build/c64.raw.asm: 92 function(s), 34 frame byte(s) max
cc8086: overlay - 4 function(s) moved to .modc, 4 entry vector(s), 20 resident shim(s), 5 loading call site(s)
os88ovl: build/c64.bin -> build/c64.trim.bin (41426 resident) + build/C64.OVL (2149 on demand)
os88pkg: part 0 compressed lz4: 20480 -> 17361 bytes (84.8%), margin 1
os88pkg: 'C64' entry=+0x0060 image=41426 bss=13190 icon=yes assoc=0
```

| | measured | of |
|---|---|---|
| resident image | **41,426** | |
| bss | **13,190** | |
| **resident total** | **54,616** | **61,440** — 6,824 spare, and **384 under §73.9's 55,000 trigger**. This is the margin |
| `C64.OVL` | **2,149** | on demand: `ovl_conv_init`, `ovl_cmd`, `ovl_load_prg`, `ovl_about_show` — the only four functions in `.modc` |
| resident overlay shims | **20** | counted inside the image |
| largest C frame | **34 bytes** | of the 96 SPEC.md §73 allows |
| the file | **58,833** | image + `C64.OVL`'s trim + the 17,361-byte compressed part (§1.4) |

The next feature worth a few hundred resident bytes will need a split first
(SPEC.md §73.14), and what is resident is resident by nature — every
callback, the core, the composer and the flush — so the split will be a real
one. Where the bytes went, wave by wave, is the plan's record.

### 13.0.1 The line, by wave

The wave-2 and wave-3 size lines, and what each fix pass moved, are in
`docs/plans/completed/C64-PORT-PLAN.md`'s wave records; §13.0 is the only
line this document keeps, because a figure here that is not the tree's is a
figure somebody will quote.

### 13.1 The file split

| file | holds | resident |
|---|---|---|
| `apps/c64/c64.c` | the translation unit's root: the GPL-2 + VICE header, prototypes, the `C64_*` constants, the key ring, `os88_main` (the RAM claim, `os88_part_seg(0)`, the sound capability, `c64_x2init`, the tier, the register files, the RAM pattern, the scratch, the keyboard map armed, the window, the five-menu set, `about_set`, `onwake` installed and the first wake kicked), `os88_paint`, `os88_onkey` (the chords), `os88_onclick`, `os88_onfile` (§11.3), `os88_oncmd` (Exit answered here, everything else through `c64_ovl_ready` to `ovl_cmd`), **`os88_onwake` — the slice driver** (§4.4) with the latches it spends before the slice — `c64_exit_req`, `c64_reset_req`, `c64_copy_req`/`c64_paste_req` — and the `#include`s in order. **There is no `os88_worker`** | yes |
| `apps/c64/c64io.c` | the `$D000-$DFFF` register files and the cdecl dispatch the core calls (§3.4); the alarm scheduler `c64_alarm_next` and `c64_advance` (§4.4); VIC, SID, colour RAM, CIA1/CIA2 with their TOD, the IRQ and NMI lines, the `$00`/`$01` port and the bank-map index (§3.2) | yes |
| `apps/c64/c64kbd.c` | the two `gtk3_sym.vkm` tables, the cached matrix and the 16-entry down-list, the once-per-wake rebuild, the scan-routed Ctrl+H/I/M, the Ctrl-held digit poll, RESTORE with the Esc read, the joystick keyset, the ScrollLock hint, both of Copy's and Paste's loops and the two conversion tables they index (filled by `ovl_conv_init`), `c64_clip_service` and the `$0277` feeder (§7.7) | yes |
| `apps/c64/c64scr.c` | the dirty-page → cell-row mapping, the 1bpp frame shadow, the flush, the `k`-row shift test, `c64_geom` and the tier table, the luminance and EGA-16 tables, the border fills, the status row, the cost constants of §9.7 | yes |
| `apps/c64/c64menu.c` | the five menu tables with every string and caption, the `OS88_MENU_DIS` greying with its fact in a comment beside it, the menu-set struct, `c64_menu_state` | yes |
| `apps/c64/c64cmd.c` | `ovl_conv_init` (§13.3's first-wake probe and the two PETSCII tables) and `ovl_cmd`, every menu command's SHELL: the file dialog, the reset latches, the clipboard latches, fullscreen, warp, pause, advance frame, swap joysticks. No per-byte loop lives here (§7.7) and File > Exit is not here at all | **no** |
| `apps/c64/c64load.c` | `ovl_load_prg` (§11.3) | **no** |
| `apps/c64/c64about.c` | `ovl_about_show` (§12) | **no** |
| `apps/c64/c64cpu.inc` | the 6510 core (§4) | yes |
| `apps/c64/c64mem.inc` | the movers and the scratch accessors (§3.6) | yes |
| `apps/c64/c64band.inc` | the composers (§9.5) | yes |
| `apps/c64/c64.asm` | the shim: `CC_PKG_NAME 'C64'`, `CC_HAS_ONKEY`/`ONCLICK`/`ABOUT`/`ONWAKE`/`MENUS`/`FDLG`/`OVL`/`PARTS` (no `WORKER`, §15.2), `CC_ICON`, `%include cc/crt0.asm`, the one-row parts table (§1.4), `c64.gen.asm`, then the three `.inc`s, `CC_IMAGE_END` | yes |
| `apps/c64/icon.inc` | the 16×16 1-bit breadbin, drawn for this port | yes |
| `apps/c64/COPYING` | VICE's GPL-2 text (§1.2) | — |
| `apps/c64/README.TXT` | the disk's README (§14.2) | — |
| `apps/c64/rom/` | the three ROM binaries + `README.md` (§1.3) | — |
| `apps/c64/hosttest/` | `c64uitest.c`, `os88.h` (its stub SDK), `c64memtest.asm` + `.sh`, `c64cputest.asm` + `.sh` (§14.5) | — |

Every file in the table is a written prerequisite in the Makefile: make
cannot see through a `#include` or a `%include`.

### 13.2 The planning figures

The plan estimated 39,000 image + 13,600 bss + 6,000 overlay, from RUNCPM's
measured ~6.3 bytes per line of C (`docs/plans/completed/C64-PORT-PLAN.md`,
*Budget*). The machine came in at §13.0's figures: the core assembles at
about 6.5KB against the ~9.6KB budgeted from `rcz80.inc`, because every
addressing mode is a `call c64_ea_*` and every access a `call c64_rd_bx`; the
overlay is a third of the estimate because the frequency split (SPEC.md
§73.14) leaves only the once-per-pick shells out there; and bss carries an
8,000-byte frame shadow, 1,024 nibbles of colour RAM, the 1,280-byte doubled
band, the two conversion tables and the register files, with the clipboard
staging in transient claims instead (§7.7).

### 13.3 The overlay rules that bind here

- **Split by FREQUENCY, never by size** (SPEC.md §73.14): a keystroke's path
  stays resident, a menu command's goes out.
- **Every callback is resident** — `os88_paint`, `os88_onkey`, `os88_onclick`,
  `os88_onwake`, `os88_onfile`, `os88_oncmd`, `os88_about` — because the
  runtime reaches one by a near offset. A callback that needs overlay code
  calls an already-loaded `ovl_*` helper.
- **A LOCKED callback never crosses into `C64.OVL` unless the module is
  already resident.** Reaching an `ovl_*` makes the runtime RESOLVE the
  module, and if it is not resident that is an `OSAPI_MEM_CLAIM` and an
  `OSAPI_FILE_READ` — a floppy seek, ~400 ms — inside whatever context asked.
  `os88_oncmd`, `os88_about` and `os88_onfile` are dispatched under the
  desktop's gfx lock, so `c64_ovl_ready(win)` is the fence: it refuses, says
  `Unable to load C64.OVL.`, clears `c64_ovl_asked` and kicks a wake — and the
  WAKE, which holds no lock and may call the file slots by contract (SPEC.md
  §74.1), is what retries the load.
- **The `.OVL` cannot be loaded from `os88_main`** — there is no instance yet
  to resolve a module against. The first `ovl_*` call is made **from the first
  wake**, and it is `ovl_conv_init()`: the far call the runtime makes on the
  way in is what loads the module, so the probe is asked at the first moment
  there is an instance, rather than discovered when a user picks a menu item.
  Its refusal prints `Unable to load C64.OVL.` on the status row *and*
  toasts, because a toast under a fullscreen window is not where the user is
  looking (§9.8). Every body in `c64cmd.c` returns 1, so a 0 from a wrapper
  never came from one of them.
- **The probe does the port's once-per-launch work while it is there**:
  `c64_sctab` and `c64_pettab` (§7.7) — the one thing in this program that
  runs exactly once. The tables are bss and stay resident; only code moves.
- `C64.O88` and `C64.OVL` are **two files in one folder** on every disk they
  share (SPEC.md §19.2.1, SPEC.md §19.9) — §14.2.

---

## 14. Names, disks, targets, machines and harnesses

### 14.1 Names

| | |
|---|---|
| package name | `C64` |
| source | `apps/c64/` |
| shipped files | `C64.O88` (with the ROM as part 0, §1.4), `C64.OVL`, `README.TXT`, `COPYING` |
| window title | `VICE (C64)` |
| menu-set `AM_NAME` | `VICE` |
| images | `build/c64.img` (1.44MB), `build/c64720.img` (720KB), `build/c64120.img` (1.2MB), `build/c64360.img` (360KB) |
| make targets | `c64` (the host checks, then the package), `c64disk`, `c64rom`, `c64memtest`, `c64cputest`, `c64bandbench`, `xt-c64` / `286-c64` / `386-c64` / `286-525-c64` |
| tools | `tools/c64rom.py` (the ROM part, §1.4), `tools/c64ref.py` (the reference compositor, §14.5), `tools/c64dec.py` (the decimal-mode reference, §4.6) |

### 14.2 Disks

Four geometries, each `os88disk.py --verify`'d in the recipe, each carrying
the same five files in one `C64/` folder: `C64.O88`, `C64.OVL`, `README.TXT`
naming the licence and the ROM copyright, and **`COPYING`, the licence text
itself** — the floppy is the distributed form of a GPL-2-or-later program and
`README.TXT` says the text travels with it, so it has to. Measured:

| geometry | clusters used |
|---|---|
| 1.44MB | 166 of 2,847 |
| 720KB | 85 of 713 |
| 1.2MB | 166 of 2,371 |
| 360KB | 85 of 354 |

If a later change puts something else on the 360KB image and `COPYING` no
longer fits, the rule is **the licence stays and the other thing goes**.

**No `.PRG` programs ship.** VICE's tree contains none to ship and every
candidate needs its own licence check. A released C64 disk boots to `READY.`
and the user types.

On `apps-all.img` the package gets **a folder of its own, `C64\`, never a
place in `APPS/`** (SPEC.md §19.9, SPEC.md §19.10) — the same five files, for
the same reason.

### 14.3 The three 86Box machines

Each is a **copy of a machine that has booted**, with the B: image
(`fdd_02_fn`) and the uuid changed and nothing else (86Box substitutes a
default for an unrecognised key and rewrites the config on exit; `git
checkout` the cfg before committing and never commit `nvr/`).

| machine | copied from | B: | target |
|---|---|---|---|
| `vm/xt-c64` | `vm/xt-runcpm` — IBM XT (`ibmxt86`), 8088 at 4.77 MHz, 640KB | `build/c64360.img` | `make xt-c64` |
| `vm/286-c64` | `vm/286-runcpm` — AMI 286 at 12.5 MHz, 3.5" DD | `build/c64720.img` | `make 286-c64` |
| `vm/386-c64` | `vm/386-runcpm` — 386DX at 25 MHz | `build/c64.img` | `make 386-c64` |
| `vm/286-525-c64` | `vm/286-525` — the AMI 286 with 5.25" HD drives | `build/c64120.img` | `make 286-525-c64` |

The fourth is the 1.2MB geometry's machine (SPEC.md §19); the section title
predates it. **`RESET=1|cmos|flash|both` clears a stale CMOS on the way in.**
**These machines are MANUAL evidence** (§14.6): a `make` target that launches
86Box cannot assert that anything booted, and no gate may rest on it.

### 14.4 The fixtures

There is no `.PRG` fixture writer: `tools/c64prg.py` was planned for wave 4
and does not exist. `hosttest/c64uitest.c` pokes its own screen contents into
its RAM model, `tools/c64ref.py` renders from a state dump the harness writes,
and `hosttest/c64cputest.sh` fetches Dormann's binary at a pinned SHA-256.

### 14.5 The harnesses and benches — automated evidence

| | what it does |
|---|---|
| `apps/c64/hosttest/c64uitest.c` | the whole program over a stub `os88.h` (`hosttest/os88.h`) with a **PIXEL** model of the glass — `gfx_blit1` writes real pixels, and `gfx_scroll` moves them and fills the vacated rows with garbage, which catches a flush that trusts a stale shadow for a vacated row. After every step it asserts, pixel for pixel over the whole 320×200 screen, that the glass shows what the shadow says; it prints §9.7's cost table and the dirty-pages-per-wake counter; it dumps the machine and the composed frame for `tools/c64ref.py`; it **enforces the clip** — a pixel written outside an armed region while no clip is armed fails with its coordinates; it models `os88_wm_close` as the deferred close it is; its stubs can refuse the clipboard, clear the TONE bit, refuse `blit1`, refuse `gfx_scroll` and model the target's 16-bit widths through `short`. It is compiled with **`-DC64_HOST`**, which keeps the cost counters out of the shipping image: nothing in `apps/c64/*.c` reads one. **The assembly half cannot run on the host**, so the routines it substitutes are transcriptions — `c64ref.py` checks the ALGORITHM, and the 8086 encoding is gated by `c64memtest.sh`, by `tests/c64band`'s identity rows and by a boot. Run by `build.sh` before every build |
| `tools/c64ref.py` | **an independent, pixel-level reference compositor**, written from VIC-II documentation and VICE's `src/vicii/`, not from `c64band.inc`: it renders the same C64 memory to a 320×200 1bpp image, and `--check` compares it bit for bit against what the package composed — twice, once with the CHARGEN ROM and once with a RAM character set; `--selftest` injects a one-bit defect and requires the compare to fail; `--lumcheck` checks the package's 16-byte luminance table against `vicii_colors_6569r5`'s Y column over all 256 ordered pairs (§9.6). It renders standard text, which is what the composer composes |
| `apps/c64/hosttest/c64cputest.asm` + `.sh` | §4.6's twelve rows with their negative controls — `make c64cputest`, minutes, not in `build.sh` |
| `apps/c64/hosttest/c64memtest.asm` + `.sh` | §3.6 — `c64mem.inc` and `c64band.inc`'s cross-segment entry points under `SS ≠ DS` with an `ES` sentinel, in raw QEMU: the movers, `c64_dirty_take` (3b), `c64_copy_row` (4b), `c64_rowspan`/`c64_rowcopy`, `c64_band1` composing out of both claims and `c64_rowsig` signing out of one, `c64_x2init`/`c64_band_x2` (6b), and four negative controls — ES, DF, BP and DS. Run by `build.sh` |
| `tests/c64band` | `make c64bandbench` — the icount bench pricing `c64_band1` per cell and per call, `c64_band_x2`, `c64_rowspan`, `c64_rowcopy` and `c64_rowsig`, with `FONT_RUN` as the bar and identity rows that compose a line through the shipping `c64_band1` and compare it against the kernel's own lettering. §9.7's per-routine numbers are written from it. It arms the clip on its rerun callbacks, saves ES around every blit, and preflights `OSAPI_GFX_BLIT1` |
| `tests/c64part.py` | the soak row for the ROM part (§1.4): `wants=build/c64.img`, so it needs the C toolchain |
| MartyPC / QEMU | `tools/os88ui.py` (`boot`, `open_drive("B")`, `path("C64/C64.O88")`, `menu_pick`) for anything on an 8088; `make test TESTAPPS=build/c64.img` with `tools/mouse.py`, `tools/qmp.py sendkey` and `tools/shot.py` for the 286/386 cases (docs/TESTING.md) |

### 14.6 Manual evidence — and the line between them

**Automated evidence** is everything in §14.5: it runs unattended and it
fails a build or a tier.

**Manual evidence** is the four 86Box machines (§14.3) and anything read off
them by a person: which chords a real AT or XT BIOS delivers (§7.5), the
`% cpu` and `fps` figures on a 386 and on an XT, the look of a scroll, the
feel of keystroke latency. It is recorded as a reading with its date and
machine, and it is never a gate.

---

## 15. What this port adds to the kernel and the SDK

**Four thunks — and nothing in the kernel.** No slot is added and no kernel
`.text` moves: each is a wrapper in `apps/cc/os88thunk.asm` + `apps/cc/os88.h`
(and the `hosttest/os88.h` stub, in the same edit) over a slot the kernel
already publishes.

| thunk | slot | the fact that needed it |
|---|---|---|
| `os88_key_down` | `OSAPI_KEY_DOWN` `0x03F0` | §15.1 — the level keyboard |
| `os88_wm_close` | `OSAPI_WM_CLOSE` `0x0470` | §15.2 — the worker idiom closes the WINDOW, not the APP |
| `os88_clip_put_seg`, `os88_clip_get_seg` | `OSAPI_CLIP_PUT` `0x0320` / `OSAPI_CLIP_GET` `0x0328` | §7.7 — clipboard staging in a CLAIM rather than in bss; both slots already take `ES:SI` / `ES:DI`, so the thunk loads `ES` from its argument instead of from `DS` |

### 15.1 `os88_key_down` — the level keyboard's state

| | |
|---|---|
| **need** | key STATE for the level keyboard model (§7.2), the joystick (§8) and CTRL+digit (§7.3). `os88_onkey` delivers presses only |
| **slot** | `OSAPI_KEY_DOWN` — SPEC.md §9.7. `AL` = a make scancode, `CF` = down, every register kept; **asking is what arms it, and the first ask clears the map** (§7.2's rule 1); advice, not an oracle. The kernel tracks every break code (`kbd_track`, `kernel/mouse.inc`, `KBD_MAPSZ` 16 = all 128 make codes) |
| **shape** | `int os88_key_down(int scan)`, CF → 1/0 |
| **what it cost** | 18 bytes in every C package: `nasm -f bin` has no dead-code elimination, so a thunk nobody calls is still image, which is why the SDK's thunk file is a place to be careful |

### 15.2 Slots used as they stand — no change

| need | slot | note |
|---|---|---|
| a slice loop on the UI task, no blocking, file slots legal | `OSAPI_WM_WAKE` `0x0450` / `OSAPI_WM_ONWAKE` `0x0458` (`CC_HAS_ONWAKE`) | used exactly as RUNCPM does (SPEC.md §74.1); `os88_main` posts the first kick itself, because `os88_wm_onwake` installs the handler and does not post |
| a time base for the flush and the speed widget | `os88_ticks()` — the 18.2 Hz tick | the machine's own clock is emulated cycles (§4.2); `OSAPI_WM_TIMER` stays unwrapped, the wake is the re-post |
| the CPU tier that seeds the wall slice and the tier table | `OSAPI_CPU_INFO` `0x0188` | §4.4, §9.8 |
| fullscreen on Alt+D | `OSAPI_FULLSCREEN` `0x0110` | §9.8 |
| a scroll moved, not redrawn | `OSAPI_GFX_SCROLL` `0x01F8` | §9.4; falls back to spans on −1 |
| a composed span down in one call | `OSAPI_GFX_BLIT1` `0x0418` | §9.5; glyphs come from CHARGEN or the RAM charset, not `OSAPI_FONT_GLYPHS` |
| voice 1 | `OSAPI_SND_TONE` `0x00E8`, `OSAPI_SND_CAPS` `0x00E0` | §11.4 |
| a self-close for Exit emulator | `OSAPI_WM_CLOSE` `0x0470`, wrapped as `void os88_wm_close(void *win)` | the worker idiom every other C package uses — `os88_wm_destroy` under the lock, then `os88_task_alive` outside it — **closes the WINDOW and does not close the APP**: `wm_destroy` frees the record and nothing repaints the dock, so Alt+Q left a dead tile on the dock strip. The close BOX was always clean because it goes through the kernel's own `app_close_win`, which is the path this slot asks for. The package has no worker and no task slot. It is spent from the WAKE and not from `os88_oncmd`: the contract is *call it and RETURN, do not draw afterwards*, and `os88_oncmd`'s own tail kicks a wake that would flush into a window that is going away |
| the ROM | `os88_part_seg(0)` — `op_seg` in `apps/os88parts.inc`, package code and no slot (SPEC.md §20.12) | §1.4 |
| a `.PRG` read to an arbitrary address | `OSAPI_FILE_READ_AT` `0x0358` exists unwrapped | **no new slot**: §11.3 does it with a scratch claim and `c64_zzcopy_in` |

### 15.3 The slot that is NOT added

**Colour.** The kernel has since published `OSAPI_GFX_BLIT1_PEN` (SPEC.md
§5.4.2.2), an ink and a paper for a 1bpp band on a colour adapter, so the
slot this section once said did not exist now does. This port does not use
it: a colour C64 composer is a per-row colour decision the composer does not
make yet, and the speed case on the target is unmeasured. The feature stays
greyed with §9.6's fact, and the EGA-16 map and per-row colour record are
kept in `c64scr.c` so that a composer against that slot drops in without
re-planning.
