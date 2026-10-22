# retro-face - LESSONS

What CDRack (SPEC.md 119.6, PR #297) learned. Each entry: the trap, what it
looked like, and what to do instead.

## Drawing

- **An off-grid BLIT1 skips the band's first `x & 7` pixels** (SPEC.md
  5.4.2.8). The band's first byte is the byte column HOLDING x. Composed from
  x itself, every label at an odd x lost its first letter: `DRack`, `RACK`,
  `IGITAL AUDIO`. Shift the band right by `x & 7` into a slack byte per row
  before the blit (`cr_bshift`), and remember the content origin is snapped to
  8 but your own inset is not.
- **The pen is not read on a 1bpp display** - a set bit is lit, which is
  white. Black ink on white paper is the band inverted before the blit. On a
  colour display in the black-and-white theme the pen does the work, so only
  a real CGA/Hercules boot exercises the inversion.
- **Grey on 1bpp is a dither**, not a colour: AND the band's ink with a
  checkerboard. A colour theme uses a dim ink instead.
- **A field band is the FIELD's width**, not the text's: a shorter value then
  erases a longer one in the same call, with no fill first.
- **`[bx+bp]` is not an 8086 addressing mode**, and `[bp+x]` addresses SS. Index
  per-button tables with SI or DI.
- **`mul` writes DX** - push the row count or the y around it.
- **`-w+error` turns an out-of-range `jmp short` into an error**, and 8086
  conditional jumps are short only. In a long proc, branch to a near `jmp`.

## The art

- **Small seven-segment digits collapse.** At 16 rows with a 3-pixel stroke, a
  mitred vertical segment has no straight part: the compact LCD's digits were
  dots and dashes. Use `block` segments below ~20 rows. The selfcheck now
  fails a set whose `1` is not inked on every row but its three strokes' -
  the check that would have caught it.
- **Generate doubled sets natively** rather than scaling a small one up: a
  scaled 16-row set has 6-row-thick horizontals.
- **Look at the previews zoomed** (`--png`) before writing asm round them.

## Names and gates

- **A proc and a bss byte with the same name** (`cr_going` the predicate,
  `cr_going` the flag) fail as "label inconsistently redefined" far from
  either definition. Name state nouns and predicates differently.
- **Check your prefix against the generated include's labels** (`cdart_`
  vs `cr_`): MIDIRack's `mrt_` collided with a generated table's (vga-face).
- **`tests/btnsites.txt` is per FILE**: the file that calls `os88ui_btn` must
  also write `BT_RECTS` and `BT_N`. Moving the greying loop into the include
  failed `btnrules` with three findings; keep it in the main `.asm`.
- **A rename touches** the Makefile (rules, `SYSOPTIONAL`, autostart links),
  `tests/suite.py` (`wants=`), the test that reads the package's bss by map
  (symbol names, window title, include paths), `tools/guide/entries.toml`,
  `manuals.lock.json` (key and `sources`), the manual's filename, driver
  comments and SPEC cross-references. Stale system images in `build/` make
  `guidelock check` report the old name until a full `make`.
- **Read bss offsets from the map**, not literals: the CD test read the .CDA
  buffer at the old player's +222.

## The OS's answers to the mockups

- **The menu has no check mark** (§12.2): name the state in the item.
- **No Help menu**: About is the application menu's (`OSAPI_ABOUT_SET`).
- **Callbacks give SI back**: `W_ONDRAG` and the menu handler both broke it on
  the first draft by calling a redraw that clobbers SI.

## Looking

- **QEMU cannot show Hercules** - a black screen, not an error. EGA (`VIDEO=ega`,
  640x350) and CGA (`VIDEO=cga`, 640x200) both work; pass `--screen` to
  `tools/mouse.py` or every click lands somewhere else.
- **Menu items are 16 px apart** from y = 28 on the 640-wide screens: one row off
  picks the neighbouring item (Compact Player instead of Theme).
- **86Box cannot be keyed from this host** (osascript keystrokes are refused).
  Resetting a `vm/*/86box.cfg` it rewrote made the AT BIOS stop in setup,
  which only the user can clear. Leave the rewritten cfg alone during a
  session; launch, capture with `screencapture -l <window id>` and let an
  `\AUTOSTRT` link drive the machine.
- **When the emulator lacks the capability** (QEMU's ATAPI drive plays no
  audio), show the data it has with the controls greyed, so the face can be
  judged - and name what was not seen in the PR.
