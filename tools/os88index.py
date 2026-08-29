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

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "docs", "INDEX.md")

# --- the subject groups -------------------------------------------------------
# Each is (title, SPEC sections, [slot-name prefixes or exact names]). The
# ORDER is the order a package author meets them, not the API's own numbering:
# you draw before you handle a click, and you open a window before either.
GROUPS = [
    ("Windows", ["11", "20"],
     ["WM_", "ABOUT_SET", "WIN_"]),
    ("Menus and the menu bar", ["12"],
     ["MENU_", "TOAST"]),
    ("Drawing", ["5", "32", "39", "76"],
     ["GFX_", "SET_COLOR", "PAL_"]),
    ("Text and fonts", ["6", "83"],
     ["FONT_", "FACE_"]),
    ("Input - keyboard and mouse", ["9", "10", "13"],
     ["KEY_", "MOUSE", "KBD_"]),
    ("Files and volumes", ["18", "19", "22", "38", "54"],
     ["FILE_", "DIR_", "VOL_", "FDLG", "DLG_", "ASSOC"]),
    ("Memory", ["2", "41", "50", "66"],
     ["MEM_", "XMEM_", "CLAIM", "SYS_KB"]),
    ("Tasks, timing and the clock", ["7", "8", "37"],
     ["TASK_", "TICKS", "TIMER", "SLEEP", "YIELD"]),
    ("Sound", ["34", "35"],
     ["SND_", "TONE"]),
    ("The system - CPU, video, clipboard, drivers", ["31", "51", "55", "57", "60"],
     ["CPU_INFO", "VIDEO", "CLIP_", "DRV_", "SYS_", "CFG_", "SNAPSHOT"]),
    ("Networking", ["62", "67", "70", "71", "72", "77"],
     ["NET_", "SOCK_"]),
    ("Fullscreen and the screen saver", ["53", "64", "79"],
     ["FSX_", "BLANK"]),
    ("Randomness and maths", ["84"],
     ["RAND", "SRAND"]),
]

# Shared includes a package opts into. The blurb is this file's own - it is the
# one thing here not extracted, because "what is this FOR" is not in the source
# in a form worth parsing, and a wrong blurb is visible where a wrong slot is
# not.
INCLUDES = [
    ("os88ui.inc", "13, 75",
     "Buttons, check boxes, radio dots, scroll bars, group boxes and the "
     "standard alert. Opt into the alert with `%define OS88UI_ALERT` and the "
     "scroll bar with `%define OS88UI_SCROLL`."),
    ("os88line.inc", "83",
     "A one-line text field: caret, horizontal scroll, focus, click-to-position "
     "and the editing keys. The caller owns a 20-byte block."),
    ("os88text.inc", "83",
     "The multi-line sibling of os88line.inc. Enter inserts a newline; no wrap, "
     "no selection, no undo."),
    ("os88chart.inc", "82",
     "A 4bpp offscreen canvas and five chart types - column, bar, line, area, "
     "pie - plus a BMP writer. Shared by CHART.O88 and Sheet's chart window."),
    ("os88fp.inc", "84",
     "IEEE-754 double arithmetic in software, with an 8087 path chosen at run "
     "time. Parse, format, add, subtract, multiply, divide, compare, sqrt, "
     "trunc, floor, round."),
    ("os88sock.inc", "62, 72",
     "The socket layer over NET.DRV or ETHER.DRV."),
    ("os88pit.inc", "37",
     "Sub-tick timing off the 8253."),
    ("os88type.inc", "54",
     "File-type recognition by name and by content."),
]


def anchor(num, title):
    """GitHub's heading anchor for "## <num>. <title>"."""
    text = "%s. %s" % (num, title)
    text = text.lower()
    text = re.sub(r"[^\w\s-]", "", text.replace("\u2014", "").replace("\u2019", ""))
    return re.sub(r"[\s]+", "-", text.strip())


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
    """Enough to say what the call IS FOR, not just what it takes.

    A slot's comment opens with its register list, and a cut there leaves
    "BX = win ptr, SI = your About handler's offset" - which answers "how do I
    call it" and not "is this the thing I want", the only question this index
    exists for. So the register clause is stepped over and the PROSE after it
    is what gets the room.
    """
    note = " ".join(note.replace("|", r"\|").split())
    if len(note) <= width:
        return note
    cut = note[:width]
    for stop in (". ", "; ", " - "):
        if stop in cut and cut.index(stop) > width // 3:
            return cut[:cut.rindex(stop)].rstrip(" -;.") + "..."
    return (cut[:cut.rindex(" ")] if " " in cut else cut) + "..."


def packages():
    """[(NAME, source)] for every .o88 the Makefile builds, from its bin rule."""
    mk = re.sub(r"\\\n\s*", " ", read("Makefile"))
    out = []
    for m in re.finditer(r"^\$\(BUILD\)/([a-z0-9]+)\.bin:([^\n]*)$", mk, re.M):
        src = re.search(r"(apps/[a-z0-9]+/[a-z0-9]+\.asm)", m.group(2))
        if not src or not os.path.exists(os.path.join(ROOT, src.group(1))):
            continue
        # the package's real name is in its own OS88_HEADER, not the file name
        hdr = re.search(r"OS88_HEADER\s+'([^']+)'", read(src.group(1)))
        if hdr:
            out.append((hdr.group(1), src.group(1)))
    return sorted(set(out))


def doc_files():
    """[(name, kind)] for docs/*.md - PLAN files are design records, not reference.

    TRACKED files, deliberately: --check runs in every `make`, so listing the
    live directory means an untracked draft parked in docs/ fails every build -
    and regenerating writes the local-only name into INDEX.md, which then fails
    --check on every other machine. A plain listdir is the fallback for a tree
    without git (a release tarball)."""
    try:
        names = subprocess.check_output(
            ["git", "-C", ROOT, "ls-files", "docs/*.md"],
            text=True, stderr=subprocess.DEVNULL).split("\n")
        names = [os.path.basename(n) for n in names if n]
    except (OSError, subprocess.CalledProcessError):
        names = []
    if not names:
        names = os.listdir(os.path.join(ROOT, "docs"))
    out = []
    for n in sorted(names):
        if not n.endswith(".md") or n == "INDEX.md":
            continue
        out.append((n, "plan" if "PLAN" in n else "notes"))
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
            w("*(no dedicated slots - see the sections above)*")
        w("")

    leftover = [(n, s, c) for (n, s, c) in slots if n not in used]
    if leftover:
        w("### Everything else")
        w("")
        w("| slot | call | takes |")
        w("|---|---|---|")
        for name, slot, note in leftover:
            w("| `%s` | `%s` | %s |" % (slot, name, trim(note)))
        w("")

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
      "shortest package that uses it is usually the fastest answer.")
    w("")
    w("| package | source | SPEC |")
    w("|---|---|---|")
    for name, src in packages():
        sec = ""
        for num, title in sorted(head.items(), key=lambda kv: len(kv[0])):
            if "." in num:
                continue
            if re.search(r"\b%s\b" % re.escape(name), title, re.I):
                sec = "§%s" % num
                break
        w("| %s | `%s` | %s |" % (name, src, sec))
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
    w("**`*-PLAN.md` files are DESIGN RECORDS, not descriptions of what "
      "shipped.** They record what was considered, including options that were "
      "rejected. SPEC.md is the current state; these are how it got there.")
    w("")
    plans = [n for n, k in doc_files() if k == "plan"]
    notes = [n for n, k in doc_files() if k == "notes"]
    w("*Design records (%d):* " % len(plans) + ", ".join("`%s`" % n for n in plans))
    w("")
    w("*Notes and reference (%d):* " % len(notes) + ", ".join("`%s`" % n for n in notes))
    w("")
    return "\n".join(L) + "\n"


def main():
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
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(text)
    print("os88index: wrote docs/INDEX.md (%d slots, %d packages)"
          % (len(api_slots()), len(packages())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
