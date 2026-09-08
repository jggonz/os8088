#!/usr/bin/env python3
"""Generate docs/INDEX.md - the "does this already exist?" index.

WHY THIS EXISTS, stated plainly because the reason is a mistake rather than a
plan. SPEC.md is ~67,000 lines and answers any question you know to ask it.
Grep answers a narrow question fast. Neither answers the question you have
BEFORE you write code, which is not "how does OSAPI_ABOUT_SET work" but "is
there already a way to put my application's name in the menu bar". Sheet and
Chart shipped without an About handler for exactly that reason: seventeen other
packages declared one, the slot was documented in SPEC.md 12.2, and a Help menu
got invented instead because nothing pointed at the question.

So this indexes CAPABILITIES, grouped by subject, each pointing at the SPEC
section and the API slot or include that provides it. It is GENERATED from the
tree - the slots from apps/os88api.inc, the sections from SPEC.md's own
headings, the shared includes from their exported labels, the packages from the
Makefile - so it cannot describe something that is not there, and it cannot go
quietly stale while the tree moves.

  python3 tools/os88index.py            rewrite docs/INDEX.md
  python3 tools/os88index.py --check    exit 1 if it would change (the gate)

The --check mode runs in the default build beside checkdocs.py. A stale index
is a build failure for the same reason a stale citation is: an index nobody can
trust is worse than no index, because it is consulted and believed.
"""

import os
import re
import subprocess
import sys
import unicodedata

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "docs", "INDEX.md")

# --- the subject groups -------------------------------------------------------
# Each is (title, SPEC sections, [slot-name prefixes or exact names]). The
# ORDER is the order a package author meets them, not the API's own numbering:
# you draw before you handle a click, and you open a window before either.
#
# Every slot must land in exactly one group: a prefix is tried against the
# groups in this order and the first match wins, and a slot no prefix claims
# fails the run by name (see build()) rather than being filed under a heading
# nobody reads. Add the prefix here when a slot is added there.
GROUPS = [
    ("Windows", ["11", "20"],
     ["WM_", "ABOUT_SET"]),
    ("Packages and the desktop", ["21", "26"],
     ["PKG_", "DESK_"]),
    ("Menus and the menu bar", ["12", "59"],
     ["MENU_", "TOAST"]),
    ("Drawing", ["5", "25", "32", "39", "76"],
     ["GFX_", "SET_COLOR", "ICON_"]),
    ("Text and fonts", ["6", "83"],
     ["FONT_"]),
    ("Input - keyboard and mouse", ["9", "10", "13"],
     ["KEY_", "MOUSE", "EVQ_"]),
    ("Files and volumes", ["18", "19", "22", "38", "54", "20.13"],
     ["FILE_", "VOL_", "FS_", "ASSOC", "ARG_FILE", "BATCH_", "DECOMP"]),
    ("Memory", ["2", "41", "50", "66"],
     ["MEM_", "XMEM_", "CLAIM_SNAPSHOT", "SYS_KB"]),
    ("Tasks, timing and the clock", ["7", "8", "37"],
     ["TASK_", "GET_TICKS", "BOOT_TICKS"]),
    ("Sound", ["34", "35"],
     ["SND_"]),
    ("The system - CPU, video, clipboard, drivers", ["31", "51", "55", "57", "60"],
     ["CPU_INFO", "VIDEO", "CLIP_", "DRV_", "SYS_SNAPSHOT", "REBOOT"]),
    ("Networking", ["62", "70", "71", "72", "77"],
     []),
    ("Fullscreen and the screen saver", ["53", "64", "79"],
     ["FSX_", "FULLSCREEN"]),
    ("Randomness and maths", ["84"],
     ["RAND", "SRAND"]),
]

# What a group with no slots of its own says instead of a table.
GROUP_NOTES = {
    "Networking":
        "No kernel slot: a package reaches the network through a DRIVER "
        "(`OSAPI_DRV_CALL`, SPEC.md 20.11), with the verbs in "
        "`drivers/net/netpkg.inc` and the driver found by `apps/os88sock.inc`.",
}

# Shared includes a package opts into. The blurb is this file's own - it is the
# one thing here not extracted, because "what is this FOR" is not in the source
# in a form worth parsing, and a wrong blurb is visible where a wrong slot is
# not.
#
# The NAMES are checked against the tree by check_includes() below, because a
# hand-written list is exactly what goes quietly stale: a new shared include was
# added and this list was not, so the index that exists to answer "does this
# already exist?" answered no about a file that did.
INCLUDES = [
    ("os88ui.inc", "13, 75",
     "Buttons, check boxes, radio dots, scroll bars, group boxes, the "
     "standard alert, the standard About card and the drop-down. Opt into "
     "the alert with `%define OS88UI_ALERT`, the About card with `%define "
     "OS88UI_ABOUT`, the scroll bar with `%define OS88UI_SCROLL` and its "
     "thumb-drag half with `%define OS88UI_SBDRAG`, and the drop-down - one "
     "pick out of a short list, a Macintosh popup's gesture - with `%define "
     "OS88UI_DROP` (SPEC.md 13.14)."),
    ("os88line.inc", "83",
     "A one-line text field: caret, horizontal scroll, focus, click-to-position "
     "and the editing keys. The caller owns a 20-byte block."),
    ("os88text.inc", "83",
     "The multi-line sibling of os88line.inc. Enter inserts a newline; no wrap, "
     "no selection, no undo."),
    ("os88chart.inc", "82",
     "A 4bpp offscreen canvas and all seven chart types - area, bar, column, "
     "line, pie, scatter, combination - plus a BMP writer. Shared by CHART.O88 "
     "and Sheet's chart window."),
    ("os88fp.inc", "84",
     "IEEE-754 double arithmetic in software, with an 8087 path chosen at run "
     "time. Parse, format, add, subtract, multiply, divide, compare, sqrt, "
     "trunc, floor, round."),
    ("os88img.inc", "93",
     "Picture decoders: .PIX, .BMP and .PCX into the packed 4bpp that "
     "OSAPI_GFX_BLIT4 takes. Owns no state - the caller passes a block "
     "in SI. 8-bit files are refused by name, not approximated."),
    ("os88sock.inc", "20.11, 62.11, 72",
     "Finding the socket driver: `net_find` answers CF=1 when neither "
     "ETHER.DRV nor NET.DRV is loaded and sets `NET_CLASS` otherwise, so every "
     "`OSAPI_DRV_CALL` after it addresses the right class. The verbs "
     "themselves are `drivers/net/netpkg.inc`'s."),
    ("os88pit.inc", "72.15.1",
     "`pit_now`: a 32-bit clock in 838ns units off the 8253 and the BIOS tick, "
     "good for an hour before it wraps. Sub-tick timing for a profiler."),
    ("os88type.inc", "6.3, 6.5",
     "Proportional type: composes a row of glyphs from an `.F88` face into a "
     "1bpp band in your own RAM and puts it up with one `OSAPI_GFX_BLIT1`."),
    ("os88parts.inc", "20.12",
     "Package parts: named, sized parts inside one `.O88` - claimed, loaded on "
     "demand, optionally into XMS, and refused with an arithmetic the package "
     "states itself. A package over 64KB is still a package."),
]


def check_includes():
    """Every apps/os88*.inc a package opts into must have a row here.

    os88api.inc is excluded: it is not opted into, it is the API itself and
    every package includes it. Anything else that appears in apps/ and not in
    INCLUDES - or the reverse - is a stale index, and this says so by name
    rather than leaving it to be noticed.
    """
    on_disk = {f for f in os.listdir(os.path.join(ROOT, "apps"))
               if f.startswith("os88") and f.endswith(".inc")
               and f != "os88api.inc"}
    listed = {name for name, _, _ in INCLUDES}
    missing = sorted(on_disk - listed)
    ghost = sorted(listed - on_disk)
    if missing or ghost:
        for f in missing:
            print("os88index: apps/%s is in the tree and not in INCLUDES" % f)
        for f in ghost:
            print("os88index: INCLUDES lists %s, which is not in apps/" % f)
        return False
    return True


def anchor(num, title):
    """GitHub's heading anchor for "## <num>. <title>".

    github-slugger's rule: lowercase, drop every character that is not a
    letter, digit, mark, space, `-` or `_`, then turn EACH space into a
    hyphen. Spaces are not collapsed, so "wm.inc — windows" is
    `wminc--windows` - the em dash goes and both spaces around it stay.
    """
    text = ("%s. %s" % (num, title)).lower()
    kept = "".join(c for c in text
                   if c.isalnum() or c in " -_"
                   or unicodedata.category(c).startswith("M"))
    return kept.replace(" ", "-")


def read(path):
    with open(os.path.join(ROOT, path), encoding="utf-8") as f:
        return f.read()


def spec_headings():
    """{number: title} for every numbered heading in SPEC.md."""
    out = {}
    for m in re.finditer(r"^#{2,6}\s+([0-9]+(?:\.[0-9]+)*)\.?\s+(.+?)\s*$",
                         read("SPEC.md"), re.M):
        out.setdefault(m.group(1), m.group(2).strip())
    return out


def api_slots():
    """[(name, slot, its comment)] in file order.

    A slot's comment runs on over following comment-only lines, and the FIRST
    line is usually just the register list - "BX = a window of YOURS; the gfx"
    cut there says nothing. So the continuation is joined and the whole thing
    trimmed to a sentence.
    """
    lines = read("apps/os88api.inc").split("\n")
    out = []
    for i, ln in enumerate(lines):
        m = re.match(r"^%define\s+(OSAPI_[A-Z0-9_]+)\s+KERNEL_SEG:(0x[0-9A-Fa-f]+)"
                     r"\s*(?:;\s*(.*))?$", ln)
        if not m:
            continue
        note = (m.group(3) or "").strip()
        for cont in lines[i + 1:]:
            c = re.match(r"^\s+;\s?(.*)$", cont)
            if not c:
                break
            note += " " + c.group(1).strip()
        out.append((m.group(1), m.group(2), " ".join(note.split())))
    return out


def trim(note, width=150):
    """The first `width` characters of a slot's comment, cut at a sentence.

    A slot's comment opens with its register list and the prose about what
    the call is FOR follows it, so the cut is made at the last sentence, clause
    or dash boundary inside the width (never in its first third) and marked
    with an ellipsis. The full text is in apps/os88api.inc.
    """
    note = " ".join(note.replace("|", r"\|").split())
    if len(note) <= width:
        return note
    cut = note[:width]
    for stop in (". ", "; ", " - "):
        if stop in cut and cut.index(stop) > width // 3:
            return cut[:cut.rindex(stop)].rstrip(" -;.") + "..."
    return (cut[:cut.rindex(" ")] if " " in cut else cut) + "..."


def make_vars(mk):
    """{NAME: value} for every plain variable assignment in the Makefile.

    Continuations are already joined. `+=` appends; the last assignment of a
    name wins, which is right for the two APPS_DATA arms and wrong for
    nothing this file reads."""
    out = {}
    for m in re.finditer(r"^([A-Za-z0-9_]+)\s*([:?+]?)=\s*(.*)$", mk, re.M):
        name, op, val = m.group(1), m.group(2), m.group(3).strip()
        out[name] = (out.get(name, "") + " " + val) if op == "+" else val
    return out


def make_expand(text, mvars, depth=12):
    """`text` with every `$(NAME)` that names a plain variable replaced by its
    value, recursively. A function call - `$(filter-out a,b)`, `$(addprefix
    p,l)` - keeps its ARGUMENTS and loses the function, which over-includes
    (a filter-out's filtered names survive) and never drops a name; the caller
    only wants the set of names reachable at all. `$(BUILD)` is left alone
    so the `$(BUILD)/x.o88` shape stays greppable."""
    for _ in range(depth):
        def sub(m):
            inner = m.group(1)
            if inner == "BUILD":
                return m.group(0)
            if inner in mvars:
                return mvars[inner]
            if " " in inner:                   # a function call
                return inner.split(" ", 1)[1].replace(",", " ")
            return ""
        new = re.sub(r"\$\(([^()$]+)\)", sub, text)
        if new == text:
            break
        text = new
    return text


def packages():
    """[(NAME, source, ships)] for every package the Makefile builds.

    TWO SHAPES, because there are two, and reading only the first left five
    packages out of an index whose whole claim is that it cannot drift:

      * an assembly package has an open-coded `$(BUILD)/x.bin:` rule whose
        prerequisites name `apps/x/x.asm` - directly, or through a variable
        such as `$(FROTZSRC)`, which is why the prerequisites are expanded
        before they are searched - and its name is in its own `OS88_HEADER`;
      * a C package (SPEC.md 73) is `$(eval $(call CC_PACKAGE,name,dir))` -
        the rule is generated, so there is no `.bin:` line to find - and its
        name is the `CC_PKG_NAME` in the same shim. LOOM open-codes its rules
        for an include-path reason, so it is found the first way.

    `ships` is whether `$(BUILD)/<stem>.o88` is reachable from ALLAPPSFILES,
    the payload of `make allapps` and the live media (SPEC.md 19.10, 80) -
    the one list every shipped application is on. What `all` builds and no
    disk carries (WIREFRAME, SPEC.md 78.9) or a test only its own target
    builds (FPTEST) is still a worked example, and the column says so.
    """
    mk = re.sub(r"\\\n\s*", " ", read("Makefile"))
    mvars = make_vars(mk)
    shipped = set(re.findall(r"\$\(BUILD\)/([a-z0-9]+)\.o88",
                             make_expand("$(ALLAPPSFILES)", mvars)))
    srcs = []
    for m in re.finditer(r"^\$\(BUILD\)/([a-z0-9]+)\.bin:([^\n]*)$", mk, re.M):
        got = re.search(r"(apps/[a-z0-9]+/[a-z0-9]+\.asm)",
                        make_expand(m.group(2), mvars))
        if got:
            srcs.append((m.group(1), got.group(1)))
    for m in re.finditer(r"CC_PACKAGE,([a-z0-9]+),([a-z0-9]+)", mk):
        srcs.append((m.group(1), "apps/%s/%s.asm" % (m.group(2), m.group(1))))
    out = {}
    for stem, src in srcs:
        if not os.path.exists(os.path.join(ROOT, src)):
            continue
        text = read(src)
        hdr = re.search(r"OS88_HEADER\s+'([^']+)'", text) \
            or re.search(r"CC_PKG_NAME\s+'([^']+)'", text)
        if hdr:
            # One source can be built under several stems (calc.bin and the
            # calcref.bin gate, notepad.bin and its small build): one row,
            # and it ships if any of them does.
            key = (hdr.group(1), src)
            out[key] = out.get(key, False) or stem in shipped
    return sorted((n, s, v) for (n, s), v in out.items())


def own_specs():
    """{NAME: 'X-SPEC'} for a package whose contract is a document of its own.

    Read out of each docs/*-SPEC.md's own H1 rather than from a table here:
    C64-SPEC and WEAVE-SPEC exist because SPEC.md is not the right home for
    them (C64-SPEC 1, WEAVE-SPEC's own preamble), and a hand-written mapping
    in a GENERATED index would be the one line in it that can go stale."""
    out = {}
    d = os.path.join(ROOT, "docs")
    for n in sorted(os.listdir(d)):
        if not n.endswith("-SPEC.md"):
            continue
        first = read("docs/" + n).split("\n")[0]
        out[n[:-3]] = first.lstrip("# ").strip()
    return out


# Where a document lives is what it IS, and that is the whole point of the
# layout: docs/ describes how the system works TODAY, docs/plans/ is work that
# is proposed or half-done, docs/plans/completed/ is the design record behind
# something that shipped, and docs/history/ is a record of a moment that has
# passed. The filename used to carry that meaning - "plan" if "PLAN" in the
# name - and it was wrong in both directions: HANDOFF-REDRAW.md is a plan with
# no PLAN in it, and KERNEL-MEMORY.md is maintained reference that happens to
# sit beside eighty design records. A reader could not tell which of the two
# any given file was, which is exactly the misleading the split exists to end.
DOC_KINDS = [
    ("docs/plans/completed/", "completed"),
    ("docs/plans/", "plan"),
    ("docs/history/", "history"),
    # A REPORT is a measurement, not a description: true of the tree it was
    # taken on and of no other, so it neither goes stale the way `docs/` does
    # nor proposes anything the way `docs/plans/` does. It has to sit ABOVE
    # the catch-all below, which is a prefix match in order - under it, every
    # report would file itself as maintained reference.
    ("docs/reports/", "report"),
    ("docs/", "reference"),
]


def doc_files():
    """[(path, kind)] for every docs/**.md, kind taken from the DIRECTORY.

    TRACKED files, deliberately: --check runs in every `make`, so listing the
    live directory means an untracked draft parked in docs/ fails every build -
    and regenerating writes the local-only name into INDEX.md, which then fails
    --check on every other machine. A walk is the fallback for a tree without
    git (a release tarball).

    The git pathspec `docs/*.md` matches at any depth - a pathspec wildcard is
    not stopped by a `/` - so the one glob still reaches all four directories.
    Do not "fix" it to `docs/**/*.md`, which git reads as a LITERAL `**`.

    **AND IT IS DE-DUPLICATED, because `ls-files` lists a CONFLICTED path once
    per stage.** During an unresolved merge git holds three entries for a file
    with a conflict in it - base, ours, theirs - and plain `ls-files` prints
    the name three times (verified: a two-line synthetic conflict returns
    `d/x.md` three times). Regenerating the index in that state wrote
    `WRITING-TESTS.md` three times into the list and 17 where the answer is
    15, and nothing caught it: `checkdocs` compares INDEX.md against what the
    tool produces, so a wrong answer generated and committed together AGREES.
    A set is the whole fix and it costs nothing in the ordinary case."""
    try:
        names = subprocess.check_output(
            ["git", "-C", ROOT, "ls-files", "docs/*.md"],
            text=True, stderr=subprocess.DEVNULL).split("\n")
        names = sorted({n for n in names if n})
    except (OSError, subprocess.CalledProcessError):
        names = []
    if not names:
        names = []
        for base, _dirs, files in os.walk(os.path.join(ROOT, "docs")):
            rel = os.path.relpath(base, ROOT).replace(os.sep, "/")
            names += ["%s/%s" % (rel, f) for f in files if f.endswith(".md")]
    out = []
    for n in sorted(names):
        if not n.endswith(".md") or n == "docs/INDEX.md":
            continue
        for prefix, kind in DOC_KINDS:
            if n.startswith(prefix):
                out.append((n, kind))
                break
    return out


def build():
    head = spec_headings()
    slots = api_slots()
    used = set()
    L = []
    w = L.append

    w("# What os8088 already does")
    w("")
    w("**Check here before designing something.** This index answers \"is there "
      "already a way to do X\" - the question that comes before \"how does X "
      "work\", which is SPEC.md's job. Every row points at the SPEC section that "
      "documents it and the API slot or include that provides it.")
    w("")
    w("Generated by `tools/os88index.py` from `apps/os88api.inc`, `SPEC.md`, the "
      "shared includes and the Makefile. Do not edit by hand - `--check` runs in "
      "the build and fails on a stale index.")
    w("")
    w("## By subject")
    w("")

    for title, secs, prefixes in GROUPS:
        w("### %s" % title)
        w("")
        cites = []
        for sec in secs:
            if sec in head:
                cites.append("[§%s %s](../SPEC.md#%s)"
                             % (sec, head[sec], anchor(sec, head[sec])))
        if cites:
            w("Read first: " + "; ".join(cites) + ".")
            w("")
        rows = []
        for name, slot, note in slots:
            if name in used:
                continue
            short = name[len("OSAPI_"):]
            if any(short.startswith(p) or short == p for p in prefixes):
                used.add(name)
                rows.append((name, slot, note))
        if rows:
            w("| slot | call | takes |")
            w("|---|---|---|")
            for name, slot, note in rows:
                w("| `%s` | `%s` | %s |" % (slot, name, trim(note)))
        else:
            w("*%s*" % GROUP_NOTES.get(
                title, "(no dedicated slots - see the sections above)"))
        w("")

    leftover = [n for (n, _s, _c) in slots if n not in used]
    if leftover:
        # A slot no group claims would be filed under a heading nobody reads,
        # which is the drift this index exists to prevent. Name it and stop.
        raise SystemExit("os88index: no GROUPS entry claims %s - add a prefix"
                         % ", ".join(leftover))

    w("## Shared includes")
    w("")
    w("A package `%include`s these itself; they are not kernel calls. Include "
      "them at the END of the package, before `OS88_BSS`.")
    w("")
    w("| include | SPEC | what it gives you |")
    w("|---|---|---|")
    for name, sec, blurb in INCLUDES:
        w("| `apps/%s` | §%s | %s |" % (name, sec, blurb))
    w("")

    w("## Packages, and what to read them for")
    w("")
    w("The tree's own worked examples. When a convention is unclear, the "
      "shortest package that uses it is usually the fastest answer. "
      "*ships* is whether a shipped floppy carries it (the `make allapps` "
      "payload, SPEC.md 19.10); a `no` is built by its own target only.")
    w("")
    w("| package | source | SPEC | ships |")
    w("|---|---|---|---|")
    specs = own_specs()
    for name, src, ships in packages():
        sec = ""
        for num, title in sorted(head.items(), key=lambda kv: len(kv[0])):
            if "." in num:
                continue
            if re.search(r"\b%s\b" % re.escape(name), title, re.I):
                sec = "§%s" % num
                break
        if not sec:
            # ...then a SUBSECTION, which is where a package that shares a
            # section with its toolchain lives (CWORD is SPEC.md 73.12).
            for num, title in sorted(head.items(),
                                     key=lambda kv: len(kv[0])):
                if "." not in num:
                    continue
                if re.search(r"\b%s\b" % re.escape(name), title, re.I):
                    sec = "§%s" % num
                    break
        if not sec:
            # ...and a package whose contract is a document of its own says
            # which one, rather than an empty cell (C64-SPEC, WEAVE-SPEC).
            for doc, title in sorted(specs.items()):
                if re.search(r"\b%s\b" % re.escape(name), title, re.I):
                    sec = "`docs/%s.md`" % doc
                    break
        w("| %s | `%s` | %s | %s |" % (name, src, sec, "yes" if ships else "no"))
    w("")

    w("## SPEC.md sections")
    w("")
    w("| § | subject |")
    w("|---|---|")
    for num in sorted((n for n in head if "." not in n), key=int):
        w("| %s | %s |" % (num, head[num]))
    w("")

    w("## docs/")
    w("")
    w("**The DIRECTORY says what a document is, and the filename does not.** "
      "`docs/` describes how the system works today - instructions, contracts "
      "and maintained reference. Everything under `docs/plans/` is a design "
      "record: what was considered, including the options that were rejected, "
      "and it is never a description of what shipped - SPEC.md is the current "
      "state and these are how it got there. `docs/plans/completed/` is the "
      "subset whose work has landed; what stays directly in `docs/plans/` "
      "still has work open. `docs/history/` is superseded or closed - a record "
      "of a moment that has passed, and true of no tree you can check out. "
      "`docs/reports/` is a MEASUREMENT taken at a point in time: true of the "
      "tree it was taken on, quotable with its date and its box, and never to "
      "be read as a description of today.")
    w("")
    docs = doc_files()
    for kind, label in (
            ("reference", "*How it works today - `docs/` (%d):*"),
            ("plan", "*Plans with work still open - `docs/plans/` (%d):*"),
            ("completed", "*Design records for what shipped - "
                          "`docs/plans/completed/` (%d):*"),
            ("history", "*Superseded and closed - `docs/history/` (%d):*"),
            ("report", "*Measurements, each true of the tree it was taken on "
                       "- `docs/reports/` (%d):*")):
        names = [os.path.basename(n) for n, k in docs if k == kind]
        w(label % len(names) + " "
          + ", ".join("`%s`" % n for n in names))
        w("")
    return "\n".join(L) + "\n"


def main():
    if not check_includes():          # a stale list is a stale index, and this
        return 1                      # one is hand-written, so it is checked
    text = build()
    if "--check" in sys.argv:
        try:
            with open(OUT, encoding="utf-8") as f:
                cur = f.read()
        except FileNotFoundError:
            cur = None
        if cur != text:
            print("os88index: docs/INDEX.md is stale - run tools/os88index.py")
            return 1
        print("os88index: docs/INDEX.md is current")
        return 0
    # newline="\n": the file is LF in the tree, and Windows would otherwise
    # rewrite it CRLF and show every line as changed.
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("os88index: wrote docs/INDEX.md (%d slots, %d packages)"
          % (len(api_slots()), len(packages())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
