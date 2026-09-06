#!/usr/bin/env python3
"""OSAPI_PKG_RUN runs an image out of memory, and refuses two (SPEC.md 21.5).

    make && make pkgrun && python3 tests/pkgrun.py

The slot exists for the Wire (SPEC.md 26.7), which fetches a `.O88` over the
network into a claim and has no file to name. This is the gate on it, and it
is the `mseg`/`covl` shape: a TEST package that no shipped floppy carries,
built by `make pkgrun`, booted in B: and asked three questions.

QEMU, and by choice rather than by necessity - nothing here is a time, and the
three answers are all state. MartyPC would do as well and costs ten times the
wall clock for a row whose whole content is "did the loader say yes, and did
it say no twice".

WHAT IT ASSERTS, and the first one is asserted against the KERNEL:

  A  an instance named HELLO is LIVE in `inst_tab` - the kernel's own table,
     read through the symbol map, so the pass does not rest on the test
     package's opinion of what happened - AND the region that instance names
     is byte-for-byte `build/hello.o88`. The second half is not belt and
     braces: the slot's first version lost the source OFFSET (it read it out
     of the caller's segment instead of the kernel's) and copied from
     whatever was next to the caller's claim, which still passed
     `ld_check_hdr`, still registered an instance, and then far-called a
     dispatcher that was not one. `hello.o88` is the shipped one and not a
     fixture: the claim is that the slot runs an ORDINARY package.
  B  a spoiled magic answers CF=1 with AL = LD_EBAD.
  C  header flags bit 2 - a package carrying PARTS (SPEC.md 20.12), which are
     read out of a FILE that does not exist here - answers CF=1 / LD_EBAD too,
     and by a different route: the flags test is made before ld_check_hdr,
     which allows the bit.

B and C also say something A cannot: the region and the instance record a
refused load reserved were given back. Three loads happen in this session and
the heap is small; a leak of either shows up as C failing with LD_ENOMEM.

HOW THE VERDICT IS READ. The test package writes a 14-byte block at offset 32
of its own image - immediately after the header, before any code - opening
with the tag 'PR'. The host finds the live instance named PKGRUN in
`inst_tab`, takes its `I_SPTR` (the region's base segment, SPEC.md 20.1) and
reads the block there. No map of the test package is needed and none can go
stale; the tag is the check that the pointer was followed correctly.
"""
import os
import signal
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
# tests/ FIRST and tools/ SECOND, so tools/ ends up at index 0: there is a
# tests/heapmap.py as well as a tools/heapmap.py and the wrong order shadows
# the one with Qmp in it (tests/vmmouse.py's note).
sys.path.insert(0, os.path.join(ROOT, "tests"))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import heapmap                                              # noqa: E402
import os88sym                                              # noqa: E402
import os88qemu                                             # noqa: E402

SOCK = os.path.join(ROOT, "build", "pkgrun.sock")
PIDFILE = os.path.join(ROOT, "build", "pkgrun.pid")

# kernel/instance.inc, mirrored - the record layout is ABI (SPEC.md 20.9)
I_STATE, I_SPTR, I_NAME, I_RECSZ, INST_MAX = 0, 6, 12, 32, 12

PR_OFF = 32                     # the verdict block, at the head of the image
PR_LEN = 14
LD_EBAD = 2                     # SPEC.md 21.4


def say(*a):
    print(*a)
    sys.stdout.flush()


def kill_stale():
    if os.path.exists(PIDFILE):
        try:
            os.kill(int(open(PIDFILE).read().strip()), signal.SIGTERM)
            time.sleep(1)
        except Exception:
            pass
    for f in (SOCK, PIDFILE):
        if os.path.exists(f):
            os.unlink(f)


def build():
    """The kernel under test and the gate's own disk. The row is builds=True
    (tests/suite.py) precisely so this may write build/, and a reader running
    the script by hand should not have to know the target's name."""
    subprocess.run(["make", "-s", "build/os8088.img", "pkgrun"], cwd=ROOT,
                   check=True, stdout=subprocess.DEVNULL)


def launch():
    em = "qemu" + "-system-i386"     # never whole on a command line: kill_stale
    subprocess.run(
        em +
        " -drive file=build/os8088.img,format=raw,if=floppy -boot a"
        " -drive file=build/pkgrun.img,format=raw,if=floppy,index=1"
        " -display none -qmp unix:%s,server,nowait -daemonize -pidfile %s"
        % (SOCK, PIDFILE), cwd=ROOT, shell=True, check=True)
    os88qemu.own(PIDFILE, SOCK)


def instances(q):
    """[(name, sptr)] of every LIVE instance record."""
    b = q.read(os88sym.linear("inst_tab"), I_RECSZ * INST_MAX)
    out = []
    for i in range(INST_MAX):
        r = b[i * I_RECSZ:(i + 1) * I_RECSZ]
        if r[I_STATE] != 1:
            continue
        name = bytes(r[I_NAME:I_NAME + 16]).split(b"\0")[0].decode(
            "ascii", "replace")
        out.append((name, r[I_SPTR] | (r[I_SPTR + 1] << 8)))
    return out


def slots(q):
    """Every record, live or not, with its name - the diagnostic behind the
    entry-count note: two PKGRUN instances would show here even if the second
    had gone."""
    b = q.read(os88sym.linear("inst_tab"), I_RECSZ * INST_MAX)
    out = []
    for i in range(INST_MAX):
        r = b[i * I_RECSZ:(i + 1) * I_RECSZ]
        nm = bytes(r[I_NAME:I_NAME + 16]).split(b"\0")[0].decode(
            "ascii", "replace")
        if r[I_STATE] or nm:
            out.append("%d:%s/%d@%04X" % (i, nm or "-", r[I_STATE],
                                          r[I_SPTR] | (r[I_SPTR + 1] << 8)))
    return " ".join(out)


def main():
    os.chdir(ROOT)              # mouse.hmp shells out to tools/qmp.py by a
                                # RELATIVE path, so this script's cwd is part
                                # of its contract
    kill_stale()
    build()
    fails = []
    try:
        launch()
        q = heapmap.Qmp(SOCK)
        for _ in range(300):
            try:
                q.hmp("info status")
                break
            except OSError:
                time.sleep(0.1)

        # --- boot, then open B: and double-click PKGRUN.O88 ------------------
        # The desktop has to be up before a click means anything, and
        # [desk_sel] existing is not that: wait on the DISK ZONE grid having
        # been laid out, which desk_init does at the end of kmain.
        t0 = time.time()
        while time.time() - t0 < 120:
            if q.read(os88sym.linear("vid_w"), 2)[0]:
                break
            time.sleep(0.25)
        time.sleep(6)               # ...and the first paint after it

        sys.argv = ["mouse.py", SOCK]
        import mouse                                       # noqa: E402
        mouse.SOCK = SOCK

        def dbl(x, y):
            mouse.goto(x, y)
            for _ in range(2):
                mouse.hmp("mouse_button 1")
                time.sleep(0.06)
                mouse.hmp("mouse_button 0")
                time.sleep(0.06)

        dbl(600, 110)               # the B: zone: ordinal 1 of the 640x480
        time.sleep(8)               # desktop, x 584..615, y 92..136

        # PKGRUN.O88 is the SECOND row, and which row it is matters: the
        # listing is sorted by name (SPEC.md 19.4) and HELLO.O88 sorts above
        # it. Double-clicking the first row would launch HELLO off the DISK
        # and assertion A would pass without the slot being called at all.
        dbl(160, 144)

        # --- WAIT FOR IT TO SETTLE, THEN STOP THE MACHINE --------------------
        # A fixed sleep and a running guest were not enough. The wake handler
        # is entered more than once - ui_task puts a wake BACK when a drag or
        # a launch ate its record (SPEC.md 74.1.1, wm_wake_redo) and this
        # script's own mouse traffic is what eats it - so the package guards
        # itself on its entry count and the host reads a machine that has
        # stopped. Every byte below then describes ONE moment.
        t0 = time.time()
        while time.time() - t0 < 90:
            live = instances(q)
            if any(n == "PKGRUN" for n, _ in live):
                seg = dict(live)["PKGRUN"]
                if q.read(seg * 16 + PR_OFF, 3)[2]:     # [pr_done]
                    break
            time.sleep(2)
        time.sleep(3)
        q.hmp("stop")

        # --- what the KERNEL says --------------------------------------------
        live = instances(q)
        names = [n for n, _ in live]
        say("instances: " + ", ".join("%s@%04X" % (n, g) for n, g in live)
            or "(none)")
        seg = dict(live).get("PKGRUN", 0)
        if not seg:
            fails.append("no live instance named PKGRUN: the gate's own "
                         "package did not launch, so nothing below ran")
        if "HELLO" not in names:
            fails.append("A: no live instance named HELLO - OSAPI_PKG_RUN did "
                         "not run the image (SPEC.md 21.5)")
        else:
            # --- AND THE COPY LANDED, byte for byte -----------------------
            # An instance existing says the slot returned; it does not say it
            # copied the RIGHT bytes. The first version of the slot read the
            # source OFFSET out of the caller's segment instead of the
            # kernel's and copied from whatever was there - which passed
            # ld_check_hdr (that reads the caller's bytes, before the copy),
            # registered an instance, and then far-called a dispatcher that
            # was not one. So the region is compared against the FILE.
            hseg = dict(live)["HELLO"]
            want = open(os.path.join(ROOT, "build", "hello.o88"), "rb").read()
            got = bytes(q.read(hseg * 16, min(len(want), 512)))
            if got != want[:len(got)]:
                n = next((i for i in range(len(got))
                          if got[i] != want[i]), 0)
                fails.append("A: the copy is wrong at byte %d - the region "
                             "holds %02X where build/hello.o88 has %02X. The "
                             "instance exists, so the slot RETURNED; what it "
                             "copied is not the image it was given "
                             "(SPEC.md 21.5)" % (n, got[n], want[n]))

        # --- ...and what the package recorded --------------------------------
        if seg:
            b = q.read(seg * 16 + PR_OFF, PR_LEN)
            say("verdict raw: " + bytes(b).hex())
            if bytes(b[:2]) != b"PR":
                fails.append("the verdict block at PKGRUN:%04X is %r, not "
                             "'PR' - I_SPTR did not name the image"
                             % (PR_OFF, bytes(b[:2])))
            else:
                done, ok = b[2], b[3]
                cfa, cfb, cfc = b[4], b[5], b[6]
                ala, alb, alc = b[7], b[8], b[9]
                ferr, ln, ent = b[10], b[11] | (b[12] << 8), b[13]
                say("pkgrun: done %d ok %02X  A cf%d al%d  B cf%d al%d  "
                    "C cf%d al%d  ferr %d len %d entries %d"
                    % (done, ok, cfa, ala, cfb, alb, cfc, alc, ferr, ln, ent))
                if ent != 1:
                    # REPORTED, NOT FAILED. More than one wake per post is the
                    # kernel putting one back that a drag or a launch ate
                    # (SPEC.md 74.1.1) and it is this script's own mouse
                    # traffic that causes it; the package's entry-count guard
                    # is what makes it harmless, and `done` says which entry
                    # finished the checks. Only the three answers below are
                    # assertions.
                    say("note: the wake handler was entered %d times and the "
                        "checks finished on entry %d - the guard held"
                        % (ent, done))
                    say("note: every instance slot: " + slots(q))
                if not done:
                    fails.append("the wake handler never ran to the end - the "
                                 "checks did not happen")
                elif ferr and not ok:
                    fails.append("OSAPI_FILE_READ of HELLO.O88 answered "
                                 "FERR %d, so no check ran" % ferr)
                else:
                    if not ln:
                        fails.append("HELLO.O88 read back as 0 bytes")
                    if (cfa, ala) != (0, 0):
                        fails.append("A: the slot answered CF=%d AL=%d, want "
                                     "CF=0 AL=0" % (cfa, ala))
                    if (cfb, alb) != (1, LD_EBAD):
                        fails.append("B: a spoiled magic answered CF=%d AL=%d, "
                                     "want CF=1 AL=%d (LD_EBAD)"
                                     % (cfb, alb, LD_EBAD))
                    if (cfc, alc) != (1, LD_EBAD):
                        fails.append("C: header flags bit 2 answered CF=%d "
                                     "AL=%d, want CF=1 AL=%d (LD_EBAD) - a "
                                     "package with PARTS reads them from its "
                                     "own FILE (SPEC.md 20.12)"
                                     % (cfc, alc, LD_EBAD))

        shot = os.path.join(ROOT, "build", "pkgrun.png")
        try:
            q.hmp("screendump " + shot)
            say("screen: " + shot)
        except Exception:
            pass
    finally:
        kill_stale()

    for f in fails:
        say("FAIL " + f)
    say("pkgrun: %d assertion(s) failed" % len(fails) if fails
        else "pkgrun: OK - the slot runs an image from memory and refuses "
             "a bad magic and a parts image")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
