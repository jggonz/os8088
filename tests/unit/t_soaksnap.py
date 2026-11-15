#!/usr/bin/env python3
"""A frozen soak runs inside a SNAPSHOT the operator's checkout cannot reach.

    python3 tests/unit/t_soaksnap.py

docs/plans/SOAK-PARALLEL.md 14.4. `tools/os88soak.py start` freezes the
checkout - tracked files, uncommitted edits, untracked files that are not
ignored - into a commit object made with a throw-away index, checks that out
into a SLOT (a detached git worktree under build/soak/src/), copies MartyPC's
staged run/ into it, and runs the whole soak from inside it. The promise is
that the session which started the run may edit, commit, `make` and rebuild
MartyPC while it runs, and no row sees any of it.

Every property that promise rests on is a host-side fact about git and the
filesystem, so this checks them on a scratch repository in a few hundred
milliseconds, with no emulator and no `make`:

  1. the snapshot carries what the session would build (an edit, a new file, a
     deletion) and leaves out what .gitignore leaves out;
  2. taking it touches NOTHING of the operator's: index bytes, refs, HEAD and
     `git status` are identical afterwards - the gate on `GIT_INDEX_FILE`;
  3. a clean checkout's snapshot IS its HEAD, so no object is written;
  4. once checked out, the slot does not change when the checkout does;
  5. re-pointing a reused slot rewrites ONLY the files that differ (an
     untouched file keeps its mtime, so the slot's warm build/ stays warm),
     keeps build/, and deletes untracked debris a row left behind;
  6. a slot a LIVE run holds, or the LATEST run's while it can be resumed, is
     never handed to another run, and pruning never removes one;
  7. MartyPC's binary is a COPY (a rebuild in the operator's build/martypc
     cannot reach the run) while its instance registry is the SHARED one
     (`reap` and `status` must see the run's emulators), and deleting a slot
     does not follow that link into the registry.

Break any of them on purpose - `snapshot_commit` without the temporary index,
`pin_instruments` linking run/ instead of copying it, `_reserved` ignoring
`latest`, `sync_slot` re-adding the worktree every time - and a numbered
check below fails naming it.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools"))

FAILS = []


def check(n, ok, what):
    print("%s %d. %s" % ("ok  " if ok else "FAIL", n, what))
    if not ok:
        FAILS.append(n)


def git(cwd, *args):
    env = dict(os.environ, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@t",
               GIT_COMMITTER_NAME="t", GIT_COMMITTER_EMAIL="t@t")
    for k in ("GIT_INDEX_FILE", "GIT_DIR", "GIT_WORK_TREE"):
        env.pop(k, None)
    return subprocess.run(["git"] + list(args), cwd=cwd, env=env, check=True,
                          capture_output=True, text=True).stdout.strip()


def write(p, text):
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "w") as f:
        f.write(text)


def read(p):
    try:
        return open(p).read()
    except OSError:
        return None


def main():
    tmp = tempfile.mkdtemp(prefix="t_soaksnap-")
    try:
        return run(tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def run(tmp):
    home = os.path.join(tmp, "home")
    os.makedirs(home)
    git(home, "init", "-q")
    write(os.path.join(home, ".gitignore"), "build/\nignored.txt\n")
    write(os.path.join(home, "a.txt"), "a1\n")
    write(os.path.join(home, "tools", "x.py"), "x = 1\n")
    write(os.path.join(home, "d.txt"), "doomed\n")
    git(home, "add", "-A")
    git(home, "commit", "-q", "-m", "base")
    head = git(home, "rev-parse", "HEAD")

    import os88soak
    os88soak.HOME = home
    os88soak.RUNS = os.path.join(home, "build", "soak")
    os88soak.SLOTS = os.path.join(os88soak.RUNS, "src")
    os88soak.KEEP_SLOTS = 2

    # 3 first, on the clean tree.
    s0 = os88soak.snapshot_commit()
    check(3, s0["commit"] == head and s0["dirty"] == 0,
          "a clean checkout's snapshot is its HEAD (%s)" % s0["commit"][:10])

    # The session's uncommitted state.
    write(os.path.join(home, "a.txt"), "a2 - uncommitted\n")
    write(os.path.join(home, "new.txt"), "untracked\n")
    write(os.path.join(home, "ignored.txt"), "ignored\n")
    os.remove(os.path.join(home, "d.txt"))
    git(home, "add", "a.txt")                     # one staged, one not
    write(os.path.join(home, "a.txt"), "a3 - edited after staging\n")

    idx = os.path.join(home, ".git", "index")
    before = (open(idx, "rb").read(), git(home, "for-each-ref"),
              git(home, "rev-parse", "HEAD"),
              git(home, "status", "--porcelain"))
    snap = os88soak.snapshot_commit()
    after = (open(idx, "rb").read(), git(home, "for-each-ref"),
             git(home, "rev-parse", "HEAD"),
             git(home, "status", "--porcelain"))
    check(2, before == after,
          "taking the snapshot leaves the index, refs, HEAD and status alone")
    parent = git(home, "rev-parse", snap["commit"] + "^")
    check(1, snap["commit"] != head and parent == head and snap["dirty"] == 3,
          "the snapshot is HEAD plus the 3 uncommitted paths (got %d, "
          "parent %s)" % (snap["dirty"], parent[:10]))

    slot = os88soak.pick_slot()
    how = os88soak.sync_slot(slot, snap["commit"])
    files = (read(os.path.join(slot, "a.txt")),
             read(os.path.join(slot, "new.txt")),
             os.path.exists(os.path.join(slot, "ignored.txt")),
             os.path.exists(os.path.join(slot, "d.txt")))
    check(1, how == "created" and files == ("a3 - edited after staging\n",
                                            "untracked\n", False, False),
          "the slot holds the WORKING TREE: the unstaged edit, the untracked "
          "file, no ignored file, no deleted file (%r)" % (files,))

    # 4: the operator carries on working.
    write(os.path.join(home, "tools", "x.py"), "x = 'broken'\n")
    git(home, "commit", "-qam", "a fix made while the soak runs")
    check(4, read(os.path.join(slot, "tools", "x.py")) == "x = 1\n"
          and read(os.path.join(slot, "a.txt")) == "a3 - edited after staging\n",
          "an edit and a commit in the checkout do not reach the slot")

    # 5: a reused slot is warm.
    write(os.path.join(slot, "build", "product"), "built\n")
    write(os.path.join(slot, "junk.txt"), "a row's debris\n")
    xp = os.path.join(slot, "new.txt")
    old = os.stat(xp).st_mtime_ns
    time.sleep(0.02)                    # so a rewrite would show in the mtime
    snap2 = os88soak.snapshot_commit()
    os88soak._claim(slot)
    how2 = os88soak.sync_slot(slot, snap2["commit"])
    check(5, how2 == "reused"
          and os.stat(xp).st_mtime_ns == old
          and read(os.path.join(slot, "tools", "x.py")) == "x = 'broken'\n"
          and read(os.path.join(slot, "build", "product")) == "built\n"
          and not os.path.exists(os.path.join(slot, "junk.txt")),
          "re-pointing a slot rewrites only what differs, keeps build/ and "
          "an untouched file's mtime, and clears untracked debris")

    # 6: reservation.
    runs = os88soak.RUNS
    r1 = os.path.join(runs, "r1")
    write(os.path.join(r1, "meta.json"),
          json.dumps({"snapshot": slot, "pid": os.getpid()}))
    other = os88soak.pick_slot()
    check(6, os.path.realpath(other) != os.path.realpath(slot),
          "a LIVE run's slot is not handed to the next run")
    dead = subprocess.Popen(["true"])
    dead.wait()
    write(os.path.join(r1, "meta.json"),
          json.dumps({"snapshot": slot, "pid": dead.pid}))
    os.symlink("r1", os.path.join(runs, "latest"))
    os.remove(other + ".claim")
    res = os88soak._reserved()
    check(6, os.path.realpath(slot) in res,
          "the LATEST run's slot stays reserved while it is resumable")
    write(os.path.join(r1, "exit"), "0\n")
    check(6, os.path.realpath(slot) not in os88soak._reserved(),
          "...and is released once that run has exited")
    os.remove(os.path.join(r1, "exit"))
    os88soak.sync_slot(other, snap2["commit"])
    os88soak.KEEP_SLOTS = 1
    gone = os88soak.prune_slots()
    check(6, os.path.isdir(slot) and not os.path.isdir(other)
          and [os.path.realpath(g) for g in gone] == [os.path.realpath(other)],
          "pruning to KEEP_SLOTS removes the free slot and never a reserved "
          "one (removed %r)" % [os.path.basename(g) for g in gone])

    # 7: instruments.
    mrun = os.path.join(home, "build", "martypc", "run")
    write(os.path.join(mrun, "martypc_headless"), "binary v1\n")
    write(os.path.join(mrun, "configs", "m.toml"), "cfg\n")
    reg = os.path.join(home, "build", "martypc", "inst")
    write(os.path.join(reg, "123-row-1-abc", "instance.json"), "{}")
    write(os.path.join(home, "build", "cc", "SmallerC", "smlrcc"), "cc\n")
    os88soak.pin_instruments(slot)
    sbin = os.path.join(slot, "build", "martypc", "run", "martypc_headless")
    sinst = os.path.join(slot, "build", "martypc", "inst")
    write(os.path.join(mrun, "martypc_headless"), "binary v2 - make marty\n")
    check(7, read(sbin) == "binary v1\n" and not os.path.islink(sbin)
          and not os.path.islink(os.path.dirname(sbin)),
          "MartyPC's binary is a COPY: a rebuild in build/martypc does not "
          "reach the slot")
    check(7, os.path.islink(sinst)
          and os.path.realpath(sinst) == os.path.realpath(reg)
          and os.path.islink(os.path.join(slot, "build", "cc")),
          "the instance registry and build/cc are the SHARED ones")
    os88soak.remove_slot(slot)
    check(7, not os.path.exists(slot) and os.path.exists(
        os.path.join(reg, "123-row-1-abc", "instance.json"))
          and slot not in git(home, "worktree", "list"),
          "removing a slot leaves the shared registry alone and forgets the "
          "worktree")

    # The command line the snapshot's copy is given.
    import argparse
    a = argparse.Namespace(tier="soak", k=["disp*"], exclude=["x1"],
                           marty_jobs=1, bpc=None, j=None, resume=True,
                           anyway=False, no_prewarm=False, user_asked=False,
                           strict=False)
    check(8, os88soak._argv(a) == ["start", "--tier", "soak", "-k", "disp*",
                                   "-x", "x1", "--marty-jobs", "1",
                                   "--resume"],
          "`start` hands the snapshot's copy the same selection")

    print("t_soaksnap: %s" % ("FAIL %s" % FAILS if FAILS else "ok"))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
