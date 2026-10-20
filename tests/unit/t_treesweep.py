#!/usr/bin/env python3
"""A MARKER IS NOT A PRODUCT: `_sweep_truncated` must never eat a stamp.

    python3 tests/unit/t_treesweep.py

`tools/os88build.py` deletes zero-length files from a private tree before make
looks at it, because an interrupted build leaves an empty output that is NEWER
than everything it was built from and make then reports the tree up to date.
That rule is right about products and catastrophic about MARKERS, every one of
which the Makefile creates with a bare `touch` and is therefore exactly zero
bytes:

  * sweeping `$(VIDSTAMP)` makes the next `make`'s parse-time rule find no
    stamp - which is its signal that THE KNOB SET CHANGED - so it deletes
    kernel.bin, kernel-full.bin, kernel.sys, both boot sectors and six drivers
    and rebuilds the lot, on a tree that was already correct;
  * so the reuse os88build advertises never happened (measured: 19.9 s for a
    "second call over an up-to-date tree" against 0.4 s once fixed), and
  * two rows sharing a tree - four pairs do - REBUILT IT UNDER EACH OTHER'S
    reader. That is msegnomem's soak failure twice and paintpack's once, every
    one of them passing when run alone.

THE RATCHET IS THE MAKEFILE AND NOT A LIST HERE. A second copy of the marker
names would go stale the first time somebody adds one; this reads every
`touch`ed target out of the Makefile itself and asserts the sweep spares it.
A marker named some third way therefore fails HERE, at a cost of milliseconds,
rather than in a soak row three hours in.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88build                                            # noqa: E402

fails = []


def markers():
    """Every `$(BUILD)/...` file the Makefile creates with a bare `touch`.

    Two spellings, and both are in the tree: `touch $(XSTAMP)` inside a
    parse-time `$(shell ...)` guard, and `@touch $@` as the whole recipe of a
    rule whose target is a stamp. The variables are resolved from their own
    definitions, and only the part that is a literal basename is kept - the
    rest is `$(if ...)` knob suffixes, which cannot change the first character
    or the extension and so cannot change the verdict.
    """
    mk = open(os.path.join(ROOT, "Makefile")).read()
    var = dict(re.findall(r'^([A-Z0-9_]+)\s*:?=\s*\$\(BUILD\)/(\S*)',
                          mk, re.M))
    out = set()
    for name in re.findall(r'touch \$\(([A-Z0-9_]+)\)', mk):
        if name in var:
            out.add(var[name])
    # `@touch $@` (or `touch $@`) as a recipe: walk back to the target.
    lines = mk.splitlines()
    for i, ln in enumerate(lines):
        if ln.strip() not in ("@touch $@", "touch $@"):
            continue
        for j in range(i - 1, max(i - 8, -1), -1):
            m = re.match(r'^(\S[^:]*):', lines[j])
            if m:
                t = m.group(1).strip()
                if t.startswith("$(BUILD)/"):
                    out.add(t[len("$(BUILD)/"):])
                elif t.startswith("$(") and t[2:-1] in var:
                    out.add(var[t[2:-1]])
                break
    return sorted(out)


def spared(name):
    """What `_sweep_truncated` decides, asked of the real thing."""
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, name)
        open(p, "w").close()
        os88build._sweep_truncated(d)
        return os.path.exists(p)


def main():
    ms = markers()
    if len(ms) < 10:
        fails.append("only %d markers found in the Makefile - the scan is "
                     "broken, not the tree" % len(ms))
    for m in ms:
        # The knob suffixes are `$(if ...)` expansions; strip them to the
        # literal head, which is all the sweep's test can see anyway.
        lit = m.split("$")[0] or m
        if not spared(lit):
            fails.append("_sweep_truncated eats the marker %r (from %r)"
                         % (lit, m))

    # ...AND IT STILL SWEEPS A PRODUCT, which is the half that made it exist.
    for n in ("kernel.bin", "os8088-360.img", "ctrl.drv"):
        if spared(n):
            fails.append("_sweep_truncated no longer removes a truncated "
                         "product (%s) - the interrupted-build case is back"
                         % n)

    if fails:
        print("t_treesweep: FAIL")
        for f in fails:
            print("  " + f)
        return 1
    print("t_treesweep: %d Makefile markers spared, 3 truncated products "
          "still swept" % len(ms))
    return 0


if __name__ == "__main__":
    sys.exit(main())
