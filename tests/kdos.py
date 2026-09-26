#!/usr/bin/env python3
"""A .COM runs under kern_dos (docs/plans/KERN-DOS-PLAN.md wave 4).

    python3 tests/kdos.py

W3 got the KERNEL's disk layer running outside the kernel for 92 bytes of
shim. This puts the DOS CORE on top of it: `kerndos/kdos.asm` is that root
plus `apps/dos/dos.asm` whole and unedited plus `kerndos/kdback.inc`, which is
the SECOND implementation of the twenty-two `dos_k_*` doors - going to the
kernel's disk layer directly instead of through an `OSAPI_*` cell, because
there is no API table here and no UI task whose stack to borrow.

**THAT IS KERN-DOS-PLAN §3's WHOLE CLAIM** - *"a second implementation of
twenty-two doors, and nothing above them changes"* - and this row is what
makes it a fact rather than a reading of the source. Nothing above the doors
is edited: `dos_int21`, the handle layer, the PSP, the FCBs, `AH=4Bh` and the
memory chain are the same bytes the shipped package assembles.

WHAT THE PROGRAM PROVES, and why it is four things and not one. Every line
`tests/dostrap/kdhello.asm` prints comes out of INT 21h and none out of the
BIOS, so each is evidence that a DOS call was SERVICED:

  1  a banner (AH=09h)       - the string services work at all
  2  the version (AH=30h)    - a dispatch that RETURNS a value, not one that
                               only has to not crash
  3  the arena, read out of  - the number this whole plan exists to move,
     PSP:0002                  read where a real DOS puts it rather than
                               where we say it is
  4  a file it opens and     - the file layer end to end, through kdback.inc's
     reads itself            - doors rather than the package's

...and then AH=4Ch with a known exit code, so a program that FINISHED can be
told from one that fell over into a zero.

THE DISKS ARE kerndos/kdboot.asm's SHAPE (tests/kerndos.py): A: carries no
file system at all - a loader and the blob raw, because a FAT volume's
sectors 1..n are its FATs - and B: is an ordinary 360KB FAT12 volume with the
program and its data file on it.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from os88geom import KD_SEG                                 # noqa: E402
import os88marty as M                                          # noqa: E402

BUILD = os.path.join(ROOT, "build", "kerndos")
BLOB = os.path.join(BUILD, "kdos.bin")
BOOT = os.path.join(BUILD, "kdboot.bin")
DISK_A = os.path.join(BUILD, "kdosboot360.img")
DISK_B = os.path.join(BUILD, "kdosprog360.img")
PROG = "KDHELLO.COM"
DATA = "KDDATA.TXT"
# Sixteen bytes is what the program reads back, so the marker is sixteen
# characters exactly - a short file would have it printing the newline too and
# a long one would prove nothing more.
MARKER = "kern_dos-reads-4"
EXITC = 0x2A


def fail(msg):
    print("kdos: FAIL: %s" % msg)
    sys.exit(1)


def run(*a):
    r = subprocess.run(a, cwd=ROOT, capture_output=True, text=True)
    if r.returncode:
        fail("%s\n%s%s" % (" ".join(a), r.stdout[-2000:], r.stderr[-2000:]))
    return r


def build():
    os.makedirs(BUILD, exist_ok=True)
    # A MAP, because the row reads kern_dos's own words out of the guest.
    # tools/os88sym.py resolves the KERNEL's symbols and this is not the
    # kernel, so the root is assembled with `[map all]` appended to a copy -
    # the same thing os88sym does one build along, and for the same reason.
    src = os.path.join(BUILD, "kdos_m.asm")
    open(src, "w").write(open(os.path.join(ROOT, "kerndos/kdos.asm")).read()
                         + "\n[map all %s]\n" % os.path.join(BUILD, "kdos.map"))
    run("nasm", "-f", "bin", "-w+error", "-DKD_GATE", "-I", "kernel/",
        "-I", "kerndos/", "-I", "apps/", "-I", "apps/dos/", "-I",
        "drivers/net/", "-o", BLOB, src)
    mp = os.path.join(BUILD, "kdboot2.map")
    src = os.path.join(BUILD, "kdboot2.asm")
    open(src, "w").write(open(os.path.join(ROOT, "kerndos/kdboot.asm")).read()
                         + "\n[map all %s]\n" % mp)
    run("nasm", "-f", "bin", "-w+error", "-o", BOOT, src)
    off = None
    for ln in open(mp):
        p = ln.split()
        if len(p) == 3 and p[2] == "blobsec":
            off = int(p[0], 16) - 0x7C00
    if off is None:
        fail("nasm's map has no `blobsec`")

    blob = open(BLOB, "rb").read()
    boot = bytearray(open(BOOT, "rb").read())
    secs = (len(blob) + 511) // 512
    boot[off:off + 2] = secs.to_bytes(2, "little")
    img = bytearray(bytes(boot).ljust(510, b"\0") + b"\x55\xAA" + blob)
    img += b"\0" * (360 * 1024 - len(img))
    open(DISK_A, "wb").write(bytes(img))
    print("kdos: A: loader + %d blob sector(s) (%d bytes)" % (secs, len(blob)))

    com = os.path.join(BUILD, PROG)
    run("nasm", "-f", "bin", "-w+error", "-o", com,
        "tests/dostrap/kdhello.asm")
    dat = os.path.join(BUILD, DATA)
    open(dat, "w").write(MARKER)
    run("python3", "tools/os88disk.py", "-o", DISK_B, "--size", "360",
        com, dat)
    print("kdos: B: %s (%d bytes) and %s" % (PROG, os.path.getsize(com), DATA))


def symbols():
    """{name: offset} out of nasm's map of the root this row just built."""
    out = {}
    for ln in open(os.path.join(BUILD, "kdos.map")):
        p = ln.split()
        if len(p) == 3:
            try:
                out[p[2]] = int(p[0], 16)
            except ValueError:
                pass
    return out


def main():
    build()
    syms = symbols()
    for want in ("dos_akb",):
        if want not in syms:
            fail("nasm's map has no `%s`" % want)
    with M.launch(DISK_A, apps=DISK_B, machine="os8088_5150_cga_gla",
                  boot=2) as m:
        seen = {"text": ""}

        def said(_):
            rows = m.screen() or []
            text = seen["text"] = "\n".join(r.rstrip() for r in rows)
            # THE WAIT ENDS ON THE LAST THING THE MACHINE SAYS, and getting
            # that wrong costs the row's whole timeout rather than failing:
            # when the gate stopped printing `kern_dos: done` this loop sat
            # out its 120 seconds and the runner killed it at 60, reporting a
            # TIMEOUT for a run that had finished in four.
            return ("restart" in text or "kern_dos: " in text
                    or "FAILED" in text)
        try:                            # a GUEST-time budget; a miss is
            M.until(m, said, "kern_dos to report", poll=0.4,  # judged on
                    limit=120.0)                              # the text below
        except M.MartyError:
            pass
        text = seen["text"]
        print("kdos: the guest's screen:")
        for r in text.splitlines():
            if r.strip():
                print("   | %s" % r)

        for bad in ("MOUNT FAILED", "LOAD FAILED", "kdboot: READ FAILED"):
            if bad in text:
                fail("kern_dos reported %r before the program ran" % bad)

        # 1: the program ran at all
        if "KDHELLO under kern_dos" not in text:
            fail("the banner never appeared - AH=09h did not reach the "
                 "screen, so either dos_prog_enter never jumped or INT 21h "
                 "is not hooked (SPEC.md 96.3, 96.5)")
        print("kdos: 1/4 the banner - AH=09h is serviced")

        # 2: a dispatch that returns a value
        mv = re.search(r"DOS version (\d+)\.(\d+)", text)
        if not mv:
            fail("no version line - AH=30h answered nothing, which a "
                 "dispatch that merely does not crash would also do")
        print("kdos: 2/4 AH=30h answered %s.%s" % mv.groups())

        # 3: the arena, out of the PSP rather than out of us.
        #
        # **THE PROGRAM'S NUMBER IS CHECKED AGAINST kern_dos's OWN**, read out
        # of the guest by name rather than printed. The first version of this
        # row compared it against a line the GATE printed - and when the gate
        # stopped printing one, the check had nothing to compare with and said
        # so; before that it matched the gate's line instead of the program's,
        # which was green and measured nothing. [dos_akb] is what kern_dos laid
        # out and PSP:0002 is what the program read, so the pair is the claim
        # (SPEC.md 96.3): a wrong number there is a wrong arena, not a wrong
        # printer.
        ma = re.search(r"PSP says (\d+) KB", text)
        if not ma:
            fail("no `PSP says` line - PSP:0002 read as nothing (SPEC.md 96.3)")
        kb = int(ma.group(1))
        laid = m.readseg(KD_SEG, syms["dos_akb"], 2)
        laid = laid[0] | (laid[1] << 8)
        if not (0 <= laid - kb <= 32):
            fail("kern_dos laid out %d KB and the program reads %d above its "
                 "PSP: the difference should be the PSP's own paragraphs plus "
                 "the file window, which is single-figure KB. A program "
                 "reading MORE than exists is SPEC.md 96.11's failure - it "
                 "would hand out the window as its own memory" % (laid, kb))
        # ...and that the BDA is where the ceiling came from. 0x9000 was wave
        # 4's hard-coded top and is 576 KB; the machine says 640, and the
        # difference is the 60 KB the constant was throwing away.
        if kb < 512:
            fail("the program has %d KB, which is below what wave 4's "
                 "hard-coded 0x9000 ceiling already gave it - the launch "
                 "block's KDL_CAP is the BDA's own int 12h figure and should "
                 "be MORE, not less" % kb)
        print("kdos: 3/4 the program has %d KB above its PSP, against the %d "
              "kern_dos laid out" % (kb, laid))

        # 4: the file layer, end to end, through the new doors
        if MARKER not in text:
            fail("the program did not read %s back: it printed %r. AH=3Dh, "
                 "3Fh and 3Eh go through kerndos/kdback.inc's doors, so this "
                 "is the second back end and not the package's"
                 % (DATA, [r for r in text.splitlines()
                           if "file says" in r] or "(no file line)"))
        print("kdos: 4/4 it opened and read %s through the NEW back end" % DATA)

        if "KDHELLO done" not in text:
            fail("the program never reached its own last line")

        # ...and AH=4Ch came back to kern_dos with the program's OWN code. A
        # zero is what a fall into the arena produces as readily as a clean
        # exit, so the number is the check and reaching the line is not.
        me = re.search(r"the program has exited, code (\d+)", text)
        if not me:
            fail("AH=4Ch did not come back to kern_dos - dos_terminate "
                 "returned somewhere else (SPEC.md 96.5, 96.38.2)")
        if int(me.group(1)) != EXITC:
            fail("kern_dos reports exit code %s where KDHELLO.COM exited with "
                 "%d: [dos_exit] is written by dos_terminate off AL, so a "
                 "wrong code means the terminate path ran on something other "
                 "than the program's own AH=4Ch" % (me.group(1), EXITC))

        # THE REBOOT IS THE ARM'S CONTRACT AND NOT A DETAIL
        # (docs/plans/KERN-DOS-PLAN.md §9): on a machine with nothing to come
        # back to, the program ending means the machine restarts, and it says
        # so before it does. A kern_dos that halted quietly instead would pass
        # every check above.
        if "restart" not in text:
            fail("kern_dos did not say it was restarting the machine. "
                 "KDLF_REBOOT is set in the block this gate stages, so the "
                 "exit path owes int 19h and a sentence first - a silent "
                 "halt is the failure that reads like success")
        print("kdos: ...and AH=4Ch came back with code %d, the machine "
              "offering the restart KDLF_REBOOT asked for" % EXITC)
    print("kdos: ok - the DOS core ran over a back end that is not the kernel")
    return 0


if __name__ == "__main__":
    sys.exit(main())
