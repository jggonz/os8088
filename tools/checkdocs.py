#!/usr/bin/env python3
"""Documentation consistency gate. Two things drift silently and have:

  1. a SPEC.md section citation that names a heading which does not exist;
  2. an API slot number in prose that is not a slot apps/os88api.inc defines.

The second is the nastier one: after a renumbering, a stale citation is often
still a VALID slot - just a different routine - so nothing catches it and the
prose quietly documents the wrong call. Run from the repo root; exit 1 on any
finding.
"""
import re
import subprocess
import sys

HEAD = re.compile(r"^#{2,6}\s+(\d{1,2}(?:\.\d+)*)\.?\s")
CITE = re.compile(r"(?:SPEC\.md\s+§?|§)(\d{1,2}(?:\.\d+)*(?:/\d{1,2}(?:\.\d+)*)*)")
SLOT = re.compile(r"(?:slot|API) (0x0[0-9A-Fa-f]{3})")   # both spellings: the
# renumbering left three "API 0x..." header comments behind (wm_sizable,
# wm_fullscreen, menu_win_set) because only "slot 0x..." was being checked, and
# a wrong number in a routine's own header is the one place an author looks.

# Numbered RULES inside a section, not sections: SPEC.md 1 and 29.2 are lists.
# Plus the reserved-range note in the preamble, which names a span, not a
# heading.
RULE_REFS = {"1.6", "1.7", "29.2.8", "45", "49"}
# Slot numbers prose may still name that apps/os88api.inc deliberately does
# not define. Nothing is held EMPTY any more (SPEC.md 20.3) - the five cells
# that were are either filled (0x00F8/0x0100 went to the sound driver) or
# gone, closed up when the numbering settled.
#
# 0x01E8 is the other case, and the one this set was kept for: RETIRED
# (SPEC.md 18.4.1/20.8). It was OSAPI_FILE_READBIG; OSAPI_FILE_READ absorbed
# it, the cell still exists and still answers CF=1 so nothing above it had to
# move, and the SDK publishes no name for it - so a package that still calls
# it fails to assemble. Prose has to keep naming the number, because the
# number is the whole reason the cell is still there.
HELD = {"0x01e8"}


def main() -> int:
    spec = open("SPEC.md", encoding="utf-8").read()
    heads = {m.group(1) for m in map(HEAD.match, spec.split("\n")) if m}
    api = {m.group(1).lower() for m in
           re.finditer(r"KERNEL_SEG:(0x0[0-9A-Fa-f]{3})",
                       open("apps/os88api.inc", encoding="utf-8").read())}

    files = [f for f in subprocess.run(["git", "ls-files"], check=True,
                                       capture_output=True, text=True).stdout.split()
             if not f.startswith("build/")
             and (f.rsplit(".", 1)[-1] in ("md", "inc", "asm", "py") or f == "Makefile")]

    bad = []
    for path in files:
        try:
            text = open(path, encoding="utf-8").read()
        except (UnicodeDecodeError, IsADirectoryError, FileNotFoundError):
            continue
        for n, line in enumerate(text.split("\n"), 1):
            for m in CITE.finditer(line):
                for num in m.group(1).split("/"):
                    if num in heads or num in RULE_REFS:
                        continue
                    bad.append(f"{path}:{n}: SPEC.md §{num} is not a heading")
            for m in SLOT.finditer(line):
                num = m.group(1).lower()
                if num not in api and num not in HELD:
                    bad.append(f"{path}:{n}: 'slot {m.group(1)}' is not an "
                               "os88api.inc slot")
    for b in bad:
        print(b, file=sys.stderr)
    print(f"checkdocs: {len(heads)} headings, {len(api)} slots, "
          f"{len(bad)} problem(s)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
