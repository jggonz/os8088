#!/usr/bin/env python3
"""A row that runs under Bochs does not pace its SCENARIO in host seconds.

    python3 tests/unit/t_bochssleep.py

Bochs runs kern_mega at `sync=slowdown`: the guest's clock is its own
instruction count, capped at real time (docs/BOCHS.md, *The clock*). A Bochs
short of CPU - four of them to a core in the soak's Bochs lane - therefore
runs FEWER guest seconds per host second, and a `time.sleep(1.0)` between two
inputs hands the guest whatever the host gave it meanwhile. megavidv2's
"Space, then Space again a second in, while the blocks are coming" paused the
play before its first frame that way, and both of its tails were wrong.

What a row means by such a pause is GUEST time: `Box.guest_sleep(secs)` on a
row that drives `os88bochs.Box` itself, and `os88marty.guest_sleep` /
`os88marty.pace` on a row ported through the shim (tests/megaport.txt), which
end on the guest's own clock. A wait on guest STATE is better still.

So this walks the script of every row that needs Bochs (tests/suite.py) and
fails on a `time.sleep(...)` that is not plainly a host wait. A sleep is
taken as one when it is

  * inside a `while` loop - a poll: the loop's test or body reads the state
    it waits on, and the sleep is only the interval between reads;
  * inside a `for _ in ...` loop that can `break` or `return` - the bounded
    poll (`for _ in range(200): try: connect; break / except: sleep`);
  * a literal under 0.1 s - event spacing to the X server, not an interval
    the guest is meant to spend;
  * or marked: `# host:` and a reason on the line or the line above it - a
    host process (a server's pacing, a socket appearing, a kill), a tail
    that waits on nothing the guest does.

Anything else is a scenario step in host seconds and is red until it is
converted or marked. What it cannot see: a host-time DEADLINE used as a
scenario window (`end = time.time() + 45; while ...`), which looks exactly
like a poll's outer budget - the review is the guard there.
"""
import ast
import os
import sys
import warnings

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                           # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tests"))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import suite                                              # noqa: E402

MARK = "# host:"


def bochs_scripts():
    """tests/*.py that some row needing Bochs runs."""
    out = set()
    for r in suite.rows():
        if "bochs" not in r.needs:
            continue
        cmd = r.cmd if isinstance(r.cmd, (list, tuple)) else r.cmd.split()
        for tok in cmd:
            if str(tok).endswith(".py"):
                p = os.path.join(ROOT, str(tok))
                if os.path.exists(p):
                    out.add(os.path.relpath(p, ROOT))
                break
    return sorted(out)


def is_sleep(call, bare):
    f = call.func
    if isinstance(f, ast.Attribute) and f.attr == "sleep" and \
            isinstance(f.value, ast.Name) and f.value.id == "time":
        return True
    return bare and isinstance(f, ast.Name) and f.id == "sleep"


def exits(loop):
    """Can LOOP's own body leave it early (a break/return not in a nested
    loop or function)?"""
    todo = list(loop.body)
    while todo:
        n = todo.pop()
        if isinstance(n, (ast.Break, ast.Return)):
            return True
        if isinstance(n, (ast.For, ast.While, ast.FunctionDef,
                          ast.AsyncFunctionDef, ast.Lambda)):
            continue
        todo.extend(ast.iter_child_nodes(n))
    return False


def host_loop(stack):
    """The innermost enclosing loop makes this sleep a poll's interval."""
    for n in reversed(stack):
        if isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef,
                          ast.Lambda)):
            return False
        if isinstance(n, ast.While):
            return True
        if isinstance(n, ast.For):
            return (isinstance(n.target, ast.Name) and n.target.id == "_"
                    and exits(n))
    return False


def small(call):
    a = call.args[0] if call.args else None
    return (isinstance(a, ast.Constant) and isinstance(a.value, (int, float))
            and a.value < 0.1)


def scan(path):
    src = open(os.path.join(ROOT, path)).read()
    lines = src.splitlines()
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")     # other files' bad escapes
        tree = ast.parse(src, path)
    bare = any(isinstance(n, ast.ImportFrom) and n.module == "time" and
               any(a.name == "sleep" for a in n.names)
               for n in ast.walk(tree))
    bad = []

    def walk(node, stack):
        if isinstance(node, ast.Call) and is_sleep(node, bare):
            ln = node.lineno
            near = lines[ln - 1] + (lines[ln - 2] if ln > 1 else "")
            if not (MARK in near or small(node) or host_loop(stack)):
                bad.append((ln, lines[ln - 1].strip()))
        for c in ast.iter_child_nodes(node):
            walk(c, stack + [node])
    walk(tree, [])
    return bad


def main():
    files = bochs_scripts()
    check(len(files) >= 60, "the rows needing Bochs name their scripts "
          "(%d found - did tests/suite.py's `needs` change?)" % len(files))
    for p in files:
        for ln, text in scan(p):
            check(False, "%s:%d: a host-timed sleep in a Bochs row - %s. "
                  "Pace the guest (Box.guest_sleep / os88marty.guest_sleep / "
                  "pace) or wait on its state; a genuine host wait says "
                  "`%s <why>` on the line" % (p, ln, text, MARK))
    print("t_bochssleep: %d scripts" % len(files))
    done("t_bochssleep")


if __name__ == "__main__":
    main()
