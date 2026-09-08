#!/usr/bin/env python3
"""os88bisect - classify a failing row, and bisect it, WITHOUT the three
mistakes that made the last bisect void.

    python3 tools/os88bisect.py classify dispsize          # the protocol
    python3 tools/os88bisect.py sample dispsize --at HEAD --at f8af49e
    python3 tools/os88bisect.py search dispsize --good f8af49e --bad HEAD

WHY THIS EXISTS.  `docs/plans/HANDOFF-SOAK-FINDINGS.md` E1 is a bisect that was
published-adjacent and wrong.  It named a commit whose entire diff to shipped
code is four comment lines.  Three errors stacked, and every one of them is
cheap to repeat by hand and impossible to repeat through this file:

  1. **N=1 WAS READ AS A RATE.**  Six points, one run each, ok/ok/ok/ok/fail/
     fail - which reads as a clean bisect and was a coin landing in an order.
     The row turned out to fail 10 times in 15 EVERYWHERE.  So every point
     here is sampled `-n` times (default 3) and the answer is a RATE; a point
     that is neither 0/N nor N/N is reported as INTERMITTENT and never as a
     side.

  2. **A ROW'S EXIT CODE WAS READ AS ONE VERDICT.**  `dispsize` has six
     independent legs; the run that showed 0 differing pixels on leg C still
     exited 1 on leg E, so it was filed as a failure and the flake stayed
     invisible for four more runs.  This parses the row's own `FAIL:` lines
     into LEGS and rates each separately, so a row with legs can be sampled at
     all.

  3. **THE POINTS DID NOT SHARE A BASE.**  Four of the six forked from a
     different commit and carried the pre-pass kernel, so they were never
     comparable with the two that did not.  `git log --graph --boundary` says
     so in one screen and was not run until after the conclusion.  Every
     command here checks ancestry FIRST and refuses to compare points that do
     not share one.

THE PROTOCOL IS THE DEFAULT, not a thing to remember.  That file's own steps
are: re-run alone on HEAD, then at the base, and bisect ONLY where those two
disagree.  `classify` is those two steps; it ends by saying whether a bisect is
even the right question, and `search` refuses to start until it has been asked.

WHAT IT SHARES BETWEEN WORKTREES, AND WHAT IT MUST NOT.  Each point is a
detached `git worktree` with its own `build/`, so two points never write one
file.  `build/martypc` and `build/cc` are symlinked in - both are pinned
upstream artefacts, identical by construction, and rebuilding SmallerC per
point would cost more than the bisect.  **Nothing else may be shared**: a
shared writable DISK is what contaminated pass 2's first bisect
(`docs/plans/completed/HANDOFF-KERNEL-SIZE-P3.md` 3).
"""
import argparse
import concurrent.futures
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORK = os.path.join(ROOT, "build", "bisect")

GREEN, RED, YELLOW, DIM, OFF = "\033[32m", "\033[31m", "\033[33m", "\033[2m", "\033[0m"
if not sys.stdout.isatty() or os.environ.get("NO_COLOR"):
    GREEN = RED = YELLOW = DIM = OFF = ""


def git(*a, **kw):
    """Run git in the repo. `ok=True` means a non-zero exit is an answer."""
    ok = kw.pop("ok", False)
    r = subprocess.run(["git"] + list(a), cwd=kw.pop("cwd", ROOT),
                       capture_output=True, text=True, **kw)
    if r.returncode and not ok:
        raise SystemExit("os88bisect: git %s failed:\n%s"
                         % (" ".join(a), r.stderr.strip()))
    return r.stdout.strip()


def short(ref):
    return git("rev-parse", "--short", ref)


# --------------------------------------------------------------------------
# 3. ANCESTRY - checked before anything is compared.
# --------------------------------------------------------------------------

def shallow():
    return git("rev-parse", "--is-shallow-repository") == "true"


def ancestry(refs):
    """Do these points share a base? Returns (ok, message).

    THE THIRD TRAP, and the cheapest of the three to check. Four of E1's six
    points forked from a different commit and carried a different kernel; they
    were never comparable with the other two, and nothing in the run said so.

    A merge-base that is one of the refs is the ordinary case - a straight
    line. What this refuses is a set whose merge-base is BEHIND all of them,
    which means they are siblings on different branches and the thing being
    measured is the branch rather than the commit.
    """
    if shallow():
        return False, ("this is a SHALLOW clone, so git answers ancestry "
                       "questions confidently and WRONGLY (CLAUDE.md's rule 1)."
                       " `git fetch --unshallow` first.")
    sha = [git("rev-parse", r) for r in refs]
    if len(sha) == 1:
        return True, "one point, %s" % short(sha[0])
    # THE POINTS MUST FORM A CHAIN. Comparable means one line of history: every
    # point an ancestor of the newest. Siblings on different branches carry
    # different trees, so the difference measured between them is the BRANCH
    # and not the commit - which is exactly what four of E1's six points were.
    for cand in sha:
        if all(_is_ancestor(o, cand) for o in sha):
            newest = cand
            break
    else:
        base = git("merge-base", *sha)
        return False, ("these points DO NOT SHARE A LINE - no one of them "
                       "contains all the others, so they are siblings on "
                       "different branches and carry different trees. Their "
                       "merge-base is %s. That is E1's third trap, and it made "
                       "four of six points incomparable.\n            "
                       "`git log --graph --boundary %s` shows it in one screen."
                       % (short(base), " ".join(refs)))
    return True, "one line, newest %s" % short(newest)


def _is_ancestor(a, b):
    """Is `a` an ancestor of `b`, or the same commit?"""
    if a == b:
        return True
    return subprocess.run(["git", "merge-base", "--is-ancestor", a, b],
                          cwd=ROOT, capture_output=True).returncode == 0


# --------------------------------------------------------------------------
# The worktree per point.
# --------------------------------------------------------------------------

SHARED = ("cc", "martypc")


# What a row needs from a worktree, whatever else that commit's `all` does.
# Every path here has been stable for the life of the repository, which is
# what makes it usable as a success test against an OLD tree.
NEEDED = ("build/os8088-360.img", "build/apps360.img", "build/kernel.bin")


def worktree(ref, quiet=True):
    """A detached worktree at `ref`, built, with the instruments shared."""
    sha = git("rev-parse", ref)
    d = os.path.join(WORK, short(sha))
    if not os.path.isdir(os.path.join(d, ".git")) and not os.path.exists(
            os.path.join(d, "Makefile")):
        os.makedirs(WORK, exist_ok=True)
        shutil.rmtree(d, ignore_errors=True)
        git("worktree", "add", "--detach", "-f", d, sha)
    b = os.path.join(d, "build")
    os.makedirs(b, exist_ok=True)
    for name in SHARED:
        src, dst = os.path.join(ROOT, "build", name), os.path.join(b, name)
        if os.path.exists(src) and not os.path.exists(dst):
            try:
                os.symlink(src, dst)
            except OSError:
                pass
    # **`-k`, AND THE IMAGES ARE THE TEST OF SUCCESS - not make's exit code.**
    # `all` depends on `test-fast`, so building an old commit runs THAT
    # COMMIT'S gates, and a gate that has since been relaxed fails there. It
    # is not the question being asked: a bisect wants the artefacts so it can
    # run ONE row against them, and whether the base's doc or font checks
    # pass says nothing about that row.
    #
    # Measured: classifying `fcpsmall` reported ERROR at the base over
    # `os88face: warning - style regular has a 1-pixel vertical stem` promoted
    # to an error - a tree whose images had built perfectly. An ERROR verdict
    # there is worse than useless, because step 2 exists precisely to say
    # whether the row failed before the branch touched it.
    #
    # So: keep going past a failing gate, then require the artefacts. A build
    # that really is broken produces no images and still raises, with make's
    # own output.
    r = subprocess.run(["make", "-j2", "-k"], cwd=d,
                       capture_output=True, text=True)
    made = [f for f in NEEDED if os.path.exists(os.path.join(d, f))]
    if len(made) != len(NEEDED):
        raise RuntimeError(
            "os88bisect: `make` did not produce %s in %s:\n%s"
            % (", ".join(f for f in NEEDED if f not in made),
               os.path.relpath(d, ROOT), (r.stdout + r.stderr)[-1500:]))
    if r.returncode and not quiet:
        print("  (that commit's own gates failed; the images built, which is "
              "what a row needs)")
    return d


def drop(ref):
    d = os.path.join(WORK, short(ref))
    git("worktree", "remove", "--force", d, ok=True)
    shutil.rmtree(d, ignore_errors=True)


# --------------------------------------------------------------------------
# 2. LEGS - a row's exit code is not one verdict.
# --------------------------------------------------------------------------

# The runner's own prefix on a row's output, and nothing more: the row's OWN
# indentation has to survive, because one of the four spellings below is a
# bare header followed by an INDENTED list.
_PFX = re.compile(r"^\s*\|\s?")
_LEG = re.compile(r"^([A-Za-z0-9_.\- ]{1,32}?):\s")
# `FAIL(ED)?`, NOT `FAILED?` - the second is FAILE + optional D,
# which matches neither spelling and made this whole arm dead.
_HDR = re.compile(r"^\S*:?\s*FAIL(ED)?\s*$")


def legs(out):
    """The distinct assertions that FAILED in one run.

    FOUR SPELLINGS, counted rather than guessed at. The first version of this
    handled one and a half of them and returned NOTHING for `gfxlk` or
    `msegnomem` - which is the same silence E1 suffered from, one layer up:

      * `FAIL: <what>`            - tests/unit/harness.py's `done()`, 72 files.
      * `<row>: FAIL: <leg>: ...` - a row keeping its own list. `dispsize` is
        this one and its legs are single letters, which is the case E1 needed
        and did not have.
      * `  [FAIL] <name> <note>`  - a row with its own `check()`, e.g. `gfxlk`,
        which also prints a summary `<row>: FAILED: a, b, c`.
      * `<row>: FAIL` then an INDENTED list - 22 files, `msegnomem` among
        them. This is why the row's own indentation must survive the strip.

    The summary line is used ONLY when nothing else matched, and split on ", "
    only when it looks like a list - a single message containing a comma is
    the ordinary case and must not become two legs.

    An empty set means the run passed, OR that it died before it could report
    - a traceback has no legs. The caller distinguishes those by the exit code;
    what this must never do is return nothing for a run that DID say why.
    """
    for c in ("\033[31m", "\033[32m", "\033[33m", "\033[0m", "\033[2m"):
        out = out.replace(c, "")
    lines = [_PFX.sub("", ln) for ln in out.splitlines()]
    found, summary, in_block = [], [], False
    for ln in lines:
        body = ln.strip()
        # form 4: a bare `<row>: FAIL` header, then indented detail
        if _HDR.match(body) and "FAILED:" not in body:
            in_block = True
            continue
        if in_block:
            if ln[:1].isspace() and body:
                found.append(body[:60])
                continue
            if body:
                in_block = False
        if body.startswith("[FAIL]"):
            found.append(body[6:].strip()[:60])
            continue
        i = body.find("FAIL: ")
        if i >= 0:
            rest = body[i + 6:].strip()
            m = _LEG.match(rest)
            found.append(m.group(1).strip() if m else rest[:60])
            continue
        j = body.find("FAILED: ")
        if j >= 0:
            summary.append(body[j + 8:].strip())
    if not found:
        for t in summary:
            parts = [x.strip() for x in t.split(", ")] if ", " in t else [t]
            found += [x[:60] for x in parts if x]
    return sorted(set(found))


def run_once(d, row, timeout=3600):
    """One run of `row` inside worktree `d`. -> (ok, legs, seconds, tail)."""
    import time
    t0 = time.time()
    p = subprocess.run(["python3", os.path.join("tools", "os88test.py"),
                        "soak", "-k", row, "--marty-jobs", "1"],
                       cwd=d, capture_output=True, text=True, timeout=timeout)
    out = p.stdout + p.stderr
    ok = p.returncode == 0 and re.search(r"^\s*ok\s+%s\b" % re.escape(row),
                                         out, re.M) is not None
    skipped = re.search(r"^SKIP\s+%s\b" % re.escape(row), out, re.M) is not None
    return (ok, ["<SKIPPED>"] if skipped else (legs(out) if not ok else []),
            time.time() - t0, out[-1500:])


# --------------------------------------------------------------------------
# 1. RATES - never a side from one run.
# --------------------------------------------------------------------------

class Point(object):
    """One commit, sampled N times, as a rate per leg."""

    def __init__(self, ref):
        self.ref, self.sha = ref, short(ref)
        self.runs, self.fails, self.leg = 0, 0, {}
        self.error = None

    def add(self, ok, ls):
        self.runs += 1
        if not ok:
            self.fails += 1
        for x in ls:
            self.leg[x] = self.leg.get(x, 0) + 1

    @property
    def verdict(self):
        if self.error:
            return "ERROR"
        if self.runs == 0:
            return "not run"
        if self.fails == 0:
            return "GOOD"
        if self.fails == self.runs:
            return "BAD"
        return "INTERMITTENT"

    def line(self):
        v = self.verdict
        c = {"GOOD": GREEN, "BAD": RED, "INTERMITTENT": YELLOW}.get(v, DIM)
        s = "  %-10s %s%-12s%s %d/%d failed" % (self.sha, c, v, OFF,
                                                self.fails, self.runs)
        if self.leg:
            s += "   legs: " + ", ".join(
                "%s %d/%d" % (k, n, self.runs)
                for k, n in sorted(self.leg.items(), key=lambda kv: -kv[1]))
        return s


def sample_points(row, refs, n, jobs, verbose=False):
    """Build every point, then run every (point, sample) in one pool."""
    pts = [Point(r) for r in refs]
    print("os88bisect: %d point(s) x %d run(s) of %s, %d at a time"
          % (len(pts), n, row, jobs))

    dirs = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, jobs)) as ex:
        futs = {ex.submit(worktree, p.ref): p for p in pts}
        for f in concurrent.futures.as_completed(futs):
            p = futs[f]
            try:
                dirs[p.sha] = f.result()
                print("  built %s" % p.sha)
            except Exception as e:                       # noqa: BLE001
                p.error = str(e)[-300:]
                print("%s  BUILD FAILED %s%s: %s" % (RED, p.sha, OFF, p.error))

    work = [(p, dirs[p.sha]) for p in pts if p.sha in dirs for _ in range(n)]
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, jobs)) as ex:
        futs = {ex.submit(run_once, d, row): p for p, d in work}
        for f in concurrent.futures.as_completed(futs):
            p = futs[f]
            try:
                ok, ls, secs, tail = f.result()
            except Exception as e:                       # noqa: BLE001
                ok, ls, secs, tail = False, ["<ERROR: %s>" % str(e)[:60]], 0, ""
            p.add(ok, ls)
            if verbose and not ok:
                print(tail)
    print()
    for p in pts:
        print(p.line())
    return pts


# --------------------------------------------------------------------------
# The verbs.
# --------------------------------------------------------------------------

def cmd_classify(a):
    """Steps 1 and 2 of the protocol, and the answer to `is a bisect right?`"""
    base = a.base or _default_base()
    ok, msg = ancestry(["HEAD", base])
    print("os88bisect: ancestry - %s" % msg)
    if not ok:
        return 1

    # STEP 1 FIRST, AND ON ITS OWN. The protocol's own economy is that step 1
    # is one row of emulator time and decides whether step 2 is even the right
    # question - two of pass 2's fifteen were resolved by it alone. Sampling
    # both points up front would build and run the base for every row that
    # step 1 was going to settle, which is most of them.
    print("\n--- step 1: %s at HEAD, alone ---" % a.row)
    head = sample_points(a.row, ["HEAD"], a.n, a.jobs, a.verbose)[0]
    print()
    if head.verdict == "GOOD":
        print("%sos88bisect: %s passes at HEAD (%d/%d).%s The failure you saw "
              "was CONTENTION or an intermittent - step 1 has answered it, "
              "there is nothing to bisect, and the base was never built."
              % (GREEN, a.row, head.runs - head.fails, head.runs, OFF))
        return 0
    if head.verdict == "INTERMITTENT":
        print("%sos88bisect: INTERMITTENT AT HEAD - %d/%d.%s A rate is not a "
              "side. Bisecting one is a coin landing in an order, which is "
              "exactly how E1 named a commit whose diff is four comment "
              "lines. Fix the row's own flake, or raise -n until HEAD is "
              "0/N or N/N." % (YELLOW, head.fails, head.runs, OFF))
        return 1

    print("\n--- step 2: the same row at %s ---" % short(base))
    old = sample_points(a.row, [base], a.n, a.jobs, a.verbose)[0]
    print()
    if old.verdict == "INTERMITTENT":
        print("%sos88bisect: INTERMITTENT AT THE BASE - %d/%d.%s HEAD is a "
              "clean %s, so whatever this row does it was ALREADY doing "
              "before the branch. Not a regression; raise -n if you want the "
              "rate itself." % (YELLOW, old.fails, old.runs, OFF, head.verdict))
        return 0
    if head.verdict == old.verdict:
        # ...BUT NOT NECESSARILY FOR THE SAME REASON. Two BADs are only
        # "pre-existing" if they are the SAME failure; a row that fails on leg
        # C at the base and leg E at HEAD has one pre-existing defect and one
        # regression, and calling that pre-existing buries the second. This is
        # E1's exit-code-as-one-verdict error in its other form.
        hl, ol = set(head.leg), set(old.leg)
        if bool(hl) != bool(ol):
            # ONE SIDE NEVER GOT TO ASSERT. A run that dies in a traceback has
            # no legs, and "no legs" is not "the same legs" - it is a row that
            # could not run at that point. `msegnomem` is the worked example:
            # it asserts at HEAD and dies on a missing fixture at the base, so
            # calling the two the same failure would file a real assertion as
            # pre-existing on the strength of a crash.
            crashed, asserts = (old, head) if hl else (head, old)
            print("%sos88bisect: both points FAIL, but %s never got as far as "
                  "an assertion.%s\n    %s reports: %s\n    %s reports nothing "
                  "- it died before it could. Read its output (-v) before "
                  "calling this pre-existing: a row that cannot RUN at a point "
                  "has not been measured there."
                  % (YELLOW, crashed.sha, OFF, asserts.sha,
                     ", ".join(sorted(hl | ol)), crashed.sha))
            return 0
        if hl and ol and hl != ol:
            print("%sos88bisect: %s fails at BOTH points - but on DIFFERENT "
                  "legs.%s\n    only at HEAD    : %s\n    only at %s : %s\n"
                  "    both            : %s\n"
                  "That is not one verdict. The legs shared by both are "
                  "pre-existing; anything only at HEAD is a REGRESSION and is "
                  "worth `search`ing on its own."
                  % (YELLOW, a.row, OFF,
                     ", ".join(sorted(hl - ol)) or "-", old.sha,
                     ", ".join(sorted(ol - hl)) or "-",
                     ", ".join(sorted(hl & ol)) or "-"))
            return 0
        print("%sos88bisect: %s fails at HEAD AND at %s, on the same leg(s).%s "
              "It is PRE-EXISTING - not a regression, and there is nothing "
              "between them to bisect. Step 2 has answered it."
              % (GREEN, a.row, old.sha, OFF))
        return 0
    n = len(git("rev-list", "%s..HEAD" % base).splitlines())
    print("%sos88bisect: they DISAGREE - HEAD %s, %s %s.%s A bisect is the "
          "right question now, over %d commit(s):\n"
          "    python3 tools/os88bisect.py search %s --good %s --bad HEAD -n %d"
          % (YELLOW, head.verdict, old.sha, old.verdict, OFF, n, a.row,
             old.sha, a.n))
    return 0


def _default_base():
    """The commit this branch was cut from, best effort."""
    for ref in ("origin/elendilon", "elendilon", "origin/main", "main"):
        b = git("merge-base", "HEAD", ref, ok=True)
        if b:
            return b
    return git("rev-list", "--max-parents=0", "HEAD").split("\n")[0]


def cmd_sample(a):
    ok, msg = ancestry(a.at)
    print("os88bisect: ancestry - %s" % msg)
    if not ok and not a.no_ancestry:
        return 1
    sample_points(a.row, a.at, a.n, a.jobs, a.verbose)
    return 0


def cmd_search(a):
    """The bisect itself - over RATES, and only between two settled sides."""
    ok, msg = ancestry([a.good, a.bad])
    print("os88bisect: ancestry - %s" % msg)
    if not ok:
        return 1
    revs = git("rev-list", "--reverse", "%s..%s" % (a.good, a.bad)).splitlines()
    if not revs:
        print("os88bisect: nothing between %s and %s" % (a.good, a.bad))
        return 1
    print("os88bisect: %d commit(s) between %s and %s; each point sampled %d "
          "times" % (len(revs), short(a.good), short(a.bad), a.n))
    lo, hi = 0, len(revs) - 1
    settled = {}
    while lo <= hi:
        mid = (lo + hi) // 2
        ref = revs[mid]
        p = sample_points(a.row, [ref], a.n, a.jobs, a.verbose)[0]
        settled[short(ref)] = p.verdict
        if p.verdict == "INTERMITTENT":
            print("\n%sos88bisect: %s is INTERMITTENT (%d/%d).%s Stopping. A "
                  "bisect step over a rate picks a side by luck; that is E1's "
                  "first error and it is not recoverable by carrying on."
                  % (YELLOW, p.sha, p.fails, p.runs, OFF))
            return 1
        if p.verdict == "ERROR":
            print("\nos88bisect: %s would not build - excluded, not a side"
                  % p.sha)
            lo = mid + 1
            continue
        if p.verdict == "BAD":
            hi = mid - 1
        else:
            lo = mid + 1
    if lo >= len(revs):
        print("\nos88bisect: every point is GOOD - the change is not in this "
              "range")
        return 1
    first = revs[lo]
    print("\n%sos88bisect: first BAD commit is %s%s" % (RED, short(first), OFF))
    print(git("show", "--stat", "--oneline", "-s", first))
    print("\nDIFF TO SHIPPED CODE - read it before believing this. E1's void "
          "bisect named a commit whose entire diff was four comment lines,\n"
          "and that should have stopped it:")
    print(git("show", "--stat", first).split("\n\n")[-1][:1200])
    return 0


def cmd_clean(a):
    for d in sorted(os.listdir(WORK)) if os.path.isdir(WORK) else []:
        git("worktree", "remove", "--force", os.path.join(WORK, d), ok=True)
    shutil.rmtree(WORK, ignore_errors=True)
    git("worktree", "prune", ok=True)
    print("os88bisect: removed %s" % os.path.relpath(WORK, ROOT))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="verb", required=True)

    def common(p):
        p.add_argument("-n", type=int, default=3,
                       help="runs per point (default 3). ONE IS NEVER ENOUGH - "
                            "E1's bisect was six points at one run each and "
                            "the row failed 10 times in 15 everywhere")
        p.add_argument("-j", "--jobs", type=int,
                       default=max(1, (len(os.sched_getaffinity(0))
                                       if hasattr(os, "sched_getaffinity")
                                       else os.cpu_count() or 2) - 1),
                       help="points/runs at once (default cores-1)")
        p.add_argument("-v", "--verbose", action="store_true")

    c = sub.add_parser("classify", help="the protocol: HEAD, then the base")
    c.add_argument("row")
    c.add_argument("--base", help="default: the merge-base with elendilon/main")
    common(c)

    s = sub.add_parser("sample", help="rate a row at named commits")
    s.add_argument("row")
    s.add_argument("--at", action="append", required=True)
    s.add_argument("--no-ancestry", action="store_true",
                   help="compare points that do not share a base ANYWAY. They "
                        "carry different trees, so the difference you measure "
                        "may be the branch and not the commit")
    common(s)

    b = sub.add_parser("search", help="bisect, over rates")
    b.add_argument("row")
    b.add_argument("--good", required=True)
    b.add_argument("--bad", required=True)
    common(b)

    sub.add_parser("clean", help="remove the worktrees")

    a = ap.parse_args()
    try:
        import signal
        signal.signal(signal.SIGPIPE, signal.SIG_DFL)
    except (AttributeError, ValueError):
        pass
    return {"classify": cmd_classify, "sample": cmd_sample,
            "search": cmd_search, "clean": cmd_clean}[a.verb](a)


if __name__ == "__main__":
    sys.exit(main())
