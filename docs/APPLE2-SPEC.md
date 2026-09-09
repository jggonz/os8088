# APPLE2 - an Apple II Plus emulator, written in C (`apps/apple2/`)

**The binding contract for the APPLE2 package.** It stands to `apps/apple2`
as `docs/C64-SPEC.md` stands to `apps/c64`: every symbol, constant, string,
register answer and layout the port depends on is pinned here, and the change
goes in here *before* it goes in the code. The design record - the intake, the
decisions, the waves, the risks - is `docs/APPLE2-PORT-PLAN.md`.

**It lives outside SPEC.md by the user's instruction, on the C64 precedent.**
SPEC.md gets no new section for this port.

**How this file is cited, and how it cites.** SPEC.md is always named in full
(`SPEC.md 73.14`). Another document is named the way the tree already names
one (`C64-SPEC §9.7`, `WEAVE-SPEC §1.2`, `CONTRIBUTING.md §6`). **This file's
own sections are referred to by NUMBER AND NAME - "APPLE2-SPEC section 7" -
and never with a bare section mark**, because `tools/checkdocs.py` resolves a
bare one against SPEC.md as well as against this file and an ambiguous
citation is exactly what that gate exists to prevent.

**Every planning figure in this document carries the word PLANNED.** A figure
without it has been measured, and the wave that measured it is named.

---

## 1. What it is, and the attribution

### 1.1 The port

`apps/apple2/`, package name **`APPLE2`**, is a **windowed 48K Apple II
Plus**: a 6502 running in a 64KB heap claim, Applesoft BASIC and the Autostart
Monitor read from a ROM **part** inside `APPLE2.O88`, the four II+
soft-switch display modes composed into 1bpp bands and blitted with
`OSAPI_GFX_BLIT1`, and a **foreign video mode** at full screen
(`FSXM_VGA13` / `FSXM_CGA640` / `FSXM_HERC`) for colour and, if it measures,
for speed.

It is a port in SPEC.md 73.12's sense and in SPEC.md 74's: **the behaviour is
taken from the reference emulators' source and not from memory; the code is
reimplemented in the C subset of SPEC.md 73 plus the hot loops that are
hand-written 8086; what cannot carry is present and greyed with the fact that
greys it** (SPEC.md 47). Nothing of any reference tree's source is vendored -
every file carrying derived tables, strings or behaviour names the file it
came from in its header.

**The structural shape is the C64's** (`C64-SPEC §4.4`, and RUNCPM's before
it, SPEC.md 74.1): no worker task and nothing blocking. The machine runs on
the UI task in wake-driven wall slices (`OSAPI_WM_WAKE` /
`OSAPI_WM_ONWAKE`), and an Apple II sitting at the `]` prompt costs nothing
until a key or a tick arrives. `CC_HAS_WORKER` is not declared: File > Quit
goes through `OSAPI_WM_CLOSE`, which is `C64-SPEC §15.2`'s correction already
made rather than repeated.

**What this port takes from `apps/c64` and what is new work** is the whole of
its risk, so it is stated at the top. Taken: the file split, the claim and
part mechanism, the dirty-page plus write-window damage model, the frame
shadow, the span compare, the k-row scroll test, the wake-driven wall slice,
the tier table, the four-geometry disks and all six harness kinds. **New:**
the three composers, the non-linear page-to-scan-line map the interleaved
address arithmetic forces, the four `OSAPI_FSX_*` C thunks, and the read-only
Disk II. There is **no bitmap composer in this tree to copy, and no text
composer either** - `apps/c64/c64band.inc` says in the shipping source that it
composes standard text only, and its cell is eight pixels wide.

### 1.2 The Apple text cell is SEVEN pixels

280 / 40 = **7**. Therefore:

- **no cell after the first lands on a byte boundary**;
- **a decoded glyph is never a pixel byte**;
- there is no per-cell store shape anywhere on this machine;
- all three composers - text, lo-res and hi-res - are **shift-accumulator
  composers of the same class**, which is what makes the text composer of
  wave 1 reusable by wave 3 rather than special.

Rendering text at 8 px instead is not an escape: MIXED puts 160 hi-res scan
lines above 32 text ones in ONE 40-byte-stride frame with one shadow.

### 1.3 Licence and attribution

`a2cpu.inc` is a **derived copy** of `apps/c64/c64cpu.inc`, which is
GPL-2-or-later by way of VICE. Therefore **`apps/apple2` is GPL-2-or-later**,
the rest of the tree is not, and every derived file's header, the About panel
(section 11), `apps/apple2/COPYING`, `apps/apple2/README.TXT` and the PR body
all say so.

| tree | licence | what it is the authority on |
|---|---|---|
| VICE 3.10 (via `apps/c64/c64cpu.inc`) | GPL-2-or-later | the 6502 core's register plan, shells, flag tails, decimal arithmetic and dispatch |
| AppleWin | GPL-2-or-later ("either version 2 ... or (at your option) any later version", in every source header) | everything II+-specific: the keyboard byte map, the soft-switch fences, the ROMs, the disk formats and the nibbliser |
| MII (`mii_emu`) | MIT | the Macintosh-shaped menu bar, the About panel, the soft-switch enumeration, the per-write dirty-line map, the flash phase and the hi-res artifact-colour index |
| apple2emu | MIT | the 24-entry row-base tables, the Edit menu, the Disk II controller, the paddle one-shot and the paste handshake |

**`apps/apple2/COPYING` and `apps/apple2/README.TXT` ship from WAVE 1**, not
from the polish wave: `a2cpu.inc` is derived from a GPL file in wave 1, wave 1
puts four floppies of that binary on disk, and the C64 disk recipe names both
files as **prerequisites and payload** (`Makefile:4855`), so a recipe modelled
on it fails with *No rule to make target* without them.

`COPYING` carries the GPL-2 text plus MII's and apple2emu's MIT notices
**verbatim** - the permission and warranty paragraphs and not only an SPDX
tag, because MIT's own condition is that "the above copyright notice **and
this permission notice**" be included - and AppleWin's own four-holder
copyright notice.

### 1.4 The ROMs - fetched at a pin, never committed

**The Apple II+ ROM images are Apple Computer's copyright.** This port takes
the stricter posture the C64 did not: they are **fetched at a pinned SHA-256
by `tools/getapple2rom.py` and never committed**, exactly as
`tools/getruncpm.py` pins RunCPM's master disk and `tools/getstories.py` the
Frotz stories. The copies used are the ones AppleWin (GPL-2-or-later) carries in its
`resource/` directory, taken at ONE pinned commit.

**The pin:** AppleWin commit
`3e8054b4627624398e4589f7f27b3d40a6b9718e`.

| repository path | offset in `APPLE2.ROM` | bytes | SHA-256 |
|---|---|---|---|
| `resource/Apple2_Plus.rom` | `0x0000` | 12,288 | `fc3e9d41e9428534a883df5aa10eb55b73ea53d2fcbb3ee4f39bed1b07a82905` |
| `resource/Apple2_Video.rom` | `0x3000` | 2,048 | `08f5d22230481019844492dde0a29a018cb193712a9e4a43770a3870608f28de` |
| `resource/DISK2.rom` | `0x3800` | 256 | `de1e3e035878bab43d0af8fe38f5839c527e9548647036598ee6fe7ec74d2a7d` |

**The layout is fixed and the package depends on it:**

| offset | length | contents |
|---|---|---|
| `0x0000` | 12,288 | ROM - Applesoft BASIC `$D000-$F7FF` and the Autostart Monitor `$F800-$FFFF` |
| `0x3000` | 2,048 | CHARGEN - the II+ character generator |
| `0x3800` | 256 | DISK2 - the P5 boot ROM, mapped at `$C600` when a controller is in slot 6 |
| `0x3900` | 256 | zero pad |
| | **14,848** | total - 29 sectors, 512-aligned by construction (SPEC.md 2.1.1) |

**The RESET vector at `$FFFC` reads `$FA62`**, which is what proves the file
is the Autostart Monitor and not some other II ROM.
`tools/getapple2rom.py --check` verifies the cache and writes nothing.

**Consequence, stated because it is a real one:** a build with no network and
no `build/apple2-rom/` cache **cannot build this package**, exactly as
`make zdisk` and `make runcpm` cannot. Nothing in `all` reaches it, and
`make clean` must spare the cache.

### 1.5 The ROM is PART 0 of `APPLE2.O88`

SPEC.md 20.12's parts, used exactly as `C64-SPEC §1.4` uses them: the shim
declares `CC_HAS_PARTS` and one `OS88_PART OP_ASSET`, and `apps/cc/crt0.asm`
calls `op_load` **before any C runs**. By the time `os88_main` executes the
14,848 bytes are claimed and the ROM is in them - or the launch was refused,
with a toast, before a sector was read.

`OP_ASSET` rather than `OP_SEGMENT` because nothing in it is far-called: the
core reaches it by segment arithmetic.

**`APP_MAX_SIZE` bounds the primary segment's image plus bss, not the FILE**
(`C64-SPEC §1.4`'s correction), so the ROM is **0 bytes of image**. The file
on disk is image + 14,848.

**And because `op_load` runs before any C**, `os88_main` can decode the
character generator and build the 7-bit reverse table there - which is what
keeps the display path off the overlay (section 7.3).

---

## 2. Where the behaviour comes from - the authority table

Every user-visible surface names ONE file in ONE reference tree. Paths are
relative to that tree; the trees are *references*, never build dependencies.
**The rule that resolves a conflict: AppleWin wins on anything II+-specific**,
because MII has no II+ mode at all and apple2emu gates the //e switches
unconditionally.

| surface | source |
|---|---|
| Menu bar: the Macintosh shape and item order. MII builds FOUR menus - Apple, File, Machine, CPU (`mii_mui_menus.c:432-441`) - and Video and Audio are SUBMENUS of Machine (`mii_mui_menus.h:118-121`), not top-level. Folded here to FOUR os8088 menus: File, Edit, Machine, CPU, with Video's and Audio's rows INSIDE Machine under separators, because the os8088 bar has no submenu mechanism at all (`apps/os88api.inc`, `AMENU_ENTSZ` 6, a flat list; grep finds no submenu in `kernel/menu.inc`). The Apple menu becomes the kernel's own name pull-down. The dynamic Stop/Stopped and Running/Continue retitling, the check marks and the greying-at-the-limit idiom are MII's | MII `ui_gl/mii_mui_menus.h:22-146` + `:118-121` (the submenu nesting) + `mii_mui_menus.c:130-205` (the `MUI_MENUBAR_ACTION_PREPARE` arm, which IS the greying and ticking authority) and `:432-441` (the four menus actually built) |
| The Edit menu and its Paste row - **not** MII's, which has no Edit menu at all | apple2emu `src/interface.cpp:379-382` |
| About panel: layout and text shape - "The `<name>` `<machine>` Emulator", version, build line, copyright, "Thanks to:". Rendered as `os88ui_about`'s card rows, TWELVE ROWS MAXIMUM. It carries the product, the version, what this port is, the four attributions and the ROM copyright line and NOTHING about how the build renders; the `][+` glyph is spelled "II Plus" because the kernel face is ASCII 32..126 | MII `ui_gl/mii_mui_about.c:96-140`, the row shape at `:101-107` |
| The product name, ONE string with one short form. Long: `TITLE_APPLE_2_PLUS` = `Apple II Plus Emulator` - the window title and the About panel's first row. Short: `Apple II+` - `AM_NAME`, the Wire record's title and the package stem `APPLE2` | AppleWin `source/Common.h:48-63`; the short form is the user's instruction, recorded here as a departure from the cited definer with its reason (the menu-bar cell) |
| Keyboard: the II+ byte map. `asciicode[3][10]` row 0 = { Left `$08`, Up `$00`, Right `$15`, Down `$00`, Select `$00`, Print/Execute/Snapshot/Insert `$00`, Delete `$00` } - a II+ has NO up arrow, NO down arrow and NO DEL, and a zero entry means DROP the key. a-z fold to uppercase unconditionally; backtick, `{ | } ~`, DEL and everything above `$7F` are REJECTED, not translated. Left arrow IS `$08`, which is why BkSp, Ctrl+H and Left all deliver `$88` with no conflict | AppleWin `source/Keyboard.cpp:38-44` (the table), `:121-195` (the II+ folds and rejections) |
| Keyboard: the prose surface - Ctrl+Reset is Ctrl+F2 or Ctrl+Break, Caps Lock is always on on a II/II+, paste feeds one character at a time and folds CR+LF to CR, and FULL SCREEN IS ALT+ENTER | AppleWin `help/keyboard.html`; the precedent it corrects is `C64-SPEC §7.5` and `§9.8` |
| Keyboard latch and strobe: `$C000` reads keycode OR (waiting ? `0x80` : 0); `$C010` clears the waiting flag. AND THE II+ FENCE: every address in `$C010-$C01F` on a II+ is nothing but "clear the strobe, then answer the floating bus" - apple2emu registers the //e status switches over that whole range unconditionally and is WRONG for this machine | AppleWin `source/Keyboard.cpp:415-448` + `Memory.cpp:563-567`, `:579-587` (the `IS_APPLE2` gate); second reading at MII `src/mii.c:603-648` |
| Soft-switch address enumeration and canonical names (`SWTEXTOFF`..`SWHIRESON`, `SWKBD`, `SWAKD`, `SWSPEAKER`) | MII `src/mii_sw.h:10-64` |
| Video soft switches, the II+ SUBSET: `$C050`/`$C051` TEXT, `$C052`/`$C053` MIXED, `$C054`/`$C055` PAGE2, `$C056`/`$C057` HIRES - and the `!IS_APPLE2` gates that say `$C000`/`$C001` 80STORE, `$C00C`/`$C00D` 80COL, `$C00E`/`$C00F` ALTCHARSET and `$C05E`/`$C05F` DHIRES are NOT on this machine. Every one returns the floating bus on a read | AppleWin `source/Video.cpp:177-243`; the flag bits at `Video.h:52-71` (`VF_HIRES` 4, `VF_MIXED` `0x10`, `VF_PAGE2` `0x20`, `VF_TEXT` `0x40`) |
| Screen address maps: the 24-entry row-base tables. Text primary `1024 + 256*((i/2)%4) + 128*(i%2) + 40*((i/8)%4)`, secondary at 2048, `hires_map[i] = 0x1C00 + text_map[i]`, and a scan line is `hires_map[row] + 1024*sub`. 48 words of `cs:`-resident table for both pages, not a 192-entry table. Its own source is cited in the file: "Apple 2 Monitors Peeled, pg 15" | apple2emu `src/video.cpp:687-718`; the closed form second reading at MII `src/mii_video.c:214-223` |
| Per-write dirty-LINE mapping, including both screen-hole tests and the MIXED split: text/lo-res hole = `(a & 0x7f) > 0x77`, `group = (a>>7)&7`, `gline = (a & 0x7f)/40`, `line = (group + gline*8)*8`, mark 8 lines; hi-res hole = `(a & 0x78) == 0x78`, `line = gline*64 + g*8 + g2`, mark ONE line; a hi-res write above the mixed line is dropped and a text write below it is kept | MII `src/mii_video.c:225-311` (`_mii_addr_to_line_text_lores`, `_mii_line_check_text_lores`, `_mii_line_check_hires_dires`) |
| Character-generator decode: `n = rom[i*8 + (y&7)]`; if the entry is in the low 1KB and bit 7 is clear, `n ^= 0x7F`; the 7 pixel bits are stored BIT-REVERSED (the comment cites UTAII:8-30) | AppleWin `source/NTSC_CharSet.cpp:180-234` (`userVideoRom2K`) and `:246-253` (`VideoRomForIIandIIPlus`) |
| Text flashing, which is a FRAME COUNTER and not an encoding: a screen byte in `$40-$7F` swaps between its normal `$80-$BF` form and its inverse `$00-$3F` form by +/- `$40` on a 16-frame counter | MII `src/mii_video.c:476-521` (`_mii_line_render_text`), `:507-511` |
| Lo-res cell rule: 40x48 blocks of 7x4, `c = (byte >> ((line/4 & 1)*4)) & 0xF`, low nibble the top four scan lines and high nibble the bottom four | apple2emu `src/video.cpp:484-509` (`render_lores_cell`); second reading at MII `src/mii_video.c:524-585` |
| Lo-res 16-colour palette, for the VGA13 DAC and for the 1bpp luminance ladder the windowed path uses. **A palette the reference DISPLAYS**: AppleWin's `PaletteRGB_NTSC` lores block is refused here because its own first line (`RGBMonitor.cpp:148`) says it is a placeholder overwritten at start-up by `VideoInitializeOriginal` (`:1186-1196`, from `NTSC.cpp:2366-2368`) with colours `GenerateBaseColors` (`NTSC.cpp:2697-2721`) computes rather than lists | MII `src/mii_video.c:94-113` (`palettes[0]`, "Color NTSC") through MII's own lo-res mapping `mii_base_clut.lores[0]` (`src/mii_video.c:173-177`) - MII's `CI_*` enum is NOT in Apple colour order; cross-check apple2emu `src/video.cpp:100-115` (the mrob.com values, indexed by colour directly, whose two greys are the same byte) - the two agree on all sixteen lit/dark decisions |
| Hi-res, monochrome: 7 pixels a byte, and BOTH references throw the high bit away in mono (apple2emu writes `byte &= 0x7f`; MII's mono arm never reads bit 7) | apple2emu `src/video.cpp:511-537` (`render_mono_hires_cell`); MII `src/mii_video.c:459-470` |
| Hi-res artifact colour, the integer form that fits a 256-colour DAC with no floating point: `run = ((b0 & 0x60) >> 5) \| ((b1 & 0x7f) << 2) \| ((b2 & 0x03) << 9)`, `odd = (x & 1) << 1`, `offset = (b1 & 0x80) >> 5`, then a TEN-entry CLUT { black, purple, green, green, purple, blue, orange, orange, blue, white } | MII `src/mii_video.c:414-455` (the index) and `:188-196` (the CLUT) |
| The speaker: `$C030`, and the fact that READS AND WRITES BOTH toggle | AppleWin `source/Memory.cpp:634-651` (`IORead_C03x` / `IOWrite_C03x`, both call `SpkrToggle`) |
| Audio rows, inside Machine: Mute / Louder / Quieter | MII `ui_gl/mii_mui_menus.h` (`m_audio_menu`) |
| The 6502 clock: `FREQ_6502` = 14,318,181.8181 * 65 / 912 = **1,020,484 Hz** (MII's coarser 1,022,727 is NOT used) | apple2emu `src/apple2emu.h:59-61`; cross-check AppleWin `source/Common.h:3-5` (`CLK_6502_NTSC`) |
| Paddles and buttons: `$C061`/`$C062`/`$C063` answer `$80` when DOWN (apple2emu's comment claims active-low and its own code does the opposite - AppleWin wins); `$C070-$C07F` arms all four one-shots at `deadline = now + value*11` EMULATED CYCLES (MII's integer form, not apple2emu's float `2816.0/255.0`); `$C064-$C067` answer bit 7 until the deadline passes | AppleWin `source/Joystick.cpp:599-691` + `Memory.cpp:721-757`; the integer arithmetic at MII `src/mii_analog.c:25-75` |
| The floating bus / an unmapped `$C1xx` read: answer `$FF` when no page is mapped | apple2emu `src/memory.cpp:745-752`; the thing being refused is AppleWin `source/Memory.cpp:2473-2489` + `Video.cpp:336-460` |
| RAM power-on pattern: fill `$0000-$BFFF` with the repeating `FF FF 00 00`, plus three specific pokes (`memmain[$4E]`/`[$4F]` forced non-zero for Pooyan's RNDL/RNDH, `[$620B]` = 0, `[$BFFD..$BFFF]` = 0). Both trees cite AppleWin issues #206 and #225, and Castle Wolfenstein / Pooyan, as why a zeroed machine misbehaves | AppleWin `source/Memory.cpp:2340-2412` (the `MIP_*` patterns); second reading at apple2emu `src/memory.cpp:236-265` |
| Reset, two kinds: power-on sets SP = `$1FF` and lays the RAM pattern; Ctrl-Reset only pulls SP down by 3 and jumps through `$FFFC`. A cold boot pokes `$3F2`/`$3F3` = `$55 $55` so the Autostart Monitor's power-up check fires | AppleWin `source/Utilities.cpp:530-538`, `572-604`; the `$3F2`/`$3F3` poke at MII `src/mii.c` (`mii_reset`) |
| Paste into the emulated keyboard - the handshake: `$C000` PEEKS the next byte with bit 7 set, `$C010` CONSUMES it, `\n` folds to `\r` (`$0D`), `\0` ends the paste. The emulated program sets the rate, so there is no pacing state at all | apple2emu `src/keyboard.cpp:165-210`; the same handshake as "only when `!(peek(SWAKD) & 0x80)`" at MII `ui_gl/mii_thread.c:151-155` |
| Applesoft zero page for Load and Save Program - the ONLY statement of these addresses in any of the three trees, and it names its own source (Bob Sander-Cederlof's S-C DocuMentor: Applesoft) | AppleWin `bin/A2_BASIC.SYM:22`, `:267`, `:604`, `:748`, `:829`, `:846` |
| `.DSK` / `.DO` / `.PO` geometry and the two sector-order tables | AppleWin `source/DiskImageHelper.cpp:81-104`; byte-identical second copy at MII `src/format/mii_dsk.c:26-37` |
| 6-and-2 nibblisation: `ms_DiskByte[0x40]`, `Code62`'s 256-to-342 six-bit split, the running-XOR checksum, `NibblizeTrack`'s framing and `SkewTrack`'s rotation | AppleWin `source/DiskImageHelper.cpp:87-96`, `:296-355`, `:508-582` |
| Disk II controller: the whole card as one switch on `(addr & 0x0F)`, and the SAMESLOT 6-cycle re-read trap | apple2emu `src/disk.cpp:104-345`, the trap specifically at `:125-132` |
| The Disk II P5 boot ROM at `$C600`, and its Autostart boot signature (`$Cn01` = `$20`, `$Cn03` = `$00`, `$Cn05` = `$03`) | AppleWin `source/Disk.cpp:2070-2100` (`InitFirmware`); the 256 bytes themselves are offset `0x3800` of `build/apple2-rom/APPLE2.ROM` |
| The disk dialog, reached from Machine > Configure Slots... : "Drive 1:" / "Drive 2:", Select becoming Eject when loaded, the empty filename field reading `Click "Select" to pick a file`, and a wrong size raising an "Invalid Disk Image" alert reading `File '%s' is the wrong size, %s too %s.` with the size formatted HUMAN-READABLY | MII `ui_gl/mii_mui_2dsk.c:87-145`, the formatting at `:110-119` |
| The drive status readout: a motor lamp and `%02d/%02d` of current track / current byte over 256 under a `Drive 1  Drive 2` header | apple2emu `src/interface.cpp:404-427`; cross-check AppleWin `source/Windows/WinFrame.cpp:1692-1745` |
| Video tint choices offered by both references - White, Amber, Green, Color - which is what the four radio rows inside Machine carry | apple2emu `src/interface.cpp:317-323` (`video_tint_types`); MII `ui_gl/mii_mui_menus.h:46-77` (`m_video_menu`) |
| The reboot confirmation for Machine > `Power On`, TWO rows and not one: `Are you sure you want to reboot?` then `(All data will be lost!)`. The five lines after it in the reference are about an AppleWin Configuration checkbox this port does not have and are dropped | AppleWin `source/Windows/WinFrame.cpp:2002-2012` |
| Every os8088-side surface that is NOT the Apple II's: the window/border/status geometry and the frame-is-content+2 rule, the `dock_top` clamp, the ROM as a PART with `op_load` before any C runs, the wake-driven wall slice, the dirty-page bitmap + write window + watch range, the frame shadow and the flush order, the k-row scroll test, the composers' span argument, the tier table and the 2x doubler, the About panel's twelve rows, the status row's delta drawing, the overlay frequency split, the four-geometry disks with `COPYING` on each, the 86Box machines and the six harness kinds | C64-SPEC §1.4, C64-SPEC §3.5, C64-SPEC §4.4, C64-SPEC §7, C64-SPEC §9.1 through C64-SPEC §9.8, C64-SPEC §10, C64-SPEC §12, C64-SPEC §13.0.1, C64-SPEC §13.1 through C64-SPEC §13.3, C64-SPEC §14.1 through C64-SPEC §14.6, C64-SPEC §15; and the shipping source `apps/c64/c64.asm`, `c64.c:1680-1692`, `c64band.inc`, `c64cpu.inc`, `c64mem.inc`, `hosttest/c64uitest.c`, `hosttest/c64memtest.asm`, `hosttest/c64cputest.asm`, `tools/c64ref.py`, `tools/c64dec.py` |

---

## 3. The memory model

### 3.1 The claims

| claim | size | contents |
|---|---|---|
| RAM | `os88_mem_claim(64)` - 65,536 bytes | the Apple's address space, `$0000-$FFFF`, flat, one segment. Only `$0000-$BFFF` is ever RAM to the emulated machine |
| ROM | `op_load`'s carve - 14,848 bytes | the ROM PART, section 1.4's layout, claimed and read before `os88_main` runs; `os88_part_seg(0)` is its base |

Launch is **defined by the claims succeeding**. The refusal sentence quotes
what was asked and `os88_mem_largest_kb()`.

Three more claims appear later and are refused politely if they cannot be had:
`APPLE2.OVL` at the first menu command (about 6,000 bytes, PLANNED), the
transient clipboard staging claims (section 6.5), and - at the fullscreen
LATCH, where a refusal is legal - the foreign-frame shadow (section 13).

**The heap is neither image nor bss and is accounted separately** (section
15). Wave 6's mounted disk image is 143,360 bytes **per drive**, over 64KB,
and is reached by segment arithmetic in `a2mem.inc` and never by a near
pointer.

### 3.2 The address map

| range | what it is | read | write |
|---|---|---|---|
| `$0000-$BFFF` | 48K RAM | the claim, direct | the claim, direct, plus the dirty bit and the write window |
| `$C000-$C0FF` | soft switches | calls out to `a2io.c`, **side-effecting** | calls out to `a2io.c` |
| `$C100-$CFFF` | slot space | the slot-ROM ladder; `$FF` where nothing is mapped; `$C600-$C6FF` is the Disk II P5 ROM once wave 6 lands | **dropped** |
| `$D000-$FFFF` | ROM | one ROM bias into the part | **dropped** |

**The read fast path is TWO instructions and one compare** -
`cmp bh,0xC0 / jae .high / mov dh,[bx] / ret` - which is **shorter than the
C64's**, because a 6502 in an Apple II has no `$00`/`$01` processor port to
test first. The write fence moves from the C64's `$D000` down to `$C000`.

### 3.3 The core's scratch - in the emulated machine, never in bss

The core's hot scratch is **256 bytes at `$CF00-$CFFF` of the 64KB claim**,
on `C64-SPEC §3.5`'s mechanism and for its reason.

**It has ZERO stated deviations**, which is strictly cleaner than the C64's
`$FFC0` with its two: on a 48K II+ **nothing between `$C000` and `$FFFF` is
ever RAM to the emulated machine**, so no program can read the scratch and no
write can reach it. The C64's two deviation paragraphs and both of their
harness cases are deleted rather than transcribed.

**AND THE FETCH TAKES THE SAME LADDER, WHICH IS THE HALF A READER WOULD MISS.**
`a2_rebias_go` never biases `$C000-$CFFF` to RAM: PC in that range leaves the
region marked "always re-bias" and every byte comes back through `a2_rd_bx`, so
a fetch at `$C030` clicks the speaker exactly as a read there does, a fetch at
`$C100` gets `$FF`, and **`JMP $CF00` finds `$FF` and not the countdown**. A
core that biased the region to RAM for the fetch alone would make the whole
page executable, and that is `a2cputest` row 7 with its negative control -
which is the shipping text's own `$C000` compare moved to `$D000` at runtime
(`a2_rebias_go.iofence`), because `_a2_run` empties `BOUND` on entry and a
poke at the scratch would not survive to the first fetch.

**The condition that makes this safe is stated here because it is the thing
that can stop being true.** It holds only while nothing models a **Language
Card** and no card claims `$C800-$CFFF` expansion ROM. Both are refused by
this port (section 10.4). A future wave that adds either **must move the
scratch first**.

**NASM gotcha, carried over:** `[bx + 0xCF00]` is refused under `-w+error`, so
`A2_SCR_DISP equ A2_SCR_BASE - 0x10000` exists and is what the core
addresses through.

### 3.4 The movers

`a2mem.inc` holds the RAM and ROM claim accessors and movers,
`a2_dirty_take` (one `rep movsw` plus one `rep stosw` over the 32-byte page
map and the window, **~190 us**, resetting the scratch in the same pass),
**`a2_wrote`** - one byte, one read, answering "has the machine written
anything since the last flush?" so that the wake can decide to flush before it
has read anything else (wave 1; without it the C's own `a2_dirty_any` is set
only by `a2_dirty_scan`, which runs INSIDE the flush, so the flush never runs
at all and every write sits in the emulated machine's memory with the glass
stale),
`a2_copy_row` (the eleven-instruction assembly Copy loop) and, from wave 6,
the segment arithmetic that reaches a track inside a disk-image claim larger
than 64KB.

**Wave 2 adds four**, and each is a mover or a clock rather than a
convenience: **`a2_zpower`**, AppleWin's `FF FF 00 00` power-on pattern over
49,152 bytes - the C form is 12,288 iterations of two near calls, about 270 ms
on the target, spent inside `os88_main` before the window is up;
**`a2_bread`**, one byte through the CORE's ladder, because the reset vector is
in the ROM and `a2_rd` is RAM by construction; and **`a2_clk_set` /
`a2_now`**, the emulated clock the paddle one-shots are armed and read against
(section 5.1). `a2_now` answers `A2_SCR_CLKB - A2_SCR_DEAD`, which is exact
INSIDE a run - which is where `$C070` and `$C064` are reached from - as well
as outside one, because `a2_expire` leaves what was not spent in `DEAD`.

Every `movs`/`cmps` loop loads DS and ES on purpose, restores both, leaves DF
clear, and carries `cc8086:allow` with its reason. `hosttest/a2memtest.asm`
is the gate, with four negative controls - one each for ES, DF, BP and DS.

---

## 4. The 6502 core

### 4.1 A derived copy, and what that costs

`apps/apple2/a2cpu.inc` is a **derived copy** of `apps/c64/c64cpu.inc`
(2,313 lines, measured 6,518 bytes with `c64mem.inc` beside it), which was
measured at **93% generic NMOS 6502**. The Apple II model replaces the other
7%.

**Kept unchanged:** the register plan (A = AL, NZC in AH's `lahf` layout, VDIB
in CH, X = CL, Y = DL, PC = SI, S = DI as the full `$0100+S`, EA = BX, DS = the
RAM claim, ES = the fetch bias, BP = frame and scratch), the entry and exit
shell, `a2_getp`/`a2_setp`, all eight addressing modes, every flag tail,
binary and decimal ADC and SBC, all 151 legal opcodes and every illegal
family, the interrupt machinery, the `cs:` cycle table, the 256-entry dispatch
table, the signed-word countdown, the two-compare boundary-guarded fetch, and
the stack wrap written as `and di,0x00FF` / `or di,0x0100` (the one-line
`and di,0x01FF` is wrong, and Dormann's `$0D89` is what catches it).

**Replaced:** the read ladder, the write ladder, `a2_rebias`'s four boundaries
(`$C000`, `$C100`, `$D000`, `$FFFF`) with the two-compare guard and the
BLO/BOUND pair unchanged - a ceiling alone is wrong, because a backward `JMP`
from the Monitor to `$0800` leaves PC BELOW the region - the ROM bias
constants, and the two I/O call-out names.

**The stated trade:** a bug found in the shared 93% must be fixed twice, and
the second fix will be forgotten. The mitigation is that **both cores run the
same fetched oracles** (section 4.4). A later factoring into
`apps/6502/m6502.inc` with the seven hooks as `%macro`s is possible and is not
a first wave.

### 4.2 Time, and what `a2_run` answers

Time is **6502 cycles**. Every opcode carries its real cost, page-cross and
taken-branch penalties included, from the `cs:` cycle table.

`_a2_run(cycles)` runs a **signed-word countdown** with `a2_cut`/`a2_expire`,
so `ran = asked - cnt` is exact, and answers the C64's own status set - a
slice that expired, a JAM, a cut.

**There is no alarm scheduler at all**, and that is a real simplification this
port states as such: a bare II+ has no timers and no interrupt source, so the
wake loop is `r = a2_run(budget)` with **no `min()` and no advance**. The
C64's alarm model (`C64-SPEC §4.4`) is not carried.

### 4.3 The wall slice

`os88_onwake` is the slice driver, on `C64-SPEC §4.4`'s shape: a raw cycle
budget seeded from `os88_cpu()`, adapted only on genuinely exhausted slices,
the flush run **at most once per host tick** and never once per slice, and the
gfx lock held only around the flush and never around a slice.

The wake also spends the latches set under the desktop's lock before it runs
the slice: exit, reset, power cycle, the clipboard pair, and the paste feeder.

#### 4.3.1 The budget is a DUTY-CYCLE CONTROLLER, and the rule it replaces
had a fixed point at 14 % of a tick

**The adaptation that shipped in wave 7 could not leave `A2_SLICE_MIN` on a
4.77 MHz 8088, and that was arithmetic rather than luck.** It doubled the
budget after **four consecutive** slices that cost no host tick and halved it
on **one** that did. A slice of length *d* crosses a tick boundary with
probability *d*/55: the wake is POSTED and dispatched (`a2_wants_wake`,
SPEC.md 74.1), never timed, so its phase against the tick drifts freely -
**measured, a 22.5 ms slice crossed on 0.40 of its wakes against the 0.41 the
model predicts**. Solving `(1-p)^4 / 4 = p` - the rate the walk steps up
against the rate it steps down - puts the fixed point at **p = 0.14**, so on
any machine where a slice is a real fraction of a tick the budget sat at 256
for ever. That is exactly the target machine, and section 16.4.1 is the
measurement.

So the crossing **rate** is measured instead of a run of luck being waited
for, and that rate **is** the duty cycle by definition. Over a window of
`A2_ADAPT_N` = 8 slices, `a2_adx` of them cost a host tick, and `a2_adx / 8`
is the share of a tick a slice is taking:

| the window said | what happens | why |
|---|---|---|
| a slice elapsed **>= 2** ticks | halve on the spot, window restarted | not a duty cycle any more - the UI task is stopped for a whole tick it will never get back, and nothing waits for statistics about a slice that has already overrun |
| `a2_adx == 0` | **double** | nothing cost a tick at all, so the budget is nowhere near this machine. This is the old rule's fast arm kept whole: a 386 still reaches the cap in a fraction of a second |
| `a2_adx <= A2_ADAPT_LO` (5) | `+ budget / 8` | under ~70 % of a tick |
| `a2_adx >= A2_ADAPT_HI` (7) | `- budget / 8` | over ~80 % |
| 6 of 8 | hold | the target band |

**The two steps are the same size on purpose**: the fixed point is then the
middle of the band and not an artefact of the arithmetic, which is the whole
defect of the rule above. The clamp - the cap, the `<= 0` test for a doubling
that landed past a 16-bit `int`, and the floor - is in ONE place under all
five arms rather than inside each.

**It self-tunes to WALL TIME and not to a cycle count, which is the property
that matters and is visible in the measurement.** Idle at the `]` prompt the
controller settles at **512** cycles and in a running `FOR/NEXT` at **648** -
and the slice is **40.5 ms** and **40.7 ms**, the same 0.74 of a host tick in
both. Applesoft spends fewer 8088 clocks per emulated 6502 cycle than the
Monitor's keyboard poll does, and the budget absorbs the difference instead
of the user paying for it in latency.

`A2_SLICE_MIN` is unchanged at 256. A tier-dependent floor was the other way
to reach the same slice and is NOT what shipped: the controller arrives at
0.74 of a tick by measuring the machine it is on, so a V20, a 4.77 MHz 8088
and a 286 each get their own budget with no tier table to keep true.

**AND `hosttest/a2uitest.c` IS THE GATE, because neither arm has a host in
this tree.** MartyPC is an 8088 and every profile in it is a 5150, so the
FAST tier - the `a2_adx == 0` arm, which is what a 386 and a 486 take - has
no emulator here at all, and the slow arm costs ten minutes a reading. So the
harness's `a2_run` stub takes a WALL-CLOCK PRICE: `h_run_cost` is microticks
of host clock per emulated cycle, and the stub advances the modelled tick by
however many boundaries the slice crossed, carrying the phase over from slice
to slice exactly as the machine does. Two cases, in `build.sh` on every build:
200 slices priced at **nothing** must reach `A2_SLICE_MAX`, and 600 priced at
the MEASURED **1,600 microticks a cycle** - section 16.4.1's 22.5 ms for 256
cycles - must settle between 384 and 768, the band being wide because the
controller oscillates inside its deadband by design. **The first thing that
case asserts is that the budget LEFT `A2_SLICE_MIN`**, which is the rule
above failing, in one line, without an emulator.

### 4.4 The core's gate - `make a2cputest`

`apps/apple2/hosttest/a2cputest.asm` on `c64cputest.asm`'s boot-sector shape,
`%include`ing the **shipping** `a2cpu.inc`, booted in raw QEMU with SS != DS
and read over the serial port. **Twelve rows, every one with a negative
control the harness requires to FAIL.** Minutes, therefore **not** in
`build.sh`.

| row | what it runs |
|---|---|
| 1 | Klaus Dormann's `6502_functional_test` to its success trap, fetched at a pinned SHA-256, never committed. Control: `ADC #` dispatched to `ORA #` |
| 2 | `tools/c64dec.py`'s 262,144 decimal ADC/SBC cases |
| 3 | reads across every Apple II mapping boundary |
| 4 | writes across every boundary, including the drop above `$C0FF` |
| 5 | **FETCHES** across every boundary, an operand straddling one, and a backward `JMP` out of ROM |
| 6 | ES and DS restoration, with the reload NOPped out **at runtime** as its control |
| 7 | the scratch page's inaccessibility from the emulated machine |
| 8 | **the soft-switch stub returns for READS as well as writes** - named, and not optional (section 5.3) |
| 9 | the illegal opcodes |
| 10 | table-driven cycle totals |
| 11 | the four unstable stores |
| 12 | interrupt push and RTI |

Every perturbation reaches the environment or the tables **at runtime**, never
the source, and each row names its own:

| row | the negative control |
|---|---|
| 1 | `ADC #` dispatched to `ORA #` |
| 2 | `SED` dispatched to `CLD`: the decimal path is never entered |
| 3, 8 | the soft-switch stub answers a constant |
| 4 | `STA abs` dispatched to `STA zp`, whose one-byte operand lands the store somewhere else entirely |
| 5, 7 | **`a2_rebias_go.iofence`'s `$C000` compare moved to `$D000` in the shipping text**, which biases `$C000-$CFFF` to RAM: precisely the core that fetches the claim's own bytes where a soft switch, a slot's `$FF` or the scratch page belongs. It cannot be done by poking the scratch, because `_a2_run` empties `BOUND` on entry |
| 6 | the `mov es, [FES]` after the call out NOPped out |
| 9 | `LAX abs` dispatched to `NOP`, and `ARR #` to `AND #` |
| 10 | one opcode's table cost made wrong |
| 11 | `SHA abs,Y` dispatched to `STA abs,Y` |
| 12 | `BRK` dispatched to `NOP` |

**Two things this harness does that the C64's could not.** The Dormann fixture
is a whole 64KB image, and on a 48K II+ its top 12KB is not RAM - so the
harness copies `$D000-$FFFF` of it into the **ROM PART** as well, where those
addresses are actually answered, and clears the core's scratch after the load
because the image lands on top of `$CF00`. And **row 12's vectors are written
through the ROM part**, not into RAM: `$FFFA-$FFFF` belong to the Autostart
Monitor on this machine and a write there is dropped.

### 4.5 Reset, both kinds

| | what happens |
|---|---|
| **power-on** | lay a `FF FF 00 00` pattern over `$0000-$BFFF` with AppleWin's three compatibility pokes (`$4E`/`$4F` forced non-zero, `$620B` = 0, `$BFFD..$BFFF` = 0), set SP = `$1FF`, poke `$3F2`/`$3F3` so the Autostart Monitor's power-up check FAILS, then take the Ctrl-Reset path below |
| **Ctrl-Reset** | set **I**, clear the jammed state, pull SP down by 3 **within page one** (`SP = (SP - 3) & 0xFF`, the stack stays at `$01xx`), and jump through `$FFFC`. **D is NOT cleared** |

**THE RESET LINE IS THE NMOS 6502'S AND NOT A CONVENIENCE.**
`AppleWin/source/CPU.cpp:769-774` and `:798-812` are the reference. Three
things this document had wrong or missing, each of which is a machine that
runs and misbehaves rather than one that fails:

- **`I` is SET.** A reset that leaves interrupts enabled runs the ROM's
  initialisation with IRQ live.
- **The JAM is cleared.** A jammed core that is never un-jammed makes
  Ctrl-Reset - the one recovery a user has - do nothing at all, which reads as
  the port having frozen.

**AND THE JAM SAYS SO, PERMANENTLY.** The line is **`6502: JAM at $XXXX`** - 18
glyphs, drawn at cell 16 of the 42-cell status row (section 9). **It is the
port's own string and this is where it is pinned**: a grep of all three
reference trees returns no JAM message at all, because none of MII, AppleWin
or apple2emu says anything on the glass when the 6502 jams, so it is modelled
on VICE's `Main CPU: JAM at $%04X` (`src/maincpu.c:612`, the string
`apps/c64/c64.c` carries) with `6502` for the CPU, this machine having one
processor and no reason to call it the main one. **It is a PERMANENT ROW STATE
and not a five-second message** (`A2_ST_JAM` read by `a2_status`, never
`a2_say`), for the two reasons `apps/c64/c64.c:1071-1087` states one machine
along: a jam does not expire, so once the message deadline passed the glass
showed a dead machine and an idle one identically; and the expiry itself
forced the row's full path and re-lettered the identical line at the identical
place, ~21 ms that changes not one pixel. **The speed field goes blank while
it is up** - a percentage about a machine that is not running is SPEC.md 47's
"grey a fact, never a guess" inverted - and the jam line in the message area
is what says why.
- **`D` is NOT cleared by an NMOS reset**, and it is tempting to clear it.
  Software that relies on the real machine's behaviour would see a different
  one here.
- **SP wraps within page one.** `SP - 3` is a byte subtraction; the stack
  pointer is a page-one offset and cannot leave that page.

AppleWin initialises SP to `$01FF` and then calls its reset, so what the ROM
actually starts on is **`$01FC`**. That is the value to reproduce, and it is
the power-on row taking the Ctrl-Reset path rather than a second constant.

**`$3F2`/`$3F3` = `$55 $55` IS AN EMULATOR TRICK AND THE REQUIRED CONDITION IS
A MISMATCH.** `$03F2`/`$03F3` is SOFTEV (the Monitor's warm-start vector) and
**`$03F4` is PWREDUP** (`AppleWin/bin/APPLE2E.SYM:65-66`). The II+ ROM at
`$FA85-$FA8E` runs `LDA $03F3; EOR #$A5; CMP $03F4; BNE ...` - so the power-up
path is taken when **`$03F4` != `$03F3` XOR `$A5`**. The `$55 $55` poke works
because a freshly filled page leaves `$03F4` = `$FF` while `$55 XOR $A5` is
`$F0`, and **it is that inequality the port has to guarantee**, not the two
`$55`s. MII's own poke is `mii_emu/src/mii.c:785-789`. Writing the pattern and
then asserting the mismatch is one line and makes the fill's choice
irrelevant; assuming the `$55`s are the signature is a machine that cold-boots
or does not depending on what happened to be in one byte.

**THE `FF FF 00 00` FILL IS A CHOSEN DETERMINISTIC APPROXIMATION and is named
as one.** AppleWin's corresponding pattern additionally **randomises offsets
`$28`/`$29`/`$68`/`$69` in every 512-byte block**, and it offers other patterns
besides (`Memory.cpp:2340-2358`); the three compatibility pokes above are
exact at `:2416-2436`. This port takes the repeating fill without the
randomisation **on purpose** - a deterministic power-on is what makes
`a2cputest` and the screendumps reproducible, and a program that depends on
uninitialised RAM is depending on a machine nobody can reproduce either. The
cost is that AppleWin's randomised bytes are the ones that break such a
program *visibly*, and here it would break quietly.

---

## 5. The soft switches

### 5.1 The II+ subset, and the fence

`$C000-$C0FF` is answered by `a2io.c` in **both directions**, and **MOST of
these addresses are RANGES rather than single bytes**: a II+ decodes only the
low four bits of the page, so `$C000-$C00F` all read the keyboard,
`$C010-$C01F` all clear the strobe and `$C030-$C03F` all toggle the speaker
(`AppleWin/source/Memory.cpp:564-566`, `:579-584`, `:636-650`). Code in the
wild uses the aliases - `LDA $C030` and `LDA $C03F` are the same click - so
the dispatch is on `addr & 0x0F` within each block and not on an equality.

**READS AND WRITES BOTH REACH A SOFT SWITCH, AND THAT IS NOT THE SAME AS
"EVERY READ HERE IS SIDE-EFFECTING", WHICH IS FALSE.** This document said the
stronger thing and it is wrong: the keyboard latch read at `$C000-$C00F`
REPORTS the waiting key without consuming it - `$C010` is what consumes it -
and the button and paddle reads at `$C061-$C067` report input and timer state
without changing either
(`AppleWin/source/Keyboard.cpp:417-434`, `Joystick.cpp:679-690`). What is true
is the thing the C side has to be built for: **a read may be a WRITE**, at
`$C010-$C01F`, `$C030-$C03F`, `$C050-$C05F` and `$C070-$C07F`, so a C
implementation that treats a read as pure gives a machine that boots to `]`
and never changes video mode (section 5.3).

| address | read | write |
|---|---|---|
| `$C000-$C00F` | keycode OR (waiting ? `0x80` : 0). **Reporting, not consuming** | - |
| `$C010-$C01F` | clear the strobe, then answer the floating bus | clear the strobe |
| `$C030-$C03F` | toggle the speaker, answer the floating bus | toggle the speaker |
| `$C050` / `$C051` | TEXT off / on | same |
| `$C052` / `$C053` | MIXED off / on | same |
| `$C054` / `$C055` | PAGE2 off / on | same |
| `$C056` / `$C057` | HIRES off / on | same |
| `$C058`-`$C05F` | **AN0-AN3, the four ANNUNCIATOR outputs**, even address off and odd on. State-only: nothing in this port reads one back | same |
| `$C061` / `$C062` / `$C063` | `$80` when the game button is DOWN. **Three digital inputs, not two** | - |
| `$C064-$C067` | bit 7 set until the one-shot's deadline passes. **Reporting: a read does not restart the timer** | - |
| `$C070-$C07F` | arm the paddle one-shots - **and only the ones that are not still running** | same |
| `$C0E0-$C0EF` | the Disk II controller (wave 6, section 14) | same |

`$C100-$CFFF` answers `$FF` where nothing is mapped.

**AN0-AN3 EXIST ON A II+ AND THIS DOCUMENT PUT THEM IN THE ABSENT LIST.** They
are the annunciator outputs on the game connector, toggled by a read or a
write at `$C058-$C05F` (`AppleWin/source/Memory.cpp:667-682`, `:876-886`).
**Only the DHIRES READING of `$C05E`/`$C05F` is //e**, and that is the part
section 10.4 is right to exclude. They are **state-only** here: this machine
has nothing on the game connector, so what the port owes is that the four bits
move and that a program writing them is not answered by the unmapped `$FF`
path. They are wave 2's, with the rest of the switch file.

**The //e switches that really are absent** are `$C000-$C00B` ON WRITES
(80STORE, RAMRD, RAMWRT, ALTZP and the rest), `$C00C-$C00F` (80COL,
ALTCHARSET) and the status READS at `$C011-$C01F`
(`Memory.cpp:2532-2547`, `:579-605`, `Video.cpp:188-205`). apple2emu registers
the `$C011-$C01F` status switches over that whole range **unconditionally**
and is wrong for this machine.

**THE II+ FENCE.** `$C010-$C01F` on a II+ is nothing but "clear the strobe,
then answer the floating bus". apple2emu registers the //e status switches
over that whole range **unconditionally** and is wrong for this machine.

**Not on this machine, and NOT greyed either** (section 10.4): `$C000`/`$C001`
80STORE, `$C00C`/`$C00D` 80COL, `$C00E`/`$C00F` ALTCHARSET,
`$C05E`/`$C05F` DHIRES.

**THE PADDLE TRIGGER DOES NOT RESTART AN ACTIVE TIMER.** A read or a write
anywhere in `$C070-$C07F` arms the four one-shots
(`Memory.cpp:753-756`, `:783-786`), but AppleWin leaves each timer that is
**still running** exactly as it is (`Joystick.cpp:725-729`) - so a program that
strobes the trigger inside its own count loop, which is what every
`PDL()`-style read does, must not be handed a timer that never expires. And
the scale is **`2816/255` ~= 11.04 emulated cycles per unit**
(`Joystick.cpp:677`, `:703-705`), not exactly 11; MII's `value * 11`
(`mii_emu/src/mii_analog.c:69-77`) is an approximation and is named as one
here rather than copied as a fact. This port's paddles answer centre (section
10.3), so the deadline is a CONSTANT and **wave 2 writes it: 127 x 2816 / 255
= 1,402 emulated cycles**, computed once rather than on every arm. The clock it
is measured against is `a2_now()` (section 3.4), and that routine exists for
this and for nothing else: the arm at `$C070` and the read at `$C064` are both
reached from INSIDE `a2_run`, where a per-slice counter kept in the C has not
moved yet.

### 5.2 The floating bus is refused, with the arithmetic

An unmapped read answers **`$FF`**, which is apple2emu's own posture. AppleWin's
true floating bus needs a cycle-exact video scanner - **262 x 65 = 17,030
cycles a frame and eighteen bit extractions on every unmapped read** - to
compute a byte almost nothing reads, and it is the one place a bug is
invisible.

### 5.3 Reads with side effects are the shape that boots and is wrong

`$C010-$C01F`, `$C030-$C03F`, `$C050-$C05F` and `$C070-$C07F` are
**read-ACTIVE**: a read there does what a write there does. The C64's plumbing
already calls out for both directions, so this is a free fit - but **a C side
that treats a read as pure gives a machine that boots to `]` and then never
changes video mode.** That is `a2cputest` row 8, and it is not optional.

**`$C061-$C067` IS NOT IN THAT LIST and used to be.** The button and paddle
reads REPORT state - the button's level, and whether the one-shot's deadline
has passed - and change nothing (`Joystick.cpp:679-690`). Reading `$C064` does
not restart a timer any more than reading `$C000` consumes a key; the trigger
is `$C070-$C07F` and section 5.1 is where it says so.

### 5.4 Every video switch is guarded BY VALUE

A write that sets a flag to the value it already held marks nothing. The C64
measured **25 forced full-width blits, ~234 ms**, for setting both the
source-changed and glass-unknown flags on a register write that changed
nothing. A mode change that actually changes the renderer marks the whole
frame dirty; one that does not marks nothing.

---

## 6. The keyboard

### 6.1 The byte map

Transcribed from AppleWin's `asciicode[3][10]` row 0 and its fold code.

| PC key | Apple II+ byte | note |
|---|---|---|
| Left arrow | `$08` | which is why BkSp, Ctrl+H and Left all deliver `$88` with no conflict |
| Right arrow | `$15` | |
| Up arrow | dropped | **a II+ has no up arrow** |
| Down arrow | dropped | **a II+ has no down arrow** |
| Select, Print, Execute, Snapshot, Insert | dropped | zero entries in the table |
| Delete | dropped | **a II+ has no DEL** |
| `a`-`z` | folded to uppercase, unconditionally | Caps Lock is always on on a II/II+ |
| backtick, `{`, `\|`, `}`, `~`, DEL, everything above `$7F` | **REJECTED**, not translated | |

A zero entry means **DROP the key**, not "send zero".

**The Ctrl folds are routed on SCAN, never on ascii** - Ctrl+H, Ctrl+I and
Ctrl+M - because the BIOS has already folded them by the time an ascii code
arrives.

### 6.2 The latch and the strobe

`$C000` reads the keycode with bit 7 set while a key is waiting. `$C010`
clears the waiting flag and answers the floating bus. Nothing else.

### 6.3 The chords

| chord | what it does | why this chord |
|---|---|---|
| **Ctrl+F2** | Ctrl-Reset | AppleWin's own, scan `0x5F`, in the classic non-enhanced set an 83-key XT BIOS delivers |
| **Ctrl+F3** | Open-Apple-Ctrl-Reset | scan `0x60`, same set. **On a II+ its body is not a //e chord carried over**: what Open-Apple-Ctrl-Reset does on a //e is force a COLD start, and on a II+ the Autostart Monitor decides that from `$03F4` != `$03F3` XOR `$A5` (section 4.5) - so the honest body is to break that equality and take the ordinary reset, RAM intact, which is what makes it a different row from `Power On`. **AND IT HOLDS PB0 ACROSS THE RESET**, which is the chord's own name read in II+ terms: what a //e calls Open-Apple IS the PB0 input on this machine, so a program that samples `$C061` in its start-up sees it held. The poll runs at the TOP of the wake and the reset service after it, which is the order that makes that survive |
| **Alt+Enter** | full screen, both directions | **AppleWin's own chord**, out of `help/keyboard.html`. An Apple II+ has no Alt key, so it collides with nothing |
| **Ctrl+F** | full screen | SPEC.md 11.2.1's unconditional door, kept |
| **F1** / **F2** | the II+'s **game buttons PB0 and PB1** (`$C061`/`$C062`), read as LEVEL through `os88_key_down` | a departure from AppleWin's Left-Alt / Right-Alt, recorded with its reason: Alt+Enter is this port's fullscreen chord. **They are HOST CONVENIENCES and not keys of the machine**: "Open-Apple" and "Solid-Apple" are //e KEYBOARD keys (`AppleWin/help/keyboard.html:29-44`) and a II+ has none - what it has is three digital inputs at `$C061-$C063` (`Memory.cpp:723-726` dispatches all three to `JoyReadButton`), of which PB0 and PB1 are the two a game reads. **PB2 is not a third convenience: it is the SHIFT-KEY MOD**, and it is a fact about the machine rather than a host choice - see below |
| **Esc** | **NOTHING. It is the Apple's own key and goes to the machine.** | **SPEC.md 11.2.1's stated exception, taken the way `C64-SPEC §9.8` takes it.** 11.2.1 binds the bare letter `f` and **Esc** to enter and leave a fullscreen surface - *"the key that got you there is the key that leaves"* - and this machine **owns both**. Esc is a key on an Apple II+ keyboard and the ROM reads it: the Autostart Monitor's **ESC-I / ESC-J / ESC-K / ESC-M** move the cursor, and the Applesoft screen editor reads the same four, so a port that swallowed Esc is a machine whose screen editor does not work. `f` is a letter, and every letter goes to the machine. **So Ctrl+F and Alt+Enter above are the WHOLE door**, and they are the reason this exception pays for itself: `C64-SPEC §9.8`'s own sentence is *"that exception only pays for itself if the chord is IMPLEMENTED"*, and both of ours are, in RESIDENT code, from wave 1 |

**Alt+Enter is the C64 precedent read correctly.** The C64's Alt+D is VICE's
OWN hotkey out of `data/hotkeys/hotkeys.vhk`, so the precedent is *keep the
ORIGINAL's chord*, not *keep the other port's key*.

**`$C063` IS THE SHIFT-KEY MOD, AND IT IS ACTIVE WHEN SHIFT IS *UP*.** This
is the one of the three digital inputs that is a fact about the II+ and not a
host convenience, and wave 3 shipped it as the inverse of its own default
state - a flat 0 - with `Memory.cpp`'s dispatch table cited for a behaviour
that lives one call down. The authority is `JoyReadButton` case `0x63`
(`AppleWin/source/Joystick.cpp:640-651`): on a II/II+ with no joystick it is
`pressed = !(GetKeyState(VK_SHIFT) < 0)`, cited in AppleWin's own comment to
Sather, *Understanding The Apple II* p7-36. So a program probing the mod on a
stock II+ reads `$80` with Shift up and `$00` with it held, and that is what
this machine answers.

**It is polled only once a program has ASKED.** Shift is two `os88_key_down`
calls (left and right), 93 us a wake on the target, for a level almost nothing
reads - so the first `$C063` read latches `a2_btn2_want` and the poll starts
there. `a2_btn[2]` is born 1, because Shift up IS the resting state, so even
the read before any poll answers what the reference answers.

**A BUTTON IS A LEVEL AND IS POLLED ONCE A WAKE**, not read across the bridge
per emulated access. A game reads `$C061` in a loop and asks whether the
button is down NOW, so `a2_kbd_poll` asks `os88_key_down` twice at the top of
each wake and the soft switch answers from the cache: two bridge crossings a
wake against two per emulated read. The map is **advice, not an oracle**
(section 6.4) and that is exactly the right shape for a level - a break code
the ISR missed leaves the button held until its next press, which is what a
real button that stuck would do.

**Ctrl-Reset also has a menu item**, which is the guaranteed route. **The
fullscreen chord must be implemented in RESIDENT code**, because a `WF_FULL`
window has no menu bar and a chord that had to load `APPLE2.OVL` would refuse
on a bar the user cannot see.

**AND THE FULLSCREEN CHORDS LAND IN WAVE 1, NOT WAVE 3** - in the wave that
makes `Machine > Toggle Fullscreen` live, because they are the ONLY way back
out of it. `kernel/wm.inc` draws no chrome at all for a `WF_FULL` window, so
the menu item that got the user in is not on the glass any more; wave 1
originally shipped the item live with the chords scheduled for wave 3 and
`a2_key()` dropping every key, and driving QEMU found a fullscreen window that
ignored `f`, `F`, Esc, Ctrl+F and Alt+Enter alike. SPEC.md 11.2.1 is binding
on this, and `C64-SPEC §9.8`'s own paragraph says the same thing one machine
along: *"that exception only pays for itself if the chord is IMPLEMENTED"*.
The pair sits at the TOP of `os88_onkey`, ahead of the About panel's modal
swallow, because 11.2.1's door is unconditional.

**Ctrl+F is the one that did not have to wait for iron.** It arrives as ASCII
6 from every BIOS, which is what makes it testable here; Alt+Enter arrives
with ascii 0 and a scan code that is `0x1C` in the classic 83-key set and
`0xA6` in the enhanced one, so **both are accepted** rather than one being
guessed. **The Ctrl-Reset chords are confirmed on iron in wave 7** (section
16.5) - QEMU's SeaBIOS passes enhanced codes a real AT BIOS drops, so a chord
table filled in under QEMU describes a different BIOS - and Alt+Enter's own
scan is re-checked there.

**What Ctrl+F costs the machine is stated**: from wave 2 the Apple cannot be
sent Ctrl-F. That is 11.2.1's price and it is paid here rather than at the
door, because the alternative found on the glass was a window with no way
out.

### 6.4 `os88_key_down`, and the one rule about arming it

`OSAPI_KEY_DOWN` (SPEC.md 9.7) is used as it stands, wrapped as
`os88_key_down()` - the thunk the C64 port added.

**ARM IT ONCE, IN `os88_main`, WITH THE ANSWER IGNORED.** The first call
clears and arms the map and always answers up; arming it from the first slice
erases the make `os88_onkey` has already seen, which is the first key of the
session, silently lost.

The map is **advice, not an oracle** (SPEC.md 9.7): a key whose break code the
ISR missed stays down until its next press.

### 6.5 Copy and Paste

Both go through the system clipboard (SPEC.md 55), and **both bodies run from
the TOP OF THE WAKE with no lock held** - the C64's wave-3 lesson, where the
same commands cost 2,000 bridge crossings and a call-counting cost model
charged one.

**Copy** walks the 40x24 text page through the interleaved row bases,
converts the Apple screen encoding to ASCII through one 128-entry table and a
mask, **one call a row** through `a2_copy_row` in assembly, into a **transient
heap claim** - not bss. The C64 gave back 3,074 bytes of bss doing exactly
this.

**THE MASK IS THE WHOLE OF THE INVERSE/FLASH/NORMAL QUESTION.** An Apple II+
character generator holds 64 glyphs and the screen byte's top two bits pick
the ATTRIBUTE rather than the character (section 7.3), so `and $7F` collapses
inverse and flashing onto the normal codes and `a2_astab[128]` folds what is
left: `$00-$1F` and `$40-$5F` are `@A-Z[\]^_`, ASCII `$40-$5F`; everything
else is ASCII `$20-$3F` unchanged. What the user sees flashing and what the
clipboard gets are the same letter, which is what a listing copied off a `]`
prompt has to be. The table is built in `os88_main` and is RESIDENT: 128
bytes of bss and a nine-line loop against a `.data` array paid for twice, on
the floppy and in the region.

**The row separator is CR** (`$0D`), which is what this OS's own clipboard
readers store - SPEC.md 27.6 folds CR LF and a lone LF onto it - and what
Paste feeds the Apple, so a Copy pastes back byte for byte. Trailing spaces
come off each row: an Apple text row is space-padded to forty cells and a
listing pasted elsewhere must not be.

**Paste** types a listing into the keyboard latch on apple2emu's
peek-and-consume handshake (`src/keyboard.cpp:176-215`): `$C000` PEEKS the next
byte and presents it with bit 7 set, `$C010` CONSUMES it and the next `$C000`
read presents the next byte, `\n` folds to `\r`, `\0` ends the paste. **The
emulated program sets the rate, so there is no pacing state at all** - and a
byte stands at the latch for exactly as long as the machine has not taken it,
which is the one shape that cannot overrun the machine's input
(PERFORMANCE.md's third emulator-invisible defect). The strobe advances on a
WRITE as well as on a read (section 5.1), or a program using `STA $C010` would
type the first character for ever.

**IT PRESENTS ONLY INTO A FREE LATCH, AND THAT ONE COMPARE IS TWO FIXES.**
`a2_paste_peek` is called on EVERY `$C000` read while a paste is in flight -
not once per byte the machine takes - and wave 4 had it rewrite the latch
unconditionally. So **a key the user typed during a paste was destroyed**:
`a2_kb_put` set the latch and the machine's next read overwrote it before
returning, which put **Ctrl-C - the only in-machine way to stop a runaway
paste of up to 2 KB - permanently out of reach**, leaving the Machine menu as
the only escape. And the cost claim was wrong with it: Applesoft's `ISCNTC`
polls `$C000` between statements and does **not** strobe `$C010` unless the
byte is Ctrl-C, so a `RUN` with a queue still in it paid the guard +
`a2_paste_peek` + `os88_peek` - ~33 us on the target - **per emulated poll**,
for a queue that could not advance. `if (a2_kb_ready) return;` closes both:
the queue's byte is still presented and `a2_paste_i` has not moved, so nothing
is lost.

**AND THE GUARD IN FRONT OF ALL THREE ARMS IS A DATA TEST, NOT A CALL.** It
was `a2_paste_live()`, and `cc8086` emits a real near call for it - a `bp`
frame, two memory compares and a `ret` - on **every `$C000` read, every
`$C010` read and every `$C010` write**, whether or not a paste exists. An idle
Apple II at `]` does nothing but poll `$C000` (the Monitor's KEYIN loop is one
read per ~15 emulated cycles) and every echoed keystroke strobes `$C010`, so
that was a permanent per-emulated-cycle tax on every session - charged
straight out of the per cent the status row reports - for a feature most
sessions never use. `a2_paste_seg` is declared in `a2io.c` beside `a2_kb_put`
and `a2_paste_up`, and the three arms test it directly: **one compare against a
word of DS, and zero calls when nothing is pasting.** What makes that legal is
`a2kbd.c`'s own invariant - `a2_paste_take` calls `a2_paste_stop` the moment
the last byte is consumed, so `a2_paste_seg != 0` and *a paste is in flight*
are the same fact and there is no second state to keep in step. `a2_paste_live`
is gone rather than left unused.

**AND THE STROBE THEN NEEDS TO KNOW WHOSE BYTE IT IS.** Once the latch can
hold something that is not the queue's, `$C010` can no longer assume it is
consuming a pasted character - without `a2_paste_up` (one byte, declared in
`a2io.c` beside `a2_kb_put`, which is the one place that clears it) every key
the user typed during a paste would swallow one queued byte on the strobe
behind it. `a2_paste_take` advances only while it is set.

**THREE FOLDS, AND EACH IS ONE LINE.** `\n` to `\r` is apple2emu's own
(`:181-183`); **a CR LF pair becomes ONE `\r`**, so a listing copied on a
DOS-line-ending machine types one RETURN a line and not two; and **lower case
folds to upper as `a2_key` folds it** (section 6.1), because a II+ keyboard
has no lower case at all and Applesoft would answer `?SYNTAX ERROR` to every
line of a lower-case listing.

**BOTH COMMANDS ARE A LATCH, AND THE WORK IS SPLIT BY FREQUENCY.**
`os88_oncmd` is dispatched under the desktop's gfx lock, so `ovl_a2_cmd` sets
one word and `ovl_a2_clip_service` (`a2kbd.c`) spends it from the top of the
next wake with no lock held and before the slice - between the pick and the
work not one emulated cycle has run, so the page copied is the page on the
glass.

**AND THE ONCE-PER-PICK HALF IS `ovl_`.** `a2_clip_service` and
`a2_copy_screen` were RESIDENT while `a2kbd.c`'s header said they were not -
about 170 x86 instructions of image for the claims, both clipboard calls, the
row walk and all six refusals, none of which runs more than once per menu
pick. They are legally `ovl_` because the wake is a UI-task context holding no
lock and the latch can only have been set through `ovl_a2_cmd`, so the module
is already resolved; a 0 from the service means the runtime refused the module
and the wake says so once. The per-byte and per-`$C000`-read halves -
`a2_paste_peek`, `_take`, `_stop` - stay resident and must.

**THE MEASURED COST OF A WHOLE-SCREEN COPY IS 31.1 ms** (`a2uitest`'s own row):
24 `a2_zcopy_out` + 24 `a2_copy_row` + one `os88_clip_put_seg`, one bridge
crossing. The per-row read is `tests/a2band`'s measured 0.314 ms; the per-cell
term, 23.6 us, is DERIVED from the 8088's instruction-fetch floor over the
loop as written (PERFORMANCE.md Part 2's `max(clocks, 4.34 x bytes)`) and is
labelled as derived in `a2uitest.c`, because `a2mem.inc` is not in that bench's
`%include` list.

**Copy's staging claim is 1KB and Paste's queue is 2KB.** Copy's bound is its
own - 24 rows of at most 40 folded cells and a CR is 984 bytes - and Paste's is
what ONE `os88_clip_get` can be given, because `OSAPI_CLIP_GET` has no offset
and so cannot be read in chunks; a longer clipboard is pasted as far as it fits
and `Pasting 2048 bytes only.` says so with the number in it. Copy's claim is
freed inside its own wake; Paste's is held for exactly as long as there are
bytes left to type, and **a reset empties the queue and frees it** - after a
Power On it is not that machine any more.

Paste is the **primary way a BASIC program gets in** before Disk II exists.

### 6.6 The keyboard-mouse rule

`C64-SPEC §7.6`, unchanged: on a machine with no mouse the kernel eats the
arrows, Space and Del as its pointer, and ScrollLock hands them back. The port
**infers this from the observable** - the down-map says an arrow is held and
`os88_onkey` has never once delivered one - over three consecutive polls, with
a one-way latch.

**IT MATTERS MORE HERE THAN ON THE C64**, and that is worth saying rather than
inheriting. On the C64 those four keys are a JOYSTICK; on this machine LEFT
and RIGHT are keys of the Apple II+ itself (`asciicode` row 0 gives `$08` and
`$15`, section 6.1) and **SPACE is the key a person typing BASIC presses most
often after the letters**. A machine whose space bar moves the desktop pointer
is a machine you cannot type at, and nothing on the glass would say why.

**THE SDK CANNOT BE ASKED "HAS A MOUSE SPOKEN"** (`C64-SPEC §7.6`'s own
finding): `os88_mouse()` answers x, y and the button and nothing else, and
adding a slot for it would spend kernel headroom, which is a decision and not
a build fix. So the package asks a question it CAN answer and that has the
same answer - `kbm_key` (`kernel/mouse.inc`) intercepts one of those keys when
and only when no mouse has spoken AND ScrollLock is off, and an intercepted
key never reaches `os88_onkey`.

**THREE CONSECUTIVE POLLS AND FOUR TICKS**, because a wake posted BEFORE a
press is dispatched ahead of the key event behind it, so a poll can
legitimately see the ISR's bit with the `W_ONKEY` still queued. **The POLL
COUNT ALONE WAS NOT ENOUGH and QEMU caught it**: a wake runs about 1,400 times
a second on a fast host, so three consecutive polls span about two
MILLISECONDS - far inside that gap - and the first Space of `PRINT 6*7` put
this message in the status row of a machine that HAS a mouse and was eating
nothing. So the window is measured in TICKS: `A2_KBM_TICKS` is 4, ~220 ms,
which is longer than any keystroke takes to come back round as an event and
shorter than a press somebody is holding to move a pointer with. The poll count
stays as the cheap half. **The latch is ONE WAY** - a kernel that has delivered
one of those keys is not going to start eating them - and the message is said
**once a session** (SPEC.md 47):

```
ScrollLock: arrows, Space.
```

25 cells of the status row's 26, which is what the message-length gate
(section 9) is for.

---

## 7. The screen

### 7.1 The window

| | |
|---|---|
| content | **336 x 218** - the Apple's 280 letterboxed **16 px left and 24 px right** so the composer's OUTPUT stays byte-aligned even though its cells are not, plus 192 scan lines, plus an 8 px border top and bottom (16), plus a 10 px status row |
| frame | **content + 2** - `os88_wm_create` authors a FRAME and `os88_wm_geom` answers the CONTENT box, and the window's two 1-pixel side borders are the difference (`C64-SPEC §9.1`) |
| origin | `os88_wm_snap` puts the content x on a cell boundary, which is what lets `OSAPI_GFX_SCROLL` accept the rect |
| height | **asked against `os88_video().dock_top`**, on `apps/c64/c64.c:1684`'s shape. A 200-line desktop cannot give 218 |

**THE CURSOR ANCHOR, and it is a correctness requirement rather than a
preference.** On a 640x200 desktop `dock_top` is 176, the content box is 137
tall and only **111 of the Apple's 192 scan lines fit**, so *which* 111 is a
question the port has to answer. The visible band **holds the character row
the Apple's own cursor is on** - `CV`, zero page `$25`, read as RAM - and
moves only when that row leaves it. **Machine > Toggle Fullscreen** is the
stated route to more of the frame - the bar has no submenu mechanism at all
(section 10.1), so there is no `Video` menu for it to hang under and the row
is folded into Machine - and even at full screen a 200-line adapter cannot
show all 192 lines, which is why the anchor is not a fullscreen-only concern.

**IT WAS A FIXED BOTTOM ANCHOR AND THAT MADE THE PORT LOOK DEAD ON CGA.** The
reasoning that shipped was *"the `]` cursor line and MIXED's four text rows are
both at the bottom"*, and the first half of that is **false of a machine that
has not scrolled yet**: the Autostart ROM's cold start prints `APPLE ][` on row
0 and leaves the cursor on row 2, so with `a2_gl0` pinned at 81 the glass began
at character row 10 and every pixel of a freshly booted screen was composed,
blitted and landed **off the band**. The window was BLACK on launch, `PRINT
6*7` answered into pixels nobody could see, and the status row went on reading
`TEXT` and 2,000% beside it - a defect with no wrong pixel in it anywhere. It
took thirty Returns to walk the prompt down to row 23 before one character
appeared. Wave 3's own CGA evidence looked fine for the same reason its
`scrolled` shot did: it had run a `FOR` loop first.

**WHEN IT MOVES, THE CURSOR ROW GOES TO THE BOTTOM OF THE BAND**, whichever
way the cursor went, because what a reader wants beside the cursor is the
HISTORY and that is above it. Anchoring the row at the TOP is the obvious other
spelling and it hides the banner one row above the prompt - photographed doing
exactly that before the clamp went in. The same arithmetic gives "line 0, show
everything" for free on a cold start, and walks to 81 - where the old code
started - as soon as a session has scrolled the cursor to row 23, and stays.

**IT MOVES RARELY, AND IT COSTS A FULL BAND REPAINT WHEN IT DOES** (~290 ms on
the target for the 14 visible rows): the frame shadow is indexed by APPLE scan
line, so a moved anchor puts every one of them at a different screen y and
`a2_sh_inval` is the honest answer. An ordinary session pays it about once.
`HOME : VTAB 20 : PRINT` in a loop pays it twice an iteration, which is the
price of showing the user what they typed, and the shape that pays it is a
program the reader is not typing at.

**IT IS A TEXT-MODE RULE.** There is no cursor in lo-res or hi-res, and in
MIXED the interactive part is the four text rows at the bottom, so both keep
the bottom anchor - which for MIXED is the same row range the cursor would have
chosen. `a2_v_text` is the gate, and a band that holds the whole frame never
reads `$25` at all.

The flush reads the **live** content box every time it runs: the status row is
at `content.y + content.h - 10` whatever that is, the scan lines drawn are
what is left between the borders, and the bottom border fills between the last
drawn line and the status row.

**THE LETTERBOX IS PART OF THE BAND AND NOT A FILL** (wave 1's measured
correction to the flush's step 5 below). A band row is 40 bytes; the composer
writes bytes 2..36 and NEVER touches bytes 0-1 and 37-39, the band buffer is
bss and the loader zeroed it, so the letterbox is dark, travels inside every
blit and every compare, and costs no second code path and no second decision.
The `border` fills are the 8-pixel frame OUTSIDE the 320-pixel band, and they
are what step 5 means.

### 7.2 The address maps, and why they are the real work

C64 rows are linear, which is what makes "window intersect pages -> rows" two
divisions. **Apple II rows are interleaved**, so a dirty 256-byte page maps to
a **scattered set of scan lines**, not a contiguous run, and the write window
narrows a **span within a line** rather than a row range.

```
text_map[i]  = 1024 + 256*((i/2)%4) + 128*(i%2) + 40*((i/8)%4)      (page 1)
             = the same + 1024                                      (page 2)
hires_map[i] = 0x1C00 + text_map[i]
scan line    = hires_map[row] + 1024*sub          sub = 0..7
```

**48 words of `cs:`-resident table for both pages**, read-only, exactly like
the C64's `c64_dmask`. Not the 192-entry table a first reading suggests.

### 7.3 The composers - three of one class

`a2band.inc` holds all three, and **all three are new work**.

| routine | composes | shape |
|---|---|---|
| `a2_band_text` | 40 x 24 cells of 7 x 8 | the 7-bit shift accumulator, one decoded glyph and one **per-cell XOR mask** |
| `a2_band_lores` | 40 x 48 blocks of 7 x 4 | `c = (byte >> ((line/4 & 1)*4)) & 0xF`, through a 16-entry luminance ladder |
| `a2_band_hires` | 280 x 192 mono | 40 source bytes -> 35 output bytes a scan line through the 128-entry 7-bit reverse table, **high bit dropped**, EIGHT scan lines a call |

All three share **ONE 7-bit shift accumulator and one span argument** - and
**they share it LITERALLY**, which wave 3 made true rather than aspirational:
phase C is `a2_pack`, one near routine the three of them CALL, so "one class"
is a fact about the image and not a family resemblance. Phase B is the only
thing that differs, and all it decides is which seven-bit value each cell
contributes: a masked glyph row, a luminance block, or a reversed hi-res byte.
Stride
is the assemble-time constant **40** and the rows are unrolled
(PERFORMANCE.md Set 64's lesson). Beside them: `a2_rowspan` (two `repe cmpsb`,
the second with DF set), `a2_rowcopy`, `a2_rowsig`, `a2_rowflash`, `a2_x2init`
and `a2_band_x2`.

**THE SPAN ARGUMENT IS A GROUP SPAN AND THE GROUP IS EIGHT CELLS** (wave 1,
and it is arithmetic rather than a choice): eight cells of seven pixels is
fifty-six bits is SEVEN BYTES exactly, so a group is byte-aligned in the
output at both ends and a group span needs no masking anywhere. A one-cell
poke therefore composes one group - seven output bytes - which is what the
write window narrows to. The signature is

```
void a2_band_text(unsigned char *dst, int g0, int g1,
                  unsigned mseg, unsigned moff, int fmask);
```

and the other two take the same five arguments and no sixth, because only text
has a flash phase:

```
void a2_band_lores(unsigned char *dst, int g0, int g1,
                   unsigned mseg, unsigned moff);
void a2_band_hires(unsigned char *dst, int g0, int g1,
                   unsigned mseg, unsigned moff);
```

**`moff` IS THE ROW'S FORTY BYTES IN TWO OF THEM AND SCAN LINE 0 IN THE
THIRD.** Text and lo-res read one forty-byte range; hi-res reads EIGHT of them
`$400` apart, and `a2_band_hires` walks that stride itself off the one address
it is handed - so the caller has one row base per row whatever the mode, which
is what keeps the flush's row loop one loop.

where `fmask` is the FLASH PHASE as the mask a `$40-$7F` cell takes right
now - `0x00` while flashing text shows its normal form, `0x7F` while it shows
its inverse. That one byte is the whole of flashing (section 7.6): there is no
second mechanism and no second code path.

**And the packing is where a transcription goes plausibly wrong**, so it is
written out: `out[j] = (g[j] << (j+1)) | (g[j+1] >> (6-j))` for `j = 0..6`,
which is the high byte of `((g[j] << 8) | (g[j+1] << 1)) << (j+1)` - the
seven-bit stream and the eight-bit byte differ by one place, and the pre-shift
of the SECOND operand is where that difference is paid.
`hosttest/a2memtest.asm` checks two of those bytes against hand-computed
values for exactly this reason.

**The luminance ladder is SIXTEEN BYTES AND ONE CONSTANT A COLOUR** (wave 3's
measured correction to the draft's `{and, xor}` pairs). A pair per entry is
what a composer that had to preserve an underlying pattern would need; on
**this** path a block is UNIFORM - the windowed renderer is monochrome by
LUMINANCE, so each colour is seven lit pixels or seven dark ones - so its
contribution is one seven-bit constant, `0x00` or `0x7F`, and `a2_band_lores`
is one table read a nibble.

**And "uniform" is this port's decision, not a fact about monochrome lo-res**,
which is worth saying because it is the sentence a later wave would reason
from. MII's own monochrome arm (`src/mii_video.c:567-586`, inside the range
section 2 cites for the lo-res cell rule) does the opposite: it takes the
4-bit colour, `reverse4`s it, replicates it (`c |= c<<4; c |= c<<8`),
phase-shifts odd columns (`if (x & 1) c >>= 2`) and emits a per-pixel DOT
PATTERN, which is what a real II+ on a mono monitor shows - `COLOR=5` is
`%0101`, alternating pixels, not solid white. **This port deliberately does
not do that on the windowed path**, and the reason is arithmetic: MII's
pattern is a 14-pixel cell carrying the NTSC phase, and a 7-pixel 1bpp cell
cannot carry it - half of it is not a dimmer version of it, it is a different
colour. The 16 colours arrive with the foreign video mode (section 13).

**THE PALETTE IS ONE THE REFERENCE ACTUALLY DISPLAYS**, and that is the whole
of why it is MII's. This ladder first shipped off AppleWin's
`PaletteRGB_NTSC` lores block (`source/RGBMonitor.cpp:149-164`) - and the
line immediately above that block, `RGBMonitor.cpp:148`, reads *"Note: this
is a placeholder. This palette is overwritten by VideoInitializeOriginal()"*.
It is: `VideoInitializeOriginal` (`RGBMonitor.cpp:1186-1196`) `memcpy`s
sixteen NTSC-generated colours over exactly that block, and `NTSC_VideoInit`
(`NTSC.cpp:2366-2368`) calls it at start-up, so **AppleWin never puts those
literals on a screen**. Nor can they simply be replaced with the ones that
overwrite them: `GenerateBaseColors` (`NTSC.cpp:2697-2721`) runs a
signal-level NTSC simulation - sixteen phases per colour, averaged - rather
than listing values, so there is nothing to transcribe.

So the definer is **MII's `palettes[0]`, "Color NTSC"**
(`src/mii_video.c:94-113`) - a live table read straight into the CLUT MII
renders through - taken through **MII's own lo-res mapping**,
`mii_base_clut.lores[0]` (`src/mii_video.c:173-177`). *That second half is
load-bearing*: MII's `CI_*` enum is not in Apple colour order (`CI_PURPLE` is
1, and lo-res colour 1 is MAGENTA), so a table read by enum index rather than
through the clut is scrambled. apple2emu's `Lores_colors`
(`src/video.cpp:100-115`, the mrob.com values) is the **cross-check**: it is
indexed by lo-res colour directly, agrees with MII exactly on twelve of the
sixteen and within a few units on the rest, and **agrees on all sixteen
lit/dark decisions**.

**WHAT THE PLACEHOLDER COST WAS ON THE GLASS.** Under it purple came out at
467 per mille and medium blue at 499, against a 500 threshold, so `GR :
COLOR=3` and `GR : COLOR=6` drew **BLACK** - three of the sixteen colours
decided by one part per mille of a palette no emulator shows. Under both live
tables purple is **568** and medium blue **613**, and both are lit, which is
what every reference this port names draws.

**WHAT IS STORED IS THE RANK AND NOT THE LUMINANCE**, and that is arithmetic
rather than taste. The figure compared is Rec.601 luma - `299R + 587G + 114B`
- and a byte's own granularity is 0.39 % while the ladder has pairs closer
than that, so ANY scaling of the figures into a byte ties an ordering the
palette has. A monochrome composer asks only "is this lighter than that", so
`a2_lum[]` is the RANK, `a2_lopat[]` is derived from it at launch, and
**`tools/a2ref.py --lumcheck` is the independent gate**: it computes its own
luminances from the same RGBs, **at full precision and not in per mille**, and
requires all 256 ORDERED pairs to agree.

**THE ONE TIE IS THE PALETTE'S OWN.** Grey 1 and grey 2 are the same three
bytes (`0x9C,0x9C,0x9C`) in MII's table and in apple2emu's, so they share rank
7; nothing else in the sixteen ties. The ranks are therefore **not dense** - 8
is unused - which is correct and is what `--lumcheck` asserts.

| colour | per mille | rank | | colour | per mille | rank |
|---|---|---|---|---|---|---|
| 0 black | 0 | 0 | | 8 brown | 376 | 1 |
| 1 magenta | 378 | 3 | | 9 orange | 569 | 6 |
| 2 dark blue | 376 | 2 | | 10 grey 2 | 611 | 7 |
| 3 purple | 568 | 5 | | 11 pink | 760 | 11 |
| 4 dark green | 418 | 4 | | 12 green | 614 | 10 |
| 5 grey 1 | 611 | 7 | | 13 yellow | 815 | 14 |
| 6 medium blue | 613 | 9 | | 14 aqua | 813 | 13 |
| 7 light blue | 806 | 12 | | 15 white | 1000 | 15 |

**THE THRESHOLD IS HALF OF WHITE**, `A2_LUM_LIT` = 5: the eleven colours from
purple (568 per mille) up are lit and the five below it are dark.
`a2ref.py` applies the same 500 to its own numbers, so a disagreement about
one colour is a bit-for-bit frame mismatch rather than a matter of opinion.

**The gate is only independent if the palette is TRANSCRIBED, and only
meaningful if the palette is one that is SHOWN** - and wave 3's review is
where both halves were learned. The ladder first shipped with dark green above
brown, off an RGB - `0x00,0x80,0x2F` - that is in no reference at all:
AppleWin's placeholder dark green (`0x00,0x80,0x00`) crossed with Le Chat
Mauve Feline's (`0x00,0x83,0x2F`, `RGBMonitor.cpp:191`). `--lumcheck` was
green because it carried the same adapted byte. Transcribing AppleWin's block
exactly fixed *that* and left the deeper defect standing, because the block
itself is a placeholder; the fix for both is a live table plus an independent
one that agrees with it.

**The character generator, and why 64 glyphs cover 128 bitmaps.** Decoding the
pinned `Apple2_Video.rom` through AppleWin's own algorithm yields **128
distinct 8-byte bitmaps, not 64**: blocks `$40`, `$80` and `$C0` are
byte-identical (normal) and block `$00-$3F` is their XOR `0x7F` (inverse). The
common reading - 64 glyphs repeating every `$40` - is true only across
`$40-$FF`. **The port stores 64 glyphs (512 bytes of bss) and applies a
per-cell XOR mask**, which is the same mechanism flashing needs, so it is not
a second one. `tools/a2ref.py --romshape` asserts the 128-bitmap count, so the
decision is checkable rather than remembered.

**AND THE DECODE IS `& 0x7F` OVER BLOCK `$C0`** (wave 1, measured against the
pinned ROM). AppleWin's `NTSC_CharSet.cpp` reads `rom[i*8 + (y&7)]` and XORs
`$7F` where the entry is in the low 1KB and bit 7 is clear; reading block
`$C0` instead makes that XOR unnecessary, because those 64 characters are
already the normal form and their bit 7 carries nothing. So `a2_chargen` is a
`lodsb` / `and al,0x7F` / `stosb` loop and the whole of the inverse and the
flash is the composer's per-cell mask.

**AND THE BIT ORDER IS ALREADY RIGHT FOR TEXT AND UPSIDE DOWN FOR HI-RES.** In
the character ROM **bit 6 is the LEFTMOST pixel**, which is the framebuffer's
own order (SPEC.md 5.4.2, MSB first), so a decoded glyph enters the shift
accumulator as it stands. **Hi-res data in RAM is the other way up** - bit 0
leftmost - and is what the 128-entry reverse table exists for. They are not
the same convention, and this is the one thing about this machine most likely
to be got backwards.

**The decode and the 128-entry reverse table are built in `os88_main`,
RESIDENT, and never in the overlay.** Both are on the display path, and a disk
with no `APPLE2.OVL` must be a **program whose menus refuse**, not a window
that draws nothing. That is a named negative control (section 16.6).

**The high bit is dropped in hi-res.** Both references drop it in mono, and
the half-dot shift it causes is not expressible in a 280-pixel frame - it
needs 560, which is the foreign mode's business (section 13).

### 7.4 MIXED, PAGE2, and the mode dispatch

MIXED is the top 160 scan lines in the graphics mode and the bottom 32 in
text, in **one 40-byte-stride frame with one shadow**. PAGE2 selects the
second text page (`$0800`) or the second hi-res page (`$4000`). The mode
dispatch is guarded by value (section 5.4); a mode change that actually
changes the RENDERER or the PAGE marks the whole frame dirty.

**A MIXED FLIP IS NOT ONE OF THOSE**, and wave 3's review is where that was
paid for. Flipping MIXED inside a graphics mode moves the renderer for rows
`A2_MIXROW..23` and for **no** other row: `a2_row_mode(r)` for `r <
A2_MIXROW` never reads `a2_v_mixed`, `a2_row_base` does not move and neither
does the page. `a2_dirty_all()` there recomposed twenty rows from identical
sources with the identical composer to produce identical pixels - **504.9 ms
against 91.2** (section 7.9.3), which is the very defect the MIXED-in-TEXT
guard beside it exists to stop, one condition along on the arm where the
switch really does something. `a2_dirty_split()` marks the split's four rows
and the status row, and it marks them **WHOLE and EXPLICITLY**: the rows
arrive from the OTHER page each way - the text page on MIXED-on, the graphics
page on MIXED-off - so neither the page bitmap nor the write window can be
trusted to speak for them. It stays safe for the shift test because `a2_shsrc`
is written only for TEXT rows and the test is refused unless `a2_v_text`, and
every transition INTO `a2_v_text` moves `a2_mode_of()` and so still takes the
full arm.

**160 SCAN LINES IS TWENTY CHARACTER ROWS EXACTLY**, and that is what makes
MIXED cost nothing structurally: the split falls on a row boundary, so no row
is ever half one renderer and half the other, `a2_row_mode(r)` is a row test,
and the flush's loop needs no partial case at all. Rows 0-19 take the
graphics composer and rows 20-23 take `a2_band_text`.

**AND THE MIXED TEXT ROWS READ THE TEXT PAGE, PAGE2 AND ALL.** One switch
selects the second page for both halves on a real II+, so with PAGE2 set the
graphics half is `$4000` and the text half `$0800` - `+$2000` on one map and
`+$400` on the other, which is why `a2_row_base(r)` is one function rather
than an offset each caller adds.

**AND THOSE FOUR ROWS ARE OUTSIDE THE WRITE WINDOW'S RANGE**, which is the
one thing about MIXED that is not free. The window is taken over the LIVE
display page (section 7.5) and that is the GRAPHICS page in a graphics mode,
so it can say nothing whatever about a write to `$0400`. Widening the watch
range to cover both would span `$0400-$3FFF` - where an Applesoft program and
every one of its variables live - and the per-row intersection would then
answer "all forty cells" for every row of every flush, which is the whole
thing the window exists to prevent. So a row outside the range is marked from
the **page bitmap alone** and composed WHOLE: four rows, twenty groups, on any
flush that carries a write to the text page. `a2_scan_range`'s `watched`
argument is that arm, and a dirty scan that asked the window anyway would
answer "the window does not reach this row" for every one of those writes and
never draw them at all.

**HI-RES IS MARKED ONE SCAN LINE AT A TIME, AND THE COMPOSER HONOURS IT.** A
hi-res row group's eight lines are eight separate 40-byte ranges `$400` apart,
so `a2_dirty_scan` asks about each one and an `HPLOT` that moves a pixel marks
ONE of them. Wave 3 shipped only half of that: the flush's per-line loop
skipped clean lines for the COMPARE and then called `a2_band_hires` for all
eight anyway, so the narrowing delivered the compare and not the compose -
**2.434 ms a group where 0.30 was owed**, on every row a single-scan-line plot
crossed. `a2_band_hires` takes a **scan-line range** now (section 7.9.2) and
the flush hands it the union of the row's dirty-or-forced lines, computed in
the same pass that already walks all eight bits. **FORCED lines are in the
union**, because the `!trust` arm draws `b0..b1` straight out of the band with
no compare and a band row this flush never wrote is last flush's pixels. Text
and lo-res take no range and want none: their eight pixel rows all come out of
one source byte a cell.

**ONLY A TEXT ROW CAN FLASH.** A lo-res byte of `$60` is two colour blocks
and a hi-res byte of `$60` is three pixels; `a2_rowflash` is asked of a text
row and nothing else, or a graphics row holding bytes in `$40-$7F` - which the
ordinary picture does - would be force-composed 3.64 times a second for a
phase that changes not one pixel of it.

### 7.5 The damage model

Taken whole from `C64-SPEC §9.2`, with the mapping step replaced.

**A BLOCK MOVE MARKS THE ROWS IT REACHED, AND NO OTHERS** (`ovl_a2_dirty_range`
in `a2scr.c`). `a2_zzcopy_in` - File > Load Program's one write into the
machine - goes round the core's own write path, so it sets no page bit and no
write window and the mark has to be made by hand. Wave 4 made it with
`a2_dirty_all()`, which is `a2_dirty_split`'s own defect one call along: a
five-line listing is ~45 bytes at `$0801`, and with text page 1 or either
hi-res page on the glass **not one displayed byte moved** - yet all 192 scan
lines were marked and all 24 rows widened, so the next flush recomposed the
whole page at full width to produce identical pixels. That is the **~301 ms**
of section 7.9.1's `a mode switch that draws the same picture`, and it was
reachable at LAUNCH: a cold double-click of a `.BAS` runs the same loader, so
301 ms was the first thing that happened after the wait for `]`. The range test
is per row and per SCAN LINE for section 7.2's reason - a text or lo-res row is
one forty-byte range at `a2_row_base(r)` and a hi-res row is eight of them
`$400` apart - so an ordinary listing marks nothing, a load displayed on text
page 2 marks the rows `$0801` lands in, and a program long enough to reach
`$2000` marks the hi-res lines it reached. `a2_force_wide` is called only if
something was marked.

- **A 32-byte dirty-page bitmap**, one bit per 256-byte page, ORed on every
  RAM write from a `cs:`-resident mask table. Reads are not the hazard; only
  writes are, which is why the table may be `cs:`.
- **A write window** beside it - the lowest and highest address written since
  the last flush - **taken over a WATCH RANGE** the C sets to the live display
  page. Without the watch range the window degenerates inside one slice:
  every `JSR` writes the stack and every BASIC statement writes zero page.
- **Dirty scan lines = the window intersected with the pages, mapped through
  the interleaved arithmetic**, with both screen-hole tests and the MIXED
  split: text and lo-res hole = `(a & 0x7f) > 0x77`, `group = (a>>7)&7`,
  `gline = (a & 0x7f)/40`, `line = (group + gline*8)*8`, mark 8 lines; hi-res
  hole = `(a & 0x78) == 0x78`, mark ONE line. **A hi-res write above the mixed
  line is dropped and a text write below it is kept.**
- **AND HI-RES IS MARKED PER SCAN LINE** (wave 3). A hi-res row group's eight
  lines are eight SEPARATE forty-byte ranges `$400` apart, so the scan asks
  about each of them: one `HPLOT` writes one, and marking the row from it
  would blit eight lines where the machine changed one. Measured at **1 blit
  and 1 group** for one changed hi-res byte (section 7.9.1).
- **THE MAPPING IS DONE ROW-WARD AND NOT ADDRESS-WARD** (wave 1), which is the
  same rule read from the other end and is cheaper. MII walks an ADDRESS to a
  line through the arithmetic above; `a2scr.c` walks the **24 rows** and asks
  whether the dirty pages and the write window reach each one's forty bytes -
  two range tests a row, once a flush, against an arithmetic chain per write.
  **The screen-hole test is then implicit and exact**: the eight bytes at the
  end of each 128-byte group belong to no row's 40-byte range, so a write
  there marks nothing, by construction rather than by a test that could be
  forgotten. The hi-res map is the same walk one wave along.
- **The flush takes all of it in ONE call** - `a2_dirty_take`, ~190 us,
  resetting the scratch in the same pass. The C64 measured the alternative at
  ~50 near thunks at ~38 us each = **~1.8 ms a flush spent before a pixel was
  decided**, and invisible to a call-counting cost model.

### 7.6 The flash phase - the one thing on the glass the damage model cannot see

Flashing text **changes pixels with no memory write**. Nothing write-driven
will ever mark its lines dirty, and a flashing cursor would simply never
blink.

**AND THE WRITE WINDOW SAYS NOTHING ABOUT WHICH CELLS FLASHED**, so the flip
sets `a2_rowwide[]` FOR THE ROWS THAT FLASH and the compose span may not be
narrowed by it there. Without that,
a cursor flashing at one end of a row while the machine printed at the other
gave a group span that did not contain the cursor: the compare never looked at
it, the line was then recorded CLEAN, and the phase change was lost until the
next flip or the next write that happened to reach that cell. On the glass
that is a cursor that stops blinking exactly while a program is printing -
which is the whole of an Applesoft session. What it costs is a full-width
COMPOSE of the flashing rows on flip flushes; the DRAW stays narrow, because
the span compare still decides what is blitted.

**AND IT IS PER ROW, WHICH IS `apps/c64/c64scr.c`'s `c64_rowd` SHAPE.** One
flush-wide flag was set by the row holding the `]` and read by all
twenty-four, so a blinking cursor composed every OTHER dirty row full width
too - 2.434 ms a group it never asked for, on exactly the flushes that are
already the expensive ones. Twenty-four bytes of bss buy the narrowing back
for every row that does not flash: a flip with one flashing row and one
narrowly-written row is **6 groups** where it was 10 (section 7.9.1).

**The fix is a phase counter in host ticks.** 16 frames at 60 Hz is ~267 ms,
which is ~5 host ticks. On each flip the flush **force-dirties every scan line
whose text row holds a byte in `$40-$7F`**, through a per-line force bitmap
(24 + 24 bytes of bss). The swap itself is `+/- $40` on the screen byte, which
in this port is the per-cell XOR mask the composer already applies - so
flashing costs no second mechanism.

**On the `CPU_8086` tier the phase is refused and the item greys with the
measured cost** (section 10.3), because a forced text repaint **3.6 times a
second** is a documented number and not a guess.

**TWO CLOCKS, AND WHICH ONE EACH NUMBER COMES FROM.** The Apple's own rate is
**16 video frames**, which the references count on the VIDEO clock - MII's
`MII_VIDEO_FLASH_FRAME_MASK` 0x10 (`src/mii_video.c:45`, `:498-511`) and
AppleWin's `(++g_nTextFlashCounter & 0xF) == 0` (`source/NTSC.cpp:441-445`).
At ~60 Hz that is **3.75 phase changes a second**, or about 1.875 complete
flash cycles. **This port counts HOST ticks instead**, because it has no video
clock to count: `A2_FLASH_TICKS` = 5 ticks at 18.2 Hz is **274.6 ms**, which
is **3.64 phase changes a second**. The two figures are the same rate measured
on two clocks and they agree to within 3%; 3.64 is the one every cost in this
document is priced from, because it is the one this machine actually spends.
Neither is "twice a second": a visible flash CYCLE is two inversions and lasts
549 ms, which is the "about twice a second" the eye reports - and it is the
INVERSIONS that cost. This document read the cycle as the
cost for three paragraphs. Refusing is legitimate; silently not flashing is
not.

**The phase is a `W_ONTIMER` and not a wake poll** (SPEC.md 13.9). It is a
periodic heartbeat, and gating `a2_wants_wake` on it made that handler a
constant 1: the Apple's cursor is a FLASHING SPACE (`$60`, inside `$40-$7F`),
so `a2_flrow[]` is never all zero on a screen anybody is looking at, and the
wake re-posted at ~1,400 round trips a second of the SHARED UI task to service
something that happens 3.64 times. `os88_wm_timer` answers -1 on `kern_small`,
which carries the slot and not the body, so the wake's poll survives as the
tested second path (SPEC.md 47) and `a2_tmr_ok` is that fact.

### 7.7 The frame shadow and the flush

A **7,680-byte frame shadow** in bss - 40 x 192 bytes, exactly the pixels last
blitted. **The shadow is the glass, not the model.**

The flush runs **at most once per host tick**, never once per slice, and holds
the gfx lock only around itself:

1. compose the dirty scan lines;
2. test for a whole-frame shift - the **k-row scroll test**, asked only when
   at least 4/5 of the rows on the glass are dirty (a fraction, not a count).
   On a hit: one `OSAPI_GFX_SCROLL`, the k vacated rows, and **the shifted
   rows are marked CLEAN**. It is **EXACT**: a **SOURCE SHADOW** of 24 x 40
   bytes holds the sources each row's pixels were composed from, and the test
   compares the row's forty live bytes against them. **THE EXACTNESS IS WHAT
   PAYS FOR THE CLEAR**, and the clear is the whole optimisation: a ROM scroll
   writes all 23 source rows, so without it `a2_dirty_scan` had every row
   marked and the flush composed all 24 at full width - 120 groups, 292 ms -
   to discover that 23 were byte-identical to the shadow it had just shifted.
   A 16-bit signature could not carry that decision: `xor al, b / rol ax, 1`
   is linear over GF(2), so two cells sixteen apart changing by the same XOR
   delta cancel exactly - a shape a text screen makes. The forty-byte compare
   is also CHEAPER than the signature it replaced (a row read is 0.31 ms
   against `a2_rowsig`'s 0.67), and it costs 1,920 bytes of bss.
   **THE SOURCE SHADOW IS ONLY A PROOF WHILE EVERY OTHER INPUT TO THE
   COMPOSITION IS UNCHANGED - which today means THE FLASH PHASE.**
   `a2_band_text` takes `a2_fl_phase` as a second argument and `a2_shsrc[]`
   records forty source bytes and nothing else, so equal sources say the moved
   pixels are right for the phase the shadow row was composed at and not for
   the phase this flush is composing at. `os88_ontimer` flips the phase and
   `a2_flash_force` marks the flashing rows dirty for a reason no source
   compare can see; the next flush consumes that mark, and during scrolling
   output that flush is a SCROLL flush - so **a row whose incoming flash flag
   is set is RECOMPOSED rather than marked clean**, and only that row. Without
   it a scrolled flashing cell holds one phase for three half-periods, ~825 ms
   against 275 - a cursor stutter during exactly the scrolling output the
   scroll path exists for, invisible in a still screendump and invisible to
   the harness's scroll case until that case flips the phase across the
   scroll. A non-flashing row's pixels are phase-independent, so the test
   stays exact and the cost is the rows that actually flash - one on an
   Applesoft prompt, five groups. **AND THE SAME HOLE ON MODE IS CLOSED THE SAME WAY** (wave 3, which is the
   wave that added the composers and owed this): `a2_band_lores` and
   `a2_band_hires` are a THIRD input `a2_shsrc[]` does not record, and a
   lo-res screen and a text screen can hold byte-for-byte identical rows. So
   `a2_sh_mkey` - the renderer, MIXED and PAGE2 in one int, written once at
   the end of every flush beside `a2_sh_phase` - has to equal the flush's own
   key before the test is asked. **MIXED enters that key only where it can
   reach a pixel** (`!a2_v_text && a2_v_mixed`), which is the same statement
   `a2_video_set`'s else-arm makes and is exact rather than a loosening: in
   TEXT mode the split does not exist, so `POKE -16302,0` marks no row and
   must not invalidate the shadow either - and every transition that makes
   MIXED visible moves `a2_mode_of()`, which is in the key already. Unguarded,
   a soft switch two files away call a no-op made the next flush refuse the
   test over a shadow exactly as valid as it had been a moment before: if that
   flush carried a scroll, a 24-row recompose and 24 blits, **~497 ms against
   ~100**. The test is a **TEXT-mode** test besides
   (`a2_v_text`, not `!a2_v_hires`: lo-res rows scroll like anything else and
   are composed by another routine). The refusal costs one flush, and it is by
   construction the flush a mode change has already marked every row of - so
   every row of `a2_shsrc` is rewritten inside it and the flush after is exact
   again. **`a2_shsrc` is maintained for TEXT rows only**, for the same
   reason: forty source bytes of a hi-res row group describe ONE of its eight
   scan lines, and recording them would be 0.31 ms a row spent on a proof
   nothing is allowed to use. **`a2_flrow[]`
   IS SHIFTED WITH THE ROWS**: it is what a phase flip forces off, and it was
   correct before only because every row was being recomposed - the moment the
   composes stop, flashing text that has scrolled stops flashing. **AND THE
   VACATED ROWS' FLAGS ARE ZEROED AFTER THAT SHIFT AND NOT BEFORE IT.** The
   shift reads `a2_flrow[i+k]` up to row 23, so zeroing rows 24-k..23 first
   hands rows 24-2k..23-k a zero whatever they were flashing (row 22 at k=1,
   rows 8..15 at k=8) - and the same pass marks them clean, so nothing
   recomposes them and `a2_rowflash` never rewrites the flag: flashing text
   that scrolled up out of the bottom k rows stops flashing permanently. The
   defect the shift exists to fix, surviving at its own boundary, and the
   fixture that catches it has to sit in the bottom k rows. **AND THE
   SHIFT IS REFUSED WHILE ANY VISIBLE LINE'S GLASS IS UNKNOWN**
   (`a2_lnf`): `gfx_scroll` would move somebody else's paint up by k rows
   while the flag stayed where it was, and the shifted shadow would then
   compare equal over the garbage. The scan asks the VISIBLE lines only -
   nothing ever clears the forced bit of a line below `a2_gl0`, so a
   whole-array scan answers "unknown" for ever on a clipped band, which
   `hosttest/a2uitest.c`'s CGA row caught. **IT IS ASKED OF A
   CLIPPED BAND TOO.** The test used to require all 192 scan lines on the
   glass, which a 640x200 desktop never has - `dock_top` 176 clamps the
   window, 111 lines fit and `a2_gl0` is 81 - so a scrolling Applesoft
   session, the ordinary case, took the span path for every scrolled line:
   ~210 ms A LINE on a 4.77 MHz 8088 against 32.4. Nothing tied it to full
   visibility but the shadow shift,
   which moved all 192 lines; the `gfx_scroll` rect was already the VISIBLE
   band. So the shadow is shifted from `a2_gl0` down, and **both the row scan
   and the source compare start at the first row the glass has ever
   shown** - a row that was never composed has no shadow source to compare,
   and comparing its permanent 0 against live memory is what made the test
   answer "no shift" on every CGA screen. **AND THE MISS PATH IS BOUNDED AT 96
   PROBES.** The k loop is `sum(k=1..23) of (24-k)` = 276 forty-byte compares
   in the worst case, 0.31 ms each (section 7.9.1's `ROWSPAN` row measures the
   equal and the differing case at the same 0.875 counts), so 87 ms to be told
   nothing scrolled - and the shape that reaches it is ordinary: a long run of
   IDENTICAL rows above the content with every row dirty, which
   `HOME : VTAB 20 : PRINT` in a loop is exactly, since `HOME` writes all 24
   rows and meets the 4/5 threshold every iteration. **It is the EQUAL probes
   that cost**, which is what rules out a hash or signature prefilter rather
   than sizing one: a differing pair is the loop's cheap terminator, because
   `repe cmpsb` stops at the first differing byte, while a run of blank rows
   has equal signatures and would take the full compare anyway. Confirming a
   true shift of `k` costs at most 23 probes and the `k' < k` that fail before
   it are failing on a screen whose content HAS moved, so each breaks in one
   or two - a k=8 scroll is ~30 probes and never reaches the budget. What 96
   refuses is the screen with many identical rows, which is the screen that
   did not scroll, and 96 x 0.31 is 30 ms against the ~301 ms whole-page
   compose the test exists to save;
3. otherwise compare each composed line against the shadow and **draw only the
   differing spans**;
4. update the shadow;
5. the BORDER fills, only when they changed - the 8-pixel frame outside the
   320-pixel band, because the letterbox is inside the band (section 7.1);
6. the status row, **delta-drawn**.

**Two flags mean different things**: one says the SOURCES changed, the other
says the GLASS is unknown over a span. Setting both on a switch write that
changed nothing is the C64's measured 25 forced blits.

**AND A DAMAGE RECT NARROWS COLUMNS AS WELL AS ROWS.** A row the glass is
unknown over is composed and drawn WHOLE - the only spelling under which the
letterbox is redrawn too - but "unknown" is only true of the pixels the rect
actually covered. `a2_blank_rect` therefore carries the rect's band-byte
range into a flush-wide `[a2_fx0, a2_fx1]`, unioned across rects, and the
forced arm widens that range to whole eight-cell GROUPS (a group is seven
output bytes and the composer cannot write half of one) and then back out to
the range's own bytes, so the letterbox bytes outside the groups are still in
it. Every force a rect model cannot describe - a `gfx_scroll`'s vacated rows,
a wholesale mark, an invalidated shadow - widens it back to the whole band
first, and the flush resets it on the way out. A pull-down closing over this
window is a rect about 190 px wide (`MENU_MAXCH` is 24 glyphs), so a group is
56 px and four of the five are what those rows owe: **8 groups against 10**,
33.9 ms against 38.8 (section 7.9.1). `apps/c64/c64scr.c`'s `c64_blank_rect`
takes a column span for the same reason and measured its own pair at 122 ms
against 75.

**A LINE THAT COMPARES EQUAL IS CLEAN AND MUST BE RECORDED AS CLEAN.** Wave 1
shipped a flush that drew nothing for such a line and left its dirty bit set,
so the row recomposed itself on every wake for the rest of the session; the
only symptom was a cost row reading ten groups where the write window had
narrowed the work to five, and the host harness is what saw it.

**THE WAKE'S FLUSH ARMS ITS OWN CLIP REGION** (SPEC.md 11.3). The kernel arms
one for `W_PAINT` and for nothing else, and the flush is a BACKGROUND painter:
without `os88_wm_clip_set` its 24 blits, five fills and the status row
go straight over whatever window is covering ours - a defect no still
screendump taken after the covering window has gone can show. It buys the
other half too: `clip_set` answers -1 when not one pixel of us is visible, and
the whole ~500 ms flush is then SKIPPED rather than spent. A window in that
state must also STOP asking for wakes - the work is still owed and `W_PAINT`
is what comes and asks for it - or the refusal itself becomes the ~1,400
round trips a second it was meant to save. **A MINIMIZED window is the same
answer by a second route**: `os88_wm_geom` says "not visible", the flush
returns on it having cleared nothing, and every dirty flag stays set - so the
wake asks the same question at the one moment the user has said they do not
want to look at us.

**THE STATUS ROW'S "NOTHING CHANGED" ANSWER IS ALSO A STATEMENT THAT THE ROW
IS CLEAN.** `a2_say()` handed a string identical to the one already on the
glass - two picks of Toggle Fullscreen while another window holds it, two menu
picks on a disk with no `APPLE2.OVL` - recomputed an identical row, drew
nothing, and left `a2_st_dirty` set; `a2_wants_wake` reads that flag, so the
handler re-posted for the whole five-second life of the message.

**AND A FORCED LINE IS DRAWN OVER ITS WHOLE 40 BYTES, LETTERBOX INCLUDED.** A
group span is the right answer for a SOURCE change and the wrong one for a
glass that somebody else painted on: the About panel is exactly the band's
width, so closing it left two white strips - band bytes 0-1 and 37-39, the 16
and 24 letterbox pixels - with the tail of the panel's own text standing in
them. Found on the glass in wave 1 (`build/port-shots/wave1-36-about-closed.png`)
and nowhere else.

**RUNS ARE MERGED WITHIN A CHARACTER ROW AND NOT ACROSS ONE.** A blit is 756
us of floor whatever it covers, so consecutive scan lines with overlapping
spans go down in ONE call, and the rule for extending a run is that the union
may never cost more BYTES than two separate blits would (`union <= this +
next`). A full 192-line repaint is then **24 blits and not 192** - one per
character row, because the composer's band buffer holds one row's eight lines
at a time - which is 18 ms of call floor against 145. Merging across rows
would need a band buffer per row and is not worth 17 ms.

**`OSAPI_GFX_BLIT1` can refuse** (a broken argument, or a `kern_small` machine
carrying the slot without the body). **Test the return and carry a second
path**: `os88_font_run` for the text mode's cells, and one `os88_gfx_fill` per
span for lo-res and hi-res. **NOTE THE 7-PIXEL CELL HERE TOO**:
`os88_font_run` draws on an 8-px grid, so **the text fallback is an
APPROXIMATION** - a legibility floor for a refusing kernel, not a second
correct renderer. `a2bandbench` preflights the call so a refusing kernel
prints REFUSED rather than timing a call that draws nothing.

### 7.8 Full screen, and the tier table

`OSAPI_FULLSCREEN` (SPEC.md 11.2's **window latch**, not SPEC.md 53's
bracket) on **Alt+Enter**, both directions, with Ctrl+F kept as SPEC.md
11.2.1's unconditional door.

The magnification is **two numbers** - `a2_scw` / `a2_sch`, decided in ONE
place - and **the doubling happens at BLIT time and nowhere else**: the frame
shadow stays 280 x 192 and every compare, signature and span stays in Apple
pixels, so no compare path needs a second version. The doubled band is bss and
not a claim, because **the flush cannot refuse**. (It is lever 1 of section
15.4 if the budget needs it, moved to a claim taken at fullscreen-LATCH time
where a refusal is legal.)

**AND "AT BLIT TIME" IS LITERAL: `a2_band_x2` is called from `a2_emit`, not
from the compose loop.** The doubler is pure DRAW preparation, so it is owed
by a row that puts pixels on the glass and by no other; on the compose side it
was charged to every recomposed row, and a flush that recomposed the frame and
blitted nothing still spent **24 x 10.59 = 254 ms** doubling it (section
7.9.3's zero-draw row: **663.5 against 410.3**). That flush is ordinary - a
mode switch that draws the same picture, the reset recompose, a rect-forced
row whose pixels turn out identical, a MIXED flip.

**AND IT IS PER RUN, NOT PER ROW** - which is the other half of the same
sentence, and wave 3's review is where it was finished. Moving the call to
`a2_emit` and leaving it doubling all forty bytes and all eight scan lines
fixed the flush that draws NOTHING and left the flush that draws a LITTLE
exactly where it was: an ordinary keystroke at 2x emits one run seven band
bytes wide over eight lines and paid for 320 source byte-rows where 56 were
owed - **2.0 ms of work and 8.6 ms of waste, more than the rest of the
keystroke put together** - and a single-scan-line `HPLOT` owed 0.40 ms and
spent 10.59, **26x**. `a2_band_x2` therefore takes a BYTE COUNT as well as a
row count and is handed the run's own rectangle, at the destination offset the
blit two lines below computes from the same three terms.

**The per-run form needs no latch, and that is why it is the right shape.**
The runs of a row are disjoint rectangles, so doubling each one can never cost
more than their union: "a row that produced three runs must not double three
times" was a property the per-row latch (`a2_x2_row`) kept by hand, and it is
now structural. The latch is deleted.

| adapter / tier | fullscreen | what decides it |
|---|---|---|
| VGA 640x480 | **2x both axes**, centred | `640 >= 640` and `454 >= 384` |
| CGA 640x200 | **2x horizontal only** - a CGA pixel is already 2:1, so 1:1 draws the picture half as wide as it should be | `640 >= 640`, `174 < 384` |
| Hercules 720x348 | 2x horizontal, 1x vertical | `720 >= 640`, `322 < 384` |
| the `CPU_8086` tier | **1:1 centred**, whatever the adapter can hold | `a2_band_x2` is 10.59 ms for a whole character row |

**IT IS ARITHMETIC ABOUT THE LIVE CONTENT BOX AND NOT A LIST OF ADAPTERS**
(wave 3). `a2_geom` asks two questions - is there room for 640 doubled pixels,
and for 384 doubled scan lines - and the three rows above are what the three
adapters answer. A `WF_FULL` window's content **is** its frame
(`kernel/wm.inc`'s `wm_geom`), so the box it asks is the whole screen; at 638
the first question could never be true, which is what the host harness's
fullscreen stub had to be corrected to model.

**THE TIER TABLE IS WRITTEN FROM `tests/a2band`'s MEASURED NUMBERS** (section
7.9.1, and wave 3 took them):

- **the `CPU_8086` tier gets 1:1**, because `BAND_X2 8r x 40b` is 29.500
  counts - **10.59 ms** a character row, **254 ms** added to a whole-frame
  repaint that is already 497 - for pixels that are twice the size and no more
  informative. **The per-run change does not soften this**, and that is the
  point of quoting it here: a whole-frame repaint draws every row at full
  width, so the union of its runs IS the whole band and the tier's arithmetic
  is unchanged. What the per-run change buys is the *small* draw, which is
  every draw the tier table is not about;
- **the `CPU_8086` tier flushes every OTHER tick**, because a full repaint is
  496.8 ms against a host tick's 55: the pacing that costs nothing on a 386 is
  a queue on an 8088, and the second flush inside one machine-visible change
  is the whole cost of the first for pixels that were already right;
- **the `CPU_8086` tier refuses the FLASH PHASE**, with the measured cost in
  the greying (section 10.3): one flip is **43.1 ms** with two flashing rows
  on the screen, and at 3.64 flips a second that is **157 ms in every second**
  of an 8088 spent on the phase rather than on the 6502. It is `a2_fl_ok` that
  is cleared - the same byte Machine > Flashing text moves - so the refusal
  and the greying cannot disagree.

**AND THE LATCH MOVES BEFORE THE CALL.** `OSAPI_FULLSCREEN` repaints the
window whole and SYNCHRONOUSLY, so `os88_paint` - and `a2_geom` under it -
runs NESTED inside it: with the latch still down, that repaint measures the
new box at 1:1, draws all 192 lines, and the next flush draws them again at
2x. Half a second of pure double-draw on every entry, invisible in a still
screendump because the second picture is the right one.
`apps/c64/c64.c`'s own order is latch, call, roll back on refusal, and this
port takes it.

**`OSAPI_FULLSCREEN` repaints synchronously in both directions**, so the
success arm does nothing - a shadow-invalidate there is pure double-draw. It
is the **REFUSED** arm that owes a repaint.

### 7.9 The cost table

**The graphics rows below are MEASURED** - wave 3 took them from
`make a2bandbench` and from the counts `hosttest/a2uitest.c` prints - and the
foreign rows are wave 5's. The bench runs under
`make test QEMU="qemu-system-i386 -icount shift=3"`, where **one PIT count is
0.359 ms of a real 4.77 MHz XT**, which is where every millisecond in this
document comes from and is why the foreign-mode measurement is not blocked on
an 86Box machine.

**`sleep=off` is NOT used** (wave 1's correction). Under `-icount
shift=3,sleep=off` the vCPU thread runs flat out and the guest stops answering
input at all: the screen saver comes up, and neither `mouse_move` nor
`sendkey` dismisses it, so the bench can be booted and never driven. Plain
`-icount shift=3` boots to a desktop in about 25 seconds on a loaded host and
drives normally, and the COUNTS - which is what the bench publishes - are
identical either way, because icount is what makes them deterministic.

#### 7.9.1 What wave 1 measured

`a2band.inc`'s own rows, taken with the recipe above. The `counts` column is
the bench's, per operation; the millisecond column is `counts x 0.359`:

| row | counts/op | XT ms |
|---|---|---|
| `FONT_RUN 40 aligned` (the kernel's own lettering, for scale) | 37.250 | 13.37 |
| `BANDTEXT 5 groups` - a whole 40-cell row | 35.000 | 12.57 |
| `BANDTEXT 1 group` - eight cells, what a poke composes | 7.875 | 2.83 |
| `BLIT1 320x8 stride 40` | 4.875 | 1.75 |
| `BAND 5 groups compose+blit` | 39.875 | 14.31 |
| `BAND 1 group compose+blit` | 12.875 | 4.62 |
| `ROWSPAN 40 equal` / `differing` | 0.875 | 0.31 |
| `ROWCOPY 40 bytes` | 0.875 | 0.31 |
| `ROWSIG 40 cells` - **BENCH-ONLY**, see below | 1.875 | 0.67 |
| one row's forty SOURCE bytes (`a2_zcopy_out`) | = `ROWCOPY` | 0.31 |
| `ROWFLASH 40 cells` | 2.875 | 1.03 |
| `BAND_X2 8r x 40b` (**wave 3's re-measure**, 25.625 / 9.20 as wave 1 first read it - the one row here that moved by more than an eighth of a count, and the tier table is written from this figure. The review re-measured it again at 29.500 after the routine gained a byte count: an eighth of a count, which is the two extra instructions a row) | 29.500 | 10.59 |

#### 7.9.2 What wave 3 measured - the other two composers

The same recipe, the same bench, the three composers side by side. The figures
below are the **review's** re-measurement, taken after `a2_band_hires` gained
a scan-line range and `a2_band_x2` a byte count. **The text and lo-res rows
are wave 1's and are KEPT**: `BANDTEXT 5 groups` re-measures at wave 1's own
**35.000**, and the one-group floors moved by a quarter of a count - the two
`mov word [mem], imm` the composers now spend telling `a2_pack` which pixel
rows to pack, 0.09 ms a call on the target.

| row | counts/op | XT ms | per SOURCE byte |
|---|---|---|---|
| `BANDTEXT 5 groups` (40 source bytes) | 35.000 | 12.57 | **0.73 us** host / 0.314 ms XT |
| `BANDTEXT 1 group` | 8.125 | 2.92 | |
| `BANDLORES 5 groups` (40 source bytes) | 28.625 | **10.28** | **0.59 us** host / 0.257 ms XT |
| `BANDLORES 1 group` | 6.750 | 2.42 | |
| `BANDHIRES 5 groups x 8 lines` (320 source bytes) | 50.875 | **18.26** | **0.13 us** host / 0.057 ms XT |
| `BANDHIRES 1 group x 8 lines` | 12.375 | 4.44 | |
| `BANDHIRES 5 groups x **1 line**` (40 source bytes) | 7.125 | **2.56** | **0.14 us** host / 0.064 ms XT |
| `BAND_X2 8r x 40b` - a whole character row doubled | 29.500 | **10.59** | |
| `BAND_X2 1r x 7b` - ...and ONE RUN of one scan line | 1.125 | **0.40** | |
| `BLIT1 640x16 stride 80` - the DOUBLED emit | 8.875 | **3.19** | |

The bench prints the per-source-byte figure itself, beside each five-group
row, rather than leaving it to be divided off the column: it is the figure
that makes the three comparable, and it is the one this table is written from.

Per GROUP and per CALL, which is what the cost model charges - taken from each
composer's own pair, five groups against one:

| composer | per group | call floor |
|---|---|---|
| `a2_band_text` | 6.719 counts, **2.412 ms** (wave 1 published 2.434 and the model keeps it) | 1.406 counts, 0.505 ms |
| `a2_band_lores` | 5.469 counts, **1.963 ms** (wave 1 published 1.930 and the model keeps it) | 1.281 counts, 0.460 ms |

**HI-RES IS THREE ROWS AND NOT TWO**, because it is the one composer that
takes a **scan-line range** - hi-res is the mode the damage model marks a line
at a time, and its eight lines are eight separate 40-byte source rows `$400`
apart, where text and lo-res make all eight pixel rows out of ONE source byte
a cell and have nothing to narrow. A group is therefore no longer one unit of
work for it, and a two-point fit in groups alone prices a one-line call **23 %
too high** - on the very call the range exists for. Three points fit it
exactly:

| `a2_band_hires` term | counts | XT ms |
|---|---|---|
| call floor | 0.875 | **0.314** |
| per SCAN LINE, whatever it is wide | 0.234 | **0.084** |
| per GROUP-LINE - the work itself | 1.203 | **0.432** |

`0.875 + 8 x 0.234 + 40 x 1.203 = 50.875`, which is the five-group row exactly.
A whole row group is **18.27 ms** and one scan line of it **0.918** - and
until wave 3's review the composer charged the whole row group for a one-line
`HPLOT`, **eight times the work the machine had asked for**, in the one mode
whose damage model already knew better (section 7.4).

**HI-RES IS THE DEAREST PER CALL AND THE CHEAPEST PER SOURCE BYTE, AND BOTH
ARE TRUE OF THE SAME ROUTINE.** A full hi-res call composes EIGHT scan lines -
320 source bytes - where a text or lo-res call composes one row's forty and
gets eight pixel rows out of them, so the per-call figure is not comparable
across the three and the per-byte one is.

**AND `a2_band_x2` IS TWO TERMS**, for the same reason one call along: it
takes a byte count and is called per RUN (section 7.8), so a floor and a
per-source-byte-row cost are what price it. From the two `BAND_X2` rows:
**0.490 counts / 0.176 ms** floor and **0.0907 counts / 0.0325 ms** a source
byte-row, and `0.176 + 320 x 0.0325 = 10.58` is the whole-row row back to two
places. An ordinary keystroke at 2x doubles 7 bytes over 8 lines - **2.0 ms**
against the 10.59 the per-row form spent.

**AND THE DOUBLED BLIT IS 1.8x THE PLAIN ONE FOR FOUR TIMES THE PIXELS**
(3.19 ms against 1.75). It measured **111 ms** until the bench's own window
was widened to the full 640: at 632 a 640-pixel blit does not fit and every
one of those rows went down the CLIPPED path, which is not the path full
screen takes. A measurement of a clip is not a measurement of the blit.

**`ROWSIG` IS NOT ON ANY SHIPPING PATH AND IS NOT IN THE SHIPPING IMAGE.** The
k-row shift test compares forty source bytes (section 7.7 step 2) and nothing
calls `a2_rowsig`; `nasm -f bin` has no dead-code elimination, so the routine
is assembled behind `%ifndef A2_SHIP` and `apple2.asm` defines `A2_SHIP`
before the `%include`. It stays in `a2band.inc` because
`tests/a2band/a2bandbench.asm` times it and `hosttest/a2memtest.asm`'s
ES-sentinel row 5b is written against it, and both `%include` the file
themselves without that define - so the routine is still their subject and the
row above is still measured, while the package carries none of it.

**A GROUP IS 2.434 ms AND A CELL IS THEREFORE 304 us**, with a call floor of
0.393 ms - taken from the two BANDTEXT rows, which differ by four groups. That
is EIGHT TIMES what a naive model of "a table lookup, a mask and eight stores"
would have said, and it is the number wave 3's tier table gets written from.
It is also the third time this tree has measured a row composer and found the
model low (PERFORMANCE.md Set 64, Set 65), which is why section 16.6 says to
measure the band before believing a per-cell guess.

Applying those figures to the calls `hosttest/a2uitest.c` counts gives the
whole-operation rows, which the harness prints on every build:

| operation | XT ms | what it costs |
|---|---|---|
| an idle wake | 0.0 | nothing at all |
| one changed cell | 11.9 | 1 blit, 1 group |
| one changed character row | 21.6 | 1 blit, 5 groups |
| two pokes at opposite ends of the page | 207.0 | 2 blits, 60 groups - the page bitmap is per 256 bytes and the window is ONE range, so twelve rows are recomposed. The stated cost of a global window |
| **ONE FLASH PHASE FLIP** | 43.1 | 2 blits, 10 groups, with **two** flashing rows on the screen - written by the harness itself, because `a2_selftext()` is wave-1 scaffolding that wave 2 deletes, and the wave-1 figure of 65.6 ms was three of `a2_selftext`'s own rows. **This is the number that decides the `CPU_8086` greying** in wave 3, and at **3.64 flips a second it is 157 ms/s** of an 8088 |
| a one-row scroll | **41.9** | 1 `gfx_scroll` + 1 blit + **5** groups + 24 forty-byte source reads. It was **406.2** while the shift test only saved the SCROLL: the shifted rows stayed marked, so all 120 groups were composed and compared to be told they matched the shadow that had just been moved under them. Step 2 is why that is now a clean mark instead - and why the compare had to become exact first |
| **a one-row scroll CARRYING A FLASH-PHASE FLIP** | **106.2** | 4 blits, **20** groups - the one vacated row plus the THREE rows the fixture flashes, each recomposed at the new phase. The source shadow does not record the phase `a2_band_text` was called with, so a verified shift may not mark a FLASHING row clean on a flush whose phase has moved (step 2): the glass would keep the previous phase, the next flip would compose back to it and draw nothing, and the cursor would hold one phase for ~825 ms instead of 275 during exactly the printing the scroll path exists for. `a2_sh_phase` is the one int that makes it cost these rows on a flip flush and NOTHING on the ordinary scrolling wake above: recomposing every shifted flashing row on EVERY scroll is equally correct and measures **91.1 ms and 20 groups** on that row against 41.9 and 5 |
| a HOME-shaped screen the shift test must **REFUSE** | 436.4 | 1 blit, 120 groups, and **96 probes** of the k loop - the budget, against 200 measured with it removed (62.8 ms of asking) and 276 worst case. A run of identical rows above the content with every row dirty is what `HOME : VTAB 20 : PRINT` makes on every iteration; the compose behind it is the real cost and the test's job is not to add to it |
| a full 320 x 192 repaint | 496.8 | 24 blits, 5 fills, 120 groups. **The fifth fill is the STATUS STRIP's**, and it is a fix rather than a cost: `A2_STATH` is 10 and `os88_font_run` draws EIGHT rows, so scan lines `a2_gsty+8` and `+9` were painted by nothing at all - `a2_border_fill` stops at `a2_gsty-1` - and under `WF_OWNBG` they kept whatever was under the window. One fill on the FULL-REDRAW arm only, `apps/c64/c64scr.c:1014`'s rule: the delta path never erases, because `font_run` arrives in final polarity |
| a partial expose, 16 scan lines of the band | 38.0 | 2 blits, 10 groups - WF_OWNBG hands back a damage RECT, and this is what asking for it is worth against the 496.8 above |
| a partial expose, 16 lines x **190 px** (a menu) | 33.2 | 2 blits, **8** groups against the 10 the full-width rect above costs. `a2_blank_rect` carries the damage rect's COLUMNS as well as its rows, so the flush's forced arm widens to whole GROUPS of the range and not to all forty cells. A group is 56 pixels, so a 190-px pull-down is four groups of the five: `apps/c64/c64scr.c` takes the same column span and measured its own pair at 122 ms against 75 |
| a flash flip and a narrow write on ANOTHER row | 33.0 | 2 blits, **6** groups - five for the flashing row, which must be composed whole, and ONE for the row whose write window narrowed it. The widening is per ROW (`a2_rowwide[]`, `c64_rowd`'s shape) and not per flush: one flag read by all 24 made a blinking `]` compose every other dirty row full width, 10 groups here |
| a write straddling two character rows | 55.8 | 2 blits, 12 groups - the compose span is an INTERSECTION and not a containment; a containment test composes 20 |
| an expose the About panel does not cover | 38.0 | 2 blits, 10 groups - the panel is 1 fill + 2 frames + 8 `font_run` over 196 cells, ~185 ms, and `os88_paint` tests the damage rect against its rect before spending it |
| closing the About panel, as damage | 301.1 | 16 blits, 80 groups - the 16 character rows it covered. It was 348.5 while `a2_about_close` invalidated the whole status row unconditionally; at the shipping geometry the panel's foot is 43 pixels clear of that row, so the test is asked rather than assumed |
| a one-row scroll on a CLIPPED band (CGA) | **32.4** | 1 `gfx_scroll` + 1 blit + **5** groups, on the 111-line band a 640x200 desktop gives. It was **238.1** for the whole-band row's reason (the shifted rows were recomposed) and 244.8 before that, while the shift test read all 24 rows: `a2_gl0` is 81 there, so the first TEN have no visible scan line, are never composed, and have a permanently-zero shadow source - reading them is 0.31 ms a row spent comparing a live value against a sentinel it cannot equal. The shift test used to require all 192 lines on the glass, which a CGA never has: every scrolled line then cost the span path, ~210 ms A LINE, so a twenty-line `LIST` was ~4.2 s where it is now ~0.65 s |

**497 ms for a full repaint is 2.5x this document's PLANNED ~200 ms**, and it
is recorded rather than smoothed: the composer is the cost, the tier table in
wave 3 is written from it, and the four size levers in section 15.4 do not
touch it.

#### 7.9.3 The whole-operation rows wave 3 measured

The same harness, the same model, with each composer charged its OWN figures
above - pricing a hi-res row at the text composer's would be quoting one
routine's measurement for another's work.

| operation | XT ms | what it costs |
|---|---|---|
| a full 40 x 48 LO-RES repaint | **430.7** | 24 blits, 120 groups. Cheaper than the text screen's 496.8 for the same 24 rows, because a lo-res group is 1.997 ms against 2.434: two nibble reads and eight stores a cell, where text is a glyph pointer and a mask |
| one changed LO-RES block | **8.9** | 1 blit, 1 group - the write window narrows a POKE to the one group its cell is in, exactly as in text |
| a full 280 x 192 HI-RES repaint | **621.9** | 24 blits, 120 groups (960 group-LINES). The dearest picture this machine draws, and the reason the `CPU_8086` tier's flush pacing is every other tick. The scan-line range buys nothing HERE - every line is dirty - which is what makes the row a control on it |
| **one changed HI-RES scan line** | **4.2** | **1 blit, 1 group, ONE group-line** - and the blit is one scan line, not eight. A hi-res row group's eight lines are eight separate ranges, so the dirty scan asks about each; marking the row from one write would blit eight lines where the machine changed one. It was **7.6** until wave 3's review, because the DRAW was narrowed and the COMPOSE was not: `a2_band_hires` composed all eight lines whatever the caller had marked, so an `HPLOT` that moves one scan line at a time paid **2.434 ms a group where 0.30 was owed** on every row it crossed. The composer takes a scan-line range now (section 7.9.2) and the flush hands it the union of the row's dirty-or-forced lines - FORCED included, because the `!trust` arm draws straight out of the band with no compare |
| a MIXED screen: hi-res over four text rows | **91.2** | 4 blits, **20 groups**. The MIXED split moves rows `A2_MIXROW..23` and NO other row - `a2_row_mode(r)` for `r < A2_MIXROW` never reads `a2_v_mixed`, and neither `a2_row_base` nor the page moves - so `a2_dirty_split` marks the split's four rows WHOLE and nothing else. It was **504.9** with `a2_dirty_all` there (wave 3's review): 120 groups composed from identical sources by the identical composer to draw four rows that owed 20 |
| clearing MIXED in hi-res, then setting it again | **108.6** / **91.2** | 4 blits, 20 groups each way. Both directions, because the four rows arrive from the OTHER page each time - the text page on MIXED-on, the graphics page on MIXED-off - and neither page may have a bit set, which is why `a2_dirty_split` marks the rows EXPLICITLY and marks them WHOLE. `POKE -16302,0 / POKE -16301,0` in a graphics mode is ordinary and a program that flips the split per frame used to pay ~500 ms of an 8088 each time |
| a mode switch that draws the same picture | **305.0** | **0 blits**, 120 groups. A change that changes the renderer marks the whole frame (section 7.4) and the span compare is what decides that nothing moved: the compose is the cost and the draw is nothing |
| eight soft-switch reads that change nothing | **0.0** | 0 blits, 0 groups. Every video switch is guarded BY VALUE (section 5.4); the C64 measured the unguarded form at 25 forced full-width blits, ~234 ms |
| a one-row scroll in a GRAPHICS mode | **396.6** | 24 blits, 120 groups, and **no `gfx_scroll`**. The k-row shift test is a TEXT-mode test - the source shadow says nothing about which composer turned forty bytes into pixels - so a graphics screen that scrolls takes the span path. It is the honest price of the refusal and it is stated rather than hidden |
| **entering full screen at 2x on VGA** | **783.6** | 24 doubled blits, 3 fills, 120 groups **and 24 `a2_band_x2` calls over 7,680 source byte-rows**. The doubling is 254 ms of it, which is what makes the `CPU_8086` tier 1:1 - and it does not move on the per-run change, because every row here draws at full width and the union of a row's runs is the whole band |
| a fullscreen recompose that DRAWS NOTHING | **410.3** | **0 blits, 0 `a2_band_x2` calls**, 120 groups. The doubling is DRAW preparation and is charged on the draw side in `a2_emit` - so a row that produces no run costs nothing. With `a2_band_x2` beside `a2_band_text` on the compose side (wave 3's first cut) this same flush was **663.5**, spending 253 ms doubling a band no pixel of which was blitted. The case is ordinary: a mode switch that draws the same picture, the reset recompose, a rect-forced row whose pixels turn out identical |
| **one changed cell at 2x** | **12.9** | 1 doubled blit, 1 group, **56 source byte-rows doubled** - the RUN's own rectangle, seven band bytes over eight scan lines. The same row priced with the per-ROW call is **21.5** - 320 byte-rows for a run that covered 56, the doubling then twice the entire rest of the keystroke - and that is arithmetic on the measured `BAND_X2` figures rather than a second run, because the only term that moves is the doubler's. The bound the harness asserts is 64 - eight bytes over eight lines - and anything wider is the per-row form back again |
| a slice with no tick boundary (an idle wake) | **0.0** | nothing at all - the wake's flush is paced at most once per host tick, and a wake that finds nothing dirty draws nothing |

#### 7.9.4 The whole-operation rows wave 5 measured - THE FOREIGN FRAME

Section 13.4 has the primitives and the recipe; these are the rows this table
owes, in milliseconds, on the two change sets that matter. The `FSXM_VGA13`
row is also `hosttest/a2uitest.c`'s, which counts the calls and prices them
from the same primitives and reads **3,369.4** against the bench's 3,365.3.

| operation | XT ms | what it costs |
|---|---|---|
| **one foreign frame, `FSXM_VGA13`, every line dirty** | **3,365.3** | 192 `a2_fsx_row` (hi-res, 15.39 each) + 192 `a2_fsx_put` that all moved (2.15). The dearest picture in this document, and the reason a foreign frame is driven off the dirty-line set rather than written whole |
| ...the same frame, windowed at 2x | **632.6** | 24 composes, 24 doublings, 24 blits - and the two are NOT alternatives: one of them is in colour |
| ...the same frame, `FSXM_CGA640` / `FSXM_HERC` | **695.4** | the identical 24 composes and doublings, then 192 `a2_fsx_put` of 70 bytes. **CUT** (section 13.1): it loses to the 632.6 above |
| **one character row, `FSXM_VGA13`** - a keystroke | **140.4** | 8 rows + 8 puts. The other 184 scan lines answer `nothing moved` |
| ...one character row, windowed at 2x | **26.4** | |
| ...one character row, `FSXM_CGA640` / `FSXM_HERC` | **34.8** | **CUT** with the row above it |
| **A SCROLL, `FSXM_VGA13`** - a RETURN at the bottom line | **1,747.2** | 184 text composes at full width + 184 puts that all moved, measured as its own bench row (`SCROLL FSXM_VGA13 (184 lines)`, 4,867 counts). There is no shift block in `a2_fsx_frame`, so the ROM's line-by-line copy is recomposed rather than moved |
| ...the same scroll, windowed | **41.9** | one `os88_gfx_scroll`, one blit, five groups - the row above this table's own (7.9.1). **41.7x**, and section 13.2 records why it is a number rather than a fix |
| a foreign frame in which NOTHING changed | **0.0** | 0 composes, 0 puts - and from the wave-5 review, **not even a frame call**: `a2_fsx_main` reads `a2_wrote()` and gates `a2_fsx_frame` on `a2_dirty_any \|\| !a2_fsx_ok`, which is `os88_onwake`'s own gate one path along. Ungated, `a2_dirty_scan` plus the per-row prologue ran 18.2 times a second for nothing: 17-21 ms a tick in text, 23-29 in hi-res. The harness asserts the drive: three frames with nothing changing write 192 lines in TOTAL, which is the first frame's and no more |

Rows still to publish, in milliseconds and never in calls:

| operation | measured in |
|---|---|
| ONE TRACK CHANGE through the nibbliser | the Disk II follow-up |

The model also charges what a call-count model hides, at
PERFORMANCE.md's own arithmetic: **756 us a `gfx_*` call, 900 us a glyph
cell**, 38 us a scratch thunk, ~190 us `a2_dirty_take`, **58 us a bridge
crossing**, 26 us a near call.

**The cost FIXTURE must not be a full screen of one character** - a fixture
filled to the cap measures the refusal path.

**MEASURE THE BAND BEFORE BELIEVING A PER-CELL GUESS.** RUNCPM's first row
composer was 306 us a cell against a model that had guessed 40, and this
port's composers are all shift-accumulator composers, a class this tree has
never measured.

---

## 8. The speaker

`$C030` toggles a one-bit speaker, and **both reads and writes toggle it**.

**The frequency estimator is this port's own design and exists in no
reference** - all three synthesize PCM - and it is stated as such here and on
the status row. It is:

- a **last-toggle emulated-cycle stamp**;
- `hz = 1,020,484 / (2 * delta)`, through **ONE 32-bit assembly muldiv, not
  two**: rounding twice put the C64's F = 341 at 19 Hz against a true 20,
  which is the difference between the floor refusing the note and playing it;
- played with `os88_snd_tone(hz, 0, prio)` **when the last N intervals agree
  within a tolerance**;
- taken down after a silent 1/18 s.

**Duration 0 means something must take it down**, so there is **exactly one**
`a2_sound_stop()`, and a pause, a reset, a JAM, warp, Machine > `Mute` and the
About panel all reach it. Capability is established **once** in `os88_main`
from `os88_snd_caps()`. A refused grant (-1: another instance holds the
speaker) is retried, bounded at eight wakes, then dropped with `The speaker is
busy.` said once.

**AND THE BOUND COUNTS CONSECUTIVE REFUSALS, NOT REFUSALS OF ONE NOTE**, which
is the difference between a bound and a sentence about one. The first writing
restarted the count whenever the measured hertz differed from the last note
ASKED for - and after a refusal nothing is sounding, so the sixty-fourth band
below is skipped and every 2 Hz wobble of a 1,000 Hz estimate read as a new
note. A refused tone loop therefore re-asked a busy kernel on every wake for
ever, never reached the eighth, and never said the fact this paragraph
promises. The same band is applied to the note last asked for, so "this note
again" means what it says while nothing is sounding.

**THE STATED FACT IS ARMED BY THE FAILURE AS WELL AS BY THE SUCCESS**, and
that is section 9's row read the right way round. `Square-wave tones only.` is
said ONCE - the first time this machine makes a noise, **OR the first time it
makes one this build cannot turn into a tone**: four consecutive wakes in
which `$C030` toggled and the estimator answered nothing steady. The first
writing latched it only inside the successful `os88_snd_tone` arm, so the
sentence fired exactly when the emulation was working and never when it was
not - and the programs the sentence EXISTS for (a click track, Karateka-style
waveform synthesis, a Mockingboard) are precisely the ones whose intervals do
not agree, so no tone is ever asked for. A user who loaded a beeping program
heard the beep and was told the limitation; a user who loaded Karateka heard
nothing and was told nothing.

**AND NEITHER SENTENCE IS SPENT WHERE NOBODY CAN READ IT.** `a2_spk_service`
runs inside the exclusive bracket as well as from the wake (section 13.3 rule
6 - the snd slots stay legal in there), and the status row is not on the glass
during a foreign-mode session: a message would expire five seconds later with
the desktop still gone. Both say-once LATCHES are therefore left unspent while
`a2_fsx_up`, so the first wake after the bracket returns says it.

It reproduces the beep, `CHR$(7)` and any square-wave tone loop, and **wave 5
measured both on the glass** with `make test-snd` + `tools/sndcheck.py`:

| what | measured |
|---|---|
| the II+ cold-start beep, and `PRINT CHR$(7)` | **934 Hz**, 0.11 s |
| a two-line `10 POKE-16336,0 / 20 GOTO 10` loop | **62 Hz** - which is Applesoft's own speed and not a defect: 510,242 / 62 is **8,230 emulated cycles a toggle**, and that is what a `POKE` of a negative expression plus a `GOTO` costs an interpreter running at 1.02 MHz |
| the silence timeout | the capture is **5.5 s of a 40-second session**: QEMU's wav backend writes only while the speaker is sounding, so the file length IS the sounding time. The beep's 0.11 s and the loop's five seconds are in it and nothing else is |

**AND "ONE FAR CALL ON A CHANGE ONLY" NEEDED A BAND, WHICH IS THE ONE PLACE A
MEASURED FREQUENCY IS NOT A REGISTER.** `C64-SPEC §11.4`'s rule is exact for a
SID write - the guest wrote a number and it either moved or it did not - and
here the number is measured off an interval that moves by a cycle whenever a
branch crosses a page. Every step of that walk is a far call at 46.7 us AND an
`out 0x43`, which restarts PIT channel 2's count (`kernel/snd.inc`'s
`spk_tone`) in the middle of a note nobody asked to change; a wake is not 18 Hz
here, it is however often `a2_wants_wake` re-posts one. So a new estimate
within **a sixty-fourth** of the sounding note is not a change. That is 0.27 of
a semitone - under a fifth of what anyone can hear, so a glide still glides -
against a wobble of 0.008 Hz at the 62 Hz measured above and 2 Hz at 1,000.
`hosttest/a2uitest.c` drives a train whose interval moves by ONE cycle over six
wakes and requires the kernel to be asked **once**.

**THE FIRST WRITING OF THAT PARAGRAPH CLAIMED IT FIXED FIVE SECONDS OF DC IN A
`make test-snd` CAPTURE, AND THAT WAS THE ANALYSIS AND NOT THE MACHINE.** The
capture really did read as a constant full-scale level - but the scan that said
so started at 30 Hz in steps of 5 and answered `60`, and 62 Hz sampled at
44.1 kHz is 355 identical samples a half-period, which is exactly what "DC"
looks like from forty samples away. The band above is kept on its own merits,
which are the far call and the `out 0x43`; the machine was right and the
instrument was not. What settled it was putting the estimator's own answer on
the status row for one build and reading `62%` off the glass.

**THE STATED FACT, which lives here and on the status row and NOT in the About
panel:**

> The speaker plays the square wave a toggle loop implies. A program that
> shapes the waveform toggle by toggle is not reproduced - a PCM stream is
> minutes of arithmetic a second on a 4.77 MHz 8088.

`OSAPI_SND_STREAM` is **refused with that arithmetic, not deferred**: a 1-bit
speaker wants a stream at ~22 kHz, and this machine runs the emulated CPU at a
few per cent of real speed on the target.

---

## 9. The status row

42 cells of 8 pixels across the 336-pixel content box, **delta-drawn** on
`C64-SPEC §10`'s shape - a field whose text has not changed is not redrawn.

| field | carries |
|---|---|
| message area | refusals, the overlay's `Unable to load APPLE2.OVL.`, **the speaker's stated fact - `Square-wave tones only.`, 23 cells, said ONCE the first time the machine makes a noise, or the first time it makes one this build cannot turn into a tone (section 8)** - the speaker's refusal `The speaker is busy.`, the two mute picks' `Muted.` / `Unmuted.` (the port's own pick-says-what-it-did idiom, beside `Stopped.` and `Warp off.`), the foreign mode's three refusals (`No colour on this screen.`, `No memory for colour.`, `The screen refused it.`) and - from the Disk II wave - the sentence that says the drive is spinning and names Ctrl-Reset as the way to `]` |
| speed | the **measured** percentage of a 1.02 MHz Apple II, right-aligned at the row's last cells, **drawn only while the message area is clear**, and **drawn only while the 6502 is RUNNING** - a jammed machine's field goes blank rather than freezing on the last window's figure, which would be a number about a machine that is not running (SPEC.md 47). There is no column a message and a widget can both have - the mode fields hold 0-15 and `Unable to load APPLE2.OVL.` is 26 glyphs, which reaches cell 41 exactly - so the choice is which one loses, and a message is transient where a speed figure is not news; narrowing the message cap to 19 instead would truncate the one message a user most needs to read whole. It is counted in **64-cycle units with the remainder carried**, because 1,020,484 cycles a second does not fit the 16-bit `int` this C has and a truncation that dropped up to 63 cycles a slice would misreport by ~0.4 % on a machine taking sixty slices a second |
| ...and **THE UNIT DOUBLES RATHER THAN THE COUNT SATURATING** | this is the field's own history and it binds. Its first form simply stopped accumulating - `if (a2_c64u < 60000u)` - and the one-second denominator is `876 x 18 / 100` = 157, so the largest per cent the arithmetic could produce was **383**: 380 %, 400 %, 1,000 % and 3,000 % all printed the same number, `pct > 9999` was unreachable dead code, and 383 is what wave 2's own screendumps show on a host that runs the core at some thousands of per cent. The count now holds units of `64 << a2_csh` cycles; when it would pass 32,767 it is halved and the shift goes up, so nothing is dropped at any speed, and the fold undoes the shift on **quotient and remainder both** (`(q << sh) + ((r << sh) / den)`) - dividing by a shifted denominator loses a third of the answer. `a2_csh` caps at 6, which holds 13,300 % of a one-second window, and the 9,999 % clamp fires first and is therefore REACHABLE. **`hosttest/a2uitest.c` is the gate**: seven machine speeds from 3 % to 6,000 % driven through the arithmetic and checked against what each should read, plus a 12,000 % case that must clamp. A field that reads the same for two very different machine speeds is the whole thing being tested |
| video mode | the live mode: `TEXT` / `LORES` / `HIRES` at cells 0-4, plus `MIXED` at 6-10 and **`PG2`** at 12-14. `PG2` and not `PAGE2`, and it is arithmetic rather than taste: the message area starts at cell **16** because `Unable to load APPLE2.OVL.` is 26 glyphs and the row is 42, so the mode fields hold cells 0-15 and the third field has **four**. `PAGE2` is five |
| drive | the motor lamp and `%02d/%02d` of current track and current byte over 256 - **the Disk II follow-up PR** |

Every message literal in `apps/apple2/*.c` is walked against the row's cell
cap by `build.sh` (section 16.6), with an explicit expected **minimum**, so a
corpus of zero literals is a failure and not a pass.

**THE UNITS ARE WHOLE PER CENT AND ON AN XT THAT IS `0%`** (section 16.4.1):
measured on MartyPC's 4.77 MHz 8088, the machine runs the Apple at **0.54% of
1.02 MHz idle and 0.64% inside a BASIC loop**, and both truncate to 0. The
field is not broken there and it is not rounded up either - a `0%` that is
honest is the point of measuring rather than estimating, and the arithmetic
that produces it is section 4.3.1's budget, ~40 ms of 6502 in a wake that
takes about two host ticks. It was 0.41% and 0.51% before that controller,
and `0%` either way - which is what a whole-per-cent field costs and what
section 16.4.1 says out loud.

**AND ONE THING THE GLASS SHOWED THAT THIS DOCUMENT CANNOT YET EXPLAIN, WHICH
IS RECORDED RATHER THAN TIDIED.** On that same MartyPC XT the mode field reads
**`TEX`**: the row's shadow holds `TEXT` and the CGA VRAM under it holds three
glyphs and a blank cell, read out of the guest byte for byte with
`_a2_st_glass` beside it - **a VRAM read and not a screendump**, which is why
no file is cited here: the difference is four bytes of CGA text memory and a
photograph of them would prove less than the bytes do. It is
**not** reproducible under QEMU's `VIDEO=cga`, where the same build draws
`TEXT` - the difference between the two runs is `a2_gox` (8 there, 0 here) and
the `CPU_8086` tier's every-other-tick flush. The shape that fits every
observation is a FIRST full-row draw that was cut short and a shadow that
recorded it as drawn, so no later delta ever repairs the middle: the `0%` at
cells 40-41 is on the glass because the speed field changed again afterwards
and its own delta redrew it. Nothing in wave 7 touched `a2_status`. It is
**open**, it is one field on one machine class, and it belongs to whoever
takes the Disk II follow-up.

**AND ONE ROUTE IS ALREADY CLOSED, WHICH IS WORTH MORE THAN A GUESS.** An
earlier draft of this paragraph sent the next reader after "invalidate
`a2_st_ok` wherever `a2_sh_ok` is invalidated" - and that is a no-op:
`a2_sh_ok` is assigned 0 at exactly ONE place in the package,
`a2_sh_inval` (`a2scr.c:428`), and `a2_st_ok = 0` is two statements below it
at 430. There is no site where one is invalidated and the other is not, so
that session would be spent proving it. It is also the wrong SHAPE of lead: a
shadow that says drawn while the glass is not is a TRUST failure and not a
missed invalidation, and this package already knows the difference one path
along - `a2_row_font` TESTS `os88_gfx_blit1`'s answer, and the comment above
it says why ("Discarding the answer and updating the shadow anyway is worse
than a missing row... the row is never retried").

**`a2_status` is the one shadow-keeper here that cannot ask.**
`os88_font_run` returns `void` (`apps/cc/os88.h:591`), so cells 0-41 are
recorded into `a2_st_glass` (`a2scr.c:1168-1171`) whatever the kernel put on
the glass, and no later delta can repair a cell whose text does not change
again - which is exactly the observed pair, `0%` surviving at cells 40-41
because the speed field changed and `TEXT`'s fourth cell not surviving
because it did not. So the two open leads are:

- **the repair shape is a periodic or on-suspicion full-row rewrite** - clear
  `a2_st_ok` every N flushes, or on the first flush after `a2_gox`/`a2_gw`
  move - rather than an invalidation hunt. A row that is redrawn whole
  occasionally cannot stay wrong forever, and a full row is ONE `font_run` of
  42 cells - the same call the delta arm already makes, only wider - which is
  a cost this document can afford occasionally and could not on every flush.
- **the reproduction should read `_a2_st_glass` AND `_a2_gox`/`_a2_gw`/
  `_a2_gsty` beside `m.vram()`** on a MartyPC launch on `os8088_5150_cga` -
  the shadow, the pen and the pixels in the same breath. `a2_gox` is the one
  recorded difference between the failing run and the passing one (0 against
  8), and the row's x is `a2_gox + f * 8` (`a2scr.c:1163`), so it is the first
  thing the pair has to agree about.

**THE STRIP IS `A2_STATH` = 10 ROWS AND `os88_font_run` DRAWS EIGHT, SO THE
FULL-REDRAW ARM FILLS IT FIRST.** `a2_border_fill` stops at `a2_gsty - 1` and
the lettering stops at `a2_gsty + 7`, so scan lines `a2_gsty+8` and `+9` were
painted by nothing at all - and `WF_OWNBG` means the kernel paints no
background either, so they kept whatever was under the window: the Disk
window's white list at launch, and **in fullscreen a white bar the width of
the screen**, because 42 cells reach 336 pixels of a 638-pixel content box.
One `os88_gfx_fill` over `(a2_gox, a2_gsty)` to `(a2_gox + a2_gw - 1,
a2_gsty + A2_STATH - 1)`, **on the `!a2_st_ok` arm only**:
`apps/c64/c64scr.c:1014` is the precedent and states the rule, which is that
the DELTA path never erases - `os88_font_run` arrives in final polarity, and
an erase there is PERFORMANCE.md rule 2's erase-then-letter pair. It is the
fifth fill in section 7.9.1's full-repaint row.

The harness cannot see this by modelling the glass, because its own
`font_run` stub is eight rows too: `hosttest/a2uitest.c`'s `no_gunk()` asks
the other question instead - **is any pixel of the content box still the
value nothing has drawn?**

---

## 10. The menus

### 10.1 The set

**FOUR menus on the kernel bar - File, Edit, Machine, CPU** - which is MII's
own count, with **Video's and Audio's rows INSIDE Machine under separators**
because the os8088 bar has no submenu mechanism at all. The Apple menu becomes
the kernel's own name pull-down. `AM_NAME` is **`Apple II+`**.

**Every caption and mnemonic not fixed in section 2 is transcribed in wave 1**
from the authority row's own file - `mii_mui_menus.h`, apple2emu's
`interface.cpp`, or the AppleWin file that owns it - and each greyed item
carries **the FACT that greys it in a comment beside it** in `a2menu.c`.

**EXACTLY ONE ROW CARRIES A SHORTCUT CAPTION, AND THE OTHERS ARE REFUSED BY
ARITHMETIC RATHER THAN BY POLICY.** `c64menu.c` captions its rows
(`Exit emulator  Alt+Q`, `Paste  Alt+Insert`) from VICE's own `hotkeys.vhk`,
and MII supplies the same material as `.kcombo` on Quit, Toggle Fullscreen,
Control-Reset, Open-Apple-Control-Reset, Mute, Louder, Stop, Running, Step and
Next. **Four chords are live**, and section 6.3 is where each comes from: Ctrl+F
and Alt+Enter in RESIDENT code **from wave 1** - they have to be, because a
`WF_FULL` window has no menu bar and they are the only way back out of the item
that got the user in - and **Ctrl+F2 (Control-Reset) and Ctrl+F3
(Open-Apple-Control-Reset) from wave 2** (`a2kbd.c`). So the only question is
the **24-glyph item cap** (`MENU_MAXCH`), asked per row against `c64menu.c`'s
`label  chord` spelling, whose separator is two spaces:

| row | arithmetic | caption |
|---|---|---|
| `Control-Reset` | 13 + 2 + `Ctrl+F2` 7 = **22** of 24 | **`Control-Reset  Ctrl+F2`**, and it ships |
| `Open-Apple-Control-Reset` | **24** with nothing appended | none, ever, at this cap |
| `Toggle Fullscreen` | 17 + 2 + `Ctrl+F` 6 = **25** | none. `Alt+Enter` is 9 and worse |
| `Color NTSC` (wave 5) | 10 + 2 + `Ctrl+F` 6 = 18, + the 2-glyph mark column = **20** of 24 | none, and it **FITS** - it is declined on the next paragraph's reason and not on arithmetic |

**`Color NTSC` IS THE ONE ROW WHERE THE CAPTION FITS AND IS STILL WRONG, AND
THAT IS A DEPARTURE FROM SPEC.md 11.2.1's KEY-HINT HALF, RECORDED HERE.**
SPEC.md 11.2.1 says, of Paint, *"Ctrl+F is what the menu item names, because a
menu item's key hint has to be true in every state"* - and the state a menu
caption is READ in is the windowed one, with the pull-down open. In that state
**Ctrl+F is Toggle Fullscreen's**, the SPEC.md 11.2 window latch, and it is
NOT a way into the SPEC.md 53 bracket this row opens. `Color NTSC  Ctrl+F`
would therefore name a key that, pressed where the caption is read, does
something else - which is the precise failure 11.2.1's sentence is about, one
level down from the one it is usually quoted for. There is no chord that
enters this row (MII has none either: `m_video_menu`'s tint rows carry no
`.kcombo`), and inventing one is inventing UI. So the row carries no caption,
the departure is stated rather than left as an accident, and section 13.3
carries the other half of it - **what the user is not told, and where the
gap is**.

So the two rows a user would most want the chord for are exactly the two that
cannot carry one, which is a fact about their names rather than a decision to
revisit; wave 3 does not re-open it. This paragraph read *"no row carries a
shortcut caption"* and gave *"not one chord is live in wave 1"* as half the
reason, which section 6.3's own **AND THE FULLSCREEN CHORDS LAND IN WAVE 1**
paragraph had already contradicted on the page above it. The rest of the
table below is the wave-1 transcription, and it replaces the draft's
table, which had been written from the plan rather than from the files:

| menu | rows, in order, as `a2menu.c` carries them |
|---|---|
| **File** (4) | `Load Program...`, `Save Program...`, separator, `Quit` |
| **Edit** (2) | `Copy`, `Paste` |
| **Machine** (11) | `Open-Apple-Control-Reset`, `Control-Reset  Ctrl+F2`, `Power On`, `Configure Slots...`, `Joystick...`, separator, `  Toggle Fullscreen`, `  Color NTSC`, `* Flashing text`, `  Mute`, `  Louder` |
| **CPU** (8) | `  Normal: 1MHz`, `  Fast: 3.5MHz`, `  Warp`, separator, `  Stop`, `  Running`, `  Step`, `  Next` |

**EVERY ROW OF A MARKED GROUP OWNS THE TWO-GLYPH COLUMN, whatever its state.**
`Fast: 3.5MHz` shipped without it while `Normal: 1MHz` and `Warp` - its two
radio partners - had it, and the sixteen pixels are VISIBLE: measured at
native resolution off `wave1-09-menu-cpu.png`, Normal's and Warp's label text
began at x=20 and Fast's at x=4, one row of a pull-down two whole cells out of
line. The same was true of `Color NTSC` and `Mute` against `Flashing text` in
Machine. MII ticks `mhz1`/`mhz3` and `vdc0` and `aud0` from ONE
`MUI_MENUBAR_ACTION_PREPARE` arm (`mii_mui_menus.c:139-175`), so every one of
those rows owns a tick slot there too. It is also what lets wave 5 revive
`Mute` without re-widening the row and shuffling every label under it.

**AND IN THE CPU MENU THE COLUMN IS THE WHOLE MENU'S, NOT THE MARKED GROUP'S.**
`Step` and `Next` shipped without it while the five rows above them had it, so
the pull-down had two labels starting two cells left of the other five - the
same sixteen pixels, one row-group along. In MII that cannot happen: the mark
is drawn INSIDE a left margin every item gets (`mui_menus_draw.c:73` - *"An
icon shifts the title right, a 'mark' doesn't"* - with `title.l +=
margin_left` unconditional at `:88`), so marked and unmarked titles start on
the same x. os8088's `menu.inc` has no such margin, so the column is spelled
into the label, and in CPU it is free: the longest label is 12 glyphs against
a cap of 24.

**MACHINE'S COLUMN IS THE ROW-GROUP'S, AND THE 24-GLYPH CAP IS THE FENCE.**
The five rows BELOW the separator - `Toggle Fullscreen`, `Color NTSC`,
`Flashing text`, `Mute`, `Louder` - all carry the column, and that is the fix
this wave's review asked for: `Color NTSC`, `Flashing text` and `Mute` had it
and the two around them did not, so **three labels in one group started two
cells right of the two beside them** - the rule's own words for the defect,
photographed in `w4r-31-menu-machine.png` and `w4r-54-cga-menu-machine.png`.
The arithmetic allows it: `Toggle Fullscreen` is 17 + 2 = **19** of 24 and
`Louder` is 6 + 2 = **8**.

**The five rows ABOVE the separator cannot follow, and that is a fact about
their names**: `Open-Apple-Control-Reset` is **24** with nothing prefixed and
`Control-Reset  Ctrl+F2` is 22, so the pair that would have to lose two glyphs
are the two whose text is fixed by the machine's own vocabulary. The
alternative - dropping the column from the markable rows and marking by some
other means - would need a mark the kernel's menu code does not have. **So the
trade is stated rather than left as an accident**: the video/audio group is
columned throughout, the reset/configure group above the separator is not, and
this is a cap on a 320-pixel screen rather than an oversight. Nothing in MII,
AppleWin or apple2emu shows a ragged edge, because none of them draws a mark
in the title - `mui_menus_draw.c` adds `margin_left` to every title
unconditionally, marked or not.

**THE PULL-DOWN CAP IS ELEVEN ITEMS** (`MENU_POPMAX`, kernel/menu.inc:208)
**and the item cap is twenty-four glyphs** (`MENU_MAXCH`, :236). Both are
facts about the smallest screen this OS runs on. Machine's sixteen rows do not
fit, so two of its sections are FOLDED on `c64menu.c`'s rule - *a section that
is entirely unavailable becomes ONE item, and that item is the section's
first*:

- MII's five video-tint rows (`Color NTSC`, `Color NTSC (Alt)`, `Color
  Mega2`, `Green`, `Amber`) are ALL unavailable on the windowed path, so they
  are one greyed `Color NTSC`;
- MII's `Louder`/`Quieter` pair is likewise one greyed `Louder`.

That is 11 rows exactly, with ONE separator, and it is measured rather than
estimated. `Open-Apple-Control-Reset` is 24 glyphs - exactly `MENU_MAXCH` -
and MII's Open-Apple glyph is spelled out because the kernel face is ASCII
32..126 and the glyph draws nothing; MII's `…` becomes three dots for the same
reason.

**FIVE rows have NO definer in any reference and are marked OURS in the
source**, and the count is written out because an earlier draft said "three"
and then listed five: `Load Program...` and `Save Program...` (section 12's
reason - there is no Disk II in this PR and a listing has to get in somehow),
`Copy` (apple2emu's Edit menu has `Paste` only), `Warp` (this port's speed
control, which `Fast: 3.5MHz`'s greying points at) and `Flashing text` (the
flash phase's own control, section 7.6).

**AND `Power On` IS NOT ONE OF THEM - IT IS AppleWin'S OWN WORDING.** It read
`Power cycle` in the first draft of `a2menu.c`, and that phrase is in no
reference's *user-visible* surface: AppleWin spells the cold boot `F2 (Power
On)` (`help/keyboard.html:15`) and titles its confirmation `Reboot`
(`source/Windows/WinFrame.cpp:2012`), while "power cycle" appears there only
as C++ identifiers and code comments (`Memory.cpp:2292`, `CardManager.cpp:286`,
`Card.h:50`). Grepping all three trees for the string returns nothing. The row
is `Power On`, 8 glyphs, cited to `help/keyboard.html:15` - transcribed, not
marked OURS.

**A check item's state is a `*` in the label**, apps/tracker's idiom, because
this kernel's menu has no check mark and its face has no glyph for one. MII
reads its own state from a TICK glyph.

**AND IN WAVE 1 EVERY COMMAND THAT NEEDS A 6502 IS GREYED**, not live and
useless: `a2_menu_state()` greys them off a single `a2_have_cpu` flag that
wave 2 sets. SPEC.md 47's rule is that nothing is live that can only refuse,
and "not in this build yet" in a toast is exactly that. Live in wave 1: `Quit`
(resident), `Toggle Fullscreen` (resident, section 6.3) and `Flashing text`
(the phase exists from wave 1).

**MII's dynamic retitling is kept**: Stop becomes Stopped and Running becomes
Continue.

**File > Quit is answered in the RESIDENT half** and not in the overlay,
because it is the one command that must work on a disk whose `APPLE2.OVL` is
missing. It goes through `OSAPI_WM_CLOSE`, spent from the WAKE and not from
`os88_oncmd`.

**AND IT DOES NOT CONFIRM, WHICH IS A DEPARTURE FROM ITS OWN CITED
AUTHORITY.** MII's `quit` action puts up
`mui_alert(..., "Quitting", "Do you really want to quit the emulator?",
MUI_ALERT_WARN)` (`mii_mui_menus.c:243-249`), and this port closes the window
straight away. The reason is that **the OS owns the close**: on os8088 a
window's close box is one click away with no confirmation at all, so a
confirming Quit would be the *slower* of two routes to the same loss and
would still not protect the fast one. Machine > Power On confirms because
there is no OS idiom behind it - it wipes 48K and leaves the window open, and
AppleWin confirms exactly that (`WinFrame.cpp:1997-2013`). The two rows are
inconsistent on the glass and this is why; recording it is the point, because
a citation that is only half true is the thing this file exists to prevent.

### 10.2 What is live

Load Program... , Save Program... , Quit, Copy, Paste, Control-Reset,
Open-Apple-Control-Reset, Power On (with the two-row confirmation), Toggle
Fullscreen, **`Color NTSC` on a VGA** (wave 5 - it enters `FSXM_VGA13`, and it
greys on CGA and Hercules with the number that cut their writers, section
13.4), Flashing text (outside the `CPU_8086` tier), **Mute** (wave 5), Stop /
Continue, Warp. **There is no `White` row**: MII's Video submenu has no such
item - White is apple2emu's tint list - and the windowed path's monochrome is
stated on `Color NTSC`'s greying instead of implied by a row that would do
nothing.

**`Color NTSC` AND `Toggle Fullscreen` ARE TWO DIFFERENT THINGS AND THE MENU
SAYS SO BY KEEPING BOTH ROWS.** Toggle Fullscreen is SPEC.md 11.2's WINDOW
LATCH - the frame becomes the whole screen, the desktop's mode and cursor and
event ladder all stay live, and the picture is the same 1bpp band magnified
(section 7.8's tier table). `Color NTSC` is SPEC.md 53's EXCLUSIVE BRACKET: the
app borrows the machine, the video mode is its own, and the picture is
280 x 192 palette indices. **MII HAS FIVE TINT ROWS** - `Color NTSC`,
`Color NTSC (Alt)`, `Color Mega2`, `Green`, `Amber`, all five separately in
`m_video_menu` (`mii_mui_menus.h:66-80`) and each independently ticked from
the `MUI_MENUBAR_ACTION_PREPARE` arm (`mii_mui_menus.c:139-150`) - and **this
port** folds them into one row that does something, which is `a2menu.c`'s own
rule 3. The sentence here used to read *"MII folds five tint rows into the
first of them"*, which credits the reference with this port's work and is
exactly the kind of claim this document's discipline exists to catch. The two
rows are not alternatives and neither replaces the other.

**Machine > `Power On`'s confirmation is TWO rows and not one:**

```
Are you sure you want to reboot?
(All data will be lost!)
```

**IT IS THE ABOUT PANEL WITH A KIND, AND THAT IS A DECISION RATHER THAN A
SHORTCUT.** AppleWin's `ConfirmReboot`
(`source/Windows/WinFrame.cpp:1997-2013`) is `MB_ICONWARNING|MB_YESNO` titled
`Reboot`, and the two rows above are its first two verbatim; the rest of that
box is about a `Confirm reboot` checkbox in a Configuration dialog this port
does not have, so it is **not transcribed** - a confirmation that points at a
control the reader cannot reach is the guess SPEC.md 47 forbids. What it needs
on this machine is exactly what the About panel already is (section 11): modal,
snapped to the band, holding the Apple scan lines it covers so nothing under
it is drawn, dismissed as DAMAGE and not as a repaint. A second copy would be
a second `ovl_about_geom`, a second hold range and a second arm in
`os88_paint`, and the two would drift; so it is `a2_pan_kind`, and
`A2_CFM_ROWS` = 3 is a 640x200 compatibility constant for the same reason
`A2_ABT_ROWS` = 10 is.

**Yes is button 0 and No is button 1** (`MB_YESNO`'s own order), the hit test
is RESIDENT and the two button rects are statics the overlay's drawing wrote -
a click is a callback and a callback is reached by a near offset, while only
code moves. **ANY KEY MEANS YES IS NOT A THING TO DO TO A ROW WHOSE SECOND
LINE IS `(All data will be lost!)`**: Enter and `Y` answer yes and every other
key - Esc, `N`, a letter typed at a machine the user had forgotten was behind
a card - answers NO and dismisses, as does a click anywhere that is not the
Yes button. **Nothing is latched by the PICK**; the ANSWER is what sets
`A2_RST_POWER`, and the wake is what spends it, because a power-on is a 48KB
fill and `os88_oncmd` runs under the desktop's lock.

**`Normal: 1MHz`'s body is WARP OFF.** MII's `mhz1` sets the machine's speed
back to `MII_SPEED_NTSC` (`mii_mui_menus.c:327-329`); this port has no throttle
at all, so what "normal" means here is the un-warped machine, and
`Normal: 1MHz` / `Fast: 3.5MHz` / `Warp` are the same three radio partners MII
has with the two reachable ends live. **Its MARK is not its state**: section
10.1's table ticks it from the MEASURED per cent on MII's own 0.9-1.1 band, so
on a host running the core at 2,000 % the row is unticked with warp off and
the status row is why. Picking it on an already un-warped machine does nothing
and says nothing, exactly as picking `Stop` on a stopped machine does - which
is MII's own behaviour and not a silent refusal.

**`Warp` is the wall slice's CAP and nothing else**, because there is no
throttle here to take off: `a2_slice` runs `a2_budget` cycles a wake and the
status row reports what that came to, so the only thing warp can lift is the
ceiling the adaptation walks the budget up to - `A2_SLICE_MAX` 16,384 to
`A2_SLICE_WARP` 30,000, still under the signed countdown's 32,767 (section
4.2). **The cap comes back down with it**, or an adapted budget would outlive
the warp it was granted for. On the `CPU_8086` tier it changes nothing and
says so (`Warp on - no change.`): at 4.77 MHz a 16,384-cycle slice is far more
than one host tick of work, so the adaptation settles the budget near its
256-cycle floor and the ceiling is not what binds. It DOES bind on a 286 or a
386, which is why the row is not greyed.

**`Stop` and `Continue` are TWO ITEMS and not one toggle** - MII's
`SIGNAL_STOP` and `SIGNAL_RUN` (`:333`, `:350`) - so picking Stop on a stopped
machine does nothing. The latch is `a2_pause`, its own flag and not an
`a2_state` value, because a paused machine is still a machine and a jammed one
is not; a stopped machine answers 0 to `a2_wants_wake` and parks.

### 10.3 Present and greyed - the fact that greys it (SPEC.md 47)

| item | the fact |
|---|---|
| **Machine > Configure Slots...** , in this PR | `No Disk II in this build. Load Program reads an Applesoft program, and Paste types a listing in.` **WAVE 4 RESTORED THE SECOND SENTENCE**, because it is the wave that wrote both routes. Wave 1 shortened the fact to its first half rather than point the reader at two routes they could not take, which is exactly the guess SPEC.md 47's rule 5 forbids; a greying may not outlive its reason either |
| Machine > Configure Slots... , once the Disk II follow-up lands: every slot but 6 | `Slot 6 holds a Disk II. There are no other cards in this port.` |
| Machine > Joystick... (already `.disabled = 1` in MII's own menu table, which is the authentic grey) | `The paddles answer centre. The game buttons PB0 and PB1 are F1 and F2 - a departure from AppleWin's Left-Alt / Right-Alt.` **WAVE 3 RESTORED THE SECOND SENTENCE**, because it is the wave that reads them: wave 1 shortened the fact to its first half rather than name a control the build did not have, which is rule 5's guess. They are host conveniences and GAME BUTTONS, not //e Apple keys - section 6.3 |
| Machine > `Color NTSC`, **on CGA and Hercules only, from wave 5** | `The window is monochrome, and the foreign modes this screen has measured SLOWER than it - 695 ms a frame against 632.` **BOTH HALVES ARE FACTS NOW AND NEITHER WAS BEFORE.** Wave 1 shortened this to the first clause because the second named a mode the build did not have; wave 5 wrote the mode AND measured `FSXM_CGA640` and `FSXM_HERC` out of it (section 13.4), so the row is **LIVE on a VGA** - it enters `FSXM_VGA13`, which is where this port is in colour - and greys on the other two adapters with the number that cut them. **The predicate is `os88_fsx_caps` itself**, asked where the answer is used and never banked: it is a question about a DISPLAY and a window moves between them on a two-display desktop (SPEC.md 39.18.2), and it is the same bit `os88_fsx_mode` would refuse on, so the greying and the refusal are one fact. **It is not a check item**, where MII ticks its five tint rows: MII's are a persistent MODE and this is a bracket that RETURNS, so a tick would describe a screen that is not on the glass |
| Machine > `Mute`, until the speaker lands | `There is no speaker in this build.` - and it is `a2_have_snd` that greys it, never a `D` baked into the literal, so **wave 5 revived the row with nothing else moving**, exactly as this line said it would. It is a CHECK item on a CAPABILITY, which is two questions about one row and not one: `a2_have_snd` says the machine has a square voice and `a2_mute` says the user switched it off, and greying the row that is ON would report the feature as unavailable AND make it impossible to un-mute |
| Machine > `Louder`, folding MII's `Quieter` | `The tone sink has no volume. This machine plays a bare square wave.` - PERMANENT, and the one audio row that stays greyed after wave 5. **THE FACT IS THE OS's AND NOT THE APPLE's, AND IT WAS THE OTHER WAY ROUND FOR A WAVE.** It read `The Apple's speaker is a one-bit toggle. There is no volume on it.`, which is a true sentence about the wrong machine and one that implies MII's own row is meaningless - **in MII this row is LIVE**: `ui_gl/mii_mui_menus.c:294-302` calls `mii_audio_volume(&mii->speaker.source, volume ± 1)`, a 0..10 sample multiplier (`src/mii_audio.c:80-89`), greyed only at the ends of that range (`menus.c:157-161`). Host playback volume has nothing to do with the speaker being one bit. **The departure is recorded rather than hidden** (section 10.2's idiom): MII's `Louder`/`Quieter` drive a host mixer, and what this OS gives a package is `os88_snd_tone(hz, ticks, prio)` - no amplitude argument, so the sink is a bare PC-speaker square-wave gate with nothing to turn up. That is a fact about wave 5's own sink, and it is the reason the row is permanent |
| Machine > `Flashing text`, **on the `CPU_8086` tier only** | `Flashing forces a text repaint 3.6 times a second. On a 4.77 MHz 8088 that is 43.1 ms each time and the machine would spend it on the phase rather than on the 6502.` **THE NUMBER IS FILLED IN AND THE ROW IS GREYED, FROM WAVE 3** (the tier table, section 7.8): the harness measures one flip at 43.1 ms with TWO flashing rows on the screen and at 3.64 flips a second that is 157 ms in every second of an 8088. Until this wave the item was LIVE on every tier, because refusing on a figure nobody had taken would have been the guess SPEC.md 47 forbids. **It is `a2_fl_ok` that both refuses and greys** - `a2_tier_init` clears it and `a2_menu_state` reads the tier beside it - so the refusal and the greying cannot disagree, and the row is greyed UNMARKED rather than greyed with a tick still beside it |
| CPU > Fast: 3.5MHz | `There is no 3.5MHz mode. CPU > Warp is this port's speed control and is beside it.` **It read `This machine runs at 1.02 MHz. Warp is this port's speed control and is beside it.` through wave 2's first form, and that is a claim about the MACHINE which the status row on the same screen refutes**: there is no throttle here at all - `a2_slice` runs `a2_budget` cycles a wake - and the measured figure is 2,180 % on the VGA desktop and 2,775 % on CGA. A greying may state what the BUILD does not have; it may not state a speed the glass above it contradicts (SPEC.md 47 rule 5). Wave 2 struck the `Warp` half with it, for a reason of its own - *the row it pointed at is greyed too, so it named a route the reader cannot take* - and **wave 4 put that half back, because the reason expired when `Warp` went live.** The first sentence lost `in this build` with it: what wave 4 removed is a 3.5 MHz MODE, and the build does have a speed control now, so the old wording said something the row two below it refutes. A greying may not outlive its reason and neither may the removal of one - the same test this wave applied to `Configure Slots...` and wave 3 to `Joystick...` |
| CPU > Step, CPU > Next | `There is no debugger in this port.` |
| CPU > `Stop` and CPU > `Continue`, **on a JAMMED machine only** | the fact is **already on the glass and is permanent**: `a2_status` draws `6502: JAM at $xxxx` in the message area for as long as `A2_ST_JAM` lasts (section 4.5), so these two rows are the one greying in this table with no sentence of its own and need none. The core never runs again after a jam - the slice, `a2_wants_wake` and the status row all test `a2_state == A2_ST_RUN` - so `Continue` was a LIVE item that set a flag nothing reads and then said `Running.` **over the top of the JAM line**, because the message arm is drawn ABOVE the jam arm and a jammed machine posts no wake to expire it. `a2_jam` already called `a2_menu_state` with the comment *there is no machine left to stop*; this is that sentence acted on. The launch spelling comes back with the `D`, because `Stopped` / `Continue` would describe a machine that could be continued |
| `.NIB`, `.WOZ`, `.2MG`, `.HDV` on the disk dialog's refusal, named rather than silently rejected (the follow-up PR) | `This build reads a 143,360-byte .DSK, .DO or .PO. A .WOZ is a flux image and needs a bit-cell model - a decision every four emulated cycles, which on a 4.77 MHz 8088 is the difference between a slow emulator and a stopped one.` |
| a wrong-sized disk image, on the "Invalid Disk Image" alert (the follow-up PR) | `File '<name>' is the wrong size, <size> too <big\|small>.` - MII's wording, with the size formatted human-readably as MII formats it, **not** as a raw byte delta |

**The speaker's synthesis fact** is stated on the status row and in section 8,
which is its only home now that a Mockingboard row is off the bar entirely.
**Wave 5 put it there**: `Square-wave tones only.` - 23 of the row's 26 cells -
said ONCE, the first time this machine actually makes a noise **or the first
time it makes one this build cannot turn into a tone** (section 8), rather
than at launch, where it would be a sentence about a feature the user has not
reached.
It is NOT in the About panel, which carries what the port IS and not how this
build renders (section 11).

**AND THREE GREYINGS ARE TEMPORARY AND SAY SO IN THE SOURCE**: in wave 1 every
command whose body needs a 6502 wears `OS88_MENU_DIS` off `a2_have_cpu`, which
**wave 2 set**; the commands whose BODIES wave 4 writes wear it off
`a2_have_cmd`, which **WAVE 4 SET** - Load Program..., Save Program..., Copy,
Paste, Power On (with its confirmation), `Normal: 1MHz`, Stop/Continue and Warp
all have bodies now, so the greying has stopped being true and 47 does not let
one outlive its reason; and `Mute` wears it off `a2_have_snd`, which wave 5
sets. They
have no user-visible fact because there is no user of a wave - what ships is
the PR - and rule 47's alternative, an item that is live and can only answer
"not yet", is the thing 47 exists to stop.

**IT IS TWO FLAGS AND NOT ONE, AND THE SPLIT IS WHERE THE BODIES ARE.** Wave 2
gave `Control-Reset` and `Open-Apple-Control-Reset` real bodies - section 4.5's
reset line is that wave's own subject and neither touches RAM - so
`a2_have_cpu` revives exactly those two. Everything else that needs a 6502
still has no body: Load Program..., Save Program..., Copy, Paste, Stop /
Continue and Warp are wave 4's. **`Power On` is on `a2_have_cmd` too and for a
reason of its own**: section 10.2 gives it a TWO-ROW CONFIRMATION, and a
data-loss row shipped without the confirmation its contract names is not the
item this document describes, so it comes alive in the wave that writes the
confirmation and not before.

**A `D` BAKED INTO THE LITERAL IS THE DEFECT THIS PARAGRAPH GUARDS AGAINST.**
`a2_menu_state()` rewrites EVERY row that a later wave revives - including
`Normal: 1MHz`, `Warp` and `Mute`, which a first draft greyed in the item
table and never rewrote, so setting `a2_have_cpu` would have left three rows
dead with no fact anywhere and no route back. `Normal: 1MHz` is the sharper
half: it is the CHECKED row, and greying the item that is ON reports the
feature as unavailable *and* makes it impossible to turn off.

### 10.4 What is absent, and why greying it would be wrong

**The whole //e machine**: 80STORE, RAMRD/RAMWRT, ALTZP, ALTCHARSET, 80COL,
**the DHIRES reading of `$C05E`/`$C05F`** and the aux bank. **NOT "AN3": the
annunciators AN0-AN3 at `$C058-$C05F` are a II+'s own** and are in the
soft-switch table where they belong (section 5.1) - it is only the //e's
*interpretation* of the AN3 pair as a double-hi-res switch that this machine
does not have. The //e keyboard's **Open-Apple and Solid-Apple keys** are
absent for the same kind of reason: what a II+ has is three game-button inputs
at `$C061-$C063`, which this port reaches through F1 and F2 as a host
convenience (section 6.3). AppleWin's `IS_APPLE2` / `IsAppleIIeOrAbove`
tests are the checklist of exactly where the fence is. **MII is //e-only and
is therefore a donor of algorithms and never of structure.**

**Six items that are in NO reference's UI**, and are therefore invented
surface rather than a port's honest refusal: the **Language Card** (a code
comment in AppleWin, never a control), **Integer BASIC** (a debugger comment),
the **80-column card**, **RAMWorks** aux memory, **Double hi-res** (not a row
in apple2emu's Video menu) and the **Mockingboard** (MII's Audio submenu is
Mute / Louder / Quieter). **Greying a //e card on an Apple II+ hands the
reader a //e checklist** and contradicts the paragraph above. The II+ / //e
fence is stated once, here.

**Integer BASIC as a second 12KB ROM part - decided, not asked.** A part costs
zero image and zero bss, so the segment cap does not bind; but the package
**FILE** would go from about 57,400 to **69,688** against The Wire's
`WIRE_FILEMAX` of **64,512**, which would force the Wire record to an archive
shape or to `floppy_only` with both buttons greyed. **Flat no, with that
arithmetic.**

**File > Print, File > Save state, File > Load state.** No definer in any of
the three trees, and this OS hibernates the whole machine (SPEC.md 52) and has
no printer path.

**The true floating bus** (section 5.2). **WOZ, NIB, 2MG, HDV and SmartPort
images, and disk WRITE** - read-only `.DSK` is the wave; a writable image
wants MII's overlay journal or an in-place rewrite of the user's file, and
neither is worth a wave here.

**Mockingboard, Phasor, SSI263, SAM, the Super Serial Card, the mouse card,
the printer card, the No-Slot Clock, the Z80 SoftCard, Uthernet and the Titan
accelerator** - about 25,000 lines of AppleWin between them, and every one is
a card in a slot this machine leaves empty.

**The symbolic debugger** (27,067 lines in AppleWin, 2,043 in apple2emu), the
mini-assembler, the disassembler and the console. A different program.

**A config file in `SYSTEM/APPDATA`.** The C64 has none either; nothing here
is worth remembering between launches that the OS does not already remember.

**The NTSC signal simulation** (AppleWin's `NTSC.cpp`, 2,857 lines) and MII's
AVX2 scanline filter. Not a computation an 8088 can afford at any frame rate;
the artifact colour is MII's ten-entry integer CLUT instead.

---

## 11. The About panel

`ovl_about_show` in `a2about.c`, on MII's panel shape and the C64's mechanics
(`C64-SPEC §12`): modal to INPUT, its close drawn as
**damage and not as a repaint**, the panel snapped to the cell grid so the
rows it covers are exact and `os88_paint` skips them, and **redrawn only when
the damage rect actually reaches its rect**.

**IT DOES NOT PAUSE THE MACHINE, AND THE CONFIRMATION DOES.** This section
read "modal, the machine paused while it is up" from wave 3 and the code never
did that; the review of wave 4 made the two agree, and it made them agree in
the direction the redraw budget argues rather than by pausing both. The About
panel's HOLD RANGE is what keeps the glass correct - the flush composes and
blits nothing under it - so a machine mid-`RUN` carries on behind it, and
stopping the 6502 because somebody opened About would be a behaviour change
with nothing asking for it. Machine > Power On's confirmation (section 10.2)
is the other way round on both counts: it is 52 pixels tall against the About
panel's 122, so it holds ~6 of the 24 character rows and a machine that is
printing kept composing and blitting the other ~18 on **every host tick,
~200 ms of the target apiece**, for as long as a human took to read two lines -
which also makes the Yes/No click feel lost on a 4.77 MHz XT, the wake being
busy drawing a picture the box is covering. And the answer is about to wipe the
machine, so there is nothing running behind it worth a pixel. `A2_CFM_UP()`
(apple2.c) is that one term, in the wake's slice arm and in `a2_wants_wake`,
so the app IDLES while the box waits instead of re-posting a wake a tick;
`a2uitest` counts `a2_run` calls across it. The panel is 1 fill + 2 frames +
8 `font_run` over 196 glyph cells - **~185 ms on the target** - so repainting
it on the latch alone made a partial expose with the panel up MORE expensive
than one without it, which inverts the whole point of the hold rows: a
pull-down closing over one corner of the window repainted the whole card.

**THE HOLD RANGE IS MEASURED BEFORE THE FLUSH READS IT, AND THE PANEL DOES NOT
SURVIVE A GEOMETRY CHANGE.** Both halves are one defect. `ovl_about_geom`
writes the panel's rect and the scan lines it covers, and it used to run only
AFTER the flush - so any `W_PAINT` that followed a geometry change held the
OLD lines (the flush skips them and nothing draws them: a full-width strip of
stale pixels under `WF_OWNBG`) and composed and blitted the NEW ones a moment
before the panel was painted over them, ~226 ms of pure double-draw that no
screendump shows. `os88_paint` measures first now. And Machine > Toggle
Fullscreen is reachable with the panel up - `os88_about` is the kernel's NAME
pull-down and a menu-bar click is not a `W_ONCLICK` - while `OSAPI_FULLSCREEN`
repaints the window whole and SYNCHRONOUSLY, so that paint runs NESTED inside
the call: the panel comes down FIRST there, its rect handed on as damage, and
the REFUSED arm owes those rows a draw because the kernel did nothing at all.
`apps/c64/c64.c`'s `c64_fullscreen_toggle` carries the same fix.

**THE HOLD RANGE IS IN APPLE SCAN LINES AND THE PANEL'S RECT IS IN SCREEN
PIXELS, AND AT 2x THOSE ARE NOT THE SAME THING.** This is the second place the
two coordinate systems cross - `a2_blank_rect` is the first - and wave 3
taught only the first of them. `ovl_about_geom` converted its rect with no
divide by `a2_sch` and set the panel's width to the constant `A2_BANDW`, so at
VGA fullscreen it **held Apple lines [131,191] while covering [66,125]: two
DISJOINT ranges**. Every one of the 62 lines under the opaque card was
recomposed and blitted straight over it on the next flush - eight rows of
compose and eight blits of pure waste per flush, for as long as the card was
up, which is exactly the double-draw the hold exists to prevent - and 65 lines
the panel did not cover were held and never drawn, so the bottom third of the
picture froze. The panel is `a2_gbw` wide now and placed at `a2_gsx`, so "as
wide as the band" is true at both magnifications, and the conversion divides
by `a2_sch` at the one place it happens.

**IT HOLDS ONLY LINES THE PANEL COVERS WHOLLY, AND IT IS SNAPPED TO THE APPLE
LINE GRID SO THAT THERE ARE NONE OTHER.** At 2x an Apple line is two screen
rows and the card's edge can fall inside one: a held line the panel does not
cover freezes a sliver, and an unheld line it does cover is drawn over the
card. `ovl_about_geom` rounds the panel's top and height to the grid at 2x -
at most one screen pixel of movement - so the two sets are identical and
neither case exists. `a2uitest` asserts the conversion by hand at 2x and then
watches every blit of a whole-frame flush: a blit landing on a held line is a
failure. Nothing saw this before, because the harness had fullscreen rows and
About rows and never crossed them.

**AND ITS CLOSE TESTS THE BORDER AND THE STATUS ROW RATHER THAN ASSUMING
THEM.** `a2_about_close` invalidated both unconditionally, which cost 42 glyph
cells - 37.8 ms - on every dismissal at a geometry where the panel cannot
reach the row: at the shipping content box the panel's foot sits 43 pixels
clear of it. `a2_blank_rect` already asks exactly those two questions of a
screen rect, so the close is one call rather than a second copy of the test.

**TWELVE ROWS MAXIMUM.** That is a 640x200 compatibility constant and it
carries a comment saying so: a control's y is `6 + row*10` and nothing clamps
it. The panel's width and height are clamped to the live content box, with the
OK button clamped inside the panel.

**What it carries, and this list is the binding part:**

1. the product - `Apple II Plus Emulator`, which is AppleWin's own
   `TITLE_APPLE_2_PLUS` (`source/Common.h:50`) and the window title's string.
   **The row carried a leading `The` and a citation to "MII's model row"
   through wave 3, and MII contains no such string**: the one row that names
   the product was the one place the name was not the product's;
2. the version - `APPLE2 1.0 for os8088`, and it is a **version**: the row
   read `APPLE2 for os8088`, which is the product name again one line down;
3. what this **port** is;
4. the four attributions - VICE, AppleWin, MII, apple2emu - with their
   licences;
5. **this package's own licence** - `GPL-2 or later - see COPYING`. The floppy
   is the distributed form of a GPL-2-or-later program (section 16.2) and the
   panel is where a reader is told so; `apps/c64/c64about.c:46-49` carries the
   identical row for the identical reason;
6. the ROM copyright line - the ROMs are Apple Computer's;
7. the porter - `Ported by Jorge Gonzalez`, the C64 panel's last row.

**Seven rows of content in a TEN-ROW budget**, which is the number the
paragraph above pins: the three that arrived paid for themselves by giving up
two of the three blank spacers, so `A2_ABT_H` did not move.

**AND NOTHING ABOUT HOW THE BUILD RENDERS.** The monochrome fact lives on its
Video grey (section 10.3) and the speaker fact on the status row (section 9).
This is the port skill's lesson 8, which removed exactly that from CWORD's
panel.

The `][+` glyph is spelled **"II Plus"**, because the kernel face is ASCII
32..126 and the glyph draws nothing here.

**The exact row text is fixed in wave 7** against the authority row's file;
the row CONTENT is the list above and nothing else.

---

## 12. Program load and save

**These two File rows have no definer in any reference** - MII's Load & Run
Binary is `.disabled = 1` with its handler commented out - so they are **this
port's own**, and the reason is stated: there is no Disk II in this PR, and a
listing has to get in somehow. They are written from AppleWin's
`A2_BASIC.SYM`, which names its own source (Bob Sander-Cederlof's S-C
DocuMentor: Applesoft).

| symbol | address |
|---|---|
| TXTTAB | `$67`/`$68` |
| VARTAB | `$69`/`$6A` |
| ARYTAB | `$6B`/`$6C` |
| STREND | `$6D`/`$6E` |
| FRETOP | `$6F`/`$70` |
| MEMSIZ | `$73`/`$74` |
| PRGEND | `$AF`/`$B0` |

**Program text begins at `$0801` on a II+.**

**Load Program...** uses `os88_fdlg` for the picker, then reads the
file into a **transient heap claim**, walks it there, and only then moves the
accepted program into `$0801` and writes `TXTTAB` = `$0801` and
`VARTAB` = `ARYTAB` = `STREND` = `PRGEND` = end.

**THE PICKER GETS NO DEFAULT NAME, AND `"*.BAS"` WAS A FILTER WRITTEN INTO A
SLOT THAT HAS NONE.** `os88_file_dlg`'s third argument is a default NAME;
SPEC.md 38.9 says outright that the dialog does no filtering by extension
("an Open dialog that hid `.TXT` from Note Pad would be a lie about what is on
the disk"), and 38.5 that Open mode has no field to type in - but the box is
still DRAWN with the name in it (`kernel/fdlg.inc`'s `fdlg_name_body` draws
`fdlg_name` in both modes and suppresses only the caret), and `fdlg_actok`
lights the Open button as soon as the name is non-empty. So the literal
`*.BAS` sat on the glass and **Open was live before anything was selected**,
committing the name `*.BAS` and ending at `Cannot read the file.` The OPEN
call passes **0**, which leaves SPEC.md 38.10's per-application last-picked
name to seed the box - what the slot is for. SAVE keeps `PROGRAM.BAS`.

**WHAT IS ACCEPTED IS DEFINED BY THE PROGRAM'S OWN STRUCTURE, NOT BY A
LENGTH-PREFIX SNIFF.** This document called the 2-byte prefix "documented" and
cited `A2_BASIC.SYM` for it; that file establishes the pointer addresses in
the table above and nothing about a file format, so the sniff was a heuristic
wearing a citation. It stays as a **hint** - if word 0 equals `filesize - 2`,
skip it - and the acceptance is the **walk**:

- a tokenised Applesoft program is a chain of lines, each `next-line pointer
  (2)`, `line number (2)`, tokens, `$00`, ending with a **`next` of `$0000`**;
- **every `next` must be greater than the current line's own address and
  within the loaded image**, so a truncated or wrong-typed file is refused
  rather than walked off the end of a 48K claim;
- **line numbers must be 0..63999** and must not go backwards;
- the walk ends at the terminator, and **the address it ends on is what the
  four pointers are written from** - not the file's length, which a trailing
  byte can make wrong.

**AND A HINT THAT CANNOT BE SECOND-GUESSED IS NOT ONE.** The test fires on a
**headerless** file whenever word 0 - which is then the first line's link,
`$0801 + len(line 1)` - happens to equal `filesize - 2`, i.e. whenever the
lines after the first total exactly 2,051 bytes. Such a file loads and RUNs on
a real Apple II, and this port refused it, with the wrong sentence, **on files
its own Save wrote** - Save is headerless. So the walk runs at the hinted base
and, if it fails, **at zero**, and only a file that walks at neither is
refused.

**THAT IS WHY THE WALK IS TWO PASSES.** `ovl_a2_walk(seg, base, end, fix)`
with `fix` = 0 **validates and writes nothing**; with `fix` = 1 it does the
same walk and repairs each link. A repairing walk at base 2 has already
overwritten the bytes a walk at base 0 would read as the first line's NUMBER,
so the fallback is only possible if the deciding pass is read-only. The
repairing pass then runs once, on the base that was accepted, and cannot fail.
It costs one extra pass of four peeks and one `a2_scan0` a LINE, once per Load
Program.

**A file whose links point at another load address is REPAIRED and not
trusted**, which is what Applesoft itself does: its own **`FIX.LINKS` at
`$D4F2`** (`AppleWin/bin/A2_BASIC.SYM:224`) rebuilds the chain from the line
lengths. The port does the same walk on the way in - each `next` recomputed
from where the line actually landed - so a program saved from a machine whose
`TXTTAB` was not `$0801` loads and RUNs here. A file that fails the walk is
refused **by name and by the reason**, on the status row (section 9), and
nothing is written into the machine's memory before the walk has passed: a
half-loaded program is a `]` prompt that crashes on RUN with nothing saying
why.

**THE WALK'S INNER LOOP IS `a2_scan0` (`a2mem.inc`), ONE CALL A LINE.** A line
ends at a `$00` and `FIX.LINKS` finds it by scanning; written in C that is one
`os88_peek` a BYTE - ~235 ms for a 5KB program. What the walk costs is four
peeks, one scan and two pokes A LINE, twice over, which is a bound this
document can state.

**AND NONE OF IT RUNS UNDER THE GFX LOCK.** `os88_onfile` is dispatched with
the desktop's lock held - `kernel/fdlg.inc:45-48` states it of `fdlg_open`,
the window procs and `fdlg_commit` alike - and the body is up to six
`os88_mem_claim`s (each of which may **compact** an arena this package has a
pinned 64KB in), a floppy read of up to 46 KB (`os88.h` prices 116KB at ~ten
seconds of motor), the walk and a 48KB-capable block move; Save is the same
shape with `os88_file_write_seg`, which `os88.h` flags as stalling every
painter for its duration. Run inline that is **seconds of frozen pointer,
frozen dock and every other task's painter blocked in `os88_gfx_lock`**, on
the very target this port is for - and it defeated the `a2_ovl_ready` fence on
the line above it, which exists so a locked caller does not go to the floppy
for the 4 KB overlay. **The handler LATCHES** - the name copied (the kernel
reuses `fdlg_name`), the mode and the size stored, one wake posted - **and the
wake spends it**, beside the launch document's arm, which has always taken
exactly this route. `a2uitest`'s `do_file` asserts that neither a claim nor a
file operation happened before the lock came off. Its sentinel is **`$FFFF` and not `-1`**: a 48K program's offsets reach
47,103, which read as a negative `int`, so a `< 0` test would refuse every line
past the 32KB mark of a large listing.

**BOTH ARMS END AT ONE CEILING, AND `A2_PRGMAX` IS 47,103.** `$C000 - $0801`
is 47,103 and the constant's own comment said 47,615, which is where the
association arm's flat 47 KB claim came from: 47 KB is 48,128, **1,025 bytes
above the ceiling the size-known arm refuses on**. A `.BAS` between 47,104 and
48,128 bytes double-clicked was therefore read and walked, and the walk's only
ceiling is `next < $C000` - so a program whose terminator landed at `$BFFF`
PASSED, one byte was written at Apple `$C000` (outside the 48K the `a2_wr`
fence protects), and `TXTTAB`..`PRGEND` were set to `$C001`, above `MEMSIZ`.
The status row said `Loaded` for a program the machine cannot RUN. `cap` is
clamped to `A2_PRGMAX` on both arms, so `end` is bounded and the two arms
refuse the same file; `a2uitest` loads a 47,104-byte well-formed chain and
requires `Too large for a 48K Apple.` with `$0801` untouched.

**AND THE ASSOCIATION ARM'S CLAIM STEPS DOWN.** Asking for the whole ceiling
whatever the file is asks the desktop for a 46 KB **pinned** claim on top of
this package's already-pinned 64 KB, and the commonest launch document is a
one-cluster listing: on a busy 640KB machine the answer was `No heap for the
program.` about 45 bytes. The claim halves until it is taken.

**AND THE READ ITSELF IS WHAT SAYS THE FILE DID NOT FIT.** The first cut of
this decided it from `got == cap` after the walk had failed, on the belief
that a read can be **cut off** by a short buffer. No os8088 kernel does that:
`kernel/diskw.inc:1836-1845` compares the directory entry's 32-bit size
against the caller's capacity **before any data I/O** and answers `FERR_BIG`
with the destination untouched, which `apps/cc/os88.h` states in words. So an
oversized file arrives as `got` = 0 with `os88_ferr()` = `FERR_BIG`, and the
arm that reads it names the ceiling that bound it - `Too large for a 48K
Apple.` when `cap` reached `A2_PRGMAX`, `Too large for free memory.` when the
heap is what bound it. The old arm was not merely unreachable: `got < 4` fired
first, so an over-large association load was refused as `Cannot read the
file.` **The gate that said otherwise passed on a HOST STUB that truncated**,
which is the port skill's lesson 7 with the sign flipped, and the stub refuses
the way the kernel refuses now. `a2uitest` gates both sentences on the same
47,104-byte file, once against `A2_PRGMAX` and once against an 8 KB heap.

**Save Program...** writes `$0801` to `VARTAB - 1`, headerless. `VARTAB` is the
end of the program INCLUDING the two zero bytes of its terminating link, which
is why a Save and a Load round-trip byte for byte: `NEW` leaves `TXTTAB` =
`$0801` and `VARTAB` = `$0803`, so a saved empty program is two zero bytes.
Both figures are measured on the glass - `PRINT PEEK(103)+PEEK(104)*256` reads
2049 at a fresh `]` and `PEEK(105)/(106)` reads 2051.

The bodies are `ovl_*` in `a2prog.c`, called by the **resident**
`os88_onfile`, which does the `size_hi` refusal (`Too large for a 48K Apple.`)
before the disk is touched. Every other refusal is SAID on the status row by
name and by reason - `Not an Applesoft program.`, `No heap for the program.`,
`Cannot read the file.`, `Cannot write the file.`, `Bad program pointers.`,
`No program to save.`, `Too large for free memory.` - and each answers 1, so a **0 at the call site can only
mean the runtime refused the module** (`a2cmd.c`'s rule).

### 12.1 The association, and what a double-click has to wait for

**`CC_ASSOC` declares `BAS` from wave 4** (`a2assoc.inc`, turned on in
`apple2.asm`), so a `.BAS` beside the package opens on the FIRST double-click
of a COLD boot with no prior run - the mount's icon harvest reads the block out
of the header's first sector, which the SDK's runtime `os88_assoc_set()` cannot
do.

**A double-click hands the package a NAME AND A FOLDER AND NO SIZE**, which is
the arm the picker never takes: the Standard File dialog reads a size out of
the mount snapshot so the program can refuse before the motor spins. With no
size the claim is what `os88_mem_largest_kb()` can spare, capped at
`A2_PRGMAX` and **stepped down until the heap takes it** (section 12's two
paragraphs on the ceiling), and **the READ's own answer is the size**.
`os88_arg_file()` is read-and-clear, so it is banked in `os88_main` and spent
in the wake, which holds no lock, may call the file slots, and is the only
place an `ovl_*` can be reached from at launch.

**AND IT WAITS FOR THE MACHINE TO REACH `]` FIRST.** The first wake happens the
moment the window is on the glass, and at that point the 6502 has run a few
hundred cycles: the Autostart Monitor has not handed over to Applesoft yet, and
**Applesoft's cold start ENDS IN A `NEW`**. A load spent on the first wake
lands in `$0801` and is wiped by the ROM a moment later, and the glass shows a
`]` prompt whose `LIST` is empty with nothing saying why - which is what the
first cut of this did, and which no screendump that does not type `LIST` can
see. The condition is the cold start's OWN OUTPUT and not a wall-clock guess:
`TXTTAB` = `$0801` and `VARTAB` >= `$0803`, where the power-on pattern leaves
`$67`/`$68` = `$00`/`$FF`. A wall clock would be wrong at both ends - the
target runs the Apple at a few per cent of its own speed, so a cold start that
is one emulated second is tens of wall seconds there and a fraction of one
under an emulator. The wait is **bounded at one minute of host ticks** and
gives up with `No ] prompt to load into.`, because a load left armed for the
session would otherwise fire on the user's own `NEW`.

**AND THE WAIT MAY NOT OUTLIVE THE 6502.** `a2_argp` sat in `a2_wants_wake`'s
LATCH arm, above the running gate, on the argument that *what it is waiting for
is the 6502* - which is precisely the reason it must sit BELOW that gate. A
wait is not a wake's worth of work, and three machines can never satisfy it: a
**jammed** one (`A2_ST_JAM`), one the user stopped with **CPU > Stop**, and one
sitting behind the **Power On confirmation**, which `A2_CFM_UP()` was added in
this very wave to stop the app re-posting for. Each of those wakes did two
`a2_rd16`s and posted another - the 100 % spin SPEC.md 8.1.2 exists to remove,
on the SHARED UI task, for a whole minute. It rides the running arm now, which
is true whenever the ROM can reach `]`, and a stopped machine idles.
`a2uitest` arms a launch document on a jammed machine and requires
`a2_wants_wake()` to answer 0.

**`CC_ASSOC` declares `BAS`** (SPEC.md 54.6), so a `.BAS` opens on the FIRST
double-click of a **cold** boot with no prior run - which CWORD's runtime
`os88_assoc_set` could not do. **`apps/apple2/a2assoc.inc` exists from wave 1
and is NOT `%include`d until the wave that makes Load Program work**: the file
is a written prerequisite in the Makefile because make cannot see through a
`%include`, and the `%define CC_ASSOC` line in `apple2.asm` is what the later
wave adds. Declaring an extension the build cannot open means a double-click
LAUNCHES THE EMULATOR AND THEN REFUSES, which is worse than no association -
the user has spent a floppy seek, a 64KB claim and a window to be told no. `DSK`, `DO` and `PO` are declared **only when
the Disk II wave lands**: declaring an extension the build cannot open
launches the emulator and refuses, which is worse than no association.

---

## 13. The foreign video mode

**The user's explicit requirement, for colour and - if it measures - for
speed.** SPEC.md 53's exclusive bracket, reached through four
`OSAPI_FSX_*` slots that already exist and are deliberately unwrapped for C.

### 13.1 The three writers

| mode | geometry | what it is for |
|---|---|---|
| **`FSXM_VGA13`** | 320x200x256, the Apple's 280x192 centred | **THIS is where the port is in colour.** Lo-res's 16 colours and hi-res's artifact colours straight into the DAC, with **no dithering** |
| **`FSXM_CGA640`** | 560x192 inside a 640x200 mono frame | **CUT** - the Apple's native monochrome geometry, and slower than the window it came from (below) |
| **`FSXM_HERC`** | the same 640-wide viewport in Hercules' four banks | **CUT**, with it |

**`FSXM_VGA13` SHIPS ON ITS COLOUR, WHICH WAS NEVER IN QUESTION. `FSXM_CGA640`
AND `FSXM_HERC` WERE A SPEED CLAIM, THEY WERE MEASURED, AND THEY ARE CUT.**

They are 560 x 192 = **13,440 bytes a frame against the windowed 7,680** -
1.75x the raster work on the slowest machine, in the name of speed - so "an XT
gets its speed back" was a claim to prove and not to assert. Wave 5 wrote all
three writers, benched them and **lost the argument for two of them**
(section 13.4 has the table):

| change set | windowed 2x | `FSXM_CGA640` / `FSXM_HERC` |
|---|---|---|
| a whole frame, every line dirty | **632.6 ms** | 695.4 ms |
| one character row - a keystroke | **26.4 ms** | 34.8 ms |

**AND THE REASON IS NOT THE RASTER THIS SECTION ARGUED ABOUT.** Both paths
compose with `a2_band_text` and double with `a2_band_x2`, byte for byte the
same routines over the same source; what differs is the EMIT, and one
`os88_gfx_blit1` of 640x16 measures **8.875 counts** against sixteen
`a2_fsx_put` compares at **2.0** each. A band that is going down WHOLE does not
need a span compare at all, and the kernel's blit is the cheaper call. The
1.75x was real and was not what decided it.

So **Machine > Color NTSC greys on CGA and on Hercules** with that number, and
*"this port is in colour"* is a claim about a **VGA-class machine** - which is
what section 13.1 already said rather than implying an XT gets colour.

**AND THE ROW IS LIVE ON THE CPU_8086 TIER, WHICH IS A DECISION AND IS
PRICED HERE RATHER THAN LEFT TO BE INFERRED.** `a2_fsx_avail` asks
`os88_fsx_caps` about the DISPLAY and nothing about the CPU, so an 8088 with a
VGA enters `FSXM_VGA13`. What it costs there, all four figures measured by
`tests/a2band` at `-icount shift=3` and converted at 0.359 ms a PIT count:

| inside `FSXM_VGA13`, on a 4.77 MHz 8088 | |
|---|---|
| the first frame of a session - every line owed | **3,376 ms** |
| one character row, narrowed to its group span | **31.2 ms** |
| a **SCROLL** - 184 lines recomposed (section 13.2) | **1,747 ms** |
| an idle tick, nothing written | one byte read (`a2_wrote`) and `fsx_wait`'s halt |

Section 7.8's tier table greys **Machine > Flashing text** on that same tier
for a cost it prices at 157 ms in every second, which is roughly a twentieth
of the scroll above - so the two are not being judged by one rule, and the
reason is that the flash phase is a cost the user did not ask for on a
timer, where colour is a cost a user asks for once by picking a menu row and
leaves with a chord. Colour is the wave's headline feature and the maintainer's
stated decision was that `FSXM_VGA13` **ships on its colour whatever the bench
says**; greying it on the target machine is a scope cut and is therefore the
maintainer's to take, not this document's. **What is not left to inference is
the price**, which is the table above.

**AND FROM WAVE 7 THE PRICE IS SAID ON THE GLASS, ONCE.** The row stays live
on every tier - that is the decision above and it did not move - and the
FIRST colour session on the `CPU_8086` tier leaves
**`Colour: 1.7 s a scroll.`** on the status row: 23 cells of 26, and the
number is section 7.9.4's measured **1,747.2 ms** for a SCROLL - 184 lines
recomposed - rounded to the tenth of a second a reader can use.

**IT NAMES THE OPERATION AND NOT AN EVENT THAT SOMETIMES CAUSES IT**, which
is a second review correction to the same row. The draft that shipped into
review read `Colour: 1.7 s per RETURN.`, and 1,747.2 ms is a scroll of the
whole 24-row TEXT page. `GR` and `HGR` set the Apple's text window to rows
20-23 (section 7.4), and `WELCOME.BAS` selects exactly that, so a RETURN in
MIXED moves three rows up and clears one - **32 scan lines, ~304 ms** by
section 7.9.4's own 7.34 + 2.15 per line - and a RETURN that is not on the
bottom line at all is one row's **140.4 ms**. So `per RETURN` was **5.7x
high in the two modes half of this port's own demo lives in**, and `a
scroll` is true in every mode. It is the same standard section 16.2 applied
to `WELCOME.BAS`'s own `40 X 40`: a number this document prints has to be
true of what the reader is looking at.

**IT IS THE RECURRING COST AND NOT THE ENTRY COST, WHICH IS A CHANGE THIS
SECTION MADE AFTER REVIEW.** Section 7.9.4 measures three figures on this
tier: **3,365.3 ms** for a whole first frame, **140.4 ms** for one character
row - a keystroke - and **1,747.2 ms** for a scroll of the full TEXT page.
The first draft said the 3.4 seconds, and that is the
one number of the three that cannot change what the reader does next: it is
paid once, it is already behind them by the time the status row exists again,
and a reader who enters colour and stays for a session is paying 1.7 s a
scroll with nothing said about it. The row is one message long, so it says the one a
reader can act on. `a2_fsx_told` is the latch and it is once
per RUN, on `Square-wave tones only.`'s shape (section 8) - a fact is news the
first time and furniture the second. **It cannot be exercised on either
emulator in this tree and section 16.4.1 says why**: the combination it needs -
an 8088 with a VGA that offers `FSXM_VGA13` - does not exist here, so what
stands behind it is the length gate, `a2uitest`'s `msgs[]` and four lines
under one flag.

**IT IS SAID ON THE WAY OUT AND NOT ON THE WAY IN**, and that is the status
row's arithmetic rather than a preference: `a2_say` stamps `a2_msg_until` five
seconds ahead, the bracket owns the whole screen for as long as the reader
stays in colour, and **there is no status row under it to read one on**. A
message raised before `os88_fsx_run` has expired unread by the time a row
exists again. The first tick after the bracket returns is the first moment the
sentence can be read, and by then the reader has spent the 3.4 seconds of the
first frame and knows what a line costs before deciding to come back. The three refusals (`No colour on this screen.`, `No memory for
colour.`, `The screen refused it.`) still win where they apply, because they
are said on the arms that never reach the bracket at all.

**WHAT THE CUT GAVE BACK IS NOTHING, AND THAT IS THE DESIGN AND NOT A
DISAPPOINTMENT.** Section 15.4's lever 3 - *"the Hercules writer becomes the
CGA640 writer at a different stride"* - was taken at the DESIGN rather than as
a rescue: `a2_fsx_put` is ONE routine for all three writers and what makes a
CGA frame different from a Hercules one is the caller's offset. The ~1,200
bytes this section booked against them were booked against a shape with three
routines in it. The two that were cut were never a second routine, so cutting
them costs and saves nothing but the C-side frame loop that was never
written - and `a2_fsx_put`'s 70-byte path stays, because it is the same code
`FSXM_VGA13` runs.

**`FSXM_VGA13` is VGA-only**, so *"this port is in colour"* is a claim about a
**VGA-class machine** and this document says exactly that rather than implying
an XT gets colour.

### 13.2 Every foreign frame is driven off the dirty-line set

**A full raster write per frame is precisely what the dirty-page bitmap, the
write window, the scan-line map and the span compare exist to avoid**
(PERFORMANCE.md Part 5). So the foreign writers are driven off the **SAME
dirty-line set the windowed flush computes**, against a **foreign-frame
shadow in a heap claim**.

The arithmetic that makes this non-negotiable, **MEASURED** in wave 5 and an
order of magnitude worse than this section planned: VGA13 at 280 x 192 =
53,760 pixels with a per-pixel artifact index is **3,376 ms a frame on a
4.77 MHz 8088** against the ~300 planned, and one character row is **140.7**. A
whole-frame write sixty times a second is not a thing that could ever have
happened; driven off the dirty-line set, an ordinary keystroke **that does not
scroll** composes and compares eight scan lines and answers "nothing moved"
for the other 184 at 1.08 ms each.

**AND A KEYSTROKE THAT DOES SCROLL IS A WHOLE-FRAME RECOMPOSE, WHICH IS
STATED HERE BECAUSE IT IS NOT FIXED.** The sentence above used to read *"an
ordinary keystroke"* with no qualifier, and a RETURN typed at the bottom line
of an Applesoft session is the most ordinary keystroke this machine has.
`a2_flush` answers a scroll with `a2_shift_test` - forty source bytes a row,
EXACT - one `os88_gfx_scroll`, one blit and five groups: **41.9 ms**
(section 7.9.1's own row).
`a2_fsx_frame` has **no counterpart**: it starts at `a2_dirty_scan` and there
is no shift block below it, so the ROM's line-by-line copy marks all 184 lines
and every one of them is composed full width and written. Measured as its own
bench row - `SCROLL FSXM_VGA13 (184 lines)`, section 13.4 - that is **184 x
(a 280-pixel text row + a differing put)** and lands where the arithmetic said
it would: **4,867 counts = 1,747.2 ms of a 4.77 MHz 8088**, **41.7x** the
windowed path's answer to the same event. The row checks against its own
parts: 184 x (20.500 + 6.125) = 4,899 against the measured 4,867, and it read
4,863 / 4,868 / 4,867 over three runs.

**WHY IT IS A NUMBER AND NOT A FIX.** The windowed shift is not one routine,
it is a second damage model: `a2_shsrc` maintained per composed row,
`a2_sh_mkey` and `a2_sh_phase` as the two proofs that the recorded sources
still describe the recorded pixels, the probe budget, the `a2_lnf` refusal, the
vacated rows' forced marks and their flash flags. A foreign copy of it needs
all six plus a mover for the framebuffer AND the shadow, and **the 54,000
resident gate has 614 bytes in it** (section 15.0.5, which is where the live
figure is kept - it was 716 when this paragraph was written against wave 5's
line, and every wave since has spent from it). So the omission is recorded with its price and with
the shape of the fix - one `rep movsw` of `(192 - 8k) x 280` bytes up `k x 8`
rows in each of the framebuffer and the shadow (~280 ms against ~1,750, a 6x),
plus `a2_fsx_frame` maintaining `a2_shsrc` with `a2_rowcopy` exactly as
`a2_flush:1942` does - rather than left as a claim the code does not keep.

**AND THE FRAME IS GATED, WHICH IS `os88_onwake`'s OWN GATE ONE PATH ALONG.**
`os88_fsx_wait(FSXW_TICK)` returns 18.2 times a second and `a2_fsx_main`
called `a2_fsx_frame` on every one of them, with no "did anything change"
test - so an idle colour session paid `a2_dirty_scan`'s 24 row probes plus,
per row, the whole prologue below, to discover that the 6502 had written
nothing: **17-21 ms a tick in text and 23-29 in hi-res**, a third of a
4.77 MHz 8088, eighteen times a second. The windowed flush has had both halves
of the gate since wave 1 (`a2_wrote()` then `a2_dirty_any || !a2_sh_ok || ...`),
and the foreign loop now has the same one: `a2_wrote()` is the single term the
C side cannot see, and every other producer already sets `a2_dirty_any`
because they all go through `a2_line_dirty`/`a2_line_force`. An idle colour
session is one byte read and `fsx_wait`'s halt.

**AND IT IS PACED BY TIER TOO, WHICH IS `a2_flush`'s SECOND TERM ONE PATH
ALONG AND WAS MISSING FOR A WAVE.** Section 7.8's tier table gave the windowed
flush its `a2_tier_slow ? (t - a2_fltick >= 2) : (t != a2_fltick)` because a
496.8 ms repaint cannot keep up with a 55 ms tick. **The foreign frame is more
expensive, not less** - 140.4 ms for ONE character row against the windowed
row's 26.4, a **5.3x** - and it was issued at 18.2 Hz with no such term at
all. The consequence is arithmetic rather than taste: on the `CPU_8086` tier
`a2_budget` sits at `A2_SLICE_MIN` = 256 (section 16.4.1), a one-row frame is
**2.5 ticks during which no slice runs**, `fsx_wait` then returns at once, and
the loop spends the session at one 256-cycle slice per ~200 ms where the
window gets one per 55. `a2_fsx_main` carries the tier term now - **four**
ticks where the window takes two, which is the ratio the row costs - so
several slices accumulate per frame instead of one, at the **same pixels**:
the dirty set is exact and accumulates across the skipped ticks, so a deferred
frame draws the same picture later rather than a different one. What it costs
is latency, which is the trade section 7.8 already took for the window. It
cost **80 bytes of image and 2 of bss** (section 15.0.5), and `!a2_fsx_ok` sits
outside the pacing on both arms because the first frame of a session is owed
all 192 lines and is what stamps `a2_fsx_tick` in the first place. **Wave 7
measured that tier and shipped a sentence about it without pricing the loop
the sentence is about**, which is what the review caught.

**AND THE GATE FOR IT IS A TEXT ROW, WHICH IS SAID RATHER THAN DRESSED UP.**
`apps/apple2/build.sh`'s `a2pace` requires BOTH redraw paths to name a tier
term and a stamp - `os88_onwake` on `a2_fltick`, `a2_fsx_main` on
`a2_fsx_tick`, `a2_tier_slow` in each - and refuses a `a2_fsx_tick` that
nothing assigns. It is a text gate because neither of the two things that
would be better can reach this loop: `a2uitest` drives the bracket with the
6502 STOPPED, so nothing marks a line between iterations and neither arm
composes a second frame to count; and no emulator in this tree can host the
slow arm at all, for section 16.4.1's reason - it needs a `CPU_8086` machine
with a foreign-mode-capable VGA and there is none. What the row refuses is the
SHAPE going away, which is exactly how the term went missing: the gate was
copied from `a2_flush` and the pacing beside it was not. Its negative control
was run - deleting the term gives
`a2pace: apps/apple2/a2scr.c: a2_fsx_main does not mention a2_fsx_tick`, exit
1 - and restoring it passes.

**AND THE ROW'S EARLY-OUT IS FIRST, WHICH IS THE THIRD SUBSTITUTION THIS
SECTION USED TO SAY THERE WERE ONLY TWO OF.** `a2_flush` computes `drew` and
the scan-line range in ONE pass over the row's eight lines and returns out
*before* `a2_row_mode`, `a2_row_base`, the span predicate and `a2_span_of`.
The foreign copy ran all of that - fifteen calls in text, twenty-three in
hi-res - for **all twenty-four rows** and only then discovered in the per-line
loop that twenty-three of them had not one marked line: ~5 ms a frame in text
and ~10 in hi-res, against the 15.7 / 31.2 ms of real work the narrowing was
written to buy. The pass is hoisted now and it answers `rowf`, `drew` and the
`ls0..ls1` range together, exactly as `a2_flush`'s does; the skipped path still
spends `a2_rowwide[r]`, or the narrowing would never re-engage.

**AND IT IS DRIVEN BY COLUMN AS WELL AS BY LINE**, which the first cut of this
wave was not. `a2_dirty_scan` fills `a2_wlo`/`a2_whi`, `a2_rowwide[]` and the
page bitmap for the foreign frame exactly as it does for the windowed flush,
and `a2_fsx_frame` read none of it: every dirty row composed all forty cells
and every line compared all 280 bytes, so an Applesoft `COUT` writing one cell
cost 140.7 ms where ~16 was owed. It takes `a2_flush`'s **own** group span now,
term for term with `a2_fsx_ok` standing in for `a2_sh_ok`, so the two paths
cannot narrow differently - **31.2 ms**, section 13.4's own bench row.

**HI-RES PADS THAT SPAN BY ONE CELL EACH SIDE and the 1bpp band does not.**
Artifact colour is decided by a pixel's NEIGHBOURS (`a2fsx.inc`'s eleven-bit
window), so a write inside cell *k* moves pixels in *k-1* and *k+1*; the
windowed composer has no artifact colour at any width and owes no such padding.
Without the pad the span is right about the SOURCE and wrong about the
PICTURE, and the wrong pixel is then recorded in the shadow and stays for the
session. `a2_fsx_row` takes a cell range and seeds a hi-res range's cross-cell
state - the byte before it, the byte after it, and the cell's parity - from the
source, and `tests/a2band`'s `ab_spanck` requires eight cells composed as a
RANGE to equal the same eight composed as part of the whole row before it times
either.

**THE SHADOW IS MADE TRUE AT ENTRY, NOT LEFT UNKNOWN - AND THAT DISTINCTION
COST A VISIBLE DEFECT.** `a2_fsx_main` clearing `a2_fsx_ok` defeats the
per-LINE skip and says nothing to `a2_fsx_put`'s per-BYTE compare one level
down. The heap gives the 53KB claim back with whatever was in it, and after a
free and a same-size claim that is very often the LAST session's shadow - so
the compare answered "nothing moved" for every line the picture still agreed
with and wrote nothing, against a framebuffer SPEC.md 53.4 had just cleared.
**Seen on the glass: entering Machine > Color NTSC a second time on an
unchanged hi-res screen drew three of its six lines and a truncated pair of
verticals.** SPEC.md 53.4 is binding that the mode set clears the screen, so
`a2_fsx_main` ZEROES the shadow at entry (`a2_fsx_zero`, 53,760 bytes once a
session): it then describes the glass exactly and every compare from the first
frame on is sound. It is also the cheaper arm, because the black parts of the
picture are already black and are not written. The gate is
`hosttest/a2uitest.c`'s `h_claim_keep` row, which hands a re-claimed slot back
unchanged the way the heap does, and its negative control draws **0** lit
pixels where the first session drew 27,344.

**`a2_fsx_put` IS THE SPAN COMPARE ONE GEOMETRY ALONG** and is what makes that
sentence true: it answers 0 for a line that has not changed and writes only the
differing run when it has - to the framebuffer AND to the shadow, one pass over
each. `hosttest/a2uitest.c` asserts the drive rather than the pixels: the first
frame of a session writes **192** lines because the shadow is a fresh claim,
and three frames with nothing changing write **192 in total**. A per-frame
raster write would be 576.

The foreign-frame shadow is **13,440 bytes for CGA640 and HERC, 53,760 for
VGA13**, claimed at the **fullscreen-LATCH**, where a refusal is legal and
greys the item with the arithmetic. **The flush may not refuse; entering
fullscreen may.**

### 13.3 The bracket's rules, obeyed by hand

**Nothing in the toolchain enforces any of these**, which is why the whole
bracket lives in one function whose header is this list:

- the entry passed to `os88_fsx_run` is a **plain resident C function** and
  **must never be an `ovl_`** - `tools/cc8086.py` refuses that address by
  name;
- after the first `os88_fsx_mode`, **every drawing slot renders DESKTOP
  geometry** and is off-limits until the bracket returns;
- nothing may touch PIT channel 0, the sound ports or `int 10h` mode sets;
- keys come from a **polled `int 16h`** and the mouse from `OSAPI_MOUSE`, with
  the app edge-detecting buttons;
- **frames are paced with `os88_fsx_wait` and never `task_sleep`**;
- no events are dispatched;
- the exit is **the proc returning**, and the chords are **Ctrl+F and
  Alt+Enter** - this port's own (section 6.3) and NOT SPEC.md 53.7's bare `f`
  with Esc, for 53.7's own stated exception one paragraph along: **the Apple
  II+ owns both of those keys.** `f` is a letter and every letter goes to the
  machine; Esc is the Monitor's ESC-I/J/K/M and the Applesoft screen editor's.
  A port that swallowed either would be a machine you cannot type at, in the
  mode whose whole point is looking at it. `os88_onkey` binds the identical
  pair for the SPEC.md 11.2 surface, and both scan codes for Alt+Enter - 0x1C
  classic and 0xA6 enhanced - are taken rather than one being guessed. The
  ~200 ms `wm_paint_all` the exit costs is paid once a session.
  **AND THE ENHANCED ONE WAS DEAD CODE FOR A WAVE, WHICH IS A SIXTEEN-BIT
  DEFECT AND NOT A KEYBOARD ONE.** `a2_fsx_key` answers `int 16h`'s AX -
  `(scan << 8) | ascii`, with 0xFFFF for an empty buffer - and the bracket
  packed it into an `int`, which is 16 bits here, and tested `k >= 0`. Every
  scan code with bit 7 set is a NEGATIVE `int`: AH=0xA6 is -22528, so the
  `KSC_ALT_ENTER` arm four lines below the test could never be reached, and
  Alt+0/-/= (AH 0x81/0x82/0x83) were dropped with it. The shim was always
  right; the C's reading of it was not. `a2_fsx_key` is `unsigned` now and the
  test is `k != 0xFFFFu`. **No harness could see it, and the fix is what makes
  one able to**: a host `int` is 32 bits, so 0xA600 is positive there whatever
  the target does - `hosttest/a2uitest.c` queues 0xA600 and requires the
  bracket to exit on it, and the empty marker is 0xFFFF on both sides now, so
  a signed test passes on neither;
- **the MOUSE is legal and this bracket reads none.** A II+ has no mouse, the
  kernel's pointer is parked for the whole session (the gfx lock is held from
  before `fsx_run` to after it), and the only input the bracket owes is the
  machine's own keyboard and the two chords. The rule is written down so the
  next bracket does not reach for the event queue instead;
- **the speaker stays live**, which is the "no sound ports" rule read the right
  way round: the snd slots are legal throughout (SPEC.md 53.7) and
  `a2_spk_service` is one far call on a change. A machine that went silent the
  moment it went to colour would be a worse machine;
- **the flash phase is POLLED here** (`a2_flash_step`), because `W_ONTIMER` is
  an event and no event is dispatched inside a bracket. It is the same routine
  the wake polls on a kernel with no timer slot and it draws nothing.

**AND NOTHING ON THE GLASS NAMES THE WAY BACK, WHICH IS STATED HERE RATHER
THAN LEFT AS AN ACCIDENT.** Picking `Color NTSC` takes the menu bar, the
status row, the desktop and the pointer away, and the only exits are Ctrl+F
and Alt+Enter. In MII the row is one of five mutually-exclusive TICKED palette
rows, `mii->video.color_mode` starts at 0 so `Color NTSC` is the DEFAULT, and
picking it removes no pixel of UI - so the reference offers no precedent for
telling the user anything, because in MII there is nothing to tell. Here there
is, and **the port has two doors to a menu-less screen and neither one names
its chord**: section 10.1's arithmetic table shows `Color NTSC  Ctrl+F` FITS
at 20 of `MENU_MAXCH` 24 and records why it is declined anyway - Ctrl+F,
pressed in the state a menu caption is read in, is Toggle Fullscreen's and not
this row's, so the caption would be false in exactly the state SPEC.md
11.2.1's key-hint sentence is about. `Toggle Fullscreen`'s own caption does not
fit at all (25 of 24). **What would close the gap is a hint drawn INSIDE the
foreign frame** - the letterbox is 20 px each side and 4 px top and bottom, so
a legible one would have to go over the Apple's own raster, which means a
renderer for mode 13h and a fact for the shadow to carry - and that is a
feature, not a review fix. Until it exists the chords are in section 6.3, in
`a2_fsx_main`'s rule 7 and in the README, and **not on the glass**.

### 13.4 Measure first, then write - AND THE MEASUREMENT

`tests/a2band/a2bandbench.asm` gained the rows below and they are what decided
the section above. The recipe is section 7.9's, unchanged - `make a2bandbench`
then `make test TESTAPPS=build/a2band.img QEMU="qemu-system-i386 -icount
shift=3"`, where one PIT count is **0.359 ms of a real 4.77 MHz XT** - and the
`counts` column is the bench's own, divided by its iteration count.

**EVERY COMPOSE ROW IS MEASURED AT TWO WIDTHS**, because both routines take a
range and a single figure cannot be divided into a floor and a slope. The
`8 cells` rows start at **cell 8** rather than cell 0 on purpose: a hi-res
range that does not begin at the row's first cell has to seed its cross-cell
state from the SOURCE, and a bench that always started at 0 would never run
that code.

| row | counts/op | XT ms |
|---|---|---|
| `FSXROW13 text 280px` - one scan line of palette indices | 20.500 | **7.36** |
| `FSXROW13 lores 280px` | 7.000 | **2.51** |
| `FSXROW13 hires 280px` - MII's five branches over 280 pixels | 42.875 | **15.39** |
| `FSXROW13 text 8 cells` - one group, which is what a COUT moves | 4.500 | **1.62** |
| `FSXROW13 lores 8 cells` | 1.875 | **0.67** |
| `FSXROW13 hires 8 cells` | 9.375 | **3.37** |
| `FSXPUT 280 differing` - a 280-byte compare and two 280-byte copies | 6.000 | **2.15** |
| `FSXPUT 280 equal` - the line that did not move | 3.000 | **1.08** |
| `FSXPUT 70 differing` - a 1bpp foreign line | 2.000 | **0.72** |
| `FSXPUT 70 equal` | 1.000 | **0.36** |

So a compose is **0.180 + 0.180/cell** ms in text, **0.213 + 0.0575** in
lo-res and **0.359 + 0.376** in hi-res, and a span compare **0.239 +
0.006838/byte** when the range moved and **0.120 + 0.003419** when it did not.
`hosttest/a2uitest.c`'s cost table is those six pairs and no longer six
per-call constants, which is what let the first cut price a narrowed compose at
the forty-cell figure and report the narrowing as free.

...and the PATHS, each doing the same visible work:

| row | counts/op | XT ms |
|---|---|---|
| `FRAME windowed 2x` - 24 rows composed, doubled and blitted | 1762 | **632.6** |
| `FRAME FSXM_VGA13` | 9405 | **3,376.4** |
| `FRAME FSXM_CGA640/HERC` | 1937 | **695.4** |
| `ROW windowed 2x` - one character row, which is a keystroke | 73.250 | **26.3** |
| `ROW FSXM_VGA13` - all forty cells of it | 392.000 | **140.7** |
| `ROW FSXM_VGA13 one group` - **as the package draws it** | 87.000 | **31.2** |
| `ROW FSXM_CGA640/HERC` | 97.000 | **34.8** |
| `SCROLL FSXM_VGA13 (184 lines)` - **added by the wave-5 review** | 4867 | **1,747.2** |

**AND THE SCROLL ROW IS THE ONE THIS BENCH DID NOT HAVE, WHICH IS WHY THE
OMISSION IT MEASURES COULD BE WRITTEN WITHOUT A NUMBER ON IT.** The rows above
it time `a2_fsx_row` and `a2_fsx_put`; none of them times the loop that
decides which of the two to call, and a scroll is the change set where that
loop has no answer at all (section 13.2). It is TEXT and not hi-res on
purpose, because the windowed shift test is a text-mode test and so the two
sides of the comparison are the same event. Its parts: 184 x (20.500 + 6.125)
= 4,899 against 4,867 measured, and three runs read 4,863 / 4,868 / 4,867.

**AND ONE ROW OF THIS TABLE HAS TO BE READ WITH ITS FLAG.** `FRAME
FSXM_VGA13` printed **`w`** and **0 counts** on all three of the review's
runs. That is `benchlib.inc`'s lap detector firing falsely, not a measurement:
the test is `ticks >= N && ticks > the PIT total's high word`, and at
`bl_n = 1` - which every `FRAME` row uses, because a frame is 192 lines of
work - *any* row that happens to cross one tick boundary satisfies both
clauses, since the high word of anything under 65,536 counts is 0. The re-run
with method T then measures a 7.9 ms row in whole ticks and reads 0. The
value stands at **9405 / 3,376.4 ms** on the wave's own run, and it is
corroborated twice over on the review's: 192 x (42.875 + 6.125) = **9,408**
from this table's own primitives, and `ROW FSXM_VGA13` at 392.25 x 24 =
**9,414**. **Do not read a `w` row's number**; read its parts.

**AND THAT LAST PAIR IS THE COLUMN NARROWING, MEASURED.** `ROW FSXM_VGA13`
composes all forty cells of eight scan lines and compares all 280 bytes of
each, and an Applesoft `COUT` moves ONE cell: `a2_fsx_frame` hands the composer
`a2_flush`'s own group span and `a2_fsx_put` the matching byte range, which is
**31.2 ms against 140.7**, 4.5x, and within a fifth of the windowed path's own
26.3. The first cut of this wave read none of the state `a2_dirty_scan` had
just filled for it and paid the 140.7 on every keystroke of a colour session -
PERFORMANCE rule 1 in the mode whose per-line cost is the highest this port
has.

**THE ROWS CHECK AGAINST THEIR OWN PARTS AND AGAINST THE HARNESS.**
192 x (42.875 + 6.000) = 9,384 against the measured 9,405; 24 x (35.000 +
29.500) + 192 x 2.000 = 1,932 against 1,937; 24 x (35.000 + 29.500 + 8.875) =
1,761 against 1,762; and the narrowed row's 8 x (9.375 + 0.667 + 56 x 0.019) =
88.8 against 87.000. The counts are integers over eight iterations, so
+/-0.125 a row is the instrument and not the machine.

**AND THE HARNESS READS THE SAME FRAME.** `hosttest/a2uitest.c` counts calls
and prices them from the first table; its `colour: first frame + SPEC 53.6 step
4's repaint` row reads **3,970.3 ms**. That row now carries the kernel's own
exit repaint, because the stub models step 4 (13.3) - and 3,970.3 - 3,376.4 =
**593.9**, which is that repaint priced by the same table that prices the
`a full repaint` row at 496.8, plus the border fills and the status row. Two
independent readings of one frame.

**AND THE CELL RANGE IS CHECKED BEFORE IT IS TIMED.** `ab_spanck` composes the
whole row, keeps cells 8..15, scrubs the buffer, composes the same eight cells
as a RANGE and requires the 56 bytes to be identical - printed as a line above
the rows. Nothing else in the tree can see that: the host harness models
`a2_fsx_row` in C rather than running it, `tools/a2ref.py` compares the
WINDOWED composer, and artifact colour is not something anyone checks by
looking at a screendump. Get the seeding wrong and the picture is right at
forty cells and wrong at eight - right the first time the mode is entered and
wrong on every keystroke after it, and then recorded in the foreign shadow, so
it stays for the session. The hi-res fixture's bit 7 alternates cell by cell
for the same reason: the seed string is ASCII, so both of MII's colour sets are
in it now and the half-dot shift's arm is taken.

**TWO CHANGE SETS, BECAUSE ONE NUMBER CANNOT ANSWER IT.** A FRAME is entering
the mode, a `HOME`, a scroll in a graphics mode, a picture load - the worst
case, and the one the 1.75x argument was about. A ROW is a keystroke, which is
what the machine spends almost all of its time doing, and is where the two
paths are closest. **The 1bpp writers lose on both**, which is why the decision
needed no judgement.

**WHAT THE BENCH DOES NOT MODEL, and it cuts both ways:** `-icount` prices
INSTRUCTIONS and not bus contention, so a write into VRAM costs what a write
into RAM costs. That is true of every row in this document including the
`BLIT1` ones, which go to the real framebuffer through the kernel, so the
comparison is like for like and neither side is being flattered.

**AND THE BENCH'S FOUR COMPOSITE BODIES HUNG THE FIRST TIME THEY RAN.**
`a2band.inc` and `a2fsx.inc` are cdecl, and cdecl here preserves BP, DS, SS:SP
and DF **and nothing else** (SPEC.md 73.3): `_a2_band_text` loads BL with the
flash mask on its second instruction, so a loop counter in BX never came back.
What that looks like is not a wrong number - it is a bench that never finishes,
which is indistinguishable from one that is merely slow under `-icount`. The
counters are in memory now and the file says why.

---

## 14. Disk II - the follow-up PR

**Read-only Disk II is NOT in this PR.** It is a **follow-up pull request**
whose first job is a measured size line. Machine > Configure Slots... greys in
the meantime with section 10.3's fact.

**The wave keeps its number** in `docs/APPLE2-PORT-PLAN.md` - it is wave 6,
marked "follow-up PR" - because renumbering a wave that is written down is how
a plan and a branch stop agreeing.

**And the wave-5 ceiling can force this regardless** (section 15.3): wave 6
adds ~1,200 bytes resident, so a wave-5 line past 54,000 means it cannot ship
in this PR whatever anyone decided.

What it will be, pinned now so the follow-up is a transcription and not a
re-design:

- **The P5 boot ROM at `$C600`** out of the ROM part's offset `0x3800`, so
  `PR#6` and the Monitor's own boot path work. **AND ITS CONSEQUENCE:**
  `DISK2.rom` carries the Autostart boot signature (`$Cn01` = `$20`,
  `$Cn03` = `$00`, `$Cn05` = `$03`), so **from that wave a power cycle with NO
  disk boots the drive and spins forever, exactly as a real II+ does.** The
  status row says the drive is spinning and names Ctrl-Reset as the way to
  `]`, so authentic behaviour is not read as the port having frozen. *"Boots
  to the `]` prompt"* is true before that wave and, after it, only with a disk
  in drive 1 or via Ctrl-Reset.
- **The controller is one switch on `(addr & 0x0F)`**: `$C0E0-$C0E7` the four
  phase magnets (`phase = (addr>>1)&3`, step +1 if `(half_track+1)&3` is
  energised and -1 if `(half_track+3)&3` is, clamp 0..79,
  `track = half_track>>1`), `$C0E8`/`$C0E9` motor with its off timer,
  `$C0EA`/`$C0EB` drive select, `$C0EC` the latch, `$C0ED` write-protect,
  `$C0EE`/`$C0EF` mode.
- **THE SAMESLOT TRAP, which would otherwise cost days.** DOS 3.3 reads the
  latch twice in quick succession to test whether the drive is spinning, so a
  read **within 6 emulated cycles** of the previous one must answer
  `data >> 4` rather than the next nibble. Without it the boot sits in a wait
  loop **with no error and no symptom**.
- **The storage model, stated before a line is written:** a mounted image
  lives in a **143,360-byte heap claim per drive**, over 64KB and therefore
  reached by segment arithmetic in `a2mem.inc`; **drive 2 refuses with
  `os88_mem_largest_kb()` in the sentence** on a machine that cannot hold a
  second one. The alternative - re-reading the track off the floppy at each
  seek - is **~400 ms an `int 13h` with the emulated machine stopped**, which
  for a DOS 3.3 boot and a CATALOG is seconds each, and is refused with that
  number.
- **Per-track 6-and-2 nibblisation on demand** into a 6,656-byte heap claim,
  one call a track change, in `a2nib.inc`'s **hand-written RESIDENT
  assembly**: 48 self-sync `$FF`; `D5 AA 96` with 4-and-4 volume, track,
  sector and checksum; `DE AA EB`; 6 sync; `D5 AA AD`; `Code62`'s 343 bytes
  with its running-XOR checksum through `ms_DiskByte[0x40]` (the 256-to-342
  six-bit split, whose offset walks `0xAC` down by `0x56, 0x56, 0x53`);
  `DE AA EB`; 27 sync; and `SkewTrack`'s `(track*768) % nibbles` rotation.
  **THE OUTPUT IS 6,384 BYTES AND NOT 6,656**, and that framing is the
  arithmetic: `48 + 16 * (14 + 6 + 349 + 27)`
  (`AppleWin/source/DiskImageHelper.cpp:514-571`). 6,656 is the **claim**, and
  the claim being bigger than the track is fine; what is not fine is rotating
  or reading the 272 bytes past the end. AppleWin returns the ACTUAL length
  and passes that length into the skew (`:576-581`), so **`nibbles` in the
  rotation above is 6,384**, the head wraps at 6,384, and the trailing bytes
  of the claim are never read. A `% 6656` there is a drive that reads
  uninitialised memory once per revolution, with no error and a boot that
  sometimes works.
  **It is assembly and not C**: it is a per-nibble loop reached from inside an
  emulated slice through the controller's latch read, and an `ovl_` there
  would be a 400 ms floppy seek with the machine stopped behind it. It is
  **priced in ms per track change in the cost table**, not asserted as free.
- **The image formats:** exactly **143,360 bytes** = 35 x 16 x 256,
  headerless, **the extension alone picking the sector order**. `.DSK` and
  `.DO` take DOS 3.3's `{00,07,0E,06,0D,05,0C,04,0B,03,0A,02,09,01,08,0F}`;
  `.PO` takes ProDOS's `{00,08,01,09,02,0A,03,0B,04,0C,05,0D,06,0E,07,0F}`. A
  40-track image is 163,840 and is refused with the number.
- **The dialog is Machine > Configure Slots...** , which is where MII puts it,
  and **not four invented File rows**. Its strings are in the authority table.
- **No `.DSK` ships.** `tools/a2dsk.py` builds the gate's fixture, and the
  README and the Wire page say where a reader gets images.

---

## 15. The budget - PLANNED, and the first MEASURED line

SPEC.md 73's cap is **61,440** for resident image + bss, and SPEC.md 73.9's
split trigger is **55,000 resident**.

**Five figures are reported at every wave from wave 2 on**, separately:
resident image, bss, `APPLE2.OVL`, resident shims, largest C frame.
**There is deliberately NO wave-1 size line** - a core with only a few opcode
families assembled is not an honest measurement of a core, and quoting one
would set a budget against a number nobody can reproduce. What wave 1's build
prints (`image 19,300 + bss 10,312`, `APPLE2.OVL` 843) is recorded in
`docs/APPLE2-PORT-PLAN.md`'s wave-1 paragraph as evidence that the package
BUILDS and packs, and it is **not** a budget figure and not to be compared
with the headline below: there is no 6502 in it.

### 15.0 THE FIRST HONEST SIZE LINE - END OF WAVE 2, MEASURED

| | measured | against |
|---|---|---|
| resident image | **28,396** | 43,500 planned |
| bss | **12,240** | 13,000 planned |
| **resident total** | **40,636** of 61,440 | **20,804 spare** |
| `APPLE2.OVL` | **883** | 6,000 planned |
| resident shims | **8** | - |
| largest C frame | **46** bytes | the 96-byte cap |
| the FILE on disk | **43,520** (image + `APPLE2.ROM`'s 14,848 and the header) | `WIRE_FILEMAX` 64,512 |

**THE bss FIGURE CARRIES THE SOURCE SHADOW**, which is 1,920 of it: the k-row
scroll test compares a character row's forty SOURCE bytes against the forty
the glass was composed from (section 7.7 step 2), where it used to compare a
16-bit signature. That is what lets a verified shift CLEAR the shifted rows'
dirty bits, and a one-row scroll is **41.9 ms instead of 406.2** because of
it. The exactness is not a luxury: `xor al, b / rol ax, 1` is linear over
GF(2), so two cells sixteen apart changing by the same XOR delta cancel
exactly - a shape a text screen makes rather than a one-in-65,536 accident -
and a test that decides what is NOT drawn cannot be a hint.

**THE CEILING IS THE TOTAL, AND THE TWO-LINE SPLIT BELOW IS INDICATIVE.** The
gate is `image + bss <= 61,440` and the two triggers (55,000 and 54,000) are
on that same total; section 15.1's 43,500/13,000 split is the plan's estimate
of where the bytes would fall and is not a second pair of ceilings. Wave 2 is
what makes saying so necessary: bss moved 10,394 -> **12,240** against a
planned 13,000 - 760 left - and 1,920 of that is the source shadow this wave
added, while the image came in **15,104 UNDER** its own planned line. The
growth still to come lands on both halves (the lo-res and hi-res composers'
shared accumulator, the paste feeder, `a2fsx.inc`'s raster writers), so wave 3
is measured against the 61,440 total and the 55,000 trigger, **not** against a
13,000 bss line it will pass on its first buffer. Re-split honestly, the
measured 28,396/12,240 leaves the plan's own 56,500 total 15,864 of headroom
wherever it is spent.

**It is 14,364 UNDER the 55,000 split trigger and 13,364 under the
end-of-wave-5 ceiling of 54,000, with the whole 6502 in it** - the core, the
Apple II memory model, the soft switches in both directions, the II+ keyboard
map and the reset line. `a2cpu.inc` assembles to **6,510 bytes**, which is
within 8 bytes of the C64 core's measured 6,518 and is the plan's `-120` term
landing where it was estimated.

**What that settles.** The plan recorded a contested budget - this document
read ~43,500 image and the adversarial fit review read 60,500-63,500 - and
said the disagreement was not settled until this line. **It is settled in the
plan's favour, and by a wide margin**, so Disk II's deferral (Decision 13) is
no longer a budget decision at all: the follow-up PR's ~1,200 resident bytes
fit with room to spare and the wave was lifted out for the two reasons that
remain, the heap it wants for two drive images and the P5 ROM's authentic
no-disk hang.

**AND THE SPEED FIGURE ON THE STATUS ROW IS A MEASUREMENT AGAIN.** It shipped
in wave 2's first form saturating at **383 %** - `a2_cyc_add` stopped
accumulating at 60,000 64-cycle units and the one-second denominator is 157,
so 380 %, 400 %, 1,000 % and 3,000 % all printed the same number and the
`pct > 9999` clamp was unreachable. The unit now DOUBLES when the accumulator
would not hold the window (`64 << a2_csh`, capped at 4,096 cycles), the fold
undoes the shift on quotient and remainder both, and
`hosttest/a2uitest.c` drives seven machine speeds from 3 % to 6,000 % through
the arithmetic and checks each against what it should read, with a 12,000 %
case that must hit the clamp. Section 9 is the field's contract.

**What is still to come**, on this document's own per-file terms: the
clipboard pair, Load and Save Program, the speaker's estimator and
`a2fsx.inc`'s three raster writers (~+2,000). The planned total below still
stands as the number to watch; what wave 2 removes is the risk that the port
would hit the ceiling mid-feature, which is how CWORD lost time twice.

### 15.0.1 END OF WAVE 3, MEASURED

| | end of wave 2 | end of wave 3 | moved |
|---|---|---|---|
| resident image | 28,396 | **31,426** | **+3,030** |
| bss | 12,240 | **13,604** | **+1,364** |
| **resident total** | 40,636 | **45,030** of 61,440 | **+4,394**, 16,410 spare |
| `APPLE2.OVL` | 883 | **996** | +113 |
| resident shims | 8 | **8** | unchanged |
| largest C frame | 46 | **54** bytes | the 96-byte cap |
| the FILE on disk | 43,520 | **46,080** (image + `APPLE2.ROM`'s 14,848 and the header) | `WIRE_FILEMAX` 64,512 |

The figures are the REVIEW's, which moved the image +302 and the overlay +113
over the wave's own first cut (31,124 / 883): `a2_band_hires`'s scan-line
range, `a2_band_x2`'s byte count, `a2_pack`'s row range, `$C063`'s shift-key
mod, and the About panel's magnification arithmetic - which is overlay code
and is where the 113 went.

**9,970 UNDER THE 55,000 SPLIT TRIGGER and 8,970 under the end-of-wave-5
ceiling of 54,000**, so **lever 1 is NOT pulled this wave** (section 15.4):
`a2_x2b`, the doubled band, stays in bss where the flush cannot refuse it
rather than moving to a claim taken at the fullscreen latch.

**WHERE THE 3,964 WENT.** The image took the two composers and the shared
`a2_pack` they and the text one now call (the extraction gave a third of that
back), the mode dispatch and the row-base and row-mode functions, the
magnification arithmetic, the per-scan-line hi-res dirty scan and the game
buttons' poll. The bss took **1,280 of `a2_x2b`** - sixteen rows of 80, the
doubled band - plus the 48-byte hi-res row map, the 16-byte lo-res pattern
table and the mode key. The estimate this document carried for the composers
was **~+1,400 with the shared accumulator** against an actual ~+1,500 of
composer, which is the one number in section 15.2 that can be checked against
a measurement rather than against another estimate.

### 15.0.2 END OF WAVE 4, MEASURED

| | end of wave 3 | end of wave 4 | moved |
|---|---|---|---|
| resident image | 31,426 | **34,220** | **+2,794** |
| bss | 13,604 | **13,852** | **+248** |
| **resident total** | 45,030 | **48,072** of 61,440 | **+3,042**, 13,368 spare |
| `APPLE2.OVL` | 996 | **4,266** | +3,270 |
| resident shims | 8 | **37** | +29 |
| largest C frame | 54 | **54** bytes | the 96-byte cap |
| the FILE on disk | 46,080 | **49,664** (image + `APPLE2.ROM`'s 14,848 and the header) | `WIRE_FILEMAX` 64,512 |

The figures are the SECOND REVIEW's. Against the wave's own first cut
(31,426+13,844 image/bss and a 3,167-byte overlay) the resident line has moved
**-60 across the two review passes**, and **the overlay is where the wave
went**: 4,266 against 996.

The second pass moved the image **-142** and the overlay **+546**, and the
whole of that is one rule applied twice more. `a2_clip_service` and
`a2_copy_screen` were resident - about 170 x86 instructions of image for code
that runs once per menu pick - while `a2kbd.c`'s own header claimed they were
not; they are `ovl_a2_clip_service` and `ovl_a2_copy_screen` now. `ovl_a2_walk`
is new, and is the load's whole chain walk lifted out of `ovl_a2_load` so the
length-prefix hint can be second-guessed (section 12). The bss took **+20**:
the file command's latch - `a2_fname[13]`, `a2_fmode`, `a2_fsize`, `a2_fileq` -
which is what buys the desktop back from a floppy read under the gfx lock, and
`a2_paste_live`'s deletion gave a few bytes of image back with it.

**6,928 UNDER THE 55,000 SPLIT TRIGGER and 5,928 under the end-of-wave-5
ceiling of 54,000**, so **lever 1 is NOT pulled this wave** (section 15.4):
`a2_x2b`, the doubled band, stays in bss where the flush cannot refuse it.

**THE OVERLAY TOOK THE LARGER SHARE OF THE WAVE, WHICH IS THE DESIGN AND NOT
AN ACCIDENT.** `APPLE2.OVL` went from 996 bytes to 4,266 - the seven command
bodies, the linked-line walk, the two file bodies, the clipboard service and
the confirmation's drawing are all once-per-command by definition - while the
resident image took only what runs per BYTE or per CALLBACK: the paste
handshake, the two new movers, `a2_scan0`, `a2_copy_row`, the panel's hit test
and the launch document's wait. Every per-command body in this wave was written `ovl_` from
the start rather than moved there afterwards, which is LESSONS.md 5's own
instruction and is why the resident line moved 2,936 for a wave that added
seven commands, a clipboard pair, a loader, a saver and a modal dialog. **The
review found two that had not been**: `a2_wr16` and `a2_named` were resident
and reachable from `ovl_a2_load`/`ovl_a2_save` alone, ~120 bytes of the
resident line (their bodies, two shims and two vectors) plus six bridge
crossings a command that existed only because the rule had not been applied to
them, and **the second review found two more**: `a2_clip_service` and
`a2_copy_screen`, whose split `a2kbd.c`'s header asserted and the build did
not have. The per-byte and per-`$C000`-read halves - `a2_paste_peek`,
`_take`, `_stop` - stay resident and must, because they are on the emulator's
hottest path.

**bss moved 248 bytes, in five groups** - and the first version of this
paragraph said "all of it is Edit > Copy's two arrays", which accounts for 176
of the 244 and is the kind of sentence the next wave sizes a decision against:

| group | bytes |
|---|---|
| Edit > Copy's fold table (128 entries) and its forty-byte row scratch | 168 |
| the paste queue's four words - `a2_paste_seg`, `_n`, `_i`, `_cr` - and the review's fifth byte, `a2_paste_up` | ~10 |
| the launch document's name, place, flag and deadline (`a2_argname[13]`, `a2_argplace`, `a2_argp`, `a2_argdl`) | ~22 |
| the wave's own latches and rects: `a2_progmsg[28]`, `a2_copy_req`, `a2_paste_req`, `a2_pause`, `a2_warp`, `a2_pan_kind`, `a2_cfm_bx[2]`, `a2_cfm_by`, `a2_ovl_told` - less the 16 bytes the review took back off `a2_refuse_kb`'s line buffer when it was cut to `TOAST_MAX` | ~28 |
| the file command's LATCH, which is what takes the loader out from under the desktop's gfx lock (section 12): `a2_fname[13]`, `a2_fmode`, `a2_fsize`, `a2_fileq` | ~20 |

**The staging buffers are heap claims and not bss** - 1KB
for Copy and 2KB for Paste, taken when the command runs and freed the moment
it is spent - which is the C64's own 3,074-byte finding applied before the
bytes were spent rather than after.

**The `os88pkg` line, verbatim:**

```
os88pkg: 'APPLE2' entry=+0x0070 image=34362 bss=13832 icon=yes assoc=1
```

`assoc=1` is new and is section 12.1's own evidence: the association block is
in the header from this wave, so a `.BAS` opens on the first double-click of a
cold boot.

### 15.0.3 THE CGA FIX, MEASURED

The wave-4 defect (section 7.1's cursor anchor and section 6.6's tick window)
against the end-of-wave-4 line:

| | end of wave 4 | with the fix | moved |
|---|---|---|---|
| resident image | 34,220 | **34,494** | **+274** |
| bss | 13,852 | **13,856** | **+4** |
| **resident total** | 48,072 | **48,350** of 61,440 | **+278**, 13,090 spare |
| `APPLE2.OVL` | 4,266 | **4,266** | 0 |

**The `os88pkg` line, verbatim:**

```
os88pkg: 'APPLE2' entry=+0x0070 image=34494 bss=13856 icon=yes assoc=1
```

The bss is `a2_gl0_moved` and `a2_key_t0`; the image is `a2_geom`'s anchor
arithmetic, the flush's one test of the moved flag and the rule's tick compare.
**Nothing crossed a lever** (section 15.4): still 6,650 under the 55,000 split
trigger.

### 15.0.4 THE END OF WAVE 5, MEASURED - **AND IT IS THE GATE**

| | the CGA fix | end of wave 5 | moved |
|---|---|---|---|
| resident image | 34,494 | **38,944** | **+4,450** |
| bss | 13,856 | **14,340** | **+484** |
| **resident total** | 48,350 | **53,284** of 61,440 | **+4,934**, 8,156 spare |
| `APPLE2.OVL` | 4,266 | **4,349** | +83 |
| resident shims | 37 | **38** | +1 |
| largest C frame | 54 | **54** bytes | the 96-byte cap |
| the FILE on disk | 49,664 | **54,272** | `WIRE_FILEMAX` 64,512 |

**THE GATE IS 54,000 RESIDENT AND THE LINE IS 53,284: it PASSES with 716 to
spare, and NOT ONE LEVER WAS PULLED** (section 15.4). `a2_x2b`, the doubled
band, stays in bss where the flush cannot refuse it; the lo-res composer stays
its own routine; and lever 3 - the Hercules writer becoming the CGA640 writer
at a different stride - was taken at the DESIGN rather than as a rescue, which
is why section 13.1's cut gave nothing back. It is also **1,716 under the
55,000 split trigger**, so Disk II's ~1,200 resident bytes still fit in the
follow-up PR by the arithmetic Decision 13 asked for - **and with 516 bytes
over, which is the number that wave opens on rather than a comfortable one.**

**THE FIRST REVIEW ADDED 808 OF THAT**, and it bought two blockers, four
majors and one defect the review did not name and the glass did:
the fence that makes `a2_fsx_up` a flag rather than a sentence, the reset
latch spent inside the bracket, the shadow invalidated on the way IN rather
than on the way out (which REMOVES a whole 633 ms repaint), `a2_flrow`
maintained where the foreign frame composes, the column narrowing (section
13.4's 31.2 ms against 140.7), and the speaker's fact armed by the estimator's
failure as well as its success.

**AND THE SECOND ADDED 170**, for one blocker-class defect and two redraw
ones: the key sentinel made `unsigned` (section 13.3 - the enhanced
Alt+Enter was dead code and the harness could not have seen it), the frame
GATED on `a2_wrote()` and `a2_dirty_any` (section 13.2 - 17-29 ms a tick of
an idle colour session, eighteen times a second), and the row's early-out
HOISTED above the prologue the way `a2_flush` has always had it (~5 ms a
frame in text, ~10 in hi-res). **Two of the three REMOVE work from every
frame**, which is why 170 bytes is the whole price of them.

**The `os88pkg` line, verbatim:**

```
os88pkg: 'APPLE2' entry=+0x0070 image=38944 bss=14340 icon=yes assoc=1
```

Where the 4,764 went, and the interesting half is that the foreign video mode -
the wave's headline feature - is the cheaper of the two:

| group | image | bss |
|---|---|---|
| the four `OSAPI_FSX_*` thunks (section 17), which every C package pays | **+92** | 0 |
| the speaker: `a2_spk_toggle`, `a2_spk_hz`, `a2_div32`, `a2_sound_stop`, `a2_spk_service`, Machine > Mute and its menu state | ~+1,034 | +28 |
| `a2fsx.inc`: `a2_fsx_row`'s three phase Bs, `a2_fsx_put`, `a2_fsx_init`, `a2_fsx_dac`, `a2_fsx_key` and the three tables | +868 | +142 |
| the bracket in `a2scr.c`: `a2_fsx_avail`, `a2_fsx_frame`, `a2_fsx_main`, `a2_fsx_enter` and the 280-byte composed row | ~+1,478 | +310 |
| **the first review's six fixes plus `a2_fsx_zero`**: the `a2_fsx_up` fence, the reset latch, the shadow moved to the entry, `a2_flrow` maintained, the column span (`a2_span_of` + the hi-res pad + `a2_fsx_row`'s cell range), the speaker's failure-armed fact, and the shadow zeroed at entry | **+808** | +4 |
| **the second review's three**: the unsigned key sentinel, the frame gate, and the hoisted row early-out (which is a REORDERING plus one `ls0`/`ls1` pass) | **+170** | 0 |

**AND THE OVERLAY BARELY MOVED (+83), WHICH IS THIS WAVE'S SHAPE AND NOT AN
OVERSIGHT.** Wave 4's rule was that a per-COMMAND body goes out; wave 5's two
features are a per-`$C030`-READ estimator and a bracket whose entry proc
`tools/cc8086.py` refuses to let be an `ovl_` at all (section 13.3, rule 1).
Machine > Color NTSC is answered in the RESIDENT half beside File > Quit and
Toggle Fullscreen for a sharper version of their reason - the 53KB shadow claim
it takes first can COMPACT the arena, so taking it from inside `ovl_a2_cmd`
would be moving the module whose code is executing - and the side effect is
that colour works on a disk with no `APPLE2.OVL`. The only thing that went out
is Machine > Mute's shell.

**THE HEAP, WHICH IS NEITHER:** the foreign-frame shadow is **53KB claimed at
the fullscreen LATCH and freed when the bracket returns** (section 13.2). It is
the largest transient claim this package takes, it is why the harness's own
scratch segments had to grow from 48KB to 54, and a refusal there is legal and
greys with the fact where the flush's could never be.

### 15.0.5 THE END OF WAVE 7, MEASURED

**The polish wave spent 158 bytes and gave 60 back**, 58 of them to every C
package in the tree, which is the shape of a wave whose features are a
message, a listing, a set of documents and one redraw fix the review found.

| | end of wave 5 | end of wave 7 | moved |
|---|---|---|---|
| resident image | 38,944 | **39,042** | **+98** |
| bss | 14,340 | **14,344** | **+4** |
| **resident total** | 53,284 | **53,386** of 61,440 | **+102**, 8,054 spare |
| `APPLE2.OVL` | 4,349 | **4,349** | 0 |
| resident shims | 38 | **38** | 0 |
| largest C frame | 54 | **54** bytes | the 96-byte cap |
| the FILE on disk | 54,272 | **54,272** | the ROM part starts at a SECTOR |

**The `os88pkg` line, verbatim:**

```
os88pkg: 'APPLE2' entry=+0x0070 image=39042 bss=14344 icon=yes assoc=1
os88pkg:   part 0 ASSET   -    sector 77 len 14848  <- build/apple2-rom/APPLE2.ROM
```

Where the 102 went, and there are groups pulling both ways:

| group | image | bss |
|---|---|---|
| section 13.1's one-time price on the `CPU_8086` tier - the `a2_fsx_told` latch, the arm and the literal | **+78** | +2 |
| section 13.2's tier pacing for `a2_fsx_frame` - `a2_fsx_tick` and the predicate around the call in `a2_fsx_main` | **+80** | +2 |
| the SDK's three overlay refusals, shortened to fit `TOAST_MAX` (section 17) | **-58** | 0 |
| `Colour: 1.7 s per RETURN.` becoming `Colour: 1.7 s a scroll.` (section 13.1) | **-2** | 0 |
| **net** | **+98** | **+4** |

**The SDK half is measured and not derived**, and it is measurable here
because the strings are in this package's own image: the same tree with the
OLD literals restored builds `image=39022` against the 38,964 of the build
that carried only wave 7's first three groups, so the shortening is **-58**
for APPLE2 exactly as it is for every other C package with an overlay
(section 17.3).

**AND THE TWO GATES ARE RESTATED HERE, WHICH THE FIRST DRAFT OF THIS SECTION
DROPPED.** Section 15.0.4 gives both against wave 5's line and nothing
restated them against wave 7's, so a reader of 13.2 or of the Disk II
follow-up was quoting a number that had moved:

- **the 54,000 resident gate now has 514 bytes in it** (it had 716 at the end
  of wave 5, 694 at the end of wave 7's first draft, and 614 until section
  4.3.1's slice controller spent **98** of them - `image + bss` is
  39,140 + 14,346 = **53,486**, against 39,042 + 14,344 before it);
- the line is **1,514 under SPEC.md 73.9's 55,000 split trigger**, so Disk
  II's ~1,200 resident bytes leave **314** rather than wave 5's 516 - which
  is the number that wave opens on, and it is smaller than it was. Section
  13.2's no-fix argument spends from the same 514, so it cites this section
  and not 15.0.4;
- **`APPLE2.O88` on the disk did not move**, and the paragraph below is why:
  the ROM part starts at a sector boundary, so those 98 bytes came out of the
  slack under it and the file is **54,272** before and after.

**THE FILE ON DISK DID NOT MOVE AND THAT IS ARITHMETIC RATHER THAN LUCK.**
`os88pkg` starts a part at a 512-byte SECTOR boundary (SPEC.md 2.1.1), so the
image has 448 bytes of slack under sector 77 before the file grows at all: an
image change of -58 or +78 is absorbed whole. `WIRE_FILEMAX` is 64,512 and the
file is 54,272, so The Wire's per-file cap has 10,240 bytes of headroom
(section 18).

**AND THE FOLDER IS FIVE FILES NOW** (section 16.2): `WELCOME.BAS` is 884
bytes of tokenised Applesoft, so the whole payload is 54,272 + 4,349 + 884 +
9,500 + 21,533 = **90,538 bytes**, which `os88disk.py --verify` reports as
**93 of a 360KB disk's 354 clusters**, folder directory included.

### 15.1 The headline - PLANNED

| | PLANNED |
|---|---|
| resident image | **43,500** |
| bss | **13,000** |
| **resident total** | **56,500** of 61,440 - **4,940 spare, and 1,500 OVER SPEC.md 73.9's 55,000 split trigger** |
| `APPLE2.OVL` | **6,000** |
| the FILE on disk | image + `APPLE2.ROM`'s 14,848 = **~58,300**, under The Wire's `WIRE_FILEMAX` of 64,512 |

**`CC_HAS_OVL` is on from the first commit and `APPLE2.OVL` exists from wave
1.** The alternative is discovering at 55,000 that the code is not the kind
that can move.

### 15.2 The basis - TOP-DOWN from the C64's MEASURED line

**The method changed from the draft's, and the draft's was wrong twice with
the two errors partly cancelling**, which is why it looked plausible:

- the C64's resident C is **6,214 measured lines** (`c64.c` 1,711 +
  `c64io.c` 1,076 + `c64kbd.c` 1,154 + `c64scr.c` 1,813 + `c64menu.c` 460),
  not 3,050, so a 3,400-line estimate for a **superset** program was never
  credible;
- the ratio is **not 6.3 bytes per line**. `C64-SPEC §13.0.1`'s measured line
  is image **39,384** + bss **13,106**; subtract crt0 and thunks ~6,100,
  `c64cpu.inc` + `c64mem.inc`'s measured 6,518, `c64band.inc` ~2,000, `.data`
  ~1,000, strings ~2,000 and the icon 68, and the C accounts for ~21,700 over
  6,214 source lines = **~3.5 bytes per SOURCE line**, because this tree's
  `.c` files are heavily commented.

**So this budget is derived TOP-DOWN from 39,384, file by file against the
C64's measured files, and every term is signed.**

| term | bytes |
|---|---|
| `a2cpu.inc` + `a2mem.inc` | **-120** - the read fast path is two instructions and one compare shorter, no bank ladder, no `_bread` |
| `a2io.c` | **-1,500** - no VIC-II raster or sprites, no SID, no two CIAs with timers and TOD, no bank ladder; ADDING the soft switches, the speaker estimator and the paddles |
| `a2kbd.c` | **-800** - no 8x8 matrix, no two-way PETSCII chain; ADDING the paste feeder |
| `a2scr.c` | **+900** - the interleaved page-to-scan-line map, both screen-hole tests, MIXED, PAGE2, the flash phase and its force pass, the clamped-window cursor anchor, all against the C64's linear row arithmetic |
| `a2band.inc` | **+1,400** - THREE shift-accumulator composers against `c64band.inc`'s one text composer |
| `a2fsx.inc` | **+2,000** - new, and carrying a dirty-line-driven update rather than a whole-frame write |
| `a2nib.inc` | **+700** - the follow-up PR, assembly |
| the four FSX thunks | **+72** |
| strings | **-300** - four menus rather than five |
| | **sum: 39,384 + 2,352 = 41,736, carried at 43,500 for margin** |

**THE ROM IS 0 BYTES OF IMAGE** (section 1.5).

**bss, every buffer with its size:**

| | bytes |
|---|---|
| frame shadow, 40 x 192 | 7,680 |
| composed band, 8 x 40 | 320 |
| pixel-doubled band, 16 x 80 | 1,280 |
| the x2 doubling table | 512 |
| **the decoded CHARGEN, 64 glyphs x 8 rows** | 512 |
| **the 7-bit reverse table** - ONE table, ONE place, 128 entries of one byte | 128 |
| soft-switch and video state | ~64 |
| the flash phase and the per-line force bitmap | 24 + 24 |
| the speaker estimator | ~32 |
| paddle deadlines | ~24 |
| the key ring and the paste feeder | ~300 |
| the Disk II controller state (the follow-up) | ~64 |
| the status line | ~48 |
| the dirty-page take target + the window | 32 + 8 |
| SmallerC statics and out-parameters | ~1,600 |
| | **~12,650, carried at 13,000** |

**512 for the CHARGEN is reachable only WITH the per-cell XOR mask** (section
7.3); "512 and no mask" are mutually exclusive.

**HEAP - neither bss nor image, and it is accounted here because a draft that
does not account for it under-reads the machine:** the 64KB RAM claim; the
~8KB `APPLE2.OVL` claim; the transient clipboard claims; the 6,656-byte nibble
track; the foreign-frame shadow (13,440 for CGA640/HERC, **53,760** for
VGA13), taken at fullscreen-LATCH time where a refusal is legal; and
**143,360 bytes PER DRIVE** for a mounted disk image, addressed by segment
arithmetic, with drive 2 refusing on `os88_mem_largest_kb()`.

**THE OVERLAY at ~6,000:** `a2cmd.c`'s shells + `a2prog.c` + `a2disk.c` +
`a2about.c`, ~1,900 lines against the C64's measured 2,149 bytes for ~699
lines of `c64cmd`/`c64about`/`c64load` = 3.1 bytes per line, so ~5,900.
**Only CODE moves**; every string and static those files name stays resident
and is already counted above.

### 15.3 The gate, and the two ceilings

| ceiling | what it means |
|---|---|
| **55,000 resident** (SPEC.md 73.9) | the split trigger. `CC_HAS_OVL` is already on, so this is a reporting line rather than a decision |
| **54,000 resident at the END OF WAVE 5** | **a gate, not a hope.** The Disk II follow-up adds ~1,200 resident, so a wave-5 line past 54,000 means it cannot ship in this PR whatever anyone decided |
| **61,440** (SPEC.md 73) | the cap |

**The decision if wave 5 breaks 54,000, taken by the maintainer and not open:
Disk II gives way, and the foreign video mode ships whole** - unless wave 5's
own bench has already shown `FSXM_CGA640` and `FSXM_HERC` losing to the
windowed path, in which case those two writers are cut **on their own merits**
and about 1,200 bytes come back.

### 15.4 The four levers, in the order they get pulled

Pulled without stopping, each reported in the wave's measured paragraph.

| # | lever | bytes |
|---|---|---|
| 1 | the pixel-doubled band moves from bss to a heap claim taken at fullscreen-LATCH time | **-1,280** |
| 2 | the lo-res composer folds into the hi-res one behind a mode argument - cheaper than a first reading suggests, because they already share the accumulator | **~-500** |
| 3 | `a2fsx.inc`'s Hercules writer becomes the CGA640 writer at a different stride | **~-450** |
| 4 | the CGA640 and HERC writers are cut entirely if wave 5's bench says they do not beat the windowed path | **~-1,200** |

**NONE OF THE FOUR WAS PULLED AND THE LAST TWO ARE SPENT** (section 15.0.4).
Wave 5 came in at 53,114 against the 54,000 gate, so levers 1 and 2 are still
there for the Disk II follow-up. Lever 3 was taken **at the design**: there is
one `a2_fsx_put` and the three writers differ only in the caller's offset, so
there was never a second routine to fold. And lever 4's ~1,200 bytes were
booked against a shape with three routines in it - the bench did cut CGA640 and
HERC (section 13.1) and the saving is **zero**, because what was cut was a
C-side frame loop that had not been written. A lever that is already pulled is
not a lever, and this table says so rather than leaving 1,650 bytes of
imaginary headroom in the follow-up's arithmetic.

**There is no second overlay.** One `.OVL` per package, by construction.

### 15.5 The file split

| file | holds | resident |
|---|---|---|
| `apps/apple2/apple2.c` | the translation unit's root and the only file nasm ever sees a `.c` through: the GPL-2+ + four-attribution header, every prototype, the geometry constants, `os88_main` (the 64KB RAM claim, `os88_part_seg(0)`, **the CHARGEN decode and the 7-bit reverse table**, the fetch-bias underflow guard, `os88_snd_caps` once, the tier init, the RAM power-on pattern, `os88_key_down` armed here and nowhere else, the window sized against `os88_video().dock_top`), `os88_paint`, `os88_onkey`, `os88_onclick`, `os88_onfile` (**which LATCHES and nothing more** - section 12), `os88_onwake` (THE slice driver), the latches the wake spends before the slice, and the `#include`s in order | yes |
| `apps/apple2/a2io.c` | the `$C000-$C0FF` soft switches, both directions; the keyboard latch and strobe, **and the paste's own `a2_paste_seg` beside them, so the three hot arms test a word of DS rather than calling** (section 6.5); the video switches guarded by value; the speaker toggle and its interval estimator; the paddle one-shots and the two buttons; the slot-ROM ladder; and (the follow-up) the Disk II controller's sixteen `$C0Ex` cases with the 6-cycle re-read. **Every read here is side-effecting and the file says so at the top** | yes |
| `apps/apple2/a2kbd.c` | the scancode-to-Apple-byte map with its rejections and folds, the Ctrl folds routed on SCAN, the reset chords, Alt+Enter, and the paste feeder's per-byte handshake (`a2_paste_peek` / `_take` / `_stop`). **The per-byte and per-`$C000`-read halves are HERE and resident**; the once-per-pick half - `ovl_a2_clip_service` and `ovl_a2_copy_screen`, every claim, both clipboard calls and all six refusals - is `ovl_` and is in `APPLE2.OVL`. The first cut of this row claimed that split and the build did not have it | mixed |
| `apps/apple2/a2scr.c` | the damage model, the frame shadow, the flash phase and its force pass, the flush, the k-row scroll test, the mode dispatch, the tier table, the letterbox fills, **the clamped-window cursor anchor**, and the fullscreen geometry (`a2_scw`/`a2_sch`, decided in ONE place) | yes |
| `apps/apple2/a2menu.c` | the four menu tables with every string, mnemonic and caption, the `OS88_MENU_DIS` greying with the FACT in a comment beside each item, the menu-set struct, and the `os88_oncmd` dispatcher - two compares and then an `ovl_`, except File > Quit | yes |
| `apps/apple2/a2cmd.c` | `ovl_*`: the first-wake probe and every menu command SHELL. **It does NOT carry the CHARGEN decode or the reverse table.** No per-byte loop is written in this file | **no** |
| `apps/apple2/a2prog.c` | `ovl_*`: the Load and Save Program bodies, the two-pass chain walk (`ovl_a2_walk`) and the zero-page pointer write (section 12) | **no** |
| `apps/apple2/a2disk.c` | `ovl_*`: the Configure Slots dialog, the image open and validate, the per-drive claim and drive 2's refusal, the sector-order pick and the eject (the follow-up PR) | **no** |
| `apps/apple2/a2about.c` | `ovl_about_show` (section 11) | **no** |
| `apps/apple2/a2cpu.inc` | the 6502 core (section 4) | yes |
| `apps/apple2/a2mem.inc` | the claim accessors and the movers (section 3.4) | yes |
| `apps/apple2/a2band.inc` | the three composers and the row primitives (section 7.3) | yes |
| `apps/apple2/a2nib.inc` | the 6-and-2 encoder (the follow-up PR) - **assembly and resident** | yes |
| `apps/apple2/a2fsx.inc` | **WAVE 5 WROTE IT**: `a2_fsx_row`'s three phase Bs (a masked glyph row, MII's lo-res CLUT, MII's artifact rule flattened into a 128-entry table), `a2_fsx_put` - the span compare one geometry along, and ONE routine for all three writers - `a2_fsx_init`, `a2_fsx_dac` and `a2_fsx_key`, which is the bracket's whole input path and is assembly because there is no `int 16h` in the C SDK and a bracket may not use the event ladder | yes |
| `apps/apple2/apple2.asm` | the shim, and nothing else belongs in it: `CC_PKG_NAME 'APPLE2'`; `CC_HAS_ONKEY` / `ONCLICK` / `ABOUT` / `ONWAKE` / `MENUS` / `FDLG` / `OVL` / `PARTS` / `ICON` (**no `WORKER`**, and **`CC_ASSOC` only from the wave that makes Load Program work**, section 12); `%include cc/crt0.asm`, then `CC_PARTS_BEGIN 1` / `OS88_PART OP_ASSET` / `CC_PARTS_END`, then `apple2.gen.asm`, then `a2cpu.inc`, `a2mem.inc`, `a2band.inc`, `a2nib.inc` and `a2fsx.inc`, then `CC_IMAGE_END`. **`a2cpu.inc` comes first** because it declares the register file and the scratch layout the other two address through | yes |
| `apps/apple2/a2assoc.inc` | the build-time association block (section 12) | yes |
| `apps/apple2/icon.inc` | a 16x16 1-bit icon **drawn for this port** - not Apple's rainbow mark, which is trade dress | yes |
| `apps/apple2/COPYING`, `README.TXT` | section 1.3 - **from wave 1** | - |

**The overlay rules that bind here**, unchanged from `C64-SPEC §13.3`: split
by FREQUENCY and never by size; every callback is resident; **a locked
callback never crosses into `APPLE2.OVL` unless the module is already
resident**, because resolving it is a claim and a `FILE_READ` - a floppy seek,
~400 ms - inside whatever context asked for it; `a2_ovl_ready(win)` is that
fence, and the **WAKE**, which holds no lock, is what retries the load; and
**the `.OVL` cannot be loaded from `os88_main`** - there is no instance yet -
so the first `ovl_*` call is `ovl_a2_init()` from the **first wake**, printing
`Unable to load APPLE2.OVL.` on the status row and toasting when it refuses.

**ONE SENTENCE FOR ONE CONDITION, AND THE TOAST IS SAID ONCE.** `a2_ovl_ready`
had a second string of its own - `No APPLE2.OVL yet.` - that **no user could
ever read**: it clears `a2_ovl_asked`, so the very next wake re-runs the probe
and the probe overwrites the row before a tick has passed. The wave shipped a
message nothing could show, and its `yet` was a promise where SPEC.md 47 asks
for the fact; both routes say `Unable to load APPLE2.OVL.` now. The ROW is
said on every menu pick, because the row is the answer to what the user just
did; the TOAST is an announcement and is made once (`a2_ovl_told`), where it
used to repeat per pick.

**AND THE TOAST IS 21 CHARACTERS, BECAUSE `TOAST_MAX` IS 24**
(`kernel/toast.inc:85`) and a longer one is TRUNCATED rather than refused.
`APPLE2: no APPLE2.OVL beside the program - the menu commands will refuse`
reached the glass as **`APPLE2: no APPLE2.OVL be`** - 76 characters of which
the user could read 24, with the consequence in the half that was cut. It says
`APPLE2: no APPLE2.OVL` and the 26-cell status row beside it carries the rest.
Two more of this package's toasts were over the same cliff and were cut with
it: `Apple II+: 64KB wanted, 384KB free.` said `Apple II+: 64KB wanted, ` -
the NUMBER, which is the whole message, in the cut half - and
`Apple II+: the ROM claim is too low in memory` stopped at
`Apple II+: the ROM claim`.

**AND THE FIRST RECOMPOSITION CUT THE WRONG WORD.** `APPLE2 needs 64K, 384K`
fits and reads as **two requirements**: the word that made the second figure
mean anything was `free`. It says **`APPLE2: 64K, 384K free`** - 13 + a
three-digit figure + 6, so 22 of the 24 - with the label and both halves of
the arithmetic intact; the ROM row is `APPLE2: ROM too low`.

`apps/apple2/build.sh` fails the build on an `os88_toast` **literal** over
`TOAST_MAX`, the way it already did on an `a2_say` over the row - and on a
**COMPOSED** one that is not in its `TOAST_COMPOSED` table with a proven
bound (`line` 22, `a2_jamline` 18). The composed arm is the one that was
missing, and it is the one that mattered: `a2_refuse_kb`'s toast is composed,
so a literal-only walk never saw the very message this section is about.
Nothing was checking, which is why all three shipped.

**AND AN `ovl_*` ANSWERS A STATUS: 0 MEANS IT DID NOT HAPPEN** (SPEC.md 73.14,
the port skill's lesson 5). `ovl_about_geom` and `ovl_about_draw` returned
`void`, so `os88_paint` had no way to find out - and both are reachable at a
moment the module is not resident, from a callback that is under the desktop's
gfx lock and may not go to the floppy for it. With the module gone the card
cannot be redrawn, and the failure is **not a missing panel**: the hold range
stays set, the flush goes on skipping the rows the panel used to cover, and
under `WF_OWNBG` nothing else paints them either - a full-width strip of stale
pixels, for ever. So both answer `int`, `os88_paint` fences the pair with
`a2_ovl_ready(win)` like every other locked caller, and a 0 from either takes
**the card down**: the latch cleared, the hold range emptied, and the panel's
rect handed on as damage before the flush draws it.

**Every file above exists from wave 1, the stubs included**, and every one is
a **written prerequisite** in the Makefile (`APPLE2SRC`, `APPLE2INC`): make
cannot see through a `#include` or a `%include`, a file a later wave adds is a
build the tree does not know about, and a stale `APPLE2.O88` reads exactly
like a change that did nothing.

---

## 16. Names, disks, targets, machines and harnesses

### 16.1 Names

| | |
|---|---|
| package name | `APPLE2` |
| source | `apps/apple2/` |
| shipped files | `APPLE2.O88` (with the ROM as part 0), `APPLE2.OVL` |
| window title | `Apple II Plus Emulator` |
| About panel, first row | `Apple II Plus Emulator` (AppleWin `source/Common.h:50`, `TITLE_APPLE_2_PLUS` - the same string as the window title; the leading `The` and the "MII's model row" citation were wave 3's and are corrected in wave 4) |
| menu-set `AM_NAME` | `Apple II+` |
| The Wire record title | `Apple II+` |
| images | `build/apple2.img` (1.44MB), `build/apple2720.img` (720KB), `build/apple2120.img` (1.2MB), `build/apple2360.img` (360KB) |
| tools | `tools/getapple2rom.py` (fetches and assembles the ROM part), `tools/a2ref.py` (the reference compositor), `tools/a2dsk.py` (the `.DSK` fixture writer, the follow-up PR) |

**Two strings with a stated rule, not three that drifted.** The long form is
the window title and the About panel's first row; the short form is `AM_NAME`,
the Wire title and the package stem. **The short form is a departure from the
cited definer** and its reason is the menu-bar cell. The name is checked
across all four surfaces in wave 7.

### 16.2 Disks

**Four geometries**, each `os88disk.py --verify`'d in the recipe, each
carrying **`APPLE2.O88` + `APPLE2.OVL` + `WELCOME.BAS` + `README.TXT` +
`COPYING` in ONE folder `APPLE2/`** - five files, since wave 7 added the
listing. The whole folder is **90,538 bytes**, which `--verify` reports as
**93 of a 360KB disk's 354 clusters**, the folder's own directory cluster
included.

**One folder, and it is a correctness requirement rather than a layout
choice**: SPEC.md 73.14 resolves an overlay in the **launched-from**
directory, and a document double-click leaves that directory on the
document's - so all of them are one folder or every menu command refuses
politely.

**`COPYING` travels with the binary.** The floppy is the distributed form of a
GPL-2-or-later program. **If space ever runs out, the licence stays and the
other thing goes.**

On `make allapps` and on `make live` the package gets **a folder of its own,
`APPLE2\`, never a place in `APPS/`** (SPEC.md 19.9, SPEC.md 19.10).

**`WELCOME.BAS` - SHIPPED, wave 7.** An Applesoft listing written for this
port that demonstrates what this build actually renders - text, INVERSE, GR
with MIXED's four text rows, HGR, and the speaker - **and nothing it does
not**, on CWORD's `WELCOME.RTF` rule. It could not ship before the wave that
can open it (Load Program is wave 4) and before the modes it demonstrates
exist (wave 3): a listing on the disk that names GR and HGR on a build with
neither would be exactly the claim that rule forbids. It does not use `FLASH`,
which the `CPU_8086` tier refuses and greys with its cost (section 7.8) - a
listing that said *"this flashes"* would be false on the machine this port is
aimed at - and where it names colour it names **Machine > Color NTSC and a
VGA** (section 13.1) rather than implying the window has any.

**AND THE RULE BITES ON A GEOMETRY, WHICH IS HOW IT WAS CAUGHT.** The first
draft printed `GR: 40 X 48 BLOCKS` and `HGR: 280 X 192` - the FULL-SCREEN
fields of both graphics modes - while the screen under the sentence was
**MIXED**, which is the top 160 scan lines in the graphics mode and the bottom
32 in text (section 7.4). `GR` and `HGR` are the Applesoft verbs that select
mixed, so the lo-res field is **40 x 40** and the hi-res one **280 x 160**,
and the program already knew: line 290's `HPLOT X,0 TO 279 - X,159` stops at
159 because 160 is the field. It now prints `GR: 40 X 40 BLOCKS` and
`HGR: 280 X 160`. **A demo is a claim about the machine and is held to the
same standard as this document** - and the failing pair was on the glass in
two published screendumps and in the website's own hero image before anybody
read the numbers rather than the picture.

**IT IS TOKENISED, AND FROM THE PINNED ROM'S OWN TABLE.** Load Program accepts
the program's STRUCTURE - the line chain of section 12 - so a plain ASCII
listing on the disk is refused by name, correctly. `apps/apple2/welcome.a2b`
is the SOURCE and `build/WELCOME.BAS` the artefact, on `tools/os88rtf.py`'s
shape one package along: **`tools/a2bas.py`** reads Applesoft's own
TOKEN.NAME.TABLE at **`$D0D0`** (`AppleWin/bin/A2_BASIC.SYM:805`, which is
offset `0xD0` of `APPLE2.ROM` by section 1.4's layout), asserts its measured
shape - 107 names from `END` to `MID$` - and implements the ROM's PARSE
(`$D559`): upper case, a quoted string verbatim, the rest of a `REM` or `DATA`
statement verbatim, `?` for `PRINT`, and otherwise the token names tried in
TABLE ORDER with spaces skipped in the input, first match winning. **A
hand-typed token table would be a second transcription of something the build
already has in front of it**, and the ROM is pinned by SHA-256, so the shipped
bytes rebuild byte for byte.

**`--selfcheck` IS THE GATE AND IT IS IN THE RECIPE**, not in a target of its
own: it tokenises, LISTs the result back through the same table and requires
the two to agree once spaces outside strings are normalised away. A tokeniser
is exactly the kind of tool whose output looks fine and runs wrong - one byte
out is a `]` prompt whose `LIST` is empty or full of `SYNTAX ERROR`, which no
screendump of a booted machine shows. It is 42 lines and **884 bytes**, and
the machine's own `LIST` printing it back is the proof that outranks the
gate (`build/port-shots/wave7-05-welcome-list.png`).

#### 16.2.1 WHAT THE LISTING COSTS ON THE MACHINE IT IS SIZED FOR

The listing's header said it *"IS SIZED FOR A 4.77 MHz XT"* and then did the
arithmetic **for the loops that were rejected** - a 1,600-`PLOT` fill - and
none at all for the ones that shipped. So the one file a user double-clicks
first named the target machine and put no number on it, which is the failure
the rest of this document exists to prevent. **The number is measured now**,
on the same MartyPC 5150 section 16.4.1's speed figure came off, by
`build/a2welcome.py`: the package is launched by **double-click on
`WELCOME.BAS`** (the `CC_ASSOC` route of wave 4), `RUN` is typed a character
at a time and read back off the Apple's own text page, and each screen is
timed off the BIOS tick counter at `0040:006C` from the keypress that starts
it to the caption the program prints when the loop is done.

| screen | what it draws | measured | before section 4.3.1 |
|---|---|---|---|
| text, from `RUN` | 8 `PRINT`s and `INVERSE` | **2-3 s** | 6.0 s (110 ticks) |
| lo-res | `GR` + 32 `HLIN 0,39` | **76.2 s** (1,386 ticks) | 91.2 s (1,659 ticks) |
| hi-res | `HGR` + 7 `HPLOT X,0 TO 279-X,159` | **78.2 s** (1,423 ticks) | 106.2 s (1,932 ticks) |

**Two runs agreed to a TENTH OF A SECOND on both graphics screens** - 76.2 /
78.2 and 76.2 / 78.1 - which is a tighter pair than the second and a half the
two pre-controller runs agreed to (6.0 / 92.2 / 104.2 and 6.0 / 91.2 / 106.2),
and is the sample the slower budget could not take. **The text figure is the
noisy one and is quoted as a range**: 2.0 s and 3.0 s over the same two runs,
being short enough that the package launch is most of it.

**That is a minute and a quarter a picture, and it is said out loud rather than
discovered.** It is what 0.64% of a 1.02 MHz Apple *is*, so it is not a defect
in the port and there is nothing to fix in the drawing: the windowed flush
narrows to the touched scan lines and costs ~16 ms a pass, which is why both
screens draw **progressively**, a band or a line at a time, and the machine
looks busy rather than hung. **The listing keeps its loops** - halving them
halves a figure that is long either way, and the pictures are the
demonstration - and the header and `README.TXT` carry the measurement instead,
which is this port's posture everywhere else: the status row prints the speed
rather than the port pretending.

**Manual evidence** (section 16.5): `build/port-shots/
wave7-xt-welcome-text.png`, `wave7-xt-welcome-gr.png` and
`wave7-xt-welcome-hgr.png` are the three screens on the XT, in that order,
each taken at the instant its caption appeared. They are also the **only**
published screendumps of the double-click route on the target machine.

### 16.3 The Makefile

`$(eval $(call CC_PACKAGE,apple2,apple2,APPLE2.OVL))` with the ROM part;
`APPLE2SRC` and `APPLE2INC` as **written** prerequisites naming every included
file; `build/apple2-rom/APPLE2.ROM` with **exactly ONE owner - the Makefile
rule** - because `build.sh` reads it and writing it from the stamp recipe
would re-make a downstream prerequisite behind make's back; the four disk
geometries each `--verify`'d; and the phony targets.

`build/WELCOME.BAS` is the fifth file on every geometry and has a rule of its
own (`tools/a2bas.py apps/apple2/welcome.a2b --selfcheck -o $@`), with the
pinned ROM as a **written prerequisite** because the token table is read out
of it (section 16.2). `make allapps` and therefore `make live` carry the whole
folder as `APPLE2\`, never a place in `APPS/` (SPEC.md 19.9, 19.10).

Nothing in `all` reaches the ROM fetch, so this package is **on demand** like
`cworddisk` and `runcpm`.

### 16.4 The 86Box machines

Each is a **copy of a machine that has booted**, with `fdd_02_fn` and the uuid
changed and **nothing else**. 86Box substitutes a default for an unrecognised
key and rewrites the config on exit, so `git checkout` the cfg before
committing and never commit `nvr/`.

| machine | copied from | B: | lands in |
|---|---|---|---|
| `vm/386-apple2` | `vm/386-c64` | `build/apple2.img` | **wave 1** - the machine the port is looked at on |
| `vm/xt-apple2` | `vm/xt-c64` | `build/apple2360.img` | **wave 7**, with the measurement that justifies it |
| `vm/286-apple2` | `vm/286-c64` | `build/apple2720.img` | **wave 7** |

**The XT is where the measured percentage of a 1.02 MHz Apple II is taken**
and written into the status row's units, and where Ctrl+F2, Ctrl+F3 and
Alt+Enter are confirmed on a real BIOS.

**An XT target before anyone has measured the port there is a claim, not a
machine**, which is why two of the three land in wave 7.

`RESET=1|cmos|flash|both` clears a stale CMOS on the way in.

#### 16.4.1 THE MEASURED XT SPEED - 9 September 2026

**0.5% of a 1.02 MHz Apple II idle and 0.6% under BASIC, and the status row
still reads `0%`.** It was 0.41% and 0.51% when this section was first written;
section 4.3.1 is the change and the before rows are kept below.

Taken on **MartyPC**, not on 86Box, and that is the whole reason the figure
exists: 86Box launches a window a person can look at and cannot assert
anything (section 16.5), where MartyPC's debug server can read the package's
own counters out of the guest - and the one read here is **`a2_c64u`**, the
raw one-second cycle fold of section 9, because `a2_pct` is whole per cent
and reads **0** on this machine (below). The machine is `os8088_5150_cga` -
docs/FIELD-MACHINES.md's calibration machine, an **8088 at 4.77 MHz** with
640K, a CGA and two 360K drives - resolved to its GLaBIOS twin
(`os8088_5150_cga_gla`) because this checkout has no IBM ROM; the CPU, the
clock and the card are the same, and a BIOS does not run the 6502. It booted
`build/os8088-360.img` with **`build/apple2360.img`** in B:, exactly what
`make xt-apple2` puts in that drive, and APPLE2 was launched from the Disk
window.

**THREE RUNS OF EACH, BEFORE AND AFTER.** Each row is that run's one-second
windows, read by sampling the guest every 0.2 s and taking `a2_c64u` at each
fold. The **before** rows are the wave-7 slice rule (`a2_budget` pinned at
`A2_SLICE_MIN` = 256); the **after** rows are section 4.3.1's duty-cycle
controller, and nothing else about the machine, the disk or the instrument
moved between them:

| what the machine was doing | | run | windows | measured |
|---|---|---|---|---|
| the `]` prompt, idle - the Monitor's keyboard poll | before | A | 11 | 0.38% - 0.46%, mean 0.41% |
| " | " | B | 11 | 0.34% - 0.46%, mean 0.41% |
| " | " | C | 12 | 0.36% - 0.46%, mean 0.41% |
| " | **after** | A | 12 | 0.43% - 0.61%, **mean 0.51%** |
| " | " | B | 11 | 0.52% - 0.62%, **mean 0.56%** |
| " | " | C | 12 | 0.47% - 0.56%, **mean 0.54%** |
| `FOR I = 1 TO 1000 : NEXT`, **running** | before | A | 13 | 0.51% - 0.58%, mean 0.52% |
| " | " | B | 12 | 0.43% - 0.61%, mean 0.51% |
| " | " | C | 12 | 0.48% - 0.53%, mean 0.51% |
| " | **after** | A | 11 | 0.56% - 0.78%, **mean 0.65%** |
| " | " | B | 12 | 0.54% - 0.79%, **mean 0.64%** |
| " | " | C | 11 | 0.50% - 0.75%, **mean 0.62%** |

**So the number is 0.54% idle and 0.64% under BASIC**, against 0.41% and
0.51% before - **a third faster at the prompt and a quarter faster under a
listing**. The loop figure is the higher of the pair for the reason a reader
would expect: Applesoft's `FOR/NEXT` spends fewer 8088 clocks per emulated
6502 cycle than the Monitor's keyboard-poll loop, so the same 40 ms of wall
clock covers more of a real Apple's second - and section 4.3.1's controller
is what turns that into cycles rather than into idle time, settling at a
budget of 648 in the loop against 512 at the prompt with the SLICE the same
length in both.

**AND THE LOOP ROWS ARE A REVIEW CORRECTION, WHICH IS RECORDED RATHER THAN
QUIETLY REPLACED.** The first take (`build/a2speed4.py`) typed
`FOR I = 1 TO 1000 : NEXT` into the guest in one `m.type_text()` burst. The
BIOS keyboard buffer holds **15 keys**, so everything after `FOR I = 1 TO 10`
was dropped - RETURN included - and its "loop" rows and its
`wave7-xt-loop.png` were both **the machine at the `]` prompt with a
half-typed command on the input line**. That is exactly why they read the same
as idle (0.41-0.50% against 0.41%), and the agreement was read as a null
result rather than as the instrument reporting the wrong state.
`build/a2speed5.py` is the re-take, and it proves each of the three things the
first one assumed: every character is read back off the **Apple's own text
page** before the next is sent (a shifted letter's shift-up and the next key's
shift-down also collapse at 4.77 MHz, so `1 TO 1000` first came back as
`1 T !)))`), RETURN is proved taken because the trailing `]` is **gone**, and
the loop is proved **still running** after the last window and again at the
screendump.

**AND `a2_pct` READS 0 IN BOTH, WHICH IS THE FIELD DOING WHAT IT SAYS.** The
speed figure is a whole per cent (section 9) and 0.4 truncates to 0; the row
is honest and simply has no resolution left at this speed. It is NOT the
`el > 36` arm discarding the window - that was the first reading's own
artefact, from sampling the guest every two seconds and seeing two folds as
one: sampled every 0.2 s the windows are **18-20 ticks**, which is the
one-second fold working exactly as designed.

**WHERE THE TICK ACTUALLY GOES, MEASURED ON THE WALL AND NOT MODELLED.** The
package was built once with four counters in it - host ticks summed across
the slice, across the flush and across the whole wake, over a thousand wakes
each, which is how a 55 ms clock measures a 7 ms event - and read out of the
guest by MartyPC. They are an instrument and are not in the shipping source;
`build/a2slice.py` is the driver:

| | before (`a2_budget` = 256) | after (512 idle) |
|---|---|---|
| the slice | **22.5 ms** - 0.41 of a host tick | **40.5 ms** - 0.74 of one |
| one flush | 110.9 ms | 125.0 ms |
| the whole wake | 54.4 ms | 96.3 ms |
| wakes a second | 17.7 | 10.2 |
| emulated cycles a second | 4,531 | 5,223 |

**AND THE FIRST READING CORRECTS THIS SECTION'S OWN ARITHMETIC.** It said the
budget was pinned because a 256-cycle slice is "~21 ms of a 55 ms tick, so the
tick boundary is crossed often enough to halve it back" - the 21 ms is right
(22.5 measured) and the conclusion is not. A 22.5 ms slice crosses a boundary
on 0.41 of its wakes, and the rule needed FOUR clean slices in a row to double
against ONE crossing to halve, so the walk's fixed point was **14 %** of a
tick whatever the machine: it was not near the tick, it was pinned a long way
below it. Section 4.3.1 has the solved equilibrium.

**AND THE SLICE IS NOT WHERE MOST OF THE XT'S TIME GOES - THE FLUSH IS.** At
the idle prompt the machine spent **39 %** of every second in the 6502 and
**50 %** inside `a2_flush`, at 110.9 ms a flush and 4.6 flushes a second. That
is the ceiling the section 4.3 lever cannot reach past, and it is why the
gain here is a third rather than the several-fold a reader might expect from
a slice that doubled: the raw core throughput on this machine is **~11,500
emulated cycles a second** (1.13 % of an Apple), and everything between 0.54 %
and that is redraw. The flush's own pacing is section 7.8's tier rule and was
deliberately not touched by this change.

**THE WHOLE BUDGET CURVE, IN ONE BOOT** (`build/a2sweep.py` - the instrumented
build takes a forced budget poked in from the host, so every row is the same
machine at the same prompt):

| forced budget | slice | whole wake | wakes/s | cycles/s | % of an Apple |
|---|---|---|---|---|---|
| 256 | 22.5 ms | 54.4 ms | 17.7 | 4,531 | 0.44% |
| 384 | 32.8 ms | 75.4 ms | 12.8 | 4,915 | 0.48% |
| **512** | **36.8 ms** | **88.3 ms** | **11.1** | **5,683** | **0.56%** |
| 768 | 49.4 ms | 119.8 ms | 8.2 | 6,298 | 0.62% |
| 1024 | 75.3 ms | 138.1 ms | 7.0 | 7,168 | 0.70% |
| 2048 | 132.7 ms | 267.8 ms | 3.7 | 7,578 | 0.74% |
| 4096 | 260.0 ms | 398.2 ms | 2.5 | 10,240 | 1.00% |

The curve is real and it keeps going, and **the price is on the same row**:
1.00 % costs a wake of 398 ms, which is the desktop's menu bar not answering
for seven ticks. 512 is where the controller lands on its own and it is the
last row whose whole wake is inside two ticks.

**AND THE MENU STILL COMES DOWN**, which is the half a speed figure cannot
say. `build/a2uilat.py` presses the bar's **Machine** cell while
`FOR I = 1 TO 9999 : NEXT` is running and polls the kernel's own `menu_ent`
and then the pull-down's PIXELS, twelve presses an arm, with the loop proved
still running at the end:

| | before | after |
|---|---|---|
| the click reaches `menu_track` | median 1 tick, worst 1 | median 1 tick, worst 3 |
| the pull-down is on the glass | median 2 ticks (110 ms), worst 3 (165 ms) | median **2 ticks** (110 ms), worst 4 (220 ms) |

**The median is unchanged and the worst case costs one more tick.** Both arms
already exceed two ticks in the worst case and did before this change, for the
flush's reason above and not the slice's.

**THE PLAN PREDICTED 3-4% AND IT WAS OUT BY SIX**, and the reason is not the
one this section first gave. docs/APPLE2-PORT-PLAN.md's risk section arrived
at "~400 8088 clocks per emulated 6502 instruction" and divided by nothing
else. The measurement says **~415 clocks per emulated CYCLE** - about 1,250 a
6502 instruction, three times the plan's figure - on a cycle-accurate 4.77 MHz
8088 with its prefetch queue and DRAM refresh in the price. That alone caps
the port at 1.13 %; the redraw takes it from there to 0.54 %. The slice is a
third of the answer and was worth taking, and it was never the whole eight.

**WHAT THAT MEANS FOR A READER**, and it is what `README.TXT`, the Wire page
and the tier all say: an XT reaches the `]` prompt, answers a keystroke and
draws - it is a machine to look at. A listing runs at about a **150th** of a
real Apple's speed - the measured 0.64% is 1/156, and the idle 0.54% is 1/185
- so what a real Apple does in a second takes minutes here, which is
`README.TXT`'s own wording. It was a 200th before section 4.3.1. The 386 is where this port is usable and a 486 is where
it is comfortable, which is why the Wire record is **tier 3** (section 18.1),
the same tier the C64 takes for the same honest reason.

**Manual evidence, recorded** (section 16.5): `build/port-shots/
wave7b-xt-idle.png` and `wave7b-xt-loop.png` are the machine at the prompt and
inside the loop **over section 4.3.1's controller**, with `0%` on the row -
and the second one **shows** the loop: `]FOR I = 1 TO 1000 : NEXT` with no `]`
under it, which is what a running program looks like on an Apple. The wave-7
pair (`wave7-xt-idle.png` / `wave7-xt-loop.png`) is kept beside them as the
before picture; the pair before THAT was indistinguishable, which is the
defect above. `wave7b-xt-welcome-text/gr/hgr.png` are the same re-take of
section 16.2.1's three. No gate rests on any of them.

**AND THE ONE-TIME COLOUR FACT CANNOT BE EXERCISED ON ANY EMULATOR IN THIS
TREE**, which is said here rather than left as an untested claim (section
13.1). It needs a `CPU_8086` machine WITH a foreign-mode-capable VGA, and
neither emulator has one: QEMU's CPU answers a fast tier, and MartyPC's
`os8088_xt_vga` - an 8088 with a VGA, which is exactly the shape - **greys
Machine > Color NTSC**, because `os88_fsx_caps` reports no `FSXM_VGA13` on
that card. Read off the live menu rather than guessed: `('  Color NTSC',
False)` in `menu_bar[]`, beside `('  Toggle Fullscreen', True)`. So the
greying is SPEC.md 47 doing its job on that machine, the sentence is checked
for length by `build.sh`'s corpus and by `a2uitest`'s `msgs[]`, and the arm
that raises it is four lines under one flag. `build/port-shots/
wave7-xtvga-01.png` is that machine with the Machine menu **held open** under
the screendump - `Color NTSC` greyed, `Toggle Fullscreen` beside it live, the
`]` prompt behind and `0%` on the row - because "colour greyed" is what the
file is cited for and a decoded `menu_bar[]` is not a photograph. The first
take of it was neither: it was shot sixteen emulated seconds in, before the
Autostart Monitor had cleared the screen, so it photographed a power-on RAM
pattern of `?` with the menu shut.

### 16.5 Automated evidence, and manual evidence

**Automated evidence** is section 16.6's harnesses and the QMP screendumps.
**Every `done_when` in `docs/APPLE2-PORT-PLAN.md` rests only on it.**

**Manual evidence** is the three 86Box machines and anything read off them by
a person: the measured XT speed, the chord table, the look of a scroll, the
feel of keystroke latency. It is **recorded here as a reading with its date
and machine**, and it is **never a gate** - a `make` target that launches a
GUI emulator cannot assert a boot.

### 16.6 The harnesses

| | what it does |
|---|---|
| `apps/apple2/build.sh` | the host checks, each of which **stops the build**, run through a stamp file that is a prerequisite of `apple2.raw.asm`: `getapple2rom.py --check`; `a2ref.py --romshape`; `a2uitest`; `a2ref.py --check <explicit mode list>` - **FIVE frames from wave 3**: text on each FLASH PHASE, then lo-res, hi-res and a MIXED screen, which is a graphics composer and the text one in one frame - plus `--selftest` on text and on hi-res, `--lumcheck`, `a2memtest.sh`, and the `a2_say()` literal walk **with an explicit expected MINIMUM**. **Every step FAILS rather than passing when its subject is absent** - `--check` takes an explicit mode list, so a mode whose composer does not exist yet fails instead of printing green, and `--lumcheck` was NOT called until the wave that wrote the ladder it reads |
| `apps/apple2/hosttest/os88.h` | the stub SDK - the same structs and constants as `apps/cc/os88.h`, only the prototypes the program calls, no `long`/`float` poison (the host needs `printf`), **plus every new thunk's prototype in the SAME edit that adds the thunk**. It is a second copy of an interface and it will drift; when it does the harness fails to COMPILE, which is the failure you want |
| `apps/apple2/hosttest/a2uitest.c` | the whole program against that stub, with a **PIXEL model of the glass**: `gfx_blit1` writes real pixels, `gfx_scroll` fills the vacated rows with **GARBAGE**, and after every driven step it asserts pixel for pixel over **the visible band** - `a2_gnl` lines from `a2_gl0`, which stopped being "everything below `a2_gl0`" the moment the anchor started following the cursor - that **the glass shows what the shadow says it shows**. **Every drawing primitive asserts BOTH of its preconditions**: the gfx lock, whose absence hangs the machine dead, and **an armed CLIP REGION**, whose absence draws over somebody else's window and shows up in no screendump taken afterwards. The region is modelled as the kernel scopes it - armed by `clip_set`, dead at the next `gfx_unlock` - so one lock hold's clip cannot vouch for the next one's drawing. **It drives the flash phase across a flip** and asserts that exactly the lines whose text rows hold `$40-$7F` bytes were forced, and that a flip landing in the same flush as a narrow write still composes the flashing rows whole. It drives **a partial expose with the About panel up**, **a content box that changes size with it up**, **`Toggle Fullscreen` with it up**, **the fullscreen chords in both directions**, **the same status message twice**, **a `clip_set` REFUSAL**, **a one-row scroll on the 111-line band a 640x200 desktop gives**, **a COLD START on that band** - the banner on row 0, the prompt on row 2, `CV` at 2, starting from the bottom anchor - which asserts that the anchor moved to line 0 and that the banner's and the prompt's pixels are LIT on the glass, and **twenty polls inside ONE tick with Space held**, which is section 6.6's window measured the way a fast host polls. Prints the cost table in milliseconds. Compiled `-DA2_HOST`, which keeps the counters out of the shipping image. **Verify the stubs model what the machine does** - the C64's `blit1` stub REFUSED for a whole wave, so the cost table priced the fallback and nobody noticed |
| `tools/a2ref.py` | an **INDEPENDENT pixel-level reference compositor** in Python, written from AppleWin's `NTSC_CharSet.cpp`, apple2emu's `video.cpp` and MII's `mii_video.c` - **never from `a2band.inc`**. **All three composers and MIXED** from wave 3, off the state file's own mode bytes. Also asserts the pinned ROM's measured shape (`--romshape`: 128 distinct 8-byte bitmaps, block `$00` = block `$80` XOR `$7F`) and the lo-res **luminance ladder** over all 256 ordered pairs (`--lumcheck`), against luminances it derives from MII's `palettes[0]` "Color NTSC" (`src/mii_video.c:94-113`) through MII's own lo-res mapping (`:173-177`) rather than from the package's table - apple2emu's `Lores_colors` being the cross-check that agrees on every lit/dark decision. The harness compares **bit for bit**. This is the file that catches a composer whose transcription is correct and whose assembly is not. `--selftest` injects a one-bit defect and requires the compare to FAIL |
| `apps/apple2/hosttest/a2memtest.asm` + `.sh` | `a2mem.inc`'s and `a2band.inc`'s string loops on a real x86 with SS != DS and an ES sentinel, in raw QEMU, with **four negative controls** - one each for ES, DF, BP and DS. **All THREE composers from wave 3**, each against hand-computed packed bytes: it is the only gate that runs the SHIPPING ASSEMBLY rather than a second transcription of it, which is what `a2uitest` and `a2ref.py` between them cannot be. In `build.sh`, because it takes seconds. From the Disk II wave it also covers the segment arithmetic that reaches a track inside a claim larger than 64KB |
| `apps/apple2/hosttest/a2cputest.asm` + `.sh` | section 4.4's twelve rows. `make a2cputest`, minutes, **not** in `build.sh` |
| `tests/a2band/a2bandbench.asm` | the composers' icount bench on `tests/benchlib.inc`, `make a2bandbench`. **Driven under plain `-icount shift=3` and NOT `sleep=off`** (section 7.9). Per CELL, per SOURCE BYTE and per CALL in microseconds for all three composers plus `rowspan`/`rowcopy`/`rowsig`/`band_x2` - and, from wave 5, **per foreign FRAME for each FSX writer against the windowed flush on the same change set**. **The tier table and the cost table are written from these numbers.** It arms the clip on its rerun callbacks, saves ES around every blit, and preflights `OSAPI_GFX_BLIT1` |
| `tests/apple2part.py` | registered in `tests/suite.py`'s **soak** tier, or the fast tier's own registration row fails the build. `c64part.py`'s shape: `APPLE2.ROM` is NOT a file on the disk; the package file is image + 14,848; `os88_part_seg(0)` is the segment the C put in the machine record; three windows of the ROM read out of the guest equal `build/apple2-rom/APPLE2.ROM` byte for byte; the RESET vector at `$FFFC` reads `$FA62`; **and the CHARGEN table and the reverse table exist after `os88_main` and BEFORE any wake** - the negative control for keeping them off the overlay |
| `tools/a2bas.py --selfcheck` | the welcome listing tokenised against the PINNED ROM's own token table and LISTed back through it, in the recipe that writes `build/WELCOME.BAS` (section 16.2). A tokeniser's output looks fine and runs wrong |
| the SDK-toast row in `tests/unit/t_mirror.py` | the SDK's three overlay refusals in `apps/cc/crt0.asm` against `TOAST_MAX` read out of `kernel/toast.inc`, for a **full-length** `CC_PKG_NAME` (the cap read out of `crt0.asm`'s own `%fatal`) and for **every** `%define CC_PKG_NAME` in the tree (section 17.3). This wave found them truncating and wrote the check in `build.sh`, which was the wrong home: that script runs only for `make apple2`, so the edit the gate exists to catch would not have run it. `t_mirror` is a FAST-tier row and runs on every `make` |
| QEMU + QMP | `make test TESTAPPS=build/apple2.img`, `tools/mouse.py`, `tools/qmp.py sendkey`, `tools/shot.py --crop --zoom`. Every screendump assertion lives here |

**THE 1BPP PASS IS BINDING AND IS NOT OPTIONAL.**
`make test VIDEO=cga TESTAPPS=build/apple2.img` with
`tools/mouse.py --screen 640x200`, and `VIDEO=herc HERCSEG=0x7000` read with
`tools/hercshot.py build/qmp.sock 0x70000`. Look at a greyed menu item (grey
rounds to black on 1bpp, so it must be a checkerboard), at the About panel's
OK button, at the status row - **and at a MIXED screen and a screen that has
SCROLLED once**, because on CGA `dock_top` is 176 and only ~114 of the Apple's
192 scan lines fit, so the CURSOR ANCHOR (section 7.1) is the only thing
putting the `]` prompt and MIXED's text window on the glass - **and a LAUNCH
screen is a first-class row beside them**, because the fixed bottom anchor
this replaced showed a black window there and nothing else could see it. **QEMU double-scans CGA mode 6 to
640x400**, so a dump cropped at 640x200 shows the top half only - take every
second row.

**A DELETED-OVERLAY RUN IS A FIRST-CLASS ROW**: boot a scratch disk with
`APPLE2.OVL` removed and screendump a working Apple II at `]` whose menu
commands toast their refusal. That is what proves the CHARGEN decode and the
reverse table are resident.

**A FLASHING run needs TWO dumps ~5 ticks apart**, or it proves nothing.

---

## 17. What this port adds to the SDK

**FOUR THUNKS, AND NOTHING IN THE KERNEL.** No slot is added, no kernel
`.text` moves and no budget constant moves: each of the four is a wrapper in
`apps/cc/os88thunk.asm` + `apps/cc/os88.h` - **and the
`apps/apple2/hosttest/os88.h` stub in the SAME edit**, because a shim without
a host stub is three link failures away - over a slot the kernel already
publishes.

All four slots **exist** and all four are deliberately unwrapped for C in
`apps/cc/os88.h`'s "what is not wrapped" list, and **no C package in this tree
uses fsx** (TANK is assembly). **The wrappers are therefore GATED** - see the
cost table below.

| thunk | slot | shape |
|---|---|---|
| `os88_fsx_caps` | `OSAPI_FSX_CAPS` slot `0x02c0` | `int os88_fsx_caps(void *win, int *kind)` - out AX is the mask, DL the display's `VID_*` kind. **`kind` is an out-parameter and must therefore be a `static`** (SPEC.md 73's rule against `&local`) |
| `os88_fsx_run` | `OSAPI_FSX_RUN` slot `0x02c8` | `int os88_fsx_run(void (*entry)(void), void *win, int flags)` - the entry is a plain **resident** C function whose near offset goes in AX, and it **must never be an `ovl_`**: `tools/cc8086.py` refuses that address by name |
| `os88_fsx_mode` | `OSAPI_FSX_MODE` slot `0x02d0` | `int os88_fsx_mode(int id, void *fsi)` - the thunk does `push ds / pop es` so ES:DI is the caller's static `FSI_SIZE` block, and puts ES back |
| `os88_fsx_wait` | `OSAPI_FSX_WAIT` slot `0x02d8` | `int os88_fsx_wait(int kind)` |

`os88_fsx_run`, `os88_fsx_mode` and `os88_fsx_wait` each answer 0, or -1 on
CF. **`os88_fsx_caps` DOES NOT TEST CF and answers the MASK**, which the block
header in `os88thunk.asm` said otherwise for a wave: SPEC.md 53.4 makes it
callable from any context, lock held or not - that is the point of it, since a
mode row has to be greyed per SPEC.md 47 *before* a bracket exists to refuse
anything - so it has no refusal to report, and "no foreign mode on this
display" is a mask of 0. `a2_fsx_avail` carried a `mask < 0` guard that could
never fire (the widest mask SPEC.md 53.4 defines is VGA's 0x1EF); it is gone,
with a comment saying why there must not be one.

**AND THE GATE IS DECLARED WHERE A C AUTHOR READS IT.** `apps/cc/os88.h`
carried the seven-line rule list and no `Needs %define CC_HAS_FSX` note, while
every other gated entry point in that file says so at its declaration
(`CC_HAS_ONTIMER`, `CC_HAS_MENUS`, `CC_HAS_ABOUT`, `CC_HAS_ONCLOSE`,
`CC_HAS_WORKER`). It is also a **new kind of gate** that the file's own
`CC_HAS_*` table did not have a row for: every row there gates a callback the
package EXPORTS, and this one gates calls the package MAKES. Both are fixed in
`os88.h` - the note at the declaration, a row in the table with the clause
that says which direction it gates, and the "133 C entry points" count
reconciled to say that four of them exist only for a package that sets the
define. Without it the author gets nasm's `binary output format does not
support external references` from a line in `build/*.gen.asm` they did not
write, which is LESSONS.md 3's own confusing failure.

**THE MEASURED COST, AND IT IS +92 TO APPLE2 AND +0 TO EVERY OTHER C PACKAGE.**
`nasm -f bin` has no dead-code elimination, so a thunk nobody calls is still
image. The C64 measured its added thunks at ~18 bytes each and this section
predicted **~72 bytes**; wave 5 measured **92**, which is **23 a thunk**. The
first cut of the edit charged that to every C package in the tree, and the four
thunks are **gated behind `%ifdef CC_HAS_FSX`** instead - `os88thunk.asm`'s own
idiom, used twice already for `CC_HAS_FDLG` and `CC_HAS_PARTS`, and
`apple2.asm`'s own for `A2_SHIP`. `apps/apple2/apple2.asm` is the ONE file in
the tree that defines it. The four prototypes and the `OS88_FSX*` constants
stay unconditional in `apps/cc/os88.h`: an unreferenced prototype costs
nothing, and a package that calls one without the `%define` gets nasm naming
the symbol.

Measured, `os88pkg`'s own lines - ungated (what the first cut shipped) against
gated (what ships):

| package | image ungated | image gated | bss | resident total | spare of 61,440 |
|---|---|---|---|---|---|
| APPLE2 | 38,944 | **38,944** | 14,340 | 53,284 | 8,156 |
| CWORD | 36,538 | **36,446** | 24,533 | 60,979 | 461 |
| PACCMAN | 42,190 | **42,098** | 5,230 | 47,328 | 14,112 |
| RUNCPM | 39,846 | **39,754** | 11,689 | 51,443 | 9,997 |
| C64 | 41,642 | **41,550** | 13,190 | 54,740 | 6,700 |
| WEAVE | 51,932 | **51,840** | 9,214 | 61,054 | 386 |
| LOOM | 55,194 | **55,102** | 6,216 | **61,318** | **122** |

### 17.1 The slots used as they stand

| need | slot | note |
|---|---|---|
| key STATE for the two buttons, the shift state and the reset chords | `OSAPI_KEY_DOWN` slot `0x03f0` (wrapped) | section 6.4 - **arm it once, in `os88_main`, with the answer ignored** |
| Copy and Paste | `OSAPI_CLIP_PUT` slot `0x0320` / `OSAPI_CLIP_GET` slot `0x0328` / `OSAPI_CLIP_SIZE` slot `0x0330` (wrapped) | SPEC.md 55; both staging buffers are **transient heap claims**, not bss |
| the speaker | `OSAPI_SND_TONE` slot `0x00e8` (wrapped, worker-safe) | section 8 |
| full screen | `OSAPI_FULLSCREEN` slot `0x0110` (wrapped) | SPEC.md 11.2's window latch, not SPEC.md 53's bracket |
| a composed span down in one call | `OSAPI_GFX_BLIT1` slot `0x0418` (wrapped) | section 7.7, **and it can refuse** |
| a scroll moved, not redrawn | `OSAPI_GFX_SCROLL` slot `0x01f8` (wrapped) | section 7.7 |
| a slice loop on the UI task | `OSAPI_WM_WAKE` slot `0x0450` / `OSAPI_WM_ONWAKE` slot `0x0458` (wrapped) | SPEC.md 74.1 |
| a self-close for File > Quit | `OSAPI_WM_CLOSE` slot `0x0470` (wrapped) | spent from the WAKE, not from `os88_oncmd` |
| the tier that seeds the wall slice | `OSAPI_CPU_INFO` slot `0x0188` (wrapped) | section 7.8 |
| the heap | `OSAPI_MEM_CLAIM` slot `0x0200` / `OSAPI_MEM_FREE` slot `0x0208` (wrapped) | section 3.1 |
| the ROM inside the package | SPEC.md 20.12's parts | section 1.5 |

### 17.2 The slot that is NOT added

**A colour band in the WINDOW.** There is no `blit1` variant taking an ink and
a paper, and **this port does not add one**. It spends kernel `.text` against
`KERN_CODE_MAX`, which `docs/KERNEL-MEMORY.md` puts at roughly 512 bytes
spare, and raising it "is a decision to take with whoever asked for the
feature, not a build fix". `OSAPI_GFX_BLIT4` exists but prices a band by its
colour RUNS at ~215 us a run, which is 0.2-0.35 s per band on the target.

**The foreign video mode answers colour instead** (section 13), and the
windowed path's monochrome is greyed as a FACT with the foreign mode named as
where colour lives.

### 17.3 The SDK's own overlay refusals, and the cap nothing was checking

**ALL THREE OF THEM WERE BEING CUT OFF THE GLASS, IN EVERY C PACKAGE IN THE
TREE.** `OSAPI_TOAST` copies `TOAST_MAX` = 24 characters
(`kernel/toast.inc:85`) and **truncates** what is longer rather than refusing
it, and `apps/cc/crt0.asm`'s three overlay refusals are assembled from
`CC_PKG_NAME`, so their length is different in every package that includes the
SDK. With this package's six-character name they were 32, 30 and 38
characters and what a reader saw was:

| the literal, before | what reached the glass |
|---|---|
| `Not enough memory for APPLE2.OVL` | `Not enough memory for A` |
| `APPLE2.OVL is not on this disk` | `APPLE2.OVL is not on thi` |
| `APPLE2.OVL does not match this program` | `APPLE2.OVL does not matc` |

**The consequence was in the half that was cut, in all three.** Nothing in the
tree was looking, which is how it outlived seven packages - and it is the same
defect this port already found in its own toast one file along
(`APPLE2: the ROM claim` reaching the glass and stopping there, apple2.c).

They are rewritten to the SHAPE the cap allows rather than trimmed by eye -
**and the shape is sized from the NAME FIELD, not from the names this tree
happens to contain today**. `crt0.asm`'s header `%fatal`s a `CC_PKG_NAME`
longer than **15** and docs/C-TOOLCHAIN.md tells every package author 15 is
legal, so 15 is the number the arithmetic has to hold:

| the literal, now | prose | fits a 15-char name at | as APPLE2 sees it |
|---|---|---|---|
| `No <NAME>.OVL` | 3 (+4) | **22** of 24 | `No APPLE2.OVL` (13) |
| `No RAM: <NAME>` | 8 | **23** of 24 | `No RAM: APPLE2` (14) |
| `Old <NAME>.OVL` | 4 (+4) | **23** of 24 | `Old APPLE2.OVL` (14) |

**THE FIRST DRAFT OF THIS FIX GOT TWO OF THE THREE WRONG, AND THE WAY IT WAS
WRONG IS THE LESSON.** It kept the prose and sized it against the longest name
in the tree: `No RAM for <NAME>.OVL` is 11 + name + 4 and holds a name of
**9**, `<NAME>.OVL is stale` is name + 17 and holds **11**. Both passed,
because the longest C package name in this tree is 7 - and both would have
failed a build in a package that has not been written yet, a legally-named
`BBSTERMINAL` or `SPREADSHEET` at 11 characters, with the failure landing
inside somebody else's build and reading as APPLE2's. **A shared SDK's message
is sized by its own field**; a cap that only the corpus enforces is not a cap.

**AND IT IS A GATE RATHER THAN A COMMENT - AND NOT IN THIS PACKAGE'S
BUILD.SH.** The check was written in `apps/apple2/build.sh`, which was the
wrong home for the same reason the defect survived seven packages: that script
runs only for `make apple2`, an on-demand C target outside `all` and outside
`make test-full`, so lengthening a literal in `crt0.asm` - the one edit the
gate exists to catch - would not have run it. It is a row of
**`tests/unit/t_mirror.py`** instead, which is a FAST-tier row and therefore
runs on every `make`. It reads `TOAST_MAX` out of `kernel/toast.inc`, the
three literals out of `crt0.asm`, the name cap out of `crt0.asm`'s own
`%fatal`, and **every `%define CC_PKG_NAME` in the tree** (ten today), and it
checks both arms: the declared cap of 15, and every name that actually exists.
Verified to fail: put the old `No RAM for` literal back and the row reports
`cc_ovm_mem is 30 characters with a 15-character name`, exit 1.

**MEASURED, AND IT IS -58 IMAGE FOR EVERY C PACKAGE WITH AN OVERLAY**, which
is the 59 characters of `.data` the three shorter strings save with a
six-character name (100 characters before, 41 after) less the byte the trim's
paragraph rounding keeps. `PACCMAN` does not move because it has no overlay
and therefore never assembled the strings at all:

| package | image before | image after | bss | resident total | spare of 61,440 |
|---|---|---|---|---|---|
| APPLE2 | 38,944 | **38,886** | 14,340 | 53,226 | 8,214 |
| CWORD | 36,446 | **36,388** | 24,533 | 60,921 | 519 |
| PACCMAN | 42,098 | **42,098** | 5,230 | 47,328 | 14,112 |
| RUNCPM | 39,754 | **39,696** | 11,689 | 51,385 | 10,055 |
| C64 | 41,550 | **41,492** | 13,190 | 54,682 | 6,758 |
| WEAVE | 51,840 | **51,782** | 9,214 | 60,996 | 444 |
| LOOM | 55,102 | **55,044** | 6,216 | **61,260** | **180** |

Every row but APPLE2's is the shipping build of that package, read off its own
`os88pkg` line; **APPLE2's is the isolated figure** - the SDK change alone
against the end of wave 5 - because this wave also spent 78 bytes on section
13.1 in the same package and 80 more on section 13.2's pacing, so its shipping
line is **39,042 / 53,386 / 8,054 spare** and section 15.0.5 has that
arithmetic.

`LOOM` is the tight package (this section's own note above) and it is 180
bytes of 61,440 clear now rather than 122.

**`make test-full` is the gate that proves the edit broke none of them**, and
the change is one file every C package includes: it is the SDK, not this
package.

**AND THE SAME FAILURE CLASS IS STILL SPOKEN IN A SECOND VOICE, WHICH THIS
WAVE IS NOT FIXING.** `crt0.asm`'s three are the SDK's; one call along,
`apps/weave/wload.c` and `apps/loom/lmprev.c` toast `WEAVE.WSM is not on this
disk.` (30), `WEAVE.WSM does not match this program.` (37) and `Not enough
memory for LOOM.WPV.` (31) through `w_say` -> `os88_toast`, and they truncate
today exactly as `crt0`'s did - so WEAVE now says `No WEAVE.OVL` for its
overlay and `WEAVE.WSM is not on thi` for its module in the same window.
**Those sentences are pinned by docs/WEAVE-SPEC.md as that family's
contract**, so rewording them is the Weave family's call and not this PR's
(this wave is not to touch another package's source), and widening
`t_mirror`'s row to walk `os88_toast` literals across `apps/` would fail the
build on three strings this wave may not edit. It is recorded here so that the
next reader is not told the SDK's toasts were fixed everywhere: **they were
fixed in the SDK**, and `apps/weave` and `apps/loom` are the follow-up.

**AND IT IS LOOM AND NOT CWORD THAT IS THE TIGHT PACKAGE, WHICH THIS SECTION
HAD WRONG.** The paragraph above was written when CWORD's 1,043 bytes spare was
the smallest margin in the tree; LOOM has shipped since, and when this
paragraph was first written it built with **122 bytes** of its 61,440 left -
which is what ungated thunks would have cut to **30**. The `crt0` shortening
above gave 58 of those back, so the live figure is the table's **180** and the
ungated-thunk arithmetic is 180 - 92 = **88**; the paragraph's point is
unchanged and its numbers are the ones in the table two screens up. That is a fact to hand to whoever adds the next SDK line rather than
a problem this wave created, and it is why the gate is there: an unconditional
addition of a dozen bytes fails a build in a package that has nothing to do
with it, and the failure reads as LOOM's.

**PACCMAN and SCRIBE were measured too.** PACCMAN is a C package that this
section's list had missed (`grep CC_PACKAGE Makefile`); SCRIBE is **assembly**
- it has no `crt0.asm` and no thunk table - so it does not move, and it is
named here so the next reader does not go looking for its row.


---

## 18. The Wire listing

**Prepared in wave 7, on a branch `apple2-wire` off os8088-web's `main`, and
NOT committed in the web repo until the OS side is done and the user has said
go.**

**The brand table in `docs/WIRE-PLAN.md` is fixed by the user and is not to be
reworded.**

### 18.1 The record

One object in `data/wire.json`, on the C64's exact shape. `validate_program`
requires **exactly** these keys, no more and no less.

| field | value |
|---|---|
| `stem` | `APPLE2` - 6 characters, matches `[A-Z0-9_-]{1,8}` |
| `title` | `Apple II+` - 23 characters maximum |
| `kind` | **0** (Applications), as C64 and RUNCPM are |
| `tier` | **3** (`486+`), from the MEASURED 0.54% of section 16.4.1 - 0.41% when the record was written, and section 4.3.1's slice controller does not move a tier that is about a machine being USABLE rather than about a decimal - the same tier the C64 takes, for the same honest reason. A judgement, and the release skill asks for every tier touched to be flagged as reviewable in the PR |
| `flags` | `["new"]` - only `new` and `floppy_only` are manifest-settable |
| `files` | `APPLE2.O88`, `APPLE2.OVL`, `WELCOME.BAS`, `{name: APPLE2.TXT, source: apps/apple2/README.TXT}`, `{name: COPYING.A2, source: apps/apple2/COPYING}` - five, because the folder is the binding shape (section 16.2) and a listing nobody can open is a file that should not be published. A bare string is taken from `<os-repo>/build/`, which is where the first three live; `files[0]` must be exactly `<STEM>.O88` |
| `summary` | one sentence, printable ASCII, the site card's |
| `description` | **pre-wrapped by the writer to 5 lines of 27 characters** - single spaces, no word longer than 27. This is the field the machine draws |
| `page` | an HTML fragment that says **honestly what an XT does and does not do, and that colour is a VGA feature** |
| `screenshot` + `crop` | both set or both null |
| `spotlight` | `null`, or `/spotlight/apple2/` with the page written |

**`WF_DISK` is set AUTOMATICALLY and cannot be avoided**: the writer sets it
when there is more than one file **or** the package header's flags byte has
bit 2 set, and **a ROM shipped as a part sets bit 2**. So the record greys
**Load Program** with `Needs its files on a disk - use Add to Disk`, exactly as
the C64's does. `WF_PIC` is derived from `screenshot` being non-null.

**`FILE_MAX` is 64,512 per file**, and the MEASURED `APPLE2.O88` is **54,272**
with its ROM inside - 10,240 bytes of headroom, where the planned figure was
~58,300. That headroom is why Integer BASIC is refused (section 10.4).

### 18.2 The rest of the change

| what | where |
|---|---|
| the package bytes | `public/wire/pkg/APPLE2.O88`, `APPLE2.OVL`, `WELCOME.BAS`, `APPLE2.TXT`, `COPYING.A2` |

**AND THE README AND THE LICENCE ARE PUBLISHED UNDER NAMES OF THEIR OWN,
WHICH IS NOT A PREFERENCE.** `public/wire/pkg/` is ONE FLAT DIRECTORY shared
by every record, and the C64 already publishes `README.TXT` and `COPYING`
there: a second record naming those files **overwrites the C64's bytes**,
which `os88wire.py --verify --pkgdir` then fails on, because the C64's catalog
entry still says 3,842 and the file on disk is this package's 9,500. Measured,
by doing it: one `release.py` run wrote `wire/pkg/README.TXT 3,842` and
`wire/pkg/README.TXT 8,575` on consecutive lines (the second figure is that
day's `README.TXT`; it is 9,500 now and the collision is the same one).
**It is the user's disk too** - Add to Disk writes the published name - so two
programs fetched into one folder would collide there as well.

**AND THE README HAD TO LEARN ITS OWN PUBLISHED NAMES**, which the review
caught: `apps/apple2/README.TXT` described the folder as *"THREE FILES AND A
LICENCE"* and told the reader the licence was in `COPYING` and that *"All four
must be in the SAME folder"*. On a Wire-installed disk the licence is
`COPYING.A2` and the readme itself is `APPLE2.TXT`, so the file named two of
the four wrongly - including the one the GPL notice points at - on the one
route where the reader cannot check by looking at the floppy they were given.
It now names both Wire names in the `COPYING` entry, and it says what actually
has to share a folder: **`APPLE2.O88` and `APPLE2.OVL`, plus `WELCOME.BAS` for
the double-click route**. The licence is a document nobody but the reader
opens and may sit wherever it lands. **The floppy build was never affected**;
only the Wire route was.

**AND AN 8.3 NAME DOES NOT ALWAYS FIT THE SIDECAR FIELD.** The catalog's
sidecar name is **12 bytes NUL-terminated**, so **11 characters**, and
`A2README.TXT` is 12 - the writer refuses it (`wire: APPLE2 sidecar: 12
bytes, maximum 11`). An 8-character stem with a 3-character extension is a
legal 8.3 name and an illegal sidecar name; the stem has to be **7 or
shorter**. `APPLE2.TXT` (10) and `COPYING.A2` (10) both fit, and both say
whose they are on a disk that may already hold the C64's.
| the machine's picture | `public/wire/pic/APPLE2.PIC` - **generated**: 1,024 bytes, 128 x 64, 1bpp, 16 bytes a row, bit 7 leftmost, **a 1:1 crop of a real screendump and never scaled** |
| the site screenshots | `public/img/apple2/`, with `tools/scenes.apple2.json` |
| the catalog and the page | `public/wire/catalog.bin` and `public/wire/index.html` - both **generated** |
| the stale count | `site/wire.html`'s program count |

`public/wire/pkg/` and `public/wire/pic/` are **swept**: a file no record names
is deleted by a build and reported as an orphan by `--check`.

### 18.3 Verified from the OS side first

```
python3 tools/os88wire.py --verify ../os8088-web/public/wire/catalog.bin \
        --pkgdir ../os8088-web/public/wire/pkg
python3 tools/os88wire.py --dump
```

then `python3 tools/build.py --check` and `python3 tools/linkcheck.py` in the
web repo. `make && make thewiretest && python3 tests/thewire.py` is unaffected
and is run anyway.
