#!/usr/bin/env python3
"""Does the LAST RELEASE's software still run on this tree? (SPEC.md 20.2.0.1)

    python3 tests/abicompat.py                    # base = the latest release
    python3 tests/abicompat.py --base <commit>    # ...or any commit
    python3 tests/abicompat.py --static           # host legs only, no emulator
    python3 tests/abicompat.py -k CALC -k MINES   # launch only these packages

`PKG_FMT` and `DRV_VER` are the two bytes that stand between a package or a
driver built against one kernel and a far call into another. They protect
anybody only if they MOVE when the surface does, and `tools/abi.lock.json`
makes moving them a written decision - but the lock judges a change to the
SDK's TEXT, one commit at a time, and nothing ever ran what a release
SHIPPED on the kernel the branch has since become. That is this row.

THE BASE IS THE LATEST RELEASE: the newest commit whose subject reads
`Release YYYYMMDD` (the release skill writes one per cut, touching
`apps/install/OS88VER.TXT`), looked for on `origin/main`, then `main`, then
every ref. It is built ONCE into `build/abicompat/<sha>/` - a git worktree of
its own, `make BUILD=build` inside it - and reused by every later run, so the
first run pays a full build and the rest pay nothing.

THE LEGS. Each prints `ok`, `DECLARED` (an incompatibility the ABI lock
recorded as a bump, which is the system working) or `FAIL`:

  A  API cells. Every `OSAPI_*` the release's SDK names sits at the same
     table offset here. A moved or reused cell is a far call into the wrong
     routine for every package built against the release.
  B  The lock's chain. The release's own `abi.lock.json` state for each guard
     is a link in this tree's history, so every SDK change since was
     decided in writing; and every bump since is listed.
  C  SDK constants. Every `equ`/`%define` in the release's `os88api.inc` and
     `os88drv.inc` keeps its value. A changed one is a re-contract the
     version byte may not have caught: it must be either a guard that was
     bumped (B) or acknowledged in `tests/abicompat.txt` with a reason.
  D  Headers. Every package of the release carries this tree's `PKG_FMT`
     and every driver this tree's `DRV_VER` - or the difference is a
     recorded bump, and then the loader's refusal is the designed answer.
  E  Streams. Every packed thing the release shipped - packages, drivers,
     `CZ` data files - expands to the same bytes with THIS tree's decoder
     as with the release's own. (`kernel/lz.inc` mirrors `tools/os88lz.py`;
     an escape word added to the format must not be one an old stream can
     contain.)
  F  The machine (MartyPC). Every package on the release's 360KB disks - the
     apps disk and the category disks (SPEC.md 24.6), which is where the
     PARTED packages are (Sheet, Word, Pixel, RealDMG, WIN16, ...) - is
     launched on THIS tree's kernel and, as the control, on the RELEASE's
     own kernel: whatever opened a window there must open one here. The
     control is what makes a self-refusal (no sound card, no drive) a
     non-event rather than a failure, and it is driven by the release's own
     tools so its kernel's symbols resolve.

What this row CANNOT see is a contract that moved in PROSE only - a driver
verb that now refuses a form it used to take, say - unless a package on those
disks takes that path at launch. Those are `tests/abicompat.txt`'s
`note:` lines, kept for the record and cleared at the next release.

RED ARMS (docs/WRITING-TESTS.md 1): `--red-version` stamps one release
package with another PKG_FMT on a scratch copy of the apps disk, and leg F
must go red on it; `--red-cell` pretends one cell moved, and leg A must.
"""
import argparse
import fcntl
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TOOLS = os.path.join(ROOT, "tools")
ALLOW = os.path.join(HERE, "abicompat.txt")

SYS_IMG = "os8088-360.img"
# the release's 360KB disks whose packages leg F launches, in order; a
# package already launched off an earlier disk is not launched again. The
# category disks (SPEC.md 24.6) carry their programs at the ROOT, and are
# where the parted packages are - Sheet, Word, Pixel, RealDMG, WIN16, ...
LAUNCH_DISKS = (("apps360.img", ("APPS", "GAMES")),
                ("office360.img", ("",)), ("network360.img", ("",)),
                ("games360.img", ("",)), ("util360.img", ("",)),
                ("emu360.img", ("",)), ("media360.img", ("",)))

RESULTS = []


def say(leg, verdict, msg):
    RESULTS.append((leg, verdict, msg))
    print("%-4s %-8s %s" % (leg, verdict, msg), flush=True)


def git(*a, cwd=ROOT, check=True):
    r = subprocess.run(("git",) + a, cwd=cwd, capture_output=True, text=True)
    if check and r.returncode:
        raise RuntimeError("git %s: %s" % (" ".join(a), r.stderr.strip()))
    return r.stdout.strip()


# =============================================================================
# the base
# =============================================================================

def find_release():
    """The newest `Release YYYYMMDD` commit, by commit date."""
    if git("rev-parse", "--is-shallow-repository") == "true":
        raise SystemExit(
            "abicompat: this clone is SHALLOW, so the release commit may be "
            "missing and every ancestry answer is wrong. "
            "`git fetch --unshallow` first (CLAUDE.md, docs/UPSTREAM.md "
            "Rule 0)")
    for ref in ("refs/remotes/origin/main", "refs/heads/main", "--all"):
        args = ["log", "-1", "--format=%H %s", "-E",
                "--grep=^Release [0-9]{8}", "--", "apps/install/OS88VER.TXT"]
        if ref == "--all":
            args.insert(1, "--all")
        else:
            if not git("rev-parse", "--verify", "-q", ref, check=False):
                continue
            args.insert(1, ref)
        out = git(*args, check=False)
        if out:
            sha, _, subj = out.partition(" ")
            return sha, subj
    raise SystemExit("abicompat: no `Release YYYYMMDD` commit found on "
                     "origin/main, main or any ref - pass --base")


def build_base(sha):
    """A worktree of the base, built once into its own build/ and reused."""
    top = os.path.join(ROOT, "build", "abicompat", sha[:12])
    src = os.path.join(top, "src")
    os.makedirs(top, exist_ok=True)
    with open(os.path.join(top, "lock"), "w") as lk:
        fcntl.flock(lk, fcntl.LOCK_EX)
        stamp = os.path.join(top, "built")
        if os.path.exists(stamp):
            return src
        if not os.path.isdir(os.path.join(src, ".git")) and \
                not os.path.isfile(os.path.join(src, ".git")):
            git("worktree", "prune", check=False)
            git("worktree", "add", "--detach", "-f", src, sha)
        log = os.path.join(top, "make.log")
        print("abicompat: building the base %s into %s (log: %s)"
              % (sha[:12], src, log), flush=True)
        jobs = str(max(1, (os.cpu_count() or 2)))
        # THE BASE'S BUILD IS ITS OWN, so none of THIS run's OS88_* reaches
        # it. Under a frozen soak the runner sets $OS88_BUILD (and the tree's
        # siblings) for every row, and the base's Makefile calls os88sym
        # without naming a build of its own: it then checked the base's map
        # against the frozen tree's kernel.bin and refused, so the base
        # "did not build" on every soak and on no bare run
        env = {k: v for k, v in os.environ.items()
               if not k.startswith("OS88_")}
        with open(log, "w") as lf:
            # ...and its ARTEFACTS, not its gates: `all` ends in checkdocs
            # and test-fast, which are the BASE's opinions of THIS box -
            # its t_w16cens answered a capability this box gained after the
            # release with exit 77, which that runner counted as a failure.
            # Whether the base was a good tree was settled when it shipped
            r = subprocess.run(["make", "-C", src, "BUILD=build", "-j" + jobs,
                                "-o", "test-fast", "-o", "checkdocs"],
                               stdout=lf, stderr=subprocess.STDOUT, env=env)
        if r.returncode:
            raise SystemExit("abicompat: the base did not build - see %s"
                             % log)
        # the base's own MartyPC would be minutes of cargo for an emulator
        # that is the same program: lend it this tree's
        mine = os.path.join(ROOT, "build", "martypc")
        theirs = os.path.join(src, "build", "martypc")
        if os.path.isdir(mine) and not os.path.exists(theirs):
            os.symlink(mine, theirs)
        open(stamp, "w").write(sha + "\n")
    return src


# =============================================================================
# reading an SDK
# =============================================================================

_EQU = re.compile(r'^\s*([A-Za-z_][\w.]*)\s+equ\s+([^;\n]+)', re.M)
_DEF = re.compile(r'^%define\s+(\w+)\s+([^;\n]+)', re.M)
_CELL = re.compile(r'^%define\s+(OSAPI_\w+)\s+KERNEL_SEG:(0x[0-9A-Fa-f]+)',
                   re.M)


def read(tree, rel):
    with open(os.path.join(tree, rel), encoding="latin1") as f:
        return f.read()


def constants(text):
    d = {}
    for rx in (_EQU, _DEF):
        for m in rx.finditer(text):
            d[m.group(1)] = re.sub(r"\s+", " ", m.group(2).strip())
    return d


def cells(text):
    return {m.group(1): int(m.group(2), 16) for m in _CELL.finditer(text)}


def allowlist():
    """tests/abicompat.txt: `NAME  reason` acknowledges a changed constant;
    `note: text` records a prose-only change for the report."""
    acks, notes = {}, []
    if not os.path.exists(ALLOW):
        return acks, notes
    for line in open(ALLOW):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("note:"):
            notes.append(line[5:].strip())
            continue
        name, _, why = line.partition(" ")
        if not why.strip():
            raise SystemExit("abicompat: %s: %r has no reason" % (ALLOW, name))
        acks[name] = why.strip()
    return acks, notes


# =============================================================================
# the static legs
# =============================================================================

def leg_a(base, red_cell=False):
    a = cells(read(base, "apps/os88api.inc"))
    b = cells(read(ROOT, "apps/os88api.inc"))
    if red_cell:
        k = sorted(b)[0]
        b[k] += 1
        print("A    red arm: pretending %s moved" % k)
    if not a:
        say("A", "FAIL", "no OSAPI cell read out of the release's SDK - the "
            "pattern no longer matches and the leg would test nothing")
        return
    bad = 0
    byoff = {}
    for k, v in b.items():
        byoff.setdefault(v, []).append(k)
    for k, v in sorted(a.items(), key=lambda x: x[1]):
        if k not in b:
            if any(o not in a for o in byoff.get(v, ())):
                say("A", "FAIL", "%s at %04Xh is gone and its offset now "
                    "names %s" % (k, v, byoff[v]))
            else:
                say("A", "FAIL", "%s at %04Xh is gone from the SDK" % (k, v))
            bad += 1
        elif b[k] != v:
            say("A", "FAIL", "%s moved %04Xh -> %04Xh: every release package "
                "calling it far-calls the wrong cell" % (k, v, b[k]))
            bad += 1
    new = sorted((v, k) for k, v in b.items() if k not in a)
    if not bad:
        say("A", "ok", "%d release cells at the same offsets; %d appended "
            "since (%s)" % (len(a), len(new),
                            ", ".join("%s@%04Xh" % (k, v) for v, k in new)
                            or "none"))


def lock_of(tree):
    return json.loads(read(tree, "tools/abi.lock.json"))


def leg_b(base):
    """Returns {guard: (from, to)} for every guard bumped since the base."""
    lb, lt = lock_of(base), lock_of(ROOT)
    bumps = {}
    for guard in ("PKG_FMT", "DRV_VER"):
        fp, ver = lb[guard]["fingerprint"], lb[guard]["version"]
        hist = [h for h in lt["history"] if h["guard"] == guard]
        at = [i for i, h in enumerate(hist) if h["fingerprint"] == fp]
        if not at and (lt[guard]["fingerprint"] != fp):
            say("B", "FAIL", "%s: the release's lock state (v%d, %s...) is "
                "not in this tree's history - the chain of decisions since "
                "the release is broken or was rewritten"
                % (guard, ver, fp[:12]))
            continue
        since = hist[at[-1] + 1:] if at else []
        now = lt[guard]["version"]
        if now != ver:
            bumps[guard] = (ver, now)
            why = [h["reason"] for h in since if h["from"] != h["to"]]
            say("B", "DECLARED", "%s %d -> %d since the release, %d decision"
                "(s) on the way: %s" % (guard, ver, now, len(since),
                                        " | ".join(why)[:400]))
        else:
            say("B", "ok", "%s stays %d; %d recorded decision(s) since the "
                "release, every one a no-bump" % (guard, ver, len(since)))
    return bumps


def leg_c(base, bumps):
    acks, _ = allowlist()
    bad = 0
    for rel in ("apps/os88api.inc", "drivers/os88drv.inc"):
        a = constants(read(base, rel))
        b = constants(read(ROOT, rel))
        for k in sorted(a):
            if k in ("PKG_FMT", "DRV_VER") and k in bumps:
                continue
            if k in b and b[k] == a[k]:
                continue
            what = ("is gone" if k not in b
                    else "changed %s -> %s" % (a[k], b[k]))
            if k in acks:
                say("C", "DECLARED", "%s: %s %s - %s" % (rel, k, what,
                                                          acks[k]))
            else:
                say("C", "FAIL", "%s: %s %s, and neither a bumped guard nor "
                    "tests/abicompat.txt says why. Bump the guard if a "
                    "release package can no longer run (tools/abilock.py), "
                    "or acknowledge it with a reason" % (rel, k, what))
                bad += 1
    if not bad:
        say("C", "ok", "every release SDK constant keeps its value or is "
            "accounted for")


def shipped(base):
    """What the release built: packages, drivers and CZ files under build/."""
    bdir = os.path.join(base, "build")
    out = {"pkg": [], "drv": [], "cz": []}
    for f in sorted(os.listdir(bdir)):
        p = os.path.join(bdir, f)
        if f.endswith(".o88"):
            out["pkg"].append(p)
        elif f.endswith(".drv"):
            out["drv"].append(p)
    for sub in ("", "faces", "zdata-lz4", "zdata-lz4/MIDI"):
        d = os.path.join(bdir, sub)
        if not os.path.isdir(d):
            continue
        for f in sorted(os.listdir(d)):
            p = os.path.join(d, f)
            if os.path.isfile(p) and not p.endswith((".o88", ".drv",
                                                      ".img")):
                with open(p, "rb") as fh:
                    if fh.read(2) == b"CZ":
                        out["cz"].append(p)
    return out


def version_of(tree, rel, name):
    m = re.search(r"^%s\s+equ\s+(\d+)" % name, read(tree, rel), re.M)
    return int(m.group(1))


def leg_d(base, files, bumps, theirs):
    """The header read off the IMAGE the release's own tools unwrap, because
    a packed driver is a `CZ` file and its header is inside the stream."""
    pf = version_of(ROOT, "apps/os88api.inc", "PKG_FMT")
    dv = version_of(ROOT, "drivers/os88drv.inc", "DRV_VER")
    for kind, want, guard in (("pkg", pf, "PKG_FMT"), ("drv", dv, "DRV_VER")):
        heads = [p for p in files[kind] if isinstance(theirs.get(p), list)
                 and theirs[p][1] is not None]
        off = [p for p in heads if theirs[p][1] != want]
        if not heads:
            say("D", "FAIL", "no %s header read - the leg would test nothing"
                % kind)
            continue
        if not off:
            say("D", "ok", "%d release %ss carry %s %d, which this kernel "
                "takes" % (len(heads), kind, guard, want))
        elif guard in bumps:
            say("D", "DECLARED", "%d of %d release %ss carry %s %d and this "
                "kernel refuses them at the version byte (bumped %d -> %d): "
                "%s" % (len(off), len(heads), kind, guard, bumps[guard][0],
                        bumps[guard][0], want,
                        " ".join(os.path.basename(p) for p in off)))
        else:
            say("D", "FAIL", "%d release %ss carry another %s with no bump "
                "recorded: %s" % (len(off), kind, guard,
                                  " ".join(os.path.basename(p) for p in off)))


_UNWRAP = r"""
import hashlib, json, sys
sys.path.insert(0, sys.argv[1])
import os88pkg, os88drv, os88lz
out = {}
for kind, path in json.loads(sys.stdin.read()):
    b = open(path, "rb").read()
    try:
        u = {"pkg": os88pkg.image_unwrap, "drv": os88drv.image_unwrap,
             "cz": os88lz.cz_unwrap}[kind](b)
        u = bytes(u)
        out[path] = [hashlib.sha256(u).hexdigest(),
                     u[2] if u[:2] == b"O8" else None]
    except BaseException as e:
        out[path] = "ERROR %s: %s" % (type(e).__name__, e)
print(json.dumps(out))
"""


def unwrap_all(tools, items):
    r = subprocess.run([sys.executable, "-I", "-c", _UNWRAP, tools],
                       input=json.dumps(items), capture_output=True,
                       text=True)
    if r.returncode:
        raise SystemExit("abicompat: unwrapping under %s failed:\n%s"
                         % (tools, r.stderr))
    return json.loads(r.stdout)


def leg_e(base, files, theirs, mine):
    items = [(k, p) for k in ("pkg", "drv", "cz") for p in files[k]]
    bad = 0
    ok = [p for _, p in items if isinstance(theirs[p], list)]
    for p in ok:
        if mine[p] != theirs[p]:
            say("E", "FAIL", "%s: this tree's decoder gives %s where the "
                "release's gives %s" % (os.path.relpath(p, base),
                                        str(mine[p])[:60],
                                        theirs[p][0][:16]))
            bad += 1
    n = len(ok)
    if not n:
        say("E", "FAIL", "no stream decoded under the release's own tools - "
            "the leg would test nothing")
    elif not bad:
        say("E", "ok", "%d release streams (%d packages, %d drivers, %d CZ "
            "files) expand identically here" % (n, len(files["pkg"]),
                                                 len(files["drv"]),
                                                 len(files["cz"])))


# =============================================================================
# leg F - the machine
# =============================================================================

def packages_on(img, dirs):
    """Every .O88 in `dirs` of a FAT12 image ('' = the root), as
    'DIR/NAME.O88' or 'NAME.O88'."""
    sys.path.insert(0, TOOLS)
    import os88fat
    v = os88fat.Fat12(img)
    out = []

    def dirents(raw):
        for i in range(0, len(raw), 32):
            e = raw[i:i + 32]
            if len(e) < 32 or e[0] == 0:
                break
            if e[0] == 0xE5 or e[11] & 0x08:
                continue
            yield e

    for _, _, e in v.entries():
        name = v.pretty(e[:11])
        if "" in dirs and not (e[11] & 0x18) and name.endswith(".O88"):
            out.append(name)
            continue
        if not (e[11] & 0x10) or name not in dirs:
            continue
        first = struct.unpack_from("<H", e, 26)[0]
        raw = b"".join(bytes(v.img[v.cluster_off(c):v.cluster_off(c)
                                   + v.spc * v.bps])
                       for c in v.chain(first))
        for s in dirents(raw):
            sn = v.pretty(s[:11])
            if not (s[11] & 0x10) and sn.endswith(".O88"):
                out.append("%s/%s" % (name, sn))
    return out


def launch_leg(a):
    """Run in a SUBPROCESS with the right tree's tools first on the path, so
    the kernel's symbols resolve against the sources that built it."""
    sys.path.insert(0, a.tools)
    import os88ui                           # noqa: E402  (the tree's own)
    want = json.loads(a.pkgs)
    res = {}

    def fresh():
        return os88ui.boot(a.sys, apps=a.apps, quick=True, verbose=False)

    ctx = fresh()
    ui = ctx.__enter__()
    try:
        cur = None
        for spec in want:
            folder, _, name = spec.rpartition("/")
            try:
                if cur != folder:
                    ui.path("B:/" + folder)
                    cur = folder
                before = {w.i for w in ui.windows()}
                try:
                    w = ui.open(name, expect="window")
                    res[spec] = "window"
                except os88ui.UIError:
                    st = ui._byte("ld_status")
                    res[spec] = "status %d" % st
                    w = None
                for o in ui.windows():
                    if o.i not in before:
                        ui.close(o)
            except Exception as e:          # a machine we cannot steer on
                res.setdefault(spec, "stuck: %s" % str(e).splitlines()[0])
                ctx.__exit__(None, None, None)
                ctx = fresh()
                ui = ctx.__enter__()
                cur = None
            print("F    %-24s %s" % (spec, res[spec]), flush=True)
    finally:
        ctx.__exit__(None, None, None)
    with open(a.out, "w") as f:
        json.dump(res, f)


def run_leg(tools, sysimg, appsimg, pkgs, tag):
    out = tempfile.mktemp(prefix="abicompat-%s-" % tag, suffix=".json")
    print("F    --- %s: %s + %s" % (tag, sysimg, appsimg), flush=True)
    # THE RELEASE'S LEG READS THE RELEASE'S KERNEL, so it gets none of this
    # run's OS88_* - a frozen soak's $OS88_BUILD names the TIP's kernel, and
    # the base's os88sym then refused its own map against it and every
    # release leg died before a launch - and $OS88_BUILD is the base's build
    env = None
    if os.path.abspath(tools) != os.path.abspath(TOOLS):
        env = {k: v for k, v in os.environ.items()
               if not k.startswith("OS88_")}
        env["OS88_BUILD"] = os.path.join(os.path.dirname(
            os.path.abspath(tools)), "build")
    r = subprocess.run([sys.executable, os.path.abspath(__file__), "--leg",
                        "--tools", tools, "--sys", sysimg, "--apps", appsimg,
                        "--pkgs", json.dumps(pkgs), "--out", out], env=env)
    if r.returncode or not os.path.exists(out):
        say("F", "FAIL", "the %s leg did not finish (exit %d)"
            % (tag, r.returncode))
        return None
    res = json.load(open(out))
    os.unlink(out)
    return res


def leg_f(base, only, red_version):
    bimg = os.path.join(base, "build")
    sys.path.insert(0, TOOLS)
    import os88build
    import os88fat
    tip_sys = os88build.at("build/" + SYS_IMG)
    seen, opened, other, bad = set(), 0, [], 0
    for img, dirs in LAUNCH_DISKS:
        apps = os.path.join(bimg, img)
        if not os.path.exists(apps):
            say("F", "NOTE", "the release has no %s" % img)
            continue
        pkgs = [p for p in packages_on(apps, dirs)
                if p.rpartition("/")[2] not in seen]
        if only:
            pkgs = [p for p in pkgs if any(k.upper() in p for k in only)]
        if not pkgs:
            continue
        seen.update(p.rpartition("/")[2] for p in pkgs)
        scratch, tip_apps = None, apps
        if red_version and not bad and not opened:
            scratch = tempfile.mktemp(prefix="abicompat-red-", suffix=".img")
            shutil.copy(apps, scratch)
            _flip_version(os88fat.Fat12(scratch), pkgs[0])
            tip_apps = scratch
            print("F    red arm: %s's version byte flipped on the tip leg's "
                  "disk" % pkgs[0])
        try:
            ctl = run_leg(os.path.join(base, "tools"),
                          os.path.join(bimg, SYS_IMG), apps, pkgs,
                          "release " + img)
            got = run_leg(TOOLS, tip_sys, tip_apps, pkgs, "tip " + img)
        finally:
            if scratch:
                os.unlink(scratch)
        if ctl is None or got is None:
            bad += 1
            continue
        for p in pkgs:
            if ctl.get(p) == "window":
                opened += 1
                if got.get(p) != "window":
                    say("F", "FAIL", "%s %s opens on the release's kernel "
                        "and not on this one: %s" % (img, p, got.get(p)))
                    bad += 1
            else:
                other.append("%s(%s)" % (p, ctl.get(p)))
    if not seen:
        say("F", "FAIL", "no package found on the release's disks")
    elif not opened:
        say("F", "FAIL", "nothing opened even on the release's own kernel - "
            "the control proves nothing")
    elif not bad:
        say("F", "ok", "%d of %d release packages open here as they do on "
            "the release's kernel%s" % (opened, len(seen),
                                        ("; refused on both: "
                                         + " ".join(other)) if other else ""))


def _flip_version(v, spec):
    """Flip PKG_FMT in the header of `spec` ('DIR/NAME' or 'NAME') on an
    image, in place: the red arm's broken package."""
    folder, _, name = spec.rpartition("/")

    def scan(off, n):
        for i in range(off, off + n, 32):
            if v.pretty(bytes(v.img[i:i + 11])) == name:
                fc = struct.unpack_from("<H", v.img, i + 26)[0]
                v.img[v.cluster_off(fc) + 2] ^= 0x40
                return True
        return False

    if not folder:
        scan(v.root_off, v.nroot * 32)
    else:
        for _, _, e in v.entries():
            if v.pretty(e[:11]) == folder:
                for c in v.chain(struct.unpack_from("<H", e, 26)[0]):
                    scan(v.cluster_off(c), v.spc * v.bps)
    v.save()


# =============================================================================

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--base", help="the commit to compare against "
                    "(default: the latest `Release YYYYMMDD` commit)")
    ap.add_argument("--static", action="store_true",
                    help="host legs only; no emulator")
    ap.add_argument("-k", action="append", default=[],
                    help="launch only packages whose path contains this")
    ap.add_argument("--red-version", action="store_true")
    ap.add_argument("--red-cell", action="store_true")
    # the launch leg's own subprocess interface
    ap.add_argument("--leg", action="store_true", help=argparse.SUPPRESS)
    ap.add_argument("--tools", help=argparse.SUPPRESS)
    ap.add_argument("--sys", help=argparse.SUPPRESS)
    ap.add_argument("--apps", help=argparse.SUPPRESS)
    ap.add_argument("--pkgs", help=argparse.SUPPRESS)
    ap.add_argument("--out", help=argparse.SUPPRESS)
    a = ap.parse_args()
    if a.leg:
        return launch_leg(a)

    if a.base:
        sha, subj = git("rev-parse", a.base + "^{commit}"), "(--base)"
    else:
        sha, subj = find_release()
    print("abicompat: base %s %s; tip %s" % (sha[:12], subj,
                                             git("rev-parse", "--short=12",
                                                 "HEAD")), flush=True)
    base = build_base(sha)

    leg_a(base, a.red_cell)
    bumps = leg_b(base)
    leg_c(base, bumps)
    files = shipped(base)
    items = [(k, p) for k in ("pkg", "drv", "cz") for p in files[k]]
    theirs = unwrap_all(os.path.join(base, "tools"), items)
    mine = unwrap_all(TOOLS, items)
    leg_d(base, files, bumps, theirs)
    leg_e(base, files, theirs, mine)
    _, notes = allowlist()
    for n in notes:
        say("note", "NOTE", n)
    if not a.static:
        leg_f(base, a.k, a.red_version)

    fails = [r for r in RESULTS if r[1] == "FAIL"]
    print("\nabicompat: %d FAIL, %d DECLARED, %d ok"
          % (len(fails), sum(r[1] == "DECLARED" for r in RESULTS),
             sum(r[1] == "ok" for r in RESULTS)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main() or 0)
