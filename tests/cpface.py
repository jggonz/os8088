#!/usr/bin/env python3
"""Pin the Control Panel to its COMPACT face for a row that drives it.

SPEC.md 135.2: on a VGA or VESA desktop whose work area holds 616x408 the
panel opens WIDE (CPWIDE.DRV), and every row written before 135 clicks the
compact panel's coordinates - its 320x151 frame, its item list at CP_IX /
CP_I0Y, its pane at CP_RX. 135.13 gives such a row two honest choices: read
the wide geometry, or be pinned to the compact face. This is the second,
for the rows whose subject is a page's LOGIC (a driver's page, the Display
page's VESA row, the Keyboard page's text), which both faces run - the wide
face hosts the very same page code (135.3).

How: the first byte of the module's resident file name, `mod_f_cpw`
('CPWIDE.DRV'), is written to 0 before the first open - which is the
kernel's OWN mark for "CPWIDE.DRV was refused" (135.2): the open tests it
and takes the compact face without a lookup, exactly as a machine whose
system disk has no CPWIDE.DRV does after its first open. Nothing in the
image changes, no kernel byte is spent, and the row's disk reads are the
ones it had before 135.

A kernel with no wide face (kern_small) has no `mod_f_cpw`: nothing to do.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)


def compact(m, sym):
    """Pin the next Control Panel open to the compact face.

    m:   a QEMU handle with `.hmp(cmd)` (heapmap.Qmp, vidxms.Q, ethernet's
         Qemu, ...) or a MartyPC handle with `.write(linear, data)`.
    sym: name -> LINEAR address (os88sym.linear or a row's own wrapper).
    Answers True when the byte was written, False on a kernel with no wide
    face. Call it before the first open: an open already wide stays wide
    until the panel closes (135.2 step 1)."""
    try:
        lin = sym("mod_f_cpw")
    except Exception:
        return False
    if lin is None:
        return False
    if hasattr(m, "hmp"):
        import os88qemu
        os88qemu.poke(m.hmp, lin, b"\0")
    else:
        m.write(lin, b"\0")
    return True
