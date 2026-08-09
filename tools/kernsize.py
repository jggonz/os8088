#!/usr/bin/env python3
"""kernsize - what this change cost the kernel, per section and in rungs.

Run by `make` after the kernel assembles. It prints five lines and NEVER
fails the build: the guards in kernel/kernel.asm are what refuse an overrun,
and this is the thing that tells you how close you came.

WHY IT EXISTS.  docs/KERNEL-MEMORY.md's numbers went three budget moves stale
once, and one step stale again while this tool was being written - because
every one of them had to be produced by hand, by bisecting a constant or by
injecting a %warning probe into a copy of kernel.asm.  A number nobody can
produce in one command is a number that stops being produced.

WHAT IT REPORTS, and why it is not just "the total".  The footprint moves in
512-byte RUNGS: .text and .bss share one, .cold has its own, .lowbss and task
0's stack a third.  A change that adds 90 bytes to .text usually moves the
footprint by ZERO - and that is not the same thing as costing nothing.  It
spends 90 bytes of the rung's remaining slack, and the next feature is the
one that finds the rung full and pays the whole 512.  So the report leads
with the per-section bytes and the SUM, and names the slack left in each
rung; a crossing is called out separately because it is the moment the
machine's RAM actually changes.

A KNOB BUILD measures the kernel that knob produces, so `make VIDEO=cga` and
friends pass their own -D flags straight through - a report that described a
different binary from the one on disk would be worse than none.  --bless
refuses them for the same reason: the baseline is the shipped kernel.

PER-MODULE (--modules) is the same measurement one level down: 33 includes,
bracketed with bare labels in each of the five sections, so every byte of
`.text` and `.cold` is attributed to the file that emitted it.  It costs the
SHIPPED KERNEL NOTHING and does not merely claim so - the markers go into a
temporary copy, never into kernel/kernel.asm, and the tool assembles BOTH and
refuses to report unless the two binaries are byte-for-byte identical.  A
measurement that can perturb what it measures is not one.

USAGE
    tools/kernsize.py                  report against the baseline
    tools/kernsize.py --modules        the per-module attribution
    tools/kernsize.py --bless          rewrite baseline AND tables to match
    tools/kernsize.py --json           machine-readable, no baseline needed

The baseline lives in docs/KERNEL-MEMORY.md between the kernsize markers, so
that the document's headline figures cannot drift from the build without the
next `make` saying so.  Bless it in the same commit as the change.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KERNEL = os.path.join(ROOT, "kernel", "kernel.asm")

# The build directory, which is NOT always build/: `make small` and the
# per-typeface targets (SPEC.md 6.2) run a sub-make with BUILD= pointed
# somewhere else, and the generated includes the kernel needs - associco.inc
# always, font8x8.inc under FONT= - are in THAT directory.  Assuming build/
# measured the wrong kernel for `small` silently and could not assemble a
# baked-font one at all.  --build is how the Makefile says which.
BUILDDIR = os.path.join(ROOT, "build")
DOC = os.path.join(ROOT, "docs", "KERNEL-MEMORY.md")
BEGIN = "<!-- kernsize:begin -->"
END = "<!-- kernsize:end -->"

# --- the two builds (docs/KERN-SPLIT-PLAN.md) --------------------------------
# kern_big and kern_small are two kernels off one tree, and BOTH ARE SHIPPED
# PRODUCTS - big by default, small for the 128KB floor. That makes the variant
# a different kind of flag from every other -D this script forwards:
#
#   VIDEO=cga, DISKCNT=1, REDRAWFULL=1 ... are KNOBS. They produce a kernel
#   nobody ships, so blessing one would write a baseline describing a binary
#   that does not exist on any disk, and --bless refuses them.
#
#   KERN_BIG / KERN_SMALL are VARIANTS. Each has a baseline of its own, and
#   each is blessable, because each IS a shipped kernel.
#
# Conflating the two is the bug this section exists to prevent: with one flat
# baseline, `make KERN_SMALL=1` reported small's sections against big's
# figures and every line was a delta of the difference between the products -
# noise that looks exactly like a regression, in the build that is supposed to
# be defended byte by byte.
VARIANTS = ("big", "small")
VARIANT_DEFS = {"-DKERN_BIG": "big", "-DKERN_SMALL": "small"}


def variant_of(nasm_args):
    """Which product is this? Defaults to big, as the Makefile does."""
    for arg in nasm_args:
        if arg in VARIANT_DEFS:
            return VARIANT_DEFS[arg]
    return "big"


def knob_args(nasm_args):
    """The args that make this a KNOB build rather than a shipped variant."""
    return [x for x in nasm_args if x not in VARIANT_DEFS]

# The sections an author can actually move a byte into, in the order the
# ladder lays them out.  `ovl` is here because it has to be watched, not
# because it costs anything: the boot overlay lands in the FAT window and is
# overwritten by the first mount (SPEC.md 2.5), so its rung is somebody
# else's.  It still has a ceiling - see the guard on OVL_SIZE.
SECTIONS = ("text", "bss", "cold", "lowbss", "ovl")

# Which rung each section rounds into.  .text and .bss share one; that is why
# a byte moved from .bss to .lowbss can cost 512 rather than saving anything
# (docs/KERNEL-MEMORY.md, "Which guard binds").
RUNGS = (
    ("image", ("text", "bss"), "imgpara"),
    ("cold", ("cold",), "coldpara"),
    ("low", ("lowbss", "stk0"), "lowpara"),
)

# --- the per-module pass ------------------------------------------------------
MODS_BEGIN = "<!-- BEGIN generated table -->"
MODS_END = "<!-- END generated table -->"
THEMES_BEGIN = "<!-- kernsize:themes -->"
THEMES_END = "<!-- /kernsize:themes -->"
MOD_SECTIONS = ("text", "bss", "lowbss", "cold", "ovl")
INCLUDE = re.compile(r'^%include\s+"([\w.]+)"')
RESIDUAL = "kernel.asm"

# How the modules group, for the theme table.  This lives here rather than in
# the document because it is a JUDGEMENT and the numbers are not: a module
# with no theme stops the report and names itself, which is the right amount
# of friction for something that happens once every few years.
#
# `fprog.inc` is window furniture and not the file system, even though it
# exists entirely for file operations: it is a widget in the menu bar
# (SPEC.md 12.8), and the theme is about where the code lives in the design.
THEMES = (
    ("the file system, end to end",
     ("disk.inc", "diskw.inc", "files.inc", "filecp.inc", "fdlg.inc",
      "loader.inc", "assoc.inc")),
    ("the window system and its furniture",
     ("wm.inc", "ui.inc", "menu.inc", "instance.inc", "desk.inc", "dock.inc",
      "fsx.inc", "clip.inc", "fprog.inc", "toast.inc")),
    ("hardware: drivers, clock, mouse, sound, CPU, XMS",
     ("mouse.inc", "clock.inc", "driver.inc", "snd.inc", "cpudet.inc",
      "xmem.inc")),
    ("drawing: adapters, primitives, glyphs, icons",
     ("vga12.inc", "vgabb.inc", "font.inc", "icons.inc", "viddet.inc",
      "vidsel.inc", "splash.inc")),
    ("the kernel proper: API table, heap, scheduler, events",
     (RESIDUAL, "memory.inc", "sched.inc", "events.inc")),
    ("the Control Panel", ("ctrl.inc",)),
    ("the three built-in kinds", ("apps.inc",)),
)


def measure(nasm_args=()):
    """The ladder, out of NASM, from kernel.asm's own equations.

    Not re-implemented in Python on purpose: a second opinion about how
    KIMG_PARA rounds is a second opinion that can drift, and this file would
    be the last place anyone looked when it did.
    """
    out_fd, out_path = tempfile.mkstemp(suffix=".bin")
    os.close(out_fd)
    try:
        cmd = ["nasm", "-f", "bin", "-w+error", "-w-error=user", "-DKERNSIZE",
               "-I", os.path.join(ROOT, "kernel") + os.sep,
               "-I", BUILDDIR + os.sep,
               *nasm_args, "-o", out_path, KERNEL]
        r = subprocess.run(cmd, capture_output=True, text=True)
    finally:
        try:
            os.unlink(out_path)
        except OSError:
            pass
    m = re.search(r"\bks: (.*?)(?: \[|$)", r.stderr, re.M)
    if not m:
        return None, (r.stderr.strip() or "nasm produced no ks: line")
    vals = {}
    for pair in m.group(1).split():
        k, _, v = pair.partition("=")
        vals[k] = int(v)
    return vals, None


def _markers(i):
    """A bare label in each section, then back to .text.

    Bare labels emit nothing, which is the whole reason this measurement is
    allowed to exist - but see assemble_modules(), which proves it rather
    than asserting it.  Ending in .text matters: every %include in
    kernel.asm sits at .text scope and every module is required to switch
    back before it ends (SPEC.md 4), so the marker must hand over what it
    was given.
    """
    out = [f"section .{s}\nKSM{i}_{s}:\n" for s in MOD_SECTIONS]
    return "".join(out) + "section .text\n"


def instrument(src):
    """kernel.asm with a marker set around every %include, and a %warning
    per module at the end reporting the differences.

    The report block goes at the very END of the file, after the last label
    exists: %assign takes a critical expression, so a label it names has to
    have been seen already.
    """
    mods, out, last_at = [], [], None
    for line in src.splitlines(keepends=True):
        m = INCLUDE.match(line)
        if m:
            out.append(_markers(len(mods)))
            mods.append(m.group(1))
            last_at = len(out) + 1        # ...after the include line itself
        out.append(line)
    if not mods:
        return None, []
    out.insert(last_at, _markers(len(mods)))

    rep = []
    for j in range(len(mods)):
        for s in MOD_SECTIONS:
            rep.append(f"%assign KV{j}_{s} (KSM{j+1}_{s} - KSM{j}_{s})\n")
        rep.append(f"%warning km: {j} "
                   + " ".join(f"{s}=KV{j}_{s}" for s in MOD_SECTIONS) + "\n")
    return "".join(out) + "\n" + "".join(rep), mods


def _nasm(path, out_path, nasm_args=()):
    return subprocess.run(
        ["nasm", "-f", "bin", "-w+error", "-w-error=user",
         "-I", os.path.join(ROOT, "kernel") + os.sep,
         "-I", BUILDDIR + os.sep,
         *nasm_args, "-o", out_path, path],
        capture_output=True, text=True)


def measure_modules(nasm_args=()):
    """Per-module sizes, or (None, why).

    The instrumented source is a TEMPORARY COPY - kernel/kernel.asm is never
    written to - and the two binaries are compared byte for byte before a
    single number is believed.  If a marker ever did emit something, this is
    what says so instead of quietly attributing it to a module.
    """
    src = open(KERNEL).read()
    inst, mods = instrument(src)
    if inst is None:
        return None, "no %include found in kernel.asm"
    tmp = tempfile.mkdtemp(prefix="kernsize.")
    try:
        asm = os.path.join(tmp, "kernel.asm")
        open(asm, "w").write(inst)
        a, b = os.path.join(tmp, "a.bin"), os.path.join(tmp, "b.bin")
        r_plain = _nasm(KERNEL, a, nasm_args)
        r_inst = _nasm(asm, b, nasm_args)
        if r_inst.returncode or not os.path.exists(b):
            tail = (r_inst.stderr.strip().splitlines() or ["?"])[-1]
            return None, f"the instrumented build failed: {tail[:120]}"
        if r_plain.returncode or not os.path.exists(a):
            return None, "the plain build failed"
        if open(a, "rb").read() != open(b, "rb").read():
            # Never reached so far, and it is a hard refusal rather than a
            # warning: an instrumented kernel that differs from the shipped
            # one is measuring something else.
            return None, ("THE MARKERS CHANGED THE BINARY - refusing to "
                          "report (nothing was written to kernel.asm)")
        per = {}
        for line in r_inst.stderr.splitlines():
            m = re.search(r"\bkm: (\d+) (.*?)(?: \[|$)", line)
            if not m:
                continue
            vals = {}
            for pair in m.group(2).split():
                k, _, v = pair.partition("=")
                vals[k] = int(v)
            per[mods[int(m.group(1))]] = vals
        if len(per) != len(mods):
            return None, f"{len(per)} of {len(mods)} modules reported"
        return per, None
    finally:
        for f in os.listdir(tmp):
            os.unlink(os.path.join(tmp, f))
        os.rmdir(tmp)


def rung_bytes(v, para_key):
    return v[para_key] * 16


def rung_used(v, parts):
    return sum(v[p] for p in parts)


def kb(n):
    return f"{n:,}"


def delta(n):
    return f"{n:+,}" if n else "+0"


def report(cur, base, variant="big", out=sys.stdout):
    """Five lines. The sum first, because that is the number an author
    controls; the rungs second, because that is what the machine feels.

    EVERY LINE NAMES ITS VARIANT, and that is not decoration: the two builds
    have separate baselines and separate budgets, and a run of figures with no
    label is a run of figures somebody will compare against the other build's.
    """
    p = lambda s: print(s, file=out)
    tag = "kernsize[%s]:" % variant

    def d(key):
        return None if base is None else cur[key] - base.get(key, cur[key])

    parts = []
    total = 0
    for s in SECTIONS:
        dv = d(s)
        if dv is not None:
            total += dv
            parts.append(f"{s} {kb(cur[s])} {delta(dv)}")
        else:
            parts.append(f"{s} {kb(cur[s])}")
    p(tag + " sections   " + "  ".join(parts)
      + (f"   (sum {delta(total)})" if base is not None else ""))

    crossed = []
    cells = []
    for name, members, para in RUNGS:
        size = rung_bytes(cur, para)
        used = rung_used(cur, members)
        slack = size - used
        cell = f"{name} {kb(size)}"
        if base is not None:
            dsz = size - rung_bytes(base, para)
            was = rung_bytes(base, para) - rung_used(base, members)
            cell += f" {delta(dsz)} ({kb(slack)} left, was {kb(was)})"
            if dsz:
                crossed.append((name, rung_bytes(base, para) // 512,
                                size // 512))
        else:
            cell += f" ({kb(slack)} left)"
        cells.append(cell)
    p(tag + " rungs      " + "   ".join(cells))

    spare = cur["budget"] - cur["ksize"]
    line = (f"{tag} footprint  KERN_SIZE {kb(cur['ksize'])}"
            f" of KERN_BUDGET {kb(cur['budget'])} -> {kb(spare)} spare"
            f" ({spare // 512} step{'' if spare // 512 == 1 else 's'})")
    if base is not None:
        line += (f", was {kb(base['budget'] - base['ksize'])}"
                 f"  [{delta(cur['ksize'] - base['ksize'])}]")
    p(line)

    seg = cur["text"] + cur["bss"]
    p(f"{tag} segment    .text+.bss {kb(seg)} of KERN_CODE_MAX"
      f" {kb(cur['codemax'])} -> {kb(cur['codemax'] - seg)} left")

    # The ladder, because every base in it moves whenever a rung does - and
    # the heap's start is the figure every RAM number in this project falls
    # out of (docs/KERNEL-MEMORY.md, "heap KB = int 12h - this").
    ks = cur["kseg"]
    cold = ks + cur["imgpara"]
    fat = cold + cur["coldpara"]
    low = fat + cur["fatpara"]
    p(f"{tag} ladder     KERNEL {ks:#06x}  COLD {cold:#06x}"
      f"  FAT {fat:#06x}  LOW {low:#06x}  HEAP {cur['kend']:#06x}"
      f" = {cur['kend'] * 16 / 1024:.1f} KB   (heap KB = int 12h"
      f" - {cur['kend'] * 16 / 1024:.1f})")

    if base is None:
        p(tag + " no baseline for this variant in docs/KERNEL-MEMORY.md"
                       " - run `--bless` on it")
    elif crossed:
        for name, a, b in crossed:
            p(f"{tag} *** the {name} rung CROSSED: {a} -> {b}"
              f" steps of 512 - the machine's RAM moved ***")
    elif total:
        p("kernsize: no rung crossed - the machine pays nothing YET, and the"
          " slack above is what the next feature has left")
    else:
        p("kernsize: unchanged")
    return crossed


def read_descriptions():
    """The editorial half of each row, recovered from the table in the doc.

    Descriptions live in the document because they are prose; only the
    numbers are generated. A module the table has never seen comes back
    undescribed and the report says so.
    """
    try:
        doc = open(DOC).read()
    except OSError:
        return {}
    m = re.search(re.escape(MODS_BEGIN) + r"(.*?)" + re.escape(MODS_END),
                  doc, re.S)
    if not m:
        return {}
    out = {}
    for row in m.group(1).splitlines():
        r = re.match(r"\|\s*`([\w.]+)`\s*(?:—\s*)?(.*?)\s*\|", row)
        if r and "(undescribed)" not in r.group(2):
            # ...never round-trip the placeholder: it is a prompt to write
            # prose, and reading it back as prose would silence the prompt.
            out[r.group(1)] = r.group(2)
    return out


def module_rows(per, totals):
    """[(name, description, text, cold, code, bss, lowbss)], biggest first,
    with kernel.asm's own contribution as the residual and a total row that
    is the section totals - which is what proves the attribution complete."""
    desc = read_descriptions()
    rows = []
    for name, v in per.items():
        rows.append([name, desc.get(name, ""), v["text"], v["cold"],
                     v["text"] + v["cold"], v["bss"], v["lowbss"]])
    rows.sort(key=lambda r: -r[4])
    resid = [RESIDUAL, desc.get(RESIDUAL, "")]
    for i, key in enumerate(("text", "cold", None, "bss", "lowbss")):
        if key is None:
            resid.append(resid[2] + resid[3])
        else:
            resid.append(totals[key] - sum(r[2 + i] for r in rows))
    rows.append(resid)
    return rows


def render_modules(rows, totals):
    def cell(n):
        return f"{n:,}" if n else "—"
    out = [MODS_BEGIN,
           "| module | `.text` | `.cold` | code | `.bss` | `.lowbss` |",
           "|---|---:|---:|---:|---:|---:|"]
    for name, d, t, c, code, b, lb in rows:
        label = f"`{name}`" + (f" — {d}" if d else " — **(undescribed)**")
        out.append(f"| {label} | {cell(t)} | {cell(c)} | **{code:,}** |"
                   f" {cell(b)} | {cell(lb)} |")
    out.append(f"| **total** | **{totals['text']:,}** | **{totals['cold']:,}**"
               f" | **{totals['text'] + totals['cold']:,}** |"
               f" **{totals['bss']:,}** | **{totals['lowbss']:,}** |")
    out.append(MODS_END)
    return "\n".join(out)


def render_themes(rows):
    code = {r[0]: r[4] for r in rows}
    total = sum(code.values())
    known = set()
    lines = [THEMES_BEGIN,
             "| theme | bytes | share |", "|---|---:|---:|"]
    body = []
    for name, members in THEMES:
        known |= set(members)
        n = sum(code.get(m, 0) for m in members)
        body.append((name, n))
    for name, n in sorted(body, key=lambda x: -x[1]):
        lines.append(f"| {name} | {n:,} | {100.0 * n / total:.1f}% |")
    lines.append(f"| **total** | **{total:,}** | |")
    lines.append(THEMES_END)
    missing = sorted(set(code) - known)
    return "\n".join(lines), missing


def read_baseline(variant):
    try:
        doc = open(DOC).read()
    except OSError:
        return None
    m = re.search(re.escape(BEGIN) + r"(.*?)" + re.escape(END), doc, re.S)
    if not m:
        return None
    try:
        blob = json.loads(re.sub(r"^```\w*$", "", m.group(1).strip(),
                                 flags=re.M).strip())
    except ValueError:
        return None
    return baseline_pick(blob, variant)


def baseline_pick(blob, variant):
    """One variant out of the block, tolerating the pre-split flat form.

    The old baseline was the figures themselves, with no variant above them.
    That form is read as BIG - which is what it described, big being the
    default and the split having removed nothing - so an un-blessed tree keeps
    reporting instead of going quiet the moment this landed. A missing variant
    answers None and `report` prints the absolute numbers with no deltas, which
    is the honest thing to say about a build nobody has blessed yet.
    """
    if not isinstance(blob, dict):
        return None
    if any(k in blob for k in VARIANTS):
        v = blob.get(variant)
        return v if isinstance(v, dict) else None
    return blob if variant == "big" else None


def write_block(begin, end, body, what):
    doc = open(DOC).read()
    new, n = re.subn(re.escape(begin) + r".*?" + re.escape(end), lambda _: body,
                     doc, flags=re.S)
    if not n:
        print(f"kernsize: no {what} markers in docs/KERNEL-MEMORY.md",
              file=sys.stderr)
        return 1
    if new != doc:
        open(DOC, "w").write(new)
        print(f"kernsize: {what} regenerated in docs/KERNEL-MEMORY.md")
    return 0


def write_baseline(v, variant):
    doc = open(DOC).read()
    # MERGE, never replace: blessing big must not delete small's figures, and
    # the two are blessed by two different commands minutes apart.
    m = re.search(re.escape(BEGIN) + r"(.*?)" + re.escape(END), doc, re.S)
    blob = {}
    if m:
        try:
            old = json.loads(re.sub(r"^```\w*$", "", m.group(1).strip(),
                                    flags=re.M).strip())
            if isinstance(old, dict):
                blob = old if any(k in old for k in VARIANTS) else {"big": old}
        except ValueError:
            blob = {}
    blob[variant] = {k: v[k] for k in sorted(v)}
    body = json.dumps({k: blob[k] for k in sorted(blob)}, indent=2)
    block = f"{BEGIN}\n```json\n{body}\n```\n{END}"
    new, n = re.subn(re.escape(BEGIN) + r".*?" + re.escape(END), block, doc,
                     flags=re.S)
    if not n:
        print("kernsize: no kernsize markers in docs/KERNEL-MEMORY.md",
              file=sys.stderr)
        return 1
    open(DOC, "w").write(new)
    print("kernsize: baseline blessed in docs/KERNEL-MEMORY.md")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--bless", action="store_true",
                    help="rewrite the baseline and the tables in "
                         "docs/KERNEL-MEMORY.md")
    ap.add_argument("--modules", action="store_true",
                    help="the per-module attribution")
    ap.add_argument("--json", action="store_true", help="the raw figures")
    ap.add_argument("--build", metavar="DIR",
                    help="where the generated includes are (default build/); "
                         "a sub-make with BUILD= set needs this")
    # Anything else is handed to NASM verbatim - parse_known_args rather than
    # a positional, because the knobs arrive looking like options
    # (-DVID_FORCE=3) and argparse would claim them.
    a, nasm_args = ap.parse_known_args()
    if a.build:
        global BUILDDIR
        BUILDDIR = os.path.abspath(a.build)
    variant = variant_of(nasm_args)
    knobs = knob_args(nasm_args)

    cur, err = measure(nasm_args)
    if cur is None:
        # NEVER fail the build: an overrun is the guards' job to refuse, and a
        # reporter that can break `make` is a reporter someone will delete.
        print(f"kernsize: could not measure ({err.splitlines()[-1][:120]})",
              file=sys.stderr)
        return 0

    if a.json:
        print(json.dumps({k: cur[k] for k in sorted(cur)}, indent=2))
        return 0

    if a.modules or a.bless:
        per, err = measure_modules(nasm_args)
        if per is None:
            print(f"kernsize: per-module pass: {err}", file=sys.stderr)
            if a.bless:
                return 1
            per = None
        else:
            rows = module_rows(per, cur)
            table = render_modules(rows, cur)
            themes, missing = render_themes(rows)
            if a.modules:
                print(table)
                print()
                print(themes)
            if missing:
                print("kernsize: no theme for " + ", ".join(missing)
                      + " - add it to THEMES in tools/kernsize.py",
                      file=sys.stderr)

    if a.bless:
        if knobs:
            print("kernsize: refusing to bless a knob build (%s) - a baseline"
                  " describes a SHIPPED kernel, and no disk carries this one"
                  % " ".join(knobs), file=sys.stderr)
            return 1
        report(cur, read_baseline(variant), variant)
        rc = write_baseline(cur, variant)
        # THE MODULE AND THEME TABLES ARE THE DEFAULT VARIANT'S, and there is
        # one of each rather than one per variant. Written by whichever bless
        # ran LAST they would silently describe kern_small on a document whose
        # every other figure is the shipped kernel's - an ordering dependency
        # nobody would think to preserve, producing a table that is wrong in a
        # way no gate can see. So only big writes them; `--modules
        # -DKERN_SMALL` prints small's on demand.
        if per is not None and variant == "big":
            rc = write_block(MODS_BEGIN, MODS_END, table, "module table") or rc
            rc = write_block(THEMES_BEGIN, THEMES_END, themes,
                             "theme table") or rc
        return rc

    report(cur, read_baseline(variant), variant)
    return 0


if __name__ == "__main__":
    sys.exit(main())
