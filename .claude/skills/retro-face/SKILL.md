---
name: retro-face
description: Build an os8088 package whose UI is a styled piece of HARDWARE from the user's mockups - an 80s rack unit, a calculator, a radio, a synth panel - the way apps/cdrack (CDRack, SPEC.md 119.6) was built. Host-generated art and fonts (seven-segment digits, small caps, logo, icons) instead of hand-drawn bitmaps, a colour theme and a black-and-white one as ROLE tables, every field composed into one band and put down in one OSAPI_GFX_BLIT1, cached element keys so only what changed is drawn, and full / mid / compact layouts picked from the window's content box so VGA, EGA, Hercules and CGA each get a face. Use when the user hands over mockups or asks for an app (or a redesign of one) that should LOOK like a device rather than a desktop dialog. For adding a colour face to an existing package while keeping its mono face as-is, use vga-face instead.
argument-hint: "<package or new app name> [mockup paths]"
---

# A retro hardware face

CDRack is the worked example: `apps/cdrack/` (three files), `tools/os88cdart.py`
(the art), SPEC.md §119.6 (the contract), `docs/cdrack/` (the mockups and the
faces as built) and the "Wave 4 — CDRack" part of `docs/HOW-CDROM-WAS-BUILT.md`
(what happened, in order). **Read [LESSONS.md](LESSONS.md) first**: every
entry cost a rebuild or a wrong picture.

What makes such a face cheap on a 4.77 MHz machine is that it is drawn as
FIELDS, not pixels. A primitive call costs ~756 us whatever it covers
(CLAUDE.md, *Performance*), so a button, an LCD number or a caption is one
composed band and one blit. A redraw touches only the fields whose state
moved.

## Step 0 - intake

- **Keep the mockups in the repo** (`docs/<app>/design/`, resized to ~1024 wide
  and quantised; CDRack's three are ~560 KB). They are the reference every
  later look is judged against, and the PR reviewer needs them.
- **List what the mockups ask for in the user's words**, and separate LOOK
  (panels, LCD, icons) from BEHAVIOUR (toggles, a program, a seek). The
  behaviour behind a pictured control is part of the job: CDRack's Shuffle,
  Program and Repeat all had to work, not just draw.
- **Map the OS first** (docs/INDEX.md). Mockups invent things the OS already
  has an answer to: a Help menu where `OSAPI_ABOUT_SET` is the convention, a
  check mark the menu cannot draw. Note each one and how the OS answers it -
  that note goes in the SPEC and the PR.
- Work in a worktree under `~/Repos/os8088-<name>` (CLAUDE.local.md), on the
  PR branch if the app already has one.

## Step 1 - the contract first

Write the package's SPEC section before the code (CLAUDE.md: SPEC.md is
updated BEFORE the change). CDRack's §119.6 has seven parts, and they are the
checklist:

1. the door / the data it shows, and what a missing capability displays;
2. **layouts and themes**: a table of the layouts (content size, which adapter,
   what is on it), the selection rule, the default theme and the theme
   colours per role;
3. **drawing**: roles, the band, the art generator and its selfcheck;
4. **the element cache** and the buttons;
5. behaviour (here: the play order);
6. **menus and keys** - every key in a table;
7. where it ships and what it costs (image, packed, bss, kernel bytes).

## Step 2 - the art is generated (`tools/os88<app>art.py`)

Copy `tools/os88cdart.py` and keep its shape:

- Every picture is **code or ASCII art in the script**: seven-segment digits
  from polygons, a 5x7 small-caps dict, logo letters as rows of `#`, icons as
  draw functions. No image file is read and no service is asked, so `make`
  needs nothing but python3 and the output is byte-identical every run.
- Emit an asm include: `cdart_<name>: dw width; db height, stride; rows`, a
  table per digit set, the caps index (`cdart_caps_ix`, ASCII 32..90) and
  glyphs (7 bytes each, bits 7..3), and **equs for every size the package's
  layout arithmetic uses** (`CDART_DL_W/H/CW`), so the asm cannot drift from
  the art.
- Make **one size of every icon per row scale**: 13 rows for VGA/EGA/Hercules
  and 7 for the CGA's 2.4:1 pixels (`rows_every`, ORing the rows it merges so
  a 1-pixel stroke never vanishes).
- Digit sets: `mitre` segments look right at 30 rows; at 16 rows and below
  use `block` segments (full-height verticals, horizontals between). Generate
  the doubled sets natively; do not scale up a small set.
- `--selfcheck`: every digit distinct, `8` the union, a `1` inked on every row
  but its three strokes', sizes as declared. `--png DIR` writes previews -
  **look at them zoomed before writing any asm**.
- The Makefile rule runs `--selfcheck`, then `--out $(BUILD)/<app>art.inc`, and
  the package is assembled with `-I $(BUILD)/`.

## Step 3 - the drawing layer (copy `apps/cdrack/crdraw.inc`)

It is generic apart from its prefix. Rename `cr_` to your prefix, and check
the prefix against the generated include's names (LESSONS). What it gives:

| routine | does |
|---|---|
| `cr_origin` | banks the content origin and makes the button rects absolute - at the top of EVERY pass that draws or hit-tests, because a moved window does not paint |
| `cr_rect` / `cr_frame` / `cr_hl` / `cr_vl` | fills and rules by ROLE (SI); a role byte is a colour, or 80h + a pattern for `OSAPI_GFX_FILL_PAT` dithers |
| `cr_text` | a system-font run (`OSAPI_FONT_RUN`), ink and ground by role |
| `cr_bnew` / `cr_bpic` / `cr_bcaps` / `cr_bdigs` | a clear band the size of the FIELD, then pictures, small caps and LCD digits ORed in at any x |
| `cr_bout` | the band put down in one `OSAPI_GFX_BLIT1` under `OSAPI_GFX_BLIT1_PEN`; inverts it on a 1bpp display when the ink is black, dithers it for a greyed control, shifts it for an x off the byte grid (§5.4.2.8) |
| `cr_capsat` / `cr_fcaps` / `cr_picat` | a static label, a fixed-width (or centred) caps field, a picture - each one band, one blit |
| the cut flag | every primitive asks `OSAPI_WM_CLIP_TEST`; a field the clip cut is forgotten and drawn again next pass |

**Themes are role tables**: `cr_tcol` and `cr_tbw`, one byte per `R_*`, with
an `%if` that both are exactly `CR_NROLES` long. The black-and-white table
uses only 0, 15 and patterns. A 1bpp display always gets black and white, and
the menu item that chooses colour is greyed there.

## Step 4 - layouts, static parts and elements (`crface.inc`'s shape)

- **A layout is a record** (`L_*` offsets): its content size, a static
  `L_DECO` proc, an `L_ELEMS` list, button rects (`CR_PARK` = 7FF0h for a
  button it lacks), per-button pictures and captions, and the coordinates the
  shared elements read. Assert the record size with `%if`.
- **Pick the layout at every full paint from `OSAPI_WM_GEOM`**, largest that
  fits, unless the user asked for the small one. Size the window with an
  `OSAPI_WM_PREFER` table you rewrite on every change of mind, and switch
  sizes with `OSAPI_WM_RESIZE` from a menu, key or button. Measured content
  budgets (frame = content + 2 wide, + `TITLE_H` + 1 tall): VGA ~638 x 416,
  EGA and Hercules ~285 rows, CGA ~136 rows. CDRack: FULL 608 x 352, MID 608 x
  232, COMPACT 440 x 84 (and 440 x 168 doubled).
- **`OSAPI_WM_OWNBG`**: the paint lays the ground itself (`cr_ground`), so a
  dark face never flashes white first.
- **Static parts** (panels, rules, labels, logo, grille) are drawn by
  `L_DECO` at a full paint only. A full paint may write a pixel twice; an
  incremental draw may not.
- **Everything that changes is an element**: a proc that computes a 16-bit
  KEY for everything it would draw, calls `cr_ek` (CF = 1: already on the
  glass), draws, then `cr_ekdone`. The paint calls `cr_forget` and then the
  same `cr_update`, so a full paint and an incremental one are one code path.
  A key must never be FFFFh (that means "forgotten"). A bar draws only the
  strip between its old length (the slot's old key) and its new one.
- **Buttons**: os88ui's record with `%define OS88UI_BOWN` and `OS88UI_OWN` in
  every flag word. `cr_bpaint` draws one frame and one band (the picture, the
  caption, the face as paper): two calls in any state. Latched controls use
  `OS88UI_LATCH`. A toggle that keeps its face and shows state in its box uses
  a private high flag bit (`CRF_ON` = 100h). ONE predicate (`cr_bok`) greys a
  button, its menu item and its key (§47 rule 2).
- **Hit-tests** for non-button areas (a grid, list rows, a scroll bar, a
  slider, a seek bar) live in the chained `W_ONCLICK` after the library's
  record; a slider drag is your own flag checked first in `W_ONDRAG` and
  `W_ONMOUSEUP`.

## Step 5 - menus, keys, About

- Menus through `OS88_MENUSET`. **No check marks**: a toggle item NAMES its
  state ("Repeat: On"), by pointing its slot at one of two strings. Greying is
  `db MENU_DIS` laid in front of the string and the slot pointed one byte
  earlier. Refresh the slots from `cr_update` (`cr_menulive`); the kernel
  reads them when a menu opens.
- About is `OSAPI_ABOUT_SET` plus os88ui's card (`BT_N = 0` while it is up);
  no Help menu.
- Every key the face has goes in a table (`cr_keytab`) and in the SPEC.

## Step 6 - look, on every adapter, and prove the cache

Per wave: `make` (fast tier), `make KERN_SMALL=1` if the package ships there,
and boots to LOOK. Build knob trees once per adapter, in their own dirs:

```
make BUILD=build-cga  VIDEO=cga  <your disk target>
make BUILD=build-ega  VIDEO=ega  <your disk target>
```

Drive them in QEMU with `tools/mouse.py --screen 640x200` (CGA) or `640x350`
(EGA). QEMU cannot display Hercules at all (black screen;
docs/HERCULES-TESTING.md) - MartyPC can, if your package runs on an 8088.

- **Crop and zoom** every face (`tools/shot.py --crop --zoom 3`): the first
  look at CDRack showed missing first letters only zoomed.
- **The identity check**: make changes through the face (pick, toggle,
  scroll), capture, force a full repaint (About up and down), capture again,
  and diff the window region with PIL - `ImageChops.difference(a, b).getbbox()`
  must be `None`. Do the same across a layout round trip (compact, then full).
- Save the finished faces in `docs/<app>/` and link them from the diary.
- When the emulator lacks a capability (QEMU's CD drive plays no audio), make
  the face show the data it does have with the controls greyed, so the face
  can still be judged - and say in the PR what was not seen.

## Step 7 - write it down, then the PR

- The SPEC section (Step 1), finished with the measured sizes.
- The guide: an `entries.toml` line and a page in `tools/guide/manuals/`, then
  `python3 tools/guidelock.py stamp KEY --sources apps/<app> --why "..."`.
- `tests/btnsites.txt`: the FILE that calls `os88ui_btn` must also write the
  record (`BT_RECTS`, `BT_N`) - keep `<pfx>_regrey` in the main `.asm`.
- `python3 tools/os88index.py` to regenerate docs/INDEX.md.
- The plan's story list and the "How it was built" diary with the pictures.
- Soak rows and `test-full` ONCE, after the last batch (CLAUDE.md, *Testing*).
- Commit by NAMED paths (86Box rewrites its `vm/*/86box.cfg`; never sweep it).
