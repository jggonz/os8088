#!/usr/bin/env python3
"""Documentation consistency gate. Three things drift silently and have:

  1. a section citation that names a heading which does not exist;
  2. an API slot number in prose that is not a slot apps/os88api.inc defines;
  3. TWO HEADINGS WITH THE SAME NUMBER, which makes every citation of that
     number ambiguous and every one of them pass.

The second is the nastier one: after a renumbering, a stale citation is often
still a VALID slot - just a different routine - so nothing catches it and the
prose quietly documents the wrong call. Run from the repo root; exit 1 on any
finding.

THE THIRD WAS INVISIBLE HERE FOR THREE RELEASES, and the reason is one word:
`headings()` returned a SET, so a number defined twice is indistinguishable
from a number defined once and check 1 passes every citation of it. Three
collisions accumulated that way and none of them was noticed by a gate that
runs in the default build - SPEC.md 22.6 across PRs #54/#61 (10 references
meant one section and 1 meant the other), 39.2.1 across #73/#81, and 52.11.4
twice inside the SAME squash. Upstream squash-merges branches that were
written in parallel (docs/UPSTREAM.md), so two authors appending "the next
free number" to one section is the NORMAL case rather than a slip, and the
number they pick is the one thing a clean textual merge cannot conflict on.

A citation names a DOCUMENT as well as a number, and this used to assume the
document was always SPEC.md - which is true in every kernel source and in
CLAUDE.md, where a bare "§11.3" means SPEC.md by convention, and false in the
plan documents under docs/, which number their own sections and self-refer.
That mismatch was 32 of 34 findings: ASSOC-PLAN §2.5.1 is that document's own
section, and SPEC.md has no such heading. So the target is resolved first and
checked second, three ways:

  * "SPEC.md §N"        -> SPEC.md, always;
  * "<DOC> §N"          -> that document, where <DOC> is any tracked .md file
                           named in full (`docs/ASSOC-PLAN.md`) or by its
                           upper-case stem (ASSOC-PLAN, SPEC);
  * a bare "§N"         -> SPEC.md, OR the containing file's own headings when
                           that file numbers its sections.

The bare case is a union rather than a choice because the plan documents use
one notation for both meanings - DISK-PERF-PLAN §3.1 and SPEC.md §18.9 are
both cited bare, from that one file - so the most that can be checked is that
the number resolves somewhere it could point. Every other file has no
numbered headings of its own, so for the kernel, CLAUDE.md and the SPEC itself
the union is exactly SPEC.md and nothing is weakened.

A document that numbers NO headings cannot be checked against - docs/TESTING.md
is prose with "SPEC.md §9.5" in a title - so a citation naming one is skipped
rather than failed. Guard against loosening that into "skip what I cannot
parse": the skip is keyed on the named document having no numbers at all, not
on the number being missing from it.
"""
import re
import subprocess
import sys

# "Part N" is PERFORMANCE.md's numbering; SPEC.md and the plans use a bare N.
HEAD = re.compile(r"^#{2,6}\s+(?:Part\s+)?(\d{1,2}(?:\.\d+)*)\.?\s")
CITE = re.compile(r"(?:SPEC\.md\s+§?|§)(\d{1,2}(?:\.\d+)*(?:/\d{1,2}(?:\.\d+)*)*)")
# The document a citation is qualified with, immediately before it. Upper-case
# only, so an ordinary word ending a sentence cannot be read as a file name -
# and the whole token is taken, so "REDRAW-SPEC.md" is never "SPEC.md".
QUAL = re.compile(r"`?((?:docs/)?(?:[A-Z][A-Z0-9-]*\.md|[A-Z][A-Z0-9-]*))`?\s+$")
SLOT = re.compile(r"(?:slot|API) (0x0[0-9A-Fa-f]{3})")   # both spellings: the
# renumbering left three "API 0x..." header comments behind (wm_sizable,
# wm_fullscreen, menu_win_set) because only "slot 0x..." was being checked, and
# a wrong number in a routine's own header is the one place an author looks.

# Numbered RULES inside a section, not sections: SPEC.md 1 and 29.2 are lists.
# Plus the reserved-range note in the preamble, which names a span, not a
# heading.
RULE_REFS = {"1.6", "1.7", "29.2.8", "45", "49"}
# Slot numbers prose may still name that apps/os88api.inc deliberately does
# not define. Nothing is held EMPTY any more (SPEC.md 20.3) - the five cells
# that were are either filled (0x00F8/0x0100 went to the sound driver) or
# gone, closed up when the numbering settled.
#
# THE SET IS EMPTY NOW, and that is SPEC.md 20.3.1 working rather than an
# omission. It was kept for 0x01E8 - RETIRED at SPEC.md 18.4.1, where
# OSAPI_FILE_READ absorbed readbig - and a retired cell is a FREE LIST, not a
# headstone: 0x01F0 went to wm_onmouseup (13.7) and 0x01E8 to OSAPI_VOL_KIND
# (18.7.2). Both are published names again, so prose naming either number is
# checkable the ordinary way. Put a number back here only for a cell the SDK
# deliberately does not define, and say which section retired it.
HELD = set()


def dupes(path):
    """{number: [line, ...]} for every number this document heads TWICE.

    Deliberately not built on headings() - that returns a set, and a set is
    exactly what hid this. The two must stay separate: a future "just reuse
    headings()" is the bug coming back.
    """
    seen = {}
    try:
        with open(path, encoding="utf-8") as fh:
            for n, line in enumerate(fh, 1):
                m = HEAD.match(line)
                if m:
                    seen.setdefault(m.group(1), []).append(n)
    except OSError:
        return {}
    return {k: v for k, v in seen.items() if len(v) > 1}


def headings(path):
    """The numbered headings a document defines, as a set of strings.

    Unreadable is an empty set rather than a raise: git lists what is TRACKED,
    which mid-rebase or mid-delete is not always what is on disk, and this runs
    in the default build now. Empty fails safe in the direction that matters -
    a citation into SPEC.md is then reported rather than passed.
    """
    try:
        with open(path, encoding="utf-8") as fh:
            text = fh.read()
    except OSError:
        return set()
    return {m.group(1) for m in map(HEAD.match, text.split("\n")) if m}


def main() -> int:
    # The file list is git's, so a source tree that is not a checkout cannot be
    # checked at all. Say so and pass, rather than failing a build over it -
    # this runs in the default `make` now, and a tarball is a legitimate way to
    # have these sources. It reports SKIPPED and never a clean bill of health.
    try:
        tracked = subprocess.run(["git", "ls-files"], check=True,
                                 capture_output=True, text=True).stdout.split()
    except (OSError, subprocess.CalledProcessError):
        print("checkdocs: not a git checkout - SKIPPED")
        return 0
    tracked = [f for f in tracked if not f.startswith("build/")]

    docs = {f: headings(f) for f in tracked if f.endswith(".md")}
    # ASSOC-PLAN -> docs/ASSOC-PLAN.md, SPEC -> SPEC.md, and so on.
    stems = {f.rsplit("/", 1)[-1][:-3].upper(): f for f in docs}
    spec = docs["SPEC.md"]

    api = {m.group(1).lower() for m in
           re.finditer(r"KERNEL_SEG:(0x0[0-9A-Fa-f]{3})",
                       open("apps/os88api.inc", encoding="utf-8").read())}

    files = [f for f in tracked
             if f.rsplit(".", 1)[-1] in ("md", "inc", "asm", "py")
             or f == "Makefile"]

    bad = []
    # ...before any citation is resolved, because a duplicate makes the
    # resolution meaningless rather than wrong: both headings exist, so every
    # citation of the number passes and points at whichever the reader finds
    # first.
    for path in sorted(docs):
        for num, lines in sorted(dupes(path).items()):
            bad.append("%s: §%s heads %d sections (lines %s) - a citation of "
                       "it cannot say which" %
                       (path, num, len(lines),
                        ", ".join(str(x) for x in lines)))
    for path in files:
        try:
            text = open(path, encoding="utf-8").read()
        except (UnicodeDecodeError, IsADirectoryError, FileNotFoundError):
            continue
        own = docs.get(path, set())        # empty for every non-.md file
        for n, line in enumerate(text.split("\n"), 1):
            for m in CITE.finditer(line):
                if m.group(0).startswith("SPEC.md"):
                    target = "SPEC.md"
                else:
                    q = QUAL.search(line[:m.start()])
                    stem = q.group(1).rsplit("/", 1)[-1] if q else ""
                    target = stems.get(stem[:-3].upper() if stem.endswith(".md")
                                       else stem.upper())
                for num in m.group(1).split("/"):
                    if target:
                        if not docs[target]:
                            continue       # that document numbers nothing
                        if num in docs[target]:
                            continue
                        if target == "SPEC.md" and num in RULE_REFS:
                            continue
                        bad.append(f"{path}:{n}: {target} §{num} is not a "
                                   "heading")
                    elif num not in spec and num not in RULE_REFS \
                            and num not in own:
                        bad.append(f"{path}:{n}: SPEC.md §{num} is not a "
                                   "heading")
            for m in SLOT.finditer(line):
                num = m.group(1).lower()
                if num not in api and num not in HELD:
                    bad.append(f"{path}:{n}: 'slot {m.group(1)}' is not an "
                               "os88api.inc slot")
    for b in bad:
        print(b, file=sys.stderr)
    print(f"checkdocs: {len(spec)} headings, {len(api)} slots, "
          f"{len(bad)} problem(s)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
