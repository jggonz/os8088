# The Interrupts page, and its second chapter

**COMPLETED, 2026-11-07: both chapters have landed.** This is the design record
(docs/README.md), not a description of the page. The page is
`tools/os88intguide.py`'s output, `build/interrupts.html`. Chapter 1 covers the
interrupt table, how the 8088 takes an interrupt, every conventional vector, and
which ones os8088 and its DOS install. Chapter 2 covers the BIOS data area, the
0500h page and one floppy read. The generator's own header is the current
account. The stories below were the plan for chapter 2, and "As built" at the
end records how each one landed.

## What was there before chapter 2

- **The generator** - `tools/os88intguide.py`, with the page's stylesheet and
  script in `tools/intguide/`. On demand, like the ladder: nothing in `make`
  runs it, and it needs no emulator or build (well under a second).
- **The anchors.** Every sentence about os8088 stands on an `A(file, regex)`
  that must still match. `--selfcheck` runs all of them (57 at the time of
  writing), and the page is never written without that check passing. A
  failing anchor means re-read the code before re-pointing the anchor:
  evidence that a hook still exists is not proof that the prose describing it
  is still right.
- **The prose rule is the ladder's**, and it is checked: the reader knows
  nothing about this machine, terms are `[[marked]]` and defined in `GLOSS`
  without leaning on another marked term, and no page text names a source
  file, a symbol or a SPEC.md section (`BANNED`).
- **Room for chapter 2 is already on the page.** The bar across the top is
  0000h-05FFh to scale. The BIOS data area and the 0500h page are drawn
  HATCHED as chapter 2's, and they have region details and a planned rung
  (`low`) saying what is coming. The chapter switch in the rail lists both
  chapters. Nothing in `page.js` is chapter-specific: chapter 2 is rungs with
  `chapter="low"` and steps that are regions or fields.

## Chapter 2: the stories

1. **The BIOS data area, field by field.** Split the `bda` region into its
   fields (a `fields` list on the region: offset, length, name, text,
   anchors) and draw them as a magnified strip under the bar, the way the
   ladder magnifies the low end of memory. Each field gets a detail: what the
   firmware keeps there, who writes it, and what os8088 does with it. At
   least these, from the survey taken for chapter 1:
   - 0400h/0408h, the serial and printer port bases;
   - 0410h, the equipment word, whose display bits os8088 rewrites;
   - 0413h, memory size, which the DOS bracket patches and puts back;
   - 0417h/0418h, the shift flags, plus 041Ah/041Ch/041Eh, the keyboard ring,
     which the overflow guard rewinds;
   - 043Eh-0441h, the floppy's recalibrate/IRQ flag, motor status, motor-off
     count and last status;
   - 0449h-0466h, the video block, left to the ROM;
   - 046Ch/0470h, the tick count and midnight flag, used to time the boot;
   - 0472h, the warm-restart word;
   - 0467h, the reset vector kern_mega uses to come back from protected mode;
   - 0487h-0489h, EGA/VGA information, patched to colour.

   Verify: every field os8088 touches has an anchor; `--selfcheck` passes.
2. **What is saved and what is put back.** For each field, show which of the
   DOS bracket, the windowed DOS run, hibernation and shutdown save it, put
   it back, or leave it alone on purpose. This would be a colour-by switch
   like the table's three moments.
3. **The 0500h page.** Print Screen's status byte, DOS's and BASIC's uses,
   os8088's floppy-table copy at 0580h during boot, and (on kern_mega) the
   A20 test byte at 0500h.
4. **One floppy read, followed down.** A walk like chapter 1's key press:
   os8088's request, the run cut at the track and at the 64 KB DMA page, the
   EOT patched in the table that 1Eh points at, the firmware's int 13h
   programming the DMA chip and the controller, IRQ 6 setting bit 7 of
   043Eh, the firmware's wait loop seeing it, the status landing in 0441h,
   and the retry with a reset. Each step shows the low-memory bytes it
   touches. Mark derived timings as derived, and quote measured ones from
   PERFORMANCE.md with their set.
5. **Boot's own use of low memory.** The boot sector at 7C00h, the move to
   the top of RAM, the stack, and the first write to the table at 0580h.
   Link to the boot ladder rather than repeat it.

## Rules for whoever builds it

- Re-survey before writing. The field list above was true of the tree the
  chapter 1 survey read (2026-11-07); the anchors are what make it true of
  yours.
- No emulator in the generator. If a story needs a measured number, take it
  once, quote it with its date and its machine, and anchor what it describes.
- When chapter 2 lands, drop `planned` from its `CHAPTERS` entry and remove
  the hatching. Then move this file to `docs/plans/completed/` in the same
  commit, with its status line fixed.

## As built

Each story above landed. The details differ from the plan in these places:

- **Story 1, the fields.** They are `FIELDS` in the generator, not a `fields`
  list on the region. They tile 0400h-05FFh exactly, which the selfcheck
  enforces, and they are drawn as a byte map of 16 bytes a row. A field that
  runs past the end of a row is split at the row boundary.
- **Story 2, what is saved and what is put back.** This is the byte map's
  colour-by switch, with views for "os8088 uses", "After a DOS program" and
  "Windowed DOS". There is also a rung, *What is put back*, with one step for
  each of the full-screen run, the windowed run and hibernation. Hibernation
  is not a colouring, because the BIOS data area is never restored: on resume
  it is the new boot's. The rung says so.
- **Story 3, the 0500h page.** This is its own rung. DOS's floppy-table copy
  at 0522h is included as "many versions of DOS", from period references, not
  from this tree.
- **Story 4, one floppy read.** This is a 13-frame walk: cylinder 5 of a
  360 KB disk into 2E80:0000. The run is cut to 12 sectors at the 30000h
  boundary, and the walk shows the head flipping at sector 9. The firmware's
  side (the DMA mode 46h, READ DATA E6h, the timing table's values and
  `RET 2`) is the IBM PC's own. The one measured figure is the 400 ms a call.
  It comes from CLAUDE.md's 5150 table (PERFORMANCE.md) and is labelled
  measured. The 270 ms streaming figure is arithmetic on the rotation.
- **Story 5, boot's own use.** This is a six-note rung. It links to the boot
  ladder by name rather than repeat it.
- **Extra: The rules a disk call obeys.** This rung covers counting in calls,
  the 64 KB line, the cylinder rule for reads against the track rule for
  writes, retries, and the pointer moving during a call.

The walk renderer became generic for this: each box is text, bits, kv, list or
code, and unchanged boxes carry forward. Chapter 1's key-press walk was moved
onto the same shape. The anchor count went from 57 to 99.
