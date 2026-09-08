#!/usr/bin/env python3
"""Every `%include` a package pulls in is a PREREQUISITE of its `.bin` rule.

    python3 tests/unit/t_pkgdeps.py

WHY THIS IS A ROW. A shared include that is not on a rule's prerequisite list
does not rebuild what includes it, and `make` says "up to date" with total
confidence. Nothing fails, nothing is logged, and the floppy carries the
package as it was before the edit - which reads EXACTLY like the change not
working. It is the same failure mode `$(VIDSTAMP)` exists to stop one layer
up (the Makefile's own comment: "a knob in $(KNOBS) and not in this string is
silent and worse than useless"), and there was no equivalent guard for the
shared app-side libraries.

It was found by accident and it was not one rule. `apps/os88ui.inc` - the UI
library nine shipped packages are built on - was missing from **nine** of them
(chart, fractal, frotz, hello, mines, tank, taskmgr, loom, npbench), and
`apps/os88type.inc` from Word, cword and Audio. The measurement that turned it
up was an A/B of a five-byte deletion in `os88type.inc` that read as ZERO
because `word.bin` never reassembled.

The direction it fails in is chosen deliberately, and the Makefile already
argues for it at $(WEAVESRC): a prerequisite the translation unit does not
include costs one unnecessary rebuild, loudly and cheaply; one it does include
and does not list costs a stale package, silently. So this checks only that
nothing is MISSING and never that anything is surplus.

WHAT IT DOES NOT SEE. Includes inside `%if`/`%ifdef` are exempt, because the
same source builds more than one binary from different knob sets and a rule
that lists the other arm's file is noise (tracker builds trklog and trkscrl
that way). A generated include under `build/` is exempt for the same reason a
missing file is - it is not in the tree to resolve.

It parses the Makefile as TEXT and never invokes make. `make -p`, `-n` and
`-q` all evaluate the `$(shell ...)` beside $(VIDSTAMP), which DELETES
build/kernel.bin and every boot sector when the knob set differs (CLAUDE.md),
so a gate that shelled out to make would be a destructive command wearing a
read-only name.
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def slurp(path):
    """The Makefile with its `include` directives spliced in.

    `$(CC_RUNTIME)` lives in apps/cc/Makefile.inc, and a scanner that stops at
    the top-level file reports every C package as missing the whole C runtime.
    """
    txt = open(os.path.join(ROOT, path)).read()
    out = []
    for ln in txt.split("\n"):
        m = re.match(r"^-?include\s+(\S+)$", ln)
        if m:
            try:
                out.append(slurp(m.group(1)))
                continue
            except OSError:
                pass
        out.append(ln)
    return "\n".join(out)


def unfold(text):
    out, buf = [], ""
    for ln in text.split("\n"):
        if ln.endswith("\\"):
            buf += ln[:-1] + " "
            continue
        out.append(buf + ln)
        buf = ""
    if buf:
        out.append(buf)
    return out


def variables(lines):
    v = {}
    for ln in lines:
        if ln.startswith("\t"):
            continue
        m = re.match(r"^([A-Za-z_][A-Za-z0-9_]*)\s*:?\??=\s*(.*)$", ln)
        if m:
            v.setdefault(m.group(1), m.group(2).strip())
    return v


def expand(text, v, depth=0):
    """Enough of make's expansion to read a prerequisite list.

    `$(wildcard ...)` is evaluated for real - it is read-only, and three rules
    use it deliberately (WEAVESRC's comment says why). An unknown name expands
    to nothing, which is what make does.
    """
    if depth > 8:
        return text

    def one(m):
        body = m.group(1)
        if body.startswith("wildcard "):
            out = []
            for pat in body[len("wildcard "):].split():
                out += sorted(glob.glob(os.path.join(ROOT, pat)))
            return " ".join(os.path.relpath(p, ROOT) for p in out)
        if body == "BUILD":
            return "build"
        if body.startswith(("call ", "if ", "addprefix ", "shell ")):
            return ""
        return expand(v.get(body, ""), v, depth + 1)

    prev = None
    while prev != text:
        prev = text
        text = re.sub(r"\$\(([^()]*)\)", one, text)
    return text


def rules(lines, v):
    """target -> (prerequisites, recipe lines), accumulated across rule lines."""
    out, cur = {}, None
    for ln in lines:
        if ln.startswith("\t"):
            if cur:
                out[cur][1].append(ln)
            continue
        m = re.match(r"^([^\t=#][^:=]*?):(?!=)(.*)$", ln)
        if not m:
            cur = None
            continue
        tgts = expand(m.group(1), v).split()
        pre = expand(m.group(2).split("|")[0], v).split()
        for t in tgts:
            out.setdefault(t, [[], []])
            out[t][0].extend(pre)
        cur = tgts[0] if tgts else None
    return out


def includes(src, ipaths, seen, cond):
    """The transitive %include set, and the names that were conditional."""
    if src in seen:
        return
    seen.add(src)
    try:
        txt = open(os.path.join(ROOT, src)).read()
    except OSError:
        return
    depth = 0
    for ln in txt.split("\n"):
        t = ln.strip()
        if re.match(r"%if", t):
            depth += 1
        elif re.match(r"%endif", t):
            depth = max(0, depth - 1)
        m = re.match(r'\s*%include\s+"([^"]+)"', ln)
        if not m:
            continue
        name = m.group(1)
        if depth:
            cond.add(name)
        for c in [os.path.join(os.path.dirname(src), name)] + \
                 [os.path.join(p, name) for p in ipaths]:
            c = os.path.normpath(c)
            if os.path.exists(os.path.join(ROOT, c)):
                includes(c, ipaths, seen, cond)
                break


def main():
    lines = unfold(slurp("Makefile"))
    v = variables(lines)
    bad = 0
    checked = 0
    for tgt, (pre, recipe) in sorted(rules(lines, v).items()):
        if not tgt.endswith(".bin") or not tgt.startswith("build/"):
            continue
        rec = expand(" ".join(recipe), v)
        m = re.search(r"nasm[^\n]*?-o\s+\S+\s+(\S+\.asm)", rec)
        if not m:
            continue                      # a C package: its .asm is generated
        src = m.group(1)
        if not os.path.exists(os.path.join(ROOT, src)):
            continue
        ipaths = re.findall(r"-I\s*(\S+)", rec)
        seen, cond = set(), set()
        includes(src, ipaths, seen, cond)
        seen.discard(src)
        checked += 1
        have = set(os.path.normpath(p) for p in pre)
        miss = sorted(x for x in seen
                      if os.path.normpath(x) not in have
                      and os.path.basename(x) not in cond)
        if miss:
            bad += 1
            print("FAIL %-24s does not depend on: %s" % (tgt, " ".join(miss)))
    if bad:
        print("t_pkgdeps: %d of %d rule(s) would not rebuild when a shared "
              "include changes - add the file to the rule's prerequisites"
              % (bad, checked))
        return 1
    print("t_pkgdeps: %d package rules, every %%include is a prerequisite"
          % checked)
    return 0


if __name__ == "__main__":
    sys.exit(main())
