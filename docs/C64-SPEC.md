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
costs nothing until a key or a tick arrives. **And that sentence is now
literally true**, which it was not when it was written: the port carried
RUNCPM's one worker — the self-close behind File > Exit emulator — until
wave 3's fix pass found that the idiom closes the WINDOW and not the APP and
replaced it with `os88_wm_close` (§15.2). `CC_HAS_WORKER` is gone and the
package holds no task slot at all.

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

### 1.4 The ROM — a PART of the package

`tools/c64rom.py` concatenates the three into `build/c64-rom/C64.ROM` with a
**fixed layout** and checks each input's SHA-256 against §1.3's table before
it writes:

| offset | length | contents |
|---|---|---|
| `0x0000` | 8,192 | KERNAL (`kernal-901227-03.bin`) |
| `0x2000` | 8,192 | BASIC (`basic-901226-01.bin`) |
| `0x4000` | 4,096 | CHARGEN (`chargen-901225-01.bin`) |
| | **20,480** | total — 40 sectors, exactly 20KB |

**IT IS PART 0 OF `C64.O88`** (SPEC.md §20.12). `tools/c64rom.py` still builds
the same 20,480 bytes from the same committed inputs — `make` produces them on
any checkout with no network and no VICE tree — and what changed is that
`os88pkg.py` appends the result to the package instead of `os88disk.py` putting
it on the floppy beside it. The shim declares one `OS88_PART OP_ASSET` and
`apps/cc/crt0.asm` calls `op_load` before any C runs, so by the time
`os88_main` executes the 20KB is claimed and the ROM is in it — or the launch
was refused, with a toast, **before a sector was read**, because the part table
`op_load` sizes from is already inside the image the kernel had to read anyway.

**It was a SIDECAR and that is the whole of what changed.** `C64.ROM` sat
beside `C64.O88` in one folder, and a file copy could separate the program from
the ROM it is useless without. Everything below used to exist to say so when it
happened, and every line of it is deleted rather than disabled — a greying may
not outlive its reason (SPEC.md §47):

- the halted-machine state (`c64_norom`) and the four-line notice on the glass
  naming the file, with its own once-only gate and its own expose repair;
- the permanent status row `C64.ROM missing - see README.TXT` (§10.1's longest
  message, which is why that gate's cap was what it was);
- Preferences > Advance frame, Edit > Copy and Edit > Paste greying on it —
  each keeps its OTHER reasons, the jam and the pause, which are real;
- and `build/c64uitest --no-rom`, a whole second host-test process that existed
  because `os88_main` decides that surface once per launch.

**The number that refused this on paper was the wrong number.** This section
used to record that an embedded build "was over 64,000 bytes against SPEC.md
§73's 61,440 cap, refused before a line was written". `APP_MAX_SIZE` bounds the
primary SEGMENT's image plus bss, not the FILE: a part lives past the image and
the segment never sees it. Measured after the conversion — image **40,854**,
bss **13,176**, sum **54,030** against the 61,440 cap, in a file of **61,440**
bytes. The cap was never the obstacle; nothing existed to put the bytes
anywhere else.

A claim that cannot be had is still a refused launch (§3.1), and it is
`op_load`'s refusal now rather than the package's: it names the figure in a
toast, and the kernel's own `Load failed` replaces it because §21 step 10
toasts for every outcome (SPEC.md §20.12.4). That is a real loss of wording
against the sidecar's on-glass notice, and it is the trade: a message about a
file that cannot go missing is worth less than the file not being able to go
missing.

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
| the JAM line on the status row, `Main CPU: JAM at $E5CF` — VICE's own format string with its three leading and trailing padding spaces (the `D'OH!` dialog's, not this row's) dropped; 22 glyphs | `src/maincpu.c:612` (`"   " CPU_STR ": JAM at $%04X   "`) + `src/6510core.c:45` (`CPU_STR` = `Main CPU`); the dialog it is shown in is `src/arch/gtk3/widgets/jamdialog.c:72` |
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
| ROM | `op_load`'s carve — 20,480 bytes | the ROM PART, §1.4's layout, claimed and read by the parts standard before `os88_main` runs; `os88_part_seg(0)` is its base |

Launch is **defined by the claims succeeding**, not by a free-KB figure: the
64KB claim must succeed and `op_load` must have had its 20KB and read the part
into it, and the refusal sentence quotes what was asked and
`os88_mem_largest_kb()`. Two more
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
- **Read-back of `$01`, and it is ONE formula.** VICE's is
  `data_read = (data | ~dir) & (data_out | pullup)` (`c64pla.c:55`) with the
  C64's `pullup` = `$17` (`c64mem.c:222`), which on a machine with no
  datasette reduces exactly to **`(data & dir) | (~dir & $17)`**: an OUTPUT
  bit reads what was written to it, and an INPUT bit reads the pull-up where
  the board has one and **0** where it has none. So bits 0–2 (the bank lines,
  pulled up) read 1 as inputs; **bit 3** (cassette write) has no pull-up and
  reads **0**; bit 4 (cassette sense) is pulled up and stays 1 because
  `tape_sense` is 0 with no datasette (`c64pla.c:66`); **bit 5** (the motor)
  is cleared for an input unconditionally (`c64pla.c:61`) and reads **0**;
  bits 6–7 are unconnected and their capacitor has discharged
  (`c64mem.c:325-335`), so they read 0 as inputs. **Wave 2's fix pass found
  the code using `~ddr & $1F`**, which pulled bits 3 and 5 HIGH as inputs —
  two bits of the register the KERNAL reads to find the datasette, and two
  bits this section already described correctly. Stated, because a program
  that reads `$01` to discover the bank sees this table and not a real 6510's
  decay behaviour.

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

| offset | what | § |
|---|---|---|
| `$00` (32 bytes) | the page dirty bitmap | §9.2 |
| `$20` | `BLO` — the low edge of the region `ES` is biased for | §4.3 |
| `$22` | `CARRY` — cycles `c64_cut()` took out of the countdown | §4.4 |
| `$24` | `DEAD` — **the countdown**, the one hot counter | §4.2 |
| `$26` | `BOUND` — the high edge of that region | §4.3 |
| `$28` | the cached fetch `ES` | §4.3 |
| `$2A` | the IRQ level and the NMI edge | §4.4 |
| `$2C`, `$2E` | the write window | §9.2 |
| `$30` | "the core wrote something" | §9.2 |
| `$32`, `$34` | the watch range | §9.2 |
| `$36`–`$38` | the three bank-map bytes the core reads above `$A000` | §3.3 |

**Wave 2 amended two rows of that table.** The *hot* counter is the
COUNTDOWN and nothing else — one `sub` and one `sbb` per instruction — and
the 32-bit total of emulated cycles the status bar divides (§10.2) is a
two-word counter in the package's own C, folded **once per `c64_run` call**
and therefore not hot at all. Putting it in the scratch would have cost two
near calls per figure to read it back for no gain. And the three bank-map
bytes moved IN, from the package's bss: the core reads one of them on every
access above `$A000`, DS-relative with no segment override, which is the
whole reason the scratch exists.

That address is chosen because the KERNAL's vectors sit there **in ROM** in
every map that runs KERNAL code, and nothing in the KERNAL or BASIC uses the
RAM under them. Two stated deviations follow, and the CPU harness (§4.6)
holds a case for each so the boundary is provably where this document says it
is:

- **A read of `$FFC0-$FFF9` in an all-RAM map reads the scratch**, not the
  RAM the emulated machine wrote.
- **A write to `$FFC0-$FFF9` is dropped.** The write path already tests the
  high byte; only the `$E0-$FF` branch pays the extra compare.

A third consequence surfaced in wave 2 and belongs here: **anything that
loads a whole 64KB image into the claim lands ON TOP of the scratch** and has
to clear it afterwards. `os88_main` does (`c64_scratch_clear`), and so does
`hosttest/c64cputest.asm` after it reads Dormann's 64KB fixture in — the first
run without it took an NMI nobody raised, because the fixture's own bytes at
`$FFCA` became the pending-interrupt flags.

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
| Y | `DL` | and **`DH` is the memory-data byte and a free scratch**: Y is indexed with `add bl,dl / adc bh,0`, so nothing ever needs `DX` as a word, and a read that had to return through a saved register would have needed one more push per access |
| PC | `SI` | |
| S | `DI` | held as the FULL stack address `$0100 + S`, so a push is `mov [di],v / dec di / or di,0x0100` and the page wrap is one instruction rather than a mask and an add |
| dispatch scratch / effective address | `BX` | |
| the cdecl frame, and 12 bytes of scratch below it | `BP` | `[bp+disp]` addresses **SS**, deliberately (CLAUDE.md's SS ≠ DS rule): the decimal ADC/SBC of §4.2 needs more temporaries than this plan has registers, and a push inside a handler would put them where a fetch cannot reach them |
| RAM | `DS` | the 64KB claim |
| the fetch segment | `ES` | §4.3 |

The dispatch is a 256-entry table: `xor bh,bh / mov bl,[es:si] / inc si /
shl bx,1 / jmp [cs:bx+tab]`.

The countdown and the boundary words are **not** registers and **not** bss:
they are in the emulated machine's own scratch (§3.5), and because `DS` is
the C64's RAM for the whole of `_c64_run` each of them is a bare
`[disp16]` with no segment override.

**`P` is a real 6502 `P` byte in `c64_m`, in both directions.** The core
unpacks it into `AH`/`CH` on entry and packs it back on exit — eight
instructions each way, ONCE PER CALL — so the C and the harness read the
register the machine has rather than this file's layout. PHP, PLP, BRK, RTI
and interrupt entry go through the same two helpers.

**The stack wrap is `and di,0x00FF / or di,0x0100` and not `and di,0x01FF`.**
The one-line form is wrong and silently so: `0x0200 & 0x01FF` is `0x0000`,
not `0x0100`, so a pull with `S = $FF` read byte zero of the address space
and left every later push writing at `$0000` downwards. BASIC boots straight
through that defect — the KERNAL never wraps the stack — and Klaus Dormann's
"proper stack wrap around" test at `$0D89` is what caught it (§4.6).

### 4.2 Time is 6510 CYCLES

**The clock is the emulated 6510's cycle count and nothing else.** Every
opcode carries its real cost from `6510core.c`'s tables, **including the
page-cross penalty on the indexed addressing modes and the taken-branch and
branch-page-cross penalties**, and the core decrements **one cycle counter**
(§3.5) by it.

That counter is **both** the wall-clock slice budget (§4.4) and the device
clock (§6.3). It is the only time in this machine.

**It is a SIGNED word, and that sets the cap on every budget in this port.**
The check between instructions is `cmp word [DEAD],0 / jle` — two
instructions, and no per-instruction test of anything else — so a budget
above 32,767 arrives negative and the core expires before its first fetch.
`C64_SLICE_MAX` is 16,384 for that reason and the CPU harness runs Dormann in
30,000-cycle passes for the same one.

The countdown is checked **between** instructions and never inside one, so
`c64_run` may overrun what it was asked for by at most one instruction's
cost. What was NOT spent is left in `c64_m.cnt` — negative by up to seven —
and the caller's `ran = asked - cnt` is therefore exact rather than
approximate. Nothing is lost or double-counted at a slice boundary.

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

- The core keeps the **region `ES` is currently biased for** in its scratch
  (§3.5) — a LOW edge and a HIGH edge, `BLO` and `BOUND`. The boundaries are
  `$A000`, `$C000`, `$D000`, `$E000` and the last byte of the address space.
- **Two `cmp`s per fetch**, and the second one is the amendment wave 2 made
  to this paragraph. A ceiling alone is only correct while PC increases: a
  `JMP` back from `$E000` to `$0400` leaves PC BELOW the biased region, not
  above it, and a one-compare guard would have fetched the KERNAL's bias over
  RAM with no warning. The alternative — re-biasing on every control transfer
  — costs twenty instructions on the most frequent path in the machine, where
  a range check costs two. When PC leaves the region, the map is consulted
  and `ES`, `BLO` and `BOUND` are recomputed. Operand fetches use the same
  guarded fetch.
- **A fetch from `$0000`/`$0001`, and from `$D000-$DFFF` with I/O mapped,
  takes the slow path** and leaves the region EMPTY, so every fetch there
  re-enters the same routine. That is what a fetch out of a register file
  costs and what it is meant to cost.
- **Every write to `$00` or `$01`** recomputes both (§3.2).
- **Every cdecl call out of the core saves and reloads the cached `ES`**
  (§3.4) — the C ABI clobbers it.

There is no "stale bias" deviation left in this design. **Every map
transition has a case in the CPU harness** (§4.6).

### 4.4 The alarm model, and the wall slice

VICE's own structure (`src/alarm.c`, `maincpu.c`): **run to the next device
event, service it, compute the next one.** There is no fixed quantum
anywhere in this machine.

**The alarm.** Before each run, `c64_alarm_next()` computes *cycles to the
next event* as the minimum of: CIA1 timer A underflow, CIA1 timer B
underflow, CIA2 timer A, CIA2 timer B, the VIC raster compare and the end of
the frame. The core runs exactly that far and RETURNS; `c64_advance(ran)`
then moves every device phase by what actually ran, and the loop computes the
next deadline. `os88_onwake` is that loop (§13.1).

**Two amendments wave 2 made to this paragraph, and the reason for each.**

- *The service is a return, not a call out of the core.* The core keeps the
  whole 6510 in registers; a cdecl call from inside it has to save and reload
  every one of them, which is the same work as the entry/exit shell — so a
  call out would cost what a return costs and be harder to read. The
  structure, the phase retention and the absence of a quantum are unchanged;
  only who is on top of the stack is.
- *The end of a raster LINE is not an alarm.* `$D012` is computed from the
  cycle counter when it is READ, which is exact and costs nothing; a per-line
  alarm would have ended the run 312 times a frame for a register most
  programs never touch. The raster COMPARE and the frame end are alarms, so a
  raster interrupt still fires on the line it was armed for and in the right
  order against every CIA interrupt.

**Interrupts are taken between instructions, and the cost of that is zero
per instruction.** The core checks the pending flags at the top of `_c64_run`
and at the three instructions that can UNMASK a line — `CLI`, `PLP` and
`RTI`. Everything else that can raise one is an alarm, and an alarm is what
ended the run, so an alarm-raised IRQ is taken with no latency at all. The
one exception is an I/O write that unmasks a flag already set (`STA $DC0D`),
which happens inside a run: `c64_cut()` ends that run at once and moves the
unspent countdown into `CARRY`, so ending early costs nothing in the cycle
accounting. **AND THE I FLAG'S ONE-INSTRUCTION TIMING IS CARRIED, in both
directions** — this paragraph used to state the opposite as a deviation, and
the fix pass retired it. VICE records the transition in the last opcode's info
and the interrupt check reads it (`6510core.c:1052` CLI, `:1493` PLP, `:1760`
SEI, `:445`, `mainc64cpu.c:702`): an IRQ *unmasked* by CLI or PLP is taken
only after ONE more instruction, and an IRQ that was visible under the OLD I
value is still taken at the end of a SEI or a PLP that masks it. RTI has
neither delay and VICE says why at `:1652`. The delay is implemented by
ENDING THE RUN after that one instruction rather than by a flag the dispatch
has to test — the countdown is set to 1, the unspent budget moves into `CARRY`
exactly as `c64_cut` does, and the C's slice loop re-enters `c64_run`, whose
first act is the interrupt check. **The one corner, stated**: where the
countdown is already 1 or less the run is ending anyway and the IRQ is taken
at the top of the next run with no instruction in between — one instruction
early, at a slice boundary the C chose, and the alternative is a compare on
the hottest path in the machine.

**BRK IS SEVEN CYCLES AND IT IS CHARGED ONCE** (`6510core.c:998`). It is an
opcode, so the dispatch charges its table entry; entering the vector path
charged seven more, and **14** is what a BRK cost until the fix pass. A
machine boots straight through that, and §4.6's cycle row could not see it
until the row became table-driven.

**The wall slice.** Separately and independently, the core is given a **raw
cycle budget** for how long it may hold the UI task. RUNCPM's structure,
exactly (SPEC.md §74.1):

- seeded from `os88_cpu()` (`OSAPI_CPU_INFO`, slot `0x0188`) — `512 <<
  os88_cpu()` clamped into the range below, which is
  `apps/runcpm/runcpm.c:1215`'s own line. **The fix pass found this sentence
  true of the plan and false of the code**: the budget always began at 256 and
  walked up four slices at a time, so the first dozen wakes of a 386 ran
  256-cycle slices with a full alarm query, a `c64_run` shell and a
  `c64_advance` over both CIAs, the VIC and the TOD around a quarter of a
  millisecond of emulated work;
- **256 to 16,384 cycles**, doubled when four slices fit inside one host
  tick, halved when one slice spans two tick boundaries;
- **and the doubling is CLAMPED to the cap, not merely stopped below it.**
  With one cap of 16,384 the test *"double only while the budget is under the
  cap"* happened to land exactly on it; with warp's higher ceiling (below) it
  lands on 32,768, and `int` is SIXTEEN BITS here — the budget arrives as
  −32,768 and the core expires before its first fetch, so warp would have
  stopped the machine dead. `hosttest/c64uitest.c` gates both ends of the
  range; nothing on a glass would have shown it;
- **only a genuinely EXHAUSTED slice adapts** — a wake that did not spend
  its budget leaves the estimate alone. On this machine there is no blocking
  console read to end a slice early (a C64 always has something to do), so
  the two ways a wake can end without spending its budget are the machine
  being PAUSED and the machine having JAMMED, and the harness drives both.
  LESSONS 13's finding: without the rule, typing into a paused machine walks
  the budget to its cap over a hundred wakes that ran no cycles at all, and
  the first wake after the resume is a second of stalled UI task.

**WARP MODE IS THIS CEILING RAISED, AND NOTHING ELSE** (Alt+W, §11.1). It is
worth being exact about, because a *"warp"* that does something else with the
same name is worse than no warp at all. **Nothing in this port paces the
machine against a wall clock** — that is SPEC.md §74.4's posture and it is why
§11.2 greys VICE's whole Emulation speed section — so the only throttle
between the 6510 and the host is the wall slice's ceiling: how many cycles a
wake may run before it hands the UI task back. Warp raises that ceiling from
16,384 to **30,000**, and 30,000 rather than 32,767 because `c64_m.cnt` is a
signed word (§4.2) and the alarm model may round a budget UP by the cycles of
the instruction in progress.

**AND THE OTHER HALF IS THAT IT DRAWS LESS, WHICH IS ALSO VICE'S AND WAS
NEARLY LOST.** One draft of this feature flushed every *ninth* host tick and
did not touch the budget at all — that is warp's second half standing in for
its first, and it is wrong. A review pass then removed the render throttle
altogether, on the reading that flushing less often *"does not run the machine
one cycle faster, it only stops showing what it does"*. **The reference
disagrees, twice, in its own words.** `src/vsync.c:339-340` sets
`warp_render_tick_interval = tick_per_second() / 10.0` under the comment
*"Limit warp rendering to 10fps"*, and `vsync.c:634-656` skips every frame
inside that interval while `warp_enabled` — *"Limit rendering fps if we're in
warp mode. It's ugly enough for dqh to weep but makes warp faster."* Drawing
less **is** half of VICE's warp, deliberately.

So this port does both, and the render cap is VICE's number rather than an
arbitrary one: **while warp is on, the flush rate is
`max(c64_flush_every, 2)`** — 18.2 Hz / 10 fps is 1.82 host ticks, so two.
The cap can only ever slow a flush down; the tier rate stands wherever it is
already slower. On this machine the render half is the **expensive** half:
§9.7 prices a full expose at 306 ms against a slice of 16,384 emulated
cycles.

**WHAT IT IS WORTH, MEASURED, AND WHERE IT IS WORTH NOTHING.** Under QEMU,
with a `FOR`/`PRINT` loop running, the status row went **2,860 % / 1,433.7 fps
→ 3,060 % / 1,534.8 fps of a real 6510**
(`build/port-shots/wave3r2-07-nowarp-speed.png`,
`wave3r2-06-warp-speed.png`) — **about 7 %**, which is the wake round trips
and the repaints saved and not emulation getting faster. **That 7 % is the
TWO HALVES TOGETHER — the lifted slice cap AND the render cap — and the
figure this paragraph used to carry is what turns it into a measurement of the
second one.** It read **2,070 % → 2,182 %, about 5 %**, off
`wave3-39-nowarp-3.png` / `wave3-40-warp-3.png`, and those two shots were
taken on the cap-only build, before the render throttle above was written.
Same machine, same `FOR`/`PRINT` loop, same reading off the same widget: the
delta between the two — 5 % with the slice cap alone, 7 % with both — is the
render cap's own worth, measured rather than argued, which matters because the
render cap is the half a review pass had already removed once on the argument
that it cannot be worth anything. **On the target — `CPU_8086` — BOTH halves are
no-ops, for two separate reasons**, and that is stated rather than discovered:

- the **slice** cap is never reached. At 4.77 MHz a 16,384-cycle slice is far
  more than one host tick of work, so the adaptation's halving arm settles the
  budget near its 256-cycle floor and the CAP is not what binds. Lifting a
  ceiling the machine never reaches changes nothing;
- the **render** cap does not bind either. That tier already flushes every
  OTHER host tick — 9.1 Hz, *slower* than VICE's 10 fps cap — for §9.8's own
  reason: a full repaint there is ~300 ms, five host ticks. `max()` of the two
  is the tier's own rate, unchanged.

Both DO bind on a 286 or a 386, where `c64_flush_every` is 1: there the cap
halves the drawing and the budget does walk up to the ceiling.
**AND THE ITEM SAYS SO ON THE TIER WHERE IT IS WORTH NOTHING, because this
document is not where the user is looking**: on `CPU_8086` the message is
`Warp mode on - no change.` and on every other tier it is
`Warp mode on.` — **this port's own status-row wording in both cases, not
VICE's**, and an earlier draft of this paragraph called the second one *"VICE's
plain `Warp mode on.`"*, which was drift of exactly the kind LESSONS.md 1
warns about. **The 8088 wording used to be
`Warp mode on - no faster on this CPU.` and it was 37 cells**, which is over
§10.1's 25-cell threshold — so the one message this port shows on the one
machine it is sized for took the LONG erase path and blanked `Joysticks:`,
both port indicators and the drive number for five seconds. It is shortened to
exactly 25 rather than reworded for taste, and §10.1 carries why the number is
25. VICE reports warp with a LED widget labelled `warp:`
(`uistatusbar.c:2607-2616`, `statusbar_led_widget_create`) and has no status
message at all; the only *"Warp mode"* sentences in the reference are
`autostart.c:703`'s log line `Turning Warp mode on.` and the monitor's
`mon_parse.y:290` `Warp mode is on.` These two follow the convention
`Paused.` / `Running.` already set on this row. The item
is not GREYED — on a 286 or a 386 the cap is a ceiling that binds and warp
does what VICE's does — but an item whose whole visible effect is a message
that is false is the shape SPEC.md §47 exists to stop, and PERFORMANCE.md's
*degrade by tier* already gives the port the means to answer: `os88_cpu()` is
a fact the code can test, and `c64_tier_init` has read it before the menu is
first drawn. The
alternative — suspending the halving arm as well, which is the only other
throttle there is — was considered and not taken: a 30,000-cycle slice on an
8088 is on the order of a second of stalled desktop, and the halving arm
exists precisely to prevent that (SPEC.md §74.1).

**The floor is never a whole jiffy.** Every device phase — the cycle counter,
each timer's remaining count, the raster position, the TOD accumulator — is
retained across slices and across wakes, so a slice may end anywhere. At the
8% of a real C64 that this port may turn out to run at, a whole emulated
jiffy would be ~200 ms of UI task; a 256-cycle floor is ~3 ms.

The wake is re-posted only **while the machine is running**, and PAUSE is
part of that condition, not only `c64_state`. Alt+P sets `c64_pause` and
leaves the state `C64_ST_RUN`, so a re-post condition written on the state
alone ran nothing, drew nothing and re-posted anyway — SPEC.md §74.1's own
sentence: *"a handler that always re-posts spins the UI task at ~1,400 wakes
a second"*, here for a machine the user deliberately stopped, at ~1 ms of far
calls and a task switch per empty round trip. It is one function,
`c64_wants_wake()`, after `apps/runcpm/runcpm.c:847`: something dirty, an
outstanding Advance frame, or a running and un-paused machine. **A MESSAGE
BEING UP IS NOT ON THAT LIST**, and §10.1 records why: it was the first term,
so a stopped machine spun the shared UI task for the whole five-second life of
every message with nothing to do inside the wake, which is this rule inverted.
A JAM stops it too, and `os88_onkey` / `os88_onclick` / `os88_oncmd` kick it
so a wake the full event ring dropped cannot park a running machine.

**EVERY 16-BIT COUNTER COMPARED AGAINST A CYCLE COUNT IS COMPARED UNSIGNED.**
`unsigned` is 16 bits here and a CIA counter's whole range is legal: `$FFFF`
is the reset-default latch AND the standard free-running-timer idiom
(`LDA #$FF / STA $DC04 / STA $DC05 / LDA #$11 / STA $DC0E`). Cast to `int`
that is `-1`, so `while (n > (int)c) { n -= (int)c + 1; … }` was true for
every n and subtracted NOTHING — an **infinite loop** inside `c64_advance`,
inside `os88_onwake`, on the UI task, raising an underflow flag for ever; any
latch in `$8000..$FFFE` was the same defect one step slower, because a
negative `(int)c` INCREASES n. In the scheduler the same wrap made
`(int)(c64_ta[k] + 1)` either 0 or negative, which always won the minimum, so
`c64_alarm_next()` answered 1 and the driver ran the core **one cycle at a
time** — a full alarm query, run shell and `c64_advance` per emulated cycle.
The rules: **compare unsigned**, and **a counter at or above `$7FFF` is
skipped rather than cast**, because the frame end is never more than 19,656
cycles away so such a counter can never be the nearest alarm. Neither showed
on the host (`int` is 32 bits there and nothing wraps) or in the core's gate,
which never uses a latch above `$7FFF`; the harness now carries the row with
a `short`/`unsigned short` model of the target's widths as its negative
control.

### 4.5 What `_c64_run` answers

Two values only:

| answer | meaning |
|---|---|
| `C64_RUN_SLICE` | the wall-slice cycle budget was spent between instructions |
| `C64_RUN_JAM` | a `KIL`/`JAM` opcode; the machine stops, the status row says so, the window stays up |

**AND ONE WAY OF "REACHING" IT WAS NOT A JAM AT ALL — found in wave 3,
fixed in the KERNEL** (`kernel/wm.inc` `wm_wake_sweep`, commit `0bc11cc`;
`docs/C64-PORT-PLAN.md`'s wave-3 record carries the reproduction). Launching a
second package — or any drag, or a launch from a Disk window — froze the speed
figures for good with no keystroke echoing, and Preferences showed **Advance
frame greyed**, which read as `C64_ST_JAM`. The 6510 had not jammed: the
package's `EVT_WAKE` had been drained off the event ring by one of six kernel
loops (`fm_dgstart`'s drag-detect among them) while `wm_wake`'s per-slot
coalescing flag stayed set, so every later post answered "already queued" and
the slice driver was never driven again — until a key or a click posted a
wake through the same flag. The flag is now the promise and the idle arm
sweeps it (SPEC.md §74.1); the wave-3 verifier saw the C64 and RUNCPM both
keep counting across a second launch and a drag.

**The line is VICE's and it is PERMANENT.** `Main CPU: JAM at $E5CF` — the
format string of `src/maincpu.c:612` with `CPU_STR` from `src/6510core.c:45`
and VICE's dialog padding dropped (§2's row). It goes up as a message,
because that is how it arrives, and then it **stays**: `C64_ST_JAM` is a
permanent status-row state — it is not a thing that stops being true, and a
five-second message
left a dead machine and an idle one showing the identical widget row
(`build/port-shots/wave2-05-jam.png` was that defect;
`wave2fix-18-jam-permanent.png` is the row eight seconds after the message
expired). §10.1 carries the rule for the row.

Alarms and I/O are **calls out of the core, not exits from it** (§3.4, §4.4),
so there is no mid-instruction exit and no caller ever has to resume a
half-executed opcode.

### 4.6 The core's gate

`apps/c64/hosttest/c64cputest.asm` + `.sh`, run by **`make c64cputest`**
(minutes, like `make rcz80test`; deliberately *not* in `build.sh`): the
**shipping `c64cpu.inc`**, in a boot sector, in raw QEMU, under `SS ≠ DS`.
It is not a Dormann wrapper — Dormann is one row of it. **There are TWELVE
rows.** `docs/C64-PORT-PLAN.md`'s Decision 22 says nine; the tenth is the
split at row 10 below (decimal `ADC`/`SBC` came out of Dormann's row and
became a row of its own when the fixture for it turned out not to exist as a
binary), and rows 11 and 12 are the fix pass's, each written because an
outside review found the row that was supposed to cover it claiming families
it did not execute. The rows:

1. **Klaus Dormann's `6502_functional_test`** to its success trap at `$3469`
   — the 65,536-byte binary, fetched at a pinned SHA-256 and **never
   committed**, read straight into the C64's RAM and started at `$0400`. The
   judgement is where the program counter SETTLES, which is how the test
   documents itself; the harness prints the address and the machine's
   registers, so a failure names its trap.
2. **Each of the seven bank maps** (§3.3): the right byte visible in each
   4KB region for each `$01` value.
3. **`$0000` and `$0001`**: DDR-derived banking, the read-back rules of §3.2,
   and a re-bank taking effect on the very next fetch.
4. **Reads, writes and FETCHES at `$9FFF`, `$A000`, `$BFFF`, `$C000`,
   `$CFFF`, `$D000`, `$DFFF`, `$E000`** — including an instruction that
   begins on one side of a boundary and takes its OPERAND from the other
   (§4.3), which is the case the first draft of this row claimed and did not
   contain: it put two whole instructions either side of `$A000` and tested
   no other boundary at all. Seven cases now: the two-instruction one, an
   immediate straddling `$9FFF`/`$A000`, one straddling `$BFFF`/`$C000`, a
   `JMP` whose HIGH byte comes from `$D000` — a register file, so the same
   case is the **slow I/O fetch** — one straddling `$DFFF`/`$E000` between
   CHARGEN and the KERNAL, a **backward `JMP`** from the KERNAL into RAM
   (§4.3's LOW edge, which a ceiling-only guard cannot see), and a **`$01`
   remap in the middle of the instruction stream**, where the program banks
   BASIC out and the next instruction must come from the RAM underneath.
5. **Real I/O stub returns**, not park-at-FAIL: the stubs answer values the
   test then checks, so the cdecl convention, the `DS` swap and the `ES`
   save/reload are all exercised rather than merely forbidden.
6. **`ES`/`DS` restoration checks** after every call out.
7. **IRQ and NMI entry**, including entry while a bank switch is pending.
8. **The illegal opcodes** `6510core.c` implements — and it EXECUTES every
   family it names, which the first draft did not: it contained LAX, SAX and
   DCP, so the missing decimal ARR and the wrong ANE magic constant both
   passed it for a whole wave. ARR in both modes with its flags, ANE, ALR,
   ANC, SBX and the four stores of row 11 are all driven, each with an answer
   only that opcode produces.
9. **Cycle totals per opcode family** against `6510core.c`'s table, page-cross
   and taken-branch penalties included (§4.2). **It is table-driven**, and
   that is the fix pass's: three shapes (LDA immediate, LDA `abs,X` either
   side of a page, one branch) left almost every family unchecked, and the
   branch in it started at `$0800` so its "page cross" never crossed. Eighteen
   entries now — every addressing mode of LDA, a store, three RMW forms, the
   stack pair, `JSR`/`RTS`, both `JMP`s, a branch taken, not taken and
   **taken across a real page boundary at `$08FD`** — plus **BRK's seven
   cycles** and an IRQ entry's seven, which is the row that catches §4.4's
   double charge.
10. **Decimal ADC and SBC over all 262,144 cases** — every accumulator, every
   operand, carry both ways, both instructions — against **`tools/c64dec.py`**,
   an independent implementation of the documented NMOS rules written in
   Python, exactly as `tools/c64ref.py` stands to the composer. The answer it
   hands the harness is four 16-bit checksums, because 262,144 expected
   results will not fit in a boot sector and any one wrong result changes the
   checksum it belongs to.

   **This row replaces "Dormann's decimal test" and the reason is a fact
   about the world.** `6502_decimal_test` is published as SOURCE only: the
   project's `bin_files/` carries the functional test's binary and the 65C02
   one and nothing else. An independent reference over the same space is what
   that row was asking for, and this is it.

   **AND IT IS A STATED DEPARTURE FROM THE WAVE'S DECISION, recorded here
   where the decision is.** The user's brief for wave 2 named the decimal
   fixture as *"fetched at a pinned SHA-256, never committed"*, and
   `tools/c64dec.py` is neither: it is a model written in this tree. A
   reference written by the same hand as the core is weaker evidence than a
   fetched one — that is the trade, taken because no fetched one exists —
   and the mitigation is that `c64dec.py` implements the documented NMOS
   rules directly and shares no line with `c64cpu.inc`'s decimal path, which
   is the same footing `tools/c64ref.py` stands on for the composer.

11. **The four unstable stores** — SHA, SHX, SHY and SHS. The mask comes from
   the **unindexed** base's high byte plus one (`6510core.c:1769`, `:1797`,
   `:1806`, `:1815`) and a page cross puts the VALUE in the target's high
   byte (`STORE_ABS_SH_*`, `:699-711`). This port used the INDEXED high byte
   — one more than VICE's whenever the index carried — and did not corrupt
   the address at all, which §4's header called a stated deviation and which
   VICE compiles. Five cases, each with a value the old code could not
   produce, and one where the store lands at `$0008` instead of `$2008`.
12. **What an interrupt puts on the stack, and what RTI takes off.** Row 7
   asks whether the handler ran and whether `I` is set afterwards, and both
   pass with the pushed bytes in any order and `B` either way. This row reads
   `$01FD`, `$01FC` and `$01FB` by name — PC high, PC low, then the status
   byte with **`B` clear** for an IRQ and an NMI (`6510core.c:456`) and
   **`B` set** for BRK — checks `S` landed at `$FA`, and then brings a BRK
   back through `RTI` to its own `PC + 2` with the stack restored.

**Every row carries a negative control** and the harness FAILS if a control
passes:

| row | the control |
|---|---|
| 1 Dormann | `ADC #` dispatched to `ORA #` — it must not reach `$3469` |
| 2 bank maps | the map row is shifted |
| 3 `$00`/`$01` | the re-bank does not take effect |
| 4 the boundary | the bank above it is made RAM |
| 5 I/O returns | the stub answers a constant |
| 6 `ES`/`DS` | the reload is NOPped out of the shipping text at runtime |
| 7 IRQ/NMI | nothing is pending |
| 8 illegals | `LAX abs` dispatched to `NOP` |
| 9 cycles | one opcode costs one cycle less |
| 10 decimal | `SED` dispatched to `CLD` |
| 8 illegals, again | `ARR #` dispatched to `AND #`, which has the same operand and a plausible answer |
| 11 unstable stores | `SHA abs,Y` dispatched to a plain `STA abs,Y` |
| 12 the interrupt stack | `BRK` dispatched to `NOP`: nothing is pushed and RTI has nothing to take back |

Every perturbation reaches the ENVIRONMENT or the core's own tables at
runtime, never the source: what is assembled is byte for byte what ships. And
two of them only started failing once the harness learned to CLEAR the bytes
a row reads before it runs — a control that read the value the positive run
had left at the same address passed, which is a control proving nothing.

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
  all off the cycle clock (§5.2). **The STATUS latches whether or not the
  interrupt is unmasked, and only the LINE is gated**: `vicii_irq_raster_set`
  (`vicii-irq.c:64`) sets `$D019` bit 0 unconditionally and
  `vicii_irq_set_line` (`:42-51`) is the only thing that reads `$D01A` —
  **and it both SETS and CLEARS `$D019` bit 7** from `irq_status & regs[0x1a]`.
  The fix pass found this port scheduling the compare only while `$D01A`
  bit 0 was set, so the commonest raster wait there is — `LDA $D019 /
  AND #$01` with interrupts disabled — never saw the bit at all; and bit 7,
  once raised, was never lowered, so `$D019` read `$8x` for the rest of the
  session after one acknowledged interrupt. The compare is now an alarm on
  every frame, which is one extra run boundary in 19,656 cycles.
- **8 hires sprites**: position, enable, priority against the background,
  x-expand and y-expand, composed into the rows they touch. Multicolour
  sprites are drawn by luminance threshold (§9.6).

### 5.2 Timing — the PAL frame, off the cycle clock

The PAL frame is **63 cycles × 312 lines = 19,656 cycles = 50.123 Hz**
(`c64.h:35-40`, `vicii-timing.h`). It has nothing to do with the 60 Hz jiffy,
which is a thing the KERNAL programs a CIA to produce (§6.3).

The raster counter is **computed from the cycle counter when it is read** —
`$D012` is `frame_cycles / 63` — and the raster COMPARE and the frame end are
alarms (§4.4). That is wave 2's amendment to this paragraph: the end of each
raster LINE was to have been an alarm, and scheduling one would have ended
the run 312 times a frame to keep a register up to date that reading it keeps
up to date for nothing. `$D012` still reads the line the cycle counter has
actually reached, a raster compare still fires at the line it was armed for
and in the right order relative to every CIA interrupt, and a program arming
two raster interrupts in a frame still gets both.

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

**FOUR THINGS A CIA HAS THAT A PAIR OF COUNTERS DOES NOT**, all four found by
the fix pass's outside review and each cited where VICE has it:

- **The two timers are advanced INDEPENDENTLY from the same elapsed count**
  (`ciacore.c:937`). Timer A's loop used to consume the count and timer B
  then advanced by what was LEFT of it, so a timer B counting φ2 lost every
  cycle timer A had already counted — and a one-shot timer A that stopped
  took timer B's whole slice with it. Only the CASCADE mode (CRB `INMODE`
  = 10) consumes timer A's underflows, which is what cascade means.
- **A raised interrupt is not cancelled by a MASK change.** The line used to
  be recomputed from `flags & mask` on every touch, so `LDA #$01 / STA $DC0D`
  — what a handler writes when it is finished with the timer — dropped the
  IRQ before the handler's own `LDA $DC0D` had acknowledged it, and
  re-enabling CIA2's mask manufactured a second NMI **edge** out of a flag
  nothing had read. VICE raises the output from the flags and drops it in the
  ICR **read** (`ciacore.c:950`'s *"Both pending interrupts and currently
  active interrupts are never cancelled or cleared"*, and `my_set_int(false)`
  at `:1345`), so the asserted output is a state of its own here.
- **The TOD is a clock, an ALARM, a read LATCH and a stop bit.** CRB bit 7
  selects which set of four registers a write lands in (`cia.h:90`,
  `ciacore.c:876`) — with no alarm registers there was no way to set an alarm
  at all, and code that tried set the TIME instead; a match raises ICR bit 2
  (`:236-242`). Reading HOURS latches all four and reading TENTHS releases
  them (`:1249-1270`), so a program cannot read 10:59:59.9 as 10:00:00.0.
  Writing HOURS **stops** the clock and writing TENTHS restarts it (`:848`,
  `:886-893`), which is how the four stores are made atomic. And the hours
  are **12-hour BCD with an AM/PM bit that toggles at 12** (`:1961-1977`):
  `09 → 10` and `12 → 01` are the two carries out of the units digit and both
  are `hl = hh; hh ^= 1`. A plain BCD increment with a `> $12` clamp — what
  this port had — runs `09, 0A … 0F, 10`, six hours that do not exist, and
  never touches AM/PM.
- **CRA/CRB bit 4 is a STROBE and is stored as 0** (`ciacore.c:1056`,
  `:1097`): VICE stores `byte & 0xEF`, so a program that reads a control
  register back, ORs a start bit in and writes it does not force-load the
  timer a second time.

### 6.2 CIA2 (`$DD00-$DDFF`)

Timers, TOD, ICR, **PRA bits 0–1 = the VIC bank** (inverted, as the hardware
has them), and **NMI from timer underflow**. The NMI line is CIA2's ICR ORed
with RESTORE (§7.4).

**PRA IS ALSO THE SERIAL BUS, AND IT IS READ BACK, NOT STORED.** Bits 3–5 are
ATN, CLK and DATA OUT; bits 6–7 are CLK and DATA IN. With no true drive VICE
answers `((PRA | ~DDRA) & 0x3F) | ((iec_fast_1541 & 0x30) << 2)` where
`iec_fast_1541` is `~(PRA | ~DDRA)` (`c64cia2.c:200-231`, `:150-162`,
`iecbus/iecbus.c:212-217`, `core/ciacore.c:805`) — so **CLK IN and DATA IN are
the INVERSE of CLK OUT and DATA OUT**: an empty bus reads back what this
machine drives. Answering the raw register instead reads DATA IN low, which is
a device replying, and `LOAD"*",8` then waits for it for ever instead of
timing out (§11.3).

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
4. **A fresh press is guaranteed one emulated KEYBOARD-SCAN INTERVAL in the
   matrix before release polling can clear it — and the unit is EMULATED
   CYCLES, not wakes.** `C64K_FRESH_CYC` is 20,000 6510 cycles: one CIA1
   timer-A period (16,421 at the KERNAL's own `$4025`) plus margin, stamped
   from `c64_cyc_lo` when the press is added and checked at every poll.

   **"One wake" is the wrong unit and it is a TARGET-ONLY defect.** A wake is
   one wall slice — 256..16,384 cycles (§4.4), floor 256 — against a scan
   every ~16,421, so a one-wake guarantee is between 1/64 and 1 of a single
   scan. Under QEMU the core runs at some thousands of per cent, an emulated
   jiffy is well under a millisecond of real time, and every press is scanned
   many times over; on a 4.77 MHz 8088 the core runs at a few per cent, a
   200 ms keypress buys a few thousand emulated cycles, and typing loses
   characters at random. That is PERFORMANCE.md's third emulator-invisible
   defect — input overrun — with the emulator-versus-target speed ratio as its
   cause, and no screendump can show it.

   **It is BOUNDED on purpose**: the entry ages on the emulated clock and not
   on being read, so a program that never scans the matrix cannot make a key
   stick. The harness asserts the rule in cycles (a press whose host key is
   already up survives several polls at 4,000 cycles and is gone one poll
   after 20,001), and the row it replaced asserted the defect.

The **down-list is 16 entries** and its overflow path is bounded and tested:
the 17th simultaneous key is dropped, not written past the end.

**The modifier keys never arrive through `os88_onkey` at all**, and that is
why the poll reads them directly: a bare Shift or Ctrl press produces no
`int 16h` event, so the host's shift state is `os88_key_down(KSC_LSHIFT) ||
os88_key_down(KSC_RSHIFT)` and nothing else.

**AND A BARE SHIFT AND A BARE CTRL ARE MATRIX KEYS IN THEIR OWN RIGHT**
(amended in the fix pass). `keyboard_latch_modifier_states`
(`src/keyboard.c:474-482`, `:505-512`) puts the left-shift and left-ctrl
positions into the matrix whenever the PHYSICAL key is down —
`left_shift_down > 0 && !virtual_deshift`, `left_ctrl_down > 0` — with no
other key needed. This port set SHIFT only when some OTHER key's mapping
asked for it and CTRL only for a recognised Ctrl chord or digit, so a game
polling `$DC01` for the shift key — the second fire button of a thousand of
them — saw nothing at all. The `.vkm`'s DESHIFT still wins, which is VICE's
`!virtual_deshift` in the same expression. It is what makes SHIFT+letter
the graphics character it is on the machine — the `.vkm` marks a letter
"optionally shifted" and the shift has to come from the host's own key, not
from the ASCII the BIOS folded it into.

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
| Alt+Insert | Paste | an AT BIOS `int 16h AH=0` drops the enhanced code `0x8B`/`0xA2`; the menu item is the route. The `int 16h` in question is `ui_task`'s own, `kernel/ui.inc:84-95` — the `AH=01h` peek and the `AH=00h` fetch that deliver every keystroke this package sees. (This row used to cite `kernel/mouse.inc:1426`, which is `mov ah, 1` inside `kbm_key`'s down-map bit builder and has nothing to do with `int 16h`; the citation had been copied into the shipping source, and in a repo where file:line IS the contract a citation pointing at an unrelated instruction is worse than none.) |
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
that no mouse has spoken, the status row's message area should print
**`ScrollLock for joystick`** — the fact, where the user is looking, instead
of a joystick that silently does nothing.

**IT SHIPS IN WAVE 3 — AND `os88_mouse()` IS NOT WHAT ANSWERS IT.** The
sentence above described a test the SDK cannot make: `osapi_mouse`
(`kernel/kernel.asm:3263`) tests `[mou_seen]` to decide whether to poll the
keyboard mouse and then answers x, y and the button. **It does not report
whether a mouse has spoken, and no slot does** — and adding one spends kernel
headroom, which SPEC.md §73.9 makes a decision rather than a build fix. So the
package asks a question it CAN answer and that has the same answer:

> `kbm_key` (`kernel/mouse.inc:934-941`) intercepts a cursor key **when, and
> only when, no mouse has spoken AND ScrollLock is off** — and an intercepted
> key never reaches `os88_onkey`. So *"the down-map says an arrow is held, and
> `os88_onkey` has never once delivered an arrow"* **is** *"the kernel is
> eating them"*, observed rather than inferred.

Three consecutive polls are required before the row says anything, because a
wake posted BEFORE a press is dispatched ahead of the key event queued behind
it, so ONE poll can legitimately see the ISR's bit with the `W_ONKEY` still in
the ring. `c64_arrow_typed` is a one-way latch: a machine that has ever
delivered a cursor key is a machine whose keys the kernel is not taking, and
it can never start taking them mid-session. The message is said **once a
session** (SPEC.md §47), and `hosttest/c64uitest.c` carries both the row and
the negative control — a machine that DELIVERS its arrows must never show it,
because on that machine the hint is wrong.

**AND THE JOYSTICK ITSELF WORKS EITHER WAY, WHICH IS WHY THIS IS A HINT AND
NOT A REFUSAL.** `kbd_track` runs inside the int 09h ISR
(`kernel/mouse.inc:1438`), *above* everything the UI task later decides, so
`os88_key_down` reports a held cursor key on a mouseless machine exactly as it
does on one with a mouse. What the user loses without ScrollLock is not the
stick — it is the desktop pointer, which every deflection drags across the
screen.

### 7.7 Copy and Paste — PETSCII in and out

**Edit > Copy (Alt+Delete)** and **Edit > Paste (Alt+Insert)** are VICE's two
clipboard actions (`src/arch/gtk3/actions-clipboard.c`), on os8088's system
clipboard (SPEC.md §55). Every conversion below is transcribed from
`src/charset.c` and none of it was invented here.

**AND EVERY PER-BYTE STEP OF BOTH IS RESIDENT, WHICH IS THE THING TO KNOW
BEFORE READING THE REST.** The command *shells* — the clipboard calls, the
refusals, the truncation notice — are `ovl_*` in `c64cmd.c`, because they run
once per pick. The 40×25 walk, the two conversion tables and the line-ending
fold are in `c64kbd.c` and are **resident**, because they run per BYTE.
SPEC.md §73.14 splits by FREQUENCY and `docs/C-TOOLCHAIN.md` states the
consequence in one line — *"the inner loop is assembly: anything that touches
bytes per iteration is a hand-written proc that C calls once"*.

**AND THE FIX PASS TOOK THAT SENTENCE THE WHOLE WAY: THE PER-CELL LOOP IS
ASSEMBLY NOW, AND THE COMMAND BODIES CAME BACK RESIDENT WITH IT.** `ovl_cmd`
sets a latch and returns; the bodies run from the wake, out of the lock (below).
What is left in `c64cmd.c` is the pick — two compares and a flag — and what is
left in `C64.OVL` for these two items is nothing at all.

Wave 3 shipped it the other way round and the review caught it: `ovl_copy`'s
inner loop was `call far [cc_ovv_c64_rd]` followed by
`call far [cc_ovm_ovl_sc_ascii]` — **two bridge crossings per cell, 2,000 for
one Copy**, each one a far call into `crt0.asm`'s `cc_ovthunk` (pop/pop, the
three-word LIFO stash, `call bx`, unstash, two pushes, `retf`) before any work
happened at all. `ovl_paste` had the same shape at one per byte. None of it
was visible: the cost model charged **one near call** for the lot (§9.7).

So, now:

| the loop | where it runs | what it costs |
|---|---|---|
| the matrix out of the RAM claim | `c64_zcopy_out`, **one call a ROW** | 25 near calls, ~4.3 ms |
| the row composed into the clipboard claim — table index, the reverse-video mask, the trailing-space trim, the `\n` and the store | **`c64_copy_row` (`c64mem.inc`), ASSEMBLY, one call a ROW** | ~20 µs a cell + 24 µs a row |
| screen code → ASCII | `c64_sctab[128]`, a **table** | one indexed load a cell |
| host byte → PETSCII | `c64_pettab[256]`, a **table** | one indexed load a byte |
| the CRLF fold and the typing | `c64_paste_feed`, **ten bytes a wake, no lock held** | ~1.4 ms a wake |

**THE COPY ROW IS ASSEMBLY BECAUSE OF WHAT SmallerC EMITS, AND THE NUMBER IS
THE ARGUMENT.** The C loop it replaces was **82 instruction bytes of
SS-relative word code reloading every local, ~356 clocks a cell — 75 µs**
(§9.7's `C64COST_CPCELL`, which is what that constant used to be).
`c64_copy_row` is **eleven instructions, 22 bytes, ~85 clocks of execution
against the 8088's 4.34-clock-a-byte fetch floor of ~95 — so ~95 clocks,
20 µs**, and it does the table index, the reverse-video mask
(`charset.c:204`), `last_non_whitespace` (`clipboard.c:67`), the trim
(`:81-85`), the `\n` and the store into the clipboard claim's own segment. The
C above it is a 25-iteration loop. This is `docs/C-TOOLCHAIN.md`'s rule and not
a preference, and the constant moving 75 → 20 µs is a **rewrite, not a
re-measurement**: the same work, emitted by hand. `c64_copy_row` has its own
case in `hosttest/c64memtest.asm` (section 4b) — the mask, the index, the store
into ANOTHER SEGMENT, the trim, an all-spaces row, a row whose last cell is
not a space, and `n = 0`.

**Both tables are built once, IN THE OVERLAY, on the FIRST WAKE** —
`ovl_conv_init` (`c64cmd.c`), which is also §13.3's first `ovl_*` call. The
two conversions are written out inside its two loops, 128 and 256 iterations,
~20 ms, off every hot path there is; the ARRAYS are bss and stay resident and
DS-relative, which is the whole of what makes the move legal (SPEC.md §73.14:
only code moves).

They used to be three resident functions — `c64_sc_ascii`, `c64_a_petscii`
and `c64_conv_init`, ~275 emitted instructions, **655 bytes of image** — and
the wave-3 review found them sitting in the resident half beside the loops
that index their output a thousand times a Copy. Once-per-launch is the *goes
out* side of §73.14's line, and this is the frequency split applied to the
port's own launch path rather than to a menu command. The three became one
function because an `ovl_*` calling an `ovl_*` crosses the bridge: three
functions would have been 384 far calls at ~58 µs each.

**The tables are unreachable on a disk that has no `C64.OVL`**, which is what
makes an unbuilt table safe: `c64_sctab` is read only by `ovl_copy`, and
`c64_paste_feed` cannot have a queue until `ovl_paste` has run. A `c64_conv_ok`
flag guards the Edit arm of `ovl_cmd` anyway, because a load refused for a
TRANSIENT reason (no heap at that moment) and granted later would otherwise
reach the loops with 128 zero bytes of table and put a screenful of NULs on
the user's clipboard.

**COPY IS `clipboard_read_screen_output` (`src/clipboard.c:41-100`).** The
40×25 matrix is read a ROW at a time and converted one screen code at a time
through three of VICE's functions composed in the order that file composes
them, then through `edit_copy_action`'s own final pass:

| step | file |
|---|---|
| screen code → PETSCII (`code & 0x7f`; `≤ $1F` +$40; `$40..$5F` +$20) | `charset_screencode_to_petscii`, `charset.c:202-210` |
| the CHROUT duplicates onto the proper codes (`$60..$7F` → `$C0..$DF`) | `petcii_fix_dupes`, `charset.c:112-120` |
| PETSCII → ASCII, `CONVERT_WITH_CTRLCODES` | `charset_p_toascii`, `charset.c:132-162` |
| unmappable PETSCII → `.` (`ASCII_UNMAPPED`, `charset.c:126`); `edit_copy_action`'s own pass changes nothing on this path | `charset.c:122-127`, `actions-clipboard.c:69-76` |

Trailing spaces come off each row (`clipboard.c:81-85`) and each row ends in
one `\n` — the line ending every non-Windows VICE uses (`:54-58`).

**AND AN UNMAPPABLE CELL COMES BACK AS `.`, NOT `?`, WHICH THIS PORT HAD
BACKWARDS.** The table row above used to read *"anything not printable ASCII →
`?` | `edit_copy_action`"*, on the reading that `charset.c`'s `.` is overridden
by `actions-clipboard.c:74`'s `?` — *"the second wins"*. It does not win,
because it never fires. `edit_copy_action`'s pass (`:69-76`) replaces a byte
only when it is **neither** `\r`/`\n` **nor** `c < 127 && isprint(c)`, and `.`
is printable; every byte `charset_p_toascii` can answer on this path is a
letter, printable ASCII, `' '`, `.` or the line ending. **VICE's Copy output
cannot contain a `?` at all.** `charset.c:122-127` gives the reason for `.` in
VICE's own words: these bytes end up in host FILENAMES, so a wildcard is the
one thing they must not become.

It is not a corner. The values that reach the unmapped arm after
`charset_screencode_to_petscii` + `petcii_fix_dupes` are `$C0` and `$DB-$DF`,
i.e. screen codes `$40`, `$5B-$5F`, `$60` and `$7B-$7F` and their
reverse-video twins — **the graphics cells**, which is exactly what a user
copying a game screen or a PETSCII drawing hits. The port was also
inconsistent with itself: its own font-fallback decoder already answers `.`
for the same cells (`c64scr.c`'s `c64_ascii_of`). `hosttest/c64uitest.c`'s
Copy fixture now puts screen code `$40` on the screen and requires `.` back,
so the substitution has a negative control; the row it had asserted only
letters, `!` and blank rows, and no graphics screen code was ever copied in a
test.

**AND THE LETTERS COME BACK LOWER CASE, WHICH IS VICE'S ANSWER AND NOT A
DEFECT.** `charset_p_toascii` maps PETSCII `$41-$5A` to `'a'-'z'` (`:156-158`)
and `$C1-$DA` to `'A'-'Z'` (`:153-155`), whatever character set the VIC is
drawing — so a Copy of the boot screen puts

```
**** commodore 64 basic v2 ****
```

on the clipboard, in lower case. `build/port-shots/wave3-14-np-pasted-z.png`
is that text read back by Note Pad. It is written down here because it is the
one detail a remembered version of this gets wrong.

**Copy says nothing on success**, because VICE says nothing: the result is on
the clipboard and the window it came from has not changed. A refused
`os88_clip_put` — over `CLIP_MAXKB`, or a heap that cannot fund it, and
`kernel/clip.inc:84` then leaves the clipboard **empty rather than stale** —
is a fact and is said on the status row (SPEC.md §47).

**COPY AND PASTE HOLD THE GFX LOCK FOR NOTHING AT ALL, WHICH IS THE FIX
PASS'S LARGEST SINGLE CHANGE.** `os88_oncmd` is dispatched **under the lock**
(`apps/cc/os88.h:566`), so every millisecond of a menu command is the whole
desktop stopped — LESSONS.md 6's *"no C between `gfx_lock` and `gfx_unlock`
that is not bounded by a count you can state"*. This section used to state
Copy's counts — 1,000 converted cells, 25 row pulls and **1,025 bytes handed
to `os88_clip_put`** — put **83.2 ms** on the harness's model beside them, call
it *"a floor, not a total"* because of the term below, and then argue that it
could stay. The argument for keeping it was that *"every other package in this
OS puts to the clipboard from `os88_oncmd` under the same lock"*, which is an
argument about those packages and not about this one: none of them converts a
thousand cells first.

So **`ovl_cmd` sets a latch — `c64_copy_req` / `c64_paste_req` — and
returns**, and a resident `c64_clip_service(base)` (`c64kbd.c`) runs the whole
body from the **TOP of the next wake**, above the `c64_abt` gate and above the
slice, with **no lock held**. **Nothing about the result changes, and the
reason is that the 6510 advances only inside `os88_onwake`**: between the
command's dispatch and the top of the next wake not one emulated cycle has
run, so the matrix and `c64_mbase` are bit-identical to what the user was
looking at when the item was picked. It is the same screen, copied a wake
later. A PAUSED or a JAMMED machine services the request as well — the wake
that serves it simply runs no slice afterwards — which is what keeps Copy live
on a jam (below). The milliseconds in §9.7's two rows are now WAKE time:
**28.6 ms for Copy, 0.7 ms for Paste, and zero held lock in both.**

**AND `os88_clip_put` IS NOT ONE FAR CALL, WHICH IS WHAT THE FIRST MODEL
CHARGED IT AS.** `kernel/clip.inc:70-125` is what runs there — a `clip_drop`,
a `mem_claim`, and a `rep movsb` of every byte — ~3.7 ms for 1,025 of them at
17 clocks a byte on the 8088's 8-bit bus, which is charged
(`C64COST_ZBYTE10`, the same constant the port's other movers are priced at)
and was the difference between the 79.4 and the 83.2 this section used to
quote. The term is unchanged; what changed is that it is spent in the wake and
not under the desktop's lock.

**THE CLAIM IS THE PART THAT IS UNBOUNDED AND IS NOT MODELLED.** `clip_put`
reclaims whenever the rounded KB differs from the clipboard it already holds
(`clip.inc:96`), so the FIRST Copy of a session always takes that path; and
`kernel/memory.inc:363` falls back to `mem_compact` and then `mem_shed_one`,
whose own header prices a compaction at *"a memcpy in tenths of a second"*.
This package is precisely the one that fragments the arena — it holds a
pinned 64KB and a pinned 20KB claim in the middle of it. **The term still
exists and is still not modelled, so §9.7's 28.6 ms is not a total either.
WHAT CHANGED IS WHOSE MILLISECONDS THEY ARE.** It is no longer a FLOOR under a
held lock — that phrasing was this section's own, and *"83.2 ms is a floor, not
a total"* was written beside the sentence that the alternative *"— doing the
put from `os88_onwake`, which holds no lock — is written down here as the move
to make if a measurement on real hardware ever says otherwise"*. No measurement
was needed to decide it: an unbounded term inside the DESKTOP's lock is the
shape LESSONS.md 6 forbids by name, and the identical term inside a wake is the
ordinary price of a heap claim that every package in this OS pays. The move
that was written down is the move that was made, and the two claims of the
section below are the rest of it.

**BOTH ITEMS ARE GREYED BY STATE (SPEC.md §47), which wave 3 did not do.**
`c64_menu_state` sets them from the same two facts that grey Advance frame:

| item | greyed when | why |
|---|---|---|
| **Paste** | `c64_norom`, `C64_ST_JAM`, **or `c64_pause`** | `c64_paste_feed` is called only from the arm of `os88_onwake` that runs a slice — `C64_ST_RUN` **and** `!c64_pause \|\| c64_adv` — and `c64_wants_wake` answers 0 in every other case, so a paste there queues bytes **nothing will ever type**, in silence. PAUSE is the third and was missed, because pause is not a STATE, it is a flag on `C64_ST_RUN`: the first two last the session, the third self-heals when the user resumes, and it is greyed anyway because a command whose whole visible effect is nothing at all is the same defect either way. The pause latch in `ovl_cmd` already calls `c64_menu_state`, so the item greys and UN-greys with the state |
| **Copy** | `c64_norom` only | the clipboard is kernel-owned and outlives this app (SPEC.md §55.3); with no machine the matrix holds `c64_ram_pattern`'s factory fill, so a Copy would **destroy whatever the user had copied somewhere else** in exchange for a screen they cannot see. On a JAM it stays LIVE: the frozen screen is real, and VICE's Copy always copies what is displayed |

The fact that greys either is already the permanent line on the status row
(§1.4, §4.5), so nothing new is said. **`os88_onkey` guards the two chords the
same way** — the kernel never dispatches a disabled item, and Alt+Delete /
Alt+Insert are dispatched by the package itself where the BIOS passes them
(§7.5), so without the guard the chord walks round the greying.

**PASTE IS `paste_callback` (`actions-clipboard.c:96-110`)**: the clipboard
converted with `charset_petconvstring(CONVERT_TO_PETSCII)` and handed to
`kbdbuf_feed`. The byte map is `charset_p_topetscii` (`charset.c:174-199`) —
`'a'-'z'` → `$41+`, `'A'-'Z'` → `$C1+` and deliberately **not** the `$61`
duplicates (`:189-192`), `` ` `` → `$27`, everything under `$20` or at or
above `$7B` → `?` (`PETSCII_UNMAPPED`, `:172`). A consequence worth knowing,
and it is a real machine's too: a listing pasted in lower case types as the
BASIC keywords it looks like, and one pasted in UPPER case types as the
graphics characters `$C1-$DA` draw.

**THE LINE ENDINGS ARE TESTED BEFORE THE BYTE MAP, AND CRLF IS ONE LINE END.**
`test_lineend` (`charset.c:49-63`) takes CRLF as two bytes and one ending, CR
and LF as one each, and every one of them becomes a single PETSCII CR
(`:72-74`). Without the pair test a document written on a DOS machine types
two RETURNs a line, which in BASIC is a listing with a blank line between
every statement. In this port the PAIR test is in the feeder and the
single-byte answer is `c64_pettab[$0D] = c64_pettab[$0A] = $0D`, written over
the byte map's own `PETSCII_UNMAPPED` in `ovl_conv_init` — because a line
ending is not a byte-map entry, which is exactly what `charset_petconvstring`
testing for it *first* means.

**AND THE CONVERSION HAPPENS TEN BYTES AT A TIME IN THE FEEDER, NOT IN THE
COMMAND.** VICE converts the whole clipboard up front and can afford to; here
that loop is ~145 µs a byte of SmallerC's own code (§9.7) and `os88_oncmd`
runs **under the gfx lock**, so a full 2,048-byte queue converted there would
be ~300 ms of desktop stopped dead for a command that draws nothing. `os88_onwake` holds
**no** lock and is already bounded by a count this section states — the ten
bytes the KERNAL's buffer holds — so that is where the byte map runs. Nothing
about it is observable from the machine's side: the same PETSCII arrives in
the same order at the same pace, and what the wake that services the command
does is one `os88_clip_get_seg` into the paste claim. **Edit > Paste is
0.7 ms whatever the clipboard holds, and none of it is held lock** (§9.7) —
the row read 0.9 ms of HELD LOCK while `ovl_paste` did the `clip_get` inside
`os88_oncmd`. The split by frequency survives the move out of the lock
unchanged: converting the whole queue in one go would still be ~300 ms in one
wake, which is a stalled UI task rather than a stopped desktop, and the
KERNAL's ten bytes are still the only count worth being bounded by.

**AND THE TYPING IS THE KERNAL'S, NOT OURS.** `kbdbuf_flush`
(`kbdbuf.c:370-410`) puts nothing into the machine while its own buffer is not
empty — `kbdbuf_is_empty` is `mem_read($C6) == 0` (`:233-236`) — and never
more than the ten bytes that buffer holds. The three numbers are
`kbdbuf_init(631, 198, 10, …)` (`src/c64/c64.c:1124`): the buffer at
**`$0277`**, the count at **`$C6`**, ten bytes. VICE calls that flush once a
frame from the vsync handler; the equivalent here is **once per wake from the
slice driver, before the slice**, so the machine drinks what it is given
inside the same wake — at most ten `c64_wr` calls and one `c64_rd`, whatever
the clipboard holds. A program that stops reading stops the paste, which is
what happens when you type at a real one.
`build/port-shots/wave3-34-paste-typing.png` is a paste caught mid-flight.

**AND A PASTE STOPS AT AN EMBEDDED NUL, BECAUSE THAT IS WHAT ENDS THE STRING
VICE CONVERTS.** `charset_petconvstring` is `while (*s)` (`charset.c:71`) and
`kbdbuf_feed` takes a C string, so a NUL is where VICE's paste ends and the
question never arises there. **It arises here, because this kernel's clipboard
is BYTES and not a string** (SPEC.md §55): a package may be handed one, and
without the test the byte map turned `$00` into `PETSCII_UNMAPPED` and the
machine typed a screenful of `?` for whatever binary followed. What has already
been produced STANDS — the bytes before the NUL are a real paste and are
typed — and the claim goes back with the queue when it drains.

**A RESET EMPTIES THE QUEUE** (`kbdbuf_abort`, `src/kbdbuf.c:312-320`, called
from `machine_reset` at `src/machine.c:262`): without it the previous
machine's paste types itself into the new one. **The citation used to be
`kbdbuf.c:318`'s `num_pending = 0` inside `kbdbuf_reset`, and that is a
different function** — `kbdbuf_reset` at `:297` re-initialises the buffer's
LOCATION and SIZE and does not clear `num_pending` at all, so the line that
was quoted for this rule is not the line that implements it. VICE's abort is
conditional on `kbd_buf_cmdline`, which has no equivalent here; this port
aborts unconditionally, and `c64_paste_stop` is the one place it happens.

**TWO STAGING AREAS, SEPARATE ON PURPOSE, EACH SIZED FROM ITS OWN DIRECTION —
AND NEITHER OF THEM IS BSS ANY MORE.** Copy's output and Paste's queue are
separate because the queue outlives its command by however long the machine
takes to drink it; one shared area would have let a Copy taken during a long
paste silently rewrite what was still being typed. That much is unchanged.
What changed is where the bytes live: **`c64_clip[1026]` and
`c64_pastebuf[2048]` were 3,074 bytes of bss held for the life of the app so
that two menu commands nobody may ever pick would have somewhere to put their
bytes**, and §13.0.1 is where that showed up as a third of the wave's cost.
Each is a **transient heap claim of 2KB** now:

- **Copy's is taken and freed inside one wake** — `os88_mem_claim`, the 25
  rows composed into it, `os88_clip_put_seg`, `os88_mem_free`, and it is gone
  before the slice runs;
- **Paste's is held from the command until the queue drains**, because that is
  exactly how long the bytes are needed, and **`c64_paste_stop` is the ONE
  place it goes back** — drained, a reset, or a second Paste over the first.
  One route out, so there is one place to get it wrong.

**A claim that cannot be had is a refusal that is SAID** — `No memory for the
copy.` / `No memory for the paste.` — and the machine is untouched: nothing was
converted, nothing was queued, the clipboard still holds whatever it held.
This is CWORD's lesson taken at face value: **the next byte to save is a buffer
moving to a claim**, and a buffer two rarely-picked commands share between them
is the cheapest one in the program to move.

**AND THE BYTES REACH A CLAIM THROUGH TWO NEW SDK THUNKS.**
`os88_clip_put_seg(seg, off, len)` and `os88_clip_get_seg(seg, off, cap)` are
`os88_file_read_seg`'s shape applied to `OSAPI_CLIP_PUT` / `OSAPI_CLIP_GET`,
and they are cheap to have: both slots already take `ES:SI` / `ES:DI`, so what
was needed was a thunk that loads `ES` from its argument instead of from `DS`.
The feeder reads its queue with **`os88_peek`** over a 21-byte window —
`C64_PFWIN = C64_KB_SIZE * 2 + 1`, because ten delivered bytes consume at most
twenty when every one of them is a CRLF pair, **and the `+ 1` is the lookahead**:
without it the pair test at the END of a window cannot see the `\n` that
follows a `\r`, and the stray `\n` becomes a second RETURN on the next wake —
this section's own two-RETURNs-a-line defect, reintroduced one window boundary
at a time.

| staging | bytes | what the number IS |
|---|---|---|
| Copy's claim (`C64_CLIPKB`) | **1,026** = `40 × 25 + 25 + 1` produced into a 2KB claim | the screen, one line ending a row and the NUL — the largest thing `clipboard_read_screen_output` can produce. This one really is Copy's bound |
| Paste's claim (`C64_PASTEKB`, `C64_PASTEMAX`) | **2,048** in a 2KB claim | what ONE `os88_clip_get_seg` can be handed. `OSAPI_CLIP_GET` has no offset (`kernel/clip.inc:145-157`), so a clipboard cannot be read in chunks and the queue IS the staging; 2,048 is ~50 lines of a BASIC listing |

Wave 3 sized the paste queue at 1,026 as well and justified it as *"the
largest thing Copy can produce"* — which is true of Copy and **is not a bound
on anything a paste can be handed**. VICE's own queue is `QUEUE_SIZE 16384`
(`src/kbdbuf.c:60`) and this OS's clipboard ceiling is 32KB (SPEC.md §55.2);
2,048 rather than either was a **bss decision taken against SPEC.md §73.9's
55,000-byte trigger** with the VIC's wave-4 work still to land, and it is
stated here rather than left as an accident. **It is a HEAP decision now and
2,048 stands anyway**: the claim is transient, so the trigger no longer argues
against it, and what argues for keeping it is that a 16KB queue would be a
16KB claim a low-memory machine can refuse for a paste of forty characters. **A clipboard bigger than the
queue is pasted as far as it fits and the truncation is said** — `Pasting the
first 2048 bytes.` The message names the number and `hosttest/c64uitest.c`
fails the build if the two ever drift apart.

**AND A FULL CLIPBOARD IS NOT AN EMPTY ONE.** `os88_clip_size` answers `AX`
and the package's `int` is 16 bits; `kernel/clip.inc:84` refuses only what is
strictly *above* `CLIP_MAXKB * 1024`, so a clipboard of **exactly 32,768**
bytes is legal and arrives as `0x8000` — **−32,768**. Wave 3 tested `sz <= 0`
and so reported `The clipboard is empty.` on a full clipboard, queued nothing,
and made the truncation notice false in the same breath: the one message the
user got was the untrue one. The test is `sz == -1`, which is the only answer
that means empty and is not a length any put can have, and the size comparison
is unsigned. The harness drives a 32,768-byte clipboard, and the stub casts
through `short` so that the host's 32-bit `int` cannot hide the one arithmetic
the target does.

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

**A KEYSET KEY DRIVES THE STICK AND IS NOT TYPED** (amended in the fix pass,
and it is the rule this section shipped without). `keyboard_key_pressed`
(`src/keyboard.c:788-798`) walks the ports mapped to NUMPAD/KEYSET1/KEYSET2,
calls `joystick_check_set` for each and **`return`s before
`kbd_queue_pushkey`** when one takes the key. Without it the four cursor keys
drove port 2 **and** entered the matrix at the same time: a game polling
`$DC00` for the stick also got phantom cursor presses out of `$DC01`, and
moving the stick in BASIC walked the cursor. The consumption is applied where
the matrix is BUILT, so the entry stays in the down-list and its release is
still tracked by the ordinary poll — the matrix is all CIA1 can see, and it
is what VICE's early return protects. **`joykeys_enable` is the flag VICE
tests first** (`joystick.c:598-601`) and it is a variable here too, so the
harness can turn the keyset off and show the same key reaching the matrix
again — the negative control the rule needs.

**A CONSEQUENCE, STATED: the four cursor keys no longer move the BASIC
cursor.** That is what a real machine with a keyset joystick on port 2 does,
and it is why `KeySetEnable` is a resource in VICE at all. The C64's own two
arrow KEYS — `←` on End and `↑` on Page_Down (§7.1) — are unaffected, and the
KERNAL's repeat is still visible on the space bar and INST/DEL, which are the
other two keys it repeats.

**Ctrl is both fire and the CTRL key, and it is the ONE departure from the
consumption rule.** A game reading CTRL+letter from the matrix while the
joystick fires sees both — which is exactly what a real machine with a
keyboard and a joystick plugged in does — but a BASIC user holding Ctrl to
type a colour code also fires port 2. Stated, not fixed: the CTRL+digit and
CTRL+letter paths of §7.3 need it in the matrix.

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
window with the **status row off the bottom** — the row that carries
§4.5's permanent jam line and every refusal
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
- **AND THE FLUSH TAKES ALL OF IT IN ONE CALL** (amended in the fix pass).
  `c64_dirty_scan` read the 32-byte bitmap, the write window's two words and
  the ANY byte **one byte at a time** through `c64_scr_rd`/`c64_scr_wr` —
  C-callable near thunks into the emulated machine's memory, about **fifty of
  them per flush**. PERFORMANCE.md prices a bare near call + `ret` at 11 µs
  and the 8088 fetches at 4.34 clocks a byte, so each is ~38 µs: **~1.8 ms a
  flush**, comparable to the whole of §9.7's `one changed cell` row, spent
  before a single pixel is decided — and invisible to a cost model that
  counts drawing calls. `c64_dirty_take` (`c64mem.inc`) is one `rep movsw`
  and one `rep stosw` over 36 bytes, ~190 µs, and it resets the scratch in
  the same pass. `hosttest/c64memtest.asm`'s case 3b runs it on a real x86
  with `SS ≠ DS`, checks the documented slots — `dst[0..31]` the bitmap,
  `dst[32..35]` the window — and checks the 37th byte is a guard it never
  reaches.

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
rerun draws from `W_ONKEY` and `W_ONCLICK`, and **this package arms a clip
region itself** in `os88_onwake`, `os88_onclick` and `os88_about`, because
none of those is a paint. Measured both ways in one session: a 40-cell
`font_run` is 718 counts unclipped and **922 clipped**, a 320×8 `blit1` 40 and
**47**; the composer's own rows do not move at all, because they are package
code and never ask the kernel.

**AND THE KERNEL ARMS NO CLIP FOR `W_PAINT`** — this paragraph used to say the
opposite, and it was wrong about the kernel in a way the next person would
have built on. SPEC.md §11.3 rule 3 is explicit that *"the repaint path must
stay unclipped"*, and `kernel/wm.inc:9642` clears `wm_clip_n` before the title
bar, which is before the `W_PAINT` dispatch below it. So the four paint-path
rows in the table — `a full expose`, `an expose with the About panel up`,
`the About panel closing`, `entering fullscreen` — are priced with the wake
path's CLIPPED constants and are therefore an **upper bound**, over-reported
by up to a quarter of their `font_run` and blit content. The wake-driven rows,
which are the ones a running machine actually pays, are exact.

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

**AND THE SCRATCH IS PRICED NOW, WHICH IS WHY EVERY ROW BELOW MOVED.** A
flush's cost is not only what it draws: it reads the core's scratch through
near thunks, and `c64_dirty_scan` used to make about fifty of them (§9.2).
`C64COST_SCRACC` = **38 µs** a `c64_scr_rd`/`c64_scr_wr` and `C64COST_TAKE` =
**190 µs** for `c64_dirty_take` are the two constants that pay for it, and
they are **MODELS, not bench rows** — the only figures in this table that are
— because `tests/c64band` measures `c64band.inc` and these live in
`c64mem.inc`. They are computed from PERFORMANCE.md's own constants: 11 µs
for a near call + `ret`, plus the body's clocks at 0.21 µs each on a 4.77 MHz
8088 whose 8-bit bus adds 4 clocks to every word access (~125 clocks for a
thunk, ~820 for the take's two `rep`s over 36 bytes). The rows that draw
nothing are what this changes: a flush that composes nothing is **0.1 ms**
where it used to read 0.0, and one that takes the dirty bitmap **0.5 ms**.

**AND THE OVERLAY BOUNDARY IS PRICED NOW, WHICH IS WHY THE COPY ROW MOVED FROM
25.0 ms TO 79.4.** Wave 3's two clipboard rows were written down *below the
floor*: the model charged `c64_rd` at 25 µs as a NEAR call, and as built that
call was a FAR call through a resident shim with a second far call to the
conversion behind it — 2,000 crossings a Copy, and the loop body itself
charged zero. Five constants pay for it now, all in `c64scr.c` beside
`C64COST_SCRACC`, all **models** and each with its arithmetic written out
there: `C64COST_OVLCALL` = **58 µs** a bridge crossing (PERFORMANCE.md's
46.7 µs far call + the shim's 11 µs near call), `C64COST_ZCALL` = 26 µs and
`C64COST_ZBYTE10` = 3.6 µs a byte for `c64_zcopy_out`, `C64COST_CPCELL` for
one cell of the copy loop and `C64COST_PSBYTE` = **145 µs** for one byte the
paste feeder types. The last two are **counted, not guessed**: the loop bodies
were extracted from `build/c64.gen.asm` and assembled — 82 and ~105
instruction bytes — and priced at the 8088's 4.34 clocks-per-byte fetch floor.
Without a bridge constant the next `ovl_*` that loops would be priced the same
way and nobody would see it.

**AND `C64COST_CPCELL` IS 20 µs, NOT 75, BECAUSE THE LOOP WAS REWRITTEN AND NOT
BECAUSE IT WAS RE-MEASURED.** The 75 µs was honest about 82 instruction bytes
of SmallerC's SS-relative word code reloading every local, ~356 clocks a cell.
Edit > Copy's per-cell loop is **assembly** now — `c64_copy_row` in
`c64mem.inc`, eleven instructions, 22 bytes, ~85 clocks of execution against a
~95-clock fetch floor, so **~95 clocks, 20 µs** (§7.7) — and a **new
constant, `C64COST_CPCALL` = 24 µs**, pays for that proc's own call and shell
once a ROW: 11 µs for the near call + `ret` and ~60 clocks of prologue,
segment load and epilogue at 0.21 µs. A per-cell constant with no per-call
constant beside it is how a proc that C calls 25 times gets its shell for
free.

**AND THE CONVERSION IS CHARGED FROM ITS OWN COUNTER NOW.** It used to be
charged from the bytes `c64_zcopy_out` moved, which was right by
**coincidence**: `c64_copy_screen` was that mover's only caller and moved
exactly one byte per converted cell. Wave 4's VIC work is precisely the shape
that breaks the coincidence — a bitmap row or a sprite fetch moves bytes
through the same mover and converts nothing — and would have been priced at
320 cells of a conversion that does not happen.

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
| one changed cell | **4.2 ms** | compose 1 cell; 1 blit call |
| one changed row | **12.5 ms** | compose `last − first + 1` cells; 1 blit call |
| a `k = 9` scroll | **258.4 ms** | 1 scroll + `k` drawn rows; **1 scroll per flush** — and all 25 rows composed and compared, which is §9.4's guarantee and not a miss |
| **a `k = 1` shift the signature got WRONG** | **256.9 ms** | 1 scroll, and the colliding row DRAWN: the glass matches the row's own sources (§9.4) |
| a `k = 3` scroll, `gfx_scroll` refusing | **302.0 ms** | spans, and the shadow stays true |
| **two pokes, rows 0 and 24** | **127.2 ms** | 13 composed rows, **2 blits** — the window ∩ the pages (§9.2); the window alone made this 25 rows and 299 ms |
| a full expose, 25 rows | **306.4 ms** | ≤ 25 composed rows + the border + the status row's 37 glyph cells |
| 25 rows changed, not a shift | **301.1 ms** | no scroll emitted |
| a `$D020`-only change | **3.5 ms** | fills only, **no band composed** — and the write must cross the background's luminance, which is the only $D020 write that changes a pixel (§9.6) |
| **a `$D020` change that keeps the LEVEL** | **0.5 ms** | **0 fills, 0 composes** — colour 14 to 15 over background 6 is still lighter than the paper, so `c64_lum_update`'s flip test says nothing moved. `c64_io_wr` used to raise `c64_border_dirty` unconditionally one line after calling the routine whose whole job is to decide it, which made the guard dead code and cost four `gfx_fill` calls on the next flush after EVERY `$D020` write — many a frame, in the rasterbars and loader flashes that are among the commonest things C64 software does |
| **a changed cell, `blit1` REFUSING** | **33.3 ms** | §9.5's font path, ONCE — the row is drawn with the kernel's face and not retried, and the fact is said (a `kern_small` kernel) |
| 8 × `$D011`, `$D016`, a `$DD00` serial edge | **0.1 ms** | **0 blits, 0 composes** — a register write that changes nothing costs nothing (§9.3) |
| a `$D016` change that draws the same picture | **255.3 ms** | 25 composed rows, **0 blits, 0 fills** — recompose, then ASK the shadow; and a frame register does not touch the border |
| an expose with the About panel up | **324.5 ms** | 12 composed rows: the 13 the panel covers are not drawn under it. It is MORE than a full expose because the panel itself is 198 glyph cells — which is why the row below exists |
| **an expose that misses the panel** | **17.0 ms** | the panel is redrawn only when the damage rect reaches it |
| the About panel closing | **139.3 ms** | 13 composed rows — the panel's rect, not the screen |
| a `k = 1` scroll on a CLAMPED window | **154.5 ms** | 1 scroll + 1 drawn row, on 15 rows of glass |
| one joystick indicator changed | **1.4 ms** | **one** `blit1`, **no fill** — the status row's delta (§10.1), reached the way the product reaches it |
| **a short message going up** | **22.8 ms** | 1 fill of the row's **first 25 cells only** and the message's 23 glyph cells — **the joystick widget, the drive number and the two lamps are not touched** (§10.1). It used to erase all 42 cells and put back the message and the lamps, which after wave 3 meant that DEFLECTING THE JOYSTICK blanked the two indicators that report the joystick |
| **…and coming down again** | **26.0 ms** | 1 fill of the same 25 cells and the two speed fields rebuilt — **24 glyph cells, not 37**: the 13 cells right of the message never changed, so they are not redrawn |
| **a long message going up** | **35.1 ms** | 1 fill of the whole row, 33 glyph cells and the two lamps — a 33-cell message has nowhere else to go, and this is the negative control that shows the row above is measuring something. The flush after it expires rebuilds the widgets it blanked (`c64_st_blank`) |
| **the speed figures changed** | **3.7 ms** | **TWO** `font_run` calls, one per changed NUMBER field, and typically one glyph cell each — **no fill**. Each must land inside its own number and never on the literal tails (§10.2); the two cannot coalesce, because the fixed `% cpu` tail sits between them at x = 56..95. This is the program's only redraw on a TIMER, and the row that used to say **1.7 ms and one call** described a FIXTURE: it advanced the cycle counter and left the frame counter at zero, so only one field moved — which one fold of this program cannot do |
| the same row with the delta switched off | **42.0 ms** | the negative control: 1 fill + 37 glyph cells, which is what the widget used to cost every second |
| **entering fullscreen** | **306.4 ms** | one whole repaint, the kernel's own (§9.8) |
| **the wake after entering fullscreen** | **0.3 ms** | **0 blits, 0 composes** — `OSAPI_FULLSCREEN` repaints synchronously in both directions, so the shadow already describes the new glass |
| **entering fullscreen at 2× on VGA** | **892.0 ms** | the same repaint with `c64_band_x2` under every band: 25 doubled rows at 20.19 ms on top of the compose, against **306.4 ms** at 1:1 in the row above. This is the number that decides §9.8's tier table, and it is why the `CPU_8086` tier does not magnify |
| **the wake after entering fullscreen at 2×** | **0.2 ms** | **0 blits, 0 composes** — the latch is not paid for twice at either scale (§9.8) |
| **a `k = 3` shift at 2×** | **126.6 ms** | 1 `gfx_scroll` — `dy = k × c64_sch` = 48 glass pixels, not 24 — and `k` doubled rows drawn. The alternative the plan allowed, falling back to bands, is 25 doubled rows and about half a second for what one scroll does (§9.8) |
| **Edit > Copy of the whole screen** | **28.6 ms** | **0 blits, 0 fills, 0 glyph runs**: Copy draws nothing at all, and **NO LOCK IS HELD for any of it** — the body runs from the top of the next wake (§7.7). 25 `c64_zcopy_out` row pulls, 25 `c64_copy_row` calls, 1,000 cells at 20 µs of ASSEMBLY, **1,025 bytes through `os88_clip_put`'s own `rep movsb`** (kernel/clip.inc:118, ~3.7 ms) and **ONE** overlay bridge crossing — `os88_oncmd` → `ovl_cmd` and back, the runtime's far entry, which is all that is left of a command whose body is resident. The row is taken on a screen with no space on it, because that is what it is called and it is the worst case; the sparse fixture the assertions run on puts 71 bytes on the clipboard and hides the copy entirely. **It is still not a total**: the claim and `clip_put` may compact the data arena and that term is not modelled — it is WAKE time now rather than desktop-stopped time, which is the whole of why it is acceptable (§7.7) |
| **Edit > Paste of any clipboard** | **0.7 ms** | **0 blits, 0 fills, 0 composes, NO LOCK HELD**, and it does not depend on the length: one `os88_clip_get_seg` into the paste claim and **ONE** bridge crossing (`os88_oncmd` → `ovl_cmd`, which sets a latch and returns). The row read 0.9 ms and FOUR crossings while the body was an `ovl_paste`. The byte map is the feeder's, below |
| **a wake typing ten pasted bytes** | **1.4 ms** | **NO LOCK HELD** — `os88_onwake`. Ten bytes converted and put in `$0277`, at 145 µs each, and only while a paste is draining (§7.7) |
| a full bitmap frame | wave 4 | the **measured ms**, not the call count |
| one sprite moved one cell | wave 4 | the spans it actually touched |
| a slice with no tick boundary | **0.1 ms** | **0** (§9.3) |

**Re-taken again by wave 2's SECOND fix pass**, which is where the `scratch`
column of the harness's own print comes from: every row gained the 0.1–0.6 ms
of scratch access the model had not been charging, the `$D020` pair became two
rows, and `the speed figures changed` more than doubled because the row now
measures what the product does rather than a fixture. **The whole table is
re-taken whenever it is quoted, and the first fix pass found three rows a
build stale** — the two constants `C64BENCH_SPAN` and
`C64BENCH_SIG` moved (1530 → 1571 µs, 1030 → 1077 µs) after the rows that
price 25 compare passes had been written down, which is 1.5 ms on every
25-row row. **The two speed rows above are the wave's one recurring redraw**
and they are the reason `hosttest/c64uitest.c` now HOLDS the once-a-second
fold for every other row: a timer landing inside an unrelated cost row prices
the status widget as part of it, and `one joystick indicator changed` came out
at 5.9 ms with four glyph cells in it that had nothing to do with a joystick.

Three decisions fall out of that table and each is a constant in `c64scr.c`:
**a changed cell is 4.2 ms and a changed row 12.5 ms**, which is the whole
reason the composer takes a span and the write window exists (§9.2); **a full
repaint is ~300 ms, five host ticks**, so the `CPU_8086` tier flushes every
OTHER tick (§9.8); and **2× is 20.19 ms for eight rows**, which used to be
read here as *"63 ms for a screen on top of the compose, which is why no tier
magnifies in wave 1"* and is now read as the row above it: 892.0 ms to enter
fullscreen magnified, against 306.4 at 1:1. **The tiers that can pay it
magnify and the `CPU_8086` tier does not** (§9.8), and `c64_band_x2` doubling
the whole 40-byte row whatever the span is — a one-cell change in fullscreen
costs the full 20.19 ms — is why the fact the code tests is `os88_cpu()` and
not a span count.

**Re-taken a third time by wave 3's SECOND review pass**, which moved three
rows and added three:

- **`Edit > Copy` went 79.4 → 83.2 ms**, and the difference is
  `os88_clip_put`'s own byte copy — the one call in that command which is not
  this package's code, and which the model was charging one bridge crossing
  and nothing else. The row is also taken on a FULL screen now, because that
  is what it is called and because the sparse fixture the assertions run on
  puts 71 bytes on the clipboard and hides the term entirely. §7.7 states the
  part that cannot be priced at all.
- **Copy's bridge count went 3 → 4.** `ovl_cmd` reaches `ovl_copy` with
  `call far [cc_ovm_ovl_copy]` (`build/c64.gen.asm`) and the hand-written
  count did not include it. 58 µs of 83 ms — but the term exists so that the
  next `ovl_*` that loops cannot be priced at zero, and the enumeration beside
  each row is how the next author decides what to charge.
- **three status-row rows are new** — a short message going up, coming down,
  and a long one — because wave 3 made the row's erase a correctness question
  and not only a cost one (§10.1).

**And re-taken a FOURTH time by wave 3's FIX pass, which moved the two
clipboard rows further than any pass has moved a row of this table and added
three:**

- **`Edit > Copy` went 83.2 → 28.6 ms and its bridge count 4 → 1**, and
  neither figure is the point: the point is that **none of the 28.6 is held
  lock**. The body left `os88_oncmd` for the top of the next wake (§7.7), so
  the crossings to `ovl_copy` and back are gone with it, and the per-cell
  constant fell 75 → 20 µs because the loop is assembly now. **Edit > Paste
  went 0.9 → 0.7 ms and 4 → 1** for the same reason. The enumeration beside
  each row is what makes a crossing count checkable, and it is what caught
  these two being four.
- **three fullscreen rows are new** — entering at 2×, the wake after it, and a
  `k = 3` shift at 2× — because 2× ships (§9.8) and the table it is written
  from is this one.

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

**2× SHIPS, AND ON EVERY TIER THAT CAN PAY FOR IT.** This section used to say
in capitals that **wave 1 ships the 1:1 centred row on every tier and does not
magnify**, with the scaler *"landing with the wave that can pay for it"* and
every 2× row of the table below marked *later*. Wave 3's fix pass wrote it,
and what made it affordable was not a faster composer but the discovery that
almost none of the program has to know: **the frame shadow stays 320×200 and
every compare, every signature and every span is still in C64 pixels. The
doubling happens at BLIT TIME and nowhere else** — which is why not one
compare, signature or span path needed a second version, and why the second
shadow geometry this section feared does not exist.

**`c64_scw` and `c64_sch` are the GLASS pixels per C64 cell** — 8 or 16 a side
— **decided in ONE place**, `c64_geom` (`c64scr.c`), and every cell↔pixel
conversion in `c64scr.c`, `c64.c` and `c64about.c` divides by them. That is the
whole of the mechanism: a magnified screen is the same program with two
different numbers in it.

**THE TWO AXES ARE DECIDED SEPARATELY, BY "DOUBLE IT IF THE BOX CAN HOLD IT."**
Neither is a tier's preference; each is an arithmetic test against the live
fullscreen content box, which is why the table below has a row per adapter and
not a row per taste.

**ONE ROUTINE SERVES BOTH AXES.** `c64_band_x2` (`c64band.inc`) always emits
**2 × rows of `C64_X2STRIDE`**, each a duplicate of its neighbour — so
**X-ONLY doubling is the same buffer read at TWICE THE STRIDE**: rows 0, 2,
4 … are the eight source rows doubled horizontally and not vertically. No
second routine, no second table, and nothing to keep in step. The doubled band
is **1,280 bytes of bss** (`C64_X2STRIDE × 16`), and it is bss rather than a
claim for the reason §7.7's two staging areas are claims read backwards: **the
flush cannot refuse.** A Copy that cannot get memory says so and the machine is
untouched; a flush that cannot get memory has no such answer.

**AND THE SCROLL STILL SCROLLS AT 2×.** The rect and the `dy` handed to
`OSAPI_GFX_SCROLL` are in GLASS pixels, so a `k`-row shift is `k × c64_sch`
and the rect is `C64_COLS × c64_scw` wide; §9.4's rule that `x1` and `x2 + 1`
must be multiples of 8 holds at either scale, because `c64_gsx` is snapped and
`40 × 16` is 640. The alternative the plan allowed — falling back to bands
whenever the screen is magnified — would have been 25 doubled rows, about half
a second, for what one `gfx_scroll` does (§9.7's `a k = 3 shift at 2×` is
126.6 ms with the scroll).

**AND THE 1:1 BORDER FLOOR IS APPLIED AT 1:1 ONLY.** `if (d < C64_BORDER) d =
C64_BORDER` is right when the picture is 320 wide on a 640-pixel screen and
wrong when it is 640: the margin there is genuinely 0, and forcing 8 slides the
picture right and clips eight pixels off the last column.

**2× HAS NO FONT FALLBACK, AND THAT IS DELIBERATE.** §9.5's font path exists
for a `kern_small` kernel that carries `OSAPI_GFX_BLIT1` without its body;
inventing a doubled one would be a **second renderer** for that case. A refusal
at 2× latches `c64_no2x`, throws the shadow away and **RETURNS from the
flush** — not `break`: the tail of `c64_flush` clears `c64_dirty_any` and sets
`c64_sh_ok`, which is exactly the bookkeeping that must not stand for a screen
that was not drawn — and the next wake lays the screen out again at 1:1, where
the font path is.

**AND A REFUSED `os88_fullscreen` OWES A REPAINT, WHICH THE SUCCESS ARM DOES
NOT.** The success arm is paid for by the kernel (below), so nothing draws
there. On the REFUSED arm nothing repaints us **at all** — and
`c64_fullscreen_toggle` has just taken the About panel down, so what the user
was left with was a hole the size of the panel until something else damaged
it. The refused arm sets `c64_dirty_any` and posts a wake.

**AND THE FIRST 2× SCREENDUMP WAS ENTIRELY BLACK, WHICH IS THE PART OF THIS
SECTION WORTH READING TWICE.** The cause was `c64_x2init` (`c64band.inc`), the
loop that builds the byte → two-byte doubling table `c64_x2tab`: it had been in
the tree for **two waves with nothing calling it** — 2× is its only consumer —
and it answered a table of **256 zeros**. It was wrong in two independent
places, and both are the ordinary shape of a bit-twiddling loop written from
its own description rather than from the machine:

- **`shl bx, 1` answers BIT 15 in `CF`**, and the byte was in the LOW half of
  `BX`. The carry is therefore zero on all eight passes whatever the byte is.
  The byte goes into the high half first now (`mov cx, 8` / `mov bx, si` /
  `shl bx, cl`).
- **The two result shifts must BOTH happen before the pair is set.** Setting
  the pair between them puts it at bits 1–2 instead of 0–1, so after eight
  passes the leftmost pair has walked out of the sixteen-bit result.

**The host harness could not see either, and the reason is worth saying in as
many words: `hosttest/c64uitest.c` MODELS the doubler in C rather than running
the assembly.** That is LESSONS.md 7's trap exactly — *verify the stubs model
what the machine does* — and a transcription that is correct is precisely what
makes the real routine's defect invisible. It was found **on the glass and
nowhere else**.

**So the gate is new, and it runs the shipping text.**
`hosttest/c64memtest.asm` section **(6b)** runs `c64_x2init` and
`c64_band_x2` on a real x86 under `SS ≠ DS`. It checks five hand-computed
entries — `$00 → $0000`, `$FF → $FFFF`, `$80 → $C000`, `$01 → $0003`,
`$55 → $3333` — and then asserts over all 512 bytes that **every byte is a
DOUBLED NIBBLE**, `(b & 0xAA) >> 1 == (b & 0x55)`, an identity that holds for
the sixteen values a doubled nibble can take and for almost nothing a wrong
loop produces. **Both halves are needed and neither is decorative**: the
identity alone passes an all-zero table, and the hand-checked entries alone
would not have caught a pair at the wrong bit position in the middle of the
range. It also asserts that `c64_band_x2`'s second scan line is a COPY of the
first — which is what lets the X-only tier read the same buffer at twice the
stride — and that `rows = 1` writes no third row. **The negative control was
taken**: with the old loop restored the case fails with a `D`, and passes with
the fix.

`build/port-shots/w3fix-16-fullscreen-2x-vga.png` is what it should have been
all along — 640×400 centred, all 25 rows, the status row the full width — and
`w3fix-19-fullscreen-2x-cga.png` is the other tier: 640 wide exactly, 1×
vertical, the widget cluster intact. `w3fix-17-back-from-fullscreen-vga.png`
and `w3fix-20-back-from-fullscreen-cga.png` are the way back, because §11.2.1's
rule is that the key that got you there is the key that leaves.

**THE COST IS WHY THE `CPU_8086` TIER DOES NOT MAGNIFY**, and it is a fact the
code tests (`os88_cpu()`) rather than a guess about speed. `c64_band_x2`
doubles the **whole 40-byte row** whatever the span is, so a one-cell change in
fullscreen costs the full `C64BENCH_X2` = **20.19 ms**; §9.7 prices entering
fullscreen at 2× on VGA at **892.0 ms** against 306.4 at 1:1. That is affordable
on a 286 or a 386 and it is three seconds of nothing on an XT.

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

The scaling is a **tier table in one place** (`c64_geom`, `c64scr.c`), written
**from** `tests/c64band`'s measured milliseconds (§14.5) and from the machine
figures, never from a guess:

| adapter / tier | fullscreen | wave |
|---|---|---|
| VGA 640×480 | **2× on BOTH axes**, 640×400 centred, all 25 rows — the band composed 16 rows deep | **3 — shipping** |
| CGA 640×200 | **2× HORIZONTAL only**, 640 wide exactly — 400 rows do not fit in 200 — and §9.1's standing clamp gives **21 of the 25 rows** above the status row (8 px of border top and bottom and the 10-row status strip leave 174 lines) | **3 — shipping** |
| Hercules 720×348 | 2× horizontal, 640×200 centred, all 25 rows, 1× vertical | **3 — shipping** |
| the `CPU_8086` tier | **1:1 centred**, whatever the adapter can hold | **1 — shipping** |

**THE CGA ROW USED TO PROMISE MORE THAN A 200-LINE SCREEN CAN GIVE.** It read
*"2×, exactly 640×200"*, and 640 wide exactly is true; the height is not.
A 200-line screen carrying **21 cell rows of 8 pixels plus the status row**, with `C64_BORDER` kept above and below (the clamp re-computes `n` so the bottom border survives), is
what §9.1's standing rule produces — *"a clamped window shows fewer C64 rows
and keeps its status row, rather than showing 25 rows it does not have and
losing the row that explains itself"* — and 640×200 of picture would have been
the status row off the bottom, which is the one thing that rule exists to
prevent.

**AND DOUBLING X ALONE ON CGA IS THE WHOLE JOB THERE, NOT HALF OF ONE.** A CGA
pixel is already 2:1, so a 320×200 picture drawn 1:1 is drawn HALF AS WIDE as
it should be. X-only doubling is what makes the picture the **right shape** on
that adapter, and it happens to be the only doubling its 200 lines can hold.

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
fullscreen` (§9.7) are the gate, at both scales.

**AND THE HARNESS GATES THE MAGNIFICATION ITSELF**, because every way of
getting it wrong draws a picture rather than failing: `c64_scw`/`c64_sch` are
read off the computed geometry on a 640×480 box and on a 640×200 one; **every
2 × 2 block of the magnified glass is asserted uniform**, which is exactly what
a blit at the wrong stride or with the wrong row count breaks; the `k = 3`
scroll's `dy` is asserted to be **48 and not 24**; and there are **two negative
controls** — the `CPU_8086` tier must NOT magnify, and a refused `blit1` must
come down to 1:1.

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

**AND `c64_slen` IS BOUNDED NOW, BECAUSE "ZERO DRAWING CALLS" IS NOT THE SAME
AS ZERO WORK.** Its header said it was asked *"once per row REBUILD — not per
flush"*, and its one caller evaluated it at the TOP of `c64_status`, **above
every early return** — i.e. on the path two lines above, the one advertised as
answering that nothing moved. So it ran on every flush for as long as a message
stood, and SmallerC's `while (s[n] != 0) n++;` is ~22 instruction bytes of
SS-relative code, fetch-bound at 4.34 clocks a byte: **~20 µs a character**, so
~0.66 ms a flush for a 33-cell message and **30–60 ms over the message's five
seconds on the target**, all of it re-measuring a string that cannot change.
The whole question the length is asked is `len <= C64_MSGSHORT`, so the scan
**stops at `C64_MSGSHORT + 1`** — the same answer, capped at 26 characters
whatever the message is. Caching the length in `c64_say` was the other option
and was refused: it is a word of bss, and it would have to be right for the two
PERMANENT lines as well (§1.4, §4.5), which `c64_say` never sees — a cache that
is correct for the messages and stale for the two strings that stand for the
session is worse than the scan it replaces. A header that contradicts its
caller is how this survived a review: the cost was written down as once a
message and paid once a flush.

**A MESSAGE OWNS THE ROW'S 40 FIELD CELLS — NOT THE ROW** (amended in the fix
pass). The alternative, measured, is a seven-cell message area; `c64_say`
clamps the text to 40. **This paragraph used to name the longest string the
port shows and it named one that no longer exists** —
`Cannot start the closer - try again.` at 36, which went with the worker
(§15.2) — so it stops naming one. The invariant is held mechanically instead,
and that is what keeps it from going stale a second time:
`hosttest/c64uitest.c` walks every message LITERAL against `C64_MSGCELLS`, and
`apps/c64/build.sh` extracts every `c64_say` literal out of `apps/c64/*.c` to
give it the list. A message this document does not know about is still
measured. **The two LAMPS at cells 40 and 41 are drawn under a message as
well**, because `P` is the one indicator that exists to report the PAUSE
state and `Paused.` is a message: hiding it left the lamp off the glass for
exactly as long as the machine was in the state it reports, and a machine the
user stopped keeps its message until the next event (below). A message expires
after about five seconds.

**AND A SHORT MESSAGE OWNS ONLY THE CELLS IT NEEDS, WHICH IS WAVE 3'S SECOND
REVIEW PASS AND IS A CORRECTNESS FIX BEFORE IT IS A SAVING.** The joystick
widget starts at `C64_ST_JOYX = 200`, i.e. **cell 25**, so a message of 25
cells or fewer never reaches it — and the ERASE has to stop where the message
does, which it did not. The row was filled black end to end and only the
message and the two lamps put back, so `Joysticks:`, BOTH port indicators and
the drive number were blank for the message's five seconds and permanently in
the two non-expiring states. Wave 3 made that self-defeating: `c64kbd.c` now
raises **`ScrollLock for joystick` FROM JOYSTICK USE** (§7.6), so the first
deflection of the stick blanked the two widgets that report the stick.

So the fill is `ox .. ox + 199` for a message of 25 cells or fewer, and the
widgets right of it stay on the glass and keep delta-drawing under it. A
LONGER message — `Pasting the first 2048 bytes.` at 29, and until §1.4
`C64.ROM missing - see README.TXT` at 32, which is where the cap came from —
still owns all 40 field cells, because
there is nowhere else for it to go. The cost, from §9.7: **22.8 ms going up and 26.0 ms
coming down**, against 36.8 + 42.0 for the erase-and-rebuild pair.

**AND `C64_MSGSHORT` IS 25 CELLS, WHICH IS WHY THE WARP MESSAGE WAS
SHORTENED RATHER THAN REWORDED** (§4.4). It read
`Warp mode on - no faster on this CPU.` — **37 cells** — and that is the
message this port shows on `CPU_8086` and nowhere else, i.e. **on the one
machine the whole port is sized for**. So the one message the target tier
raises took the LONG path and erased `Joysticks:`, both port indicators and the
drive number for five seconds: **the precise defect the three-flag model below
exists to stop, sitting in the string the model was written in the same wave
as.** It survived because every screendump offered as evidence was taken on a
386-class QEMU, where the string is the 13-cell `Warp mode on.` instead and
nothing is erased — a tier-conditional message read on the wrong tier. It is
now `Warp mode on - no change.`, **exactly 25**, which takes the short path.

Three flags carry it, and they are three because they are three different
facts: `c64_st_ok` (the row's pixels are ours — cleared by an expose, a damage
rect that reaches this row, and `c64_sh_inval`, and by nothing else),
`c64_st_lok` (the LEFT field cells say what we last put there — which is what
`c64_say` and `c64_jam` clear, and clearing `c64_st_ok` there instead is
exactly what erased the widgets), and `c64_st_blank` (a long message is
standing where the widgets go, so the flush after it comes down has to rebuild
them). `hosttest/c64uitest.c` counts the lit pixels in the widget's own band
before and after, with a 33-cell message as the negative control.

**Wave 3's messages, and what each of them is a fact ABOUT**: `The clipboard
is empty.` and `Pasting the first 2048 bytes.` (§7.7 — an Edit > Paste that
found nothing, and one truncated at the buffer); `The clipboard refused the
screen.` and `The clipboard refused to be read.` (a refused `os88_clip_put` /
`os88_clip_get`, which `kernel/clip.inc:84` leaves EMPTY rather than stale);
`No square voice here - no SID sound.` (§11.4's capability, said once a
session and never retried); `No memory for the copy.` and
`No memory for the paste.` (§7.7 — a refused transient claim, with the machine
untouched); **`Warp mode on - no change.`**, which
is the plain `Warp mode on.` on every other tier (§4.4 — the item is live
everywhere and on an 8088 neither half of warp binds, so saying only
`Warp mode on.` there would be announcing a change the machine does not make,
which is what SPEC.md §47 exists to stop; **both wordings are this port's own,
following `Paused.` / `Running.`, and are NOT VICE's** — VICE reports warp with
a `warp:` LED and no message at all); and
**`ScrollLock for joystick`** (§7.6), which this section used to say the port
did not show — and which is the message that made the erase width a
correctness question, because it is raised BY the joystick. Edit > Copy on
success says
**nothing**, because VICE says nothing and the window it came from has not
changed.

**A PERMANENT ROW STATE IS NOT A MESSAGE.** `Main CPU: JAM at $XXXX` (§4.5)
is a LINE, not a message: it is not a thing that stops being true after five
seconds, and a jam that expired left the ordinary widget row up — `0% cpu
0.0 fps` — which is what an IDLE machine looks like, so the glass could not
tell a dead machine from a stopped one (pause at least inverts `P`). The row's
selector is therefore: a message while one is up, then the jam line, then the
widgets. **There were TWO such lines** — `C64.ROM missing - see README.TXT`
(§1.4) was the other, and it went when the ROM became a part of the package
and stopped being a thing that can go missing.

**AND A PERMANENT LINE DOES NOT ARRIVE AS A MESSAGE**, which is the fix pass's
correction to the sentence that used to stand here (*"the message is only how
it arrives"*). Routed through `c64_say`, `Main CPU: JAM at $XXXX` went up as
`msg == 1`; five seconds later the deadline cleared it, `msg` became 3,
`msg != c64_st_shown` forced the full path, and the row was filled black and
**re-lettered with the same 22 glyphs at the same place** — 1 fill + 1
`font_run` + 22 cells, **~21 ms that change not one pixel**, and on the glass a
row that blanks and re-letters five seconds after the event with nothing having
happened. That is PERFORMANCE.md rule 2's erase-then-letter in the one place it
is free to avoid. `c64_jam` therefore raises the three things a permanent row
state needs — `c64_dirty_any`, `c64_st_lok = 0`, and the toast §9.8's
both-routes rule wants — and never touches `c64_msg`. (`c64_st_lok` and not
`c64_st_ok`: the jam line is 22 cells, so like every short message it leaves
the widgets right of cell 24 alone.) `hosttest/c64uitest.c`
gates it with the second draw as its negative control.

**AND THE DEADLINE IS EXAMINED FIRST THING IN THE FLUSH**, before any branch
can return past it. It used to sit down beside the status row, past an early
return — `c64_flush`'s ROM-less branch, which §1.4 deleted along with the state
it drew — so on a disk with no `C64.ROM` nothing ever cleared `c64_msg`, and
the first menu command a user picked owned the row for the rest of the session
with that permanent line behind it. There is exactly one writer of `c64_msg`
(`c64_say`) and one reader of the clock, and it stays at the top: it belongs
where every flush passes through it, whatever branches get added below it
later.

**BUT A MESSAGE IS NOT A REASON TO ASK FOR ANOTHER WAKE.** A running machine
already asks, so its messages expire on the ordinary flush cadence. **A machine
the user stopped — paused or jammed — keeps its message until the next
event**, and that is a decision rather than an
oversight: `c64_msg[0] != 0` used to be the first term of `c64_wants_wake`, so
a stopped machine re-posted for the whole five-second life of every message
with nothing inside the wake but a re-read of the clock — SPEC.md §74.1's
~1,400 round trips a second at 693 µs each, ~7,000 of them, about **4.8 seconds
of the shared UI task**, which is the very rule the function exists to enforce,
inverted. Wave 2 made it newly reachable at the worst moment: Alt+P now
genuinely stops the machine and answers `Paused.`, and on a ROM-less disk
*every* menu command ends in a `c64_say`. It cannot be gated on the flush's
tick boundary instead — that answers 0 at the moment the boundary has not
arrived, no wake is posted and the message never comes down at all — and the
alternative, a worker sleeping a tick to post 18 wakes a second, buys five
seconds of expiry for a background task and a spawn site — and it costs more
than it did when this was written, because the port's other worker is gone
(§15.2) and this would be the only task it has. The next event is a keystroke, a click, a menu pick or an
expose, and every one of them flushes: the widgets and §1.4's permanent line
come back on the next thing the user does. A flush with nothing dirty composes
no row and `c64_status` answers "nothing moved" in **zero drawing calls**
(§9.7's `a wake with no tick boundary`).

### 10.2 The speed widget — VICE's own strings, and what they count

`statusbarspeedwidget.c` prints `%7.0f%% cpu` (`:572`, `CPU_DECIMAL_PLACES`
0) and `%8.1f fps` (`:653`, `FPS_DECIMAL_PLACES` 1). **Both are folded onto
this one row**, LEFTMOST as VICE appends them (§10.1), e.g.
`   100% cpu    50.1 fps`.

**THE TWO LITERAL TAILS ARE DRAWN ONCE, AND THE NUMBERS DELTA AGAINST THE
GLASS.** `% cpu` and `fps` change on nearly every fold on a machine whose
speed fluctuates, and drawing both fields whole was 24 glyph cells and 2 call
floors — ~23 ms EVERY SECOND, ten times what a keystroke costs (§9.7) — of
which nine cells were the two constants and most of the rest were leading
blanks re-inked. So `% cpu` and ` fps` are drawn only with the row itself, and
each numeric field is compared against what was last drawn and re-run over the
span between the first and last differing cell (`c64_st_field`, the idea
`c64_rowspan` already uses on a band). A typical second is one or two cells
**per field**, and it is **measured, not claimed**: §9.7's `the speed figures
changed` row is **3.7 ms — TWO `font_run` calls, one per changed number, no
fill** — with an assertion that each landed inside its OWN number and not on
either literal tail, and `the same row with the delta switched off` beside it
as the negative control at **42.0 ms**, which is what the whole-row path
costs. **Two, not one, and the row used to say one**: both figures come from
ONE fold of one clock, so a second that moves `% cpu` moves `fps` as well —
the old row advanced the cycle counter and left the frame counter at zero,
which is a fixture this program cannot produce. The two runs cannot coalesce
either: the fixed `% cpu` tail sits between the fields at x = 56..95. This is the
first thing in this program that draws on a TIMER rather than on an event, so
it is the row that has to be gated.

**AND THE WIDGET'S OWN DELTA IS PART OF THE FLUSH GATE.** The flush ran only
while the C64 screen was dirty, and `c64_dirty_any` comes from a RAM WRITE:
a machine that runs without writing RAM — an ML poll loop on `$D012`, a
`WAIT` — froze the figures at whatever they were when it last wrote, which is
precisely the moment somebody is looking at them. `c64_pct`/`c64_fps10`
differing from what is on the glass is a reason to flush; it costs nothing
when nothing moved, because `c64_status` answers in zero drawing calls.

**THE CLAMP IS BEFORE THE CAST, ON BOTH FIELDS.** `c64_div32` answers up to
`$FFFF` and `unsigned` is 16 bits: a quotient of 32,768 or more was already
NEGATIVE by the time a `raw > 30000` test saw it, sailed past the clamp, and
came back as a `c64_pct` that prints a flat `      0% cpu` beside a live `fps`
— the pair of numbers that could not both be true, one cast earlier than where
the wave first fixed it. **`fps` had exactly the same defect one field to the
right**, and the fix pass found it: `c64_muldiv` answers `$FFFF` on overflow
(`c64mem.inc:266`), the cast makes that −1, and `c64_st_num` prints
`     0.0 fps` beside a live `% cpu`. Both clamps are applied while the value
is still unsigned, to the same 30,000. The harness row drives one scenario
through **both** fields — 183,500,800 emulated cycles and the 9,335 VIC frames
that speed implies — and asserts both read their cap; zeroing the frame
counter is how the first version of that row walked past its own defect.

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

**The arithmetic, exactly, because a percentage with no float in it is where
a plausible wrong number comes from.** 985,248 cycles a second over 18.2 host
ticks is 5,413.45 cycles per hundredth of a tick, so

    raw = c64_div32(cycles_hi, cycles_lo, 5413)      one `div`
    % cpu     = c64_muldiv(raw, 10, elapsed_ticks)   one `mul`, one `div`
    fps × 10  = c64_muldiv(frames, 182, elapsed_ticks)

`c64_div32` and `c64_muldiv` are four lines each in `c64mem.inc` — a 16-bit C
cannot express either and the 8086 does both in one instruction. Both answer
`0xFFFF` on overflow or a zero divisor rather than raising `#DE`: a status row
is not worth a trap, and the caller clamps. The window is one second
(`elapsed ≥ 18`) and is RESTARTED rather than published if the wakes stopped
for ten (`elapsed > 182`), because that window measures nothing.

**Both figures are quantised by the window**, and the fps one visibly so: it
counts whole emulated frames, so at 100 % over eighteen ticks it reads 49.5
or 50.6 and only settles on 50.1 over a longer one. That is the honest
resolution of "frames per second" measured in frames.

**AND THE TWO CLAMPS SATURATE AT DIFFERENT EMULATED SPEEDS, WHICH IS STATED
RATHER THAN HIDDEN.** They are the same constant in different units: `raw` is
capped at 30,000, which at `el = 18` caps `% cpu` at 30000 × 10 / 18 ≈
**16,666 %**; `c64_fps10` is capped at the same 30,000, which is **3,000.0
fps** — 5,985 % on a PAL machine. Between roughly 5,985 % and 16,666 % the fps
field therefore sits at its cap while `% cpu` goes on climbing. The sentence
below used to say the only cap needed was the one that keeps a 16-bit `int`
positive, and that is true of `raw` and false of `fps10`. **It cannot be
resolved by clamping both at the same emulated speed**: the fps field is in
TENTHS, so 16,666 % is 83,497 tenths and does not fit a 16-bit `int` at all —
one of the two fields has to saturate first, and the honest thing is to say
which and where. The measured host runs at ~2,900 %, so a host about twice as
fast reaches the fps cap.

**THE FRAME COUNTER IS ONE WORD, and that is deliberate.** `fps` is the
DIFFERENCE over a one-second window, taken masked to 16 bits, so it is exact
for any window under 65,536 frames — twenty-two minutes. A high word was kept
beside it and read by nobody; the fix pass dropped it.

**The clamp on `raw` is 30,000 and not a small number.** Wave 2's first draft
capped it at 3,200 — "320 % of a real 6510, and nothing this port runs on
will see it" — and under QEMU the core runs at some **thousands** of per
cent, so the cap clipped an honest figure into a wrong one that still looked
like a number (`1777% cpu` beside `1195.1 fps`, two figures that could not
both be true). The only cap that is needed is the one that keeps a 16-bit
`int` positive.

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
screen: `(vid_h − 22) / 16` is 11 on a 200-line CGA and clamped to 11 above
it, which is why `[vid_popmax]` is now that constant rather than that
arithmetic (SPEC.md §39.2). Three rules follow:

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
   Preferences > Advance frame. All three were greyed with their fact; wave 2
   made Advance frame LIVE, because the fact that greyed it — no raster
   accumulator — stopped being true when the alarm model landed. **A greying
   is retired the moment its fact is,** and that is the same rule read the
   other way round.
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
| File > Reset > Reset machine CPU | Alt+F9 | **and it CLEARS THE PAUSE, which is VICE's own order**: `machine_reset_action` calls `ui_pause_disable()` straight after `machine_trigger_reset()` (`src/arch/gtk3/actions-machine.c:121`). Without it a Reset on a paused machine reset the 6510 and then ran nothing — no boot screen, Preferences > Pause emulation still checked, §10.2's `P` lamp still standing, and the user's only clue that anything happened was that the screen did not change. `c64_adv` is cleared with it and `c64_menu_state` re-runs, so the two checks on the menu tell the truth about the machine that is now running |
| File > Reset > Power cycle machine | Alt+F12 | caption kept; the item is the route (§7.5). **RAM pattern fill: VICE's C64 factory pattern** (`src/ram.c:169-177` — `RAMInitStartValue` 0, `RAMInitValueInvert` 4, `RAMInitValueOffset` 2, `RAMInitPatternInvert` 16384, `RAMInitPatternInvertValue` 255, put through `ram_init_with_pattern` at `:257`), which is the eight-byte period `00 00 FF FF FF FF 00 00` with every other 16K block inverted — **not zeros**, which is what wave 1 first wrote here and at power-on. `Reset machine CPU` still does not touch RAM, which is the whole difference between the two items — and it clears the pause the same way, above. **AND THE FILL NO LONGER RUNS IN THE COMMAND**: `c64_ram_pattern` is 65,536 bytes through `c64_zfill`, about a quarter of a second on the target, and `os88_oncmd` is dispatched under the DESKTOP's gfx lock — a quarter-second of every task's drawing stopped for an item that draws nothing. The whole reset body is a latch now (`c64_reset_req`, 1 = CPU reset, 2 = power cycle) spent by `c64_reset_service()` at the top of the next wake, which is §7.7's argument for Copy and Paste applied to the third command that had a body worth measuring |
| File > Exit emulator | Alt+Q | **`os88_wm_close`, the kernel's own close path — NOT the worker self-close idiom, which closed the window and left the app** (§15.2). Spent from the WAKE and not from `os88_oncmd`: `c64_exit_req` is set by the command, and the top of `os88_onwake` sets `C64_ST_DEAD` and calls it as the last thing that happens |
| Edit > Copy | Alt+Delete | **LIVE as of wave 3, and GREYED WITH NO `C64.ROM`** (§7.7): the 40×25 screen through VICE's own screen-code → PETSCII → ASCII chain, to the system clipboard, silently — because VICE says nothing on a Copy. With no machine the matrix holds the factory RAM pattern and the clipboard is kernel-owned and outlives this app (SPEC.md §55.3), so the item would destroy what the user had copied elsewhere; on a JAM it stays live, because the frozen screen is real. **It holds the gfx lock for NOTHING**: the command sets a latch and the body runs from the top of the next wake, 28.6 ms with no lock held (§7.7, §9.7) — this row used to say 79 ms of held lock, and that was a fact about the desktop and not about Copy. It was greyed with the fact that greyed it (the tables were not written); wave 3 wrote them, so the greying's reason stopped being true and SPEC.md §47 does not let a greying outlive its reason. The caption is kept and the MENU ITEM is the guaranteed route (§7.5); where a BIOS passes the chord, `os88_onkey` dispatches scan **`0xA3`** to this item — a code no unmodified key produces, so it cannot be confused with the C64's own Del (`0x53`) |
| Edit > Paste | Alt+Insert | **LIVE as of wave 3, and GREYED WITH NO `C64.ROM`, ON A JAM AND WHILE PAUSED** (§7.7): the clipboard queued, then converted to PETSCII and typed into the KERNAL's own buffer at `$0277` ten characters at a time and only while `$C6` is zero — the machine's pace, not ours, and the conversion is the feeder's so the command holds no lock at all and the wake that services it is 0.7 ms whatever the clipboard holds. In any of the three greyed states nothing drains the queue, so a live item would be a silent no-op (SPEC.md §47) — the row used to say *"either"*, before pause was counted. Same greying history, same caption rule; the chord is scan **`0xA2`**, against the C64's own Ins (`0x52`) |
| Preferences > Fullscreen | Alt+D | §9.8. CHECK: rule 4's `*` |
| Preferences > Warp mode | Alt+W | **the wall slice's cap lifted from 16,384 to 30,000 cycles AND the flush capped at VICE's own 10 fps** (`max(c64_flush_every, 2)` ticks, `src/vsync.c:339-340`, `:634-656`) — §4.4 carries the whole of it, including what it is measured to be worth (**about 7 %** under QEMU, the two halves together — the row said about 5 %, which was the cap-only build measured before the render cap was written) and where it is worth nothing (the target, where NEITHER half binds: the slice cap is never reached and that tier already flushes slower than 10 fps). **On `CPU_8086` the message says so** — `Warp mode on - no change.`, 25 cells so that it takes §10.1's short-erase path — rather than announcing a change the machine does not make (§4.4, §10.1). CHECK: rule 4's `*`, and §10.2's `W` lamp |
| Preferences > Pause emulation | Alt+P | CHECK: rule 4's `*`, and §10.2's `P` lamp |
| Preferences > Advance frame | Alt+Shift+P | **LIVE as of wave 2, and it is VICE's action and not this port's idea of one.** `src/arch/gtk3/actions-speed.c:72-80` (identically `src/arch/gtk3/ui.c:2735-2743`) is the whole body — `if (ui_pause_active()) { vsyncarch_advance_frame(); } else { ui_pause_enable(); }` — with `vsyncarch_advance_frame` (`src/arch/gtk3/vsyncarch.c:56-60`) being `ui_pause_disable(); pause_pending = 1;` and `vsyncarch_postsync` re-pausing at the next frame end. So **from a RUNNING machine the item only PAUSES and advances nothing**; only from an already-paused machine does it run one frame (to the next VIC frame end, §6.3) and stop. The fix pass found the first version setting the request unconditionally, which ran 19,656 emulated cycles from a running machine and then paused — invented semantics for the one live item this wave added. It was greyed before with the fact *"there is no raster accumulator until the alarm model lands"*, and wave 2 landed the alarm model — `c64_frame_cyc` IS that accumulator and `C64_PAL_FRAME` is the frame end — so the fact stopped being true and SPEC.md §47 does not let a greying outlive its reason. **The command body raises a request and runs nothing:** `os88_oncmd` is dispatched under the gfx lock and a PAL frame is 19,656 emulated cycles, which on the target is a fraction of a second of stopped desktop; the slice driver serves it in the wall slices it already sizes, and re-posts while it is outstanding. **AND IT IS GREYED AGAIN WHENEVER THERE IS NO MACHINE TO ADVANCE** — a disk with no `C64.ROM` (`C64_ST_HALT`) or a JAMMED CPU — because `c64_advance_frame` answers those states by doing nothing at all, and a live black item that is a silent no-op is the one shape SPEC.md §47 forbids; the fact that greys it is already the permanent line on the status row (§1.4, §4.5). `c64_menu_state` swaps the two spellings, the way rule 4 swaps the check items. **The chord reads the shift level**: the BIOS hands Alt+Shift+P the same ascii/scan pair as Alt+P, so `os88_onkey` asks `os88_key_down(KSC_LSHIFT/KSC_RSHIFT)` and dispatches Advance frame or Pause emulation accordingly — without that the chord this row advertises RESUMED a paused machine, which is the opposite of VICE in the only state VICE advances from |
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
| Preferences > Settings... (Alt+O), Load/Save settings, Restore default settings | no resources file in this build: every setting this port has is on the Preferences menu itself |
| Help > Browse manual, Command line options, Compile time features, Hotkeys | no manual on this floppy and no command line in this OS; the hotkeys are the menu captions |
| Machine model other than C64 PAL (C64C, NTSC variants, Drean, SX, Japanese, GS, PET64, MAX) | one ROM set and one timing are carried: PAL 985248 Hz, 312 lines, 19,656 cycles a frame (`c64.h`) |
| SID voices 2–3, waveforms other than the gate, ADSR, filters, `$D41B`/`$D41C` | the PC speaker is one square wave, and the sound driver's FM path is not wrapped for C (§11.4) |
| Colour on the glass, VGA included | §9.6 — SPEC.md §5.4.1's span writer, ~215 µs a colour run, ~1,000 runs a text band |
| Sprite–sprite and sprite–background collision registers | the composer draws cells, not pixels: `$D01E`/`$D01F` answer 0 (§5.3) |
| Cycle-exact raster effects (fine scroll, mid-line colour changes, bad-line timing, FLD/FLI) | the VIC is serviced at raster-LINE granularity (§5.2): `$D011`/`$D016` scroll is honoured as whole cells and a register written part-way along a line takes effect from that line |
| Status bar: the `Tape:` field, drive 8's LED and track field | no tape, no drive (above) — drawn with SPEC.md §47's pen |
| Power cycle (Alt+F12), Paste (Alt+Insert), Copy (Alt+Delete) **as chords** | §7.5 — the caption is VICE's and the menu item is the route |

**AND THREE ITEMS ARE GREYED BY STATE RATHER THAN BY THE BUILD**, which is the
same rule reached from the other side: the fact that greys them is on the
status row and it can stop being true, so `c64_menu_state()` re-runs on every
path that changes the state.

| item | greyed while | and the fact is |
|---|---|---|
| Preferences > Advance frame | `C64_ST_JAM` | §4.5's permanent line. It was `c64_norom` OR a jam until §1.4 made the ROM a part; the missing-ROM half went with the state, because a greying may not outlive its reason (SPEC.md §47) |
| Edit > Paste | `C64_ST_JAM` **or `c64_pause`** | for the first, §4.5's permanent line — and in **any** of the three, nothing would ever drain the queue (§7.7), which is why the sentence here used to read *"in either greyed state"* and now reads any. **The PAUSED state's fact is not a message**: `Paused.` is a `c64_say` and expires after ~5 s, so what stands for as long as the pause does is §10.2's **`P` lamp** on the status row and the check beside Preferences > Pause emulation. **This is a deliberate departure from VICE**, which queues a paste on a paused machine and delivers it on resume: there the queue is drained by the vsync handler, which runs whatever the machine is doing, and here `c64_paste_feed` is called only from the RUNNING arm of `os88_onwake` — so nothing would ever drink it |
| Edit > Copy | *nothing* | it was greyed by `c64_norom` alone, and with the ROM a part (§1.4) there is no machine that never started — so the one fact that greyed it is gone and the item is always live. It is **not** greyed by a pause or a jam either: the frozen screen is real, and the body runs from the wake whatever the machine's state is |

**The chords are guarded with them.** `os88_onkey` dispatches Alt+Shift+P,
Alt+Delete and Alt+Insert itself (§7.5), and the kernel's *"a disabled item is
never dispatched"* does not reach a chord the package delivers: each tests the
item's own first byte for `OS88_MENU_DIS` before calling `os88_oncmd`.

### 11.3 Program loading

**File > Smart attach... (Alt+A)** opens the Standard File dialog on `.PRG`.

**AND IT READS THE DIALOG'S ANSWER, WHICH IT USED TO DISCARD.**
`os88_file_dlg` answers **−1** when another modal dialog already owns the
screen — one at a time, machine-wide — and the return was thrown away, so
picking Smart attach while a dialog was up **did nothing and said nothing**:
the silent no-op SPEC.md §47 forbids, in the one item on File that is live.
It now says `A file dialog is already open.` on the row, which is a fact about
the machine and not about this app.

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

**`LOAD"*",8` answers with the KERNAL's `?DEVICE NOT PRESENT  ERROR`.** That
is the honest machine with no drive, and it is written here so nobody files it
as a defect. There is no string to transcribe in the VICE tree — the ROM is
the authority — so wave 2 ran it and transcribed what the machine actually
prints, here and in `README.TXT` (`build/port-shots/wave2fix-11-load.png`):

```
LOAD"*",8

SEARCHING FOR *
?DEVICE NOT PRESENT  ERROR
READY.
```

**Two spaces before `ERROR`**, which is the KERNAL's own spacing and the one
detail a remembered quote gets wrong.

**AND GETTING THERE NEEDED CIA2 PRA MODELLED, NOT STORED.** Typing that LOAD
was the first time this port ever did, and the machine sat at `SEARCHING FOR
*` for ever: `$DD00` answered the raw register file, so DATA IN read LOW — a
device answering — and the KERNAL waited for it exactly as it is meant to.
The serial bus with nothing on it reads back what the machine itself drives,
INVERTED: VICE hands the CPU `(iec_fast_1541 & 0x30) << 2`
(`iecbus/iecbus.c:212-217`, the no-true-drive configuration) and
`store_ciapa` put `~(PRA | ~DDRA)` there (`c64cia2.c:150-162`,
`core/ciacore.c:805`), so CLK IN and DATA IN are the inverse of CLK OUT and
DATA OUT (§6.2). Released DATA then reads high, the KERNAL times out, and the
line above is what it prints.

### 11.4 Sound

**SID voice 1's frequency and gate** go to `os88_snd_tone`
(`OSAPI_SND_TONE`, slot `0x00E8`) once per slice, and only when they changed
— one far call.

**AND THE HERTZ IS ONE ROUNDING, NOT TWO** (amended in the fix pass). A 6581 at
the PAL dot clock sounds `Fn × 985248 / 2^24` Hz — **0.0587257 Hz a step**.
This port computed it as `(raw >> 4) × 15 / 16`, which is the same factor to a
quarter of a percent and is **wrong for a different reason: it rounds TWICE**,
once into a 12-bit number and again on the way out. Near the bottom of the
range that is a whole hertz where a hertz is the unit — `F = 341` answered
**19** and the true figure is **20**, which is the difference between the 20 Hz
floor REFUSING the note and playing it. It is `c64_muldiv(f, 3848, 0xFFFF)`
now: the product is kept in 32 bits, which a 16-bit C cannot express and
`c64mem.inc` can, and 3848/65535 is 0.0587166 — within **0.015 %** of the real
constant across the whole range, `$FFFF` answering 3848 Hz where the ratio says
3848.6. (The divisor is spelled `0xFFFF` because SmallerC's `int` is 16-bit
SIGNED and a decimal 65535 is *"Constant too big for 16-bit signed type"*.)
The harness asserts both ends of the range and the floor.

**AND A HELD NOTE DOES NOT SURVIVE A STOP.** The tone is played with
**duration 0**, which SPEC.md §34 holds until something takes it down — and
the only thing that ever did was the guest closing the gate. So Alt+P, a JAM, a
reset and the About panel (which pauses the machine and owns the glass) each
left the last note sounding **for ever**, on a desktop the user had gone back
to. `c64_sound_stop()` is the one place it comes down: it silences the speaker
and raises `c64_sid_dirty`, so the wake re-reads the CURRENT SID registers on
the way out and plays **what the machine actually holds** rather than
remembering the note it took away — remembering it would be a second model of
the SID, kept in step with the first by hand. It is called on entering WARP as
well, which is what VICE does: `vsync.c:181`'s warp arm calls `sound_suspend`
(`sound.c:1819`), because a machine at 3,000 % has nothing meaningful to play.

**AND THE CAPABILITY IS ESTABLISHED BEFORE THE SLOT IS CALLED**, once, in
`os88_main`: `os88_snd_caps() & SND_CAP_TONE` (`OSAPI_SND_CAPS`, slot
`0x00E0`). A capability is a fact to test rather than a guess (SPEC.md
§73.11), and this one is worth being honest about in both directions: **every
machine this OS boots answers yes** — `kernel/snd.inc:804` ORs `SND_CAP_TONE`
in unconditionally — so the guard is not a path a user will meet. It is
written anyway, and it is *reachable in the harness*, whose stub can clear the
bit: a machine with no square voice is never asked for a tone at all, the SID
latch is dropped rather than left raised to be re-asked every wake, and the
fact is said once a session — `No square voice here - no SID sound.` That is a
different sentence from the busy-speaker one below, because it is a different
fact and it is not retried.

**AND A REFUSED GRANT IS NOT PERMANENT** (amended in the fix pass).
`os88_snd_tone` answers **-1 refused** when another instance holds the
speaker, which SPEC.md §34.3 allows; all three returns were discarded and the
latch was cleared BEFORE the call. The latch is raised in exactly one place —
a write to `$D400-$D41C` — so a single refusal at the wrong moment (a toast
tone, another app's grant) silenced the emulated SID until the guest next
poked a SID register, which for the ordinary *set the frequency, gate on,
leave it* is never: the machine then played nothing for the rest of the
session and said nothing about it, which is the shape SPEC.md §47 forbids. So
the latch is cleared only when the grant takes; the retry is **bounded at
eight wakes** and then dropped, and the next SID register write re-arms it —
a machine whose speaker is held for good costs eight far calls, not one a
wake for ever. The give-up is said ONCE a session, on the status row and in a
toast: `The speaker is busy - no SID sound.`
`hosttest/c64uitest.c`'s stub can refuse, which is what makes any of this
checkable, and the row's negative control is the shipped behaviour: shown
staying silent. Voices 2 and 3, every waveform beyond the gate, ADSR and the
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

### 13.0 The measured line — wave 2 (2026-08-22)

The whole 6510 is in: 256 opcodes with the illegal ones, decimal ADC and SBC,
the seven bank maps, the boundary-guarded fetch, the alarm scheduler, both
CIAs with their timers, TOD and ICR, the level keyboard, and BASIC booting to
`READY.` off the real ROMs.

| | measured | of |
|---|---|---|
| resident image | **36,434** | |
| bss | **11,346** | |
| **resident total** | **47,780** | **61,440** — 13,660 spare, **7,220 under §73.9's 55,000 trigger** |
| `C64.OVL` | **1,424** | on demand |
| resident overlay shims | **24** | counted inside the image above |
| largest C frame | **32 bytes** | of the 96 SPEC.md §73 allows |

(The wave's THREE fix passes moved it from 33,306 + 11,270 = 44,576, and each
carries its own figures rather than the total.

**The first: +988 image, +62 bss, +18 overlay, one more shim** — the unsigned
timer and alarm rules, `c64_wants_wake`, the JAM line as a permanent row
state, the keyboard's freshness stamp in emulated cycles, the status row's
per-field delta, Advance frame going live, and CIA2 PRA read back rather than
stored.

**The second: +188 image, and nothing else moved** — the message deadline
hoisted to the top of the flush, the `fps` clamp beside the `% cpu` one, the
JAM line off the message route, the message term out of `c64_wants_wake`,
VICE's real Advance-frame action with its greying and its shift-read chord.

**The third — this one: +1,952 image, +14 bss, +4 overlay, no new shim.** It
is the largest of the three and all of it is fidelity: the CLI/SEI/PLP I-flag
timing, BRK's cycles, the RMW dummy write, decimal ARR, the four unstable
stores' real masks and address corruption, the CIA timers' independent
advance, the TOD's alarm/latch/stop/12-hour rollover, the asserted-interrupt
latch, the raster status ungated, `$01`'s read-back, the keyset's consumption
and the bare modifiers, `c64_dirty_take`, and the SID's bounded retry. Two
things came OUT in the same pass — the harness-only cost counters, which are
`#ifdef C64_HOST` now, and the frame counter's unread high word — for −128
image and −16 bss of it.)

**The plan's estimate was 39,000 + 13,600 + 6,000 and the machine is
smaller than that on all three.** The two that moved most are worth naming.
`c64cpu.inc` was budgeted at `rcz80.inc`'s measured ~9.6KB and assembles at
**6,518 bytes with `c64mem.inc` beside it** — a 6510 handler really is
smaller than a Z80 one once every addressing mode is a `call c64_ea_*` and
every memory access a `call c64_rd_bx`, which is the trade this core makes
deliberately (image against a near call per access, on a machine where
correctness was the stated posture). And `C64.OVL` is **1,424** rather than
6,000 — **not because the split has subsystems still to receive**, which is
what this paragraph used to say: `ovl_conv_init`, `ovl_cmd`, `ovl_copy`,
`ovl_paste`, `ovl_load_prg` and `ovl_about_show` are already the only six
functions in `.modc` (`build/c64.gen.asm`) — and `ovl_conv_init` is not
decorative, it is §13.3's first-wake probe **and** the two PETSCII conversion
tables, which are the port's own once-per-launch code (§7.7). What is thin is their BODIES, because waves 3 and 4
have not written the features they carry — Copy/Paste, the disk work, the
panel's grown contents. The figure grows **inside the module** as they are
written, and the resident image does not, which is the whole point of having
put them there before they were big.

### 13.0.1 The measured line — wave 3 (2026-08-22)

Edit > Copy and Edit > Paste with VICE's own conversions and the KERNAL's own
pacing, warp as the wall slice's cap **and VICE's 10 fps render cap**, the
sound capability established before the slot is called, §7.6's ScrollLock
hint on an observable the SDK can actually be asked for — and, after the fix
pass, both clipboard commands out of the desktop's lock (§7.7), the per-cell
loop in assembly (§9.7) and **fullscreen at 2×** (§9.8).

**The line, from a clean rebuild:**

```
cc8086: build/c64.raw.asm: 92 function(s), 34 frame byte(s) max, lowered 846 site(s)
cc8086: overlay - 4 moved to .modc, 4 entry vectors, 20 resident shims, 5 loading call sites
os88ovl: build/c64.bin -> build/c64.trim.bin (39384 resident) + build/C64.OVL (2149 on demand)
os88pkg: 'C64' entry=+0x0060 image=39384 bss=13106 icon=yes assoc=0
```

| | measured | of |
|---|---|---|
| resident image | **39,384** | +2,950 on wave 2, **+1,106** on this wave's pre-fix-pass build |
| bss | **13,106** | +1,760 on wave 2, **−1,756** on the pre-fix-pass build — the two clipboard staging buffers left it for heap claims (−3,074, §7.7) and fullscreen's doubled band arrived in it (+1,280, §9.8) |
| **resident total** | **52,490** | **61,440** — 8,950 spare, and **2,510 UNDER §73.9's 55,000 trigger. THIS IS THE MARGIN; every other mention of it in this document points here** |
| `C64.OVL` | **2,149** | on demand; −226, because Copy's and Paste's bodies came back resident with the lock fix |
| resident overlay shims | **20** | −10, same reason: 4 functions in `.modc`, 4 entry vectors, 5 loading call sites |
| largest C frame | **34 bytes** | +2, and still comfortably inside the 96 SPEC.md §73 allows |

**THE MOVEMENT IS NOT ONE THING, AND IT IS WORTH SAYING WHICH.** The
pre-fix-pass build of this wave measured **38,278 + 14,862 = 53,140, 1,860
under the trigger**, and that is the figure this section carried. bss FELL by
**1,756 net** — 3,074 of clipboard staging out to transient claims, 1,280 of
doubled band in — while the resident image ROSE by **1,106**, because Edit >
Copy's and Edit > Paste's bodies moved OUT of `C64.OVL` and back into the
resident half (they run from the wake now, §7.7), the assembly row composer
arrived, and the whole 2× path is resident code. The overlay shrank with them.
**The net is 650 bytes recovered, and, more to the point, 2,510 of headroom for
wave 4** — the bitmap modes, multicolour, the sprites, the PRG loader — where
there were 1,860.

**What the second review pass did to those figures.** It moved the two PETSCII
conversions out of the resident image (`ovl_conv_init`, §7.7) — **655 bytes
back**, measured off a `nasm -l` listing — and spent about 560 of them on the
status row's three-flag erase model (§10.1), the warp render cap (§4.4), the
`c64_conv_ok` guard and Paste's third greying. Net **−94 image, +6 bss**, and
`C64.OVL` +658. That is an honest accounting rather than a headline: the
frequency split recovered real bytes and a correctness fix in the row spent
most of them, and **wave 4's first job is still a split**, because the VIC
work it carries is per-FRAME and therefore resident by nature.

**These are the figures after the wave's review AND its fix pass**, and both
are why they moved. The first build put the conversions and both loops in `C64.OVL`
(2,549 bytes) and reported 50,614 resident — a smaller number bought by
putting a **per-byte loop across the segment boundary**, 2,000 far calls for
one Edit > Copy (§7.7, §9.7). The split is by FREQUENCY and not by size
(SPEC.md §73.14): the loops came back resident, the shells stayed out, and
`C64.OVL` is smaller than it was because what is out there is now only the
part that runs once per pick.

**bss WAS the wave's real cost, and the fix pass spent the saving it named.**
The list this paragraph carried was `c64_clip` 1,026 + `c64_pastebuf` 2,048 +
`c64_sctab` 128 + `c64_pettab` 256 + `c64_scrow` 40, with LESSONS.md 5's
*"bss is the cheap half"* to make it affordable and the observation that **the
paste queue is the one figure in the list that could be given back**. Both
clipboard buffers were given back, and not only the queue: each is a transient
2KB heap claim now (§7.7), which is CWORD's lesson — *the next byte to save is
a buffer moving to a claim* — applied to 3,074 bytes held for the life of the
app so that two menu commands nobody may ever pick would have somewhere to put
their bytes. What is left is the two conversion tables, the matrix row and
fullscreen's doubled band, and the band is bss on purpose because a flush
cannot refuse (§9.8). The margin is the table above. Wave 4's VIC work is
per-FRAME and therefore resident by nature, so the split it will need is a real
one, and that table is the number to watch.

### 13.1 The file split

| file | holds | resident |
|---|---|---|
| `apps/c64/c64.c` | the translation unit's root: the GPL-2 + VICE header, prototypes, the key ring, `os88_main` (window, the five-menu set, `about_set`, `onwake` install, the RAM and ROM claims + `read_seg`, **`os88_key_down` armed here**, §7.2), `os88_paint`, `os88_onkey`, `os88_onclick`, **`os88_onfile`** (§11.3), **`os88_onwake` — the slice driver** (§4.4), the three latches the wake spends before the slice — `c64_exit_req` (File > Exit, through `os88_wm_close`, §15.2), `c64_reset_req` (§11.1) and the clipboard pair (§7.7) — the `#include`s in order. **There is no `os88_worker`**: the self-close idiom went with wave 3's fix pass | yes |
| `apps/c64/c64io.c` | the `$D000-$DFFF` register files and the cdecl dispatch the core calls (§3.4); **the alarm scheduler `_c64_alarm` and "cycles to the next event"** (§4.4); VIC, SID, colour RAM, CIA1/CIA2, the IRQ and NMI lines, the `$00`/`$01` port and the bank-map index (§3.2) | yes |
| `apps/c64/c64kbd.c` | the 152-entry `gtk3_sym.vkm` table (`.data`), the cached matrix and the 16-entry down-list, the once-per-wake rebuild, the scan-routed Ctrl+H/I/M, the Ctrl-held digit poll, RESTORE with the Esc read, the joystick keyset, **both of Copy's and Paste's per-byte loops** (§7.7) and the two conversion TABLES they index (filled by `ovl_conv_init`, which is not here), **`c64_clip_service` — the whole of both commands' bodies, run from the top of the wake with no lock held** — and the `$0277` feeder. **The two clipboard staging buffers are not here any more**: each is a transient heap claim (§7.7), and the row composer they fill is `c64mem.inc`'s | yes |
| `apps/c64/c64scr.c` | the dirty-page → cell-row mapping, the 1bpp frame shadow, the flush, the `k`-row shift test, the tier table, the EGA-16 luminance table, the border fills, the status row, the dirty-pages-per-wake counter | yes |
| `apps/c64/c64menu.c` | the five menu tables with every string and caption, the `OS88_MENU_DIS` greying with its fact in a comment beside it, the menu-set struct, the `oncmd` dispatcher (two compares, then an `ovl_`) | yes |
| `apps/c64/c64cmd.c` | `ovl_*`: §13.3's first-wake probe **and the two PETSCII conversions it runs once a launch** (`ovl_conv_init`), then every menu command SHELL — warp/pause/advance-frame, swap joysticks, the greyed items' refusal toasts. **The per-byte loops are not here** (§7.7): a loop written in this file crosses the segment boundary every iteration. **Nor are the four commands with a body worth measuring**: Copy, Paste, reset and power cycle set a latch here and the WAKE spends it, out of the desktop's lock (§7.7, §11.1). **File > Exit emulator is answered in the RESIDENT half** (`os88_oncmd`) and not here at all, because it is the one command that must work on a disk whose `C64.OVL` is missing | **no** |
| `apps/c64/c64load.c` | `ovl_*`: the Smart-attach body and the autostart state machine's setup, called by the resident `os88_onfile` (§11.3) | **no** |
| `apps/c64/c64about.c` | `ovl_about_show` (§12) | **no** |
| `apps/c64/c64cpu.inc` | the 6510 core (§4) | yes |
| `apps/c64/c64mem.inc` | the movers (§3.6) | yes |
| `apps/c64/c64band.inc` | the composers (§9.5) | yes |
| `apps/c64/c64.asm` | the shim: `CC_PKG_NAME 'C64'`, `CC_HAS_ONKEY`/`ONCLICK`/`ABOUT`/`ONWAKE`/`MENUS`/`FDLG`/`OVL` (**no `WORKER`**, §15.2), `CC_ICON`, `%include cc/crt0.asm`, `c64.gen.asm`, then the three `.inc`s, `CC_IMAGE_END` | yes |
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
| **resident total** | **52,600** of 61,440 — 2,400 under SPEC.md §73.9's 55,000 trigger, 8,840 under the cap. **This is what was PLANNED; the margin that binds is the measured one in §13.0.1's table** |

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
  `os88_onwake`, `os88_onfile`, `os88_oncmd`, `os88_about` — because the
  runtime reaches one by a near offset. A callback that needs overlay code
  calls an already-loaded `ovl_*` helper (§11.3). (`os88_worker` was on this
  list until wave 3's fix pass took the worker out, §15.2.)
- **AND A LOCKED CALLBACK NEVER CROSSES INTO `C64.OVL` UNLESS THE MODULE IS
  ALREADY RESIDENT.** Reaching an `ovl_*` makes the runtime RESOLVE the
  module, and if it is not resident that resolution is an `OSAPI_MEM_CLAIM`
  and an `OSAPI_FILE_READ` — **a floppy seek, ~400 ms a call** — inside
  whatever context asked for it. `os88_oncmd`, `os88_about` and `os88_onfile`
  are all dispatched **under the desktop's gfx lock**, so a first-wake probe
  that failed for a transient reason left every later menu pick able to go to
  the DISK with the whole machine stopped behind it. `c64_ovl_ready(win)` is
  the fence: it refuses, says `Unable to load C64.OVL.` on the row, clears
  `c64_ovl_asked` and kicks a wake — and **the WAKE, which holds no lock and
  may call the file slots by contract (SPEC.md §74.1), is what retries the
  load.** The rule is the overlay's half of §7.7's: the lock decides where the
  work runs, not the convenience of the call site.
- **The `.OVL` cannot be loaded from `os88_main`** (LESSONS 13) — there is no
  instance yet. The first `ovl_*` call is made **from the first wake**, and
  its refusal prints **`Unable to load C64.OVL.`** in the status row *and*
  toasts, because a toast under a fullscreen window is not where the user is
  looking (§9.8). That call is **`ovl_conv_init()`** — the far call the
  RUNTIME makes on the way in is what loads the module, so the probe is asked
  once at the first moment there is an instance to resolve it against, rather
  than discovered when a user picks a menu item. Wave 1 did not have it, and
  every overlay wrapper's 0 returned silently: `os88_oncmd` and `os88_about`
  now say the same sentence, because every body in `c64cmd.c` returns 1 and a
  0 therefore never came from one of them.
- **…and the probe does the port's once-per-launch WORK while it is there.**
  Its body used to be `return 1`. It now builds `c64_sctab` and `c64_pettab`
  (§7.7) — the one thing in this program that runs exactly once, and therefore
  the one thing besides a menu command that §73.14 sends out. The tables
  themselves are bss and stay resident: only code moves.
- `C64.O88` and `C64.OVL` are **two files in one folder** on every disk they
  share (SPEC.md §19.2.1, SPEC.md §19.9) — §14.2. `C64.ROM` was a third and is
  a PART of `C64.O88` now (§1.4).

---

## 14. Names, disks, targets, machines and harnesses

### 14.1 Names

| | |
|---|---|
| package name | `C64` |
| source | `apps/c64/` |
| shipped files | `C64.O88` (with the ROM as part 0, §1.4), `C64.OVL` |
| window title | `VICE (C64)` |
| menu-set `AM_NAME` | `VICE` |
| images | `build/c64.img` (1.44MB), `build/c64720.img` (720KB), `build/c64360.img` (360KB) |
| tools | `tools/c64rom.py` (builds the ROM the packer appends, §1.4), `tools/c64prg.py` (writes `.PRG` fixtures, §14.4), `tools/c64ref.py` (the reference compositor, §14.5) |

The name is checked against `apps/`, `vm/`, the Makefile and `build/` before
wave 1 (LESSONS 1's rule about two programs sharing an ambition).

### 14.2 Disks

Three geometries, each `os88disk.py --verify`'d in the recipe. Each carries
**`C64.O88` + `C64.OVL` in one folder**, plus a `README.TXT` naming the licence
and carrying the ROM copyright line (§1.2, §1.3). The ROM is inside `C64.O88`
(§1.4), so the byte count is unchanged and the file count is one lower.

**AND `COPYING` TRAVELS WITH THE BINARY.** The floppy is the distributed form
of a GPL-2-or-later program, `apps/runcpm`'s disks ship their upstream licence
beside the CCP for the same reason, and `README.TXT` on this disk says the
full licence text accompanies every release — which was not true of the disk
that said it. `apps/c64/COPYING` is now a prerequisite of the image and a file
on it, and `README.TXT` points at it *on the disk* as well as in the source
tree.

**Which geometries carry it, with the arithmetic:**

| geometry | clusters | `C64.O88` (ROM included) + `C64.OVL` + `README.TXT` | `COPYING` (17,989 B) | carries it |
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
resolve in one directory, and `README.TXT` and `COPYING` beside them for the
reason above: that disk is a distributed form of the binary exactly as the
dedicated ones are, so the licence travels there too. Five files, ~120 of
2,847 clusters.

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
| `apps/c64/hosttest/c64uitest.c` | the whole program over a stub `os88.h` with a **PIXEL** model of the glass — `gfx_blit1` writes real pixels, and `gfx_scroll` moves them and fills the vacated rows with GARBAGE, which is what catches a flush that trusts a stale shadow for a row the scroll vacated. After every step it asserts, pixel for pixel over the whole 320×200 screen, that **the glass shows what the shadow says it shows**; it prints §9.7's cost table in milliseconds and the dirty-pages-per-wake counter, and it dumps the machine and the composed frame for `tools/c64ref.py`. It compiles this same C with **`-DC64_HOST`**, which is what keeps those counters out of the shipping image (§9.7): nothing in `apps/c64/*.c` reads one. Wave 2 adds the scripted core, the level keyboard and the alarm path behind it. Run by `build.sh` before every build. **The assembly half cannot run on the host**, so the routines it substitutes are transcriptions — which makes `c64ref.py` a check on the ALGORITHM and not on the 8086 encoding; the encoding is gated by `c64memtest.sh`, by `tests/c64band`'s identity rows and by the QEMU screendumps, all of which run the shipping text. It also **enforces the clip** — a pixel written outside an armed region while no clip is armed is a failure with its coordinates, which is what makes SPEC.md §11.3 checkable for the callbacks that are not `W_PAINT` — and it models `os88_wm_close` as the deferred close it is — the call RETURNS and the state must be `C64_ST_DEAD` with nothing drawn after it — which is what makes File > Exit emulator's teardown checkable at all (it modelled `os88_task_alive` as a call that never returns while the port still had a worker, §15.2). **`--no-rom` is a second process**: `os88_main` decides the refusal surface once per launch, so the screen a user of a mis-copied disk actually sees needs its own run. **Wave 3 adds a stub clipboard that can refuse, a stub `os88_snd_caps` whose TONE bit a row can clear, and counters for the matrix read, the row pulls and the bytes the paste feeder types** — because a cost row with no drawing in it would otherwise print 0.0 ms, which is LESSONS.md 7's *"a stub that always refuses measures the fallback path"* read the other way round. **The review's fix pass added four things to it, and each is a row that would have failed before**: `os88_clip_size` now casts through `short`, so the host's 32-bit `int` cannot hide what the target's 16-bit one does with a 32,768-byte clipboard; a Copy that reads the matrix a CELL at a time (which in the shipped build is a cell per far call) fails on the `c64_rd` counter; the overlay boundary is a term in the cost model with the crossing count written beside each row; and the truncation message is compared against `C64_PASTEMAX` so the two cannot drift. The `--no-rom` run drives Alt+Delete and Alt+Insert and asserts that **the system clipboard survives**. **The SECOND review pass added six more**: the conversion tables must NOT exist after `os88_main` and must exist after the first wake (the frequency split's own negative control, §7.7); a GRAPHICS screen code is copied and `.` required back (§7.7 — the fixture had only letters, so the substitution was untested); `os88_clip_put`'s byte count is a term in the cost model and the Copy row is taken on a FULL screen, because the sparse one hides it; the warp render cap is asserted on BOTH tiers with warp OFF as the control (§4.4); a SHORT message is required to leave the joystick widget's pixels untouched, with a 33-cell message as the control (§10.1); and every message LITERAL is walked against `C64_MSGCELLS` — the row that used to do this measured `c64_msg` *after* `c64_say`'s clamp and could never fail, and `apps/c64/build.sh` now extracts the literals to walk mechanically out of `apps/c64/*.c`, so a message this document never heard of is measured too (§10.1). **The FIX pass added the magnification's gates**: `c64_scw`/`c64_sch` read off the computed geometry on a 640×480 box and a 640×200 one, every 2 × 2 block of the magnified glass asserted uniform, the `k = 3` scroll's `dy` asserted to be 48 and not 24, and two negative controls — the `CPU_8086` tier must not magnify and a refused `blit1` must come down to 1:1 (§9.8) — plus the SID's hertz at both ends of the range and at the 20 Hz floor (§11.4) |
| **`tools/c64ref.py`** | **an independent, pixel-level reference compositor.** Python, written from VIC-II documentation and VICE's `src/vicii/` as the authority, **not** from `c64band.inc`: it renders the same C64 memory to a 320×200 1bpp image, and the harness compares it **bit for bit** against what the package composed. This is what validates hires bitmap, multicolour, a custom character set, the cell transpose and sprite priority/expansion — a cell-identity glass model provably cannot. **`--lumcheck` is the other half**: the package's 16-byte luminance table against `vicii_colors_6569r5`'s own Y column, held here as parts per thousand straight off `vicii-color.c:441`, over all 256 ORDERED PAIRS — which is the only way to ask about §9.6's seven equal-luminance pairs in both directions, since a rendered frame carries one background at a time. The oracle derived its luminance from `vice.vpl` until this wave's fix pass, i.e. it kept the defect the package had already had removed |
| `apps/c64/hosttest/c64cputest.asm` + `.sh` | §4.6's **twelve** rows with their negative controls — `make c64cputest`, minutes, not in `build.sh`. Rows 11 and 12 and the rebuilds of rows 4, 8 and 9 are wave 2's third fix pass: a row that names a family it does not execute is a row that passes over the defect, and four of them did |
| `apps/c64/hosttest/c64memtest.asm` + `.sh` | §3.6 — `c64mem.inc` **and** `c64band.inc`'s cross-segment entry points under `SS ≠ DS` with an `ES` sentinel: the movers, `c64_rowspan`/`c64_rowcopy`, and **`c64_band1` composing out of both claims and `c64_rowsig` signing out of one**, which nothing called until this wave's fix pass. **Four negative controls, one per thing the discipline check claims to check** — ES, DF, BP and DS. The BP and DS ones are new because both checks were inoperative: BP was recorded AFTER the call and compared with itself, and the checker did its own bookkeeping through whatever DS the routine under test had left behind. **Section 4b is `c64_copy_row`'s** (§7.7): the reverse-video mask, the table index, the store into ANOTHER SEGMENT, the trim, an all-spaces row, a row whose last cell is not a space, and `n = 0` — a proc that composes into a heap claim is exactly the shape that passes on the host and writes into the wrong segment on the target. **Section 6b is `c64_x2init` and `c64_band_x2`'s** (§9.8), and it exists because the harness above MODELS the doubler in C: five hand-computed `c64_x2tab` entries, the doubled-nibble identity over all 512 bytes, the second scan line asserted to be a copy of the first, and `rows = 1` writing no third row — the identity alone passes the all-zero table the shipped loop actually built, so both halves are the case. Run by `build.sh` |
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

**FOUR THUNKS — and still nothing in the kernel.** No slot is added, no kernel
`.text` moves and no budget constant moves: each of the four is a wrapper in
`apps/cc/os88thunk.asm` + `apps/cc/os88.h` (**and the `hosttest/os88.h` stub in
the SAME edit**) over a slot the kernel already publishes. This section said
*"one thunk"* until wave 3's fix pass, and the three that joined it are named
where the fact that needed them is:

| thunk | slot | the fact that needed it |
|---|---|---|
| `os88_key_down` | `OSAPI_KEY_DOWN` `0x03F0` | §15.1 — the level keyboard |
| `os88_wm_close` | `OSAPI_WM_CLOSE` `0x0470` | §15.2 — the worker idiom closes the WINDOW, not the APP |
| `os88_clip_put_seg`, `os88_clip_get_seg` | `OSAPI_CLIP_PUT` / `_GET` | §7.7 — clipboard staging in a CLAIM rather than in bss |

### 15.1 `os88_key_down` — the level keyboard's state

| | |
|---|---|
| **need** | key STATE for the level keyboard model (§7.2), the joystick (§8) and CTRL+digit (§7.3). `os88_onkey` delivers presses only |
| **slot** | `OSAPI_KEY_DOWN` — slot `0x03F0`, SPEC.md §9.7. `AL` = a make scancode, `CF` = down, every register kept; **asking is what arms it, and the first ask clears the map** (§7.2's rule 1); advice, not an oracle. The kernel already tracks every break code (`kernel/mouse.inc:1438`, `kbd_track`, `KBD_MAPSZ` 16 = all 128 make codes), and `apps/arkanoid` and `apps/cyclone` already use it |
| **action** | add `int os88_key_down(int scan)` to `apps/cc/os88thunk.asm` and `apps/cc/os88.h` (`CC_T_A1`-shaped, CF → 1/0, about a dozen lines) **and the `hosttest/os88.h` stub in the SAME edit** |
| **what it cost** | **18 bytes in every C package**, measured: CWORD's image went 35,938 → 35,956 with it in. `nasm -f bin` has no dead-code elimination, so a thunk nobody calls is still image — which is why the SDK's thunk file is a place to be careful. CWORD, the tightest C package in the tree, keeps 973 bytes of its 61,440 |

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
| a self-close for Exit emulator | `OSAPI_WM_CLOSE` slot `0x0470`, **WRAPPED — `void os88_wm_close(void *win)`, `CC_T_WIN`-shaped, in `apps/cc/os88thunk.asm` and `apps/cc/os88.h` with a stub in `apps/c64/hosttest/os88.h`** | **THIS ROW IS REVERSED, and it is the one row of this table that is.** It read *"exists **unwrapped** — keep RUNCPM's worker idiom (`CC_HAS_WORKER`); a `WM_CLOSE` thunk is a follow-up, not a requirement"*, and that decision was wrong on the glass. The worker idiom — `os88_wm_destroy` under the lock, then `os88_task_alive` outside it, which is what every C package in this tree does — **closes the WINDOW and does not close the APP**: `wm_destroy` frees the record and **nothing repaints the dock**, so Alt+Q left a **dead tile on the dock strip that answered neither a click nor a double-click**, four cycles out of four, on both adapters. The close BOX was always clean, because it goes through the kernel's own `app_close_win` — which is the same path this thunk asks for. The package now has **no worker at all** (`CC_HAS_WORKER` is gone) and no task slot. **It is spent from the WAKE and not from `os88_oncmd`**: the slot returns and closes on the next UI pass, so it may legally be called under a command's lock, but its contract is *call it and RETURN, do not draw afterwards* — and `os88_oncmd`'s own tail kicks a wake that runs a slice and flushes into a window that is going away. So File > Exit sets `c64_exit_req`, and the top of `os88_onwake` spends it, sets `C64_ST_DEAD` and returns with the call as the last thing that happens |
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
