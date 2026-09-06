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
| AppleWin | GPL-2 | everything II+-specific: the keyboard byte map, the soft-switch fences, the ROMs, the disk formats and the nibbliser |
| MII (`mii_emu`) | MIT | the Macintosh-shaped menu bar, the About panel, the soft-switch enumeration, the per-write dirty-line map, the flash phase and the hi-res artifact-colour index |
| apple2emu | MIT | the 24-entry row-base tables, the Edit menu, the Disk II controller, the paddle one-shot and the paste handshake |

**`apps/apple2/COPYING` and `apps/apple2/README.TXT` ship from WAVE 1**, not
from the polish wave: `a2cpu.inc` is derived from a GPL file in wave 1, wave 1
puts four floppies of that binary on disk, and the C64 disk recipe names both
files as **prerequisites and payload** (`Makefile:4855`), so a recipe modelled
on it fails with *No rule to make target* without them.

`COPYING` carries the GPL-2 text plus MII's and apple2emu's MIT notices
verbatim.

### 1.4 The ROMs - fetched at a pin, never committed

**The Apple II+ ROM images are Apple Computer's copyright.** This port takes
the stricter posture the C64 did not: they are **fetched at a pinned SHA-256
by `tools/getapple2rom.py` and never committed**, exactly as
`tools/getruncpm.py` pins RunCPM's master disk and `tools/getstories.py` the
Frotz stories. The copies used are the ones AppleWin (GPL-2) carries in its
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
| Lo-res 16-colour palette, for the VGA13 DAC and for the 1bpp luminance ladder the windowed path uses | apple2emu `src/video.cpp:94-113` (mrob.com values); cross-check AppleWin `source/RGBMonitor.cpp:118-165` (`PaletteRGB_NTSC`) |
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
| The reboot confirmation for Machine > Power cycle, TWO rows and not one: `Are you sure you want to reboot?` then `(All data will be lost!)`. The five lines after it in the reference are about an AppleWin Configuration checkbox this port does not have and are dropped | AppleWin `source/Windows/WinFrame.cpp:2002-2012` |
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
`a2_copy_row` (the eleven-instruction assembly Copy loop) and, from wave 6,
the segment arithmetic that reaches a track inside a disk-image claim larger
than 64KB.

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
the source.

### 4.5 Reset, both kinds

| | what happens |
|---|---|
| **power-on** | lay AppleWin's `FF FF 00 00` pattern over `$0000-$BFFF` with its three pokes (`$4E`/`$4F` forced non-zero, `$620B` = 0, `$BFFD..$BFFF` = 0), SP = `$1FF`, poke `$3F2`/`$3F3` = `$55 $55` so the Autostart Monitor's power-up check fires, then jump through `$FFFC` |
| **Ctrl-Reset** | pull SP down by 3 and jump through `$FFFC`. Nothing else moves |

---

## 5. The soft switches

### 5.1 The II+ subset, and the fence

`$C000-$C0FF` is answered by `a2io.c` in **both directions**.
**EVERY READ HERE IS SIDE-EFFECTING and the file says so at the top.**

| address | read | write |
|---|---|---|
| `$C000` | keycode OR (waiting ? `0x80` : 0) | - |
| `$C010-$C01F` | clear the strobe, then answer the floating bus | clear the strobe |
| `$C030` | toggle the speaker, answer the floating bus | toggle the speaker |
| `$C050` / `$C051` | TEXT off / on | same |
| `$C052` / `$C053` | MIXED off / on | same |
| `$C054` / `$C055` | PAGE2 off / on | same |
| `$C056` / `$C057` | HIRES off / on | same |
| `$C061` / `$C062` / `$C063` | `$80` when the button is DOWN | - |
| `$C064-$C067` | bit 7 set until the one-shot's deadline passes | - |
| `$C070-$C07F` | arm all four one-shots at `now + value*11` emulated cycles | same |
| `$C0E0-$C0EF` | the Disk II controller (wave 6, section 14) | same |

`$C100-$CFFF` answers `$FF` where nothing is mapped.

**THE II+ FENCE.** `$C010-$C01F` on a II+ is nothing but "clear the strobe,
then answer the floating bus". apple2emu registers the //e status switches
over that whole range **unconditionally** and is wrong for this machine.

**Not on this machine, and NOT greyed either** (section 10.4): `$C000`/`$C001`
80STORE, `$C00C`/`$C00D` 80COL, `$C00E`/`$C00F` ALTCHARSET,
`$C05E`/`$C05F` DHIRES.

### 5.2 The floating bus is refused, with the arithmetic

An unmapped read answers **`$FF`**, which is apple2emu's own posture. AppleWin's
true floating bus needs a cycle-exact video scanner - **262 x 65 = 17,030
cycles a frame and eighteen bit extractions on every unmapped read** - to
compute a byte almost nothing reads, and it is the one place a bug is
invisible.

### 5.3 Reads with side effects are the shape that boots and is wrong

`$C010`, `$C030`, `$C050-$C057`, `$C061-$C063`, `$C064-$C067` and `$C070` are
all **read-active**. The C64's plumbing already calls out for both directions,
so this is a free fit - but **a C side that treats a read as pure gives a
machine that boots to `]` and then never changes video mode.** That is
`a2cputest` row 8, and it is not optional.

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
| **Ctrl+F3** | Open-Apple-Ctrl-Reset | scan `0x60`, same set |
| **Alt+Enter** | full screen, both directions | **AppleWin's own chord**, out of `help/keyboard.html`. An Apple II+ has no Alt key, so it collides with nothing |
| **Ctrl+F** | full screen | SPEC.md 11.2.1's unconditional door, kept |
| **F1** / **F2** | the Open-Apple and Solid-Apple **buttons**, read as LEVEL through `os88_key_down` | a departure from AppleWin's Left-Alt / Right-Alt, recorded with its reason: Alt+Enter is this port's fullscreen chord |

**Alt+Enter is the C64 precedent read correctly.** The C64's Alt+D is VICE's
OWN hotkey out of `data/hotkeys/hotkeys.vhk`, so the precedent is *keep the
ORIGINAL's chord*, not *keep the other port's key*.

**Ctrl-Reset also has a menu item**, which is the guaranteed route. **The
fullscreen chord must be implemented in RESIDENT code**, because a `WF_FULL`
window has no menu bar and a chord that had to load `APPLE2.OVL` would refuse
on a bar the user cannot see.

**All three chords are confirmed on iron in wave 7** (section 16.5). QEMU's
SeaBIOS passes enhanced codes a real AT BIOS drops, so a chord table filled in
under QEMU describes a different BIOS.

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

**Paste** types a listing into the keyboard latch on apple2emu's
peek-and-consume handshake: `$C000` peeks the next byte with bit 7 set,
`$C010` consumes it, `\n` folds to `\r`, `\0` ends the paste. **The emulated
program sets the rate, so there is no pacing state at all.**

Paste is the **primary way a BASIC program gets in** before Disk II exists.

### 6.6 The keyboard-mouse rule

`C64-SPEC §7.6`, unchanged: on a machine with no mouse the kernel eats the
arrows, Space and Del as its pointer, and ScrollLock hands them back. The port
**infers this from the observable** - the down-map says an arrow is held and
`os88_onkey` has never once delivered one - over three consecutive polls, with
a one-way latch.

---

## 7. The screen

### 7.1 The window

| | |
|---|---|
| content | **336 x 218** - the Apple's 280 letterboxed **16 px left and 24 px right** so the composer's OUTPUT stays byte-aligned even though its cells are not, plus 192 scan lines, plus an 8 px border top and bottom (16), plus a 10 px status row |
| frame | **content + 2** - `os88_wm_create` authors a FRAME and `os88_wm_geom` answers the CONTENT box, and the window's two 1-pixel side borders are the difference (`C64-SPEC §9.1`) |
| origin | `os88_wm_snap` puts the content x on a cell boundary, which is what lets `OSAPI_GFX_SCROLL` accept the rect |
| height | **asked against `os88_video().dock_top`**, on `apps/c64/c64.c:1684`'s shape. A 200-line desktop cannot give 218 |

**THE BOTTOM ANCHOR, and it is a correctness requirement rather than a
preference.** On a 640x200 desktop `dock_top` is 176 and only about **114 of
the Apple's 192 scan lines fit**. The visible band therefore anchors to the
**BOTTOM** of the Apple frame, because the `]` cursor line and MIXED's four
text rows are both there. Machine > Video > Full screen is the stated route to
the whole frame.

The flush reads the **live** content box every time it runs: the status row is
at `content.y + content.h - 10` whatever that is, the scan lines drawn are
what is left between the borders, and the bottom border fills between the last
drawn line and the status row.

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
| `a2_band_lores` | 40 x 48 blocks of 7 x 4 | `c = (byte >> ((line/4 & 1)*4)) & 0xF`, through a 16-entry luminance ladder held as `{and, xor}` pairs |
| `a2_band_hires` | 280 x 192 mono | 40 source bytes -> 35 output bytes a scan line through the 128-entry 7-bit reverse table, **high bit dropped** |

All three share **ONE 7-bit shift accumulator and one span argument**. Stride
is the assemble-time constant **40** and the rows are unrolled
(PERFORMANCE.md Set 64's lesson). Beside them: `a2_rowspan` (two `repe cmpsb`,
the second with DF set), `a2_rowcopy`, `a2_rowsig`, `a2_x2init` and
`a2_band_x2`.

**The luminance ladder is `{and, xor}` pairs and not an XOR alone**, because
an equal-luminance cell is `{0x00, level}` and an XOR alone cannot express a
uniform block.

**The character generator, and why 64 glyphs cover 128 bitmaps.** Decoding the
pinned `Apple2_Video.rom` through AppleWin's own algorithm yields **128
distinct 8-byte bitmaps, not 64**: blocks `$40`, `$80` and `$C0` are
byte-identical (normal) and block `$00-$3F` is their XOR `0x7F` (inverse). The
common reading - 64 glyphs repeating every `$40` - is true only across
`$40-$FF`. **The port stores 64 glyphs (512 bytes of bss) and applies a
per-cell XOR mask**, which is the same mechanism flashing needs, so it is not
a second one. `tools/a2ref.py --romshape` asserts the 128-bitmap count, so the
decision is checkable rather than remembered.

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
changes the renderer marks the whole frame dirty.

### 7.5 The damage model

Taken whole from `C64-SPEC §9.2`, with the mapping step replaced.

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
- **The flush takes all of it in ONE call** - `a2_dirty_take`, ~190 us,
  resetting the scratch in the same pass. The C64 measured the alternative at
  ~50 near thunks at ~38 us each = **~1.8 ms a flush spent before a pixel was
  decided**, and invisible to a call-counting cost model.

### 7.6 The flash phase - the one thing on the glass the damage model cannot see

Flashing text **changes pixels with no memory write**. Nothing write-driven
will ever mark its lines dirty, and the `]` cursor would simply never blink.

**The fix is a phase counter in host ticks.** 16 frames at 60 Hz is ~267 ms,
which is ~5 host ticks. On each flip the flush **force-dirties every scan line
whose text row holds a byte in `$40-$7F`**, through a per-line force bitmap
(24 + 24 bytes of bss). The swap itself is `+/- $40` on the screen byte, which
in this port is the per-cell XOR mask the composer already applies - so
flashing costs no second mechanism.

**On the `CPU_8086` tier the phase is refused and the item greys with the
measured cost** (section 10.3), because a forced text repaint twice a second
is a documented number and not a guess. Refusing is legitimate; silently not
flashing is not.

### 7.7 The frame shadow and the flush

A **7,680-byte frame shadow** in bss - 40 x 192 bytes, exactly the pixels last
blitted. **The shadow is the glass, not the model.**

The flush runs **at most once per host tick**, never once per slice, and holds
the gfx lock only around itself:

1. compose the dirty scan lines;
2. test for a whole-frame shift - the **k-row scroll test** on a 16-bit
   signature of the row's SOURCES, asked only when at least 4/5 of the rows on
   the glass are dirty (a fraction, not a count). On a hit: one
   `OSAPI_GFX_SCROLL` plus the k vacated rows. The signature is a **hint** -
   every shifted row is still composed and compared against the moved shadow,
   so a collision costs a redraw and never a wrong screen;
3. otherwise compare each composed line against the shadow and **draw only the
   differing spans**;
4. update the shadow;
5. the letterbox fills, only when they changed;
6. the status row, **delta-drawn**.

**Two flags mean different things**: one says the SOURCES changed, the other
says the GLASS is unknown over a span. Setting both on a switch write that
changed nothing is the C64's measured 25 forced blits.

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

| adapter / tier | fullscreen |
|---|---|
| VGA | **2x both axes**, centred |
| CGA 640x200 | **2x horizontal only** - a CGA pixel is already 2:1, so 1:1 draws the picture half as wide as it should be |
| Hercules 720x348 | 2x horizontal, 1x vertical |
| the `CPU_8086` tier | **1:1 centred**, whatever the adapter can hold |

**The tier table is written FROM `tests/a2band`'s measured numbers in wave
3**, and the same measurement decides whether the `CPU_8086` tier flushes
every tick or every other one.

**`OSAPI_FULLSCREEN` repaints synchronously in both directions**, so the
success arm does nothing - a shadow-invalidate there is pure double-draw. It
is the **REFUSED** arm that owes a repaint.

### 7.9 The cost table - PLANNED until wave 3 measures it

**Every row here is PLANNED and is rewritten from `make a2bandbench` in wave
3**, and again in wave 5 for the foreign rows. The bench runs under
`make test QEMU="...-icount shift=3,sleep=off"`, where **one PIT count is
0.359 ms of a real 4.77 MHz XT**, which is where every millisecond in this
document comes from and is why the foreign-mode measurement is not blocked on
an 86Box machine.

Rows to publish, in milliseconds and never in calls:

| operation | measured in |
|---|---|
| one changed cell | wave 3 |
| one changed text row | wave 3 |
| one changed hi-res scan line | wave 3 |
| two pokes at opposite ends of the page | wave 3 |
| a k-row scroll | wave 3 |
| a full text repaint | wave 3 |
| a full 280 x 192 hi-res repaint | wave 3 |
| **ONE FLASH PHASE FLIP** - the number that decides the `CPU_8086` greying | wave 3 |
| a mode switch that draws the same picture | wave 3 |
| eight soft-switch reads that change nothing | wave 3 |
| entering fullscreen at 2x on VGA | wave 3 |
| **ONE FOREIGN FRAME per FSX writer, against the windowed flush on the same change set** | wave 5 |
| ONE TRACK CHANGE through the nibbliser | the Disk II follow-up |
| a slice with no tick boundary | wave 3 |

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
`a2_sound_stop()`, and a pause, a reset, a JAM, warp, Machine > Audio > Mute
and the About panel all reach it. Capability is established **once** in
`os88_main` from `os88_snd_caps()`. A refused grant (-1: another instance
holds the speaker) is retried, bounded at eight wakes, then dropped with the
fact said once.

It reproduces the beep, `CHR$(7)` and any square-wave tone loop.

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
| message area | refusals, the overlay's `Unable to load APPLE2.OVL.`, the speaker's stated fact, and - from the Disk II wave - the sentence that says the drive is spinning and names Ctrl-Reset as the way to `]` |
| speed | the **measured** percentage of a 1.02 MHz Apple II. The units are written from the XT reading taken in wave 7 |
| video mode | the live mode: `TEXT` / `LORES` / `HIRES`, plus `MIXED` and `PAGE2` |
| drive | the motor lamp and `%02d/%02d` of current track and current byte over 256 - **the Disk II follow-up PR** |

Every message literal in `apps/apple2/*.c` is walked against the row's cell
cap by `build.sh` (section 16.6), with an explicit expected **minimum**, so a
corpus of zero literals is a failure and not a pass.

---

## 10. The menus

### 10.1 The set

**FOUR menus on the kernel bar - File, Edit, Machine, CPU** - which is MII's
own count, with **Video's and Audio's rows INSIDE Machine under separators**
because the os8088 bar has no submenu mechanism at all. The Apple menu becomes
the kernel's own name pull-down. `AM_NAME` is **`Apple II+`**.

| menu | rows |
|---|---|
| **File** | Load Program... , Save Program... , separator, Quit |
| **Edit** | Copy, Paste |
| **Machine** | Ctrl-Reset, Open-Apple-Ctrl-Reset, Power cycle, Configure Slots... , Joystick... , separator, **Video:** Full screen, White, Color, Green, Amber, Flashing text, separator, **Audio:** Mute, Louder, Quieter |
| **CPU** | Stop / Continue (dynamic), Warp, Fast: 3.5MHz, Step, Next |

**Every caption, mnemonic and shortcut string not fixed in section 2 is
transcribed in wave 1** from the authority row's own file -
`mii_mui_menus.h`, apple2emu's `interface.cpp`, or the AppleWin file that owns
it - and each greyed item carries **the FACT that greys it in a comment beside
it** in `a2menu.c`.

**MII's dynamic retitling is kept**: Stop becomes Stopped and Running becomes
Continue.

**File > Quit is answered in the RESIDENT half** and not in the overlay,
because it is the one command that must work on a disk whose `APPLE2.OVL` is
missing. It goes through `OSAPI_WM_CLOSE`, spent from the WAKE and not from
`os88_oncmd`.

### 10.2 What is live

Load Program... , Save Program... , Quit, Copy, Paste, Ctrl-Reset,
Open-Apple-Ctrl-Reset, Power cycle (with the two-row confirmation), Full
screen, White, Flashing text (outside the `CPU_8086` tier), Mute, Stop /
Continue, Warp.

**Machine > Power cycle's confirmation is TWO rows and not one:**

```
Are you sure you want to reboot?
(All data will be lost!)
```

### 10.3 Present and greyed - the fact that greys it (SPEC.md 47)

| item | the fact |
|---|---|
| **Machine > Configure Slots...** , in this PR | `No Disk II in this build. Load Program reads an Applesoft program, and Paste types a listing in.` |
| Machine > Configure Slots... , once the Disk II follow-up lands: every slot but 6 | `Slot 6 holds a Disk II. There are no other cards in this port.` |
| Machine > Joystick... (already `.disabled = 1` in MII's own menu table, which is the authentic grey) | `The paddles answer centre. The Open-Apple and Solid-Apple buttons are F1 and F2.` |
| Machine > Video > Color / Green / Amber, on the windowed path | `The window is monochrome. Colour is in the foreign video mode - Machine > Video > Full screen on a VGA.` |
| Machine > Audio > Louder / Quieter (Mute ships live) | `The Apple's speaker is a one-bit toggle. There is no volume on it.` |
| Machine > Video > Flashing text, **on the `CPU_8086` tier only** | `Flashing forces a text repaint twice a second. On a 4.77 MHz 8088 that is <measured> ms each time and the machine would spend it on the phase rather than on the 6502.` The number is filled in from wave 3's bench |
| CPU > Fast: 3.5MHz | `This machine runs at 1.02 MHz. Warp is this port's speed control and is beside it.` |
| CPU > Step, CPU > Next | `There is no debugger in this port.` |
| `.NIB`, `.WOZ`, `.2MG`, `.HDV` on the disk dialog's refusal, named rather than silently rejected (the follow-up PR) | `This build reads a 143,360-byte .DSK, .DO or .PO. A .WOZ is a flux image and needs a bit-cell model - a decision every four emulated cycles, which on a 4.77 MHz 8088 is the difference between a slow emulator and a stopped one.` |
| a wrong-sized disk image, on the "Invalid Disk Image" alert (the follow-up PR) | `File '<name>' is the wrong size, <size> too <big\|small>.` - MII's wording, with the size formatted human-readably as MII formats it, **not** as a raw byte delta |

**The speaker's synthesis fact** is stated on the status row and in section 8,
which is its only home now that a Mockingboard row is off the bar entirely.

### 10.4 What is absent, and why greying it would be wrong

**The whole //e machine**: 80STORE, RAMRD/RAMWRT, ALTZP, ALTCHARSET, 80COL,
DHIRES/AN3 and the aux bank. AppleWin's `IS_APPLE2` / `IsAppleIIeOrAbove`
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
(`C64-SPEC §12`): modal, the machine paused while it is up, its close drawn as
**damage and not as a repaint**, the panel snapped to the cell grid so the
rows it covers are exact and `os88_paint` skips them, and redrawn only when
the damage reaches it.

**TWELVE ROWS MAXIMUM.** That is a 640x200 compatibility constant and it
carries a comment saying so: a control's y is `6 + row*10` and nothing clamps
it. The panel's width and height are clamped to the live content box, with the
OK button clamped inside the panel.

**What it carries, and this list is the binding part:**

1. the product - `The Apple II Plus Emulator`;
2. the version;
3. what this **port** is;
4. the four attributions - VICE, AppleWin, MII, apple2emu - with their
   licences;
5. the ROM copyright line - the ROMs are Apple Computer's.

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

**Load Program...** uses `os88_fdlg` for the picker, then reads the file into
`$0801` with a documented **2-byte-length-prefix sniff** - if word 0 equals
`filesize - 2`, skip it; otherwise the file is headerless - and writes
`TXTTAB` = `$0801` and `VARTAB` = `ARYTAB` = `STREND` = `PRGEND` = end.

**Save Program...** writes `$0801` to `VARTAB - 1`, headerless.

The bodies are `ovl_*` in `a2prog.c`, called by the **resident**
`os88_onfile`.

**`CC_ASSOC` declares `BAS`** (SPEC.md 54.6), so a `.BAS` opens on the FIRST
double-click of a **cold** boot with no prior run - which CWORD's runtime
`os88_assoc_set` could not do. `DSK`, `DO` and `PO` are declared **only when
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
| **`FSXM_CGA640`** | 560x192 inside a 640x200 mono frame | **the Apple's native monochrome geometry**, half-dot shift and all |
| **`FSXM_HERC`** | the same 640-wide viewport in Hercules' four banks | as above |

**`FSXM_VGA13` ships on its COLOUR, which is not in question. `FSXM_CGA640`
and `FSXM_HERC` ship ONLY IF THEY MEASURE FASTER THAN THE WINDOWED PATH on
the XT tier.** They are 560 x 192 = **13,440 bytes a frame against the windowed
7,680** - 1.75x the raster work on the slowest machine, in the name of speed -
so "an XT gets its speed back" is a claim to prove and not to assert. If they
lose they are **cut**, about 1,200 bytes come back, and Machine > Video > Full
screen greys on those adapters with the measured fact.

**`FSXM_VGA13` is VGA-only**, so *"this port is in colour"* is a claim about a
**VGA-class machine** and this document says exactly that rather than implying
an XT gets colour.

### 13.2 Every foreign frame is driven off the dirty-line set

**A full raster write per frame is precisely what the dirty-page bitmap, the
write window, the scan-line map and the span compare exist to avoid**
(PERFORMANCE.md Part 5). So the foreign writers are driven off the **SAME
dirty-line set the windowed flush computes**, against a **foreign-frame
shadow in a heap claim**.

The arithmetic that makes this non-negotiable: VGA13 at 280 x 192 = 53,760
pixels with a per-pixel artifact index is **~300 ms a frame on a 4.77 MHz
8088** (PLANNED, measured in wave 5).

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
- the exit is **the proc returning**, on F with Esc as the escape hatch
  (SPEC.md 53.7). The ~200 ms `wm_paint_all` it costs is paid once a session.

### 13.4 Measure first, then write

**Wave 5 gains its bench rows before it writes a writer.** `a2bandbench`
prints ms per foreign frame for each writer against the windowed flush **on
the same change set**, and that measurement is what decides whether CGA640 and
HERC exist at all.

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

## 15. The budget - PLANNED

SPEC.md 73's cap is **61,440** for resident image + bss, and SPEC.md 73.9's
split trigger is **55,000 resident**.

**Five figures are reported at every wave from wave 2 on**, separately:
resident image, bss, `APPLE2.OVL`, resident shims, largest C frame.
**There is deliberately NO wave-1 size line** - a core with only a few opcode
families assembled is not an honest measurement of a core, and quoting one
would set a budget against a number nobody can reproduce.

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
| `a2scr.c` | **+900** - the interleaved page-to-scan-line map, both screen-hole tests, MIXED, PAGE2, the flash phase and its force pass, the clamped-window bottom anchor, all against the C64's linear row arithmetic |
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

**There is no second overlay.** One `.OVL` per package, by construction.

### 15.5 The file split

| file | holds | resident |
|---|---|---|
| `apps/apple2/apple2.c` | the translation unit's root and the only file nasm ever sees a `.c` through: the GPL-2 + four-attribution header, every prototype, the geometry constants, `os88_main` (the 64KB RAM claim, `os88_part_seg(0)`, **the CHARGEN decode and the 7-bit reverse table**, the fetch-bias underflow guard, `os88_snd_caps` once, the tier init, the RAM power-on pattern, `os88_key_down` armed here and nowhere else, the window sized against `os88_video().dock_top`), `os88_paint`, `os88_onkey`, `os88_onclick`, `os88_onfile`, `os88_onwake` (THE slice driver), the latches the wake spends before the slice, and the `#include`s in order | yes |
| `apps/apple2/a2io.c` | the `$C000-$C0FF` soft switches, both directions; the keyboard latch and strobe; the video switches guarded by value; the speaker toggle and its interval estimator; the paddle one-shots and the two buttons; the slot-ROM ladder; and (the follow-up) the Disk II controller's sixteen `$C0Ex` cases with the 6-cycle re-read. **Every read here is side-effecting and the file says so at the top** | yes |
| `apps/apple2/a2kbd.c` | the scancode-to-Apple-byte map with its rejections and folds, the Ctrl folds routed on SCAN, the reset chords, Alt+Enter, the paste feeder's handshake, and Copy's screen-encoding walk. **The per-byte loops are HERE and resident**; only the command shells are `ovl_` | yes |
| `apps/apple2/a2scr.c` | the damage model, the frame shadow, the flash phase and its force pass, the flush, the k-row scroll test, the mode dispatch, the tier table, the letterbox fills, **the clamped-window bottom anchor**, and the fullscreen geometry (`a2_scw`/`a2_sch`, decided in ONE place) | yes |
| `apps/apple2/a2menu.c` | the four menu tables with every string, mnemonic and caption, the `OS88_MENU_DIS` greying with the FACT in a comment beside each item, the menu-set struct, and the `os88_oncmd` dispatcher - two compares and then an `ovl_`, except File > Quit | yes |
| `apps/apple2/a2cmd.c` | `ovl_*`: the first-wake probe and every menu command SHELL. **It does NOT carry the CHARGEN decode or the reverse table.** No per-byte loop is written in this file | **no** |
| `apps/apple2/a2prog.c` | `ovl_*`: the Load and Save Program bodies (section 12) | **no** |
| `apps/apple2/a2disk.c` | `ovl_*`: the Configure Slots dialog, the image open and validate, the per-drive claim and drive 2's refusal, the sector-order pick and the eject (the follow-up PR) | **no** |
| `apps/apple2/a2about.c` | `ovl_about_show` (section 11) | **no** |
| `apps/apple2/a2cpu.inc` | the 6502 core (section 4) | yes |
| `apps/apple2/a2mem.inc` | the claim accessors and the movers (section 3.4) | yes |
| `apps/apple2/a2band.inc` | the three composers and the row primitives (section 7.3) | yes |
| `apps/apple2/a2nib.inc` | the 6-and-2 encoder (the follow-up PR) - **assembly and resident** | yes |
| `apps/apple2/a2fsx.inc` | the three foreign-mode raster writers (section 13) | yes |
| `apps/apple2/apple2.asm` | the shim, and nothing else belongs in it: `CC_PKG_NAME 'APPLE2'`; `CC_HAS_ONKEY` / `ONCLICK` / `ABOUT` / `ONWAKE` / `MENUS` / `FDLG` / `OVL` / `PARTS` / `ICON` / `ASSOC` (**no `WORKER`**); `%include cc/crt0.asm`, then `CC_PARTS_BEGIN 1` / `OS88_PART OP_ASSET` / `CC_PARTS_END`, then `apple2.gen.asm`, then the six `.inc` files, then `CC_IMAGE_END` | yes |
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
| About panel, first row | `The Apple II Plus Emulator` |
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
carrying **`APPLE2.O88` + `APPLE2.OVL` + `README.TXT` + `COPYING` + `WELCOME.BAS`
in ONE folder `APPLE2/`**.

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

**`WELCOME.BAS`** is an Applesoft listing written for this port that
demonstrates what this build actually renders - text, GR, HGR - **and nothing
it does not**, on CWORD's `WELCOME.RTF` rule.

### 16.3 The Makefile

`$(eval $(call CC_PACKAGE,apple2,apple2,APPLE2.OVL))` with the ROM part;
`APPLE2SRC` and `APPLE2INC` as **written** prerequisites naming every included
file; `build/apple2-rom/APPLE2.ROM` with **exactly ONE owner - the Makefile
rule** - because `build.sh` reads it and writing it from the stamp recipe
would re-make a downstream prerequisite behind make's back; the four disk
geometries each `--verify`'d; and the phony targets.

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
| `apps/apple2/build.sh` | the host checks, each of which **stops the build**, run through a stamp file that is a prerequisite of `apple2.raw.asm`: `getapple2rom.py --check`; `a2uitest`; `a2ref.py --check <explicit mode list>` + `--selftest` + `--lumcheck` + `--romshape`; `a2memtest.sh`; and the `a2_say()` literal walk **with an explicit expected MINIMUM**. **Every step FAILS rather than passing when its subject is absent** - wave 1 asks `--check` for text alone and wave 3 adds lores and hires, so a step whose subject does not exist yet fails instead of printing green |
| `apps/apple2/hosttest/os88.h` | the stub SDK - the same structs and constants as `apps/cc/os88.h`, only the prototypes the program calls, no `long`/`float` poison (the host needs `printf`), **plus every new thunk's prototype in the SAME edit that adds the thunk**. It is a second copy of an interface and it will drift; when it does the harness fails to COMPILE, which is the failure you want |
| `apps/apple2/hosttest/a2uitest.c` | the whole program against that stub, with a **PIXEL model of the glass**: `gfx_blit1` writes real pixels, `gfx_scroll` fills the vacated rows with **GARBAGE**, the clip is **enforced** (a pixel outside an armed region fails with its coordinates), and after every driven step it asserts pixel for pixel over the whole 320x192 that **the glass shows what the shadow says it shows**. **It drives the flash phase across a flip** and asserts that exactly the lines whose text rows hold `$40-$7F` bytes were forced. Prints the cost table in milliseconds. Compiled `-DA2_HOST`, which keeps the counters out of the shipping image. **Verify the stubs model what the machine does** - the C64's `blit1` stub REFUSED for a whole wave, so the cost table priced the fallback and nobody noticed |
| `tools/a2ref.py` | an **INDEPENDENT pixel-level reference compositor** in Python, written from AppleWin's `NTSC_CharSet.cpp`, apple2emu's `video.cpp` and MII's `mii_video.c` - **never from `a2band.inc`**. Also asserts the pinned ROM's measured shape (`--romshape`: 128 distinct 8-byte bitmaps, block `$00` = block `$80` XOR `$7F`). The harness compares **bit for bit**. This is the file that catches a composer whose transcription is correct and whose assembly is not. `--selftest` injects a one-bit defect and requires the compare to FAIL |
| `apps/apple2/hosttest/a2memtest.asm` + `.sh` | `a2mem.inc`'s and `a2band.inc`'s string loops on a real x86 with SS != DS and an ES sentinel, in raw QEMU, with **four negative controls** - one each for ES, DF, BP and DS. In `build.sh`, because it takes seconds. From the Disk II wave it also covers the segment arithmetic that reaches a track inside a claim larger than 64KB |
| `apps/apple2/hosttest/a2cputest.asm` + `.sh` | section 4.4's twelve rows. `make a2cputest`, minutes, **not** in `build.sh` |
| `tests/a2band/a2bandbench.asm` | the composers' icount bench on `tests/benchlib.inc`, `make a2bandbench`. Per CELL, per SOURCE BYTE and per CALL in microseconds for all three composers plus `rowspan`/`rowcopy`/`rowsig`/`band_x2` - and, from wave 5, **per foreign FRAME for each FSX writer against the windowed flush on the same change set**. **The tier table and the cost table are written from these numbers.** It arms the clip on its rerun callbacks, saves ES around every blit, and preflights `OSAPI_GFX_BLIT1` |
| `tests/apple2.py`, `tests/apple2part.py` | registered in `tests/suite.py`, or the fast tier's own registration row fails the build. `apple2part.py` is `c64part.py`'s shape: `APPLE2.ROM` is NOT a file on the disk; the package file is image + 14,848; `os88_part_seg(0)` is the segment the C put in the machine record; three windows of the ROM read out of the guest equal `build/apple2-rom/APPLE2.ROM` byte for byte; the RESET vector at `$FFFC` reads `$FA62`; **and the CHARGEN table and the reverse table exist after `os88_main` and BEFORE any wake** - the negative control for keeping them off the overlay |
| QEMU + QMP | `make test TESTAPPS=build/apple2.img`, `tools/mouse.py`, `tools/qmp.py sendkey`, `tools/shot.py --crop --zoom`. Every screendump assertion lives here |

**THE 1BPP PASS IS BINDING AND IS NOT OPTIONAL.**
`make test VIDEO=cga TESTAPPS=build/apple2.img` with
`tools/mouse.py --screen 640x200`, and `VIDEO=herc HERCSEG=0x7000` read with
`tools/hercshot.py build/qmp.sock 0x70000`. Look at a greyed menu item (grey
rounds to black on 1bpp, so it must be a checkerboard), at the About panel's
OK button, at the status row - **and at a MIXED screen and a screen that has
SCROLLED once**, because on CGA `dock_top` is 176 and only ~114 of the Apple's
192 scan lines fit, so the bottom anchor is the only thing putting MIXED's
text window and the `]` cursor on the glass. **QEMU double-scans CGA mode 6 to
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
uses fsx** (TANK is assembly).

| thunk | slot | shape |
|---|---|---|
| `os88_fsx_caps` | `OSAPI_FSX_CAPS` slot `0x02c0` | `int os88_fsx_caps(void *win, int *kind)` - out AX is the mask, DL the display's `VID_*` kind. **`kind` is an out-parameter and must therefore be a `static`** (SPEC.md 73's rule against `&local`) |
| `os88_fsx_run` | `OSAPI_FSX_RUN` slot `0x02c8` | `int os88_fsx_run(void (*entry)(void), void *win, int flags)` - the entry is a plain **resident** C function whose near offset goes in AX, and it **must never be an `ovl_`**: `tools/cc8086.py` refuses that address by name |
| `os88_fsx_mode` | `OSAPI_FSX_MODE` slot `0x02d0` | `int os88_fsx_mode(int id, void *fsi)` - the thunk does `push ds / pop es` so ES:DI is the caller's static `FSI_SIZE` block, and puts ES back |
| `os88_fsx_wait` | `OSAPI_FSX_WAIT` slot `0x02d8` | `int os88_fsx_wait(int kind)` |

Each returns 0, or -1 on CF.

**THE MEASURED COST TO EVERY OTHER C PACKAGE.** `nasm -f bin` has no dead-code
elimination, so a thunk nobody calls is still image. The C64 measured its
added thunks at **~18 bytes each** in CWORD's image, so four is **~72 bytes**
charged to CWORD, RUNCPM, C64, WEAVE and LOOM. **CWORD ships with 1,043 bytes
spare, so it fits - but it is MEASURED before and after on all five**, and the
`os88pkg` lines go here when wave 5 takes them:

| package | before | after |
|---|---|---|
| CWORD | measured in wave 5 | measured in wave 5 |
| RUNCPM | measured in wave 5 | measured in wave 5 |
| C64 | measured in wave 5 | measured in wave 5 |
| WEAVE | measured in wave 5 | measured in wave 5 |
| LOOM | measured in wave 5 | measured in wave 5 |

`make test-full` is the gate that proves the edit broke none of them.

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
| `tier` | from the **MEASURED** speed of wave 7's XT and 386 readings. A judgement, and the release skill asks for every tier touched to be flagged as reviewable in the PR |
| `flags` | `["new"]` - only `new` and `floppy_only` are manifest-settable |
| `files` | `APPLE2.O88`, `APPLE2.OVL`, `{name: README.TXT, source: apps/apple2/README.TXT}`, `{name: COPYING, source: apps/apple2/COPYING}`. `files[0]` must be exactly `<STEM>.O88` |
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

**`FILE_MAX` is 64,512 per file**, and the PLANNED `APPLE2.O88` is ~58,300
with its ROM inside. That headroom is why Integer BASIC is refused (section
10.4).

### 18.2 The rest of the change

| what | where |
|---|---|
| the package bytes | `public/wire/pkg/APPLE2.O88`, `APPLE2.OVL`, `README.TXT`, `COPYING` |
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
