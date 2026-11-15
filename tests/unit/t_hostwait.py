#!/usr/bin/env python3
"""Rows do not pace the GUEST in HOST time: a one-way ratchet per test file.

    python3 tests/unit/t_hostwait.py              the gate
    python3 tests/unit/t_hostwait.py --list       every site, by file and kind
    python3 tests/unit/t_hostwait.py --rank       files by (row secs x sites)
    python3 tests/unit/t_hostwait.py --update     rewrite tests/hostwait.txt DOWN
    python3 tests/unit/t_hostwait.py --update --raise "why"
                                                  ...and let counts rise/appear

WHY (docs/plans/soak-speed-2/flake.md). A soak runs eight emulators on four
cores, and a guest short of CPU runs FEWER guest seconds per host second. A
row that waits in host seconds for guest work - a `time.sleep(3)` after a
click, a `deadline = time.time() + 45` round a poll - hands the guest
whatever the host gave it meanwhile: green on an idle box, red under load,
and red as "the feature did not work", several frames from the cause. The
cure is the guest's clock or the guest's state (os88marty `until(guest=)`,
`quiesce`, `ui_done`, `key_taken`, `guest_sleep`; tests/os88qemu.py's ticks;
docs/plans/completed/SOAK-SPEED.md techniques 1, 4, 5, 9, 12-14).

WHAT IS COUNTED, per file, by KIND (an AST walk, so comments and strings
never count; a site with `# host:` and a reason on its line or the line above
is a genuine host wait and is not counted):

  sleep     `time.sleep(x)` / bare `sleep(x)` that is NOT a poll's interval -
            not inside a `while`, not inside a `for _ in ...` that can break
            or return, and not a literal under 0.1 s (event spacing). The
            same test as tests/unit/t_bochssleep.py, over every file.
  deadline  `time.time()` / `time.monotonic()` / `time.perf_counter()` in a
            DEADLINE: added to (`t + 45`), or subtracted inside a comparison
            (`time.time() - t0 > 30`). Elapsed time that is only printed is
            not counted. An outer budget round a guest poll IS counted: under
            load it is the line that fires, and the guest-clock budget
            (`until(..., guest=)`, tools/os88deadline.py's `Guest`) is the
            form that does not. A deadline and its test are two sites.
  bootpace  `launch(..., boot=<number>)` - a blind N-second pace (SOAK-SPEED 12).
  clickpace `click(..., settle=<number > 0>)` - N x 4.5 guest s (SOAK-SPEED 13).
  poll      `poll=<literal > 0.05>` - a host floor per wait (SOAK-SPEED 8).
  advance   `.advance(frames=N)` - a blind wait the waitlog does not log
            (SOAK-SPEED 4). `cycles=` is the guest's clock and is not counted.
  passwait  `required=False` / `need=False` - a wait whose PASS is its timeout
            (SOAK-SPEED 5): the whole budget on every green run.
  port      a fixed TCP port - a `"127.0.0.1:NNNN"` / `"localhost:NNNN"`
            literal, or `port=`/`addr=` given a literal - which two concurrent
            rows (or a row and an agent's bench) both take.
  tmppath   a fixed `/tmp/...` path or `build/qmp.sock` literal (docstrings
            excepted) - shared between concurrent rows. tests/os88qemu.py's
            OS88_QEMU_DIR and os88marty's run dirs are the private forms.

THE FIRST FOUR KINDS - sleep, deadline, port, tmppath - are the ones that
FLAKE: host seconds bounding guest work, or a host resource two runs share.
The rest are paced on the guest's clock already (launch's `boot=N` and
`click(settle=N)` are guest time, `advance` is frames) and cost host time on
every green run without ever going red for it; they are ratcheted so the
next row does not add one, and `--rank` orders by the flaky four.

What it does not count, on purpose: a `build/...` literal opened directly
(tests/unit/t_artpath.py owns that, against the frozen tree), and `settle`'s
or `until`'s own `limit=`/`quiet=`, which the harness already charges on the
guest's clock (os88marty `_Progress`, GUEST_BUDGET_RATIO).

THE REGISTRY, tests/hostwait.txt, is one line per (file, kind):
`<path> <kind> <count>  # <reason>`. A file's count above its line fails (a
new host wait: convert it, or mark a genuine one `# host:`); a count BELOW
its line fails too, so the registry never rots into a record of what used to
be true - `--update` lowers it, and is the one command after a fix. `--update`
never raises a count or adds a line unless `--raise "<reason>"` says why.

THE FAILING CASES (docs/WRITING-TESTS.md 1), tried when this was written:
add `time.sleep(2)` after a click in tests/dispcp.py and the row names the
file, the kind and the count; delete it and lower nothing, and the row says
the registry is stale and that `--update` is the fix.
"""
import ast
import os
import re
import sys
import warnings

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from harness import check, done                           # noqa: E402

ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
REGISTRY = os.path.join(ROOT, "tests", "hostwait.txt")
MARK = "# host:"
KINDS = ("sleep", "deadline", "bootpace", "clickpace", "poll", "advance",
         "passwait", "port", "tmppath")
CLOCKS = ("time", "monotonic", "perf_counter")
FLAKY = ("sleep", "deadline", "port", "tmppath")
PORTLIT = re.compile(r"^(127\.0\.0\.1|localhost):\d{2,5}$")


def files():
    """Every test script: tests/*.py and tests/<dir>/*.py, the unit gates
    and the suite registry excepted (they run no emulator)."""
    out = []
    tdir = os.path.join(ROOT, "tests")
    for dp, dn, fn in os.walk(tdir):
        dn[:] = sorted(d for d in dn if d not in ("unit", "__pycache__"))
        rel = os.path.relpath(dp, ROOT)
        if rel.count(os.sep) > 1:
            dn[:] = []
        for f in sorted(fn):
            if f.endswith(".py") and f != "suite.py":
                out.append(os.path.join(rel, f).replace(os.sep, "/"))
    return sorted(out)


def _clock_call(n, bare):
    """`time.time()` / `time.monotonic()` / `time.perf_counter()`, or the bare
    name when the file `from time import`s it."""
    if not isinstance(n, ast.Call) or n.args:
        return False
    f = n.func
    if isinstance(f, ast.Attribute) and f.attr in CLOCKS and \
            isinstance(f.value, ast.Name) and f.value.id == "time":
        return True
    return isinstance(f, ast.Name) and f.id in bare and f.id in CLOCKS


def _exits(loop):
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


def _poll(stack):
    for n in reversed(stack):
        if isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef, ast.Lambda)):
            return False
        if isinstance(n, ast.While):
            return True
        if isinstance(n, ast.For):
            return (isinstance(n.target, ast.Name) and n.target.id == "_"
                    and _exits(n))
    return False


def _num(n):
    return (isinstance(n, ast.Constant) and not isinstance(n.value, bool)
            and isinstance(n.value, (int, float)))


def _fname(call):
    f = call.func
    return f.attr if isinstance(f, ast.Attribute) else \
        f.id if isinstance(f, ast.Name) else ""


def scan(path):
    """[(line, kind, text)] for one file."""
    src = open(os.path.join(ROOT, path), errors="replace").read()
    lines = src.splitlines()
    try:
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            tree = ast.parse(src, path)
    except SyntaxError:
        return []
    bare = set()
    for n in ast.walk(tree):
        if isinstance(n, ast.ImportFrom) and n.module == "time":
            bare.update(a.asname or a.name for a in n.names)
    docs = set()
    hits = []

    def add(node, kind):
        ln = node.lineno
        near = lines[ln - 1] + (lines[ln - 2] if ln > 1 else "")
        if MARK not in near:
            hits.append((ln, kind, lines[ln - 1].strip()))

    parent = {}

    def up(node):
        """The enclosing nodes, innermost first."""
        n = parent.get(id(node))
        while n is not None:
            yield n
            n = parent.get(id(n))

    def in_cmp(node):
        for n in up(node):
            if isinstance(n, ast.Compare):
                return True
            if isinstance(n, (ast.stmt, ast.Lambda)):
                return False
        return False

    todo = [tree]
    DOC = (ast.Module, ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)
    while todo:
        node = todo.pop()
        kids = list(ast.iter_child_nodes(node))
        for c in kids:
            parent[id(c)] = node
        todo.extend(kids)
        if isinstance(node, DOC) and node.body:
            d = node.body[0]
            if isinstance(d, ast.Expr) and isinstance(d.value, ast.Constant):
                docs.add(id(d.value))
        if isinstance(node, ast.Call):
            nm = _fname(node)
            if nm == "sleep" and (
                    (isinstance(node.func, ast.Attribute) and
                     isinstance(node.func.value, ast.Name) and
                     node.func.value.id == "time") or
                    (isinstance(node.func, ast.Name) and "sleep" in bare)):
                a = node.args[0] if node.args else None
                if not ((_num(a) and a.value < 0.1) or
                        _poll(list(up(node))[::-1])):
                    add(node, "sleep")
            if not node.keywords:
                continue
            kw = dict((k.arg, k.value) for k in node.keywords if k.arg)
            if nm == "launch" and "boot" in kw and _num(kw["boot"]) \
                    and kw["boot"].value > 0:
                add(node, "bootpace")
            if nm in ("click", "dblclick") and "settle" in kw and \
                    _num(kw["settle"]) and kw["settle"].value > 0:
                add(node, "clickpace")
            if "poll" in kw and _num(kw["poll"]) and kw["poll"].value > 0.05:
                add(node, "poll")
            if nm == "advance" and "frames" in kw:
                add(node, "advance")
            for k in ("required", "need"):
                v = kw.get(k)
                if isinstance(v, ast.Constant) and v.value is False:
                    add(node, "passwait")
            for k in ("port", "addr"):
                v = kw.get(k)
                if v is not None and isinstance(v, ast.Constant) and (
                        (_num(v) and v.value >= 1024) or
                        (isinstance(v.value, str) and PORTLIT.match(v.value))):
                    add(node, "port")
        elif isinstance(node, ast.BinOp):
            if (_clock_call(node.left, bare) or
                    _clock_call(node.right, bare)) and (
                    isinstance(node.op, ast.Add) or
                    (isinstance(node.op, ast.Sub) and in_cmp(node))):
                add(node, "deadline")
        elif isinstance(node, ast.Compare):
            # `time.time() < end` / `end > time.time()`: the deadline's test
            if any(_clock_call(s, bare)
                   for s in [node.left] + list(node.comparators)):
                add(node, "deadline")
        elif isinstance(node, ast.Constant) and isinstance(node.value, str) \
                and id(node) not in docs:
            v = node.value
            if PORTLIT.match(v) and not isinstance(parent.get(id(node)),
                                                   ast.keyword):
                add(node, "port")
            elif v.startswith("/tmp/") or v == "build/qmp.sock":
                add(node, "tmppath")
    # one site per (line, kind): `while time.time() - t0 < 30` is one deadline
    seen, out = set(), []
    for h in hits:
        if (h[0], h[1]) not in seen:
            seen.add((h[0], h[1]))
            out.append(h)
    return out


def census():
    """{(path, kind): [(line, text)]}"""
    out = {}
    for p in files():
        for ln, kind, text in scan(p):
            out.setdefault((p, kind), []).append((ln, text))
    return out


def load():
    """{(path, kind): (count, reason)}, and the file's lines for rewriting."""
    reg, raw = {}, []
    if os.path.exists(REGISTRY):
        raw = open(REGISTRY).read().splitlines()
    for line in raw:
        body = line.split("#", 1)[0].split()
        if len(body) == 3 and body[1] in KINDS and body[2].isdigit():
            reason = line.split("#", 1)[1].strip() if "#" in line else ""
            reg[(body[0], body[1])] = (int(body[2]), reason)
    return reg, raw


HEADER = """\
# tests/hostwait.txt - host-timed waits a test file is still allowed
# (tests/unit/t_hostwait.py; docs/plans/soak-speed-2/flake.md)
#
# A row that paces the guest in HOST seconds is green on an idle box and red
# under a loaded soak. ONE LINE PER (file, kind): <path> <kind> <count>  # why
# The kinds are t_hostwait.py's docstring. The list turns ONE WAY: a count
# above its line fails (convert the wait, or mark a genuine host wait
# `# host: <why>`), and one below fails too until it is lowered:
#
#     python3 tests/unit/t_hostwait.py --update
#
# is the whole of that, and never raises a line; `--raise "<why>"` does.
"""


def write(entries):
    """entries: {(path, kind): (count, reason)}"""
    out = [HEADER]
    cur = None
    for (p, k) in sorted(entries):
        n, why = entries[(p, k)]
        if p != cur:
            out.append("")
            cur = p
        out.append("%-34s %-9s %3d  # %s" % (p, k, n, why))
    with open(REGISTRY, "w") as fh:
        fh.write("\n".join(out) + "\n")


def row_secs():
    """{script: total declared secs of the rows that run it}"""
    sys.path.insert(0, os.path.join(ROOT, "tests"))
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import suite
    out = {}
    for r in suite.rows():
        cmd = r.cmd if isinstance(r.cmd, (list, tuple)) else r.cmd.split()
        for tok in cmd:
            if str(tok).startswith("tests/") and str(tok).endswith(".py"):
                out[str(tok)] = out.get(str(tok), 0.0) + (r.secs or 0)
                break
    return out


def main(argv):
    cen = census()
    if "--list" in argv:
        for (p, k) in sorted(cen):
            for ln, text in cen[(p, k)]:
                print("%s:%d %s  %s" % (p, ln, k, text[:100]))
        return 0
    if "--rank" in argv:
        secs = row_secs()
        per = {}
        for (p, k), v in cen.items():
            per.setdefault(p, {})[k] = len(v)
        # ranked by the kinds that FLAKE under load (host seconds bounding
        # guest work); the rest (guest-clock waste: advance, poll,
        # passwait, clickpace) cost time on every run but cannot flake
        def nflake(p):
            return sum(v for k, v in per[p].items() if k in FLAKY)
        rank = sorted(per, key=lambda p: (-secs.get(p, 0) * nflake(p),
                                          -secs.get(p, 0) *
                                          sum(per[p].values())))
        print("%9s %6s %3s %-34s %s" % ("secs*flk", "secs", "flk", "file",
                                        "kinds"))
        for p in rank:
            n = nflake(p)
            print("%9.0f %6.0f %3d %-34s %s" % (
                secs.get(p, 0) * n, secs.get(p, 0), n, p,
                " ".join("%s=%d" % kv for kv in sorted(per[p].items()))))
        return 0
    reg, _ = load()
    if "--update" in argv:
        why = None
        if "--raise" in argv:
            i = argv.index("--raise")
            why = argv[i + 1] if i + 1 < len(argv) else ""
            check(len(why) >= 20, "--raise needs a reason of 20+ characters")
        new, refused = {}, []
        for key in sorted(set(reg) | set(cen)):
            have = len(cen.get(key, ()))
            old, reason = reg.get(key, (0, ""))
            if have > old and why is None:
                refused.append("%s %s %d > %d" % (key[0], key[1], have, old))
                have = old
            if have:
                new[key] = (have, reason if have <= old and reason else
                            why or reason)
        write(new)
        for r in refused:
            print("t_hostwait: NOT raised (needs --raise \"why\"): " + r)
        print("t_hostwait: wrote %s (%d lines, %d sites)" % (
            os.path.relpath(REGISTRY, ROOT), len(new),
            sum(v[0] for v in new.values())))
        return 1 if refused else 0
    for key in sorted(set(reg) | set(cen)):
        have = cen.get(key, [])
        allow, reason = reg.get(key, (0, ""))
        sites = ", ".join(str(ln) for ln, _ in have[:12])
        check(len(have) <= allow,
              "%s: %d `%s` host wait(s), %d allowed (lines %s) - wait on the "
              "guest's clock or state, or mark a genuine host wait `%s <why>`"
              % (key[0], len(have), key[1], allow, sites, MARK))
        check(len(have) >= allow,
              "%s: %d `%s` site(s) left, tests/hostwait.txt allows %d - lower "
              "it: python3 tests/unit/t_hostwait.py --update"
              % (key[0], len(have), key[1], allow))
        if allow:
            check(len(reason) >= 10, "%s %s: the registry line has no reason"
                  % key)
    print("t_hostwait: %d files, %d sites in %d (file, kind) lines" % (
        len(files()), sum(len(v) for v in cen.values()), len(cen)))
    done("t_hostwait")


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]) or 0)
