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

**A disk without `C64.ROM` says so on the glass**, naming the file. The
machine is not started; the window IS.

That is a wave-1 amendment and it was made by a screendump. The first build
returned 0 from `os88_main` and toasted `C64: no C64.ROM` — and what reached
the glass was the KERNEL's own `Load failed`, because a refused launch raises
its own toast over the package's. So the refusal now takes LESSONS.md 13's
RUNCPM shape: the window comes up, the screen area says which file is missing
and what it is for, the status row carries `C64.ROM missing - see README.TXT`
as a permanent line rather than a message that expires, and the toast goes out
as well (§9.8's both-routes rule). A claim that cannot be had is still a
refused launch and still quotes `os88_mem_largest_kb()` (§3.1): there is
nothing to put a window on when the memory is not there.

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
| the 16 colours, their order and their LUMINANCE ladder | `src/vicii/vicii-color.c` — `vicii_colors_6569r5` (`:441`), which is what VICE 3.10 as shipped compiles and selects for a PAL C64: `TOBIAS_COLORS` is defined (`:52`) and `PEPTO_COLORS`/`COLODORE_COLORS`/`MARKO_LUMAS` are not (`:43-49`), so the switch at `:672` picks it for `VICII_MODEL_6569`. **NOT `data/C64/vice.vpl`**: an external palette file is loaded only when `${CHIP}ExternalPalette` is set and that resource factory-defaults to 0 (`src/video/video-resources.c:393`; `pepto-pal` at `src/vicii/vicii-resources.c:170` is only the name it would take). To 1bpp by luminance on every adapter in this port; the EGA-16 map kept in `c64scr.c` for the day colour bands exist — §9.6 |
| window title `VICE (C64)` | `src/arch/gtk3/ui.c:1842` (`"VICE (%s)"`, `machine_get_name()`) + `src/c64/c64.c:179` (`machine_name = "C64"`) |
| status bar: message area, `Tape:` (greyed), `Joysticks:` two 5-dot indicators, drive 8 with its track counter ` 18.5` (greyed), the speed widget's two labels `%7.0f%% cpu` and `%8.1f fps` (`CPU_DECIMAL_PLACES` 0, `FPS_DECIMAL_PLACES` 1) folded onto one row, the warp and pause LEDs as two labelled lamps `W` and `P`, inverted when lit (§10.2); Recording, Volume, CRT and Mixer dropped with the row-width fact stated in §10.3 | `src/arch/gtk3/uistatusbar.c` (line 1403 track counter, 1961/1971 volume, 2594 recording, 2670/2679 CRT/Mixer; the LEDs are created at `:2607-2616` and appended by `statusbar_append_led` `:2339-2362` into `led_row_grid` — a SEPARATE row at column 0, not the speed widget's cell — each carrying a text label, `statusbar_led_widget_create("warp:", …)` / `("pause:", …)`), `src/arch/gtk3/widgets/statusbarspeedwidget.c:572`, `:653`, `:467` |
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
| Alt+D | Fullscreen | **implemented, both directions** (§9.8) — it is not in this table's "cannot deliver" sense but is listed here because it is the one chord the port must dispatch itself: a `WF_FULL` window has no menu bar, so it is the only way back |
| Alt+Insert | Paste | an AT BIOS `int 16h AH=0` (`kernel/mouse.inc:1426`) drops the enhanced code `0x8B`/`0xA2`; the menu item is the route |
| Alt+Delete | Copy | the same, code `0xA3` |

They work where the BIOS passes them and are captioned exactly as VICE
captions them either way.

**EVERY OTHER CHORD THE MENU ADVERTISES IS DISPATCHED BY `os88_onkey`**, and
that is a wave-1 fix: a caption is not an accelerator in this kernel — SPEC.md
§12.2's bar binds none of them — so `Alt+A`, `Alt+F9`, `Alt+Q`, `Alt+W`,
`Alt+P` and `Alt+J` were printed beside their items and fell straight into the
C64's key ring, which is a promise on the glass that the machine did not keep.
They go to `os88_oncmd`, the same helper a menu pick reaches, so a chord and a
pick cannot drift apart; an Alt chord is as rare as the command it stands for,
so the overlay call it makes is on the right side of SPEC.md §73.14's
frequency split. **The chord table** — Alt+D, Alt+F9, Alt+F12,
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

Authored **338 × (`TITLE_H` + 226 + 1)** at (7, 20) for a **CONTENT box of
336 × 226**: 320×200 of C64 screen, an 8-pixel border on every side, and a
10-pixel status row. **The content height is `W_H − TITLE_H − 1`** —
LESSONS 13's finding, where a window authored `TITLE_H + 200` showed 24 rows
and a sliver. `wm_snap` puts the content x on a cell boundary, which is what
lets `OSAPI_GFX_SCROLL` accept the rect (§9.4).

**AND THE CONTENT WIDTH IS `W_W − 2`, WHICH IS WHY THE FRAME IS 338** (amended
in wave 1's review, which found this on the glass). `os88_wm_create` authors a
FRAME and `os88_wm_geom` answers the CONTENT box: `kernel/wm.inc:5442` is
`sub cx, 2`, the window's two 1-pixel side borders. The first draft authored
336 as the frame and then laid §10.1's 42-cell = 336-pixel status row inside
the 334 pixels that left, so the last cell — the pause lamp `P` at 328..335 —
needed pixels the box does not have: on the first paint, whose clip is the
frame, it spilled two pixels onto the window's right border, and on every
later flush-driven redraw, whose clip is the content, it was dropped
entirely. **`P` was never on the glass in a windowed C64**
(`build/port-shots/w1r2-lamps-on.png` shows the row reading `8` + an inverted
`W` and nothing after it). The centring said the same from the other side:
`(334 − 320) / 2 = 7`, clamped up to the 8-pixel border, so the border was 8
left and 6 right against this section's own sentence. With 336 of content the
row is 42 whole cells, the border is 8 on every side again, and the scroll
rect's `x1` and `x2 + 1` are both multiples of 8 by construction rather than
by `c64_geom`'s `& ~7` clamp. `hosttest/c64uitest.c` asserts
`C64_W_W − 2 == GW`, because its own `GW` was the frame width and that is
exactly why nothing could see this.

**226 of content is a 480-line number, and the window ASKS the adapter before
it authors one** (amended in wave 1's review). `os88_main` calls
`os88_video()` and clamps the height to `dock_top − 20`; a 200-line CGA
desktop cannot give 226, and authoring it anyway left `wm_fit` to clamp the
window with the **status row off the bottom** — the row that carries §1.4's
permanent `C64.ROM missing` fact and every refusal
(`build/port-shots/wave1-cga-launch.png` is the first draft doing exactly
that). The flush then reads the LIVE content box every time it runs:

- the status row is at `content.y + content.h − 10`, whatever that is;
- the cell rows drawn are `(status_y − border − content.y − border) / 8`,
  clamped to 25;
- the bottom border fills between the last drawn row and the status row.

So a clamped window shows fewer C64 rows and keeps its status row, rather than
showing 25 rows it does not have and losing the row that explains itself.

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

**And the core keeps a WRITE WINDOW beside the bitmap** — the lowest and the
highest address written since the last flush, two words of scratch (§3.5) and
a few instructions on the write path. This is a wave-1 amendment, and the
reason is arithmetic: a 256-byte page is 6.4 character rows, so a page-granular
bitmap alone can only ever say *"recompose these seven rows, all forty
cells"*, and §9.7's `one changed cell composes one cell` is unreachable by
construction. Measured on the harness, that is the difference between
**3.7 ms and 28 ms** for one changed cell. Colour RAM keeps its own window in
the package, because colour RAM is not in the claim (§3.1).

**THE WINDOW IS TAKEN OVER A WATCH RANGE, NOT OVER THE ADDRESS SPACE**
(amended in wave 1's review, and the review was right that the first draft
degenerates the day the core runs). Two more scratch words, `C64_SCR_WATLO`
and `C64_SCR_WATHI`, hold `mbase .. mbase+999` — the screen matrix — written
by the C from `c64_frame_regs` and re-written the moment `$D011`, `$D016`,
`$D018` or CIA2 `$DD00`'s bank bits move the matrix. `_c64_wr` widens the
window only for an address inside that range: two compares and a branch, the
same order of cost the four unconditional updates already paid.

Without it the mechanism is exact while the only writer is `c64_poke_boot`
and **useless from the first slice of a real machine**: every `JSR` writes the
stack at `$01xx`, every BASIC statement writes zero page and the variables
above `$0800`, so within one slice the window is `[~$0100, >$0800]` and spans
every matrix row in full. The per-row intersection then always answers "all
forty cells", the four instructions and the four scratch bytes buy nothing,
and every per-cell figure in §9.7 describes a machine with no CPU in it.

**And the dirty ROWS of the matrix are the window INTERSECTED WITH the pages.**
Neither structure can do it alone, and the first draft used one of them:

- The **pages** alone are 6.4 character rows each, so one changed cell marks
  seven rows and §9.7's *one changed cell composes one cell* is unreachable.
- The **window** alone is ONE `lo/hi` pair over the whole matrix, so two pokes
  in distant rows — a score at row 0 and a status line at row 24, which is
  what a game and the KERNAL both do inside one flush interval — span every
  row between them. Measured: **25 recomposed rows, 1000 cells, ~299 ms** for
  two changed bytes.

Row `i`'s forty matrix bytes lie in **at most two** 256-byte pages, so row `i`
is dirty when the window's row range `(wlo − mbase) / 40 .. (whi − mbase) / 40`
covers it **and** one of those two pages has its bit set. The window then
still narrows the SPAN inside each dirty row (`c64_span_of`), so one changed
cell is one cell, and the two distant pokes cost the seven rows of page 4 plus
the six of page 7 — **13 rows, 481 cells, 125.6 ms and 2 blits**, the harness
row `two pokes, rows 0 and 24`. That row is the gate: it fails if the
intersection is dropped.

The page bitmap also stays for what the window cannot see — a RAM character
set today, bitmap and sprite sources in wave 4 — and for the "dirty pages per
wake" counter.

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

**TWO FLAGS, AND THEY MEAN DIFFERENT THINGS** (amended in wave 1's review).
`c64_rowd` says THE SOURCES changed — `0` clean, `1` a span inside the write
window, `2` the whole row, which is what a frame-register write sets because
the window says nothing about it. `c64_force` says THE GLASS is unknown, over
a **cell span** `c64_fc0..c64_fc1`: a scroll's vacated rows, a partial expose,
the rect an About panel has stopped covering.

Setting both for a register write — which the first draft did — makes every
write to `$D011`, `$D016`, `$D018` or `$DD00` cost **25 forced full-width
blits, ~234 ms**, with the frame compare that exists to answer *"nothing
changed, draw nothing"* switched off. Two ordinary things reach it: a raster
interrupt's `LDA #$1B / STA $D011` fifty times a second, and a smooth-scroll
program writing `$D016` once a frame. So:

- **every one of those registers is guarded by VALUE** — a write that stores
  the byte already there returns without dirtying anything;
- **`$DD00` is guarded by its two BANK BITS**, because the KERNAL bit-bangs
  the serial bus through that register's other six and a `LOAD` from drive 8
  was hundreds of full-screen repaints a second;
- a genuine change sets `c64_rowd = 2` and **not** `c64_force`: the row is
  recomposed and then compared, so a `$D016` write that draws the same
  picture composes 25 rows and draws **nothing**. That is the harness row
  `a $D016 change that draws the same picture — 0 blit`.

`c64_force`'s cell span matters for the same reason: a menu closing over this
window is a damage rect about 190 px wide (`MENU_MAXCH` is 24 glyphs), and
forcing its thirteen rows full width composed 13 × 40 cells where 13 × 24 was
the answer — 122 ms against 75. **And the force path does not FILL first:**
the composed band arrives in final screen polarity, so filling the damage
black and blitting over it is the erase-then-letter pair PERFORMANCE.md's
rule 2 names.

### 9.4 The scroll test — a shift of **k = 1..24** rows

Flushing once per host tick means several rows can have scrolled since the
last one. So the shift test is not the one-row test:

- For `k = 1..24`, test whether cell row *i* matches shadow row *i + k*. The
  test is on a 16-bit **signature of the row's SOURCES** — its forty matrix
  bytes and its forty colour nibbles, `c64_rowsig` (§9.5) — and not on
  composed pixels, because composing 25 rows to find out is the cost the
  scroll exists to avoid. Within one frame the character generator and the
  background are fixed, so equal sources compose to equal pixels.
- **The signature is a HINT and nothing rests on it.** A 16-bit signature can
  collide. After the scroll is emitted the shadow is moved with it and every
  row still goes through `c64_rowspan` against the moved shadow, so a row that
  did not really shift simply compares unequal and is drawn. The scroll is an
  optimisation whose failure mode is a redraw, never a wrong screen.
- **It is only asked when at least four fifths of the rows ON THE GLASS are
  dirty** (`C64_SHIFT_NUM`/`C64_SHIFT_DEN`, which is 20 of 25 on a full
  window), and that threshold is measured rather than tasteful. It is a
  FRACTION and not the count the first draft used, because `nrows` is not 25
  on every adapter: on a clamped 200-line window an absolute 20 is a
  threshold the screen can never reach, so **no scroll would ever be detected
  on the target machine** and every BASIC scroll would pay the whole-frame
  path. A
  scroll dirties the whole matrix by definition — the KERNAL moves 960 bytes
  and clears 40. A one-row change dirties the rows of the two pages it
  touches, which is fourteen of them; at a threshold of eight the test ran,
  found a spurious match on a screen with several blank rows, and emitted a
  scroll the span compare then had to undo — 1 scroll and 24 blits where 1
  blit was the answer. Correctness survived it. The cost did not.
- **The test runs over the rows ON THE GLASS, `0..nrows-1`,** and so does the
  compose loop and the re-signing. The flush never writes `c64_shsig[]` past
  `nrows`, so a test over all 25 compares live signatures against power-on
  zeros and fails for every `k` — the defect above, stated as its mechanism.
- **The row signatures are updated only for rows the flush RECOMPOSED.**
  Re-signing all 25 at the end of every flush cost 25 × 1.03 ms — half a host
  tick, on every flush, for a keystroke that touched one row. A row that was
  not recomposed did not change its sources either.
- **And when the shift test has run, its signatures are READ rather than
  taken again.** The test fills `c64_sig[]` from the live sources; the
  compose loop then wrote the identical value per recomposed row — the gfx
  lock is held for the whole flush, so nothing can have moved in between —
  which is another 25 × 1.03 ms on the one path that recomposes everything.
  The post-scroll slide of `c64_shsig[]` is the same copy and costs no
  `c64_rowsig` call at all. Measured: a `k = 9` shift, **281.7 ms → 256.0 ms**.

- On a hit, emit **one `OSAPI_GFX_SCROLL` (slot `0x01F8`) plus the `k`
  vacated rows** — `k + 1` calls, not 25.
- **THE `dy` IS POSITIVE, BECAUSE POSITIVE MOVES THE CONTENT UP** (SPEC.md
  §5.5, `os88.h:437`, `kernel/vga12.inc:3927`, and `apps/runcpm/rcterm.c:631`
  passing `rc_scr_n << 3` with its vacated rows at `rows − n`). Wave 1's first
  draft passed `−k * 8` — the one argument negated in a block otherwise
  written entirely for content-up — so a BASIC scroll slid the picture DOWN,
  left the top `k` rows unspecified and never forced them, and left the shadow
  claiming the opposite slide, which switches the span compare off for every
  row of every later frame. It shipped because **the harness's own
  `os88_gfx_scroll` stub modelled the opposite convention** (it implemented
  only the `dy < 0` branch, as content-up, with the sentinel at the bottom),
  so the package and the model were wrong together and the call counts
  matched. The stub is now §5.5's, both directions and all four refusals, and
  the k = 9 step checks that **the pixels moved UP** against a snapshot taken
  before the scroll — a count cannot see a direction.
- **AND EVERY SHIFTED ROW IS STILL COMPOSED AND COMPARED**, which is the
  bullet above stated as a cost that is not negotiable. Skipping the rows the
  test matched — 16 of them on a `k = 9` scroll, 188 ms of compose and 24 ms
  of compare — was proposed on the grounds that the shadow bytes for those
  rows were composed from those same sources and just `memcpy`'d into place.
  It makes the 16-bit signature PROOF, and a collision is trivial to build:
  `c64_rowsig` is an XOR under a per-cell rotate, so a cell's contribution is
  `rol(v, 40 − i)` and two rows differing in one cell each, one column apart,
  collide whenever the second difference is the first rotated once — screen
  code 34 in column 5 against screen code 36 in column 6, over an otherwise
  blank row, is such a pair, and they are different glyphs. The scroll is
  emitted (that is what the hint is for) and the row that did not really
  shift is then the only thing standing between the shadow and a wrong row
  **that never repairs itself**, because after that flush nothing is dirty.
  `hosttest/c64uitest.c` drives that exact pair and checks the glass against
  the row's own SOURCES — the one question `audit()` cannot ask, since a
  package that slides its shadow the same wrong way agrees with itself. The
  signature saves the SCROLL, not the compare.
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
| `c64_rowspan(a, b, n)` | compares a composed band against the frame shadow — eight pixel rows of `n` bytes at stride 40, both in the package's own segment — and answers `(first << 8) \| last`, the differing CELL columns, or −1 for "same". Both scans are `repe cmpsb` and the second sets `DF` on purpose, which is why §3.6's harness covers this file |
| `c64_rowcopy(dst, src, n)` | brings the shadow up to date with what was drawn |
| `c64_rowsig(mseg, moff, col, n)` | §9.4's shift test: a 16-bit signature of one row's SOURCES. Named `c64_rowshift` in the draft; it takes a row and answers a number, because the k-loop belongs in the C where it costs nothing |

**The composer takes a span, never "always 40 cells."** A one-cell change
composes one cell. This is the difference between a keystroke costing
~1 ms and ~8 ms.

The glyph bytes come from the **CHARGEN ROM in the claim** or from the RAM
character set the VIC is pointed at — never from `OSAPI_FONT_GLYPHS`. This is
a C64 face.

Each composed span goes down in **one `OSAPI_GFX_BLIT1`** (slot `0x0418`); a
−1 (a `kern_small` kernel, or a broken argument) falls back to the font path
— **and that is a DRAW, not a deferral.** `c64_row_font` maps the row's screen
codes to the kernel's own face and emits the span as one `os88_font_run`, once,
and the shadow is updated with the composed band anyway: on this path the
shadow stops being "the pixels on the glass" and becomes a proxy for THE
SOURCES, which is still exactly what the compare needs — if the composed band
equals it, the sources decode the same way and the font path would draw the
same characters. Wave 1 shipped two wrong answers before this one and the
harness gates both: discarding the −1 and updating the shadow anyway (a
permanently blank screen the compare then refuses to repair — the outcome
SPEC.md §47 exists to prevent), and keeping the row OWED (the wake re-posts,
the row recomposes, and is refused again, for ever). What the fallback cannot
carry is stated rather than faked: it is not the C64's face, a reverse-video
cell is drawn plain, and a graphics cell has no glyph in this face and is
drawn as a dot — so the status row says `No bands here - text only.` the first
time it happens, on both routes (§9.8).

### 9.6 Monochrome, by luminance, on every adapter — **a fact, not a limitation being worked around**

**The C64 screen is 1bpp through `OSAPI_GFX_BLIT1` on every adapter, VGA
included.** Every one of the C64's 16 colours is resolved to black or white by
a luminance threshold, and multicolour text, multicolour bitmap and
multicolour sprites are thresholded the same way.

**The luminances are the VIC-II's own ladder and not a palette file's.** The
table is `vicii_colors_6569r5`'s Y column (`src/vicii/vicii-color.c:441`) —
the one VICE 3.10 as shipped compiles and picks for a PAL C64 (§2's authority
row has the `#ifdef` chain). **It has NINE levels for sixteen colours, shared
in seven pairs**: blue = brown (0.237), red = dark grey (0.306), purple =
orange (0.363), medium grey = light blue (0.461), green = light red (0.500),
cyan = light grey (0.639), yellow = light green (0.763). The first draft
derived the table from `data/C64/vice.vpl` — a file VICE does not read by
default — which made all sixteen distinct (yellow 212 against light green
185), so the mono glass showed contrast in seven places where a real C64 and
VICE show a flat field, and `c64_lum_update`'s *"the same colour: a uniform
cell"* branch, written for exactly this case, was unreachable code.

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

**`tests/c64band` answered on 2026-08-21 and was RE-TAKEN on 2026-08-22 under
an armed clip**, under `-icount shift=3,sleep=off` where one PIT count is
0.359 ms of real 4.77 MHz XT (PERFORMANCE.md Part 4) and the bench takes
N = 8 iterations a row.

**THE CLIP IS THE RE-TAKE, and it is 28% of a line of text.** The bench's
rerun draws from `W_ONKEY` and `W_ONCLICK`, and the kernel arms a clip region
for `W_PAINT` and for nothing else (SPEC.md §11.3) — so the first draft drew
over anything covering it and timed the kernel's *unclipped* paths, which
`c64_flush` never takes: `os88_onwake` arms the clip before it, and a paint
arrives with one armed. Measured both ways in one session: a 40-cell
`font_run` is 718 counts unclipped and **922 clipped**, a 320×8 `blit1` 40 and
**47**; the composer's own rows do not move at all, because they are package
code and never ask the kernel.

The `FONT_RUN` row is the bar taken in the same run, and it is also the
calibration check: 922 counts ÷ 8 × 0.359 = **41.4 ms** against
PERFORMANCE.md's model of 40 × 900 µs + 756 = 36.8 ms for the same forty
cells. The model is the machine to 12%, and the 12% is the clip.

| the bench row | counts | per operation |
|---|---|---|
| `FONT_RUN` 40 aligned — the bar | 922 | 41.4 ms |
| `c64_band1` 40 cells | 168 | 7.54 ms |
| `c64_band1` 1 cell | 8 | 0.36 ms |
| `OSAPI_GFX_BLIT1` 320×8 stride 40 | 47 | 2.11 ms |
| `c64_rowspan` 40 equal | 35 | 1.57 ms |
| `c64_rowspan` 40 differing | 38 | 1.71 ms |
| `c64_rowcopy` 40 | 35 | 1.57 ms |
| `c64_rowsig` 40 | 24 | 1.08 ms |
| `c64_band_x2` 8 rows | 450 | 20.19 ms |

**And the bench SAVES ES around every `OSAPI_GFX_BLIT1` now**, because a
window callback is entered with `ES = KERNEL_SEG` (SPEC.md §20.1) and must
return it that way: the first draft did `push ds / pop es` at all three blit
sites and never put it back, so it returned the wrong ES into the kernel and
timed a body that omits the preservation the shipping code has to do. **It
also PREFLIGHTS the blit**: `OSAPI_GFX_BLIT1` answers CF = 1 refused with
nothing drawn, and the timed bodies ignored that — so on a `kern_small`
kernel the bench printed attractive numbers for a call that draws nothing and
published them as the cost of drawing a band. Every row whose body contains a
blit now prints `REFUSED (CF=1)` instead of a number.

Solving the two `c64_band1` rows — which the clip does not touch: **a composed
CELL is 184 µs and the call floor is 175 µs.** Those two are `C64BENCH_CELL` and `C64BENCH_CALL` in
`c64scr.c`, and `hosttest/c64uitest.c` prices its whole cost table from them —
a gfx call at PERFORMANCE.md's 756 µs floor plus 3.4 µs a band byte, which is
what the 320×8 row above says a blit's pixels cost.

**Re-taken after the wave's FIX pass.** Every row moved by about half a
percent, because the two constants the model prices a compare and a signature
from were re-measured with the bench (`C64BENCH_SPAN` 1530 → 1571 µs,
`C64BENCH_SIG` 1030 → 1077 µs); the two scroll rows are now measurements of a
scroll that goes the right way, which the earlier ones were not; and two rows
are new — the signature collision, and a changed cell on a kernel whose
`blit1` refuses.

**Re-measured after wave 1's review**, with the harness now writing the stack,
zero page and BASIC's variables between flushes (`h_noise`) so that the
per-cell rows describe a machine with a core rather than one without — **and
re-taken again after the review's second pass, which found the model priced
NO TEXT.** `os88_font_run` and `os88_font_str` were counted and then left out
of the sum, so every glyph this package draws was charged zero: the About
panel's 160 cells in 9 calls read as 0.0 ms where PERFORMANCE.md prices them
at ~153 ms. The cost model now charges a font call the same 756 µs floor as
any other `gfx_*` call **plus 900 µs a glyph cell**, which is
PERFORMANCE.md's own arithmetic (756 + 78 × 900 = 70.9 ms against the ~71 ms
it quotes for a 78-cell line). Every row below that draws a string moved.

| operation | measured, on the harness | gated on |
|---|---|---|
| one changed cell | **3.8 ms** | compose 1 cell; 1 blit call |
| one changed row | **12.0 ms** | compose `last − first + 1` cells; 1 blit call |
| a `k = 9` scroll | **257.8 ms** | 1 scroll + `k` drawn rows; **1 scroll per flush** — and all 25 rows composed and compared, which is §9.4's guarantee and not a miss |
| **a `k = 1` shift the signature got WRONG** | **256.4 ms** | 1 scroll, and the colliding row DRAWN: the glass matches the row's own sources (§9.4) |
| a `k = 3` scroll, `gfx_scroll` refusing | **301.4 ms** | spans, and the shadow stays true |
| **two pokes, rows 0 and 24** | **126.8 ms** | 13 composed rows, **2 blits** — the window ∩ the pages (§9.2); the window alone made this 25 rows and 299 ms |
| a full expose, 25 rows | **304.6 ms** | ≤ 25 composed rows + the border + the status row's 37 glyph cells |
| 25 rows changed, not a shift | **300.7 ms** | no scroll emitted |
| a `$D020`-only change | **3.0 ms** | fills only, **no band composed** |
| **a changed cell, `blit1` REFUSING** | **29.6 ms** | §9.5's font path, ONCE — the row is drawn with the kernel's face and not retried, and the fact is said (a `kern_small` kernel) |
| 8 × `$D011`, `$D016`, a `$DD00` serial edge | **0.0 ms** | **0 blits, 0 composes** — a register write that changes nothing costs nothing (§9.3) |
| a `$D016` change that draws the same picture | **254.6 ms** | 25 composed rows, **0 blits, 0 fills** — recompose, then ASK the shadow; and a frame register does not touch the border |
| an expose with the About panel up | **322.6 ms** | 12 composed rows: the 13 the panel covers are not drawn under it. It is MORE than a full expose because the panel itself is 198 glyph cells — which is why the row below exists |
| **an expose that misses the panel** | **16.7 ms** | the panel is redrawn only when the damage rect reaches it |
| the About panel closing | **139.0 ms** | 13 composed rows — the panel's rect, not the screen |
| a `k = 1` scroll on a CLAMPED window | **153.8 ms** | 1 scroll + 1 drawn row, on 15 rows of glass |
| one joystick indicator changed | **0.8 ms** | **one** `blit1`, **no fill** — the status row's delta (§10.1), reached the way the product reaches it |
| **entering fullscreen** | **304.6 ms** | one whole repaint, the kernel's own (§9.8) |
| **the wake after entering fullscreen** | **0.0 ms** | **0 blits, 0 composes** — `OSAPI_FULLSCREEN` repaints synchronously in both directions, so the shadow already describes the new glass |
| a full bitmap frame | wave 4 | the **measured ms**, not the call count |
| one sprite moved one cell | wave 4 | the spans it actually touched |
| a slice with no tick boundary | **0.0 ms** | **0** (§9.3) |

Three decisions fall out of that table and each is a constant in `c64scr.c`:
**a changed cell is 3.7 ms and a changed row 11.9 ms**, which is the whole
reason the composer takes a span and the write window exists (§9.2); **a full
repaint is ~300 ms, five host ticks**, so the `CPU_8086` tier flushes every
OTHER tick (§9.8); and **2× is 20.19 ms for eight rows**, 63 ms for a screen
on top of the compose, which is why no tier magnifies in wave 1 (§9.8).

A change that moves a row of this table up is a regression against a
documented number, and this table, `c64scr.c`'s constants and the harness
change together or not at all.

### 9.8 Fullscreen, and the tier table

`OSAPI_FULLSCREEN` (slot `0x0110`, SPEC.md §11.2's latch) on **Alt+D**, both
directions — VICE's own binding. **This is a stated exception to
SPEC.md §11.2.1**, taken the way SPEC.md §74.2 takes Alt+F for a terminal:
the C64 owns F and Esc, so neither can carry the latch here.

**AND THE CHORD IS IMPLEMENTED, BECAUSE IN FULLSCREEN IT IS THE ONLY DOOR.**
SPEC.md §11.2 is explicit that `wm_draw_win` draws **no chrome at all** for a
`WF_FULL` window — no menu bar — so Preferences > Fullscreen, the item that
got the user in, is not on the glass any more, and §11.2.1's rule (*"the key
that got you there is the key that leaves"*) is the whole reason the exception
had to name a key rather than only a caption. `os88_onkey` tests
`ascii == 0 && scan == 0x20` and calls the same resident
`c64_fullscreen_toggle` the menu item calls. The body is **resident** and not
in the overlay: a keystroke is the frequent side of SPEC.md §73.14's split,
and a chord that had to load `C64.OVL` to work would refuse on a disk without
it — with the refusal printed on a bar the user cannot see.

**WAVE 1 SHIPS THE 1:1 CENTRED ROW ON EVERY TIER AND DOES NOT MAGNIFY**, and
that is stated here rather than left to be discovered on the glass. The item
is LIVE and it works — the C64 screen is centred in the fullscreen content
box, the border fills the rest of it and the status row runs the full width —
it simply does not scale. The fact: `c64_band_x2` is **20.19 ms for eight
rows** on `tests/c64band` (§9.7), 63 ms for a screen *on top of* the compose,
and it needs a second shadow geometry; the scaler lands with the wave that can
pay for it. Nothing here is faked and nothing is silently missing.

Wave 1's review found the first draft doing something worse than either:
`c64_flush` clamped its drawable width to `C64_W_W` (336) and anchored the
screen at `org.x + C64_BORDER`, while `kernel/wm.inc:7295` records that
`wm_draw_win`'s SPEC.md §11.2 branch fills **the whole frame white** for a
`WF_FULL` window *and has no opt-out*. Alt+D on a 640×480 desktop therefore
left a 320×200 picture in the corner of a white 640×480 screen with a
336-pixel status strip under it, and nothing in the package read the `c64_full`
latch at all. The geometry is now computed in one place (`c64_geom`) and the
screen's left edge is snapped to a multiple of 8, because `OSAPI_GFX_SCROLL`
refuses a rect whose `x1` or `x2+1` is not (§9.4).

The scaling, when it lands, is a **tier table in one place** (`c64scr.c`),
written **from** `tests/c64band`'s measured milliseconds (§14.5) and from the
machine figures, never from a guess:

| adapter / tier | fullscreen | wave |
|---|---|---|
| every tier | **1:1 centred** | **1 — shipping** |
| CGA | 2×, **exactly 640×200** | later |
| VGA | 2×, 640×400 centred — the band composed 16 rows deep | later |
| Hercules | 2× horizontal, 640×200 centred, 1× vertical | later |
| the `CPU_8086` tier | 1:1 centred | **1 — shipping** |

The rest of the screen is a border fill. **A toast raised while fullscreen
goes to the status row as well** — the bar a toast lands on is *under* a
`WF_FULL` window (LESSONS 13), so every refusal in this port takes both
routes.

**AND THE LATCH IS NOT PAID FOR TWICE.** `OSAPI_FULLSCREEN` repaints the
window whole, synchronously, in *both* directions — `os88.h:604` says so and
`kernel/wm.inc:4147` shows it (`wm_fullscreen` calls `wm_raise` with `AL = 1`,
so `W_PAINT` runs nested inside the call and has already invalidated the
shadow and drawn all 25 rows for the new geometry). The success arm of
Preferences > Fullscreen therefore does **nothing**: an `c64_sh_inval()` there
threw that shadow away and the next wake drew the identical picture again —
25 bands, ~300 ms, four host ticks of pure double-draw, and invisible in an
emulator. `apps/runcpm/runcpm.c:1086-1095` records the same defect in its own
words. The harness rows `entering fullscreen` and `the wake after entering
fullscreen` (§9.7) are the gate.

---

## 10. The status bar

One 10-pixel row under the screen, **336 pixels wide = 42 cells**, delta-drawn
— 336 being the CONTENT width, which is why the window is authored 338
(§9.1).

### 10.1 What is on it

**42 cells is the whole design constraint, and wave 1 measured it.** VICE's
two speed strings are 12 cells each with their own field widths (§10.2), so
24 of the 42 are spoken for before anything else is on the row.

**THE ORDER IS VICE'S**, and wave 1's review found the first draft had it
reversed. `uistatusbar.c:2816-2826` appends the SPEED widget first — leftmost
— then the checkboxes, then tape + joysticks, then the drive units, with the
volume button at the far end. So, in pixels from the content origin:

| x | field | width | |
|---|---|---|---|
| 0 | `%7.0f%% cpu` | 12 cells | the SPEED widget, which `uistatusbar.c:2816` appends first |
| 96 | `%8.1f fps` | 12 cells | |
| 192 | — | 1 cell | **the one separator cell the row can afford** |
| 200 | `Joysticks:` | 10 cells | the JOYSTICK widget (`uistatusbar.c:1763`) |
| 280 | control port 1's `+` | 2 cells, **one** `blit1` | `draw_joyport_cb`, `uistatusbar.c:780` |
| 296 | control port 2's `+` | 2 cells, **one** `blit1` | |
| 312 | the drive number `8` | 1 cell | the three single-glyph state indicators, clustered at the right end |
| 320 | the warp lamp `W` | 1 cell, one `font_run` | §10.2 |
| 328 | the pause lamp `P` | 1 cell, one `font_run` | |

42 cells exactly — 41 of them fields, which is why there is exactly one
separator and it is spent after `fps`. The lamps were first placed at 192 and
the row read `0.0 fpsWPJoysticks:` on the glass, with the two lamps
indistinguishable from the text either side; VICE's LEDs are on a **different
row** from the speed widget (§10.2), so folding both rows onto one is this
port's decision and there is no VICE order to break by moving them. Volume is dropped (§10.3), and the CRT/Mixer checkboxes
`uistatusbar.c:2818` appends between the speed and the joysticks are dropped
too (§10.3 already said so).

**EACH INDICATOR IS VICE'S PLUS, NOT A ROW OF DOTS** (amended in the wave's
fix pass). `draw_joyport_cb` (`uistatusbar.c:780-860`) draws five squares
arranged in a `+` — up above, left and right beside, fire in the centre, down
below. The first draft laid the same five bits in a straight horizontal line
at a 3-pixel pitch and started port 2 sixteen pixels after port 1, so on the
glass the ten off-dots read as one continuous leader `..........` between
`Joysticks:` and the drive number, with nothing saying which dot was a
direction, which was fire, or where port 1 ended. The plus is three 2-pixel
columns inside the same 16 × 3 band — 6 pixels of shape — which also leaves
~10 pixels of real gap between the two ports. VICE's bit order is already this
port's (0 up, 1 down, 2 left, 3 right, 4 fire, §8), and the cost is unchanged:
one `blit1` a group.

**A DOT IS NOT A `gfx_fill`.** Ten dots drawn one fill each is 7.6 ms — a
drawing call costs 756 µs whatever it draws — and a whole status redraw was
more than two host ticks, for a row wave 2 wants to touch once a second. Each
five-dot group is composed into a band in the package's own RAM and goes down
in one call; an OFF dot is its centre pixel rather than nothing, because a
band carries no pen for SPEC.md §47's grey to survive in.

**And the row DELTA-DRAWS**, which is what wave 1 claimed and did not do
(`c64_st_joy1`/`c64_st_joy2` were declared for it and never read). A full
redraw — a fresh window, an expose, a message going up or coming down —
erases the row first, because those are the cases where its pixels are
unknown. Every other repaint draws only the field that changed, over its own
cells, with no erase: `font_run` and `blit1` both arrive in final polarity.

**`c64_status` IS CALLED FROM EVERY FLUSH AND ITS OWN COMPARE IS THE GATE.**
The review's second pass found the delta unreachable from the product: it was
called only under `if (c64_st_dirty)`, and every setter of that flag either
also cleared `c64_st_ok` (so the row redrew whole) or changed a field the
compare already sees. The `0.8 ms` row of §9.7 was produced by the harness
raising the flag by hand — a path the package could not take. The flag is
gone; the row is examined on every flush and answers *"nothing moved"* in
**zero drawing calls**, which is what the delta was built for.

**A MESSAGE OWNS THE WHOLE ROW while it is up.** The alternative, measured,
is a seven-cell message area — and the two messages this port has to show,
`ScrollLock for joystick` (§7.6) and `C64.ROM missing - see README.TXT`
(§1.4), are 23 and 32 characters. A message expires after about five seconds
and the widgets come back, so nothing is permanently hidden; §1.4's fact is a
permanent line rather than a message, for the same reason.

**AND TAKING IT DOWN IS WORK NOBODY ELSE ASKS FOR**, which wave 1's review
found on the glass: the deadline is examined *inside* the flush, and the flush
ran only while something was dirty. `c64_say` raised the flag, the flush drew
the message and cleared it, and with no core running — `C64_ST_HALT`, which is
every state wave 1 has — no further wake was posted, so five seconds never
arrived and `Warp mode on.` owned the row until the next keystroke. So
`os88_onwake` treats *a message being up* as a reason both to re-post the wake
and to flush; a flush with nothing dirty composes no row and `c64_status`
answers "nothing moved" in **zero drawing calls** (§9.7's `a wake with no tick
boundary`), so what it costs is the wake.

### 10.2 The speed widget — VICE's own strings, and what they count

`statusbarspeedwidget.c` prints `%7.0f%% cpu` (`:572`, `CPU_DECIMAL_PLACES`
0) and `%8.1f fps` (`:653`, `FPS_DECIMAL_PLACES` 1). **Both are folded onto
this one row**, LEFTMOST as VICE appends them (§10.1), e.g.
`   100% cpu    50.1 fps`.

**THE WARP AND PAUSE LEDs ARE TWO LABELLED LAMPS, `W` AND `P`, INVERTED WHEN
LIT.** In VICE they are not part of the speed widget at all: they are made at
`uistatusbar.c:2607-2616` and appended by `statusbar_append_led`
(`:2339-2362`) into `led_row_grid` — a **separate row** of the status bar at
column 0 — and each is `statusbar_led_widget_create("warp:", …)` /
`("pause:", …)`, so **each carries a text label beside its lamp**
(`statusbarledwidget.c:342-348`). Wave 1's first draft cited
`statusbarspeedwidget.c:406-415`, which is inside two `#if 0` blocks and is
not compiled, and drew two unlabelled three-pixel dots wedged between `fps`
and `Joysticks:` — two specks with nothing on the glass saying what they were.

`warp:` + lamp + `pause:` + lamp is 13 cells and the row has 42, of which the
two speed strings take 24, `Joysticks:` 10, the two joystick indicators 4 and
the drive number 1. **VICE's label text does not fit, so the lamp and its
label are folded into ONE GLYPH each**: the letter is always drawn — the label
never disappears — and its cell is **inverted, black on white, while the latch
is on**, which is the lamp. It costs the same two cells the unlabelled dots
cost, and it reads the same on a 1bpp adapter as on VGA, which a grey would
not (SPEC.md §39.4). VICE's `warp:`/`pause:` label TEXT is therefore in
§10.3's dropped list with the width as its fact.

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
(`uistatusbar.c` 1961/1971, 2594, 2670/2679). **And, measured in wave 1, so
are the `Tape:` field and drive 8's track counter ` 18.5`** — the drive
NUMBER stays, because that is the one of them a user looks for. The
fact is the width, and here is the arithmetic: 42 cells, minus 24 for the two
speed strings, minus 10 for `Joysticks:`, minus 6 for its two indicators,
minus 1 for the drive number, minus 2 for §10.2's warp and pause lamps, leaves
**nothing**. `Tape:` is 5 and ` 18.5` is 5.

**AND THE DRIVE NUMBER IS DRAWN PLAIN, NOT GREYED** (amended in the wave's fix
pass, on the evidence of `build/port-shots/w1r2-cga-status.png`, where that
cell is simply EMPTY). `os88_gfx_pen(1)` buys a checkerboard on a 1bpp
adapter, and the checkerboard is laid in `[gfx_disink]`, which
`kernel/vga12.inc:3414` sets to `CBLACK` for every content pen because a
window's content is white in both themes. **This row's paper is black**, so a
greyed glyph on it is a black stipple on black: gone on CGA and Hercules, and
nearly gone on VGA. `font_str` does exactly the same thing — the pen is not
the problem, the surface is. It is the same fact that made §10.2 refuse a grey
for the two lamps, and the same one behind an OFF dot being a pixel rather
than nothing: **this row has no vocabulary for grey.** Inverting the cell was
the other candidate and is worse — on this row an inverted cell means a LIT
LAMP, and a drive permanently lit says something false rather than nothing. So
the unit number is drawn, white, and never lights; that there is no drive is
carried where SPEC.md §47 wants it, on the greyed File > Attach/Detach items
(§11.2).

**VICE's `warp:` and `pause:` LED label TEXT is dropped for the same reason**
and by the same arithmetic — 13 cells for the labelled pair against the 2 the
row has — and what replaces it is a labelled lamp rather than a bare one
(§10.2), so nothing on the row is unexplained.

Dropped is stated here; everything else missing is greyed with its fact
(§11.2).

---

## 11. The menus

### 11.1 The set, and what is live

**Exactly five** — the kernel's `MENU_APPMAX` — with `AM_NAME` = **`VICE`**:
**File, Edit, Snapshot, Preferences, Help**. VICE's Debug menu is `#ifdef
DEBUG` in `uimachinemenu.c` and is **absent here by VICE's own rule**, not
greyed.

Every item string is `uimachinemenu.c`'s and every hotkey caption is a
`hotkeys*.vhk` line, transcribed (§2). Submenus are folded into their head
item.

**TWO KERNEL LIMITS SHAPE EVERY LINE OF `c64menu.c`, and wave 1 measured
both**: a pull-down is at most `MENU_POPMAX` = **11 items** and each item is
truncated to `MENU_MAXCH` = **24 glyphs** (`kernel/menu.inc:195`, `:207`).
They are facts about the machine, and they are measured on the SMALLEST
screen: `vid_popmax` is `(vid_h − 22) / 16`, which is 11 on a 200-line CGA and
clamped to 11 above it. Three rules follow:

1. **A section that is ENTIRELY unavailable folds into ONE item, and that item
   is the section's FIRST** — its submenu head label where it has one,
   otherwise the first item of the section, which is the action the section
   exists for. Never a word invented to summarise it, and **never the
   section's last item**: wave 1's review found the tape section folded onto
   `Datasette controls` (`:327`, its last) and the cartridge section onto
   `Cartridge freeze` (`:402`, its last), so a user looking for tape or
   cartridge support read a greyed *controls* or *freeze* rather than a greyed
   *attach*. A section with a LIVE item in it is **not folded** — folding it
   would take the working item away, which is why `reset_submenu` (`:244`,
   head label `Reset` at `:521`) contributes three of File's ten slots
   rather than one.
   **And the fold is per SECTION, taken to its end.** The review's second pass
   found two places where a section had been carved instead: File's disk
   section is ONE section between separators (`:286-:301` — Attach disk image,
   Create and attach..., Detach disk image, Flip list), so `Flip list` is not
   an item of its own; and Snapshot's event section is ONE section with no
   separator inside it (`:576-:598`, six items), so `Set recording milestone`
   and `Return to milestone` are not either. File is **10 items** and Snapshot
   **8**.
   **The one exception, and it is taken twice**: where the section's first
   item does not FIT `MENU_MAXCH`, the fold lands on the first item of the
   section that does, because `menu_trunc` would otherwise print a *shortened*
   label and rule 2 forbids that. `Attach datasette image...` (`:313`),
   `Attach cartridge image...` (`:393`) and `Detach cartridge image(s)`
   (`:398`) are all **25 glyphs against 24** — the first of them measured on
   the glass, printed `Attach datasette image..`. So the tape section folds
   onto `Detach datasette image` (`:319`, 22 glyphs, and it still names the
   medium a reader is looking for) and the cartridge section onto `Cartridge
   freeze` (`:402`, 16).
2. **The label is VICE's and is never shortened.** Where the label plus its
   `.vhk` caption passes 24 glyphs the CAPTION is dropped and the label stands
   alone; **the chord is then in §11.2's table**, which lists every dropped
   caption beside the fact that greys its item. (This rule used to point at
   §7.5, and §7.5 is a three-row table of the chords the XT/AT BIOS cannot
   DELIVER — a different question, and it contains none of them.)
   `Power cycle machine`, `Load`/`Save snapshot image...`,
   `Quickload`/`Quicksave snapshot`, `Advance frame` and `Datasette controls`
   are the items this costs a caption.
   **The one exception:** where dropping the caption would take away the only
   chord a LIVE item has, the SEPARATOR gives instead — one space rather than
   two. The label is still VICE's, entire. `Reset machine CPU Alt+F9` is 24
   glyphs that way and 25 with the two spaces every other captioned item uses.
   It is taken once.
   **AND A CAPTION IS ONLY TAKEN FROM AN ITEM THAT HAS ONE.** A VICE hotkey
   binds to an `.action`; a `UI_MENU_TYPE_SUBMENU` entry has a `.submenu` and
   no `.action`, so VICE prints no chord on it. Wave 1's review found three
   heads captioned with a chord belonging to an item *inside* their submenu —
   `Attach disk image` with `<Alt>8` (`drive-attach-8:0`,
   `hotkeys-drive.vhk:9`), `Flip list` with `<Alt>i` (`fliplist-add-8:0`,
   `hotkeys-fliplist.vhk:10`) and `Printer/plotter` with `<Alt>4`
   (`printer-formfeed-4`, `hotkeys.vhk:52`) — which told the reader that
   Alt+8 opens a list when in VICE it attaches to drive 8. All three heads
   carry the label alone.
3. **A present-but-impossible item wears `OS88_MENU_DIS` and the fact that
   greys it is in a comment beside the string** (§11.2, SPEC.md §47) — **and
   nothing is LIVE that only toasts a refusal.** An item a user can pick and
   that answers *"not yet"* is the one thing §47 exists to stop, and wave 1
   shipped three of them: Edit > Copy, Edit > Paste and
   Preferences > Advance frame. All three are greyed with their fact.
4. **A CHECK item's state is a `*` in the label**, and the item pointer is
   swapped between the two spellings (`c64_menu_state`). **Eight** of these
   items are `UI_MENU_TYPE_ITEM_CHECK` in `uimachinemenu.c` — `:682`
   Fullscreen, `:692` Show menu/status in fullscreen, `:704` Warp mode,
   `:708` Pause emulation, `:732` Show status bar
   (`settings_menu_statusbar_primary`), `:757` Mouse grab, `:771` Swap
   joysticks, `:786` Allow keyset joysticks; wave 1 counted six and left out
   `:692` and the statusbar item, and `Show status bar` was greyed with no
   state while the item below it wore one. It is the identical case as
   `Allow keyset joysticks`: a CHECK that is ON and cannot be turned off, so
   it wears rule 4's marker ON **and** `MENU_DIS` together. In VICE the
   checkbox **is** how the state is read; this kernel's menu has no check mark and its face has no
   glyph for one (LESSONS 8). The `*` is `apps/tracker`'s idiom
   (`tracker.asm:2135-2150`) and **not** `apps/solitaire`'s MENU_DIS twin, for
   the reason tracker states there: `MENU_DIS` is §47's *"you cannot have
   this"*, so greying the item that is ON would report the feature as
   unavailable and make it impossible to turn off. The marker is two glyphs
   so both spellings are the same width; the longest,
   `* Pause emulation  Alt+P`, is exactly 24. The kernel reads the item
   strings through the set's `items` pointer at draw time
   (`kernel/menu.inc:1977`), so swapping a pointer is enough and
   `os88_menu_set` is not called again.

**AND THE ORDER INSIDE A MENU IS `ui_machine_menu_bar_create`'s** (amended in
the wave's fix pass). `uimachinemenu.c:1153-1160` adds `settings_menu_head`,
then `settings_menu_statusbar_primary`, then `settings_menu_speed` — so
**`Show status bar` is the item immediately after the head section, BEFORE
`Warp mode`**, not after `Emulation speed` where the first draft put it. The
Preferences menu is therefore, in order: `Fullscreen`, `Restore display
state`, `Show status bar`, `Warp mode`, `Pause emulation`, `Advance frame`,
`Emulation speed`, `Mouse grab`, `Swap joysticks`, `Allow keyset joysticks`,
`Settings...` — 11 items, which is `MENU_POPMAX` exactly.

**That 11 is also why `:692` (`Show menu/status in fullscreen`) is folded onto
`Restore display state` rather than carried**, even though rule 1's fold is
for a section that is entirely unavailable and this section has a live item in
it. Both are greyed and both are display-state items the kernel owns; carrying
`:692` separately would make the menu 12 items against a hard 11, and rule 2
forbids shortening something else to make room. It is stated here because it
is a departure from rule 1 taken for a measured reason, not an oversight.

Live items:

| item | caption | note |
|---|---|---|
| File > Smart attach... | Alt+A | the Standard File dialog on `.PRG` (§11.3) |
| File > Reset > Reset machine CPU | Alt+F9 | |
| File > Reset > Power cycle machine | Alt+F12 | caption kept; the item is the route (§7.5). **RAM pattern fill: VICE's C64 factory pattern** (`src/ram.c:169-177` — `RAMInitStartValue` 0, `RAMInitValueInvert` 4, `RAMInitValueOffset` 2, `RAMInitPatternInvert` 16384, `RAMInitPatternInvertValue` 255, put through `ram_init_with_pattern` at `:257`), which is the eight-byte period `00 00 FF FF FF FF 00 00` with every other 16K block inverted — **not zeros**, which is what wave 1 first wrote here and at power-on. `Reset machine CPU` still does not touch RAM, which is the whole difference between the two items |
| File > Exit emulator | Alt+Q | the worker self-close idiom |
| Edit > Copy | Alt+Delete | **wave 3.** Caption kept, item guaranteed: the 40×25 screen, PETSCII → ASCII, to the clipboard. Greyed until then (rule 3) |
| Edit > Paste | Alt+Insert | **wave 3.** Caption kept, item guaranteed: the clipboard typed through `$0277`, ten characters a jiffy. Greyed until then |
| Preferences > Fullscreen | Alt+D | §9.8. CHECK: rule 4's `*` |
| Preferences > Warp mode | Alt+W | flush every 9 ticks instead of every tick. CHECK: rule 4's `*`, and §10.2's `W` lamp |
| Preferences > Pause emulation | Alt+P | CHECK: rule 4's `*`, and §10.2's `P` lamp |
| Preferences > Advance frame | Alt+Shift+P | **wave 2.** Run to the next VIC frame end (§6.3), then stop — there is no raster accumulator until the alarm model lands. Greyed until then |
| Preferences > Swap joysticks | Alt+J | §8. CHECK: rule 4's `*`, and the two status indicators swap with it |
| Help > About VICE... | | `uimachinemenu.c:988`; the kernel's name pull-down About opens the same panel (§12) |

### 11.2 Present and greyed — the fact that greys it (SPEC.md §47)

| item | the fact |
|---|---|
| File > Attach disk image (ONE item: the whole disk section — Create and attach..., Detach disk image and Flip list — folds into it, §11.1 rule 1). **No caption**: it is a `UI_MENU_TYPE_SUBMENU` with no `.action`, so VICE prints none; Alt+8/9/0/1 and Alt+I/K/N belong to items *inside* those submenus (`drive-attach-8:0`, `fliplist-add-8:0`, …) | no 1541 in this build: a D64 needs the drive's directory walk and the KERNAL serial traps, and this port loads `.PRG` files only (Smart attach) |
| File > Attach/Create/Detach datasette image, Datasette controls (Alt+T captions kept) | no tape emulation in this build: T64/TAP are not read |
| File > Attach cartridge image..., Detach cartridge image(s), Cartridge freeze (Alt+C / Alt+Z captions kept) | no cartridge port in this build: the bank maps carry the cartridge-less 7 of VICE's 32 (`exrom = game = 1`, §3.3) |
| File > Printer/plotter (ONE item, a `UI_MENU_TYPE_SUBMENU` with no `.action` and therefore **no caption**; Alt+3/4/5/6 are the `printer-formfeed-*` actions inside it, `hotkeys.vhk:52`, `:56`) | no printer path in this OS |
| File > Activate monitor (Alt+H) | no monitor in this build: VICE's monitor is 30,000 lines of host C |
| File > Reset drive #8..#11 | no drives in this build |
| Snapshot > every item (load/save Alt+L/S, quick snapshots Alt+F10/F11, the six-item event section folded onto `Start recording events` including the milestones Alt+E/U, media recording Alt+Shift+R/S, quicksave screenshot on Pause) — the chords are here because rule 2 drops the captions that do not fit 24 glyphs | no snapshot format in this build: a VSF carries every chip's state and this machine's chips are not VICE's |
| Preferences > Restore display state (Alt+R), Fullscreen decorations (Alt+B), Show menu/status in fullscreen | the window is os8088's: the kernel places it, and the bar is under a fullscreen window (SPEC.md §11.2) |
| Preferences > Emulation speed (200%..10%, Custom CPU speed), 50/60/Custom FPS | nothing throttles here: the machine delivers what the 8088 can and the status bar prints it (§10.2; SPEC.md §74.4's `F_CPUSPEED` reasoning) |
| Preferences > Show status bar | the status row is the window's bottom row and is always drawn. A CHECK that is ON and cannot be turned off: it wears rule 4's `*` marker **and** `MENU_DIS` together (§11.1 rule 4) |
| Preferences > Mouse grab (Alt+M) | no 1351 mouse in this build: the pointer is the desktop's |
| Preferences > Allow keyset joysticks (Alt+Shift+J) | the keyset **is** the joystick here (the only joystick source this machine has); it wears rule 4's `*` marker ON **and** `MENU_DIS` together — it is on, and it cannot be turned off (§8) |
| Edit > Copy (Alt+Delete), Edit > Paste (Alt+Insert) | the screen-code → PETSCII → ASCII tables and the `$0277` keyboard-buffer feeder arrive with the machine that HAS a keyboard buffer (wave 3) |
| Preferences > Advance frame (Alt+Shift+P) | there is no raster accumulator until the alarm model lands with the core (wave 2) |
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

**`LOAD"*",8` answers with the `?DEVICE NOT PRESENT` error.** That is the
honest machine with no drive, and it is written here so nobody files it as a
defect. **The screen LINE is deliberately not quoted** — the KERNAL prints its
error text followed by ` ERROR`, and a `SEARCHING FOR *` line before it, so a
shortened quote here and in `README.TXT` would invite the bug report it exists
to prevent. There is no string to transcribe in the VICE tree: the ROM is the
authority, and wave 2 is the first wave that can run it. Transcribe what the
machine actually prints then, into both places.

### 11.4 Sound

**SID voice 1's frequency and gate** go to `os88_snd_tone`
(`OSAPI_SND_TONE`, slot `0x00E8`) once per slice, and only when they changed
— one far call. Voices 2 and 3, every waveform beyond the gate, ADSR and the
filters are greyed with §11.2's fact: `OSAPI_SND_FM` and the streaming path
are driver verb protocols deliberately not wrapped for C.

---

## 12. The About panel

`ovl_about_show` in `c64about.c` — **eight rows**, modal, the machine
**paused while it is up**, its close drawn as damage and not as a repaint
(the `rcabout.c` shape; the close and hit test stay resident in `c64.c`).
Reached from **Help > About VICE...** and from the kernel's own About item
alike (`about_set`).

The rows, from `uiabout.c`, `configure.ac` and `README` 186–290:

```
About VICE
The Commodore 64 Emulator
VICE 3.10
os8088 port: PAL
Copyright 1996-2025, VICE team
GPL-2 or later - see COPYING
ROMs Copyright Commodore
Business Machines
                                    [ OK ]
```

**THE FOURTH ROW SAYS WHAT THE PORT IS AND NOTHING ABOUT HOW IT RENDERS.**
PAL is a fact about the emulated MACHINE (§5.2's 63 × 312 = 19,656-cycle
frame). Wave 1 shipped `os8088 port: PAL, 1bpp, no drive`, and LESSONS 8
forbids both additions verbatim: `1bpp` is how the build draws and `no drive`
is a thing it cannot do, and both belong in this document and in the greyed
items that name them — where they already are, at §9.6 and at
File > Attach disk image / Reset drive #8 (§11.2).

**The panel is 336 wide — the C64 screen and its border — SNAPPED TO THE CELL
GRID, and that is a redraw decision.** At 280 px in a 336-px box it left a
28-px strip of C64 screen down each side, so an expose while the panel was up
had to compose and blit every row the panel covered — all forty cells of each
— and then paint the panel over the middle of what it had just drawn. 336
makes *"the rows the panel covers"* exact horizontally, so `os88_paint` skips
them entirely (`c64_hold_r0`/`c64_hold_r1`) and nothing under the panel is
drawn at all: **322.0 ms for a whole expose with the panel up, 12 composed
rows of 25** (and `os88_paint` checks the horizontal cover rather than
assuming it, because a window narrower than 336 clamps the panel).

**The GRID SNAP is the vertical half of the same statement**, and the review's
second pass found it missing. `c64_hold_r0`/`r1` are `(y − screen_y) / 8` and
C truncates toward zero, so an unsnapped panel left the partly covered cell
row at each end *held but uncovered*: with the shipped geometry, six pixel
rows above the panel and four below it were inside a held row and outside the
panel, and `WF_OWNBG` means nobody whitens them — two full-width strips of
whatever damaged them, until the panel closed. `ovl_about_show` now rounds the
panel's height up to a whole cell and its origin down onto the grid.

**AND IT IS REDRAWN ONLY WHEN THE DAMAGE REACHES IT.** The panel is 1 fill +
2 frames + 9 `font_str` over 160 glyph cells — **~153 ms**, which the cost
model used to charge as zero (§9.7) — so redrawing it on every paint made an
expose with the panel up cost *more* than the full expose the hold rows exist
to beat, and a menu closing over one corner of the window paid all of it. An
expose that misses the panel is **16.5 ms** (§9.7).

**`ovl_about_show` ANSWERS A STATUS AND THE LATCH IS THAT ANSWER.** 0 means a
refused overlay load — no `C64.OVL` on the disk, a stale module, no heap —
which `c64cmd.c`'s own rule calls a normal path. Latching `c64_abt = 1` over
it left `os88_onwake` returning early with no panel on the glass, `os88_paint`
holding rows for a panel that does not exist, and the click that clears it
running `c64_blank_rect` over a rect never assigned: the machine looked
frozen and the toast that said why had expired.

**Its close is `c64_blank_rect` over the panel's own rect**, not
`c64_sh_inval`. Wave 1's comment said "damage, not a repaint" on the line
above a call that forced all 25 rows, the border and the status row — ~271 ms
for a thirteen-row panel. Measured after: **138.3 ms, 13 composed rows.**

It must fit CGA's ~136-row framed content box with OK inside the panel and
the panel inside the content box — the constraint RUNCPM's panel is measured
against in SPEC.md §74.4 — and both the panel's width and its height are
clamped to the live content box, with the OK button clamped inside the panel.

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
that can move (LESSONS 5). Every file in the table above exists from wave 1,
`c64cmd.c`, `c64load.c` and `c64about.c` included, and every one of them is a
written prerequisite in the Makefile: make cannot see through a `#include` or
a `%include`, and a file a later wave adds is a build the tree does not know
about (LESSONS 9).

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
  looking (§9.8). That call is `ovl_probe()`, whose body is `return 1`: the
  point of it is the far call the RUNTIME makes on the way in, which is what
  loads the module — asked once, for nothing, at the first moment there is an
  instance to resolve it against, rather than discovered when a user picks a
  menu item. Wave 1 did not have it, and every overlay wrapper's 0 returned
  silently: `os88_oncmd` and `os88_about` now say the same sentence, because
  every body in `c64cmd.c` returns 1 and a 0 therefore never came from one of
  them.
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
naming the licence and carrying the ROM copyright line (§1.2, §1.3).

**AND `COPYING` TRAVELS WITH THE BINARY.** The floppy is the distributed form
of a GPL-2-or-later program, `apps/runcpm`'s disks ship their upstream licence
beside the CCP for the same reason, and `README.TXT` on this disk says the
full licence text accompanies every release — which was not true of the disk
that said it. `apps/c64/COPYING` is now a prerequisite of the image and a file
on it, and `README.TXT` points at it *on the disk* as well as in the source
tree.

**Which geometries carry it, with the arithmetic:**

| geometry | clusters | `C64.O88` + `C64.OVL` + `C64.ROM` + `README.TXT` | `COPYING` (17,989 B) | carries it |
|---|---|---|---|---|
| 1.44MB | 2,847 × 512 B | ~78 | 36 | **yes** |
| 720KB | 713 × 1KB | ~40 | 18 | **yes** |
| 360KB | 354 × 1KB | ~40 | 18 | **yes** — 58 of 354, and the disk has no other software on it |

All three carry it; the 360KB disk has room because it carries nothing but
this package. If a later wave puts anything else on the 360KB image and
`COPYING` no longer fits, the rule is **the licence stays and the other thing
goes**, and `README.TXT` on that geometry says where the text is. Nothing
ships a GPL binary with the licence dropped.

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
| `apps/c64/hosttest/c64uitest.c` | the whole program over a stub `os88.h` with a **PIXEL** model of the glass — `gfx_blit1` writes real pixels, and `gfx_scroll` moves them and fills the vacated rows with GARBAGE, which is what catches a flush that trusts a stale shadow for a row the scroll vacated. After every step it asserts, pixel for pixel over the whole 320×200 screen, that **the glass shows what the shadow says it shows**; it prints §9.7's cost table in milliseconds and the dirty-pages-per-wake counter, and it dumps the machine and the composed frame for `tools/c64ref.py`. Wave 2 adds the scripted core, the level keyboard and the alarm path behind it. Run by `build.sh` before every build. **The assembly half cannot run on the host**, so the routines it substitutes are transcriptions — which makes `c64ref.py` a check on the ALGORITHM and not on the 8086 encoding; the encoding is gated by `c64memtest.sh`, by `tests/c64band`'s identity rows and by the QEMU screendumps, all of which run the shipping text. It also **enforces the clip** — a pixel written outside an armed region while no clip is armed is a failure with its coordinates, which is what makes SPEC.md §11.3 checkable for the callbacks that are not `W_PAINT` — and it models `os88_task_alive` as a call that never returns, which is what makes File > Exit emulator's teardown checkable at all. **`--no-rom` is a second process**: `os88_main` decides the refusal surface once per launch, so the screen a user of a mis-copied disk actually sees needs its own run |
| **`tools/c64ref.py`** | **an independent, pixel-level reference compositor.** Python, written from VIC-II documentation and VICE's `src/vicii/` as the authority, **not** from `c64band.inc`: it renders the same C64 memory to a 320×200 1bpp image, and the harness compares it **bit for bit** against what the package composed. This is what validates hires bitmap, multicolour, a custom character set, the cell transpose and sprite priority/expansion — a cell-identity glass model provably cannot. **`--lumcheck` is the other half**: the package's 16-byte luminance table against `vicii_colors_6569r5`'s own Y column, held here as parts per thousand straight off `vicii-color.c:441`, over all 256 ORDERED PAIRS — which is the only way to ask about §9.6's seven equal-luminance pairs in both directions, since a rendered frame carries one background at a time. The oracle derived its luminance from `vice.vpl` until this wave's fix pass, i.e. it kept the defect the package had already had removed |
| `apps/c64/hosttest/c64cputest.asm` + `.sh` | §4.6's nine rows with their negative controls — `make c64cputest`, minutes, not in `build.sh` |
| `apps/c64/hosttest/c64memtest.asm` + `.sh` | §3.6 — `c64mem.inc` **and** `c64band.inc`'s cross-segment entry points under `SS ≠ DS` with an `ES` sentinel: the movers, `c64_rowspan`/`c64_rowcopy`, and **`c64_band1` composing out of both claims and `c64_rowsig` signing out of one**, which nothing called until this wave's fix pass. **Four negative controls, one per thing the discipline check claims to check** — ES, DF, BP and DS. The BP and DS ones are new because both checks were inoperative: BP was recorded AFTER the call and compared with itself, and the checker did its own bookkeeping through whatever DS the routine under test had left behind. Run by `build.sh` |
| `tests/c64band` | `make c64bandbench` — the icount bench pricing `c64_band1` (text, bitmap, multicolour) **per cell and per call**, `c64_band_x2` at 8 and 16 rows, `c64_rowspan` and `c64_rowshift`. **§9.7's milliseconds and §9.8's tier table are written from these numbers**, and they become a new Set in PERFORMANCE.md. It **arms the clip** on its rerun callbacks (they are `W_ONKEY`/`W_ONCLICK`, not `W_PAINT`), which is both correct and 28% of the `FONT_RUN` bar; it **saves ES** around every blit, because a callback returns `ES = KERNEL_SEG`; and it **preflights `OSAPI_GFX_BLIT1`**, so a kernel that refuses bands prints `REFUSED (CF=1)` on the rows that contain one instead of timing a call that draws nothing |
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
