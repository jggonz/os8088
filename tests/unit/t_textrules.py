#!/usr/bin/env python3
"""Transparent text is a CLOSED LIST, and this is the gate (SPEC.md 6.6).

    python3 tests/unit/t_textrules.py

`font_run` (SPEC.md 6.1) draws a cell's background and its glyph in ONE pass.
`font_char` and `font_str` are TRANSPARENT - they leave whatever is underneath
- so a caller that fills a rect and then letters it has written every one of
those pixels twice, with a gap between the two passes that is VISIBLE on a
4.77MHz 8088.  PERFORMANCE.md Part 1 rates that double-draw flash one of three
defects an emulator cannot show you.

THE DEFECT THIS CLOSES IS NOT A SLOW PRIMITIVE.  It is that `font_str` is what
everybody reaches for: it is the shortest call, every existing package uses it,
and a new window written that way flickers until a person notices and sends it
back.  SPEC.md 6.6 is the argument and the list of SIX cases where transparent
text is genuinely right; this file is the part that binds.

IT IS A RATCHET, NOT A CLEAN GATE, and that is deliberate.  `tests/textsites.txt`
starts at the count the tree actually has, because a rule that cannot be
enforced from the day it is written is not enforced at all.  What it stops from
day one is the number going UP.  Three failures:

  * a FILE NOT IN THE REGISTRY calling transparent text - the case that
    matters, because it is what a new package hits;
  * a file EXCEEDING its registered count;
  * a registered count that is now too HIGH, so the numbers cannot rot upward
    quietly behind a conversion that lowered them.

The precedent is two rows over: t_asmrules.py already scans every source in the
tree, and t_registry.py already checks that every test is registered somewhere
or says why not.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                           # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
REGISTRY = os.path.join(ROOT, "tests", "textsites.txt")

# Every spelling of "draw text transparently".  The kernel-internal names are
# in here as well as the SDK's, because kernel/ and drivers/ are where more
# than a third of the sites are - ctrl.inc alone has 28 - and a rule that only
# reached packages would leave the biggest population unwatched.
CALL = re.compile(
    r"\bcall\s+(?:[A-Z_]+SEG:)?(?:cw_)?(?:font_char|font_str|font_str_x)\b"
    r"|\bcall\s+OSAPI_FONT_(?:CHAR|STR)_XPARENT\b"
    r"|\bos88_font_(?:char|str)_xparent\s*\("
    # ...and the SDK's own MACRO, whose body IS one of the two calls above
    # (apps/os88ui.inc:67 kernel arm, :116 package arm).  A macro invocation
    # matches no `call` at all, so without this line every package that draws
    # a button label through ui_button walks straight past the gate - which is
    # precisely the headline case, a FILE NOT IN THE REGISTRY drawing
    # transparent text.  The `%macro`/`%endmacro` lines that DEFINE it are not
    # invocations, so they are excluded rather than counted twice.
    r"|^(?!\s*%)(?:[^;]*?\b)?UI_STR\b")

# font.inc IMPLEMENTS them, so its internal calls are not call sites; the C
# SDK's thunk and header likewise declare rather than use.  Named rather than
# pattern-matched so that adding one is a decision.
EXEMPT = {
    os.path.join("kernel", "font.inc"): "implements them",
    os.path.join("apps", "cc", "os88thunk.asm"): "the C SDK's thunk IS the call",
    os.path.join("apps", "cc", "os88.h"): "declares them",
    os.path.join("apps", "cword", "hosttest", "cwuitest.c"): "a HOST-side stub, "
                                                             "no framebuffer",
    os.path.join("apps", "cword", "hosttest", "os88.h"): "...and its header",
}


def sources():
    for sub in ("kernel", "drivers", "apps", "tests"):
        for base, _dirs, files in os.walk(os.path.join(ROOT, sub)):
            for f in sorted(files):
                if f.endswith((".asm", ".inc", ".c", ".h")):
                    p = os.path.relpath(os.path.join(base, f), ROOT)
                    if p.replace("\\", "/") not in {k.replace("\\", "/")
                                                    for k in EXEMPT}:
                        yield p


def code(ln):
    """The line with its comment removed, or "" if it is only a comment.

    An asm comment starts at `;` and a C one at `//` or `*` on a continuation
    line - and a C STATEMENT also ends in `;`, so splitting on it is safe only
    because everything this matches sits left of that semicolon.  Prose about
    these calls is common in this tree (SPEC.md 6.6 is quoted in half a dozen
    headers) and counting it would make the registry a fiction.
    """
    t = ln.lstrip()
    if t.startswith((";", "//", "*", "/*")):
        return ""
    return ln.split(";")[0]


def scan():
    """{path: count} for every file that draws text transparently."""
    out = {}
    for p in sources():
        with open(os.path.join(ROOT, p), errors="replace") as fh:
            n = sum(1 for ln in fh if CALL.search(code(ln)))
        if n:
            out[p.replace("\\", "/")] = n
    return out


def registry():
    """{path: count} from tests/textsites.txt; `#` is a comment."""
    out = {}
    with open(REGISTRY) as fh:
        for ln in fh:
            ln = ln.split("#")[0].strip()
            if not ln:
                continue
            n, _, path = ln.partition(" ")
            out[path.strip()] = int(n)
    return out


def main():
    have, want = scan(), registry()

    for path in sorted(set(have) | set(want)):
        got, reg = have.get(path, 0), want.get(path, 0)
        if path not in want:
            check(False, "textrules: %s is not registered" % path,
                  why="SPEC.md 6.6: transparent text draws every pixel twice and "
                      "flashes on the target machine. Use font_run (6.1) - or, if "
                      "this is one of 6.6.2's six cases, add a line to "
                      "tests/textsites.txt saying WHICH, so the choice is in the "
                      "diff rather than in somebody's head",
                  got="%d transparent call(s), file not in the registry" % got,
                  want="0 calls, or a registry line carrying a reason")
        else:
            check(got <= reg, "textrules: %s drew more than it is allowed" % path,
                  why="SPEC.md 6.6's ratchet only turns one way. A new transparent "
                      "call needs a registry line saying which of 6.6.2's six "
                      "cases it is",
                  got="%d transparent call(s)" % got,
                  want="at most the registered %d" % reg)
            check(got >= reg, "textrules: %s is registered too high" % path,
                  why="SPEC.md 6.6.3: a count left high after a conversion lets the "
                      "next one creep back up unnoticed. Lowering it is the diff "
                      "saying the work happened",
                  got="%d transparent call(s)" % got,
                  want="the registered %d - lower the registry line to %d"
                       % (reg, got))

    total = sum(have.values())
    kept = sum(1 for ln in open(REGISTRY)
               if " # " in ln and "backlog:" not in ln
               and not ln.lstrip().startswith("#"))
    print("t_textrules: %d transparent call site(s) in %d file(s); %d file(s) "
          "carry a SPEC.md 6.6.2 reason, the rest are backlog"
          % (total, len(have), kept))
    done("t_textrules")


if __name__ == "__main__":
    main()
