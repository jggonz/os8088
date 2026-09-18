#!/usr/bin/env python3
"""The built-in commands - a COMMAND.COM that is not a file (SPEC.md 96.30).

    make build/dossh360.img && python3 tests/dosshell.py

EVERY ANSWER IS A FILE, twice over.  The probe writes the eight exit codes into
RESULT.TXT, and the host then walks the volume with an independent FAT12
reader - because a shell that reports success and writes nothing looks perfect
from inside the guest, the directory it would show being drawn from the same
structures that are wrong.

THE PROBE RUNS UNDER A REAL IBM DOS UNCHANGED (tests/dostrap/shellref.asm,
docs/DOS-DEBUGGING.md), which is what makes the expected codes a MEASUREMENT of
DOS rather than a description of us.  Under DOS it drives the genuine
COMMAND.COM; here it drives apps/dos/dosh.inc.

THE THREE .TXT BODIES DIFFER ON PURPOSE.  "A file arrived" is not the
assertion - "the RIGHT file arrived" is, and a copy engine that fetched the
wrong directory entry would pass the first and fail the second.

AND THE MOVE'S CLAIM IS THE HOST'S ALONE: a move that quietly copied would put
TWO.TXT in SUB with the right bytes and take it out of the root, passing every
row the probe can write.  What says it was RE-LINKED (SPEC.md 22.25) is the
first cluster being the same number on the untouched gate image and on the one
the guest left.
"""
import os
import shutil
import struct
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88build                                               # noqa: E402
import os88fat                                                 # noqa: E402
import os88ui                                                  # noqa: E402

SYS = "build/os8088-360.img"
# **BOTH RESOLVED AGAINST THE RUN'S TREE** (tools/os88build.at). This row is
# the shape docs/plans/SOAK-PARALLEL.md 14.2 does not reach on its own: it
# WRITES a scratch image and then boots it, and only the BOOT went through the
# resolver - so under `os88soak` the copy landed in the shared `build/` while
# os88marty.launch looked for it in the frozen tree, and the row died in 0.1s
# with a FileNotFoundError naming a directory it had never heard of. It passes
# under `os88test` and fails under `os88soak`, which is the worst way for a
# row to be wrong: whoever runs it the quick way sees nothing.
GATE = os88build.at("build/dossh360.img")
SCRATCH = os88build.at("build/dossh-run.img")

# The exit code each check must answer.  None = "anything but zero", which is
# what a refusal is: the CODE is DOS's business and only its being non-zero is
# a contract (SPEC.md 96.30.2).
CHECKS = [
    ("copy SRC.TXT DST.TXT",      "0"),
    ("del DST.TXT > NUL",         "0"),
    ("ren ONE.TXT ONE.BAK",       "0"),
    ("move TWO.TXT SUB",          "0"),
    ("copy *.BAK SUB",            "0"),
    ("frobnicate  (unknown)",     None),
    ("copy NOSUCH.TXT X.TXT",     None),
    ("copy SRC.TXT > OUT.TXT",    None),
    ("copy SHBIG.DAT SUB (full)", None),
]


def fail(msg):
    print("dosshell: FAIL: %s" % msg)
    sys.exit(1)


def subdir(v, name):
    """One subdirectory's entries.  os88fat.Fat12 walks the root only, and
    deliberately - its comment says why - so this uses the public primitives:
    find() gives the folder's entry, chain() its clusters, and a directory is
    32-byte records exactly like the root."""
    _, _, e = v.find(name)
    if e is None:
        return {}
    out = {}
    csz = v.spc * v.bps
    for c in v.chain(struct.unpack_from("<H", e, 26)[0]):
        off = v.cluster_off(c)
        for i in range(csz // 32):
            r = bytes(v.img[off + i * 32:off + i * 32 + 32])
            if r[0] == 0:
                return out
            if r[0] == 0xE5 or r[11] & 0x08 or r[:1] == b".":
                continue
            out[v.pretty(r[:11])] = r
    return out


def body(v, raw):
    size = struct.unpack_from("<I", raw, 28)[0]
    first = struct.unpack_from("<H", raw, 26)[0]
    if not first:
        return b""
    csz = v.spc * v.bps
    return b"".join(bytes(v.img[v.cluster_off(c):v.cluster_off(c) + csz])
                    for c in v.chain(first))[:size]


def main():
    for p in (SYS, GATE):
        if not os.path.exists(p):
            fail("%s is missing - `make build/dossh360.img` builds the gate" % p)

    shutil.copyfile(GATE, SCRATCH)          # the probe really writes
    was = os88fat.Fat12(GATE)
    two_clus = struct.unpack_from("<H", was.find("TWO.TXT")[2], 26)[0]

    with os88ui.boot(SYS, apps=SCRATCH) as ui:
        m = ui.m
        if not ui.path("B:/SHELLREF.COM"):
            fail("double-clicking SHELLREF.COM opened no window")
        # Eight shell-outs, each of them real file I/O on a 4.77MHz machine.
        # The probe exits by itself; what is waited for is the WINDOW coming
        # back, which is the bracket ending.
        rows = []
        end = time.time() + 240.0
        while time.time() < end:
            rows = m.screen() or []
            if any("SHELLREF DONE" in r for r in rows):
                break
            time.sleep(0.5)
        else:
            print("dosshell: note: no DONE marker; the last text screen was %r"
                  % ([r.rstrip() for r in rows if r.strip()][:12],))
        print("dosshell: the bracket's text screen:")
        for r in (rows or [])[:12]:
            if r.strip():
                print("      | %s" % r.rstrip())
        time.sleep(2)
        m.flush(1, os.path.abspath(SCRATCH))   # ABSOLUTE: every instance boots
                                               # its own clone, so the file on
                                               # this host was never written

    v = os88fat.Fat12(SCRATCH)
    names = {}
    for _, _, raw in v.entries():
        if raw[0] in (0, 0xE5) or raw[11] & 0x08:
            continue
        names[v.pretty(raw[:11])] = raw
    print("dosshell: the root afterwards: %s" % sorted(names))
    sub = subdir(v, "SUB")
    print("dosshell: SUB afterwards:      %s" % sorted(sub))

    if "RESULT.TXT" not in names:
        fail("no RESULT.TXT, so the probe did not finish - the root holds %s"
             % sorted(names))
    raw = v.read("RESULT.TXT").split(b"\r")[0].decode("ascii", "replace")
    n = len(CHECKS)
    res = ["".join((raw[i], raw[i + n])) if len(raw) > i + n else "--"
           for i in range(n)]
    print("dosshell: RESULT.TXT = %r -> %s" % (raw, res))
    if len(raw) < n * 2:
        fail("RESULT.TXT is %r, shorter than the %d checks" % (raw, n))

    bad = []
    for i, (label, want) in enumerate(CHECKS):
        got = res[i]
        if got.startswith("X"):
            ok = False
            why = "the EXEC itself was refused - there is no shell at all"
        elif want is None:
            ok = got not in ("00", "--")
            why = "wanted any non-zero code, got %r" % got
        else:
            ok = got == want.rjust(2, "0")
            why = "wanted %r, got %r" % (want, got)
        print("dosshell: %s  %d %-26s exit %s"
              % ("ok " if ok else "FAIL", i, label, got))
        if not ok:
            bad.append("%s: %s" % (label, why))
    if bad:
        fail("%d check(s) wrong: %s" % (len(bad), "; ".join(bad)))

    # --- and now the files, which is the half the guest cannot assert -------
    if "DST.TXT" in names:
        fail("DST.TXT survived `del DST.TXT > NUL`, which reported success")
    if "ONE.TXT" in names or "ONE.BAK" not in names:
        fail("the rename did not happen: the root holds %s" % sorted(names))
    if "TWO.TXT" in names:
        fail("TWO.TXT is still in the root after `move TWO.TXT SUB`")
    if "OUT.TXT" in names or "X.TXT" in names:
        fail("a REFUSED command left a file behind: %s" % sorted(names))

    for want_name, want_body in (("TWO.TXT", b"os8088 shell two"),
                                 ("ONE.BAK", b"os8088 shell one")):
        if want_name not in sub:
            fail("SUB does not hold %s - it holds %s" % (want_name, sorted(sub)))
        got = body(v, sub[want_name])
        if got != want_body:
            fail("SUB/%s holds %r and should hold %r"
                 % (want_name, got, want_body))
        print("dosshell: ok  - SUB/%s reads %r off the volume itself"
              % (want_name, got))

    # ...and the UNDO: check 8's copy ran the volume out of space part way
    # through, so the destination was CREATED and then failed. A short file
    # that looks whole is worse than no file (SPEC.md 96.30.6).
    if "SHBIG.DAT" in sub:
        got = body(v, sub["SHBIG.DAT"])
        fail("SUB/SHBIG.DAT survived a copy that ran out of disk: %d bytes of "
             "a %d-byte source, which looks like a whole file to anything that "
             "opens it. dsh_stream's undo did not run (SPEC.md 96.30.6)"
             % (len(got), 20 * 1024))
    print("dosshell: ok  - the out-of-space copy left no partial file behind")

    now = struct.unpack_from("<H", sub["TWO.TXT"], 26)[0]
    if now != two_clus:
        fail("SUB/TWO.TXT starts at cluster %d and TWO.TXT started at %d: MOVE "
             "COPIED IT. OSAPI_FILE_COPY's move verb (SPEC.md 22.24) either "
             "did not try its re-link or the re-link declined and it fell "
             "through to its copy" % (now, two_clus))
    print("dosshell: ok  - MOVE re-linked: still cluster %d, no data moved"
          % now)

    r = subprocess.run([sys.executable, "tools/os88disk.py", "--verify",
                        SCRATCH], capture_output=True, text=True)
    if r.returncode:
        fail("the volume the shell left does not verify:\n%s"
             % (r.stdout + r.stderr)[-2000:])
    print("dosshell: ok  - the volume verifies")
    print("dosshell: PASS")


if __name__ == "__main__":
    main()
