#!/usr/bin/env python3
"""Does LOOM pack the same bytes as the host packer, ON THE MACHINE?

    make loom && python3 tests/weavepack.py
    python3 tests/weavepack.py --no-make          # ...with the package as built

**THE GATE WAVE 6 CLOSES ON** (WEAVE-SPEC 12.3, 13.1). WEAVE-SPEC 11.1 says
the pack step has two implementations and one output: *"Loom's pack of every
demo and template must equal `weavesim --pack`'s output byte for byte - two
implementations written from this file, sharing no code, which is what makes
an on-machine compiler trustworthy at all."* This row is what holds them to
it, and it does the one thing its host-side half cannot:

  IT COMPILES ON AN 8086, WITH 16-BIT `int`s, AND READS THE BUNDLE BACK OFF
  THE GUEST'S OWN FLOPPY.

`tests/unit/t_lmpack.py` builds the same five compiler sources with the host
`cc` and diffs the same seven projects in four seconds, which is what makes
them writable at all - but `int` is 32 bits there and 16 bits here, so that
row proves the LOGIC and this one proves the ARITHMETIC. A wave closes on this
one. (WEAVE-SPEC 12.3.3 says the same thing from the other side.)

HOW IT READS THE ANSWER, and it is the part that makes the comparison mean
something: `m.flush()` writes the guest's live floppy image out to a host file
- eframe's Save Floppy As, reached over the debug socket - and the `.WAB` is
then read out of it by `tools/os88flush.py`'s independent FAT12 reader. So the
bytes compared are the bytes on the disk, not something the OS was asked about
itself. Without that a scripted session can make a program save a file and
then has to ask the program whether it worked, which cannot catch the case
where the writer and the reader agree on the same wrong thing.

THE SEVEN PROJECTS ARE FLATTENED ONTO ONE DISK, and the flattening is not
cosmetic. WEAVE-SPEC 11.2 finds a project's companion files by the `.WML`'s
own stem first, so `FORM.WML` + `FORM.WJS` in a directory IS a project by that
rule - and it lets one boot pack all seven, because each is a double-click on
its own `.WML` in the Disk window. The host reference is packed from the SAME
flattened files, so the two packers are given identical input by construction
rather than by a Makefile that has to be kept in step.

WHAT IT DOES **NOT** ASSERT, said here rather than discovered by a reader:

  * WEAVE-SPEC 10.5's SENTENCE identity over the whole `tests/weave/packerr/`
    corpus. Forty cases is forty launches - about twelve minutes of emulator -
    and the sentences are built by the very C this row is running, so
    `t_lmpack` proves them over all forty in four seconds. What runs HERE is
    four representative cases, one per compiler (WML, WJS, FX, WSP), and the
    assertion is the one only a machine can make: Pack refused, and wrote no
    `.WAB` at all. A packer that refuses and writes a truncated bundle anyway
    is the failure this leg exists for.
  * The EDITOR. Typing into a source and packing what was typed is
    `weavesession`'s shape, not this row's: what is under test here is the
    compiler, and a project read off the disk is the same input the host
    packer got.
"""
import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "unit"))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import os88marty as M                                          # noqa: E402
import os88flush                                               # noqa: E402
import os88sym                                                 # noqa: E402
import dispcp                                                  # noqa: E402
from os88mouse import Mouse                                    # noqa: E402
from harness import check, done                                # noqa: E402

BUILD = os.path.join(ROOT, "build")
PROJ = os.path.join(BUILD, "lmproj")
IMG = os.path.join(BUILD, "loompack.img")

# The default machine is weavesmoke's: a GLaBIOS twin is the only pair that
# boots in a tree with no IBM ROM under tools/martypc/roms/, which is every
# checkout of this one.
MACHINE = "os8088_5150_cga_gla"

# WEAVE-SPEC 1.7: Pack is `^P`, read in os88_onkey as a control character, and
# it is NOT a kernel accelerator - no OSAPI_* slot binds a key to a menu item
# (SPEC.md 12.2). A keystroke is also the only way to drive this row without
# a menu drag per project.
K_PACK = "KeyP"

# The flattening (see the header). stem -> the files that make the project,
# each as (source path, destination name).
def flatten():
    """build/lmproj/<STEM>.<EXT> for all seven projects, and the host
    reference bundle beside each. Returns [(stem, ref_path)]."""
    demos = os.path.join(ROOT, "apps", "weave", "demos")
    tmpl = os.path.join(ROOT, "apps", "loom", "templates")
    plan = []
    for stem in ("FORM", "SHEET", "PONG"):
        src = {}
        for ext in ("wml", "wjs", "wfx", "wsp"):
            p = os.path.join(demos, "%s.%s" % (stem.lower(), ext))
            if os.path.isfile(p):
                src[ext.upper()] = p
        plan.append((stem, src))
    for name, stem in (("FORM", "TFORM"), ("SHEET", "TSHEET"),
                       ("GAME", "TGAME"), ("PLAIN", "TPLAIN")):
        d = os.path.join(tmpl, name)
        if not os.path.isdir(d):
            continue
        src = {}
        for f in sorted(os.listdir(d)):
            ext = f.rsplit(".", 1)[-1].upper()
            src[ext] = os.path.join(d, f)
        plan.append((stem, src))

    shutil.rmtree(PROJ, ignore_errors=True)
    os.makedirs(PROJ, exist_ok=True)
    out = []
    for stem, src in plan:
        for ext, p in src.items():
            dst = os.path.join(PROJ, "%s.%s" % (stem, ext))
            text = open(p, "rb").read()
            if ext == "WML":
                # 3.3's <script src=""> names the .WJS outright, so the
                # flattened copy has to name the flattened script. It is the
                # ONE edit the flattening makes, and both packers see it.
                text = text.replace(b'src="MAIN.WJS"', b'src="%s.WJS"'
                                    % stem.encode())
                text = text.replace(b'src="FORM.WJS"', b'src="%s.WJS"'
                                    % stem.encode())
            open(dst, "wb").write(text)
        wml = os.path.join(PROJ, "%s.WML" % stem)
        ref = os.path.join(PROJ, "%s.ref" % stem)
        r = subprocess.run(["python3", "tools/weavesim.py", "--pack", wml,
                            "-o", ref], cwd=ROOT, capture_output=True,
                           text=True)
        if r.returncode != 0:
            check(False, "the host packer packs %s" % stem,
                  "a project the reference packer refuses is a broken "
                  "fixture, not a LOOM defect", got=r.stderr.strip()[:300],
                  want="exit 0")
            continue
        out.append((stem, ref))
    return out


def errcases():
    """Four representative packerr cases, one per compiler, flattened the same
    way. The names are E<n>.WML so that a refusal writes E<n>.WAB or nothing,
    and nothing is the assertion."""
    corpus = os.path.join(ROOT, "tests", "weave", "packerr")
    want = ("unknown-element", "break-outside-loop", "fx-unknown-function",
            "wsp-illegal-char")
    out = []
    for i, name in enumerate(want):
        d = os.path.join(corpus, name)
        if not os.path.isdir(d):
            continue
        stem = "E%d" % i
        for f in sorted(os.listdir(d)):
            ext = f.rsplit(".", 1)[-1].upper()
            if ext not in ("WML", "WJS", "WFX", "WSP"):
                continue
            text = open(os.path.join(d, f), "rb").read()
            if ext == "WML":
                text = text.replace(b'src="MAIN.WJS"',
                                    b'src="%s.WJS"' % stem.encode())
            open(os.path.join(PROJ, "%s.%s" % (stem, ext)), "wb").write(text)
        out.append((stem, name))
    return out


def build_disk(projects, errs):
    files = [os.path.join(BUILD, "loom.o88"), os.path.join(BUILD, "LOOM.OVL")]
    for stem, _ in projects:
        for ext in ("WML", "WJS", "WFX", "WSP"):
            p = os.path.join(PROJ, "%s.%s" % (stem, ext))
            if os.path.isfile(p):
                files.append(p)
    for stem, _ in errs:
        for ext in ("WML", "WJS", "WFX", "WSP"):
            p = os.path.join(PROJ, "%s.%s" % (stem, ext))
            if os.path.isfile(p):
                files.append(p)
    r = subprocess.run(["python3", "tools/os88disk.py", "-o", IMG,
                        "--size", "1440"] + files, cwd=ROOT,
                       capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout + r.stderr)
    return r.returncode == 0


def pack_one(m, mo, S, wx, wy, stem):
    """Open <stem>.WML from the Disk window at (wx, wy), press ^P, close it.

    WAIT ON THE WINDOW, NOT ON THE PICTURE - weavesmoke's own note, and its
    reason applies twice as hard here: a package LOAD draws nothing while it
    runs, and so does a PACK, so `settle` sees perfect stillness partway
    through either and returns. os88marty.until is the bounded wait that the
    retry can catch."""
    before = set(dispcp.win_list(m, S))
    win = None
    for attempt in range(3):
        try:
            dispcp.open_named(m, mo, S, M.settle, wx, wy, "%s.WML" % stem)
            M.until(m, lambda mm: set(dispcp.win_list(mm, S)) - before,
                    "LOOM's window for %s" % stem, limit=25.0)
        except Exception as e:                                  # noqa: BLE001
            if attempt == 2:
                check(False, "%s: LOOM opens on the double-click" % stem,
                      "os88mouse refuses a double-click whose two presses "
                      "straddled the kernel's 9-tick window - a statement "
                      "about the HOST, retried three times and then "
                      "reported. The .WML association is LOOM's "
                      "(WEAVE-SPEC 1.5 step 2)",
                      got=str(e)[:200], want="a package window")
                return 0
            continue
        M.settle(m)
        now = set(dispcp.win_list(m, S)) - before
        if now:
            win = sorted(now)[-1]
            break
    if win is None:
        return 0

    m.ctrl(K_PACK)                      # WEAVE-SPEC 1.7's ^P
    M.settle(m)
    M.settle(m)                         # the write is a disk revolution or
    M.settle(m)                         # three; settle again rather than sleep

    x, y = dispcp.win_rect(m, S, win)[:2]
    mo.click(x + 9, y + 9)              # the close box
    try:
        M.until(m, lambda mm: win not in dispcp.win_list(mm, S),
                "LOOM's window for %s to close" % stem, limit=20.0)
    except Exception:                                           # noqa: BLE001
        check(False, "%s: the window closes again" % stem,
              "each project is one instance and they cannot all be open at "
              "once - a 640KB machine has room for about four (WEAVE-SPEC "
              "1.4). A window that will not close is also the close guard "
              "refusing, which for an UNMODIFIED project it must not do "
              "(SPEC.md 75.1)")
        return 0
    M.settle(m)
    return 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default=MACHINE)
    ap.add_argument("--no-make", action="store_true")
    args = ap.parse_args()

    if not args.no_make:
        r = subprocess.run(["make", "loom"], cwd=ROOT, capture_output=True,
                           text=True)
        if r.returncode != 0:
            print(r.stdout[-2000:] + r.stderr[-2000:])
            check(False, "make loom", "the package under test has to build",
                  got="non-zero", want="exit 0")
            done("weavepack")
            return
    if not os.path.isfile(os.path.join(BUILD, "loom.o88")):
        print("weavepack: SKIP - build/loom.o88 is not here; the C toolchain "
              "(tools/setup-cc.sh) is what builds it")
        done("weavepack")
        return

    projects = flatten()
    errs = errcases()
    if not check(build_disk(projects, errs), "the pack disk builds",
                 "one 1.44MB floppy: LOOM.O88, LOOM.OVL and the seven "
                 "flattened projects (WEAVE-SPEC 11.2's stem rule)"):
        done("weavepack")
        return

    boot = os.path.join(BUILD, "os8088-360.img")
    boot = os.path.join(BUILD, "os8088-360.img")
    with M.launch(boot, apps=IMG, machine=args.machine) as m:
        M.settle(m)
        S = os88sym.linear
        mo = Mouse(marty=m)
        desk = set(dispcp.win_list(m, S))
        dispcp.open_drive(m, mo, S, M.settle, "B")
        M.until(m, lambda mm: set(dispcp.win_list(mm, S)) - desk,
                "drive B's Disk window to open", limit=20.0)
        disk = sorted(set(dispcp.win_list(m, S)) - desk)[-1]
        wx, wy = dispcp.win_rect(m, S, disk)[:2]
        for stem, _ in projects:
            pack_one(m, mo, S, wx, wy, stem)
        for stem, _ in errs:
            pack_one(m, mo, S, wx, wy, stem)
        m.flush(1, IMG)

    vol = os88flush.Volume(open(IMG, "rb").read(), IMG)
    names = set(n.upper() for n in vol.names())
    for stem, ref in projects:
        want = open(ref, "rb").read()
        wab = "%s.WAB" % stem
        if not check(wab in names, "%s: LOOM wrote it" % stem,
                     "WEAVE-SPEC 11.4: Pack writes the .WAB whole on the UI "
                     "task. No file at all means the pack refused, and the "
                     "sidebar has the sentence saying why",
                     got=sorted(names), want=wab):
            continue
        got = vol.read(wab)
        i = -1
        for k in range(min(len(got), len(want))):
            if got[k] != want[k]:
                i = k
                break
        if i < 0 and len(got) != len(want):
            i = min(len(got), len(want))
        check(got == want,
              "%s: %d bytes, byte for byte the host packer's" % (stem,
                                                                len(want)),
              "WEAVE-SPEC 11.1's gate, and the whole reason an on-machine "
              "compiler can be trusted. The first difference is at +0x%04X; "
              "`python3 tools/lmdiff.py` names the section" % max(i, 0),
              got=got, want=want)

    for stem, name in errs:
        check("%s.WAB" % stem not in names,
              "%s (%s): the refusal wrote NOTHING" % (stem, name),
              "WEAVE-SPEC 10.5's refusals are checked sentence for sentence "
              "by tests/unit/t_lmpack.py over all forty cases; what only a "
              "machine can say is that a refused pack left no truncated "
              "bundle on the disk",
              got="%s.WAB is on the disk" % stem, want="no file")

    done("weavepack")


if __name__ == "__main__":
    main()
