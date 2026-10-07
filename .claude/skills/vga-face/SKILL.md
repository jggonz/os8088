---
name: vga-face
description: Give an os8088 package a COLOUR FACE on VGA/EGA - fewer redraws first, then a neater layout, styled panes and bevelled, picture-faced buttons with their captions inside - while the Hercules and CGA faces stay exactly as they were. The method MIDIRack's (SPEC.md 105.9.4, 105.9.5) was built with, step by step. Manual invocation only.
argument-hint: "<package, e.g. apps/tracker>"
disable-model-invocation: true
---

# The colour face

A package on this machine is drawn for the 1bpp adapters first - black ink,
white paper - and on a VGA it looks the same. This skill gives one a second
face for a **4bpp display** without touching the first: colour where colour
carries meaning, a layout cut on the card's own grid, and buttons whose
captions sit inside them. **And it starts with the redraws, not the paint**:
a face that is twice as pretty and repaints twice as much is a regression on
the machine this OS is for (CLAUDE.md, *Performance*).

The worked example is MIDIRack: SPEC.md §105.9.4 (the caches), §105.9.5 (the
colour face), §13.8.10 (the library hook it needed), `apps/midirack/mrui.inc`
(all of it), `tests/mrdraw.py` (the instrument) and `tools/os88midart.py`
(the button pictures). Read those before writing a line - this skill is the
order they were done in and why, and the code is the reference.

**Read [LESSONS.md](LESSONS.md) first.** Every entry cost a rebuild or a
wrong picture during the MIDIRack work, and most of them look like a bug in
the package when they are a bug in the method.

## Step 0 - scope, and the pictures to keep

- `$ARGUMENTS` names the package. Read its SPEC.md section and the window
  code end to end - layout, painter, every caller of a repaint. Check
  docs/INDEX.md for what the library already gives (`os88ui.inc`'s buttons,
  check boxes, radio groups, group boxes, scroll bar, About card).
- **Take the baseline on all three adapters** before changing anything: VGA
  (`os8088_xt_vga`), Hercules (`os8088_5150_herc_*`), CGA
  (`os8088_5150_cga_gla`), through MartyPC's `fbuf` (see Step 6's script
  shape). The mono pictures are the contract: at the end they must be
  identical except where you meant them not to be, and you will need the
  "before" to prove it.
- Work in a worktree of your own (`git worktree add`), never the main
  checkout - several sessions write there (docs/UPSTREAM.md, the memory).

## Step 1 - measure the redraws (before touching the paint)

PERFORMANCE.md's first sentence: a redraw is priced by the PRIMITIVE CALLS it
makes. So count them, per gesture:

- Copy `tests/mrdraw.py` to `tests/<pkg>draw.py`. It arms `os88marty.bp_trace`
  on the API table's DRAWING CELLS themselves (`KERNEL_SEG:0x38` fill, `0x40`
  frame, `0x1E5` font_run, `0x39D` icon, `0x3A4` BLITP, ...) - so the
  kernel's own painting (menus, the title bar) is never counted - and for
  each hit reads SS:SP and the far return address: **which task** made the
  call (a worker's stack is not the UI task's) and **which package routine**
  (the nearest symbol below the return IP, from `tests/mrprobe.py`-style
  re-assembly).
- Drive the gestures a user makes: play, pause, a selection move, a skip, a
  toggle, a card up and down - by KEY where the package has keys, so the
  menu's own painting stays out of the count.
- **Write the numbers down.** MIDIRack's first table read Pause 197 calls, of
  which 152 were the button group drawn twice; Next 229; Stop-when-stopped
  178. The routine names point straight at the waste.

## Step 2 - draw what changed (the caches)

The pattern (§105.9.4), one cache per kind of thing on the glass:

| on the glass | the cache | MIDIRack's |
|---|---|---|
| any string that can change | a FIELD SLOT holding its cells as drawn; a redraw draws one `font_run` of the run from the first differing cell to the last | `mru_field`, `mru_fowe` |
| list rows | a KEY per row (entry, highlight, mark); an add/remove/clear owes them all | `mru_list_sync`, `mru_list_owe` |
| a scroll bar | its three numbers | `mru_sbk` |
| buttons | each one's flags as drawn | `mru_btn_sync`, `mru_bdr` |
| check boxes | each one's state (and tell the cache when the library toggled it) | `mru_ck_sync`, `mru_ckd` |
| bars, meters | the drawn length; a change is ONE fill of the strip between | `mru_lw`, `mru_rack_bar` |

- One routine, `<pkg>_update`, walks the whole window through the caches.
  Every command calls it instead of a repaint. W_PAINT owes every cache and
  is otherwise unchanged.
- **A card going UP draws the card alone**; going DOWN is the one command
  that repaints - and so is a card replacing another (LESSONS.md).
- **THE IDENTITY ASSERTION is what makes the caches trustworthy**: after a
  run of gestures, capture the content, force a full repaint (a card up and
  down), capture again - identical to the pixel. Put it in the draw test
  with per-gesture CEILINGS a repaint would blow through, register it as a
  soak row, and run it on Hercules AND VGA.
- Then re-measure. MIDIRack: 1,177 calls across the gestures became 86.

## Step 3 - when to draw the colour face

- **Ask the DISPLAY the window is on, at each full paint**:
  `OSAPI_WM_DISPLAY` (BX = the window) answers DH = bits per pixel; > 1 is
  colour (§39.16.4; it answers the more restrictive display for a straddle).
  Not `OSAPI_VIDEO` - that is the primary's. Bank the answer in a byte the
  paint sets and every later draw reads (`mru_col`), so a worker draw and a
  W_PAINT never disagree mid-flight.
- **Gate it on the content box too**, and compute the gate from the
  narrowest real case. MIDIRack's needs 600 x 270: a VGA's window is
  624 x 311, an **EGA's ~624 x 279** (350 lines less the 20-line menu bar,
  the dock and the title). Below the gate, a colour display gets the mono
  face - which is a complete face.
- The compact (CGA) layout is never the colour face.

## Step 4 - the layout

- **Fixed columns where the mono face uses shares**, sized from what must be
  WHOLE: an 8.3 name (12 cells + mark + scroll bar), the longest label, the
  longest value. MIDIRack: playlist 140, Now Playing 248, the rack the rest.
- **Put anything you will BLITP on the 8-pixel grid.** `OSAPI_GFX_BLITP`
  takes x on a multiple of 8, and a window's content origin already is one
  (§11.94) - so it is your own inset that has to be. MIDIRack's transport
  starts at 152 and steps 48.
- **Anchor rows bottom-up** so the panes' last rows line up across the
  window (Load Directory with Eject/Loop, Add/Remove with the transport's
  bottom edge), and let the tallest flexible element (the display, the list)
  take what is left.
- Derive pitches (line spacing) from the space left, clamped (10..14), and
  centre what remains - then check the arithmetic at the EGA height.

## Step 5 - the vocabulary, and what each costs

Content is the application's (§76): white paper stays, because the kernel
white-fills before W_PAINT (there is `OSAPI_WM_OWNBG` to opt out, but every
pixel you then own is yours on every expose). Spend colour on meaning:

- **Panes**: a `CDGRAY` frame, a `CLGRAY` title strip ruled off under it,
  the title black on the strip. Four calls a pane, full paint only.
- **A display** (a now-playing readout, a status LCD): one black fill, text
  `CLGREEN`/`CGREEN`/`CYELLOW` on black through the field slots with their
  own pair.
- **Meters**: zones (green to 5/8, yellow to 7/8, red) over black in a grey
  frame; growing is one fill per zone crossed, of the change alone.
- **Highlight**: `CBLUE` with white text; a "current" mark green.
- **Every value the caches draw takes the face's pair** (`[mru_pair]`), so
  the incremental draw and the full paint use the same colours by
  construction.
- Full-paint decoration may write a pixel twice (strip then title); an
  INCREMENTAL draw never may (CLAUDE.md, *Nothing writes a pixel twice*).

## Step 6 - the buttons

- **Keep the library's gesture.** `%define OS88UI_BOWN` and set
  `OS88UI_OWN` in a button's flags (§13.8.10): `os88ui_btn` then calls the
  label entry as YOUR painter (AX index, BX rect, DI flags with DOWN/DIS
  resolved), so press, drag-off and release redraw your face, not the
  standard one. Point the record's labels array at the painter only on the
  colour face (`mru_recmain`); the mono face keeps its standard buttons.
- **A bevelled text button**: black frame; white top/left and grey
  bottom/right lines (pressed: one grey line top/left, caption +1,+1;
  greyed: grey caption); the caption opaque on `CLGRAY` with the face
  RINGED round it in up to four fills - no pixel twice (`mru_bevel`).
- **Picture faces with the caption inside** (the transport): draw them on
  the host - `tools/os88midart.py` is the template: shapes with an
  automatic outline and bevel, labels set from `faces/helv.t88` **by the
  ink** with a 1-pixel gap (the face's EVEN advances read "Pa use" at this
  size), PLANAR output for `OSAPI_GFX_BLITP` (one call and a few ms a face on
  the 8088, against ~30 ms for a packed `BLIT4`), up/pressed/greyed states,
  `--png` to look, `--selfcheck` and `--check-asm` (the package's MRA_*
  numbers against the art) in the fast tier.
- **Where the pictures live**: a lazy part (`OP_ASSET | OP_LAZY`, §20.12)
  is the OS's standard - UNLESS the image is compressed: `os88pkg.py`
  refuses `--compress` beside parts. Then a SIDECAR beside the package,
  LZ-wrapped, read at entry only when the primary is colour, freed at close
  and on a failed entry. Either way the fallback (no file, short heap,
  BLITP refusing a straddle) is the bevelled caption - a complete face.
- Check every disk the package rides for the extra clusters (§24.6.1); a
  disk left with one spare cluster is not a fit.

## Step 7 - verify, on every face

- The draw test's ceilings and identity on Hercules and VGA; the package's
  existing soak rows; `make` (fast) and `make test-full`.
- **Look**: VGA, Hercules and CGA screenshots through MartyPC's `fbuf` -
  compare the mono ones with Step 0's; on VGA crop and zoom the buttons in
  every state (latched, pressed, greyed). Scroll every dialog on CGA: a
  dialog that fits on paper can sit under the dock.
- EGA has no MartyPC profile: say so, and show the layout arithmetic at its
  height instead of claiming it was seen.
- Show the user the VGA face before calling it done; "neat" is theirs to
  judge.

## Step 8 - write it down, then the PR

- The package's SPEC section gets the caches (with the before/after call
  table and the identity assertion), the colour face (the gate, the layout's
  reasons, the vocabulary, where the pictures live and why) - before the
  merge, not after.
- A new library flag gets its own SPEC subsection and `tools/os88index.py`'s
  description of the include; regenerate docs/INDEX.md.
- New rows in `tests/suite.py`; new targets in the Makefile with a comment;
  CONTRIBUTING-style commit messages that say what was measured.
- A PR from the worktree's branch with the numbers in its body.
