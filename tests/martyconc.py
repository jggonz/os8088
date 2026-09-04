#!/usr/bin/env python3
"""Two MartyPC instances at once, and every way that used to go wrong.

    python3 tests/martyconc.py

WHY THIS EXISTS.  The harness was one emulator per box, and not by accident:
`os88marty.launch` killed EVERY martypc_headless before starting its own,
because a survivor holding the one fixed port meant the client silently drove
the STALE machine - a different image at a different point in its boot, which
reads as a hang or as a change that did nothing.  The sweep fixed that and
created a worse one: two terminals, two agents or two rows of the suite in one
checkout took turns killing each other's machines, and the victim saw
BrokenPipeError from a socket to a process that no longer existed.  Every
symptom pointed at the emulator or the guest while the cause was somewhere
else entirely.

So the sweep is gone and three things that were shared are private (the port,
the run tree, the process table's ownership).  This is the gate on that, and
it is worth having because EVERY FAILURE IT CATCHES IS SILENT.  Two instances
sharing a floppy do not error - one of them boots the other's disk.  Two
sharing a port do not error - the second attaches to the first's machine.  A
second client on one instance used to not error either: it hung until the
timeout.  Nothing here would show up as a crash, so nothing here would show up
at all.

WHAT IT ASSERTS, in the order the failures used to bite:

  1. two instances launch and get different ports, directories and processes;
  2. their floppies are SEPARATE FILES with the right contents, which is the
     one that used to be `media/floppies/run0.img` for everybody;
  3. their memories are separate - a poke into one is not visible in the other;
  4. a second client on one instance is REFUSED IN WORDS, in well under a
     second, and the first client is undisturbed by the attempt;
  5. asking for a port another instance holds is an error that NAMES it,
     rather than a silent attach to the wrong machine;
  6. an ORPHAN - an emulator whose owning script died - is reaped, and a live
     instance with a live owner beside it is NOT touched;
  7. and, the headline, two machines BOOT to a desktop concurrently.

Step 7 is the expensive one (two boots) and `--quick` leaves it out; the rest
is a few seconds.  The suite runs the whole thing.
"""
import hashlib
import os
import subprocess
import sys
import time

sys.path.insert(0, "tools")
import os88marty as M                                      # noqa: E402

# GLaBIOS, because it is the ROM set this repo can ship.  The period-accurate
# machines need IBM's BIOS, which is not in the tree (tools/martypc/README.md),
# and a gate that cannot run in a fresh container is not a gate.
MACHINE = "os8088_5150_cga_gla"
IMG = "build/os8088-360.img"
APPS = "build/apps360.img"

fails = []


def check(ok, what, detail=""):
    print("  %s %s%s" % ("ok  " if ok else "FAIL", what,
                         "" if not detail else "  -- " + detail))
    if not ok:
        fails.append(what)
    return ok


def md5(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        for blk in iter(lambda: f.read(1 << 20), b""):
            h.update(blk)
    return h.hexdigest()


def isolation(quick):
    """Two paused machines: ports, trees, floppies, memories."""
    print("two instances, paused:")
    with M.launch(IMG, apps=APPS, machine=MACHINE, boot=0, label="conc-a") as a, \
            M.launch(IMG, apps=APPS, machine=MACHINE, boot=0, label="conc-b") as b:

        check(a.port != b.port, "the two instances got different ports",
              "%d and %d" % (a.port, b.port))
        check(a.pid != b.pid, "...different processes",
              "%d and %d" % (a.pid, b.pid))
        check(a.run_dir != b.run_dir, "...and different run directories")

        # 2. THE FLOPPIES. This is the one that used to be shared: one
        # `media/floppies/run0.img` in the staged tree, per DRIVE, so the
        # second launch overwrote the first's disk under a running machine.
        want = md5(IMG)
        for name, m in (("a", a), ("b", b)):
            p = os.path.join(m.run_dir, "media", "floppies", "run0.img")
            check(os.path.isfile(p) and not os.path.islink(p),
                  "%s has a real floppy of its own" % name, p)
            check(md5(p) == want, "%s's floppy is the image it was given" % name)
        check(os.path.realpath(os.path.join(a.run_dir, "media/floppies")) !=
              os.path.realpath(os.path.join(b.run_dir, "media/floppies")),
              "the two floppy directories are not the same directory")
        for name, m in (("a", a), ("b", b)):
            check(os.path.isfile(os.path.join(m.run_dir, "martypc.log")),
                  "%s kept its own log" % name)

        # 3. THE MEMORIES. Both machines are paused at cycle 0, so a byte
        # written into one can only appear in the other if they are the same
        # machine - which is exactly what a shared port produced.
        a.write(0x00500, b"\xA1")
        b.write(0x00500, b"\xB2")
        check(a.read(0x00500, 1) == b"\xA1" and b.read(0x00500, 1) == b"\xB2",
              "a poke into one machine is not visible in the other",
              "a=%s b=%s" % (a.read(0x00500, 1).hex(), b.read(0x00500, 1).hex()))

        # 4. A SECOND CLIENT. It used to sit in the accept backlog and block
        # until the read timed out - sixty seconds of nothing, reported as a
        # hung guest.
        t0 = time.time()
        try:
            M.Marty(a.addr, timeout=30)
            check(False, "a second client on one instance is refused",
                  "it was ACCEPTED")
        except M.MartyError as e:
            took = time.time() - t0
            check(took < 5.0, "a second client is refused rather than hung",
                  "%.2fs" % took)
            check("one at a time" in str(e) or "already has a client" in str(e),
                  "...and the refusal says what happened",
                  str(e).split("\n")[0][:90])
        check(a.status()["cycles"] == 0 and b.status()["cycles"] == 0,
              "both first clients survived the refused connection")

        # 5. AN EXPLICITLY REQUESTED PORT THAT IS TAKEN. The old failure was
        # to bind-fail in a log nobody reads and drive the other machine.
        try:
            M.launch(IMG, machine=MACHINE, boot=0, addr=a.addr).close()
            check(False, "asking for a held port is an error", "it succeeded")
        except M.MartyError as e:
            check(str(a.pid) in str(e) or "already held" in str(e),
                  "asking for a held port is an error that names the holder",
                  str(e).split("\n")[0][:90])

        # 6. THE REGISTRY, and reap()'s promise to leave live work alone.
        live = {d["port"]: d for d in M.instances() if d["alive"]}
        check(a.port in live and b.port in live,
              "both instances are in the registry")
        check(all(live[p]["owner_alive"] for p in (a.port, b.port)),
              "...both owned by this process")
        killed, _ = M.reap()
        check(killed == 0, "reap() killed nothing while both owners are alive")
        check(a.status()["cycles"] == 0 and b.status()["cycles"] == 0,
              "...and both machines are still answering")


def recycled_pid():
    """A STALE RECORD NAMING A LIVE INSTANCE'S PID MUST NOT KILL IT.

    This is a regression test for a bug this file's own thesis missed, found
    by running eight instances at once. `pid_max` is 32768 on the container
    it appeared on, so a session launching emulators steadily WRAPS the PID
    counter in minutes - and it did: a finished row's record and a live
    instance both named pid 1666. `reap()` read the stale record, asked "is
    1666 a live martypc_headless" (it was - somebody ELSE's), called it an
    orphan and killed it. Silently: no log line, no panic, nothing but a
    socket that closed.

    So a record is pinned to a PROCESS now - (pid, start time) - and the two
    shapes below are the two ways the old code got it wrong. Both are
    fabricated rather than waited for: reproducing a real PID wrap takes
    32,768 processes, and the failure it produces is indistinguishable from
    the emulator having crashed.
    """
    print("a recycled PID:")
    import json
    with M.launch(IMG, machine=MACHINE, boot=0, label="conc-victim") as v:
        root = M._inst_root()
        made = []
        for name, extra in (("wrong-start", {"pid_start": "1"}),
                            ("no-start", {})):
            # A record from a session that died, naming the victim's PID -
            # which is what a wrapped counter hands out.
            d = os.path.join(root, "conc-stale-%s" % name)
            os.makedirs(d, exist_ok=True)
            rec = {"pid": v.pid, "port": 1, "machine": MACHINE, "label":
                   "conc-stale-%s" % name, "owner_pid": 999999,
                   "detached": False, "started": time.time(), "private": True,
                   "run_dir": d, "log": os.path.join(d, "martypc.log"),
                   "ended": False}
            rec.update(extra)
            with open(os.path.join(d, "instance.json"), "w") as f:
                json.dump(rec, f)
            made.append(d)
        try:
            killed, _ = M.reap()
            check(M._is_marty(v.pid),
                  "a stale record naming a live instance's PID does not kill it",
                  "reap() killed %d" % killed)
            check(v.status()["cycles"] == 0,
                  "...and the victim is still answering")
        finally:
            import shutil
            for d in made:
                shutil.rmtree(d, ignore_errors=True)


def orphan():
    """An emulator whose owner died is the survivor case, and reap() takes it.

    Deliberately leaked from a child process that calls `os._exit` - which
    skips every `finally`, every `atexit` and `close()` - because that is what
    a killed session, a crashed script and a `^C` in the wrong place all leave
    behind, and it is the only kind of stray this layer is allowed to kill.
    """
    print("an orphan:")
    # THE BYSTANDER GOES UP FIRST, and the order is the point: `launch()`
    # reaps on the way in, so a bystander started after the orphan would have
    # cleared it before the explicit `reap()` below could be measured. Started
    # first, it is instead the live, owned instance that reap() must NOT
    # touch - which is the half of reap()'s contract that the old blanket
    # sweep got wrong.
    with M.launch(IMG, machine=MACHINE, boot=0, label="conc-bystander") as by:
        _orphan_against(by)


def _orphan_against(by):
    child = (
        "import os, sys; sys.path.insert(0, 'tools'); import os88marty as M;"
        "m = M.launch(%r, machine=%r, boot=0, label='conc-orphan');"
        "print(m.pid, m.port); sys.stdout.flush(); os._exit(0)"
        % (IMG, MACHINE))
    out = subprocess.run([sys.executable, "-c", child], capture_output=True,
                         text=True, timeout=180)
    if out.returncode != 0 or not out.stdout.strip():
        check(False, "the leaking child started an emulator",
              (out.stderr or out.stdout)[-200:])
        return
    pid, port = (int(x) for x in out.stdout.split()[-2:])
    check(M._is_marty(pid), "the leaked emulator outlived its script",
          "pid %d on port %d" % (pid, port))

    rec = [d for d in M.instances() if d.get("pid") == pid]
    check(bool(rec) and rec[0]["alive"] and not rec[0]["owner_alive"],
          "the registry reports it as an orphan - alive, owner gone")

    check(by.status()["cycles"] == 0,
          "the live instance beside it is untouched by the orphan's arrival")

    killed, _ = M.reap()
    check(killed == 1, "reap() killed exactly the orphan", "killed %d" % killed)
    check(not M._is_marty(pid), "...the orphan is gone")
    check(by.status()["cycles"] == 0,
          "...and the live, owned instance beside it never noticed")


def boots():
    """The headline: two machines reach a desktop at the same time."""
    print("two instances, booted:")
    t0 = time.time()
    with M.launch(IMG, apps=APPS, machine=MACHINE, label="conc-boot-a") as a, \
            M.launch(IMG, apps=APPS, machine=MACHINE, label="conc-boot-b") as b:
        took = time.time() - t0
        for name, m in (("a", a), ("b", b)):
            check(M.desktop_up(M._Screen(m)),
                  "%s reached a desktop" % name)
        check(a.status()["cycles"] > 0 and b.status()["cycles"] > 0,
              "both machines ran", "%.1fs for the pair" % took)


def main():
    quick = "--quick" in sys.argv
    if not os.path.exists("build/martypc/run/martypc_headless"):
        sys.exit("no MartyPC - `make marty` first")
    for img in (IMG, APPS):
        if not os.path.exists(img):
            sys.exit("no %s - `make` first" % img)

    mine = set()
    try:
        isolation(quick)
        recycled_pid()
        orphan()
        if not quick:
            boots()
    finally:
        # Never leave one of OUR OWN behind, and never reach past them: a
        # blanket sweep here would be this file failing its own thesis.
        for d in M.instances():
            if d["alive"] and (d.get("label") or "").startswith("conc-"):
                mine.add(d["pid"])
                try:
                    os.kill(d["pid"], 9)
                except OSError:
                    pass
        M.reap()

    if fails:
        sys.exit("martyconc: %d of the concurrency guarantees failed: %s"
                 % (len(fails), "; ".join(fails)))
    print("martyconc: two instances run side by side - separate ports, "
          "directories, disks and memories - a second client on one is "
          "refused in words, and reap() takes orphans and nothing else")


if __name__ == "__main__":
    main()
