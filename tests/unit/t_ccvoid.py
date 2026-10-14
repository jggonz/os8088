#!/usr/bin/env python3
"""The C peephole pass reads a function's RETURN TYPE, not a parameter's.

    python3 tests/unit/t_ccvoid.py

tools/ccpeep.py's `dead` rule treats AX as dead at the `ret` of a function
SmallerC's own `; glb f : ( ... ) void` comment calls void, and deletes what
computed it. tools/ccverify.py, the pass's differential check, asks the same
`void_functions()` which functions those are - so a misread there is a
miscompile that the checker calls EQUIVALENT.

The comment NESTS, and the first version read the first `) <type>` line as
the function's. Two shapes then read as void when they are not:

  int each(void (*cb)(int v), int n)   a function-pointer PARAMETER with
                                       parameters of its own closes with its
                                       own `) void`; `return keep + 1` was
                                       deleted, and each() answered whatever
                                       AX the callback left
  void (*getf(void))(int)              a function RETURNING one: its `(void)
                                       * (` line, then the pointer's `) void`;
                                       `mov ax, _tgt` was deleted

No package had either shape when it was found (every built *.raw.asm was
classified the same before and after the fix), so nothing else would have
caught it. The blocks below are SmallerC's own comments for those two and
for the shapes around them, as build/cc/SmallerC/smlrc writes them.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                           # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import ccpeep                                             # noqa: E402

# name -> (SmallerC's comment, void?)
SHAPES = {
    "tgt": ("; glb tgt : (\n"
            "; prm     v : int\n"
            ";     ) void\n", True),
    "each": ("; glb each : (\n"
             "; prm     cb : * (\n"
             "; prm         v : int\n"
             ";         ) void\n"
             "; prm     n : int\n"
             ";     ) int\n", False),
    "getf": ("; glb getf : (void) * (\n"
             "; prm     <something> : int\n"
             ";     ) void\n", False),
    "vcb": ("; glb vcb : (\n"
            "; prm     cb : * (\n"
            "; prm         v : int\n"
            ";         ) void\n"
            ";     ) void\n", True),
    "getg": ("; glb getg : (\n"
             "; prm     a : int\n"
             ";     ) * (\n"
             "; prm     <something> : int\n"
             ";     ) int\n", False),
}


def main():
    for name, (text, void) in SHAPES.items():
        got = ccpeep.void_functions(text + "section .text\n_%s:\n" % name)
        check(("_" + name in got) == void,
              "%s is %s" % (name, "void" if void else "NOT void"),
              why="the dead rule deletes AX's last write in a void "
                  "function, and ccverify drops AX from its trace there",
              got=sorted(got), want=["_" + name] if void else [])
    done("t_ccvoid")


if __name__ == "__main__":
    main()
