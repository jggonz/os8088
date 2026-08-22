# C64 — a Commodore 64 emulator, written in C (`apps/c64/`)

**The binding contract for the C64 package.** It stands to `apps/c64` as
SPEC.md §74 stands to `apps/runcpm`: every symbol, constant, string, register
answer and layout the port depends on is pinned here, and the change goes in
here *before* it goes in the code. The design record — waves, budgets, risks,
the user's decisions and the review that reshaped them — is
`docs/C64-PORT-PLAN.md`.

It lives outside SPEC.md by the user's instruction. **A bare `§N.M` in this
file is a section of THIS file**; SPEC.md's sections are always cited in full
as `SPEC.md §N`, and other documents by name (`CONTRIBUTING.md §6`,
`C64-SPEC §4.3` from outside). `tools/checkdocs.py` resolves all three.

---

## 1. What it is, and the attribution

### 1.1 The port

`apps/c64/`, package name **`C64`**, is a native reimplementation of **VICE
3.10's `x64`** — the fast, non-cycle-exact C64 machine — as a **windowed
Commodore 64**: a 6510 running in a 64KB heap claim, the KERNAL, BASIC and
character ROMs read from a sidecar file into a second claim, a VIC-II and two
CIAs written in the C this toolchain compiles, and the 320×200 screen composed
into 1bpp bands and blitted into a window.

It is a port in SPEC.md §73.12's sense, and in SPEC.md §74's: **the behaviour
is VICE's, taken from its source and not from memory; the code is
reimplemented in the C subset of SPEC.md §73 plus the hot loops that are
hand-written 8086; what cannot carry is present and greyed with the fact that
greys it** (SPEC.md §47). Nothing of VICE's *source* is vendored — every file
carrying derived tables, strings or behaviour names the VICE file in its
header and carries the GPL-2-or-later attribution.

The structural shape is RUNCPM's (SPEC.md §74.1): **no worker task and
nothing blocking.** The machine runs on the UI task in wake-driven wall
slices (`OSAPI_WM_WAKE` / `OSAPI_WM_ONWAKE`), and a C64 sitting at `READY.`
costs nothing until a key or a tick arrives.

### 1.2 Licence and attribution

VICE is **GPL-2-or-later**, © 1996–2025 the VICE team. Therefore:

- `apps/c64/` **is GPL-2-or-later**. The rest of the tree is not, and the file
  headers, the About panel (§12), `apps/c64/COPYING` and the PR body all say
  so.
- `apps/c64/COPYING` is VICE's GPL-2 text, verbatim (~18KB, in the repo, not
  on the floppy). Every derived file's header cites it by path.
- The disk's `README.TXT` names the licence and points at `apps/c64/COPYING`;
  the release zip carries `COPYING` (`.claude/skills/release-os8088`).
- The About panel carries `Copyright 1996-2025, VICE team`, `GPL-2 or later`
  and the ROM copyright line, from VICE's `src/arch/gtk3/uiabout.c` and
  `README` lines 186–290.

### 1.3 The ROMs — a sidecar, and a stated departure from `CONTRIBUTING.md` §6

**The three Commodore ROM binaries are committed under `apps/c64/rom/`.**
This is a **user-decided departure from `CONTRIBUTING.md` §6** ("nothing
third-party is committed"), taken deliberately and for those three files
only, so that the build does not depend on a VICE checkout being present
beside this repo:

| file | bytes | SHA-256 |
|---|---|---|
| `apps/c64/rom/kernal-901227-03.bin` | 8,192 | `83c60d47047d7beab8e5b7bf6f67f80daa088b7a6a27de0d7e016f6484042721` |
| `apps/c64/rom/basic-901226-01.bin` | 8,192 | `89878cea0a268734696de11c4bae593eaaa506465d2029d619c0e0cbccdfa62d` |
| `apps/c64/rom/chargen-901225-01.bin` | 4,096 | `fd0d53b8480e86163ac98998976c72cc58d5dd8eb824ed7b829774e74213b420` |

They are **the C64 defaults VICE 3.10 itself picks**, not a guess:
`src/c64/c64-resources.c:378` sets `KernalName` to `C64_KERNAL_REV3_NAME`
(`kernal_revision = C64_KERNAL_REV3`, `:74`), `:381` sets `BasicName` to
`C64_BASIC_NAME` and `:375` `ChargenName` to `C64_CHARGEN_NAME` — the three
names spelled out in `src/c64/c64rom.h:52`, `:31` and `:60`.

`apps/c64/rom/README.md` states, and must keep stating: **the ROM images are
Copyright © Commodore Business Machines**, they are neither GPL nor ours, and
they are distributed here exactly as VICE distributes them in `data/C64/`.
Nothing else third-party is committed.

### 1.4 `C64.ROM` — the sidecar file

`tools/c64rom.py` concatenates the three into `build/c64-rom/C64.ROM` with a
**fixed layout** and checks each input's SHA-256 against §1.3's table before
it writes:

| offset | length | contents |
|---|---|---|
| `0x0000` | 8,192 | KERNAL (`kernal-901227-03.bin`) |
| `0x2000` | 8,192 | BASIC (`basic-901226-01.bin`) |
| `0x4000` | 4,096 | CHARGEN (`chargen-901225-01.bin`) |
| | **20,480** | total — 40 sectors, exactly 20KB |

20,480 is 512-aligned and 1KB-exact, so the file is read straight into the
base of a 20KB claim with `os88_file_read_seg` and no scratch buffer
(SPEC.md §2.1.1's alignment rule is met by construction). **The ROMs are not
embedded in the package** — on paper an embedded build was over 64,000 bytes
against SPEC.md §73's 61,440 cap, refused before a line was written.

`C64.ROM` is a **shipped file, not a fetched one**: it is built from
committed inputs, so `make` produces it on any checkout with no network and
no VICE tree.

**A disk without `C64.ROM` refuses at launch**, naming the file — the machine
is not started and the refusal quotes the file name, or
`os88_mem_largest_kb()` when the claim rather than the file was what failed
(§3.1).

---

## 2. Where the behaviour comes from — the authority table

Every user-visible surface names ONE VICE file. Paths are relative to the
**VICE 3.10 source tree**; the tree is the *reference*, never a build
dependency (§1.3).

| surface | source |
|---|---|
| menu bar: File / Edit / Snapshot / Preferences / Help (Debug is `#ifdef DEBUG` in VICE and absent here by VICE's own rule) and every item string under them, submenus folded into their head item; Help > About VICE... is the live item (`uimachinemenu.c:988`) and the kernel's name pull-down About opens the same panel; the menu-set `AM_NAME` is `VICE` | `src/arch/gtk3/uimachinemenu.c` (`ui_machine_menu_bar_create`, the `.label` fields) |
| hotkey captions on EVERY item, live or greyed: Alt+A smart attach, Alt+F9 reset CPU, Alt+F12 power cycle, Alt+Q exit, Alt+D fullscreen, Alt+W warp, Alt+P pause, Alt+Shift+P advance frame, Alt+J swap joysticks, Alt+Shift+J keyset toggle, Alt+Insert paste, Alt+Delete copy, Alt+H monitor, Alt+L/S snapshots, Alt+F10/F11 quick snapshots, Alt+E/U milestones, Alt+Shift+R/S media, Pause screenshot, Alt+R restore display, Alt+B decorations, Alt+M mouse grab, Alt+O / Alt+Shift+O settings, Alt+C / Alt+Shift+C / Alt+Z cartridge, Alt+T / Alt+Shift+T tape, Alt+I/K/N/Shift+Alt+N fliplist, Alt+8/9/0/1 drive attach, Alt+3/4/5/6 printer formfeed — every binding in `hotkeys.vhk` and its `!include`s, transcribed, so a greyed item is never captioned from memory | `data/hotkeys/hotkeys.vhk` + every `!include`'d `hotkeys-*.vhk` (drive, tape, cartridge, printer, ...) |
| keyboard map: PC key → (row, col, shiftflag) of the 8×8 matrix, the `!LSHIFT`/`!RSHIFT`/`!LCBM`/`!LCTRL` positions, RESTORE on Page_Up, Tab = C=, Escape = RUN/STOP | `data/C64/gtk3_sym.vkm` (152 entries; transcribed whole to a static table keyed by ascii 32..126 and by scan code for the rest) |
| the keyboard's LEVEL model: a key is in the matrix while it is down, released when it goes up | `src/keyboard.c` (`keyboard_key_pressed`/`keyboard_key_released` → the matrix `c64cia1.c` reads); os8088 side: SPEC.md §9.7 `OSAPI_KEY_DOWN`, `kernel/mouse.inc:1438` `kbd_track` (break code clears the bit) |
| the 16 colours and their order | `data/C64/vice.vpl` (to 1bpp by luminance on every adapter in this port; the EGA-16 map kept in `c64scr.c` for the day colour bands exist — §9.6) |
| window title `VICE (C64)` | `src/arch/gtk3/ui.c:1842` (`"VICE (%s)"`, `machine_get_name()`) + `src/c64/c64.c:179` (`machine_name = "C64"`) |
| status bar: message area, `Tape:` (greyed), `Joysticks:` two 5-dot indicators, drive 8 with its track counter ` 18.5` (greyed), the speed widget's two labels `%7.0f%% cpu` and `%8.1f fps` (`CPU_DECIMAL_PLACES` 0, `FPS_DECIMAL_PLACES` 1) folded onto one row, warp and pause LEDs as two dots (SPEC.md §47 pen when off); Recording, Volume, CRT and Mixer dropped with the row-width fact stated in §10.3 | `src/arch/gtk3/uistatusbar.c` (line 1403 track counter, 1961/1971 volume, 2594 recording, 2670/2679 CRT/Mixer), `src/arch/gtk3/widgets/statusbarspeedwidget.c:572`, `:653`, `:467`, `warp_led_set_active`/`pause_led_set_active` |
| About box: `About VICE`, `The Commodore 64 Emulator`, version 3.10, `Copyright 1996-2025, VICE team`, GPL-2-or-later, the ROM copyright line | `src/arch/gtk3/uiabout.c` (title, model string `Commodore 64`), `configure.ac` (`vice_version` 3.10), `README` lines 186–290 (copyright notice, "The ROM files ... are Copyright © by Commodore Business Machines"), `COPYING` (copied verbatim to `apps/c64/COPYING`) |
| machine model: C64 PAL, 985248 Hz, 63 cycles/line, 312 lines, 50.12 Hz (the default; NTSC greyed) | `src/c64/c64.h:35-40`, `src/c64/c64model.c` (default model), `src/arch/gtk3/c64ui.c` (model names) |
| 6510 opcode semantics incl. illegal opcodes, BCD, **per-opcode cycle costs with the page-cross and taken-branch penalties**, IRQ/NMI entry | `src/6510core.c`, `src/c64/c64pla.c`, `src/interrupt.c`; the oracle is Klaus Dormann's `6502_functional_test` + `6502_decimal_test` (fetched, pinned SHA, never committed) |
| the `$00`/`$01` processor port: DDR semantics, the data port's read-back, and which write re-banks | `src/c64/c64pla.c`, `src/c64/c64mem.c` (`zero_read`/`zero_store`, `pport`) |
| bank maps (which of RAM/BASIC/KERNAL/CHARGEN/IO each 4KB region shows for each `$01` value with `!exrom=!game=1`) | `src/c64/c64meminit.c`, `src/c64/c64mem.c` (`colorram_read`/`store`, the `$01` read-back bits) |
| **the event/alarm model**: "run to the next device event, then service it" | `src/alarm.c`, `src/maincpu.c` (`maincpu_clk`, the alarm context) |
| CIA register file, timer A/B modes, ICR semantics, TOD, CIA1 PRA/PRB keyboard+joystick read, CIA2 PRA VIC bank bits | `src/core/ciacore.c`, `src/core/ciatimer.h`, `src/c64/c64cia1.c` (`read_ciapa`/`read_ciapb`), `src/c64/c64cia2.c` |
| VIC-II registers `$D000-$D02E`, the five legal modes + idle/illegal-black, screen/char base from `$D018` and the CIA2 bank, raster compare, IRQ flag/mask, sprite registers | `src/vicii/vicii-mem.c`, `src/vicii/viciitypes.h`, `src/vicii/vicii-draw.c`, `src/vicii/vicii-timing.h` |
| PRG autostart: VICE's RAM-injection mode (`AutostartPrgMode=1`, the only one possible without a drive; VICE's default is `AUTOSTART_PRG_MODE_DISK`, `autostart-prg.h:45`): 2-byte load address, inject, `mem_set_basic_text(start, end)` on EVERY load (`autostart-prg.c:383`, not only at `$0801`), wait for `READY.` on screen, type `RUN\r` into `$0277` with count at `$C6` | `src/autostart-prg.c:354-395`, `src/autostart-prg.h`, `src/autostart.c` (`check("READY.")`, `AutostartRunCommand`), `src/c64/c64mem.c` (`mem_set_basic_text`), `src/kbdbuf.c` |
| joystick bits: `$DC00`/`$DC01` bits 0–4 active low (up, down, left, right, fire); "Swap joysticks"; port-2 keyset default is THIS PORT's choice (VICE's `JoyDevice2` default is `JOYDEV_NONE`, `joystick.c:2092`; `KeySetEnable=1` at `:553` matches "shown checked") | `src/joyport/joystick.c`, `src/c64/c64cia1.c` |
| SID register file (`$D400-$D41C`, voice frequency/control/gate), `$D41B`/`$D41C` read-backs | `src/sid/sid.c` (register semantics only; no synthesis carries) |
| icon: a 16×16 1-bit breadbin drawn for this port, not copied | `data/common/vice-x64_16.png` is the REFERENCE LOOK only; `apps/runcpm/icon.inc` is the format precedent |
| the platform precedent every non-VICE surface follows (slice loop, wake, fullscreen chord exception, About panel rows, toast-under-fullscreen, the keyboard-mouse sentence, the sidecar file read at launch) | SPEC.md §74, §74.1, §74.2 (`SPEC.md:57224` for the SPEC.md §9.6 sentence), §74.4, §9.6, §9.7, §5.4.1, §5.4.2; `apps/runcpm/runcpm.c`, `rcterm.c`, `rcz80.inc`, `rcband.inc`, `rcmem.inc`, `rcabout.c`; `docs/RUNCPM-PORT-PLAN.md` (the measured ratios) |

---

## 3. The memory model

### 3.1 The two claims

Neither the C64's RAM nor its ROMs live in the package. Both are heap claims,
their own segments:

| claim | size | contents |
|---|---|---|
| RAM | `os88_mem_claim(64)` — 65,536 bytes | the C64's RAM, `$0000-$FFFF`, flat, one segment |
| ROM | `os88_mem_claim(20)` — 20,480 bytes | `C64.ROM` read straight in, §1.4's layout |

Launch is **defined by the claims succeeding**, not by a free-KB figure: the
64KB claim, the 20KB claim and the `C64.ROM` read must all succeed, and the
refusal sentence quotes what was asked and `os88_mem_largest_kb()`. Two more
claims appear later and are refused politely if they cannot be had: the
package's own `C64.OVL` module (§13.3) at the first menu command, and a
transient claim the size of the `.PRG` during Smart attach (§11.3).

Colour RAM (1,024 nibbles), the VIC/CIA/SID register files, the 1bpp frame
shadow and the keyboard matrix are in the package's **bss** (§13.2). The
core's own hot scratch is in neither — §3.5.

### 3.2 `$0000` and `$0001` are NOT RAM

**Every read and every write of `$0000` and `$0001` is a special case**, on
the fast path and the slow path alike. They are the 6510's processor port
(`c64pla.c`, `c64mem.c`'s `zero_read` / `zero_store`), not memory:

- **`$00`** is the data-direction register. Reset value `$2F`.
- **`$01`** is the data port. Reset value `$37`.
- **A write to `$01` (or to `$00`, which can change which bits `$01` drives)
  re-evaluates the bank map at once**, together with the fetch segment and
  the next-boundary word (§4.3). The core never executes one instruction
  under a stale map.
- **Read-back of `$01`.** Bits 0–2 read what was written where `$00` makes
  them outputs, and the pulled-up level otherwise. There is no datassette in
  this build (§11.2), so bit 4 (cassette sense) reads **1**; bits 3 and 5
  read back what was written when they are outputs and 0 when they are not;
  bits 6–7 are unconnected and read 0. Stated, because a program that reads
  `$01` to discover the bank sees this table and not a real 6510's decay
  behaviour.

Underneath the port, RAM `$0000`/`$0001` still exists and is still what the
VIC and any DMA-less read of those bytes would see; this port keeps the two
RAM bytes and the two port registers separately, as VICE does.

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

Rows 0 and 4 are the same map; the table is 8 entries and 7 shapes.
`$01 = $37` at reset selects row 7, which is the machine BASIC runs in.

### 3.4 The fast path, the slow path, and the slow FETCH path

The core's memory access is priced in instructions, not in calls:

- **Reads below `$A000`, except `$0000`/`$0001`** (§3.2), and **writes
  outside `$D000-$DFFF`, except `$0000`/`$0001` and the core's scratch
  (§3.5)**, are the two-instruction fast path against `DS` = the RAM claim.
  This is the overwhelming majority of every program's traffic.
- **Every write additionally sets one bit in the 256-page dirty bitmap**
  (§9.2). That is the stated per-write cost of this design: about five
  instructions, paid on every store, in exchange for a screen model that can
  see a bitmap, a RAM character set or sprite data change (§9.2).
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
  the biased `ES` — the biased fetch physically cannot read a register file.
  Fetching from I/O is a thing real programs do only by accident, and this
  port executes what the accident really produces instead of silently
  executing RAM.

### 3.5 The core's scratch — in the emulated machine, never in bss

LESSONS 13 is binding: **a hot counter in the package's bss is a TCG slow
path under QEMU** when that page also holds translated code, and it cost
RUNCPM 5× on the whole Z80 core. The 6510 core's hot words therefore live in
the RAM claim, at **`$FFC0-$FFF9` — 58 bytes at the top of the C64's own
memory, BELOW the six vector bytes**: `$FFFA-$FFFF` (NMI, RESET, IRQ) stay
real RAM, because a program that banks the KERNAL out (`$01 = $35`) puts its
own vectors there and the core fetches them from RAM in that map.

| what | §  |
|---|---|
| the 32-byte page dirty bitmap | §9.2 |
| the emulated-cycle counter (two words) and the current slice deadline | §4.2, §4.4 |
| the next-mapping-boundary-above-PC word and the cached fetch `ES` | §4.3 |
| the pending IRQ/NMI flags | §4.4 |

That address is chosen because the KERNAL's vectors sit there **in ROM** in
every map that runs KERNAL code, and nothing in the KERNAL or BASIC uses the
RAM under them. Two stated deviations follow, and the CPU harness (§4.6)
holds a case for each so the boundary is provably where this document says it
is:

- **A read of `$FFC0-$FFF9` in an all-RAM map reads the scratch**, not the
  RAM the emulated machine wrote.
- **A write to `$FFC0-$FFF9` is dropped.** The write path already tests the
  high byte; only the `$E0-$FF` branch pays the extra compare.

The mask table the dirty bitmap needs is `cs:`-resident and **read-only** —
reads of a translated page are not the TCG hazard, only writes are.

### 3.6 The movers, and every cross-segment routine

`apps/c64/c64mem.inc` is `rcmem.inc`'s shape for these two claims: `c64_rd` /
`c64_wr` / `c64_rd16` (near cdecl accessors), `c64_rom_rd`, `c64_zcopy_in` /
`c64_zcopy_out` (package ↔ RAM), `c64_zzcopy_in` (claim → RAM, far-to-far,
the `.PRG` load) and `c64_zfill` (the power-cycle pattern).

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
  `c64band.inc`'s compare and copy loops, with an ES-not-restored negative
  control that must fail.

---

## 4. The 6510 core

`apps/c64/c64cpu.inc`, hand-written 8086 in `rcz80.inc`'s shape, inside the
shim (§13.1).

### 4.1 The register plan

| 6510 | 8086 | note |
|---|---|---|
| A | `AL` | |
| P (N, Z, C) | `AH` | `lahf` layout, so the host flags carry them |
| P (V, D, I, B) | `CH` | NEVER `cs:` statics — a write into the package's own code page is the TCG slow path of LESSONS 13, and V moves on every ADC/SBC/BIT |
| X | `CL` | indexed addressing is `add bl,cl / adc bh,0`, whose carry IS the page-cross penalty §4.2 charges — so `CH` is free for the flags at no cost |
| Y | `DX` | `DH` held 0 |
| PC | `SI` | |
| S | `DI` | the stack page is `[di+0x100]` |
| dispatch scratch / effective address | `BX` | |
| the fetch scratch / boundary compare | `BP` | `ds:` override where it addresses memory |
| RAM | `DS` | the 64KB claim |
| the fetch segment | `ES` | §4.3 |

The dispatch is a 256-entry table: `xor bh,bh / mov bl,[es:si] / inc si /
shl bx,1 / jmp [cs:bx+tab]`.

The cycle counter and the boundary word are **not** registers and **not**
bss: they are in the emulated machine's own scratch (§3.5).

### 4.2 Time is 6510 CYCLES

**The clock is the emulated 6510's cycle count and nothing else.** Every
opcode carries its real cost from `6510core.c`'s tables, **including the
page-cross penalty on the indexed addressing modes and the taken-branch and
branch-page-cross penalties**, and the core decrements **one cycle counter**
(§3.5) by it.

That counter is **both** the wall-clock slice budget (§4.4) and the device
clock (§6.3). It is the only time in this machine.

A count of *control transfers* is not a cycle count and is not used as one:
an unrolled straight-line sequence and a tight branch loop execute the same
6510 cycles at wildly different branch densities, so a branch-counted clock
would advance the CIAs and the VIC at a rate that depended on the program's
shape. The cycle table costs 256 bytes of read-only image and is the price of
CIA timer modes, IRQ phase, `$D012` reads and raster interrupts meaning what
they say.

### 4.3 The fetch segment, and the boundary word

`ES` is biased so that `[es:si]` is always PC's byte: for a PC in RAM it is
the RAM claim; for a PC in ROM it is the ROM claim **minus that bank's own
bias** (§1.4's layout gives one constant per bank).

**It is re-evaluated whenever the fetch address crosses a mapping boundary —
not merely at control transfers.** Falling through from `$9FFF` to `$A000`,
`$BFFF` to `$C000`, `$CFFF` to `$D000` or `$DFFF` to `$E000` changes the
visible bank with no branch in sight, and an instruction *beginning* at
`$9FFE` fetches operand bytes from the other side. So:

- The core keeps a **"next mapping boundary above PC" word** in its scratch
  (§3.5). The boundaries are `$A000`, `$C000`, `$D000`, `$E000` and the wrap
  to `$0000`.
- **One `cmp` per instruction fetch** against that word. When PC reaches or
  passes it, the map is consulted, `ES` and the boundary word are recomputed.
  Operand fetches use the same guarded fetch.
- **Every write to `$00` or `$01`** recomputes both (§3.2).
- **Every cdecl call out of the core saves and reloads the cached `ES`**
  (§3.4) — the C ABI clobbers it.

There is no "stale bias" deviation left in this design. **Every map
transition has a case in the CPU harness** (§4.6).

### 4.4 The alarm model, and the wall slice

VICE's own structure (`src/alarm.c`, `maincpu.c`): **run to the next device
event, service it, compute the next one.** There is no fixed quantum
anywhere in this machine.

**The alarm.** Before each run, `c64io.c` computes *cycles to the next event*
as the minimum of: CIA1 timer A underflow, CIA1 timer B underflow, CIA2
timer A, CIA2 timer B, the VIC raster compare, the end of the current raster
line (so `$D012` steps), the end of the frame, and the CIA TOD tick. The core
runs until the cycle counter reaches that deadline and then makes **one cdecl
call to `_c64_alarm()`**, which services what is due, raises IRQ or NMI on
the pending flags, and answers the next deadline. Interrupt entry is taken
between instructions on the pending flags.

**The wall slice.** Separately and independently, the core is given a **raw
cycle budget** for how long it may hold the UI task. RUNCPM's structure,
exactly (SPEC.md §74.1):

- seeded from `os88_cpu()` (`OSAPI_CPU_INFO`, slot `0x0188`);
- **256 to 16,384 cycles**, doubled when four slices fit inside one host
  tick, halved when one slice spans two tick boundaries;
- **only a genuinely EXHAUSTED slice adapts** — a slice that ends early
  (paused, jammed, the alarm handler stopped it) leaves the estimate alone.
  LESSONS 13's finding: without that, ordinary idling walks the budget to its
  cap and the next busy slice is a second of stalled UI task.

**The floor is never a whole jiffy.** Every device phase — the cycle counter,
each timer's remaining count, the raster position, the TOD accumulator — is
retained across slices and across wakes, so a slice may end anywhere. At the
8% of a real C64 that this port may turn out to run at, a whole emulated
jiffy would be ~200 ms of UI task; a 256-cycle floor is ~3 ms.

The wake is re-posted only **while the machine is running**; pause and a JAM
stop it, and `os88_onkey` / `os88_onclick` kick it so a wake the full event
ring dropped cannot park a running machine.

### 4.5 What `_c64_run` answers

Two values only:

| answer | meaning |
|---|---|
| `C64_RUN_SLICE` | the wall-slice cycle budget was spent between instructions |
| `C64_RUN_JAM` | a `KIL`/`JAM` opcode; the machine stops, the status row says so, the window stays up |

Alarms and I/O are **calls out of the core, not exits from it** (§3.4, §4.4),
so there is no mid-instruction exit and no caller ever has to resume a
half-executed opcode.

### 4.6 The core's gate

`apps/c64/hosttest/c64cputest.asm` + `.sh`, run by **`make c64cputest`**
(minutes, like `make rcz80test`; deliberately *not* in `build.sh`): the
**shipping `c64cpu.inc`**, in a boot sector, in raw QEMU, under `SS ≠ DS`.
It is not a Dormann wrapper — Dormann is one row of it:

1. **Klaus Dormann's `6502_functional_test` and `6502_decimal_test`** to
   their success loops (fetched at pinned SHA-256s, **never committed**).
2. **Each of the seven bank maps** (§3.3): the right byte visible in each
   4KB region for each `$01` value.
3. **`$0000` and `$0001`**: DDR-derived banking, the read-back rules of §3.2,
   and a re-bank taking effect on the very next fetch.
4. **Reads, writes and FETCHES at `$9FFF`, `$A000`, `$BFFF`, `$C000`,
   `$CFFF`, `$D000`, `$DFFF`, `$E000`** — including an instruction that
   begins on one side of a boundary and takes its operand from the other
   (§4.3).
5. **Real I/O stub returns**, not park-at-FAIL: the stubs answer values the
   test then checks, so the cdecl convention, the `DS` swap and the `ES`
   save/reload are all exercised rather than merely forbidden.
6. **`ES`/`DS` restoration checks** after every call out.
7. **IRQ and NMI entry**, including entry while a bank switch is pending.
8. **The illegal opcodes** `6510core.c` implements.
9. **Cycle totals per opcode family** against `6510core.c`'s table, page-cross
   and taken-branch penalties included (§4.2).

**Every row carries a negative control** — a deliberately wrong flag
finisher, a deliberately stale `ES`, a deliberately missing branch penalty —
and the harness must report failure for each, or it is proving nothing.

---

## 5. The VIC-II

### 5.1 What is modelled

- The five legal modes: **standard text, multicolour text, extended-background
  text, hires bitmap, multicolour bitmap** — plus idle/illegal-black.
- Screen base and character base from `$D018` **and** the CIA2 `$DD00` bank
  bits (§6.2).
- Border and background `$D020-$D024`.
- The 25/24-row (`$D011`) and 40/38-column (`$D016`) flags, honoured **as
  border**.
- `$D011`/`$D016` fine scroll honoured as a **whole-cell offset only**
  (stated deviation).
- `$D012` raster read, `$D019` flag, `$D01A` mask, and the raster compare —
  all off the cycle clock (§5.2).
- **8 hires sprites**: position, enable, priority against the background,
  x-expand and y-expand, composed into the rows they touch. Multicolour
  sprites are drawn by luminance threshold (§9.6).

### 5.2 Timing — the PAL frame, off the cycle clock

The PAL frame is **63 cycles × 312 lines = 19,656 cycles = 50.123 Hz**
(`c64.h:35-40`, `vicii-timing.h`). It has nothing to do with the 60 Hz jiffy,
which is a thing the KERNAL programs a CIA to produce (§6.3).

The raster counter is an **alarm** (§4.4): the end of each raster line is a
scheduled event, so `$D012` reads the line the cycle counter has actually
reached, and a raster compare fires at the line it was armed for, in the
right order relative to every CIA interrupt. A program arming two raster
interrupts in a frame gets both.

**What is still not there** is *sub-line* fidelity: the VIC is serviced at
line granularity, so a register written part-way along a line takes effect
from that line. §5.3 lists what that costs.

### 5.3 What does not carry

- **Sprite–sprite and sprite–background collision:** `$D01E` and `$D01F`
  **answer 0.** The composer draws whole cells, and a collision needs the
  pixel compare (§11.2).
- **Cycle-exact raster effects** — mid-line colour changes, bad-line timing,
  FLD, FLI, sprite stretching, opening the border. Text, BASIC, the KERNAL
  and most BASIC-era programs are unaffected; demos are.
- **Colour.** §9.6.

---

## 6. The CIAs

### 6.1 CIA1 (`$DC00-$DCFF`)

Timers A and B (one-shot and continuous, underflow IRQ), the ICR with VICE's
mask/flag semantics (`ciacore.c`), TOD registers read and write, and **PRA/PRB
with the DDR**: PRA drives the keyboard matrix columns and PRB reads the rows,
against §7's cached matrix, with the joystick bits (§8) ORed in exactly as
`c64cia1.c`'s `read_ciapa` / `read_ciapb` do. The **IRQ line** into the 6510
is CIA1's ICR ORed with the VIC's `$D019 & $D01A`.

**The 60 Hz jiffy is not a constant of this design — it emerges.** The KERNAL
writes `$4025` (16,421 cycles) into CIA1 timer A on a PAL machine, and the
timer underflows when the cycle clock says it does. `TI$`, the cursor blink
and the keyboard scanner then run at the rate the emulated machine chose,
which is what makes a program that reprograms timer A behave.

### 6.2 CIA2 (`$DD00-$DDFF`)

Timers, TOD, ICR, **PRA bits 0–1 = the VIC bank** (inverted, as the hardware
has them), and **NMI from timer underflow**. The NMI line is CIA2's ICR ORed
with RESTORE (§7.4).

### 6.3 Three independent phase accumulators

One clock, three phases, none derived from another — this is what keeps the
VIC from being 20% wrong:

| accumulator | period | drives |
|---|---|---|
| the CIA timers | whatever their latches say (the KERNAL's PAL timer A is 16,421 cycles ≈ 60 Hz) | the jiffy IRQ, `TI$`, every timer interrupt |
| the VIC raster | 63 cycles a line, 19,656 a frame ≈ 50.123 Hz | `$D012`, raster IRQs, the frame counter the status bar prints (§10.2) |
| the CIA TOD | its own **50 Hz** mains accumulator off the cycle clock | the TOD registers |

---

## 7. The keyboard — a level model

VICE's `keyboard.c` is a **level** model: a key is in the matrix while it is
down and leaves it when it comes up. So is this one. **There is no invented
hold time**, which is what makes RUN/STOP+RESTORE work, makes a game reading
`$DC01` work, and leaves the KERNAL's own repeat timing to the KERNAL.

### 7.1 The map

`apps/c64/c64kbd.c` carries `data/C64/gtk3_sym.vkm` transcribed **whole** —
all 152 entries — to a static `.data` table of `{key, row, col, flags}`,
keyed **by ascii for 32..126 and by scan code for everything else**. From the
`.vkm`, verbatim: **Tab = C=**, **Escape = RUN/STOP**, **Page_Up =
RESTORE**, and the `!LSHIFT` / `!RSHIFT` / `!LCBM` / `!LCTRL` matrix
positions. Home, Ins, Del, F1–F8, the arrows, End/PgDn (the two C64 cursor
keys) and Pound on backslash are all the `.vkm`'s, never invented.

### 7.2 The level model, on this OS — polled once per WAKE

`os88_onkey` delivers **presses only**. The state comes from the kernel's own
key-state map, `OSAPI_KEY_DOWN` (slot `0x03F0`, SPEC.md §9.7): `AL` = a make
scancode, `CF` = down, every register kept.

Four rules, each of which exists because getting it wrong is silent:

1. **The map is armed ONCE, in `os88_main`.** `OSAPI_KEY_DOWN`'s first call
   clears and arms the map and always answers "up". Arming it from the first
   slice would erase the make `os88_onkey` had already seen — the first key
   of the session, lost.
2. **Host key state is polled ONCE PER WAKE and cached.** Every emulated CIA1
   read in that wake reads the cached 8×8 matrix. The bound is **≤ ~20 far
   calls per wake** at 46.7 µs — the down-list, the five joystick scancodes,
   Ctrl, and the ten digit scancodes only while Ctrl is down (§7.3) — under
   1 ms, **and it does not multiply by the number of slices in the wake.**
3. **The matrix is REBUILT from the whole down-list every wake, never cleared
   incrementally.** Several `.vkm` mappings share the synthetic SHIFT, CTRL
   and C= bits, so clearing one key's bits can clear another key's; rebuilding
   from what is still down is the only correct operation.
4. **A fresh press is guaranteed at least one slice in the matrix before
   release polling can clear it**, or a quick press is cleared before the
   emulated machine ever scans the keyboard.

The **down-list is 16 entries** and its overflow path is bounded and tested:
the 17th simultaneous key is dropped, not written past the end.

**The map is advice, not an oracle** (SPEC.md §9.7). A key whose break code
the ISR missed stays in the matrix until its next press. That is stated here
because a stuck key inside a game is a possible and visible symptom, and it
is still strictly better than any invented hold.

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
polled in the same once-per-wake pass and their matrix bits set. This is what
the level model buys, and it means **no VICE Alt+digit chord is stolen** to
fake them (Alt+8/9/0/1 and Alt+3/4/5/6 stay VICE's drive and printer
captions, §11.2).

### 7.4 RESTORE, and RUN/STOP+RESTORE

**Page_Up = RESTORE** and raises an NMI. At that moment
`os88_key_down(KSC_ESC)` is read: Esc down means RUN/STOP is held, and the
KERNAL's own NMI handler warm-starts to `READY.` — the real machine's
behaviour arrived at the real machine's way, through the matrix, not by
special-casing a chord.

### 7.5 The chords the target class cannot deliver

Three of VICE's bindings do not survive this hardware, and **the caption is
kept while the menu item is the guaranteed route**:

| chord | VICE's item | what happens here |
|---|---|---|
| Alt+F12 | Power cycle machine | an 83-key XT keyboard **has no F12**; the menu item is the route |
| Alt+Insert | Paste | an AT BIOS `int 16h AH=0` (`kernel/mouse.inc:1426`) drops the enhanced code `0x8B`/`0xA2`; the menu item is the route |
| Alt+Delete | Copy | the same, code `0xA3` |

They work where the BIOS passes them and are captioned exactly as VICE
captions them either way. **The chord table** — Alt+D, Alt+F9, Alt+F12,
Alt+Insert, Alt+Delete, Ctrl+letter, Ctrl+digit, Alt+Q, Alt+J, each recorded
*arrives* / *does not* on `vm/386-c64` and `vm/xt-c64` under 86Box — belongs
in this section and is filled in **by hand, from those machines** (§14.6),
never from QEMU's SeaBIOS, which passes enhanced codes an AT BIOS drops.

### 7.6 The keyboard-mouse rule

SPEC.md §9.6 stands, and this document states it the way SPEC.md §74.2 does:
**on a machine with no mouse the arrows, Space, keypad 0/5 and Del are the
kernel's mouse; ScrollLock hands them back.** When `os88_mouse()` reports
that no mouse has spoken, the status row's message area prints
**`ScrollLock for joystick`** — the fact, where the user is looking, instead
of a joystick that silently does nothing.

---

## 8. The joystick

- **Port 2** is the cursor/numpad arrows with **Ctrl as fire**. This is
  **this port's choice**, and it is stated: VICE's `JoyDevice2` default is
  `JOYDEV_NONE` (`joystick.c:2092`), but a keyset is the only joystick source
  this machine has, so `KeySetEnable=1` (`:553`) is the shipped state and
  Preferences > Allow keyset joysticks is shown **checked and disabled**.
- **Port 1** is empty.
- Read from the once-per-wake cached key state (§7.2) — 5 of the ≤ ~20 calls
  — and presented as `$DC00`/`$DC01` bits 0–4 **active low** (up, down, left,
  right, fire).
- **Alt+J, Swap joysticks**, swaps the ports, as VICE's item does.
- The status row carries VICE's two 5-dot indicators (§10.1).

**Ctrl is both fire and the CTRL key.** A game reading CTRL+letter from the
matrix while the joystick fires sees both — which is exactly what a real
machine with a keyboard and a joystick plugged in does — but a BASIC user
holding Ctrl to type a colour code also fires port 2. Stated, not fixed.

---

## 9. The screen

### 9.1 The window

Authored **336 × (`TITLE_H` + 216 + 10 + 1)** at (7, 20): 320×200 of C64
screen, an 8-pixel border on every side, and a 10-pixel status row. **The
content height is `W_H − TITLE_H − 1`** — LESSONS 13's finding, where a window
authored `TITLE_H + 200` showed 24 rows and a sliver. `wm_snap` puts the
content x on a cell boundary, which is what lets `OSAPI_GFX_SCROLL` accept the
rect (§9.4).

### 9.2 Dirty tracking — a 256-page bitmap, set by the core

**A shadow of the screen matrix and colour RAM cannot see most of what a C64
draws.** Bitmap data (8KB), a RAM character set and sprite bitmap bytes all
change without any screen code, colour nibble or VIC register changing. A
matrix-only shadow would simply fail to repaint the bitmap, custom-charset
and sprite modes this port ships.

So the core does the tracking:

- **A 32-byte, 256-page dirty bitmap** in the core's scratch (§3.5). Every
  RAM write ORs one bit into it — about five instructions, the stated
  per-write cost of this whole design (§3.4).
- The flush reads the bitmap, maps the dirty pages through the **frame
  registers as they currently stand** (`$D011`, `$D016`, `$D018`, the CIA2
  bank, the sprite pointers) onto the cell rows those pages feed — screen
  matrix, character generator or bitmap, colour RAM, sprite data and sprite
  pointers — and **composes only those rows**. A frame-register write dirties
  every row by itself.
- The bitmap is cleared by the flush, under the same lock, so nothing is
  lost between a write and a read of it.

The counter the harness prints is **dirty pages per wake**. There is no
per-tick compare of a 2,000-byte shadow anywhere in this design; that idea
and the `c64_rowdiff` cell compare it needed are both gone.

### 9.3 The frame shadow, and the flush

**The shadow is the glass, not the model:** an **8,000-byte 1bpp frame
shadow** in bss — 320 × 200 bits, exactly the pixels last blitted.

The flush runs **at most once per host tick**, never once per slice
(`os88_ticks()`, the `rc_ticks32` shape), and holds the gfx lock **only**
around itself, never around a slice:

1. compose the dirty rows (§9.2) into 1bpp bands (§9.5);
2. test for a whole-frame shift first (§9.4);
3. otherwise compare each composed row against the frame shadow and **draw
   only the differing spans** — a span is `(first cell .. last cell)` and the
   composer and the blit both take it (§9.5);
4. update the shadow with what was drawn;
5. border fills **only if `$D020` changed**;
6. the status row, delta-drawn (§10).

Because the shadow is pixels, it validates the composer as well as the model:
if the composed row equals the shadow, nothing is drawn, whatever the model
believed.

### 9.4 The scroll test — a shift of **k = 1..24** rows

Flushing once per host tick means several rows can have scrolled since the
last one. So the shift test is not the one-row test:

- For `k = 1..24`, test whether composed cell row *i* equals shadow cell row
  *i + k* for every *i* below `25 − k`.
- On a hit, emit **one `OSAPI_GFX_SCROLL` (slot `0x01F8`) plus the `k`
  vacated rows** — `k + 1` calls, not 25.
- `OSAPI_GFX_SCROLL` refuses when the clip does not contain the rect; on a −1
  the flush **falls back to spans and the shadow stays true** (nothing
  moved), exactly as `rcterm.c` does.

**The gate asserts one scroll per FLUSH, not one per emulated line.** A
`FOR`-loop of `PRINT`s that scrolls nine lines between two ticks is one
scroll of nine rows and nine composed rows, and asserting "one scroll per
printed line" would be asserting a slower program.

### 9.5 The composers

`apps/c64/c64band.inc` — `rcband.inc`'s shape, **1bpp only**, every entry
point taking explicit `(segment, offset)` pairs for anything in a claim
(§3.6):

| routine | does |
|---|---|
| `c64_band1(dst, first, last, matrix_seg:off, colour_seg:off, chargen_seg:off, mode, bg)` | composes cells `first..last` × 8 rows into a 1bpp band — text by glyph, XOR'd by fg/bg luminance; bitmap by the cell transpose; multicolour by threshold |
| `c64_band_sprites(band, first, last, sprite_regs, ram_seg)` | overlays the visible sprite rows into a composed band |
| `c64_band_x2(band, rows)` | pixel-doubles a band through a 256-entry byte→word table, 8 or 16 rows deep, for fullscreen |
| `c64_rowspan(a_seg:off, b_seg:off, n)` | compares a composed row against the shadow and answers `(first, last)` differing cell, or "same" |
| `c64_rowshift(a_seg:off, b_seg:off)` | §9.4's shift test |

**The composer takes a span, never "always 40 cells."** A one-cell change
composes one cell. This is the difference between a keystroke costing
~1 ms and ~8 ms.

The glyph bytes come from the **CHARGEN ROM in the claim** or from the RAM
character set the VIC is pointed at — never from `OSAPI_FONT_GLYPHS`. This is
a C64 face.

Each composed span goes down in **one `OSAPI_GFX_BLIT1`** (slot `0x0418`); a
−1 (a `kern_small` kernel) falls back to the font path.

### 9.6 Monochrome, by luminance, on every adapter — **a fact, not a limitation being worked around**

**The C64 screen is 1bpp through `OSAPI_GFX_BLIT1` on every adapter, VGA
included.** Every one of VICE's 16 colours (`data/C64/vice.vpl`) is resolved
to black or white by a luminance threshold, and multicolour text, multicolour
bitmap and multicolour sprites are thresholded the same way.

The fact that decides it: **SPEC.md §5.4.1 — VGA keeps the span writer.**
`gfx_blit4` therefore prices a band by its colour *runs*, at ~215 µs a run
(PERFORMANCE.md Set 44), and a C64 text band is ~1,000–1,600 runs — **0.2 to
0.35 s per band** on the target and **0.3 to 0.5 s a frame even on a 386**.
A 4bpp composer was priced out before it was written.

`gfx_blit1` reads no pen (SPEC.md §5.4.2 pins Set/Reset off at rest), so
there is no package-side answer. **Colour is a KERNEL question**: a `blit1`
variant taking ink and paper would make a colour C64 text band cost exactly
what the 1bpp band costs, and it spends kernel `.text` against
`KERN_CODE_MAX`, which is a decision to take with whoever asks for the
feature and not a build fix (`docs/KERNEL-MEMORY.md`).

**This port therefore keeps the EGA-16 map and a per-row colour record**
(§9.2's mapping) so that slot drops in without re-planning. That is a
follow-up, and `docs/C64-PORT-PLAN.md`'s Decisions section records it as one.

### 9.7 The cost — **in milliseconds, not in calls**

A call count hides the composer, which is where a C64 frame's cost actually
is. RUNCPM's measured band is ~860 µs a call **+ ~173 µs a cell**, so a
40-cell band is ~7.8 ms and 25 of them ~195 ms *before* the C64's harder
modes and its sprites. **"25 calls" is therefore not an acceptance
criterion.**

**The gates are measured milliseconds on the `tests/c64band` icount harness**
(§14.5) — compose instructions, cells and the resulting µs — with the call
count reported beside them as a secondary figure:

| operation | gated on |
|---|---|
| one changed cell | compose 1 cell; 1 blit call |
| one changed row | compose `last − first + 1` cells; 1 blit call |
| a `k`-row scroll | 1 scroll + `k` composed rows; **1 scroll per flush** |
| a full expose | ≤ 25 composed rows + 1 fill + border; the **measured ms** on the target from the bench, written here when the bench answers |
| a full bitmap frame | the **measured ms**, not the call count |
| one sprite moved one cell | the spans it actually touched |
| Alt+D, either direction | **0 flush calls** (SPEC.md §74.2's zero — the latch does its own paint) |
| a slice with no tick boundary | **0** (§9.3) |

A change that moves a row of this table up is a regression against a
documented number, and this table and the harness change together or not at
all. **The numbers are written in from the bench, wave by wave; until then
each cell says so.**

### 9.8 Fullscreen, and the tier table

`OSAPI_FULLSCREEN` (slot `0x0110`, SPEC.md §11.2's latch) on **Alt+D**, both
directions — VICE's own binding. **This is a stated exception to
SPEC.md §11.2.1**, taken the way SPEC.md §74.2 takes Alt+F for a terminal:
the C64 owns F and Esc, so neither can carry the latch here.

The scaling is a **tier table in one place** (`c64scr.c`), written **from**
`tests/c64band`'s measured milliseconds (§14.5) and from the machine figures,
never from a guess:

| adapter / tier | fullscreen |
|---|---|
| CGA | 2×, **exactly 640×200** |
| VGA | 2×, 640×400 centred — the band composed 16 rows deep |
| Hercules | 2× horizontal, 640×200 centred, 1× vertical |
| the `CPU_8086` tier | 1:1 centred |

The rest of the screen is a border fill. **A toast raised while fullscreen
goes to the status row as well** — the bar a toast lands on is *under* a
`WF_FULL` window (LESSONS 13), so every refusal in this port takes both
routes.

---

## 10. The status bar

One 10-pixel row under the screen, **336 pixels wide = 42 cells**, delta-drawn.

### 10.1 What is on it

Left to right: the **message area**, `Tape:` (greyed), `Joysticks:` with two
5-dot indicators, drive **8** with its track counter ` 18.5` (greyed), and
the speed widget.

### 10.2 The speed widget — VICE's own strings, and what they count

`statusbarspeedwidget.c` prints `%7.0f%% cpu` (`:572`, `CPU_DECIMAL_PLACES`
0) and `%8.1f fps` (`:653`, `FPS_DECIMAL_PLACES` 1). **Both are folded onto
this one row**, e.g. `   100% cpu    50.1 fps`, and the warp and pause LEDs
become **two dots**, drawn with SPEC.md §47's pen when off.

- **cpu %** = emulated 6510 cycles delivered per second ÷ 985,248. 100% *is*
  a real C64. Nothing throttles, so this number is the machine's honest
  output and the reason Preferences > Emulation speed is greyed (§11.2).
- **fps** = **emulated VIC frames** per second (§6.3's raster accumulator).
  It can reach 50.1, and 50.1 is what a machine running at 100% prints.
- **Flushes per second is a different number and is not on the bar** — it is
  a harness counter (§14.5), capped at the host's 18.2 Hz tick by §9.3.
  Labelling redraw rate as "fps" would have made a correct machine look
  broken.

**Counters are two-word (lo/hi), folded often**, the way `rc_ticks32` does.
"No `long`" is a C rule of SPEC.md §73, **not** a rule against 32-bit
quantities: a cycle count over a second, or `cycles × 100` before a division,
overflows 16 bits on any machine faster than an XT, and a 16-bit counter that
laps produces small plausible numbers rather than an error (CLAUDE.md's
performance rule 3).

### 10.3 What is dropped rather than greyed

**Recording, Volume, CRT and Mixer** are not on the row at all
(`uistatusbar.c` 1961/1971, 2594, 2670/2679). The fact is the width: 42 cells
hold the message area, `Joysticks:`, drive 8 and the two speed strings and
nothing more. Dropped is stated here; everything else missing is greyed with
its fact (§11.2).

---

## 11. The menus

### 11.1 The set, and what is live

**Exactly five** — the kernel's `MENU_APPMAX` — with `AM_NAME` = **`VICE`**:
**File, Edit, Snapshot, Preferences, Help**. VICE's Debug menu is `#ifdef
DEBUG` in `uimachinemenu.c` and is **absent here by VICE's own rule**, not
greyed.

Every item string is `uimachinemenu.c`'s and every hotkey caption is a
`hotkeys*.vhk` line, transcribed (§2). Submenus are folded into their head
item. Live items:

| item | caption | note |
|---|---|---|
| File > Smart attach... | Alt+A | the Standard File dialog on `.PRG` (§11.3) |
| File > Reset > Reset machine CPU | Alt+F9 | |
| File > Reset > Power cycle machine | Alt+F12 | caption kept; the item is the route (§7.5). RAM pattern fill |
| File > Exit emulator | Alt+Q | the worker self-close idiom |
| Edit > Copy | Alt+Delete | caption kept, item guaranteed: the 40×25 screen, PETSCII → ASCII, to the clipboard |
| Edit > Paste | Alt+Insert | caption kept, item guaranteed: the clipboard typed through `$0277`, ten characters a jiffy |
| Preferences > Fullscreen | Alt+D | §9.8 |
| Preferences > Warp mode | Alt+W | flush every 9 ticks instead of every tick |
| Preferences > Pause emulation | Alt+P | shown checked by text swap |
| Preferences > Advance frame | Alt+Shift+P | run to the next VIC frame end (§6.3), then stop |
| Preferences > Swap joysticks | Alt+J | §8 |
| Help > About VICE... | | `uimachinemenu.c:988`; the kernel's name pull-down About opens the same panel (§12) |

### 11.2 Present and greyed — the fact that greys it (SPEC.md §47)

| item | the fact |
|---|---|
| File > Attach disk image / Detach disk image / Create and attach an empty disk image / Flip list (folded to one item each; captions Alt+8/9/0/1, Alt+I/K/N kept) | no 1541 in this build: a D64 needs the drive's directory walk and the KERNAL serial traps, and this port loads `.PRG` files only (Smart attach) |
| File > Attach/Create/Detach datasette image, Datasette controls (Alt+T captions kept) | no tape emulation in this build: T64/TAP are not read |
| File > Attach cartridge image..., Detach cartridge image(s), Cartridge freeze (Alt+C / Alt+Z captions kept) | no cartridge port in this build: the bank maps carry the cartridge-less 7 of VICE's 32 (`exrom = game = 1`, §3.3) |
| File > Printer/plotter formfeed items (Alt+3/4/5/6 captions kept) | no printer path in this OS |
| File > Activate monitor (Alt+H) | no monitor in this build: VICE's monitor is 30,000 lines of host C |
| File > Reset drive #8..#11 | no drives in this build |
| Snapshot > every item (load/save Alt+L/S, quick snapshots Alt+F10/F11, event recording, milestones Alt+E/U, media recording Alt+Shift+R/S, quicksave screenshot on Pause) | no snapshot format in this build: a VSF carries every chip's state and this machine's chips are not VICE's |
| Preferences > Restore display state (Alt+R), Fullscreen decorations (Alt+B), Show menu/status in fullscreen | the window is os8088's: the kernel places it, and the bar is under a fullscreen window (SPEC.md §11.2) |
| Preferences > Emulation speed (200%..10%, Custom CPU speed), 50/60/Custom FPS | nothing throttles here: the machine delivers what the 8088 can and the status bar prints it (§10.2; SPEC.md §74.4's `F_CPUSPEED` reasoning) |
| Preferences > Show status bar | the status row is the window's bottom row and is always drawn |
| Preferences > Mouse grab (Alt+M) | no 1351 mouse in this build: the pointer is the desktop's |
| Preferences > Allow keyset joysticks (Alt+Shift+J) | the keyset **is** the joystick here (the only joystick source this machine has); shown **checked and disabled** (§8) |
| Preferences > Settings... (Alt+O), Load/Save settings, Restore default settings | no resources file in this build: every setting this port has is on the Preferences menu itself |
| Help > Browse manual, Command line options, Compile time features, Hotkeys | no manual on this floppy and no command line in this OS; the hotkeys are the menu captions |
| Machine model other than C64 PAL (C64C, NTSC variants, Drean, SX, Japanese, GS, PET64, MAX) | one ROM set and one timing are carried: PAL 985248 Hz, 312 lines, 19,656 cycles a frame (`c64.h`) |
| SID voices 2–3, waveforms other than the gate, ADSR, filters, `$D41B`/`$D41C` | the PC speaker is one square wave, and the sound driver's FM path is not wrapped for C (§11.4) |
| Colour on the glass, VGA included | §9.6 — SPEC.md §5.4.1's span writer, ~215 µs a colour run, ~1,000 runs a text band |
| Sprite–sprite and sprite–background collision registers | the composer draws cells, not pixels: `$D01E`/`$D01F` answer 0 (§5.3) |
| Cycle-exact raster effects (fine scroll, mid-line colour changes, bad-line timing, FLD/FLI) | the VIC is serviced at raster-LINE granularity (§5.2): `$D011`/`$D016` scroll is honoured as whole cells and a register written part-way along a line takes effect from that line |
| Status bar: the `Tape:` field, drive 8's LED and track field | no tape, no drive (above) — drawn with SPEC.md §47's pen |
| Power cycle (Alt+F12), Paste (Alt+Insert), Copy (Alt+Delete) **as chords** | §7.5 — the caption is VICE's and the menu item is the route |

### 11.3 Program loading

**File > Smart attach... (Alt+A)** opens the Standard File dialog on `.PRG`.
`os88_onfile` — **resident**, because the runtime reaches a callback by a
near offset (§13.1) — refuses by size before touching the disk: a file whose
2-byte load address plus length passes `$FFFF` is refused with that fact, and
the ceiling is 65,533 bytes. It then calls an already-loaded `ovl_*` helper
to do the work:

a transient claim of `ceil(size/1KB)`, `os88_file_read_seg` into it,
`c64_zzcopy_in` into the RAM claim at the load address, `os88_mem_free`.
(`OSAPI_FILE_READ_AT`, slot `0x0358`, exists but is unwrapped and offers
nothing this needs; the package does the arbitrary-address landing itself.)

Autostart is **VICE's RAM-injection mode** — `AutostartPrgMode=1`, the only
one possible without a drive; VICE's own default is
`AUTOSTART_PRG_MODE_DISK` (`autostart-prg.h:45`), a stated deviation forced
by there being no 1541:

1. reset;
2. wait for **`READY.`** in screen RAM (a 4-line check, resident in the slice
   driver, `autostart.c`'s `check("READY.")`);
3. inject at the 2-byte load address;
4. **`mem_set_basic_text(start, end)` unconditionally** — on EVERY load, not
   only at `$0801` (`autostart-prg.c:383`);
5. type `RUN\r` into `$0277` with the count at `$C6`.

**`LOAD"*",8` answers `?DEVICE NOT PRESENT`.** That is the honest machine
with no drive, and it is written here so nobody files it as a defect.

### 11.4 Sound

**SID voice 1's frequency and gate** go to `os88_snd_tone`
(`OSAPI_SND_TONE`, slot `0x00E8`) once per slice, and only when they changed
— one far call. Voices 2 and 3, every waveform beyond the gate, ADSR and the
filters are greyed with §11.2's fact: `OSAPI_SND_FM` and the streaming path
are driver verb protocols deliberately not wrapped for C.

---

## 12. The About panel

`ovl_about_show` in `c64about.c` — 12 rows, modal, the machine **paused while
it is up**, its close drawn as damage and not as a repaint (the `rcabout.c`
shape; the close and hit test stay resident in `c64.c`). Reached from **Help >
About VICE...** and from the kernel's own About item alike (`about_set`).

The rows, from `uiabout.c`, `configure.ac` and `README` 186–290:

```
About VICE
The Commodore 64 Emulator
VICE 3.10
<what this port is>
Copyright 1996-2025, VICE team
GPL-2 or later
ROMs Copyright Commodore Business Machines
                                    [ OK ]
```

It must fit CGA's ~136-row framed content box with OK inside the panel and
the panel inside the content box — the constraint RUNCPM's panel is measured
against in SPEC.md §74.4.

---

## 13. The budget — **PLANNED**

**Every figure in this section is a planning figure and is replaced, wave by
wave, by the measured `os88pkg` line.** SPEC.md §73's cap is **61,440** for
resident image + bss, and SPEC.md §73.9's split trigger is **55,000
resident**.

**Five figures are reported at every wave, separately:** resident image, bss,
`C64.OVL`, resident shims, largest frame. **There is no wave-1 "stub build"
size line** — a core with only the LDA/STA/ADC/branch families assembled is
not an honest measurement of a core, and quoting one would set a budget
against a number nobody can reproduce. **The first honest size line is at the
end of wave 2, with the whole core in.**

### 13.1 The file split

| file | holds | resident |
|---|---|---|
| `apps/c64/c64.c` | the translation unit's root: the GPL-2 + VICE header, prototypes, the key ring, `os88_main` (window, the five-menu set, `about_set`, `onwake` install, the RAM and ROM claims + `read_seg`, **`os88_key_down` armed here**, §7.2), `os88_paint`, `os88_onkey`, `os88_onclick`, **`os88_onfile`** (§11.3), **`os88_onwake` — the slice driver** (§4.4), `os88_worker` (the Exit self-close), the `#include`s in order | yes |
| `apps/c64/c64io.c` | the `$D000-$DFFF` register files and the cdecl dispatch the core calls (§3.4); **the alarm scheduler `_c64_alarm` and "cycles to the next event"** (§4.4); VIC, SID, colour RAM, CIA1/CIA2, the IRQ and NMI lines, the `$00`/`$01` port and the bank-map index (§3.2) | yes |
| `apps/c64/c64kbd.c` | the 152-entry `gtk3_sym.vkm` table (`.data`), the cached matrix and the 16-entry down-list, the once-per-wake rebuild, the scan-routed Ctrl+H/I/M, the Ctrl-held digit poll, RESTORE with the Esc read, the joystick keyset, the PETSCII↔ASCII tables | yes |
| `apps/c64/c64scr.c` | the dirty-page → cell-row mapping, the 1bpp frame shadow, the flush, the `k`-row shift test, the tier table, the EGA-16 luminance table, the border fills, the status row, the dirty-pages-per-wake counter | yes |
| `apps/c64/c64menu.c` | the five menu tables with every string and caption, the `OS88_MENU_DIS` greying with its fact in a comment beside it, the menu-set struct, the `oncmd` dispatcher (two compares, then an `ovl_`) | yes |
| `apps/c64/c64cmd.c` | `ovl_*`: every menu command body — reset, power cycle, Exit, Copy, Paste, warp/pause/advance-frame, swap joysticks, the greyed items' refusal toasts | **no** |
| `apps/c64/c64load.c` | `ovl_*`: the Smart-attach body and the autostart state machine's setup, called by the resident `os88_onfile` (§11.3) | **no** |
| `apps/c64/c64about.c` | `ovl_about_show` (§12) | **no** |
| `apps/c64/c64cpu.inc` | the 6510 core (§4) | yes |
| `apps/c64/c64mem.inc` | the movers (§3.6) | yes |
| `apps/c64/c64band.inc` | the composers (§9.5) | yes |
| `apps/c64/c64.asm` | the shim: `CC_PKG_NAME 'C64'`, `CC_HAS_ONKEY`/`ONCLICK`/`ABOUT`/`ONWAKE`/`MENUS`/`FDLG`/`WORKER`/`OVL`, `CC_ICON`, `%include cc/crt0.asm`, `c64.gen.asm`, then the three `.inc`s, `CC_IMAGE_END` | yes |
| `apps/c64/icon.inc` | the 16×16 1-bit breadbin, drawn for this port | yes |
| `apps/c64/COPYING` | VICE's GPL-2 text (§1.2) — in the repo, not on the floppy | — |
| `apps/c64/rom/` | the three ROM binaries + `README.md` (§1.3) | — |

**`CC_HAS_OVL` is on from the first commit** and `C64.OVL` exists from wave
1 — the alternative is discovering at 55,000 that the code is not the kind
that can move (LESSONS 5).

### 13.2 The planning figures

| | planned |
|---|---|
| resident image | **39,000** (range 38,000–40,000) |
| bss | **13,600** |
| `C64.OVL` | **6,000** |
| **resident total** | **52,600** of 61,440 — 2,400 under SPEC.md §73.9's 55,000 trigger, 8,840 under the cap |

**Basis** — measured ratios, not cword's 5.9 bytes/line. RUNCPM ships image
39,412 + `RUNCPM.OVL` 7,389 for 4,679 lines of C once ~9.6KB of
`rcz80`/tables/movers, ~6KB of crt0/thunks and ~1.5KB of strings and icon
come out: **~6.3 bytes per line of C**.

Resident image:

| | bytes |
|---|---|
| C, resident: `c64.c` ~800 + `c64io.c` ~800 + `c64kbd.c` ~400 + `c64scr.c` ~750 + `c64menu.c` ~300 = ~3,050 lines × 6.3 | ~19,200 |
| `c64cpu.inc` at `rcz80.inc`'s measured size — a 6510 handler is not smaller than a Z80 one here: every reader carries the bank test, every writer the two-range test and the dirty-bit OR, and there are eight addressing modes per ALU op | ~9,600 |
| `c64band.inc` ~400 lines, `c64mem.inc` ~220 lines | ~1,900 |
| crt0 + thunks (`ccsmoke` alone is 3,406) | ~6,000 |
| **overlay call/loading shims — these stay RESIDENT** | ~600 |
| `.data`: the 152-entry vkm table (608), the ×2 byte→word table (512), the 6510 cycle table (256), the seven bank maps (112), the luminance/EGA tables (48), the dirty-bit mask table (256) | ~1,800 |
| strings: 5 menus × ~10 items × ~30 bytes of `.vhk` caption (~1,500), About (~300), toasts (~500) | ~2,300 |
| icon | 128 |
| **ROMs — 0 bytes of image** (§1.4). Embedded they would have been 39,000 + 20,480 + 13,600 = **73,080, refused on paper**, which is what decided the sidecar | 0 |

bss:

| | bytes |
|---|---|
| the 1bpp frame shadow, 320 × 200 bits (§9.3) | 8,000 |
| colour RAM (1KB of nibbles) | 1,024 |
| the composed band (8 rows × 40) and the ×2 band (16 rows × 80) | 1,600 |
| sprite composition staging | ~520 |
| VIC 47 + SID 29 + CIA 2×16 | ~110 |
| the cached matrix 8, the down-list 16 × 2, per-key state 32 | ~72 |
| key ring 64, paste feeder 256, status line 48 | ~370 |
| device phase and alarm state, span and dirty-row tables | ~140 |
| two-word counters, About and misc, SmallerC statics and out-parameters | ~1,700 |
| | **~13,600** |

The core's own hot scratch is **not** in this table: it is 64 bytes of the
C64's own RAM (§3.5).

### 13.3 The overlay rules that bind here

- **Split by FREQUENCY, never by size** (SPEC.md §73.14): a keystroke's path
  stays resident, a menu command's goes out.
- **Every callback is resident** — `os88_paint`, `os88_onkey`, `os88_onclick`,
  `os88_onwake`, `os88_onfile`, `os88_worker` — because the runtime reaches
  one by a near offset. A callback that needs overlay code calls an
  already-loaded `ovl_*` helper (§11.3).
- **The `.OVL` cannot be loaded from `os88_main`** (LESSONS 13) — there is no
  instance yet. The first `ovl_*` call is made **from the first wake**, and
  its refusal prints **`Unable to load C64.OVL.`** in the status row *and*
  toasts, because a toast under a fullscreen window is not where the user is
  looking (§9.8).
- `C64.O88`, `C64.OVL` and `C64.ROM` are **three files in one folder** on
  every disk they share (SPEC.md §19.2.1, SPEC.md §19.9) — §14.2.

---

## 14. Names, disks, targets, machines and harnesses

### 14.1 Names

| | |
|---|---|
| package name | `C64` |
| source | `apps/c64/` |
| shipped files | `C64.O88`, `C64.OVL`, `C64.ROM` |
| window title | `VICE (C64)` |
| menu-set `AM_NAME` | `VICE` |
| images | `build/c64.img` (1.44MB), `build/c64720.img` (720KB), `build/c64360.img` (360KB) |
| tools | `tools/c64rom.py` (builds `C64.ROM`, §1.4), `tools/c64prg.py` (writes `.PRG` fixtures, §14.4), `tools/c64ref.py` (the reference compositor, §14.5) |

The name is checked against `apps/`, `vm/`, the Makefile and `build/` before
wave 1 (LESSONS 1's rule about two programs sharing an ambition).

### 14.2 Disks

Three geometries, each `os88disk.py --verify`'d in the recipe. Each carries
**`C64.O88` + `C64.OVL` + `C64.ROM` in one folder**, plus a `README.TXT`
naming the licence and pointing at `apps/c64/COPYING` and carrying the ROM
copyright line (§1.2, §1.3).

**No `.PRG` programs ship.** VICE's tree contains none to ship, every
candidate needs its own licence check, and what is worth shipping depends on
the measured speed. `tools/c64prg.py` writes the test fixtures from BASIC
listings (§14.4). A released C64 disk boots to `READY.` and the user types.

On `apps-all.img` the package gets **a folder of its own, `C64\`, never a
place in `APPS/`** (SPEC.md §19.9, SPEC.md §19.10) — three files that must
resolve in one directory.

### 14.3 The three 86Box machines

Each is a **copy of a machine that has booted**, with the B: image
(`fdd_02_fn`) and the uuid changed and **nothing else** (LESSONS 9: 86Box
substitutes a default for an unrecognised key and rewrites the config on
exit; `git checkout` the cfg before committing and never commit `nvr/`).

| machine | copied from | B: | target |
|---|---|---|---|
| `vm/xt-c64` | `vm/xt-runcpm` — IBM XT, 8088 at 4.77 MHz, 640KB | `build/c64360.img` | `make xt-c64` |
| `vm/286-c64` | `vm/286-runcpm` — AMI 286 at 12.5 MHz | `build/c64720.img` | `make 286-c64` |
| `vm/386-c64` | `vm/386-runcpm` — 386DX | `build/c64.img` | `make 386-c64` |

All three ship. `vm/386-c64` is the machine the port is *looked at* on and
lands in wave 1; the other two land in the final wave. Each target is the
three-line `$(UNPROTECT)` / `$(BOX)` pattern the RUNCPM machines use in the
Makefile, and all three are documented in `README.md` (the command list and
the machine table) and in `CLAUDE.md`'s machine list.

**`RESET=1|cmos|flash|both` clears a stale CMOS on the way in.**

**These machines are MANUAL evidence** (§14.6). `make 386-c64` launches
86Box; it cannot assert that anything booted, and no gate in this port may
rest on it.

### 14.4 The fixtures

`tools/c64prg.py` is a **deterministic BASIC V2 tokeniser**: a listing in, a
`$0801` `.PRG` out, plus a raw-bytes mode for a hand-assembled poke loop. It
writes onto a scratch copy of an image through `tools/os88disk.py`. It exists
because the port itself cannot write a `.PRG` and the loading wave needs
something to load.

### 14.5 The harnesses and benches — automated evidence

| | what it does |
|---|---|
| `apps/c64/hosttest/c64uitest.c` | the whole program over a stub `os88.h` with a model of the glass and a **scripted core** (the real `c64io.c` behind it): drives the level keyboard press-poll-release, the alarm path, the autostart state machine, Copy/Paste and the About panel key by key; asserts glass == model == 1bpp shadow after every step; prints §9.7's cost table and the dirty-pages-per-wake counter. Run by `build.sh` before every build |
| **`tools/c64ref.py`** | **an independent, pixel-level reference compositor.** Python, written from VIC-II documentation and VICE's `src/vicii/` as the authority, **not** from `c64band.inc`: it renders the same C64 memory to a 320×200 1bpp image, and the harness compares it **bit for bit** against what the package composed. This is what validates hires bitmap, multicolour, a custom character set, the cell transpose and sprite priority/expansion — a cell-identity glass model provably cannot |
| `apps/c64/hosttest/c64cputest.asm` + `.sh` | §4.6's nine rows with their negative controls — `make c64cputest`, minutes, not in `build.sh` |
| `apps/c64/hosttest/c64memtest.asm` + `.sh` | §3.6 — `c64mem.inc` **and** `c64band.inc`'s string loops under `SS ≠ DS` with an `ES` sentinel and an ES-not-restored negative control. Run by `build.sh` |
| `tests/c64band` | `make c64bandbench` — the icount bench pricing `c64_band1` (text, bitmap, multicolour) **per cell and per call**, `c64_band_x2` at 8 and 16 rows, `c64_rowspan` and `c64_rowshift`. **§9.7's milliseconds and §9.8's tier table are written from these numbers**, and they become a new Set in PERFORMANCE.md |
| QEMU + QMP | `make test TESTAPPS=build/c64.img`, `tools/mouse.py`, `tools/qmp.py sendkey`, `tools/shot.py --crop --zoom`; `VIDEO=cga` with `--screen 640x200`, `VIDEO=herc` with `tools/hercshot.py`. Every screendump assertion lives here |

### 14.6 Manual evidence — and the line between them

**Automated evidence** is everything in §14.5: it runs unattended, it fails a
build or a tier, and every `done_when` in `docs/C64-PORT-PLAN.md` rests only
on it.

**Manual evidence** is the three 86Box machines (§14.3) and anything read off
them by a person: the chord table (§7.5), the `% cpu` and `fps` figures on a
386 and on an XT, the look of a scroll, the feel of keystroke latency. It is
**recorded here as a reading with its date and machine**, and it is never a
gate — a `make` target that launches a GUI emulator cannot assert a boot.

---

## 15. What this port adds to the kernel and the SDK

**One thunk.** Nothing else in the kernel changes, and no budget constant
moves.

### 15.1 `os88_key_down` — the level keyboard's state

| | |
|---|---|
| **need** | key STATE for the level keyboard model (§7.2), the joystick (§8) and CTRL+digit (§7.3). `os88_onkey` delivers presses only |
| **slot** | `OSAPI_KEY_DOWN` — slot `0x03F0`, SPEC.md §9.7. `AL` = a make scancode, `CF` = down, every register kept; **asking is what arms it, and the first ask clears the map** (§7.2's rule 1); advice, not an oracle. The kernel already tracks every break code (`kernel/mouse.inc:1438`, `kbd_track`, `KBD_MAPSZ` 16 = all 128 make codes), and `apps/arkanoid` and `apps/cyclone` already use it |
| **action** | add `int os88_key_down(int scan)` to `apps/cc/os88thunk.asm` and `apps/cc/os88.h` (`CC_T_A1`-shaped, CF → 1/0, about a dozen lines) **and the `hosttest/os88.h` stub in the SAME edit** |

### 15.2 Slots used as they stand — no change

| need | slot | note |
|---|---|---|
| a slice loop on the UI task, no blocking, file slots legal | `OSAPI_WM_WAKE` slot `0x0450` / `OSAPI_WM_ONWAKE` slot `0x0458` (wrapped; `CC_HAS_ONWAKE`) | used exactly as RUNCPM does (SPEC.md §74.1) |
| a time base for the flush and the speed widget | `os88_ticks()` — the 18.2 Hz tick | the machine's own clock is emulated cycles (§4.2); `OSAPI_WM_TIMER` stays unwrapped, the wake is the re-post |
| the CPU tier that seeds the wall slice | `OSAPI_CPU_INFO` slot `0x0188` (wrapped) | §4.4, §9.8's tier table |
| fullscreen on Alt+D | `OSAPI_FULLSCREEN` slot `0x0110` (wrapped) | §9.8; **verify on a real BIOS** that the chord reaches `os88_onkey` as ascii 0 / scan `0x20` unconsumed — on `vm/386-c64` and `vm/xt-c64`, not only under QEMU's SeaBIOS |
| a scroll moved, not redrawn | `OSAPI_GFX_SCROLL` slot `0x01F8` (wrapped) | §9.4; falls back to spans on −1 |
| a composed span down in one call | `OSAPI_GFX_BLIT1` slot `0x0418` (wrapped) | §9.5; glyphs come from CHARGEN or the RAM charset, not `OSAPI_FONT_GLYPHS` |
| voice 1 | `OSAPI_SND_TONE` slot `0x00E8` (wrapped) | §11.4 |
| a self-close for Exit emulator | `OSAPI_WM_CLOSE` slot `0x0470` exists **unwrapped** | keep RUNCPM's worker idiom (`CC_HAS_WORKER`); a `WM_CLOSE` thunk is a follow-up, not a requirement |
| a `.PRG` read to an arbitrary, non-512-aligned address | `OSAPI_FILE_READ_AT` slot `0x0358` exists unwrapped | **no new slot**: §11.3 does it in the package with a scratch claim and `c64_zzcopy_in`. The ROM claim is `read_seg`'d directly — 20,480 is 512-aligned and starts at the claim's base (§1.4) |

### 15.3 The slot that is NOT added

**Colour.** There is no slot for a band with an ink and a paper, and this
port does not add one: it spends kernel `.text` against `KERN_CODE_MAX`,
which `docs/KERNEL-MEMORY.md` puts at roughly 512 bytes spare, and
"raising either is a decision to take with whoever asked for the feature, not
a build fix." **The feature is greyed with §9.6's fact**, the EGA-16 map and
the per-row colour record are kept so the slot drops in without re-planning,
and the slot itself is a follow-up whose case is made by the measured 8088
speed.
