#!/usr/bin/env python3
"""Can a DOS program drive the HARDWARE in the box? (SPEC.md 96.18)

Every other DOS row here asks a question about INT 21h, which is OUR code
answering our own program.  This one asks about the machine underneath: a
program inside the fsx bracket hooks a real interrupt vector, unmasks a real
line at a real 8259, and runs a real transfer on the same 8237 os8088's own
floppy uses.

IT IS THE ROW THAT SAYS WHAT THE DOS BOX IS FOR.  A box that can only host
programs which poll can host almost no DOS software worth running - every
sound card, every comms program and every mouse driver in the era is built on
a hardware interrupt - so "the bracket hands the machine over" is a claim that
needs measuring rather than asserting, and this is where it is measured.

WHY THE SOUND BLASTER: its DSP command 0F2h raises the card's IRQ with no DMA
and no buffer, which makes it the cheapest hardware interrupt on the machine
to ask for - one write, and the line either fires or it does not.  Phase 2
then does what only a transfer can prove.

THE FOUR THINGS THAT FAIL SEPARATELY, which is why the program reports numbers
and this reads them rather than looking for one word:

  DSP    the card answered its reset handshake and its version query, so the
         port path works BOTH ways.  Without this a zero interrupt count means
         nothing: a card that never answered was never asked.
  MASK   the 8259's IMR as the BRACKET handed it over, expected to have IRQ7
         masked.  A bracket that handed over a mask the program could not
         change would fail at the count with no clue why; this names it.
  IRQ    the interrupt count for a ONE-SHOT request, so the answer is exactly
         1.  Not "at least 1": a 2 would mean the line is being re-raised,
         which on a shared XT IR7 is the spurious-interrupt case.
  DMA    the completion interrupt from a 256-byte single-cycle transfer, also
         exactly 1 - and the number that could not be inferred from IRQ,
         because os8088 takes channel 2 of that same 8237 inside dsk_xfer.

PHYS is not asserted, it is REPORTED, and it is the one piece of arithmetic a
DOS program does differently here to on a bare machine: its segment is
wherever the arena put it, so the page register has to come from the segment
rather than from habit.  Printing it is what makes a wrong page a readable
failure instead of a silent zero.
"""
import os
import re
import struct
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88geom                                                # noqa: E402
import os88ui                                                  # noqa: E402

SYS = "build/os8088-360.img"
IRQ = "build/dosirq360.img"
MACHINE = "os8088_5150_herc_sb_gla"      # ...the only kind with a card in it


def fail(msg):
    print("dosirq: FAIL: %s" % msg)
    sys.exit(1)


def field(text, name):
    m = re.search(r"^%s\s+(\S+)\s*$" % name, text, re.M)
    return m.group(1) if m else None


def main():
    for p in (SYS, IRQ):
        if not os.path.exists(p):
            fail("%s is missing - `make doscom` builds the gate disks" % p)

    with os88ui.boot(SYS, apps=IRQ, machine=MACHINE) as ui:
        m = ui.m
        row0 = m.sym("drv_tab")

        def loaded():
            return struct.unpack("<H", m.read(row0 + os88geom.DRVR_SEG, 2))[0]

        if not loaded():
            fail("SOUND.DRV is not loaded at the desktop - this machine has a "
                 "card and SPEC.md 51.3.1's boot sniff should have mounted it, "
                 "so the state this row runs from is not the one it describes")

        if not ui.path("B:/DOSIRQ.COM"):
            fail("double-clicking DOSIRQ.COM opened no window")

        rows = []
        end = time.time() + 240.0
        while time.time() < end:
            rows = m.screen() or []
            if any("READY" in r for r in rows):
                break
            time.sleep(0.4)
        else:
            fail("the program never finished; the last text screen was %r"
                 % ([r.rstrip() for r in rows if r.strip()][:12],))

        text = "\n".join(r.rstrip() for r in rows)
        print("dosirq: the bracket's text screen:")
        for r in rows[:12]:
            if r.strip():
                print("   | %s" % r.rstrip())

        if loaded():
            fail("SOUND.DRV is still loaded while the DOS program owns the "
                 "card - its IRQ vector and its TF_SERVICE feeder are both "
                 "live against a DSP the program is resetting (SPEC.md 96.17)")

        dsp = field(text, "DSP")
        if dsp is None:
            fail("the card never answered its reset - with no DSP the numbers "
                 "below are about nothing, because a card that did not answer "
                 "was never asked")
        print("dosirq: the DSP answered version %s" % dsp)

        mask = field(text, "MASK")
        if mask is None:
            fail("no MASK line - the program could not read the 8259's IMR")
        imr = int(mask, 16)
        if not imr & 0x80:
            fail("the bracket handed over IMR %02X, which already has IRQ7 "
                 "UNMASKED - os8088 has no use for the line once SOUND.DRV is "
                 "out of the way, so something is still holding it and this "
                 "row would be measuring that rather than the bracket" % imr)
        if imr & 0x01:
            fail("the bracket handed over IMR %02X, which has IRQ0 MASKED - "
                 "the BIOS tick is what the program's own wait is counted in, "
                 "so the numbers below would be a stopped clock" % imr)
        print("dosirq: the bracket handed over IMR %02X - IRQ7 masked, IRQ0 "
              "live, which is the state the program has to work from" % imr)

        hits = field(text, "IRQ")
        ticks = field(text, "TICKS")
        if hits is None or ticks is None:
            fail("no IRQ/TICKS lines - the program did not reach its report")
        if int(ticks) < 18:
            fail("the program's wait spent %s BIOS ticks of the 18 it asked "
                 "for, so the machine stopped rather than the line failing to "
                 "fire - nothing below this is measurable" % ticks)
        if int(hits) == 0:
            fail("NO HARDWARE INTERRUPT REACHED THE DOS PROGRAM. It reset the "
                 "DSP, read version %s back, hooked INT 0Fh, unmasked IRQ7 and "
                 "asked the card for an interrupt, and over %s BIOS ticks the "
                 "handler never ran. A DOS box that can only host programs "
                 "which poll can host almost nothing (SPEC.md 96.18)"
                 % (dsp, ticks))
        if int(hits) != 1:
            fail("the one-shot DSP request produced %s interrupts, not 1 - a "
                 "line being re-raised is the spurious-IR7 case and is not the "
                 "same thing as it working" % hits)
        print("dosirq: the interrupt arrived, exactly once, over %s ticks"
              % ticks)

        phys = field(text, "PHYS")
        if phys is None:
            fail("no PHYS line - the program did not reach its DMA phase")
        print("dosirq: the DMA buffer landed at physical %s, which is inside "
              "the arena and so nowhere a program would have been tested"
              % phys)

        dma = field(text, "DMA")
        if dma is None:
            fail("no DMA count - the program skipped its transfer; the screen "
                 "says why (%r)" % (text.splitlines()[-3:],))
        if int(dma) == 0:
            fail("THE DMA TRANSFER NEVER COMPLETED. The 8237 was programmed "
                 "for 256 bytes at physical %s on channel 1 and the DSP was "
                 "told to play them, and no completion interrupt arrived - "
                 "which is the half that could not be inferred from the IRQ "
                 "above, because os8088 takes channel 2 of the same "
                 "controller inside dsk_xfer (SPEC.md 96.18)" % phys)
        if int(dma) != 1:
            fail("one transfer produced %s completion interrupts, not 1" % dma)
        print("dosirq: ...and the transfer completed, exactly once")

        m.type_text("x")

    print("dosirq: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
