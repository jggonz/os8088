#!/usr/bin/env python3
"""The shipped image set, $(SHIPIMGS), read out of the Makefile.

Three readers want this list and none may restate it: `tests/unit/t_volsig.py`
compares the set's signatures, `tools/os88soak.py` builds the set before a
run, and `tools/os88disk.py` gives each member its own signature slot
(SPEC.md 18.8.2.2). A restated copy went stale once already - it stopped at
media360.img when the category disks joined - so the Makefile is the one
authority and this is its one reader.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def ship_images(root=ROOT):
    """The basenames $(SHIPIMGS) expands to, SORTED.

    The order is the slot order os88disk.py uses, so it must not depend on
    how the Makefile happens to lay the variable out.
    """
    # BACKSLASH CONTINUATIONS JOINED FIRST: $(SHIPIMGS) is three lines, and a
    # line-at-a-time read finds four of the twelve and reports a green row over
    # a third of the set.
    mk = re.sub(r"\\\n\s*", " ", (Path(root) / "Makefile").read_text())
    var = {}
    for m in re.finditer(r"^([A-Z0-9_]+)\s*:?=\s*(.+?)\s*$", mk, re.M):
        var.setdefault(m.group(1), m.group(2))
    out, seen = [], set()

    def expand(text, depth=0):
        if depth > 8:
            return
        for tok in text.split():
            m = re.fullmatch(r"\$\(([A-Z0-9_]+)\)", tok)
            if m:
                if m.group(1) in var and m.group(1) not in seen:
                    seen.add(m.group(1))
                    expand(var[m.group(1)], depth + 1)
            elif tok.endswith(".img"):
                out.append(Path(tok).name)

    expand(var.get("SHIPIMGS", ""))
    return sorted(set(out))


if __name__ == "__main__":
    for i, n in enumerate(ship_images(), 1):
        print("%2d  %s" % (i, n))
