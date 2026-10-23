# XTMP3 — the face (S4): contract notes for SPEC.md §121.8 and §121.9

These are the face track's notes for the integrator to fold into SPEC.md
§121.8 (the face) and §121.9 (menus and keys). Everything here is what the
code on `xtmp3/face` does and was looked at; the measured-on-hardware column
is empty because nothing in this wave was run on an XT.

The design is the owner's two mockups, `docs/xtmp3/design/xtmp3-color.png`
and `xtmp3-mono.png`. The faces as built are beside this file, and
`vs-mockup-color.png` / `vs-mockup-mono.png` put each mockup next to the VGA
face scaled by the mockup's own 1.6.

## Files

| file | holds |
|---|---|
| `apps/xtmp3/xtmp3.asm` | header (icon, `.MP3` association), entry, the commands and their ONE predicate (`x3_bok`), the queue, the file dialog, menus, keys, callbacks, data, bss |
| `apps/xtmp3/x3draw.inc` | the drawing layer: CDRack's `crdraw.inc` renamed `x3d_`, plus font descriptors, a two-colour dither, a 4bpp canvas and a Bresenham plotter |
| `apps/xtmp3/x3face.inc` | **the state record** (`X3S_*`), the four layouts as data, the deco interpreter, every element, the key painter, `x3f_update` |
| `apps/xtmp3/x3demo.inc` | the demo engine: S8's `x3eng.inc` replaces it and keeps every `x3_e_*` name |
| `tools/os88x3art.py` | every picture and font; `--selfcheck`, `--png DIR` previews; reads the masters with its own PNG decoder |
| `apps/xtmp3/art/{knob,vu,logo}.png` | the codex-made masters (unchanged; `PROMPT.md` has their prompts) |

## Layouts

Chosen at every full paint from `OSAPI_WM_GEOM` (largest that fits, unless
the user asked for the compact one); sized by an `OSAPI_WM_PREFER` table that
`x3_prefset` rewrites on every change of mind; switched with
`OSAPI_WM_RESIZE` (Options > Compact/Full Player, `C`).

| layout | content | where | what is on it |
|---|---|---|---|
| FULL | **512 x 352** | VGA (and VESA) | everything in the mockup; the queue three rows |
| MID | **512 x 248** | EGA 640x350, Hercules 720x348 (and VGA where the band is EGA's) | the same, with MID's art: 8-row title face, 27-row time digits, 14-row total, 84x40 dials, a 56x42 knob, 7-row screws, 12-row logo; the queue two rows |
| C1 | **440 x 88** | the CGA's 200 lines | the LCD (system-font title, 18-row time, 9-row total, a 4-row bar, the format), the five keys with 7-row icons and OPEN (eject icon + caption), the status line |
| C2 | **440 x 172** | Options > Compact Player on VGA, EGA and Hercules | C1's arrangement with FULL's art |

The frame adds 2 to the width and `TITLE_H + 1` = 19 to the height: FULL is a
514 x 371 window, MID 514 x 267, C1 442 x 107, C2 442 x 191.

## The engine boundary

**The face reads one record, `x3_st`, and nothing else of the engine's.**
The engine writes it and calls `x3f_update` (gfx lock held: any window
callback) or `x3f_poke` (no lock: an `OSAPI_WM_ONWAKE` handler). The face
writes only the user's two fields, `X3S_QWANT` and `X3S_VOL`, and calls
`x3_e_qual` / `x3_e_vol` after it does.

| offset | name | size | meaning |
|---|---|---|---|
| 0 | `X3S_STATE` | byte | `X3ST_*` below |
| 1 | `X3S_QUAL` | byte | `X3Q_*`: the rung RUNNING; `AUTO` = none chosen yet |
| 2 | `X3S_QWANT` | byte | `X3Q_*`: the user's override; `AUTO` = none. **Face writes** |
| 3 | `X3S_FLAGS` | byte | `X3SF_EST` 01h total is an estimate (`~`), `X3SF_NOTOT` 02h no total (`--:--`), `X3SF_DEVLIM` 04h the format is the device's limit (the LCD adds DEVICE), `X3SF_VOLOK` 08h the knob does something (clear: greyed, a dotted indicator) |
| 4 | `X3S_ELAP` | word | seconds played (or decoded) |
| 6 | `X3S_TOTAL` | word | the track's seconds |
| 8 | `X3S_RATE` | word | the OUTPUT rate, Hz |
| 10 | `X3S_CHANS` | byte | output channels 1/2; 0 = no output yet |
| 11 | `X3S_BITS` | byte | output bits 8/16 |
| 12 | `X3S_LEVEL` | byte | the BUFFER gauge 0..255: the PCM ring's fill live, conversion progress while decoding |
| 13 | `X3S_METL` | byte | left (or mono) meter 0..255 |
| 14 | `X3S_METR` | byte | right meter 0..255 |
| 15 | `X3S_PCT` | byte | conversion percentage 0..100 (the bar and `Decoding to disk NN%` read it in `X3ST_DECODE`) |
| 16 | `X3S_SPEED` | word | measured speed x 100 (31 = 0.31x) |
| 18 | `X3S_UNDER` | word | underruns |
| 20 | `X3S_PEAK` | word | peak memory, KB |
| 22 | `X3S_ERR` | word | the error's NUL text in the package's segment; 0 = none |
| 24 | `X3S_VOL` | byte | 0..10, the knob's detent. **Face writes** |
| 25 | `X3S_CUR` | byte | the queue entry the LCD shows; FFh none |
| 26 | `X3S_TITLE` | 41 | its title, NUL-ended (40 characters) |
| | `X3S_SIZE` | 68 | |

`X3ST_*`: 0 IDLE, 1 CHECK (Checking performance), 2 CHOOSE (Choosing output
drive), 3 DECODE (Decoding to disk NN%), 4 READY, 5 LIVE (Playing live), 6
FILE (Playing decoded file), 7 PAUSED, 8 BUFFER (Buffering), 9 DONE
(Finished), 10 ERROR (`Error: ` + `X3S_ERR`).

`X3Q_*`: 0 AUTO, 1 ORIGINAL, 2 HALF, 3 QUARTER, 4 XT STANDARD, 5 XT FAST, 6
XT TURBO, 7 XT SPEECH.

**The engine's procs** (x3demo.inc's header is their contract; each runs on
the UI task with the lock held, returns CF = 0, may clobber AX, CX, DX, SI,
DI): `x3_e_init`, `x3_e_tick` (every `X3_TICK` = 5 ticks), `x3_e_play` AL,
`x3_e_cue` AL (load without playing), `x3_e_pause`, `x3_e_resume`,
`x3_e_stop`, `x3_e_seek` AX, `x3_e_qual`, `x3_e_vol`, `x3_e_cancel`,
`x3_e_qadd` AL (an entry joined: the parser's turn to fill its title and
duration), `x3_e_qdrop` AL, `x3_e_qclear`.

**The queue** belongs to the face (`x3_q`, 32 entries of 60 bytes): the 8.3
name, its volume and folder cluster (`OSAPI_FILE_HERE` at the dialog's
answer, `OSAPI_ARG_FILE`'s pair at a launch), a duration word (FFFFh =
unknown, shown `--:--`), an estimate flag, and a 40-character title that is
the name's stem until the parser (S5) writes the tag's.

## Meters (SPEC.md 121.8's honesty)

- `X3S_CHANS` = 2: the dials are **L** and **R**, both VU (red zone).
- `X3S_CHANS` = 1: the left dial is **MONO** (`X3S_METL`); the right one is
  **BUFFER** (`X3S_LEVEL`) with its own scale, `0 50 100`, and its zone drawn
  GREEN at the full end - a full buffer is health, not an overload.
- Nothing loaded: both at rest.
- A needle is redrawn at most every `X3F_DIALT` = 5 ticks (3.6 a second);
  a full paint draws it whatever the time. Options > Meters: Off rests
  both needles and the dials stop being redrawn.
- Needle positions: 25, from -48 to +48 degrees about the pivot.

## Roles and themes

A primitive takes a ROLE; a theme is one byte per role. `80h + n` is a
`OSAPI_GFX_FILL_PAT` pattern (black and white only), `40h + n` a two-colour
dither composed as bands (`x3d_dtab`). A 1bpp display is always black and
white; the Theme item is greyed there.

| role | colour | black and white |
|---|---|---|
| `R_GND` the panel | light grey | white |
| `R_EDGE` rules | black | black |
| `R_HI` / `R_SH` bevel light / shade | white / dark grey | white / black |
| `R_LABEL` lettering, logo, screws, grilles | black | black |
| `R_LCD` / `R_LCDRIM` | black / dark grey | black / white |
| `R_LCDTTL` title line | light cyan | white |
| `R_LCDTIM` seven segments | light green | white |
| `R_LCDTOT` thin total | white | white |
| `R_LCDFMT` / `R_LCDRATE` | white / light cyan | white / white |
| `R_PBFRM` / `R_PBFG` / `R_PBBG` | white / light green on green 50% / black | white / 50% / black |
| `R_KEY` / `R_KEYINK` / `R_KEYDIS` | light grey / black / dark grey | white / black / black (picture dithered) |
| `R_KEYON` / `R_KEYONI` latched | black / light green | black / white |
| `R_BEZEL` dial bezels | dark grey | black |
| `R_QBOX` / `R_QTXT` / `R_QSEL` / `R_QSELT` | white / black / blue / white | white / black / black / white |
| `R_TROUGH` / `R_THUMB` | white / 50% | white / 50% |
| `R_STAT` / `R_STATTXT` | light grey / black | white / black |

The dial faces and the knob are not roles: the VU face is the master's cream
(yellow and white checker), its zone red (green on BUFFER), its marks black;
the knob is the master's four greys. Both have explicit 1bpp faces.

## Elements (each one keyed; a full paint forgets every key)

| element | key | drawn as |
|---|---|---|
| title line | hash of the composed line | one band (FULL/MID/C2 face) or one `OSAPI_FONT_RUN` (C1) |
| elapsed time | seconds (7FFFh: none) | one band, seven segments |
| total | seconds, bit 14 = estimate (7FFFh none) | one band, '/' and '~' pictures |
| progress bar | its lit length in pixels | ONLY the strip between the old length and the new (one dither rect, or one glass rect); the bar is the conversion in DECODE |
| format | rate/64, channels, 16-bit, DEVICE | two bands (the channel word; the rate and bits) |
| dial x2 | needle position, mode | colour: the mode's face (built once a paint) copied for the box the old and new needles share, the needle drawn, ONE `OSAPI_GFX_BLIT4`; black and white: those rows of the 1bpp face, one `OSAPI_GFX_BLIT1` |
| dial label x2 | the string | one band (L, R, MONO, BUFFER) |
| knob | detent, greyed | one 80x68 (FULL) / 80x52 (MID) canvas - panel, the knob's planes, the tick ring and the indicator - in ONE blit |
| queue row x3 / x2 | entry, selected, shown, the queue's generation | three rects and one `OSAPI_FONT_RUN` |
| scroll bar | top, entries | three rects |
| quality field | hash | one `OSAPI_FONT_RUN` |
| state field | hash | one `OSAPI_FONT_RUN` (or the advanced figures) |

The keys are `os88ui` records (`OS88UI_BOWN`, `OS88UI_OWN`, Play and Pause
`OS88UI_LATCH`): each key is ONE band - outline with cut corners, the shadow
dither along two sides (moved to the top-left while held), the picture and
the caption - and ONE blit in any state. `x3_bok` is the one predicate for a
key's greying, its menu item and its key.

**Identity** (incremental == full repaint, `ImageChops.difference(...).getbbox()`
is `None`) was checked on: FULL colour and black and white, C2 colour, a FULL
-> C2 -> FULL round trip (QEMU VGA); MID colour and black and white (QEMU
`VIDEO=ega`); MID (QEMU `VIDEO=herc HERCSEG=0x7000`, read with
`tools/hercshot.py`); C1 (QEMU `VIDEO=cga`). The full repaint was forced
with T twice, C twice, or the About card up and down.

## Menus and keys

| menu | items (a greyed item is `x3_bok`'s refusal) |
|---|---|
| File | Open... O / Add to Queue... / Remove from Queue / Clear Queue / Decoded Files... |
| Playback | Play/Pause Space / Stop S / Previous Track / Next Track / Rewind , / Fast Forward . / Decode First... D |
| Options | `Quality: <choice>` Q / `Theme: Color` or `Black and White` T / `Meters: On/Off` / `Compact Player` or `Full Player` C / `Status: Basic/Advanced` I |

There is no check mark: each item NAMES its state, and Quality cycles Auto,
Original, Half, Quarter, XT Standard, XT Fast, XT Turbo, XT Speech, Auto.
About is `OSAPI_ABOUT_SET`'s card; no Help menu.

| key | does | key | does |
|---|---|---|---|
| Space | play / pause | S | stop |
| Left / Right | previous / next track | `,` `.` | back / forward 10 s |
| Up / Down | queue selection | Enter | play the selection |
| `+` `=` `-` | volume | O | open |
| Q | cycle quality | D | decode first (greyed until S9) |
| T | theme (greyed on 1bpp) | C | compact / full (greyed where full does not fit) |
| I | status: advanced figures | Esc | cancel a conversion (live only while decoding) |

Mouse: a queue row selects, a second click on the selection plays it; the
scroll bar's arrows step a row and the trough a page; the wheel scrolls the
queue (the volume on the compact player); the knob's left half turns it
down a detent and its right half up; a click on the progress bar seeks.

**Greyed until their engine exists** (the predicate's `.no` rows, with this
reason in the source): Decode First and Decoded Files are S9's output-drive
dialog and conversion (SPEC.md 121.7). Esc is live only in `X3ST_DECODE`.

`.MP3` is DECLARED (the header's association block, SPEC.md 54.6), not
claimed at run time with `OSAPI_ASSOC_SET`: nothing else claims `.MP3`, and a
runtime claim is STICKY (54.5) - it would override a later choice of the
user's every time the player ran. A double-click on an `.MP3` opens XTMP3
with it (`OSAPI_ARG_FILE`), queued and played.

## The art (`tools/os88x3art.py`)

From the MASTERS (block centres to the logical grid, the sixteen colours):

- **Knob** (`knob.png`, 64x64 at 8x): the 56x56 square from (4,4). The
  groove at 12 o'clock is removed - its pixels refilled from two columns
  away on their own side (an even offset keeps the dither's phase) - since
  the indicator is a line drawn at the volume's angle. Two planes give the
  four greys; the black and white face is the master's greys AVERAGED over
  3x3 inside the rim and ordered-dithered, the rim solid. MID: rows sampled
  to 42.
- **VU dial** (`vu.png`, 84x56 at 6x): a black mask and a red mask; the cream
  is the master's checker, regenerated. The black and white face is the
  black mask plus the red zone hatched. MID: masks ORed down to 40 rows.
- **Logo** (`logo.png`, 96x20 at 8x): the ink's box, 92x16; MID ORed to 12.

GENERATED: the panel's 5x7 small caps (with `k` and `z` for `kHz`), the LCD
title face (the caps made bold and tall, 10 rows; MID 8), the dial's 3x5
scale digits, six seven-segment sets (`tf` 22x36, `tm` 22x27, `tc` 20x18 in
a trapezoid-segment style; `nf` 11x18, `nm` 11x14, `nc` 11x9 one-pixel
"thin"), the transport icons at 13 and 7 rows, screws, grilles, scroll
arrows, and the needle (25 ends), indicator (11) and tick-ring (11) line
tables. `--selfcheck` holds every digit set distinct with 8 the union and a
`1` inked on its rows, the masters' sizes, the groove gone, every line
inside its canvas and every scale mark on its face.

## Sizes (measured on this branch)

| | bytes |
|---|---|
| `XTMP3.O88` image | 26,496 |
| ...packed (lz4) | 16,671 |
| bss | 9,747 |
| art (`build/x3art.inc`, data) | 12,970 |
| ...the largest parts | digit sets 4,584; dials 3,192; knobs 2,082; fonts 1,616 |

bss: the state record 68, the queue 1,920, two colour dial faces 4,704, the
band/canvas 2,720, keys and the rest. No kernel byte.

## Where it ships (temporary)

On the **1.44MB apps disk only** (`APPS:` first in `$(APPSIMG)`'s recipe, not
in `$(APPS_TOOLS)`), and on the live media (`$(LIVEPKGARGS)`), because
`t_livefull` holds the live volume to the 1.44MB disk. S10 decides the
disks; `make KERN_SMALL=1` builds it. On kern_small `x3d_bput` retries a
refused off-grid `OSAPI_GFX_BLIT1` on the byte grid (the x & 7 pixels before
the field laid in its paper) - untested there, since kern_small shipping is
S10's call.

## Not yet seen, and why

- No XT number: the needle's box copy plus one BLIT4, and the knob's canvas,
  are the expensive paths on a 4.77 MHz VGA XT and want MartyPC's cycle
  counter in S10. A colour dial face is built once per mode per paint (a
  per-pixel compose of 4,704 pixels).
- 86Box was not used; Hercules was QEMU's software renderer read back with
  `tools/hercshot.py`, not a card.
- The wheel was driven on QEMU's PS/2 mouse (`MOUSEPORT=ps2`).
