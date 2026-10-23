#!/usr/bin/env python3
"""Every package says OS88_HIGH and OS88_HIGHMEM, or says here why it does not.

    python3 tests/unit/t_megahigh.py

On kern_mega a package's region and its claims can live ABOVE 1MB, and two
bits of the header's class byte (+15) are the whole of the opt-in
(apps/os88api.inc, KERN-MEGA-PLAN 13.3 and item 81):

  * `high`    - OS88_HIGH (bit 7): the region may be loaded above 1MB;
  * `highmem` - OS88_HIGHMEM (bit 6): its claims of 64KB or less may be too.

Both stay OPT-IN, at the owner's word (KERN-MEGA-PLAN 14.51), "but only with
a full tier gate. They need to except themselves if they want to not opt in,
and they need a damn good reason to do so." This is that gate, in
t_movable.py's shape: every package declares both bits, or carries a line in
tests/megahigh.txt per bit it does not declare, with a REASON - something in
the code that breaks above 1MB, or exactly what would settle it. And the
registry turns ONE WAY: a line for a package that now declares is a FAILURE,
so it cannot rot into a list of things that used to be true.

WHERE THE BITS ARE READ FROM, AND WHY IT IS THE SOURCE AND AN ARTEFACT BOTH.
The truth is the class byte of the image kern_mega loads. But the C packages
(SPEC.md 73) are built by their own targets and not by `make`, and a gate
that skips what is not built reports on less than the tree. So the verdict
is decided from the SOURCE - OS88_HEADER's fourth argument, or the
`%define CC_STACK_CLASS` a C shim hands apps/cc/crt0.asm - evaluated through
a small model of nasm's %ifdef/%ifndef/%else/%endif with the defines the
SHIPPED recipe passes, and every built image that is current is then
CROSS-CHECKED against it: a built `.bin` whose byte 15 disagrees with the
parse fails the row. That is what keeps the parser honest, and it is exact
for the image: the class byte sits inside the 32-byte header, which is
inside the clear prefix tools/os88pkg.py never compresses (its
clear_prefix() is at least HEADER_SIZE), so the `.bin` nasm wrote and the
`.o88` on the disk agree on it byte for byte. It reads no image it would
have to decompress, which is what keeps the row at a fraction of a second.

RE-HOMED PROGRAMS SAY IT IN THE PROGRAM PART'S HEADER (OS88_HIGH's note in
apps/os88api.inc, SPEC.md 20.12.10.9). Every program part (word.asm,
scribe.asm, skies.asm, dos.asm, pxgame.asm) emits its own OS88_HEADER and
is a package to this row like any other, so it is checked as itself. Its
LOADER - a package whose own source invokes OS88_REHOME - is not asked for
`high` at all: it is freed inside the re-home, so where it was loaded buys
nothing. It IS asked for `highmem` when its own source claims - PIXELSTEIN
3D's loader makes the sprite set's 60KB claim for the program, under the
slot the re-home keeps (KERN-MEGA-PLAN 14.48 S7), and the bit decides where
that lands. A loader whose own source makes no plain claim has nothing the
bit reaches: its carve and its fetches are the parts library's DMA door
(apps/os88partsbody.inc), which is conventional on kern_mega whatever the
class byte says, and the carve itself goes above 1MB with the program's KEEP
(KERN-MEGA-PLAN 14.44). That is a fact about the code and not a judgement,
so it lives here and not in the registry.

DOS IS THE ONE PACKAGE WHOSE HEADER DEPENDS ON THE BUILD. kern_mega's
DOS.O88 is build/<megak>/kdos/DOS.O88, whose part 0 is `dosp.bin`, built
from apps/dos/dos.asm with -DDOSKPART -DDOS_EXTCORE and, under KERN_MEGA,
-DDOS_MEGA - the arm whose header says OS88_HIGH. `dos.bin` is the same file
built plain, for the icon pipeline, and ships nowhere. SHIPPED_STEM names
that, the defines are read off that recipe rather than restated, and the
artefact cross-check for a recipe with a KERN_MEGA arm reads the kern_mega
tree's image alone (kern_big's dosp.bin is byte 15 = 0 on purpose).

A PACKAGE WITH A kern_mega ARM IS ASKED ABOUT THE ARM (KERN-MEGA-PLAN
14.66, SPEC.md 20.18). Its code that only kern_mega runs is under `%ifdef
PKG_MEGA`, `make mega` builds that arm ($(MEGAAPPDIR)/<stem>.bin, -DPKG_MEGA)
onto kern_mega's own disks, and the SHIPPED build is free to declare less -
its claims stay conventional, or it refuses under protected mode - because
the shipped build is what an 8088 runs. So for a stem in $(MEGAPKGS) the
class is read with the arm's defines, and the artefact cross-checked is
build/megak/apps/<stem>.bin; the shipped build's own byte is held only to
being no MORE than the arm's (a shipped build that says OS88_HIGHMEM when
the arm does not is a fail-safe turned inside out).

THE FAILING CASES (docs/WRITING-TESTS.md 1), all three tried when this was
written: take OS88_HIGHMEM out of apps/hello/hello.asm and the row fails
naming HELLO; put a `highmem` line in for a package that declares (CALC was
the one) and it fails as a stale exemption; and a wrong parse fails too -
a copy of build/paccman.bin with byte 15's bit 6 cleared, put under
$OS88_TREE, and the cross-check names the disagreement.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from harness import check, done                           # noqa: E402
import t_movable                                          # noqa: E402

ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
REGISTRY = os.path.join(ROOT, "tests", "megahigh.txt")
SDK = os.path.join("apps", "os88api.inc")
CRT0 = os.path.join("apps", "cc", "crt0.asm")
# $OS88_TREE is where a run's artefacts live (tools/os88build.py tree_root);
# $OS88_BUILD names a KERNEL's directory and is not this
BUILD = os.environ.get("OS88_TREE") or "build"
MEGADIR = os.path.join(BUILD, "megak")

KINDS = ("high", "highmem")
MINREASON = 40          # a reason with a mechanism in it, not "not yet"

# The one package whose SHIPPED header is not the plain build's: see the
# docstring. Asserted against the Makefile below, so a renamed part fails
# here rather than quietly checking the icon pipeline's build instead.
SHIPPED_STEM = {"apps/dos/dos.asm": "dosp"}

REHOME = re.compile(r"^\s*OS88_REHOME\b", re.M)
OWN_CLAIM = re.compile(r"^\s*call\s+OSAPI_MEM_(?:CLAIM|CLAIM_HI|HUGE)\b",
                       re.M)
HEADER = re.compile(r"^\s*OS88_HEADER\s+(.*)$")
CCCLASS = re.compile(r"^\s*%define\s+CC_STACK_CLASS\s+(.*)$")
CRT0INC = re.compile(r'^\s*%include\s+"cc/crt0\.asm"', re.M)


def read(path):
    with open(os.path.join(ROOT, path), errors="replace") as fh:
        return fh.read()


def code(line):
    """One source line without its comment. nasm's comment is `;`, and none
    of the lines this row reads carries a `;` inside a string."""
    return line.split(";", 1)[0].rstrip()


def sdk_values():
    """{name: value} for every OS88_STACK_* and the two bits, from the SDK
    itself - so a renumbering moves this row with it."""
    vals = {}
    for m in re.finditer(r"^(OS88_(?:STACK_\w+|HIGH|HIGHMEM))\s+equ\s+"
                         r"(0x[0-9A-Fa-f]+|\d+)", read(SDK), re.M):
        vals[m.group(1)] = int(m.group(2), 0)
    return vals


def evaluate(expr, vals):
    """A class expression - `OS88_STACK_192 | OS88_HIGH` - to its byte, or
    None when it names something this row cannot resolve."""
    expr = expr.strip()
    if not expr:
        return 0
    if not re.fullmatch(r"[\w\s|()+]+", expr):
        return None
    names = re.findall(r"[A-Za-z_]\w*", expr)
    if any(n not in vals for n in names):
        return None
    try:
        return int(eval(expr, {"__builtins__": {}}, dict(vals))) & 0xFF
    except Exception:                                # noqa: BLE001
        return None


def split_args(text):
    """OS88_HEADER's arguments: the quoted name first, then commas."""
    m = re.match(r"\s*('[^']*'|\"[^\"]*\")\s*,(.*)$", text)
    if not m:
        return None
    return [m.group(1)] + [a.strip() for a in m.group(2).split(",")]


def preprocess(text, defines):
    """[(line, state)] for the lines of `text`, state True when nasm would
    assemble the line with `defines` given on the command line, None when
    this model cannot tell (an %if over an expression). Only what governs a
    header is modelled: %define/%undef, %ifdef/%ifndef/%elifdef/%elifndef,
    %if/%elif (unknown unless a literal), %else and %endif."""
    defs = set(defines)
    stack = []              # [live, taken, unknown] per open %if
    out = []

    def live():
        if any(s[2] for s in stack):
            return None
        return all(s[0] for s in stack)

    for raw in text.splitlines():
        ln = code(raw).strip()
        tok = ln.split()
        d = tok[0].lower() if tok else ""
        arg = tok[1] if len(tok) > 1 else ""
        outer = all(s[0] for s in stack) and not any(s[2] for s in stack)
        if d in ("%ifdef", "%ifndef"):
            c = (arg in defs) == (d == "%ifdef")
            stack.append([c, c, False])
        elif d == "%if":
            lit = ln[3:].strip()
            if lit in ("0", "1"):
                stack.append([lit == "1", lit == "1", False])
            else:
                stack.append([False, False, True])
        elif d in ("%elifdef", "%elifndef") and stack:
            s = stack[-1]
            c = (arg in defs) == (d == "%elifdef")
            s[0] = (not s[1]) and c
            s[1] = s[1] or c
        elif d == "%elif" and stack:
            stack[-1][2] = True
        elif d == "%else" and stack:
            s = stack[-1]
            s[0] = not s[1]
            s[1] = True
        elif d == "%endif" and stack:
            stack.pop()
        elif d in ("%define", "%xdefine", "%assign") and outer and arg:
            defs.add(arg)
        elif d == "%undef" and outer and arg:
            defs.discard(arg)
        out.append((raw, live()))
    return out


def makefile():
    """The Makefile with its continuations joined, once."""
    return re.sub(r"\\\n\s*", " ", read("Makefile"))


def recipes(mk):
    """{main source: [(stem, plain defines, KERN_MEGA-arm defines)]} for
    every `$(BUILD)/<stem>.bin:` rule whose recipe assembles that source, and
    one plain entry per `CC_PACKAGE,<name>,<dir>` (73.2's generated rule).
    A define behind any OTHER `$(if ...)` is a knob and is left out: the
    shipped build is the knob-less one."""
    out = {}
    cur = first = None
    for ln in mk.split("\n"):
        m = re.match(r"^\$\(BUILD\)/([A-Za-z0-9_-]+)\.bin\s*:\s*(\S*)", ln)
        if m:
            cur, first = m.group(1), m.group(2)
            continue
        if not ln.startswith("\t"):
            cur = None
            continue
        if cur is None:
            continue
        src = re.search(r"-o\s+\$@\s+(apps/\S+\.asm)", ln)
        if not src and re.search(r"-o\s+\$@\s+\$<", ln) and \
                re.fullmatch(r"apps/\S+\.asm", first or ""):
            src = re.match(r"(.*)", first)  # `-o $@ $<`: the first prerequisite
        if not src:
            continue
        mega = set(re.findall(r"\$\(if \$\(KERN_MEGA\),-D(\w+)\)", ln))
        bare = re.sub(r"\$\(if [^()]*(?:\([^()]*\)[^()]*)*\)", " ", ln)
        plain = set(re.findall(r"(?<!\S)-D(\w+)", bare))
        out.setdefault(src.group(1), []).append((cur, plain, mega))
    for m in re.finditer(r"CC_PACKAGE,([a-z0-9]+),([a-z0-9]+)", mk):
        out.setdefault("apps/%s/%s.asm" % (m.group(2), m.group(1)),
                       []).append((m.group(1), set(), set()))
    return out


def shipped_recipe(main, rs):
    """The recipe kern_mega's disks carry the output of: SHIPPED_STEM's, or
    the one passing the fewest defines (a bench or a diagnostic build is the
    same source with something added), the stem named like the source
    breaking a tie."""
    if not rs:
        return None
    want = SHIPPED_STEM.get(main)
    if want:
        for r in rs:
            if r[0] == want:
                return r
        return None
    base = os.path.splitext(os.path.basename(main))[0]
    return sorted(rs, key=lambda r: (len(r[1]) + len(r[2]),
                                     r[0] != base))[0]


def class_of(main, defines, vals):
    """(class byte or None, what was read, problem or None)."""
    text = read(main)
    is_c = bool(CRT0INC.search(text))
    hits = []
    for raw, state in preprocess(text, defines):
        ln = code(raw)
        if is_c:
            m = CCCLASS.match(ln)
            if m and state is not False:
                hits.append((state, m.group(1).strip()))
        else:
            m = HEADER.match(ln)
            if m and state is not False:
                args = split_args(m.group(1))
                if args is None:
                    return None, ln.strip(), "cannot split OS88_HEADER's " \
                        "arguments"
                hits.append((state, args[3] if len(args) >= 4 else ""))
    if any(s is None for s, _ in hits):
        return None, "", "an OS88_HEADER sits under an %if this row cannot " \
            "evaluate - name the shipped build in SHIPPED_STEM, or make the " \
            "condition an %ifdef"
    if is_c and not hits:
        return 0, "(no CC_STACK_CLASS: crt0 writes 0)", None
    if len(hits) != 1:
        return None, "", "%d live header(s) - want exactly one" % len(hits)
    byte = evaluate(hits[0][1], vals)
    if byte is None:
        return None, hits[0][1], "cannot evaluate the class expression"
    return byte, hits[0][1] or "(no 4th argument: 0)", None


def artefacts(stem, mega):
    """The built images of `stem` that this row may compare with: the
    kern_mega tree's always, kern_big's only when the recipe has no KERN_MEGA
    arm (it is then the same image, and it is what the shipped apps disks
    carry - kern_mega pairs with them)."""
    out = [os.path.join(MEGADIR, stem + ".bin")]
    if not mega:
        out.append(os.path.join(BUILD, stem + ".bin"))
    return [p for p in out if os.path.isfile(os.path.join(ROOT, p))]


def mega_arms(mk):
    """{stem: defines} of every kern_mega arm: a `$(MEGAAPPDIR)/<stem>.bin`
    rule's nasm line, and only the stems $(MEGAPKGS) names"""
    out = {}
    cur = None
    for ln in mk.split("\n"):
        m = re.match(r"^\$\(MEGAAPPDIR\)/([A-Za-z0-9_-]+)\.bin\s*:", ln)
        if m:
            cur = m.group(1)
            continue
        if not ln.startswith("\t"):
            cur = None
            continue
        if cur and "NASM" in ln:
            bare = re.sub(r"\$\(if [^()]*(?:\([^()]*\)[^()]*)*\)", " ", ln)
            out[cur] = set(re.findall(r"(?<!\S)-D(\w+)", bare))
    m = re.search(r"^MEGAPKGS\s*:?=\s*(.*)$", mk, re.M)
    listed = set(re.findall(r"\$\(MEGAAPPDIR\)/([a-z0-9_-]+)\.o88",
                            m.group(1))) if m else set()
    return {k: v for k, v in out.items() if k in listed}


def registry():
    out, bad = {}, []
    with open(REGISTRY) as fh:
        lines = fh.read().split("\n")
    i = 0
    while i < len(lines):
        raw = lines[i]
        i += 1
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        body, _, reason = raw.partition("#")
        parts = body.split()
        # a reason runs on over the indented comment lines that follow it
        while i < len(lines) and lines[i].startswith(" ") \
                and lines[i].lstrip().startswith("#"):
            reason += " " + lines[i].lstrip()[1:].strip()
            i += 1
        if len(parts) != 2 or parts[0] not in KINDS:
            bad.append((i, raw))
            continue
        out[(parts[0], parts[1])] = reason.strip()
    return out, bad


def main():
    vals = sdk_values()
    check(all(k in vals for k in ("OS88_HIGH", "OS88_HIGHMEM",
                                  "OS88_STACK_DEFAULT")),
          "megahigh: the SDK's class names did not parse",
          why="the row evaluates every header's class expression against "
              "apps/os88api.inc's own equs, so it cannot read a stale "
              "number of its own",
          got=sorted(vals), want="OS88_HIGH, OS88_HIGHMEM, OS88_STACK_*")
    hi, hm = vals.get("OS88_HIGH", 0x80), vals.get("OS88_HIGHMEM", 0x40)

    # The two places the byte is EMITTED, so a change to either is a change
    # to what this row means rather than a silent one.
    check(re.search(r"^\s*db %4\s*;\s*\+15", read(SDK), re.M),
          "megahigh: OS88_HEADER no longer writes its 4th argument at +15",
          why="the kernel reads OS88_HIGH and OS88_HIGHMEM out of the "
              "header's +15 (loader.inc's `test byte [es:LD_H_CLASS], 0x80`, "
              "mega_ownerof's 0x40), and this row reads OS88_HEADER's fourth "
              "argument as that byte",
          got="no `db %4 ; +15` in " + SDK, want="the macro emits it")
    check(re.search(r"%ifdef CC_STACK_CLASS\s*\n\s*db CC_STACK_CLASS\s*;"
                    r"\s*\+15", read(CRT0)),
          "megahigh: crt0.asm no longer writes CC_STACK_CLASS at +15",
          why="a C package declares both bits through CC_STACK_CLASS "
              "(KERN-MEGA-PLAN 10.3 item 87) and this row reads that define "
              "as the byte; if crt0 stopped writing it every C package "
              "would un-declare at once",
          got="no `db CC_STACK_CLASS ; +15` under %ifdef in " + CRT0,
          want="crt0 emits it")

    pkgs = t_movable.packages()
    check(len(pkgs) >= 40, "megahigh: found too few packages to check",
          why="the discovery is t_movable.py's - a package is a file under "
              "apps/ that emits a header. A short list means the row is "
              "asserting nothing about the rest",
          got="%d package(s)" % len(pkgs), want="the tree's packages")

    mk = makefile()
    allr = recipes(mk)
    armd = mega_arms(mk)
    for src, stem in SHIPPED_STEM.items():
        check(any(r[0] == stem for r in allr.get(src, [])),
              "megahigh: SHIPPED_STEM names %s.bin for %s and the Makefile "
              "builds no such thing from it" % (stem, src),
              why="the table says which build of a conditional header is "
                  "the one kern_mega's disk carries; a stale name would "
                  "check a build nobody ships",
              got=[r[0] for r in allr.get(src, [])], want=stem)
    check("dosp.bin" in mk and
          re.search(r"/kdos/DOS\.O88\s*:\s*\$\(BUILD\)/dosload\.bin\s+"
                    r"\$\(BUILD\)/dosp\.bin", mk),
          "megahigh: kdos/DOS.O88 is no longer dosload.bin + dosp.bin",
          why="SHIPPED_STEM's DOS line rests on that recipe: the DOS box "
              "kern_mega's system disk carries is that file, its program "
              "part dosp.bin",
          got="the kdos/DOS.O88 rule changed", want="dosload.bin dosp.bin")

    reg, bad = registry()
    for n, raw in bad:
        check(False, "megahigh: tests/megahigh.txt line %d is malformed" % n,
              why="one line per exemption: `<kind> <path>  # <reason>`, kind "
                  "'high' or 'highmem'",
              got=raw.strip(), want="high|highmem <path>  # <reason>")

    need = set()
    nhigh = nmem = crossed = 0
    for main_src in sorted(pkgs):
        rs = allr.get(main_src, [])
        rec = shipped_recipe(main_src, rs)
        plain, mega = (rec[1], rec[2]) if rec else (set(), set())
        arm = armd.get(rec[0]) if rec else None
        if arm is not None:
            sbyte, swhat, _ = class_of(main_src, plain, vals)
            byte, what, problem = class_of(main_src, arm, vals)
            if sbyte is not None and byte is not None:
                check(sbyte & (hi | hm) & ~byte == 0,
                      "megahigh: %s's SHIPPED build declares a bit its "
                      "kern_mega arm does not" % main_src,
                      why="the arm is what kern_mega is meant to run; a "
                          "shipped build may declare LESS (its claims "
                          "conventional where reading a selector claim is "
                          "the arm's), never more (KERN-MEGA-PLAN 14.66)",
                      got="shipped `%s`" % swhat, want="within `%s`" % what)
        else:
            byte, what, problem = class_of(main_src, plain | mega, vals)
        if problem:
            check(False, "megahigh: cannot read %s's class byte" % main_src,
                  why=problem, got=what, want="one decidable class")
            continue

        own = "\n".join(t_movable.strip(read(p)) for p in pkgs[main_src])
        loader = bool(REHOME.search(t_movable.strip(read(main_src))))
        ask_high = not loader
        ask_mem = (not loader) or bool(OWN_CLAIM.search(own))

        if rec:
            arts = [os.path.join(MEGADIR, "apps", rec[0] + ".bin")] \
                if arm is not None else artefacts(rec[0], mega)
            arts = [a for a in arts if os.path.isfile(os.path.join(ROOT, a))]
            for art in arts:
                if os.path.getmtime(os.path.join(ROOT, art)) < \
                        os.path.getmtime(os.path.join(ROOT, main_src)):
                    continue            # stale: the next build decides
                with open(os.path.join(ROOT, art), "rb") as fh:
                    head = fh.read(16)
                if len(head) < 16 or head[:2] != b"O8":
                    continue            # not a package image at all
                crossed += 1
                check(head[15] & (hi | hm) == byte & (hi | hm),
                      "megahigh: %s's built image disagrees with its source"
                      % main_src,
                      why="this row decides from the source and checks every "
                          "current image against it; a disagreement is this "
                          "row's parse being wrong (fix the model) or a "
                          "build with defines it does not know about",
                      got="%s byte 15 = %02Xh" % (art, head[15]),
                      want="%02Xh from `%s`" % (byte, what))

        nhigh += bool(byte & hi)
        nmem += bool(byte & hm)
        for kind, bit, ask in (("high", hi, ask_high),
                               ("highmem", hm, ask_mem)):
            if byte & bit or not ask:
                continue
            need.add((kind, main_src))
            check((kind, main_src) in reg,
                  "megahigh: %s does not declare %s"
                  % (main_src, "OS88_HIGH" if kind == "high"
                     else "OS88_HIGHMEM"),
                  why="KERN-MEGA-PLAN 14.51: every package declares both "
                      "bits, or excepts itself in tests/megahigh.txt with a "
                      "reason - a concrete thing in its code that breaks "
                      "above 1MB, or what would settle it. 'Nobody looked' "
                      "is not one. Declare it in the class byte "
                      "(OS88_HEADER's 4th argument, or CC_STACK_CLASS), after "
                      "running it on kern_mega (docs/BOCHS.md)",
                  got="class `%s`" % what,
                  want="the bit, or a '%s' line with a reason" % kind)

    for (kind, path), reason in sorted(reg.items()):
        if path not in pkgs:
            check(False, "megahigh: tests/megahigh.txt names %s, which is not "
                         "a package" % path,
                  why="the registry is checked against the tree so it cannot "
                      "keep a renamed or deleted package alive",
                  got=path, want="a package under apps/")
            continue
        check((kind, path) in need,
              "megahigh: %s is exempt from '%s' and no longer needs to be"
              % (path, kind),
              why="the list only turns one way: the package declares now (or "
                  "is a loader this row does not ask), so deleting the line "
                  "is the diff saying the work happened",
              got="a '%s' line in tests/megahigh.txt" % kind,
              want="no line")
        check(len(reason) >= MINREASON,
              "megahigh: %s's '%s' exemption carries no reason" % (path, kind),
              why="'a damn good reason' (KERN-MEGA-PLAN 14.51): what in the "
                  "code breaks above 1MB, and what would retire the line",
              got="reason %r" % reason,
              want="at least %d characters" % MINREASON)

    print("t_megahigh: %d package(s); OS88_HIGH %d, OS88_HIGHMEM %d; %d "
          "exemption(s); %d built image(s) cross-checked"
          % (len(pkgs), nhigh, nmem, len(reg), crossed))
    done("t_megahigh")


if __name__ == "__main__":
    main()
