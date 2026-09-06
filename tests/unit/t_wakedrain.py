#!/usr/bin/env python3
"""Every event-queue drain gives a package's wake back.

    python3 tests/unit/t_wakedrain.py

SPEC.md 74.1.1.  `wm_wake` coalesces one queued wake per window slot, and
`wm_wkq[slot]` is the flag that says a record for that window is in the ring.
The flag and the record are ONE FACT.  Six loops in this kernel pop records
they are not interested in and throw them away - `ui_drag`, `ui_grow` and both
of `fm_drag`'s drain everything that is not an `EVT_MUP`, and `fsx`, `snd` and
`blk_pass` empty the ring outright - and a wake eaten by one of those used to
leave the flag set with no record behind it.  The window is then DEAF FOR THE
REST OF ITS LIFE: every `wm_wake` after it reads "one is already queued" and
answers CF=0, a promise nothing will ever keep.

**Since kernel size pass 3 the discipline lives in TWO SHARED HELPERS** and
not in six copies of it: `evq_drain` and `evq_mup` (SPEC.md 10.3), the second
being the "pop until an EVT_MUP" form the four tracking loops all call now.
So `seen` is 2 rather than 6, and that is the healthy number - the blindness
guard below is what keeps it from being 0.  A new drain that writes the loop
out again instead of calling a helper is still caught, which is the point.

The field found it as *"dragging the window during the write kills the FTP
transfer, and the client cannot reconnect"* - a kernel bug that presents as a
network one, because what breaks is whatever the package was doing across its
worker boundary.  It was chased into `ETHER.DRV` first and was never there.

So: a `call evq_pop` whose record can be DISCARDED must be within reach of
either a `call wm_wake_eaten` or an `EVT_WAKE` comparison that dispatches it.
That is the rule this checks, and it is a lint rather than a proof - it reads
a window of lines after the call and does not follow control flow.  What it
buys is that the FIFTH drain, written a year from now, cannot land silently.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WINDOW = 14                     # lines after the call to look in


def main() -> int:
    bad = []
    seen = 0
    # kernel.asm is in scope too, and not only the modules it includes: a
    # drain written in kmain or in one of the far shims would otherwise land
    # exactly as silently as the ones this exists for.
    srcs = (sorted((ROOT / "kernel").glob("*.inc"))
            + [ROOT / "kernel" / "kernel.asm"])
    for src in srcs:
        lines = src.read_text().splitlines()
        for i, line in enumerate(lines):
            # The far form too: a .cold drain pops through the KERNEL_SEG
            # shim, and the two fm_drag loops this pattern missed were
            # exactly that - live wake-eaters passing the gate built for
            # them. `evq_init` used to be matched here as well, a whole-ring
            # reset being a drain that owes the same bookkeeping; it was
            # deleted in size pass 3 (SPEC.md 10) and the exemption its one
            # legitimate caller needed went with it.
            if not re.match(
                    r"\s*call\s+(KERNEL_SEG:)?(cw_)?evq_pop\s*(;.*)?$",
                    line):
                continue
            seen += 1
            after = "\n".join(lines[i:i + WINDOW])
            if "wm_wake_eaten" in after or "EVT_WAKE" in after:
                continue
            bad.append("%s:%d: a drain that can DISCARD the record, with no "
                       "`call wm_wake_eaten` and no EVT_WAKE dispatch within "
                       "%d lines (SPEC.md 74.1.1)"
                       % (src.relative_to(ROOT), i + 1, WINDOW))

    if seen == 0:
        print("wakedrain: FOUND NO evq_pop AT ALL - the checker has gone "
              "blind, which is worse than a failure")
        return 1
    for b in bad:
        print(b)
    print("wakedrain: %d evq_pop site(s), %d problem(s)" % (seen, len(bad)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
