#!/usr/bin/env python3
"""No SHIPPED package or driver carries kern_mega-only code it was not allowed.

    python3 tests/unit/t_megapkg.py            the gate
    python3 tests/unit/t_megapkg.py --list     every marker site, by file
    python3 tests/unit/t_megapkg.py --sizes    each arm against its shipped build

THE OWNER'S RULE (docs/plans/KERN-MEGA-PLAN.md 14.66): "mega specific work
that more than trivially increases the disk size is gated to mega builds
only." A package's kern_mega-only code is assembled under `%ifdef PKG_MEGA`
(apps/os88mega.inc, SPEC.md 20.18), no shipped recipe defines it, and `make
mega` builds the arm onto kern_mega's own disks. This row is what makes that
a rule rather than a habit. Three checks:

  1. THE MARKER CENSUS. kern_mega-only code has to KNOW it is on kern_mega,
     and there are exactly a handful of ways to know: CPU_F_PM (the flag
     OSAPI_CPU_INFO answers under protected mode) and the cells and macro
     that exist for selectors - OSAPI_MEM_CODESEL, OSAPI_MEM_SUBSEG,
     OSAPI_XMEM_RUN, OS88_CODESEL. Every shipped build's LIVE source is
     walked: its main file and every %include it names, through a model of
     nasm's %ifdef/%ifndef/%else/%endif with the SHIPPED recipe's own -D
     defines, PKG_MEGA and KERN_MEGA unset (an `%if` over an expression
     counts as live - the census errs towards a site). A code line naming a
     marker is a site; each FILE's sites, the most any one shipped build
     makes live, must equal tests/megapkg.txt's count for that file, and a
     file not in the registry may have none. The registry turns ONE WAY: a
     count above what the source uses fails too, so a conversion lowers the
     line in the same diff.
  2. NO PART ASSEMBLED FOR PROTECTED MODE. A shipped `.o88` rule may not
     take a `--part` whose recipe passes a kern_mega define (-DPKG_MEGA,
     -DVB_PM, -DKERN_MEGA, -DDOS_MEGA, -DKD_MEGA), and no shipped recipe may
     pass one bare. This is the check 2e77a97e's VIDEO.O88 fails: its part 1,
     vblkpm.bin, was 5,023 bytes of block decoder assembled -DVB_PM - 1,798
     packed bytes of every apps disk that no 8088 can fetch.
  3. EVERY ARM IS BUILT AND SHIPPED WHERE IT BELONGS. A package whose live
     shipped graph names PKG_MEGA has an arm in $(MEGAPKGS) (else its
     kern_mega code is built nowhere); every arm's recipe passes -DPKG_MEGA;
     and on a CURRENT build/megaapps*.img each armed package is the arm, not
     the shipped build (MEGASUB forgotten in a list ships the plain one -
     correct and merely slower or refusing, which is exactly why a test and
     not a reader has to notice).

WHAT IT DOES NOT SEE, said so nobody reads more into a green row: code that
is kern_mega-shaped without naming a marker - a `test dl, 4` that asks
whether a claim is an LDT selector, an 8-paragraph granule. Those ride a
marker's arm when they are dead without it (a selector claim exists only for
an OS88_HIGHMEM package), and t_megagrid.py and t_megahigh.py hold the rest.

THE FAILING CASES (docs/WRITING-TESTS.md 1), all tried when this was written:
the tree at 2e77a97e fails six ways - VIDEO.O88's, VIDEOD's and VIDEOR's
vblkpm part (2), and the live CPU_F_PM sites of video.asm, os88spkfx.inc and
midirack.asm (1); delete the `%ifdef PKG_MEGA` pair round os88spkfx_init's
CPU_F_PM test and (1) names the file and the count; take
$(MEGAAPPDIR)/audio.o88 out of $(MEGAPKGS) and (3) names AUDIO. What (1)
cannot see is an arm's code with NO marker in it - os88spk.inc's second ISR
family is only data-selector stores - so its `%ifdef` pair is held by (3)
and the arm's size (`make megapkgs`), not by the census.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from harness import check, done                           # noqa: E402

ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
REGISTRY = os.path.join(ROOT, "tests", "megapkg.txt")
BUILD = os.environ.get("OS88_TREE") or "build"

MARKERS = re.compile(r"\b(CPU_F_PM|OSAPI_MEM_CODESEL|OSAPI_MEM_SUBSEG|"
                     r"OSAPI_XMEM_RUN|OS88_CODESEL)\b")
MEGADEFS = ("PKG_MEGA", "VB_PM", "KERN_MEGA", "DOS_MEGA", "KD_MEGA")
# THE SDK'S OWN FILES ARE NOT A PACKAGE'S CODE: they DEFINE the markers (and
# OS88_CODESEL's body tests CPU_F_PM, OS88_MEGA_REFUSE's too), so walking them
# would count every package that includes them. A package that USES one names
# the macro or the cell in its own file, which is what is counted.
SDK = {"apps/os88api.inc", "drivers/os88drv.inc", "apps/os88mega.inc"}
INCLUDE = re.compile(r'^\s*%include\s+"([^"]+)"')
LABEL = re.compile(r"^[A-Za-z_.$@?][\w.$@?#~]*:\s*")
MINREASON = 30
MEGA_IMGS = ("megaapps.img", "megaapps120.img")


def read(path):
    with open(os.path.join(ROOT, path), errors="replace") as fh:
        return fh.read()


def code(line):
    """A line without its nasm comment (no line this reads has `;` in a
    string that matters)."""
    return line.split(";", 1)[0].rstrip()


def makefile():
    return re.sub(r"\\\n\s*", " ", read("Makefile"))


def strip_ifs(line):
    """A recipe line with every `$(if ...)` removed - a knob's, or the
    KERN_MEGA sub-make's arm, neither of which the shipped build passes."""
    prev = None
    while prev != line:
        prev = line
        line = re.sub(r"\$\(if [^()]*(?:\([^()]*\)[^()]*)*\)", " ", line)
    return line


def recipes(mk, prefix=r"\$\(BUILD\)"):
    """{stem: (source, plain defines, -I dirs)} for every `<prefix>/<stem>.bin:`
    rule that assembles an apps/ or drivers/ source."""
    out = {}
    cur = first = None
    for ln in mk.split("\n"):
        m = re.match(r"^" + prefix + r"/([A-Za-z0-9_%-]+)\.bin\s*:\s*(\S*)", ln)
        if m:
            cur, first = m.group(1), m.group(2)
            continue
        if not ln.startswith("\t"):
            cur = None
            continue
        if cur is None or "NASM" not in ln:
            continue
        src = re.search(r"-o\s+\$@\s+((?:apps|drivers)/\S+\.asm)", ln)
        if not src and re.search(r"-o\s+\$@\s+\$<", ln) and \
                re.fullmatch(r"(?:apps|drivers)/\S+\.asm", first or ""):
            src = re.match(r"(.*)", first)
        if not src:
            continue
        bare = strip_ifs(ln)
        defs = set(re.findall(r"(?<!\S)-D(\w+)", bare))
        incs = re.findall(r"(?<!\S)-I\s*(\S+)", bare)
        out[cur] = (src.group(1), defs, [i for i in incs if "$" not in i])
    return out


def o88_rules(mk, prefix=r"\$\(BUILD\)"):
    """{stem: [part .bin paths named by --part]} for `<prefix>/<stem>.o88:`
    rules, with VBLKPARTS-style variables expanded one level."""
    vars_ = dict(re.findall(r"^([A-Z][A-Z0-9_]*)\s*:?=\s*(.*)$", mk, re.M))
    out = {}
    cur = None
    for ln in mk.split("\n"):
        m = re.match(r"^" + prefix + r"/([A-Za-z0-9_-]+)\.o88\s*:", ln)
        if m:
            cur = m.group(1)
            out.setdefault(cur, [])
            continue
        if not ln.startswith("\t"):
            cur = None
            continue
        if cur is None:
            continue
        keep = ("BUILD", "OS88PKG", "OS88PKGB", "MEGAAPPDIR", "MEGADIR")
        ln = re.sub(r"\$\(([A-Z][A-Z0-9_]*)\)",
                    lambda v: vars_.get(v.group(1), v.group(0))
                    if v.group(1) not in keep else v.group(0), ln)
        ln = re.sub(r"\$\(addprefix --part ,([^()]*(?:\([^()]*\)[^()]*)*)\)",
                    lambda v: " ".join("--part " + w
                                       for w in v.group(1).split()), ln)
        out[cur] += re.findall(r"--part\s+(\$\((?:BUILD|MEGAAPPDIR)\)/"
                               r"[A-Za-z0-9_/-]+)\.bin", ln)
        m = re.search(r"\$\(OS88PKGB?\)\s+(\$\((?:BUILD|MEGAAPPDIR)\)/"
                      r"[A-Za-z0-9_/-]+)\.bin", ln)
        if m:
            out[cur].append("main:" + m.group(1))
    return out


def stem_of(ref):
    """`$(BUILD)/pxgif` -> ('BUILD', 'pxgif')"""
    m = re.match(r"\$\((\w+)\)/(.*)$", ref.replace("main:", ""))
    return (m.group(1), m.group(2)) if m else (None, ref)


def resolve(inc, cur, incdirs):
    for d in [os.path.dirname(cur)] + incdirs + ["apps", "drivers"]:
        cand = os.path.normpath(os.path.join(d, inc)).replace("\\", "/")
        if cand.startswith(BUILD + "/") or cand.startswith("build/"):
            return None                 # generated: not a source
        if os.path.isfile(os.path.join(ROOT, cand)):
            return cand
    return None


def live_sites(main, defines, incdirs):
    """{file: [(line number, text)]} - marker sites nasm would assemble in the
    build of `main` with `defines`, through every %include it reaches - and
    the set of files whose live code names PKG_MEGA."""
    defs = set(defines)
    sites, pkgmega, seen = {}, set(), set()
    # A MACRO WHOSE BODY TESTS PKG_MEGA (PiXEL's PXSW, OS88_MEGA_REFUSE) puts
    # the arm's code into whoever INVOKES it, not whoever defines it: a part
    # that includes pxpart.inc and stores nothing is the same in both builds
    megamacros = {"OS88_MEGA_REFUSE"}

    def walk(path, depth):
        if depth > 20:
            return
        seen.add(path)
        stack = []                      # [live, taken, unknown]
        inmacro = None
        for n, raw in enumerate(read(path).splitlines(), 1):
            ln = code(raw).strip()
            if not ln:
                continue
            tok = ln.split()
            d = tok[0].lower()
            arg = tok[1] if len(tok) > 1 else ""
            if d in ("%macro", "%imacro"):
                inmacro = arg
                continue
            if d in ("%endmacro", "%endm"):
                inmacro = None
                continue
            if inmacro is not None:
                if d in ("%ifdef", "%ifndef") and arg == "PKG_MEGA":
                    megamacros.add(inmacro)
                if MARKERS.search(ln) and path not in SDK:
                    sites.setdefault(path, []).append((n, ln))
                m = INCLUDE.match(raw)  # (os88parts.inc's OS88_PARTS_CODE
                if m:                   # is a macro that %includes the body)
                    f = resolve(m.group(1), path, incdirs)
                    if f and f not in SDK:
                        walk(f, depth + 1)
                continue                # a body is code where it is invoked
            outer = all(s[0] or s[2] for s in stack)
            if d in ("%ifdef", "%ifndef"):
                c = (arg in defs) == (d == "%ifdef")
                stack.append([c, c, False])
                if arg == "PKG_MEGA" and outer:
                    pkgmega.add(path)
                continue
            if d.startswith("%if"):
                stack.append([True, True, True])        # unknown: live
                continue
            if d in ("%elifdef", "%elifndef") and stack:
                s = stack[-1]
                c = (arg in defs) == (d == "%elifdef")
                s[0] = (not s[1]) and c
                s[1] = s[1] or c
                continue
            if d.startswith("%elif") and stack:
                stack[-1] = [True, True, True]
                continue
            if d == "%else" and stack:
                s = stack[-1]
                if not s[2]:
                    s[0] = not s[1]
                    s[1] = True
                continue
            if d == "%endif" and stack:
                stack.pop()
                continue
            if not outer:
                continue
            if d in ("%define", "%xdefine", "%assign") and arg:
                defs.add(arg)
                continue
            if d == "%undef" and arg:
                defs.discard(arg)
                continue
            m = INCLUDE.match(raw)
            if m:
                f = resolve(m.group(1), path, incdirs)
                if f and f not in SDK:
                    walk(f, depth + 1)
                continue
            if MARKERS.search(ln) and path not in SDK:
                sites.setdefault(path, []).append((n, ln))
            first = LABEL.sub("", ln).split()
            if first and first[0] in megamacros:
                pkgmega.add(path)

    walk(main, 0)
    return sites, pkgmega


def registry():
    out, bad = {}, []
    if not os.path.isfile(REGISTRY):
        return out, bad
    lines = open(REGISTRY).read().split("\n")
    i = 0
    while i < len(lines):
        raw = lines[i]
        i += 1
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        body, _, reason = raw.partition("#")
        while i < len(lines) and lines[i].startswith(" ") \
                and lines[i].lstrip().startswith("#"):
            reason += " " + lines[i].lstrip()[1:].strip()
            i += 1
        parts = body.split()
        if len(parts) != 2 or not parts[1].isdigit():
            bad.append(raw)
            continue
        out[parts[0]] = (int(parts[1]), reason.strip())
    return out, bad


def shipped(mk):
    """{stem: recipe} - the SHIPPED build of every main source: the recipe
    passing the fewest defines (a bench or a diagnostic build is the same
    source with something added), the stem named like the source breaking a
    tie. Test packages (tests/) are not here: recipes() reads apps/ and
    drivers/ sources only."""
    by_src = {}
    for stem, r in recipes(mk).items():
        if "%" in stem:
            continue                    # a pattern rule: a variant family
        by_src.setdefault(r[0], []).append((stem, r))
    out = {}
    for src, rs in by_src.items():
        base = os.path.splitext(os.path.basename(src))[0]
        stem, r = sorted(rs, key=lambda x: (len(x[1][1]), x[0] != base))[0]
        out[stem] = r
    return out


def arms(mk):
    """{stem: recipe} of $(MEGAAPPDIR)'s builds, and the $(MEGAPKGS) list."""
    a = recipes(mk, prefix=r"\$\(MEGAAPPDIR\)")
    m = re.search(r"^MEGAPKGS\s*:?=\s*(.*)$", mk, re.M)
    pk = re.findall(r"\$\(MEGAAPPDIR\)/([a-z0-9_-]+)\.o88", m.group(1)) if m \
        else []
    return a, pk


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else ""
    mk = makefile()
    ship = shipped(mk)
    arm, megapkgs = arms(mk)
    check(len(ship) >= 60, "megapkg: found too few shipped recipes",
          why="the census is only as wide as the recipes it parses; a short "
              "list is a Makefile spelling this row no longer reads",
          got="%d" % len(ship), want="the tree's packages and drivers")

    if mode == "--sizes":
        sys.path.insert(0, os.path.join(ROOT, "tools"))
        import os88pkg
        for p in megapkgs:
            a = os.path.join(ROOT, BUILD, "megak", "apps", p + ".o88")
            s = os.path.join(ROOT, BUILD, p + ".o88")
            if not (os.path.isfile(a) and os.path.isfile(s)):
                print("megapkg: %-10s not built" % p)
                continue
            fa, fs = open(a, "rb").read(), open(s, "rb").read()
            ia, is_ = os88pkg.image_unwrap(fa), os88pkg.image_unwrap(fs)
            print("megapkg: %-10s shipped %6d file / %6d image   kern_mega "
                  "arm %6d / %6d   (+%d file, +%d image)"
                  % (p.upper(), len(fs), len(is_), len(fa), len(ia),
                     len(fa) - len(fs), len(ia) - len(is_)))
        return

    reg, bad = registry()
    for raw in bad:
        check(False, "megapkg: tests/megapkg.txt line is malformed",
              why="one line per file: `<path> <sites>  # <reason>`",
              got=raw.strip(), want="<path> <count>  # reason")

    worst = {}                         # file -> (sites, the build it was in)
    uses_pkgmega = {}                  # stem -> files naming PKG_MEGA
    for stem, (src, defs, incs) in sorted(ship.items()):
        for d in MEGADEFS:
            check(d not in defs,
                  "megapkg: the shipped recipe of %s passes -D%s" % (stem, d),
                  why="a kern_mega define on a shipped nasm line puts the "
                      "arm's code on every apps disk; the arm is "
                      "$(MEGAAPPDIR)'s rule (KERN-MEGA-PLAN 14.66)",
                  got="-D%s on $(BUILD)/%s.bin" % (d, stem),
                  want="it only on a $(MEGAAPPDIR) rule")
        sites, pm = live_sites(src, defs - set(MEGADEFS), incs)
        if pm:
            uses_pkgmega[stem] = sorted(pm)
        for f, s in sites.items():
            if f not in worst or len(s) > len(worst[f][0]):
                worst[f] = (s, stem)

    if mode == "--list":
        for f in sorted(worst):
            s, stem = worst[f]
            print("%s  %d  (in %s)" % (f, len(s), stem))
            for n, ln in s:
                print("    %5d  %s" % (n, ln))
        return

    for f, (s, stem) in sorted(worst.items()):
        allow = reg.get(f, (0, ""))[0]
        check(len(s) <= allow,
              "megapkg: %s assembles %d kern_mega marker site(s) into a "
              "SHIPPED build (%s), %d allowed" % (f, len(s), stem, allow),
              why="kern_mega-only code goes under %ifdef PKG_MEGA "
                  "(apps/os88mega.inc, SPEC.md 20.18), so an 8088's disk "
                  "carries none of it - the owner's rule is that kern_mega "
                  "work that more than trivially grows a shipped disk is "
                  "built for kern_mega alone. `make mega` assembles the arm "
                  "(-DPKG_MEGA) onto kern_mega's own disks; the SHIPPED "
                  "build must still be correct there - run low and plain, "
                  "refuse the one tier, or refuse to start with "
                  "OS88_MEGA_REFUSE, and say which at the place. When the "
                  "site is that fail-safe or trivially small, a line in "
                  "tests/megapkg.txt with the reason instead. `--list` "
                  "prints the sites",
              got="; ".join("%d: %s" % x for x in s[:4]),
              want="at most %d" % allow)
    for f, (allow, reason) in sorted(reg.items()):
        have = len(worst.get(f, ([], ""))[0])
        check(have == allow,
              "megapkg: tests/megapkg.txt allows %s %d site(s) and the "
              "shipped builds assemble %d" % (f, allow, have),
              why="the registry turns one way: a site gated away or deleted "
                  "lowers the line in the same diff, so the file cannot rot "
                  "into a list of things that used to be true",
              got="%d live" % have, want="%d, or the line lowered" % allow)
        check(len(reason) >= MINREASON,
              "megapkg: %s's allowance carries no reason" % f,
              why="what the site is for and why it is not the arm's",
              got=repr(reason), want="at least %d characters" % MINREASON)

    # --- 2. no part assembled for protected mode -----------------------------
    allr = recipes(mk)
    shipped_o88 = o88_rules(mk)
    part_of = {}
    for stem, parts in sorted(shipped_o88.items()):
        if stem not in ship and stem not in allr:
            continue
        for ref in parts:
            pre, p = stem_of(ref)
            if pre != "BUILD":
                continue
            part_of.setdefault(p, stem)
            if ref.startswith("main:"):
                continue
            r = allr.get(p)
            if not r:
                continue
            for d in MEGADEFS:
                check(d not in r[1],
                      "megapkg: %s.o88 carries part %s.bin, assembled -D%s"
                      % (stem, p, d),
                      why="a part built for protected mode is bytes of every "
                          "shipped disk no real-mode kernel fetches; it "
                          "belongs on the arm's rule ($(MEGAAPPDIR)/%s.o88)"
                          % stem,
                      got="--part $(BUILD)/%s.bin" % p,
                      want="that part on the kern_mega arm alone")

    # --- 3. every arm built and shipped -----------------------------------
    mega_o88 = o88_rules(mk, prefix=r"\$\(MEGAAPPDIR\)")
    for stem, files in sorted(uses_pkgmega.items()):
        owner = part_of.get(stem, stem)
        if owner != stem and owner in megapkgs:
            subst = any(stem_of(r) == ("MEGAAPPDIR", stem)
                        for r in mega_o88.get(owner, []))
            check(subst, "megapkg: %s is a part of %s, names PKG_MEGA, and "
                         "the arm takes the plain build of it" % (stem, owner),
                  why="the arm's rule must name $(MEGAAPPDIR)/%s.bin, the "
                      "part assembled -DPKG_MEGA" % stem,
                  got="$(MEGAAPPDIR)/%s.o88's parts" % owner,
                  want="$(MEGAAPPDIR)/%s.bin among them" % stem)
            continue
        check(stem in megapkgs,
              "megapkg: %s names PKG_MEGA and has no kern_mega arm" % stem,
              why="its kern_mega code is then assembled into NO build at all: "
                  "a new arm is a $(MEGAAPPDIR)/%s.o88 rule assembling the "
                  "package -DPKG_MEGA and a $(MEGAPKGS) entry, in the same "
                  "diff (SPEC.md 20.18, KERN-MEGA-PLAN 14.66)"
                  % stem,
              got="PKG_MEGA in " + ", ".join(files), want="an arm")
    for p in megapkgs:
        built = [stem_of(r)[1] for r in mega_o88.get(p, [])
                 if stem_of(r)[0] == "MEGAAPPDIR"]
        ok = [b for b in built if b in arm and "PKG_MEGA" in arm[b][1]]
        check(bool(ok),
              "megapkg: $(MEGAPKGS) names %s and its $(MEGAAPPDIR) rule "
              "assembles nothing with -DPKG_MEGA" % p,
              why="the arm IS the build with the define - its image, or a "
                  "part of it (PiXEL's arm is its parts)",
              got=built or "no $(MEGAAPPDIR)/%s.o88 rule" % p,
              want="a $(MEGAAPPDIR) .bin passing -DPKG_MEGA")
        check(p in ship, "megapkg: the arm %s has no shipped build" % p,
              why="an arm is a second build of a SHIPPED package; MEGASUB "
                  "substitutes it for $(BUILD)/%s.o88" % p,
              got="no $(BUILD)/%s.bin" % p, want="one")
    disks(megapkgs)

    print("t_megapkg: %d shipped build(s), %d file(s) with a marker site, "
          "%d arm(s): %s" % (len(ship), len(worst), len(megapkgs),
                             " ".join(megapkgs)))
    done("t_megapkg")


def disks(megapkgs):
    try:
        from t_image import Vol
    except Exception as e:                                  # pragma: no cover
        check(False, "megapkg: the FAT12 reader loads", got=str(e),
              want="t_image.Vol")
        return
    want = {}
    for p in megapkgs:
        a = os.path.join(ROOT, BUILD, "megak", "apps", p + ".o88")
        s = os.path.join(ROOT, BUILD, p + ".o88")
        if os.path.isfile(a) and os.path.isfile(s):
            nm = (p[:8] + ".O88").upper()
            want[nm] = (os.path.getsize(a), os.path.getsize(s),
                        max(os.path.getmtime(a), os.path.getmtime(s)))
    for img in MEGA_IMGS:
        path = os.path.join(ROOT, BUILD, img)
        if not os.path.isfile(path) or not want:
            continue
        if os.path.getmtime(path) < max(w[2] for w in want.values()):
            continue                    # stale: `make mega` decides
        with open(path, "rb") as f:
            v = Vol(f.read(), img)
        found = set()
        for folder, name11, attr, clus, size in v.walk():
            nm = (name11[:8].decode("ascii", "replace").strip() + "." +
                  name11[8:].decode("ascii", "replace").strip()).upper()
            if nm not in want:
                continue
            found.add(nm)
            asz, ssz, _ = want[nm]
            check(size == asz or asz == ssz,
                  "megapkg: %s: %s%s is not the kern_mega arm" % (img, folder,
                                                                  nm),
                  why="kern_mega's apps disk carries each package's arm; a "
                      "list that forgot MEGASUB ships the plain build there",
                  got="%d bytes" % size, want="%d (the arm)" % asz)
        for nm in want:
            check(nm in found, "megapkg: %s carries no %s" % (img, nm),
                  why="the arm is on kern_mega's apps disk where the shipped "
                      "build is on the shipped one", got="absent",
                  want="the arm")


if __name__ == "__main__":
    main()
