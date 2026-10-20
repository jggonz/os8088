# STICKY-APPS-NEXT: what the Sticky Apps size pass left for the next round

**PLAN, nothing here is built on `elendilon`.** Written 2026-10-17 at the end
of the `main-size-pass` round: main's Sticky Note and Desk Clock were merged
into elendilon, size-passed, and the shared library went into `DESK.DRV`
(SPEC.md §110.15). Every figure below was measured on that round's builds. Each
is an option the owner has seen and deferred, not one that was refused.

## 1. Desk Clock: one Settings layout, scaled by aspect

**Built and pushed, not merged:** branch `deskclock-onelayout`, commit
`5f000e4d`, on top of `c2ed61df`.

- **What it is.** `dks_tall` and `dks_mid` go. `dks_one` is COMPACT's table
  with every y and height a word: the rows on CGA, plus the rows gained per
  CGA height on a screen with a taller aspect. `dk_slay` turns those into
  rows once, at the first open. The previews are sized from the room each
  screen has, capped at a 64-px dial. The 8-px text and 12-px controls never
  stretch; only the gaps do.

- **What it looks like:**

  | adapter | dial / digital preview | content rows |
  |---|---|---|
  | VGA | 64 / 96 px (today's TALL sizes) | 338, about 50 rows shorter than TALL |
  | Hercules | 58 / 86 | 233, against MID's 274 |
  | CGA | 38 / 56 | 149 |

  VGA loses the "Hide date, alarms and desktop label." subtitle and the
  "Choose a face" heading. The owner: *"I can barely tell the difference."*

- **What it buys:**
  - image −192 (−184 before the paragraph pad), and **packed file only −67**
    (shipped build); small build −192 and −75;
  - bss +0;
  - the hot path unchanged (`os88rdiff --hot`, 70 routines).

  The tables it removes compressed well and the scaling code does not.

- **Why deferred:** it is a look change for 67 bytes of disk. Take it when a
  disk needs those bytes, or when Settings is next reworked anyway.

**Measurement method.** The screenshots and the per-option figures came from
scratch MartyPC sessions (the `dkshots.py` script, not kept in the tree).
Re-take them on the tree it lands on.

## 2. Desk Clock: other options priced and not taken

| option | measured or estimated | why not now |
|---|---|---|
| merge the alarm window into Settings | measured: image +18 / packed −17 (controls in all layouts), or −29 / +25 (one expanded entry) | about zero bytes. A usability choice, not a size one |
| `GFXE_NOBOLD` | −96 image (measured on the shared include's pass) | unasked; the bold text is the panel's look |
| collapse the form plumbing (`[dk_fm]`, per-window records) now the forms are fewer | estimate −40 to −80 | not built |
| 12-byte layout entries to 9-10 | estimate −120 to −180 | about 40 field accesses to touch |
| the settings MID layout on Hercules forced to COMPACT, or COMPACT everywhere | −175 / −401 image | REFUSED on the glass: the previews overflow their tiles (tile heights are CGA rows). §1 is the shape that works |

## 3. Sticky Note

- **The in-place tick.** A one-sector `OSAPI_FILE_WRITE_AT` at offset 0, with
  the full rewrite kept behind it for a compressed note.
  docs/plans/completed/STIKNOTE-NOTEPAD-PLAN.md §3.5 left it to the 5150's
  tick time, which `tests/stiknote.py` prints.
- **The owner's no's** (do not reopen): a 256-byte head, greyed items without
  their reason, and the MKDIR fallback.

## 4. Found on the way, outside the Sticky Apps

- **`OSAPI_PKG_START`'s document arm takes the document's folder from where
  the CALLER stands**, not from the locator's directory word. SPEC.md §21.5.3
  describes the word as authoritative. Sticky Note works around it by
  standing in `NOTES` before the launch (`sn_launch`). Either the kernel or
  the spec is wrong.
- **`tools/os88modcost.py` refuses to report** (`ovl attributed 3446 of 5118`)
  on this tree. Nobody checked whether it already did before the round.
