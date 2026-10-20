"""A HELD stream that runs out of room, and a DELETE while its hold is
pending (SPEC.md 18.4.9.1, 18.4.9.2; the DOS box's writes are held streams
since SPEC.md 96.53).

tests/dostrap/dosfull.asm fills an empty 360KB floppy through one handle in
8KB writes until the box refuses, then deletes the file WITHOUT closing it.
Every cluster must be free afterwards but the program's own, and the host's
own fsck (os88disk --verify) says so or names the lost ones.

WHAT IT WOULD CATCH: a failed held call that flushes its half-built
sub-chain instead of freeing it (on a full disk that is every free cluster
left), and a DELETE that does not commit the pending hold first (the held
chain is never linked, so the delete frees only the committed part, and the
hold's later commit links a freed cluster). VERIFIED TO FAIL on each: 345
lost clusters with the gate unfixed.
"""
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88build                                               # noqa: E402
import os88fat                                                 # noqa: E402
import os88marty                                               # noqa: E402
import os88ui                                                  # noqa: E402

SYS = os88build.at("build/os8088-360.img")
GATE = os88build.at("build/dosfull360.img")
SCRATCH = os88build.at("build/dosfull-run.img")


def fail(msg):
    print("dosfull: FAIL: %s" % msg)
    sys.exit(1)


def main():
    for p in (SYS, GATE):
        if not os.path.exists(p):
            fail("%s is missing - `make build/dosfull360.img`" % p)
    shutil.copyfile(GATE, SCRATCH)
    with os88ui.boot(SYS, apps=SCRATCH) as ui:
        m = ui.m
        if not ui.path("B:/DOSFULL.COM"):
            fail("double-clicking DOSFULL.COM opened no window")
        rows = []

        def done(mm):
            rows[:] = [r.rstrip() for r in (mm.screen() or []) if r.strip()]
            return any(r.startswith("READY") for r in rows)
        try:
            os88marty.until(m, done, "the probe's READY line", poll=1.0,
                            limit=900.0)
        except os88marty.MartyError:
            fail("the probe never finished; the last text screen was %r"
                 % (rows,))
        for r in rows:
            print("   | %s" % r)
        line = [r for r in rows if r.startswith(("FULL", "FAILED"))]
        if not line or not line[0].startswith("FULL"):
            fail("the probe could not start: %r" % line)
        if not line[0].endswith("DEL 0"):
            fail("the DELETE of the held file was refused: %r" % line[0])
        m.type_text("x")
        os88marty.until(m, lambda mm: "Disk" in ui.titles(),
                        "the desktop to come back", guest=60.0, poll=0.5)
        os88marty.quiesce(m, lambda: (m.disk().get("writes"),
                                      m.disk().get("write_sectors")),
                          guest=2.0, what="the floppy's writes to stop")
        m.flush(1, os.path.abspath(SCRATCH))

    v = os88fat.Fat12(SCRATCH)
    names = [v.pretty(r[:11]) for _, _, r in v.entries()
             if r[0] not in (0, 0xE5) and not r[11] & 0x08]
    if "FULL.DAT" in names:
        fail("FULL.DAT is still on the disk after its DELETE: %r" % names)
    r = subprocess.run(["python3", "tools/os88disk.py", "--verify", SCRATCH],
                       capture_output=True, text=True)
    out = (r.stdout + r.stderr).strip()
    print("dosfull: %s" % out.splitlines()[-1] if out else "dosfull: (no fsck output)")
    if r.returncode or "verify OK" not in out:
        fail("the floppy does not verify after a held stream filled it and "
             "was deleted: %s (SPEC.md 18.4.9.1, 18.4.9.2)" % out)
    print("dosfull: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
