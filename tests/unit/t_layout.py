#!/usr/bin/env python3
"""A GUEST ADDRESS IS NOT A FILE OFFSET (SPEC.md 2.9).

    python3 tests/unit/t_layout.py

`build/kernel.bin` used to be `.text` at offset 0, so an address off the guest
- a symbol, a near return address, a section's segment - indexed the file
directly.  SPEC.md 2.9 put stage 2 in front of it.  Every host-side reader that
crosses from one to the other now has to say which it means, and
`tools/os88layout.py` is the only place that knows: `text_at`, `seg_at`,
`kernel_text`, `cold_span`.

**FIVE READERS GOT IT WRONG INDEPENDENTLY**, and not one of them failed in a
way that named the cause:

  * `tests/linefast.py` and `tests/wirefps.py` scanned for `call
    gfx_line_fast`, read 6,656 bytes of the wrong thing, found nothing and
    exited "the dispatch moved" - which points at the kernel.  Both had been
    dead since 2.9 landed.
  * `tests/dispcold.py` and `tests/dispreboot.py` compared `.cold` against the
    build and landed on the cold THUNK TABLE in `.text` instead - runs of
    `9A offset <cold seg> / C3`.  They reported ~98% of `.cold` differing,
    which is indistinguishable from the corruption they exist to catch.
    dispreboot printed "THIS IS CORRUPTION" every run and exited 0.
  * `tools/stkwater.py` was handed the raw file for its call-site guard and
    recognised **126 of 3,000** real near-call sites, so a stack dump lost
    almost every attribution and kept a handful of coincidences.

That is the shape of this defect and why it is worth a row: it is SILENT.
Nothing throws, the offsets stay in range, and the bytes it lands on are real
code - so the reading is plausible and points somewhere else.

THE RULE: a host-side file that reads `build/kernel.bin` and also deals in
guest addresses (anything from `os88sym`) must go through `os88layout`, or be
exempt here with a reason.  A file that only hashes or copies the image deals
in no addresses and is exempt on exactly that ground.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                           # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
WHERE = ("tests", os.path.join("tests", "unit"), "tools")

# Exempt, and why.  The reason has to say what the file does with the image -
# "it does not index it" is the only ground that works here.
EXEMPT = {
    "fatwpin.py": "md5s the whole image and never indexes it, so no address "
                  "of any kind crosses into it",
    "os88ladder.py": "md5s the whole of kernel.bin for the page's provenance "
                     "stamp and never indexes it. Every os88sym address it "
                     "holds is handed to the EMULATOR - m.read(), a "
                     "breakpoint - so it addresses the guest's memory and "
                     "not the file, and `.boot2` (which os88sym will not "
                     "place) it resolves against the ladder's own kend",
    "os88layout.py": "is the answer",
}

READS = re.compile(r'kernel\.bin["\']\s*,\s*["\']rb|open\([^)]*kernel\.bin')
ADDRS = re.compile(r'\bos88sym\.')
# A CALL, named and with its parenthesis - not a mention, and not an
# attribute either. This took three goes, each verified by putting the real
# bug back in tests/wirefps.py and watching the row pass anyway:
#   `\bos88layout\b`     - every one of these files NAMES the module in a
#                           comment explaining the subtraction, so all of them
#                           satisfied it
#   `\bos88layout\.\w`    - "tools/os88layout.py" in that same prose matches
#                           an attribute access, because `.py` is one
# Naming the API is the price of precision, and the failure mode is the safe
# one: add a helper without adding it here and this row fails loudly on a file
# that is correct, which is a minute's work and not a silent wrong answer.
USES = re.compile(r'\bos88layout\.(?:boot2_pad|text_at|seg_at|kernel_text'
                  r'|cold_span)\s*\(')


def main():
    seen = 0
    for d in WHERE:
        full = os.path.join(ROOT, d)
        for name in sorted(os.listdir(full)):
            if not name.endswith(".py"):
                continue
            path = os.path.join(full, name)
            if not os.path.isfile(path):
                continue
            src = open(path, encoding="utf-8", errors="replace").read()
            if not (READS.search(src) and ADDRS.search(src)):
                continue
            seen += 1
            if name in EXEMPT:
                continue                # its reason is that it indexes
                                        # nothing; the scan below only counts
            check(bool(USES.search(src)),
                  "%s crosses guest addresses into kernel.bin via os88layout"
                  % name,
                  "it reads build/kernel.bin AND deals in os88sym addresses, "
                  "but calls nothing in os88layout - so it is indexing the "
                  "file with a guest offset. Since SPEC.md 2.9 that is 6,656 "
                  "bytes early, onto real code, and nothing throws. Use "
                  "os88layout.text_at / seg_at / kernel_text / cold_span - or "
                  "add an EXEMPT entry here saying why no address of any kind "
                  "crosses into the image.")

    check(seen >= 5, "the scan still finds the readers it is about",
          "only %d file(s) both read kernel.bin and use os88sym addresses. "
          "The patterns probably stopped matching, which would make this row "
          "pass by finding nothing." % seen)
    done("kernel.bin addressing")


if __name__ == "__main__":
    main()
