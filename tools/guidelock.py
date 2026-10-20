#!/usr/bin/env python3
"""The project guide's lock: a manual is checked against what it describes.

    python3 tools/guidelock.py check            # fails on anything owed
    python3 tools/guidelock.py check --warn     # says so, exits 0 (fast tier)
    python3 tools/guidelock.py stamp CALC_O88 --why "added the History menu"
    python3 tools/guidelock.py stamp CALC_O88 --sources apps/calc --why "..."
    python3 tools/guidelock.py stamp --all --why "first stamp"

`tools/abilock.py`'s shape, for the guide. Two things are owed:

1. A PAGE IS STALE when the user-visible strings of the sources it describes
   changed since it was stamped - a menu item, a message, a dialog label, a
   key table. Not the whole source: a rename, a speed-up or a fix that says
   nothing new leaves a manual true, and a lock that fired on every commit
   would be stamped without reading. The strings are what a user reads, so
   they are what makes a manual wrong. The way out is to read the page,
   correct it, and `stamp` it with a reason.

2. A FILE ON A DISK HAS NO PAGE. Every file and folder on a built disk needs
   a line in tools/guide/entries.toml, and a program, driver or system file
   needs a page in tools/guide/manuals/ as well. The layouts themselves need
   nothing: the guide reads them off the images. A page or entry whose file
   is on no disk is reported too - but only when every released disk is
   built, since an on-demand disk that is not built cannot be asked.

The fast tier runs `check --warn`: every `make` hears about it and none is
held up. The full tier runs `check`, which fails until it is paid up.
"""

import argparse
import ast
import hashlib
import json
import os
import re
import subprocess
import sys
if sys.version_info < (3, 11):                 # tomllib, below (CONTRIBUTING.md)
    sys.exit("%s: needs python3 3.11 or newer, for tomllib; this is %d.%d at %s"
             % (os.path.basename(__file__), sys.version_info[0],
                sys.version_info[1], sys.executable))
import tomllib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
GUIDE = os.path.join(ROOT, "tools", "guide")
LOCK = os.path.join(GUIDE, "manuals.lock.json")
MANUALS = os.path.join(GUIDE, "manuals")

# The kinds whose file must have a page, not only a line.
NEEDS_PAGE = ("program", "driver", "system", "dos-tool")
# Sources shared by every package: their strings would make every manual
# stale at once, and none of them is a menu a manual describes.
SHARED = re.compile(r"^(apps/os88[^/]*\.inc|apps/cc/|drivers/os88drv\.inc)")
SRC_EXT = (".asm", ".inc", ".c", ".h", ".py", ".md", ".txt", ".wtx")


# --- the strings a user reads -----------------------------------------------
_ASM_DB = re.compile(r"\bdb\b(.*)$", re.I)
_QUOTED = re.compile(r"'([^']*)'|\"([^\"]*)\"")
_C_STR = re.compile(r'"((?:[^"\\\n]|\\.)*)"')


def _uncomment(line):
    """An assembler line without its comment; a ';' inside quotes stays."""
    q = None
    for i, c in enumerate(line):
        if q:
            if c == q:
                q = None
        elif c in "'\"":
            q = c
        elif c == ";":
            return line[:i]
    return line


def strings_of(path):
    """The literal text in one source file that a user could read."""
    try:
        text = open(path, encoding="utf-8", errors="replace").read()
    except OSError:
        return []
    ext = os.path.splitext(path)[1].lower()
    out = []
    if ext in (".asm", ".inc"):
        for line in text.splitlines():
            m = _ASM_DB.search(_uncomment(line))
            if m:
                out += [a or b for a, b in _QUOTED.findall(m.group(1)) if (a or b).strip()]
    elif ext in (".c", ".h"):
        for line in text.splitlines():
            if line.lstrip().startswith("#include"):
                continue
            out += [s for s in _C_STR.findall(line) if s.strip()]
    elif ext == ".py":
        # The surface of a host tool: its options and their help, and what it
        # prints. Docstrings and comments are for the reader of the source.
        try:
            tree = ast.parse(text)
        except SyntaxError:
            return []
        for node in ast.walk(tree):
            if isinstance(node, ast.Call):
                fn = getattr(node.func, "attr", getattr(node.func, "id", ""))
                if fn in ("add_argument", "print", "say", "fail", "Label", "Button"):
                    for sub in ast.walk(node):
                        if isinstance(sub, ast.Constant) and isinstance(sub.value, str) and sub.value.strip():
                            out.append(sub.value)
    else:
        out.append(text)                    # a README or a document: the whole text
    return out


def files_of(src):
    p = os.path.join(ROOT, src)
    if os.path.isfile(p):
        return [src]
    found = []
    for base, dirs, names in os.walk(p):
        dirs[:] = sorted(d for d in dirs if not d.startswith(".") and d not in ("cache", "hosttest", "demo"))
        for n in sorted(names):
            if n.lower().endswith(SRC_EXT):
                found.append(os.path.relpath(os.path.join(base, n), ROOT))
    return found


def fingerprint(sources):
    h = hashlib.sha256()
    for src in sorted(sources):
        for f in files_of(src):
            if SHARED.match(f):
                continue
            h.update(os.path.relpath(f, src).encode() + b"\0")
            for s in strings_of(os.path.join(ROOT, f)):
                h.update(s.encode("utf-8", "replace") + b"\0")
    return h.hexdigest()[:16]


# --- which sources a page describes ------------------------------------------
_RULES = None


def _makefile_rules():
    global _RULES
    if _RULES is None:
        _RULES = {}
        text = open(os.path.join(ROOT, "Makefile"), encoding="utf-8").read().replace("\\\n", " ")
        for m in re.finditer(r"(?m)^\$\(BUILD\)/([a-z0-9_]+)\.bin\s*:\s*(.*)$", text):
            pre = [p for p in m.group(2).split() if re.match(r"^(apps|drivers|boot|dostools|kernel)/", p)]
            if pre and m.group(1) not in _RULES:
                _RULES[m.group(1)] = pre
    return _RULES


# Where a name and its source folder disagree, or a kernel module's body is
# not in the file its name suggests. Checked against kernel/kernel.asm's
# %include list and the Makefile's rules.
ALIASES = {
    "drmarco_o88": ["apps/drmario"], "pxstein_o88": ["apps/pixelstein"],
    "solitair_o88": ["apps/solitaire"], "artful_o88": ["apps/artful"],
    "desk_drv": ["kernel/deskmod.inc"], "dock_drv": ["kernel/dockmod.inc"],
    "extd_drv": ["kernel/extmod.inc"], "win95_drv": ["kernel/winmod.inc"],
    "format_drv": ["kernel/diskw.inc"], "os88net_com": ["drivers/net/os88net.asm"],
    "coldhma_sys": ["boot/osrom.asm"],
}


def guess_sources(key):
    """Where a page's subject is built from - a first guess, kept in the lock."""
    if key.startswith("TOOL_"):
        import os88guide
        for t in os88guide.TOOLS:
            if t["id"].upper().replace("-", "_") == key:
                return [f for f, _ in t["files"]]
        return []
    if key == "FW_PICOMEM1":
        return ["firmware/picomem1/README.md", "firmware/picomem1/SHA256SUMS"]
    if key.lower() in ALIASES:
        return list(ALIASES[key.lower()])
    stem, _, ext = key.lower().rpartition("_")
    if not stem:
        return []
    if ext in ("o88", "drv", "com", "sys"):
        for d in ("apps/" + stem, "drivers/" + stem, "dostools/" + stem):
            if os.path.isdir(os.path.join(ROOT, d)):
                return [d]
        rule = _makefile_rules().get(stem)
        if rule:
            first = rule[0]
            d = os.path.dirname(first)
            return [d if d not in ("kernel", "boot") else first]
        for f in ("kernel/%s.inc" % stem, "dostools/%s.asm" % stem):
            if os.path.isfile(os.path.join(ROOT, f)):
                return [f]
    return []


# --- the lock ------------------------------------------------------------------
def load():
    try:
        return json.load(open(LOCK))
    except (OSError, ValueError):
        return {}


def save(lock):
    with open(LOCK, "w") as f:
        json.dump(dict(sorted(lock.items())), f, indent=1, sort_keys=True)
        f.write("\n")


def commit():
    try:
        return subprocess.check_output(["git", "-C", ROOT, "rev-parse", "--short", "HEAD"], text=True).strip()
    except Exception:
        return "?"


def page_keys():
    return sorted(f[:-3] for f in os.listdir(MANUALS) if f.endswith(".md"))


def key_of(name):
    """The page key for a file name: CALC.O88 -> CALC_O88, FONTS/ -> FONTS."""
    return name.rstrip("/").replace(".", "_")


# --- what to do about it ---------------------------------------------------------
# Printed under the list whenever a page is owed, so the instruction travels
# with the warning and nobody has to find this file to learn the rule.
HOWTO = [
    "How to pay a stale page: the manual describes the program AS IT IS NOW,",
    "at the level of detail it already has. Change it only where a first-time",
    "writer, reading today's program, would have written it differently. A",
    "change below the page's level of detail (an internal figure, a new",
    "message for a rare case, a fix) needs no words at all: stamp it unchanged",
    "and say why. Never write history - no 'now', 'new', 'no longer', 'used",
    "to', 'since': a manual is the current state, not a changelog, and it must",
    "not grow with every commit. Then: python3 tools/guidelock.py stamp KEY",
    "--why \"what you checked\". A file with no page or line: write the line in",
    "tools/guide/entries.toml, and for a program, driver or system file a page",
    "in tools/guide/manuals/ (tools/guide/MANUALS.md is the brief).",
]


# --- the checks ----------------------------------------------------------------
def check():
    """Everything owed, as (kind, message) pairs."""
    import os88guide
    owed = []
    lock = load()
    pages = set(page_keys())

    # 1. stale pages
    for key in sorted(pages):
        rec = lock.get(key)
        if rec is None:
            if guess_sources(key):
                owed.append(("unstamped", "%s has a page but was never stamped: read it, then "
                             "`python3 tools/guidelock.py stamp %s --why ...`" % (key, key)))
            continue
        if not rec.get("sources"):
            continue
        now = fingerprint(rec["sources"])
        if now != rec.get("strings"):
            owed.append(("stale", "%s may be out of date: the text in %s changed since it was "
                         "stamped at %s (`git diff %s -- %s`). Check the page, then "
                         "`python3 tools/guidelock.py stamp %s --why ...`"
                         % (key, ", ".join(rec["sources"]), rec.get("stamped", "?"),
                            rec.get("stamped", "HEAD"), " ".join(rec["sources"]), key)))

    # 2. files with no line or no page
    os88guide.HASH_EXT = ()             # (previews are matched below, by source)
    data = os88guide.collect()
    entries = data["entries"]
    names, built_all = {}, True
    for d in data["disks"]:
        if not d.get("tree"):
            if not d.get("sameas"):
                built_all = False
            continue
        for row in d["tree"]:
            if len(row) > 3:
                continue                    # described by where it is
            n = row[0].rstrip("/").split("/")[-1] + ("/" if row[0].endswith("/") else "")
            names.setdefault(n, set()).add(d["file"])
    for n in sorted(names):
        where = ", ".join(sorted(names[n])[:3]) + (" ..." if len(names[n]) > 3 else "")
        e = entries.get(n)
        if e is None:
            owed.append(("undescribed", "%s is on %s and has no line in tools/guide/entries.toml" % (n, where)))
        elif e.get("kind") in NEEDS_PAGE and key_of(n) not in pages:
            owed.append(("no-page", "%s (%s) is on %s and has no page: write tools/guide/manuals/%s.md"
                         % (n, e.get("kind"), where, key_of(n))))

    # 2b. a demo video with no preview, or one made from other bytes
    sys.path.insert(0, os.path.join(ROOT, "tools", "guide"))
    import vidpreview
    for src in vidpreview.owed():
        owed.append(("no-preview", "%s has no video preview made from its current bytes: "
                     "python3 tools/guide/vidpreview.py (needs ffmpeg), then commit "
                     "tools/guide/videos/" % src))

    # 3. pages and lines for files no disk carries (only when all are built)
    if built_all:
        on_disk = set(names)
        for n in sorted(entries):
            if n not in on_disk:
                owed.append(("orphan", "tools/guide/entries.toml describes %s, which is on no disk" % n))
        keys_on_disk = {key_of(n) for n in on_disk}
        for key in sorted(pages):
            if key not in keys_on_disk and not key.startswith(("TOOL_", "FW_")):
                owed.append(("orphan", "tools/guide/manuals/%s.md describes a file on no disk" % key))
    return owed, built_all


def cmd_check(a):
    owed, built_all = check()
    tag = "warn: " if a.warn else ""
    for kind, msg in owed:
        print("%sguide %s: %s" % (tag, kind, msg))
    if owed:
        for line in HOWTO:
            print("%s  %s" % (tag, line))
    if not built_all:
        print("guidelock: some on-demand disks are not built, so pages for files on no disk were not looked for")
    print("guidelock: %d owed" % len(owed))
    return 0 if (a.warn or not owed) else 1


def cmd_stamp(a):
    lock = load()
    keys = page_keys() if a.all else [k.upper() for k in a.keys]
    if not keys:
        sys.exit("guidelock: name the pages to stamp, or --all")
    if not a.why:
        sys.exit("guidelock: say why with --why: what you checked or changed")
    c = commit()
    for key in keys:
        if not os.path.exists(os.path.join(MANUALS, key + ".md")):
            sys.exit("guidelock: there is no tools/guide/manuals/%s.md" % key)
        rec = lock.get(key, {})
        if a.sources:
            rec["sources"] = [s.strip().rstrip("/") for s in a.sources.split(",") if s.strip()]
        elif "sources" not in rec:
            rec["sources"] = guess_sources(key)
        for s in rec["sources"]:
            if not os.path.exists(os.path.join(ROOT, s)):
                sys.exit("guidelock: %s names %s, which does not exist" % (key, s))
        rec["strings"] = fingerprint(rec["sources"]) if rec["sources"] else None
        rec["stamped"], rec["why"] = c, a.why
        lock[key] = rec
    save(lock)
    print("guidelock: stamped %d page(s) at %s" % (len(keys), c))
    return 0


def main(argv):
    ap = argparse.ArgumentParser(description="The project guide's lock.")
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("check")
    c.add_argument("--warn", action="store_true", help="report and exit 0")
    s = sub.add_parser("stamp")
    s.add_argument("keys", nargs="*")
    s.add_argument("--all", action="store_true")
    s.add_argument("--sources", help="comma-separated paths the page describes")
    s.add_argument("--why", help="what was checked or changed")
    a = ap.parse_args(argv)
    return cmd_check(a) if a.cmd == "check" else cmd_stamp(a)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
