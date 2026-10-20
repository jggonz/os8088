#!/usr/bin/env python3
"""OSAPI_FILE_COPY - the published engine, both verbs (SPEC.md 22.24).

    make fcpapi && python3 tests/fcpapi.py [machine]

EVERY ANSWER IS A FILE, and that is the point.  A copy engine that goes wrong
strands clusters, cross-links two chains or writes a directory entry pointing
at nothing, and all three look perfectly fine from inside the guest - the
listing is drawn from the same structures that are wrong.  So the package
writes its verdict into RESULT.TXT and stops, and this walks the volume
afterwards with an independent FAT12 reader: the COPIES it checks are files,
and so is the verdict.

WHY THE SLOT EXISTS AT ALL.  kernel/filecp.inc's engine had one caller -
Paste - behind a header sentence saying "gfx lock held by the caller", which
describes its callers and not the engine: nothing on the copy path draws, and
the two bodies it streams through are the same two OSAPI_FILE_WRITE and
OSAPI_FILE_APPEND have always published.  The door is a filler for that
engine's own paste path - the operation record set from registers instead of
copied off the clipboard - and what this row would catch is the record it
must NOT inherit: a stale [fcp_lclus] or [fcp_ovwsz] from a previous Paste,
which the shared path sets fresh per operation, and check 2 is what sees an
empty copy if it ever stops doing so.

AND THE MOVE'S CLAIM IS ONE THE GUEST CANNOT MAKE.  A move that quietly copied
would pass every row the package writes: the file is in the new folder, it is
gone from the old one, and the bytes are right.  What says it was RE-LINKED is
that the first cluster is the SAME NUMBER before and after, which needs both
images open at once - so that check is here and not in the package.
"""
import os
import shutil
import struct
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88build                                               # noqa: E402
import os88fat                                                 # noqa: E402
import os88marty                                               # noqa: E402
import os88ui                                                  # noqa: E402

SYS = "build/os8088-360.img"
GATE = "build/fcpapi.img"
SCRATCH = "build/fcpapi-run.img"
# **AND BOTH ARE RESOLVED, because this row MANUFACTURES one of them**
# (docs/plans/SOAK-PARALLEL.md 14.2). `os88marty.launch` puts every image it is
# handed through `os88build.at`, so a parallel run boots out of its FROZEN tree
# - but a row that writes a scratch image with a literal `build/...` string
# writes it to the SHARED one, and the two then disagree. This row did, and the
# failure named neither: `FileNotFoundError: build/trees/plain-<hash>/
# fcpapi-run.img`, 0.2s in, about a copy engine it had not reached. Resolved
# here, `at()` is the identity function for a standalone run and the tree's own
# path under the runner, which is what every other path in the suite gets for
# free by being read rather than written.
def _at(p):
    return os88build.at(p)
WANT = b"os8088 copy"
WANT_MV = b"os8088 move"

CHECKS = [
    "a copy into a subfolder of one volume",
    "...and the bytes arrived",
    "a name in our own folder still resolves afterwards",
    "a missing source is refused with FERR_NOENT",
    "...and no destination was left behind",
    "a move into a subfolder of one volume",
    "...and it left the folder it came from",
    "a move into the folder it is already in answers FERR_EXIST",
    "...and left the source where it was",
]


def subdir(v, name):
    """The entries of one subdirectory of the root, which os88fat.Fat12 does
    not walk - deliberately, and its comment says why.  Reading one here needs
    the public primitives and nothing else: find() gives the folder's entry,
    chain() its clusters, and a directory is 32-byte records like the root."""
    _, _, e = v.find(name)
    if e is None:
        return {}
    first = struct.unpack_from("<H", e, 26)[0]
    out = {}
    csz = v.spc * v.bps
    for c in v.chain(first):
        off = v.cluster_off(c)
        for i in range(csz // 32):
            r = bytes(v.img[off + i * 32:off + i * 32 + 32])
            if r[0] == 0:
                return out
            if r[0] == 0xE5 or r[11] & 0x08 or r[:1] == b".":
                continue
            out[v.pretty(r[:11])] = r
    return out


def subfile(v, ent):
    """The bytes of a subdirectory entry, off its chain."""
    size = struct.unpack_from("<I", ent, 28)[0]
    fc = struct.unpack_from("<H", ent, 26)[0]
    csz = v.spc * v.bps
    return b"".join(bytes(v.img[v.cluster_off(c):v.cluster_off(c) + csz])
                    for c in v.chain(fc))[:size]


def first_clus(v, name):
    _, _, e = v.find(name)
    if e is None:
        return None
    return struct.unpack_from("<H", e, 26)[0]


def fail(msg):
    print("fcpapi: FAIL: %s" % msg)
    sys.exit(1)


def main():
    for p in (SYS, _at(GATE)):
        if not os.path.exists(p):
            fail("%s is missing - `make fcpapi` builds the gate disk" % p)

    # A COPY OF THE IMAGE: the package really writes, and the next test to
    # boot the shipped one would see it.
    shutil.copyfile(_at(GATE), _at(SCRATCH))

    with os88ui.boot(SYS, apps=SCRATCH) as ui:
        # A LONGER WAIT, WITH THE REASON THE MESSAGE ASKS FOR: this package
        # does every one of its checks INSIDE ITS ENTRY, and a window is not
        # up until the entry returns. Five copies, reads and writes is tens of
        # int 13h calls, and one of those is ~400ms of a real 4.77MHz machine
        # - so the default 30 guest seconds is not a hang, it is the work.
        # The verdict is on the disk either way, so a wait that still runs out
        # is a note rather than the answer.
        try:
            ui.path("B:/FCPAPI.O88", limit=180.0)
        except Exception as e:
            print("fcpapi: note: the window did not arrive (%s)"
                  % str(e).splitlines()[0][:70])
        # ...then the rest of its drive traffic, before the image is flushed
        try:
            os88marty.quiesce(ui.m, lambda: (ui.m.disk().get("reads"),
                                             ui.m.disk().get("writes")),
                              guest=2.0, budget=180.0,
                              what="the package's disk traffic to stop")
        except os88marty.MartyError as e:
            print("fcpapi: note: %s" % str(e).splitlines()[0][:70])
        # ...AND THE VOLUME COMES BACK OFF THE GUEST, not off SCRATCH. Every
        # instance boots its OWN clone of the image (which is what makes
        # --marty-jobs safe), so the file on this host was never written to
        # and reading it says "the package did nothing" however well it ran.
        ui.m.flush(1, os.path.abspath(_at(SCRATCH)))  # ABSOLUTE: the emulator's
                                                  # cwd is its own run dir

    v = os88fat.Fat12(_at(SCRATCH))
    names = set()
    for _, _, raw in v.entries():
        if raw[0] in (0, 0xE5) or raw[11] & 0x08:
            continue
        n = raw[:8].decode("ascii", "replace").rstrip()
        e = raw[8:11].decode("ascii", "replace").rstrip()
        names.add(n + "." + e if e else n)
    print("fcpapi: the volume afterwards: %s" % sorted(names))

    if "RESULT.TXT" not in names:
        fail("the package wrote no RESULT.TXT, so it did not finish - the "
             "volume holds %s" % sorted(names))
    res = v.read("RESULT.TXT").split(b"\r")[0].decode("ascii", "replace")
    if len(res) < len(CHECKS):
        fail("RESULT.TXT is %r, shorter than the %d checks" % (res, len(CHECKS)))
    bad = [CHECKS[i] for i, c in enumerate(res[:len(CHECKS)]) if c != "P"]
    for i, c in enumerate(res[:len(CHECKS)]):
        print("fcpapi: %s  %d %s" % ("ok " if c == "P" else "FAIL", i + 1,
                                     CHECKS[i]))
    if bad:
        fail("%d check(s) failed: %s" % (len(bad), "; ".join(bad)))

    sub_ents = subdir(v, "SUB")
    print("fcpapi: SUB afterwards: %s" % sorted(sub_ents))

    # ...and the copy's own bytes, read by something that shares no code with
    # the engine that wrote them.
    if "SRC.DAT" not in sub_ents:
        fail("SUB/SRC.DAT is not on the volume, though the guest said the "
             "copy worked - the directory entry and the guest's listing "
             "disagree")
    got = subfile(v, sub_ents["SRC.DAT"])
    if got != WANT:
        fail("SUB/SRC.DAT holds %r and SRC.DAT holds %r" % (got, WANT))
    print("fcpapi: ok  - SUB/SRC.DAT reads %r off the volume itself" % got)

    if "NOSUCH.DAT" in sub_ents or "NOSUCH.DAT" in names:
        fail("NOSUCH.DAT exists: a copy whose source was missing left a "
             "destination behind (fcp_undo, SPEC.md 22.5.2)")

    # --- the move, which is the host's claim and not the package's --------
    if "MOVE.DAT" in names:
        fail("MOVE.DAT is still in the root: the move did not take, though "
             "the guest said it did")
    if "MOVE.DAT" not in sub_ents:
        fail("MOVE.DAT is in neither the root nor SUB - a move that lost the "
             "file (SUB holds %s)" % sorted(sub_ents))
    body = sub_ents["MOVE.DAT"]
    fc = struct.unpack_from("<H", body, 26)[0]
    got = subfile(v, body)
    if got != WANT_MV:
        fail("SUB/MOVE.DAT holds %r and MOVE.DAT held %r" % (got, WANT_MV))
    print("fcpapi: ok  - SUB/MOVE.DAT reads %r off the volume itself" % got)

    # ...AND IT WAS RE-LINKED, NOT COPIED. Same cluster number, both images.
    was = first_clus(os88fat.Fat12(_at(GATE)), "MOVE.DAT")
    if was is None:
        fail("MOVE.DAT is not on the UNTOUCHED gate image %s - the fixture "
             "is wrong, not the kernel" % GATE)
    if fc != was:
        fail("SUB/MOVE.DAT starts at cluster %d and MOVE.DAT started at %d: "
             "the file's DATA MOVED, so that was a copy and not the re-link "
             "a same-volume move promises (SPEC.md 22.24)" % (fc, was))
    print("fcpapi: ok  - re-linked: still cluster %d, so no data was moved"
          % fc)

    if "SRC.DAT" not in names or v.read("SRC.DAT") != WANT:
        fail("SRC.DAT is not in the root as it was: the one destructive call "
             "that named it was a move INTO ITS OWN FOLDER, which must answer "
             "FERR_EXIST having written nothing (SPEC.md 22.24)")

    r = subprocess.run([sys.executable, "tools/os88disk.py", "--verify",
                        _at(SCRATCH)], capture_output=True, text=True)
    if r.returncode:
        fail("the volume the engine left behind does not verify:\n%s"
             % (r.stdout + r.stderr)[-2000:])
    print("fcpapi: ok  - the volume verifies (no stranded or cross-linked "
          "chain)")
    print("fcpapi: PASS")


if __name__ == "__main__":
    main()
