#!/usr/bin/env python3
"""Documentation consistency gate. Two things drift silently and have:

  1. a SPEC.md section citation that names a heading which does not exist
     (in the two cross-fork documents, MAIN_ONLY below is the allowlist of
     sections that exist on the other branch and not on this one);
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
# Historical slot numbers prose may still name. Nothing is held empty any more
# (SPEC.md 20.3): the five cells that were are either filled - 0x00F8/0x0100
# went to the sound driver - or gone, closed up when the fork stopped keeping
# main's numbers free. Left as an empty set rather than deleted, because the
# next retired-but-still-documented slot wants exactly this.
HELD = set()
# These two describe BOTH forks, so they cite main's numbering on purpose.
CROSS_FORK = {"BRANCH-DIFFERENCES.md", "docs/PORTING.md"}
# ...and inside those two ONLY, these are sections that exist on `main` and not
# here. Listing them rather than exempting cross-fork files wholesale is the
# point: a citation of OUR spec is still checked in those files, and this set
# is the reviewable record of what we are pointing at across the fork. Every
# entry is something the parity audit examined; shrink it when one is ported.
MAIN_ONLY = {
    "2.5",      # the package arena
    "2.6",      # the arena's grant map (cmem.inc)
    "5.5",      # gfx_scroll
    "20.7",     # memory a package asks for
    "20.8",     # Forbidden (binding)
    "41.7",     # xmem testing
    "41.10",    # xmem acceptance
}


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
                    if path in CROSS_FORK and num in MAIN_ONLY:
                        continue
                    bad.append(f"{path}:{n}: SPEC.md §{num} is not a heading")
            if path in CROSS_FORK:
                continue
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
