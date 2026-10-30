#!/usr/bin/env python3
"""A `.cold` label is not resolved through os88sym.linear() by a script.

    python3 tests/unit/t_coldsym.py

`.cold` MOVES AT BOOT. On a machine with neither a VGA nor an EGA - every CGA,
Hercules and MDA machine, MartyPC's DEFAULT among them - kmain_o slides it
down over the VGA rung by VGA_PARA paragraphs (SPEC.md 2.10.9,
docs/plans/MOVABLE-COLD-PLAN.md 2). `os88sym.linear()` answers from the MAP,
so for a `.cold` label it names the address the routine had BEFORE the slide,
0x7F0 bytes above where it runs: a breakpoint there lands in some OTHER
`.cold` routine, a read returns that routine's bytes, and nothing errors.

`m.sym()` follows the slide - it reads [api_coldseg] - and is what a row on a
live machine uses. tests/fddpark.py set `m.bp_exec(S("dsk_rb_go"))` with
S = os88sym.linear on a CGA and stopped in dskw_norm.hi in the SECOND boot,
after the int 19h it existed to read the machine at: a green row that tested
nothing, and red only when that other code was not reached in time.
tests/usbmouse.py carried the same line, and tests/sheetmove.py armed two
`.cold` breakpoints that could never match.

So this walks every Python file under tests/ and tools/ and fails on a call
of os88sym.linear - directly, or through a name bound to it in the enclosing
scope (`S = os88sym.linear`, `S = lambda n: os88sym.linear(n)`), or
through a resolver the function was handed as a PARAMETER (which may be
either, so it must be m.sym()'s job or a listed exception) - whose
symbol is a LITERAL that kern_big's map puts in `.cold`. A use that wants the
unslid address on purpose, or runs only where nothing slides (a VGA machine,
kern_small, which has no slide), is a line in tests/coldsym.txt with its
reason; an entry nothing matches any more fails too, so the list only turns
one way. What it cannot see is a symbol named by a variable, and a table
built over every symbol - tools/os88vidprof.py and tools/os88rate.py had
that shape, and take `m.cold_slide()` now.
"""
import ast
import os
import re
import subprocess
import sys
import warnings

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                           # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88sym                                            # noqa: E402

ALLOW = os.path.join(ROOT, "tests", "coldsym.txt")
SKIP = {"tools/os88sym.py", "tools/os88marty.py",       # the resolvers
        "tests/suite.py"}           # the registry: prose, and 1s of parse


def is_linear(node):
    """`os88sym.linear` or a bare `linear` (from os88sym import linear)."""
    return ((isinstance(node, ast.Attribute) and node.attr == "linear"
             and isinstance(node.value, ast.Name)
             and node.value.id == "os88sym")
            or (isinstance(node, ast.Name) and node.id == "linear"))


def aliases_linear(value):
    if is_linear(value):
        return True
    return (isinstance(value, ast.Lambda) and isinstance(value.body, ast.Call)
            and is_linear(value.body.func))


def scan(path, rel, cold):
    try:
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")     # other files' bad escapes
            tree = ast.parse(open(path, errors="replace").read(), rel)
    except SyntaxError:
        return []
    # ONE walk: the parents, the bindings and the candidate calls together
    parent, assigns, calls = {}, [], []
    for n in ast.walk(tree):
        for c in ast.iter_child_nodes(n):
            parent[c] = n
        if isinstance(n, ast.Assign):
            assigns.append(n)
        elif isinstance(n, ast.Call) and n.args \
                and isinstance(n.args[0], ast.Constant) \
                and n.args[0].value in cold:
            calls.append(n)
    scopes = (ast.Module, ast.FunctionDef, ast.AsyncFunctionDef, ast.Lambda)

    def scope_of(n):
        n = parent.get(n)
        while n is not None and not isinstance(n, scopes):
            n = parent.get(n)
        return n

    # name -> {scope: [does each binding alias linear]}
    binds = {}
    for n in assigns:
        for t in n.targets:
            if isinstance(t, ast.Name):
                binds.setdefault(t.id, {}).setdefault(
                    scope_of(n), []).append(aliases_linear(n.value))

    def params(s):
        if isinstance(s, (ast.FunctionDef, ast.AsyncFunctionDef, ast.Lambda)):
            a = s.args
            return {x.arg for x in a.posonlyargs + a.args + a.kwonlyargs}
        return set()

    def resolves_linear(call):
        """True for os88sym.linear or an alias of it; "param" for a resolver
        handed in as an ARGUMENT, which no reading of this file can place -
        the caller may pass os88sym.linear (tests/dispcp.py's helpers take
        `S` from rows that bind it either way)."""
        f = call.func
        if is_linear(f):
            return True
        if not isinstance(f, ast.Name):
            return False
        s = scope_of(call)
        while s is not None:
            if s in binds.get(f.id, {}):
                return any(binds[f.id][s])
            if f.id in params(s):
                return "param"
            s = scope_of(s)
        return False

    # (either kind: an alias of linear, or a resolver handed in)
    return [(n.lineno, n.args[0].value) for n in calls if resolves_linear(n)]


def main():
    sect = os88sym.sections()
    cold = {k for k, v in sect.items() if v == ".cold"}
    check(len(cold) > 500, "kern_big's map has its .cold labels",
          why="an empty set passes every check below", got=len(cold),
          want="> 500")
    files = subprocess.run(["git", "ls-files", "tests", "tools"], cwd=ROOT,
                           capture_output=True, text=True).stdout.split()
    files = [f for f in files if f.endswith(".py") and f not in SKIP]
    check(len(files) > 300, "the tree's scripts were found", got=len(files),
          want="> 300")
    allow = {}
    for ln in open(ALLOW):
        ln = ln.strip()
        if ln and not ln.startswith("#"):
            f = ln.split(None, 2)
            check(len(f) == 3, "tests/coldsym.txt: path, symbol and a reason",
                  got=ln)
            allow[(f[0], f[1])] = False
    lit = re.compile(r"""["']([A-Za-z_][\w.]*)["']""")
    hits = []
    for f in files:
        # only a file that SPELLS a `.cold` label is worth a parse: that
        # cuts ~600 files to ~70, which is the whole of this row's cost
        try:
            text = open(os.path.join(ROOT, f), errors="replace").read()
        except OSError:
            continue
        if not cold.intersection(lit.findall(text)):
            continue
        for line, name in scan(os.path.join(ROOT, f), f, cold):
            if name not in cold:
                continue
            if (f, name) in allow:
                allow[(f, name)] = True
            else:
                hits.append("%s:%d: %s" % (f, line, name))
    check(not hits, "no .cold label is resolved through os88sym.linear()",
          why="on a CGA, Hercules or MDA machine `.cold` slid down by "
              "VGA_PARA at boot, so linear()'s address is 0x7F0 bytes above "
              "the routine and lands in another one; use m.sym(), or list "
              "the use in tests/coldsym.txt with why the slide cannot reach "
              "it", got=hits[:12], want="[]")
    stale = ["%s %s" % k for k, used in allow.items() if not used]
    check(not stale, "every tests/coldsym.txt entry still matches a use",
          why="the list only turns one way: delete the line", got=stale,
          want="[]")
    done("t_coldsym")


if __name__ == "__main__":
    main()
