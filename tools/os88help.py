#!/usr/bin/env python3
"""`make help`: the user-facing targets and knobs, read out of the Makefile.

    python3 tools/os88help.py            print the list (what `make help` runs)
    python3 tools/os88help.py --check    exit 1 if the markers and CLAUDE.md /
                                         AGENTS.md disagree (the fast-tier gate)

THE MARKER. One line beside the rule or the knob's definition:

    #> [Section] target :: one line saying what it does
    #> [Knobs] NAME=values :: one line saying what it does

The Makefile is ~20,000 lines and its comment blocks are design notes, so a
reader cannot skim it for "what can I type". The marker is the skimmable
line; the comment block under it stays the authority on the detail.

THE 86Box MACHINES are found rather than marked: every rule whose recipe runs
$(BOX) is a machine target, so a new vm/ profile is listed the day its rule
lands and nobody writes a line for it.

--check is what stops this drifting:
  - every target marker names a rule that exists (in the Makefile or a file it
    includes), and every knob marker names a variable the Makefile reads;
  - no name is marked twice;
  - every `make <target>` and every knob row CLAUDE.md's and AGENTS.md's
    Commands sections mention is marked - so the short list there and this
    one cannot say different things.
"""
import os
import re
import sys
import textwrap

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ORDER = ["Start", "Build", "Run", "Test", "Kernels", "Applications",
         "Gate disks", "Media and bundles", "Knobs"]
MARK = re.compile(r"^#> \[([^\]]+)\] (.+?) :: (.+)$")
RULE = re.compile(r"^([A-Za-z0-9][\w.+-]*(?: [\w.+-]+)*)\s*:(?!=)")


def makefiles():
    """The Makefile and every file it `include`s, in order."""
    out = ["Makefile"]
    with open(os.path.join(ROOT, "Makefile"), encoding="utf-8") as f:
        for line in f:
            m = re.match(r"^-?include\s+(\S+)\s*$", line)
            if m and "$" not in m.group(1):
                out.append(m.group(1))
    return out


def scan():
    marks, rules, machines, text = [], set(), [], []
    for name in makefiles():
        with open(os.path.join(ROOT, name), encoding="utf-8") as f:
            lines = f.read().split("\n")
        text.append("\n".join(lines))
        for i, line in enumerate(lines):
            m = MARK.match(line)
            if m:
                marks.append((m.group(1), m.group(2), m.group(3),
                              "%s:%d" % (name, i + 1)))
                continue
            m = RULE.match(line)
            if not m or line.startswith("\t"):
                continue
            targets = m.group(1).split()
            rules.update(targets)
            body = []
            j = i + 1
            while j < len(lines) and lines[j].startswith("\t"):
                body.append(lines[j])
                j += 1
            if any("$(BOX)" in b for b in body):
                machines.extend(t for t in targets if t not in machines)
    return marks, rules, machines, "\n".join(text)


def knob_names(name):
    """`ADLIB=1 / SB16=1` -> ['ADLIB', 'SB16']"""
    return re.findall(r"([A-Z][A-Z0-9_]*)=", name)


def doc_mentions(path):
    """Targets and knobs a Commands section names."""
    try:
        with open(os.path.join(ROOT, path), encoding="utf-8") as f:
            s = f.read()
    except FileNotFoundError:
        return set(), set()
    m = re.search(r"^## Commands\n(.*?)^## ", s, re.M | re.S)
    if not m:
        return set(), set()
    sec = m.group(1)
    targets = set(re.findall(r"(?:^|`)make ([a-z0-9][\w.+-]*)", sec, re.M))
    knobs = set(re.findall(r"^\| `([A-Z][A-Z0-9_]*)=", sec, re.M))
    return targets, knobs


def check():
    marks, rules, machines, text = scan()
    bad = []
    seen = {}
    marked_t, marked_k = set(), set()
    for sec, name, _desc, where in marks:
        if sec not in ORDER:
            bad.append("%s: unknown section [%s]" % (where, sec))
        keys = knob_names(name) if sec == "Knobs" else [name]
        for k in keys:
            if k in seen:
                bad.append("%s: %s is already marked at %s" % (where, k, seen[k]))
            seen[k] = where
        if sec == "Knobs":
            for k in keys:
                marked_k.add(k)
                if not re.search(r"\$\(%s\)|\b(?:ifdef|ifndef)\s+%s\b" % (k, k),
                                  text):
                    bad.append("%s: knob %s is never read by the Makefile"
                               % (where, k))
        else:
            marked_t.add(name)
            if name != "help" and name not in rules:
                bad.append("%s: no rule for target `%s`" % (where, name))
    if "help" not in marked_t:
        bad.append("Makefile: `help` has no marker")
    for doc in ("CLAUDE.md", "AGENTS.md"):
        targets, knobs = doc_mentions(doc)
        for t in sorted(targets - marked_t - set(machines)):
            bad.append("%s names `make %s`, which has no #> marker" % (doc, t))
        for k in sorted(knobs - marked_k):
            bad.append("%s names knob %s=, which has no #> marker" % (doc, k))
    return bad


def show():
    marks, _rules, machines, _text = scan()
    width = 24
    print("os8088 make targets. The comment block beside each rule in the "
          "Makefile has the detail;")
    print("SPEC.md is the authority on what each thing IS.")
    for sec in ORDER:
        rows = [(n, d) for s, n, d, _w in marks if s == sec]
        if not rows:
            continue
        print("\n%s%s" % (sec, " (make VAR=value <target>)" if sec == "Knobs"
                           else ""))
        for name, desc in rows:
            lead = "  %-*s " % (width, name)
            if len(lead) > width + 3:
                print(lead.rstrip())
                lead = " " * (width + 3)
            print(textwrap.fill(desc, 79, initial_indent=lead,
                                subsequent_indent=" " * (width + 3),
                                break_on_hyphens=False))
    if machines:
        print("\n86Box machines (make <name>; vm/<name>/ is the profile, and "
              "the comment above")
        print("each rule says what it is for - docs/86BOX.md):")
        print(textwrap.fill(" ".join(sorted(machines)), 79,
                            initial_indent="  ", subsequent_indent="  ",
                            break_on_hyphens=False))


def main():
    if "--check" in sys.argv:
        bad = check()
        for b in bad:
            print("os88help: " + b)
        if bad:
            print("os88help: %d problem(s) - add or fix a `#> [Section] name "
                  ":: description` line beside the rule" % len(bad))
            return 1
        print("os88help: make help is current")
        return 0
    show()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except BrokenPipeError:           # `make help | head`
        sys.stderr.close()
        sys.exit(0)
