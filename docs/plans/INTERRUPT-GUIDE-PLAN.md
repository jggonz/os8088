# The Interrupts page, and its second chapter

A PROPOSAL for the half not yet written (docs/README.md). Chapter 1 has shipped:
`tools/os88intguide.py` writes `build/interrupts.html`, an interactive page in
the boot ladder's hand (`tools/os88ladder.py`) covering the interrupt table,
how the 8088 takes an interrupt, every conventional vector, and which ones
os8088 and its DOS install. **Chapter 2, the BIOS data area and the disk path
through low memory, is planned and not started.** This file is that plan.

## What is there now

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
