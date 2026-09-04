#!/usr/bin/env python3
"""martylock.py - the mutex over MartyPC AND over build/kernel.bin's identity.

WHY THIS EXISTS.  Two independent hazards share one cure, so they share one
lock.

  1. ONE MACHINE, ONE CLIENT.  Every emulator test in this tree drives
     MartyPC's debug server on 127.0.0.1:9001.  A second client does not
     error - it HANGS, or worse, silently drives the FIRST one's machine
     (tools/os88test.py's SERIAL ROWS note).  Two agents benching at once do
     not collide loudly; they quietly report each other's numbers.

  2. A REBUILD INVALIDATES THE SYMBOL MAP.  tools/os88sym.py re-assembles
     kernel.asm and refuses an address unless the result is byte-identical to
     build/kernel.bin.  The About box's build number is the COMMIT COUNT
     (SPEC.md 14.2), so a new commit moves three bytes of .text and a MartyPC
     row then dies saying "the map describes a DIFFERENT kernel".

     BE PRECISE ABOUT WHICH STEP FIRES IT, because the two are separable and
     it matters when a hook or a person wants to commit mid-session.  The
     build number reaches the assembler through build/buildnum.inc, which is
     GENERATED - tools/buildnum.py rewrites it, and only `make` runs it.  So:

       * `git commit` alone changes NOTHING under build/.  buildnum.inc still
         holds the old count, os88sym re-assembles to the same bytes, and a
         running MartyPC session is undisturbed.  The commit ARMS the hazard.
       * `make` is what FIRES it: it regenerates buildnum.inc to the new
         count, rebuilds kernel.bin, and rewrites every floppy image -
         including the one the emulator has MOUNTED, which is the worse half.

     The conservative rule below still says to hold the lock to commit, and
     that is deliberate: agents run `make` constantly, so an unlocked commit
     leaves a landmine for whoever builds next.  But a commit that is followed
     by a DEFERRED make - taken under the lock later - is safe, and that is
     the documented escape when something must be committed now.

  So the rule is not "hold the lock to use MartyPC".  It is:

     HOLD THE LOCK TO USE MARTYPC, **OR** TO DO ANYTHING THAT CHANGES
     build/kernel.bin - `make`, `git commit`, editing kernel/*.

  A writer that skips the lock does not break itself.  It breaks somebody
  else, minutes later, with an error that points at the kernel.

HOW IT IS ATOMIC.  os.mkdir() is atomic on POSIX: exactly one caller creates
the directory and everyone else gets FileExistsError.  No flock, no fcntl, no
dependence on the filesystem honouring advisory locks.

LEASES, NOT PIDS.  An agent holds the lock across MANY Bash calls - boot,
click, screenshot, read the floppy back - and each call is a different shell
with a different PID, so PID liveness cannot answer "is the holder alive".
Instead every lock carries a LEASE with an expiry.  A holder that is still
working renews it; a holder that died lets it lapse, and the next caller may
take it after the lease expires.  That is why `acquire` prints the renew
command it wants you to use.

USAGE (agents: prefer `run`, it cannot leak a lock)

    # one command, lock held only for its duration - the safe default
    python3 tools/martylock.py run --holder finder-wm --why build -- make

    # a multi-step MartyPC session
    python3 tools/martylock.py acquire --holder bench-cga --why marty \
        --purpose "gfxbench baseline, CGA" --ttl 60
    ... drive the emulator across as many calls as you like ...
    python3 tools/martylock.py renew --holder bench-cga --ttl 30
    python3 tools/martylock.py release --holder bench-cga

    python3 tools/martylock.py status          # who holds it, and for how long
    python3 tools/martylock.py wait --timeout 900   # block until free

    # last resort, and it is LOUD and LOGGED
    python3 tools/martylock.py break --reason "holder crashed, lease lapsed 40m ago"

EXIT CODES.  0 success.  1 error.  75 (EX_TEMPFAIL) "someone else holds it" -
so a caller can retry on 75 and give up on 1.
"""
import argparse
import errno
import json
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOCKDIR = os.path.join(ROOT, "build", ".martylock")
META = os.path.join(LOCKDIR, "holder.json")
LOG = os.path.join(ROOT, "build", "martylock.log")

BUSY = 75           # EX_TEMPFAIL: held by someone else, retry is meaningful
DEFAULT_TTL = 45    # minutes
DEFAULT_PORT = 9001
# Seconds a lock directory may exist with no holder.json before it counts
# as rubble rather than as an acquire still in flight.  See _read().
MKDIR_GRACE = 60

# What a holder says it is doing.  This is not decoration: `marty` and the
# build/commit classes are mutually exclusive for DIFFERENT reasons, and the
# status line says which hazard is live so a waiter knows whether to wait
# seconds or minutes.
WHY = {
    "marty":  "driving the emulator (debug server, screenshots, benches)",
    "build":  "running make / rebuilding build/kernel.bin",
    "commit": "git commit - moves the build number, invalidates every map",
    "edit":   "editing kernel sources; the tree is mid-change",
    "other":  "something else that must not overlap the above",
}


def _now():
    return time.time()


def _log(action, holder, detail=""):
    os.makedirs(os.path.dirname(LOG), exist_ok=True)
    stamp = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime())
    with open(LOG, "a") as fh:
        fh.write("%s  %-8s %-24s %s\n" % (stamp, action, holder, detail))


def _read():
    """Current holder metadata, or None if the lock is free."""
    if not os.path.isdir(LOCKDIR):
        return None
    try:
        with open(META) as fh:
            return json.load(fh)
    except (IOError, OSError, ValueError):
        # The directory exists but the metadata does not, or is torn.  There
        # are exactly two ways to get here and they want OPPOSITE answers:
        #
        #   a) somebody won the mkdir microseconds ago and has not written
        #      holder.json yet.  Stealing here would hand the lock to TWO
        #      callers, which is the one outcome this file exists to prevent.
        #   b) a holder died between the two, or something deleted the file.
        #      Then the lock is rubble and must be breakable.
        #
        # The directory's own mtime separates them.  Inside GRACE seconds it
        # is (a) and the lock is held with an un-stealable lease; after that
        # it is (b), and the lease is already expired so `break` applies.
        try:
            born = os.path.getmtime(LOCKDIR)
        except OSError:
            return None
        return {"holder": "<unknown - metadata missing>", "why": "other",
                "purpose": "acquire in progress, or a holder died mid-acquire",
                "acquired": born, "expires": born + MKDIR_GRACE,
                "pid": 0, "port": DEFAULT_PORT}


def _write(meta):
    tmp = META + ".tmp"
    with open(tmp, "w") as fh:
        json.dump(meta, fh, indent=2, sort_keys=True)
    os.replace(tmp, META)


def _fmt_left(meta):
    left = meta["expires"] - _now()
    if left < 0:
        return "LEASE EXPIRED %d min ago" % int(-left / 60)
    return "%d min left on lease" % int(left / 60 + 0.5)


def _describe(meta):
    held = int((_now() - meta["acquired"]) / 60)
    return ("held by %s  (why=%s: %s)\n"
            "  purpose : %s\n"
            "  since   : %d min ago, %s\n"
            "  pid     : %s (AT ACQUIRE - NOT liveness; a holder works across\n"
            "            many shells, so this pid is dead almost at once and\n"
            "            its absence proves nothing) port: %s" %
            (meta["holder"], meta["why"], WHY.get(meta["why"], "?"),
             meta["purpose"] or "(none given)", held, _fmt_left(meta),
             meta["pid"], meta["port"]))


def _try_acquire(args):
    """One attempt.  Returns (ok, meta_of_current_holder_or_None)."""
    os.makedirs(os.path.dirname(LOCKDIR), exist_ok=True)
    try:
        os.mkdir(LOCKDIR)
    except OSError as exc:
        if exc.errno != errno.EEXIST:
            raise
        cur = _read()
        if cur is None:                       # raced with a release
            return _try_acquire(args)
        if cur["holder"] == args.holder:      # re-entrant: extend, do not fail
            cur["expires"] = _now() + args.ttl * 60
            _write(cur)
            _log("reacquire", args.holder, args.purpose)
            return True, cur
        if cur["expires"] < _now():
            # The lease lapsed.  Take it - ATOMICALLY.  This used to _write a
            # fresh holder.json into the existing directory, and two callers
            # that both read the expired lease both did so and both returned
            # ACQUIRED, which is the one outcome this file exists to prevent.
            #
            # NOT by renaming the directory away, which was the first fix and
            # measured 2 of 8 racers ACQUIRED: a racer that read the expired
            # lease and was then delayed renames away the FRESH directory the
            # winner has just re-created, because the rename is not
            # conditional on the directory being the one it read - and
            # putting it back cannot close that either, since a third
            # racer's mkdir can land in the gap.  The steal is a COMPARE-AND-
            # SWAP instead: a `steal` token created O_EXCL is the step exactly
            # one caller can win, and the winner RE-READS the lease under it.
            # A lease that is still expired on the second read is stolen; one
            # that is fresh belongs to whoever won a moment ago and is BUSY.
            # A token whose maker died is rubble after MKDIR_GRACE seconds.
            # A lapsed lease is still said at the top of the log, because
            # somebody died mid-session and their half-driven emulator may
            # be running.
            token = os.path.join(LOCKDIR, "steal")
            try:
                fd = os.open(token, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
            except FileExistsError:
                try:
                    if _now() - os.path.getmtime(token) > MKDIR_GRACE:
                        os.remove(token)
                except OSError:
                    pass
                return False, cur
            except OSError:
                return _try_acquire(args)     # the directory went under us
            os.close(fd)
            try:
                again = _read()
                if again is None:
                    return _try_acquire(args)
                if again["expires"] >= _now():
                    return False, again       # somebody won it just now
                _log("steal", args.holder,
                     "from %s (lease expired)" % again["holder"])
                meta = _mk(args)
                _write(meta)
                return True, None
            finally:
                try:
                    os.remove(token)
                except OSError:
                    pass
        return False, cur

    meta = _mk(args)
    _write(meta)
    _log("acquire", args.holder, "why=%s %s" % (args.why, args.purpose))
    return True, None


def _mk(args):
    return {
        "holder": args.holder,
        "why": args.why,
        "purpose": args.purpose,
        "acquired": _now(),
        "expires": _now() + args.ttl * 60,
        "pid": os.getpid(),
        "port": args.port,
    }


def cmd_acquire(args):
    deadline = _now() + args.wait
    while True:
        ok, cur = _try_acquire(args)
        if ok:
            if cur is None:
                print("martylock: ACQUIRED by %s (why=%s, ttl=%d min, port=%d)"
                      % (args.holder, args.why, args.ttl, args.port))
            print("martylock: renew with -> python3 tools/martylock.py renew "
                  "--holder %s --ttl 30" % args.holder)
            print("martylock: release with -> python3 tools/martylock.py "
                  "release --holder %s" % args.holder)
            return 0
        if _now() >= deadline:
            print("martylock: BUSY - not acquired.\n" + _describe(cur),
                  file=sys.stderr)
            if cur["expires"] < _now():
                print("\nmartylock: that lease has EXPIRED. If you are sure "
                      "the holder is gone:\n"
                      "  python3 tools/martylock.py break --reason '...'",
                      file=sys.stderr)
            return BUSY
        time.sleep(min(10, max(1, deadline - _now())))


def cmd_release(args):
    cur = _read()
    if cur is None:
        print("martylock: already free")
        return 0
    # getattr: `run` reaches here through cmd_run and its subparser defines no
    # --force, so a bare `args.force` was an AttributeError on the one path
    # where the lock was re-acquired rather than fresh.
    if cur["holder"] != args.holder and not getattr(args, "force", False):
        print("martylock: REFUSED - you are %s but the lock is\n%s\n"
              "  (use `break --reason ...` if you really mean to take it away)"
              % (args.holder, _describe(cur)), file=sys.stderr)
        return 1
    shutil.rmtree(LOCKDIR, ignore_errors=True)
    _log("release", args.holder,
         "held %d min" % int((_now() - cur["acquired"]) / 60))
    print("martylock: released by %s" % args.holder)
    return 0


def cmd_renew(args):
    cur = _read()
    if cur is None:
        print("martylock: nothing held - nothing to renew", file=sys.stderr)
        return 1
    if cur["holder"] != args.holder:
        print("martylock: REFUSED - held by %s, not %s"
              % (cur["holder"], args.holder), file=sys.stderr)
        return 1
    cur["expires"] = _now() + args.ttl * 60
    _write(cur)
    _log("renew", args.holder, "+%d min" % args.ttl)
    print("martylock: renewed, %s" % _fmt_left(cur))
    return 0


def cmd_status(args):
    cur = _read()
    if cur is None:
        print("martylock: FREE")
        return 0
    print("martylock: " + _describe(cur))
    return 0 if args.quiet else BUSY


def cmd_wait(args):
    deadline = _now() + args.timeout
    while _now() < deadline:
        cur = _read()
        if cur is None:
            print("martylock: FREE")
            return 0
        if cur["expires"] < _now():
            print("martylock: holder's lease has EXPIRED\n" + _describe(cur))
            return 0
        time.sleep(10)
    print("martylock: still busy after %ds\n" % args.timeout + _describe(_read()),
          file=sys.stderr)
    return BUSY


def cmd_break(args):
    cur = _read()
    if cur is None:
        print("martylock: already free")
        return 0
    print("martylock: BREAKING a lock held by %s" % cur["holder"],
          file=sys.stderr)
    print(_describe(cur), file=sys.stderr)
    print("martylock: reason: %s" % args.reason, file=sys.stderr)
    _log("BREAK", cur["holder"], args.reason)
    shutil.rmtree(LOCKDIR, ignore_errors=True)
    return 0


def cmd_run(args):
    """Acquire, run a command, release - even if the command fails."""
    rc = cmd_acquire(args)
    if rc != 0:
        return rc
    try:
        print("martylock: running: %s" % " ".join(args.cmd), file=sys.stderr)
        proc = subprocess.run(args.cmd, cwd=ROOT)
        return proc.returncode
    finally:
        cmd_release(args)


def main():
    ap = argparse.ArgumentParser(
        description=__doc__.split("\n")[0],
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)

    def holder_args(p, ttl_default=DEFAULT_TTL):
        p.add_argument("--holder", required=True,
                       help="who you are - a stable name, e.g. 'finder-wm'")
        p.add_argument("--ttl", type=int, default=ttl_default,
                       help="lease minutes (default %d)" % ttl_default)

    p = sub.add_parser("acquire", help="take the lock")
    holder_args(p)
    p.add_argument("--why", choices=sorted(WHY), default="marty")
    p.add_argument("--purpose", default="", help="one line: what you are doing")
    p.add_argument("--wait", type=int, default=0,
                   help="seconds to block waiting (default 0 = fail fast)")
    p.add_argument("--port", type=int, default=DEFAULT_PORT)
    p.set_defaults(fn=cmd_acquire)

    p = sub.add_parser("release", help="give it back")
    p.add_argument("--holder", required=True)
    p.add_argument("--force", action="store_true")
    p.set_defaults(fn=cmd_release)

    p = sub.add_parser("renew", help="extend your lease")
    holder_args(p, 30)
    p.set_defaults(fn=cmd_renew)

    p = sub.add_parser("status", help="who holds it")
    p.add_argument("--quiet", action="store_true",
                   help="exit 0 even when held")
    p.set_defaults(fn=cmd_status)

    p = sub.add_parser("wait", help="block until free")
    p.add_argument("--timeout", type=int, default=1800)
    p.set_defaults(fn=cmd_wait)

    p = sub.add_parser("break", help="force-remove a dead holder's lock")
    p.add_argument("--reason", required=True)
    p.set_defaults(fn=cmd_break)

    p = sub.add_parser("run", help="acquire, run a command, always release")
    holder_args(p)
    p.add_argument("--why", choices=sorted(WHY), default="build")
    p.add_argument("--purpose", default="")
    p.add_argument("--wait", type=int, default=1800)
    p.add_argument("--port", type=int, default=DEFAULT_PORT)
    p.add_argument("cmd", nargs=argparse.REMAINDER)
    p.set_defaults(fn=cmd_run)

    args = ap.parse_args()
    if getattr(args, "cmd", None) == "run" or args.fn is cmd_run:
        if args.cmd and args.cmd[0] == "--":
            args.cmd = args.cmd[1:]
        if not args.cmd:
            ap.error("run: give a command after --")
    for attr, default in (("purpose", ""), ("why", "other"),
                          ("ttl", DEFAULT_TTL), ("port", DEFAULT_PORT),
                          ("wait", 0)):
        if not hasattr(args, attr):
            setattr(args, attr, default)
    return args.fn(args)


if __name__ == "__main__":
    # `martylock.py status | head -3` closes the pipe under us, and an
    # unhandled BrokenPipeError prints a traceback that reads exactly like the
    # lock itself failing - which is the one thing this tool must never look
    # like.  Restore the default SIGPIPE so we die quietly the way `cat` does,
    # and keep the except for platforms that do not have it.
    try:
        import signal
        signal.signal(signal.SIGPIPE, signal.SIG_DFL)
    except (ImportError, AttributeError, ValueError):
        pass
    try:
        sys.exit(main())
    except BrokenPipeError:
        try:
            sys.stderr.close()
        except Exception:
            pass
        os._exit(0)
