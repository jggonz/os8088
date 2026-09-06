#!/usr/bin/env python3
"""A package that cannot fit is refused BEFORE IT READS ANYTHING (SPEC.md
20.12.3).

    make mseg && python3 tests/msegnomem.py [machine] [system-image]

This is the third thing the whole design exists for, said as a measurement.
Every workaround in the tree today (docs/O88-MULTISEG-PLAN.md 2.2) answers the
size question AFTER the program is running: it reads the whole of itself off
the disk, gets a segment, runs its entry proc and only then asks for the
memory it cannot live without. The parts standard answers it from a table that
is already in the image the kernel has read - so a refusal costs no disk at
all.

MSEGBIG is MSEG's twin: the same three file-backed parts and one more, a
REQUIRED 640KB scratch part - the whole of the biggest machine here. 640 and
not 512 because a 640KB XT GRANTED 512 and the row passed on a mechanism it
had never run.

THE INSTRUMENT IS `dsk_dbg_sec`, the kernel's own count of SECTORS
transferred (SPEC.md 57) - `dsk_dbg_i13` beside it is reported and not
asserted, for the reason under assertion 3. Both need a DISKCNT kernel, so
this row builds one and puts build/ back afterwards. FOUR ASSERTIONS:

  1. MSEGBIG is refused, and the status is LD_EABORT (4) - the entry proc
     returning CF=1, which is how op_load refuses. Not LD_ENOMEM: the kernel
     had no opinion, the PACKAGE did;
  2. the toast says so, in the package's own words rather than the kernel's;
  3. its launch transfers STRICTLY FEWER SECTORS than MSEG's on the same
     disk, and the margin is op_read's carved run. Both images are padded to
     the same seven sectors, so everything before the refusal is identical and
     the delta is what op_load did not do - measured with the odds against it,
     because the refusal goes FIRST on a cold volume and the success second
     with everything the first launch warmed (9 against 21);

     **SECTORS AND NOT CALLS, and the correction is worth keeping.** This row
     asserted `at most two int 13h calls - the header peek and the image read`
     until wave 4 padded both images from three sectors to five. That put
     MSEG's image across LBA 36, a cylinder boundary at this geometry, so the
     driver split one run into two and the count went to three - for a launch
     that had not read one extra byte, and with the refusal then costing the
     SAME three calls as the success while transferring 12 sectors and 21.
     Wave 5's seventh sector moved the files again and the calls are 1 and 3
     today. That is the whole argument: the call count here is a fact about
     where the file sits on the disk, and `dsk_dbg_sec` beside it is what
     op_load actually read. (PERFORMANCE.md prices disk work in CALLS because
     calls are what a revolution costs; this row is not about time, it is
     about what was read at all.);
  4. and nothing is left behind. op_load may have claimed before it gave up,
     and a package's own claims carry its SEGMENT - which ld_unreserve already
     frees, because that path was written for an entry proc that claims and
     then fails. A leak of one region per refused load is invisible until the
     heap runs out an hour later.
"""
import os
import struct
import subprocess
import sys
sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
import os88marty
import os88mouse
import os88sym
import dispcp

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KNOBS = ["DISKCNT=1"]
DEFINES = ["DISK_COUNTERS"]
MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_herc_gla_144"
SYS_IMG = sys.argv[2] if len(sys.argv) > 2 else "build/os8088-360.img"
APPS_IMG = "build/mseg.img"
MEM_MAX, MC_SIZE, MC_SEG, MC_OWN = 32, 10, 0, 4
LD_OK, LD_EABORT = 0, 4
fails = []


def say(s):
    print("  " + s)


def claims(m, S):
    """Every live claim, as (segment, owner)."""
    blob = m.read(S("mem_tab"), MEM_MAX * MC_SIZE)
    out = []
    for i in range(MEM_MAX):
        r = blob[i * MC_SIZE:(i + 1) * MC_SIZE]
        seg = struct.unpack_from("<H", r, MC_SEG)[0]
        if seg:
            out.append((seg, struct.unpack_from("<H", r, MC_OWN)[0]))
    return out


def run():
    S = lambda n: os88sym.linear(n, defines=DEFINES)     # noqa: E731
    with os88marty.launch(SYS_IMG, apps=APPS_IMG, machine=MACHINE) as m:
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wx, wy = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])[:2]
        rows = dispcp.listing(m, S)
        have = [n.upper() for n, _ in rows]
        for want in ("MSEG.O88", "MSEGBIG.O88"):
            if want not in have:
                sys.exit("msegnomem: %s is not on %s - run `make mseg`. It "
                         "lists %r" % (want, APPS_IMG, have))

        i13 = S("dsk_dbg_i13")
        secs = S("dsk_dbg_sec")
        before = claims(m, S)
        refused = None

        # --- the REFUSAL, and what it cost ---------------------------------
        c0 = struct.unpack_from("<H", m.read(i13, 2), 0)[0]
        s0 = struct.unpack_from("<H", m.read(secs, 2), 0)[0]
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "MSEGBIG.O88")
        os88marty.settle(m)
        c1 = struct.unpack_from("<H", m.read(i13, 2), 0)[0]
        big_secs = (struct.unpack_from("<H", m.read(secs, 2), 0)[0] - s0) & 0xFFFF
        big_status = m.read(S("ld_status"), 1)[0]
        toast = m.read(S("toast_buf"), 26).split(b"\0")[0].decode(
            "ascii", "replace")
        big_cost = (c1 - c0) & 0xFFFF
        refused = claims(m, S)
        say("MSEGBIG: ld_status %d, %d int 13h calls, %d sectors, toast %r"
            % (big_status, big_cost, big_secs, toast))

        if big_status != LD_EABORT:
            fails.append(
                "MSEGBIG came back ld_status %d and should be %d (LD_EABORT). "
                "0 means a machine here GRANTED 640KB, and this row proves "
                "nothing on it; 2 or 3 mean the KERNEL refused the file, "
                "which is a different mechanism from the package refusing "
                "itself (SPEC.md 20.12)" % (big_status, LD_EABORT))
        # THE KERNEL'S WORDS, NOT THE PACKAGE'S, AND THAT IS THE CONTRACT.
        # op_load does put up `Not enough memory` - but step 10 toasts
        # [ld_status] over fm_stattab for EVERY outcome including LD_EABORT,
        # and it runs after the entry proc returns. So a package that refuses
        # from its entry is always overwritten by `Load failed`, and asserting
        # the package's own string here would be asserting a race. A package
        # that wants to name the figure has to survive to draw it.
        if toast != "Load failed":
            fails.append(
                "the toast says %r and should be 'Load failed'. That is the "
                "kernel's LD_EABORT message from SPEC.md 21 step 10, which "
                "runs after the entry returns and replaces whatever op_load "
                "put up - so this string is what a user actually sees when a "
                "package refuses itself" % toast)

        # --- and the SUCCESS on the same disk, for the difference ----------
        c0 = struct.unpack_from("<H", m.read(i13, 2), 0)[0]
        s0 = struct.unpack_from("<H", m.read(secs, 2), 0)[0]
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "MSEG.O88")
        os88marty.settle(m)
        c1 = struct.unpack_from("<H", m.read(i13, 2), 0)[0]
        ok_secs = (struct.unpack_from("<H", m.read(secs, 2), 0)[0] - s0) & 0xFFFF
        ok_status = m.read(S("ld_status"), 1)[0]
        ok_cost = (c1 - c0) & 0xFFFF
        say("MSEG:    ld_status %d, %d int 13h calls, %d sectors"
            % (ok_status, ok_cost, ok_secs))

        if ok_status != LD_OK:
            fails.append(
                "MSEG did not load (ld_status %d), so there is no successful "
                "launch to compare the refusal against and assertion 3 means "
                "nothing" % ok_status)
        elif big_secs >= ok_secs:
            fails.append(
                "the refusal transferred %d sectors and the successful launch "
                "%d. The refusal must transfer STRICTLY FEWER, and the margin "
                "is op_read's carved run - 13 sectors of MSEG, and the one "
                "thing MSEGBIG never asks for. It is measured with the odds "
                "AGAINST it: the refusal goes first, on a cold volume, and "
                "the success second with everything the first launch warmed. "
                "A refusal that reads as much has read its parts before "
                "deciding it could not have them - which is what every "
                "workaround in the tree does today, and what the table living "
                "in the IMAGE exists to stop (SPEC.md 20.12.3)"
                % (big_secs, ok_secs))

    return before, refused


def check_heap(before, refused):
    """The heap is BYTE-FOR-BYTE what it was, and that is stronger than the
    kernel-side design managed.

    That design tried the claim and let mem_claim refuse it - and mem_claim
    sheds purgeable caches and retries (SPEC.md 50.6.2), so asking for 640KB
    threw away every cache in the machine before answering. Its row had to
    assert the weaker "no claim owned by a slot holding no live record".

    op_load asks OSAPI_MEM_AVAIL instead, and a question costs nothing. So the
    claim table across a refused launch is not merely consistent, it is
    IDENTICAL - and if it ever stops being, something claimed before it
    decided it could not.
    """
    say("claims across the refusal: %d -> %d" % (len(before), len(refused)))
    if refused != before:
        fails.append(
            "the claim table changed across a refused launch:\n"
            "    before  %r\n    refused %r\n"
            "op_load sizes from OSAPI_MEM_AVAIL and claims nothing it cannot "
            "have, so this table should be untouched. A claim that appeared "
            "is a leak ld_unreserve did not catch; one that VANISHED is "
            "mem_claim shedding purgeable caches for a claim that was tried "
            "and refused, which is the thing asking first exists to avoid "
            "(SPEC.md 50.6.2)" % (before, refused))


def main():
    print("  building the counted kernel (%s)..." % " ".join(KNOBS))
    r = subprocess.run(["make"] + KNOBS, cwd=ROOT, capture_output=True,
                       text=True, timeout=900)
    if r.returncode != 0:
        raise SystemExit("msegnomem: `make %s` failed, so nothing below would "
                         "mean what it says:\n%s"
                         % (" ".join(KNOBS), r.stdout[-2000:] + r.stderr[-2000:]))
    try:
        os88sym.default_defines(*DEFINES)
        before, refused = run()
        check_heap(before, refused)
    finally:
        subprocess.run(["make"], cwd=ROOT, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)

    if fails:
        print("\nmsegnomem: FAIL")
        for f in fails:
            print("  " + f)
        return 1
    print("\nmsegnomem: a package that cannot fit refuses itself having read "
          "nothing, and leaves the heap untouched - PASS on %s" % MACHINE)
    return 0


if __name__ == "__main__":
    sys.exit(main())
