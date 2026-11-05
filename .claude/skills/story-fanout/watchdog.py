#!/usr/bin/env python3
"""Stall watchdog for a story fan-out (SKILL.md Step 3).

The log Monitor reports what an agent WRITES. A stalled agent writes nothing,
so that Monitor stays silent and the stall looks like progress. This script
watches for the silence instead.

An agent counts as stalled when its log's last status is not terminal
(DONE, FAILED or BLOCKED) and, for --stale minutes, ALL of these hold:

  * no new line in its log;
  * no file in its worktree modified (git status paths, plus .git's index
    and HEAD, which a commit touches);
  * no live process whose working directory is inside the worktree (a make,
    nasm or emulator it started).

A stall is printed once. It is printed again only after another --stale
period with nothing new, so the Monitor gets one line per stall, not one per
poll. A RUNNING line may carry an estimate as `~<N>m`, and that agent's
threshold becomes max(--stale, N + 5) until its next line.

Usage (run it as a Monitor; each stdout line is an event):

  python3 watchdog.py --logs ~/Repos/<feature>-logs \\
      --worktree drv=~/Repos/os8088-pce-drv --worktree kmega=... [--stale 10]

The key in --worktree is the log FILE's stem (drv for drv.log), so a verifier
that logs to its implementer's file (`[verify-drv]` in drv.log) is covered by
the same entry. A log with no --worktree entry is judged on its log alone.
"""
import argparse
import os
import re
import subprocess
import sys
import time

TERMINAL = ("DONE", "FAILED", "BLOCKED")
LINE = re.compile(r"^\[(?P<key>[^\]]+)\]\s+\S+\s+(?:STATUS:\s*)?(?P<st>[A-Z]+)\b:?(?P<rest>.*)$")
EST = re.compile(r"~(\d+)\s*m")


def active_agents(path):
    """[(key, text, estimate_minutes)] for every agent in the log still at work.

    A log can hold several agents (an implementer and then its verifier), so
    each [key] is followed on its own. DONE and FAILED are STICKY: a later
    PROGRESS line from the same key (an ACK, a note) does not reopen it; only
    STARTED or RUNNING does. BLOCKED is terminal until the agent writes
    anything after it (the ACK of an answer means it is working again)."""
    try:
        with open(path, errors="replace") as f:
            lines = f.read().splitlines()
    except OSError:
        return []
    state = {}                        # key -> (state, text, est)
    for ln in lines:
        m = LINE.match(ln.strip())
        if not m:
            continue
        key, st = m["key"], m["st"]
        est = EST.search(m["rest"]) if st == "RUNNING" else None
        est = int(est[1]) if est else 0
        prev = state.get(key, ("active", "", 0))[0]
        if st in ("DONE", "FAILED"):
            new = "done"
        elif st == "BLOCKED":
            new = "blocked"
        elif st in ("STARTED", "RUNNING"):
            new = "active"
        else:                         # PROGRESS and anything else
            new = "done" if prev == "done" else "active"
        if st != "RUNNING" and new == "active" and prev == "active":
            est = state.get(key, ("", "", 0))[2]  # PROGRESS keeps the RUNNING estimate
        state[key] = (new, ln.strip(), est)
    return [(k, t, e) for k, (s, t, e) in state.items() if s == "active"]


def newest_write(tree):
    """Newest mtime among changed files in the worktree and its git state."""
    best = 0.0
    try:
        out = subprocess.run(["git", "-C", tree, "status", "--porcelain", "-z"],
                             capture_output=True, text=True, timeout=30).stdout
        gitdir = subprocess.run(["git", "-C", tree, "rev-parse", "--git-dir"],
                                capture_output=True, text=True, timeout=30).stdout.strip()
    except (OSError, subprocess.TimeoutExpired):
        return best
    paths = [p[3:] for p in out.split("\0") if len(p) > 3]
    if gitdir:
        gd = gitdir if os.path.isabs(gitdir) else os.path.join(tree, gitdir)
        paths += [os.path.join(gd, "index"), os.path.join(gd, "HEAD"),
                  os.path.join(gd, "logs", "HEAD")]
    for p in paths:
        fp = p if os.path.isabs(p) else os.path.join(tree, p)
        try:
            best = max(best, os.stat(fp).st_mtime)
        except OSError:
            pass
    return best


def busy_trees(trees):
    """The subset of worktrees that some live process has as its cwd."""
    cwds = []
    if os.path.isdir("/proc"):
        for pid in os.listdir("/proc"):
            if pid.isdigit():
                try:
                    cwds.append(os.readlink(f"/proc/{pid}/cwd"))
                except OSError:
                    pass
    else:
        try:
            out = subprocess.run(["lsof", "-a", "-d", "cwd", "-Fn"],
                                 capture_output=True, text=True, timeout=60).stdout
            cwds = [ln[1:] for ln in out.splitlines() if ln.startswith("n")]
        except (OSError, subprocess.TimeoutExpired):
            return set(trees)        # cannot tell: never call it a stall
    real = {t: os.path.realpath(t) for t in trees}
    busy = set()
    for c in cwds:
        rc = os.path.realpath(c)
        for t, rt in real.items():
            if rc == rt or rc.startswith(rt + os.sep):
                busy.add(t)
    return busy


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--logs", required=True)
    ap.add_argument("--worktree", action="append", default=[],
                    help="<log stem>=<worktree path>, repeatable")
    ap.add_argument("--stale", type=float, default=10.0, help="minutes")
    ap.add_argument("--interval", type=float, default=60.0, help="seconds")
    ap.add_argument("--once", action="store_true", help="one pass, then exit")
    a = ap.parse_args()

    logs = os.path.expanduser(a.logs)
    trees = {}
    for w in a.worktree:
        k, _, p = w.partition("=")
        trees[k] = os.path.expanduser(p)
    reported = {}                     # stem -> activity time when last reported

    while True:
        now = time.time()
        busy = busy_trees(list(trees.values()))
        for name in sorted(os.listdir(logs)):
            if not name.endswith(".log"):
                continue
            stem = name[:-4]
            path = os.path.join(logs, name)
            tree = trees.get(stem)
            for key, text, est in active_agents(path):
                tag = f"{stem}:{key}"
                if tree and tree in busy:
                    reported.pop(tag, None)
                    continue
                last = os.stat(path).st_mtime
                if tree:
                    last = max(last, newest_write(tree))
                limit = max(a.stale, est + 5) * 60
                idle = now - last
                if idle < limit:
                    reported.pop(tag, None)
                    continue
                if tag in reported and now - reported[tag] < limit:
                    continue
                reported[tag] = now
                where = f"; nothing running or written in {tree}" if tree else ""
                print(f"STALL {key} ({name}): {idle / 60:.0f} min quiet since "
                      f"'{text[:160]}'{where}", flush=True)
        if a.once:
            return 0
        time.sleep(a.interval)


if __name__ == "__main__":
    sys.exit(main())
