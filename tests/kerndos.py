#!/usr/bin/env python3
"""kern_dos's disk layer reads a file, outside the kernel (KERN-DOS-PLAN W3).

    python3 tests/kerndos.py

docs/plans/KERN-DOS-PLAN.md §4 assembles the KERNEL'S OWN `disk.inc` and
`diskw.inc` under a root that is not `kernel.asm`, and its §5 says the shim those
two files need is the work nobody can estimate from outside - big enough, and
the reuse stops paying and the answer is a purpose-written FAT reader instead.

**IT ASSEMBLES AND IT WORKS ARE DIFFERENT CLAIMS, and only the second is worth
anything.** Every entry in `kerndos/kdshim.inc` is a `ret`, a refusal or a
handful of bytes; a `ret` where a value was expected assembles perfectly and
returns garbage, and a mount that half-works returns a directory of plausible
rubbish rather than an error. So this row boots the thing and reads a file.

WHAT IT DOES. Two floppies: **A: carries no file system at all** - sector 0 is
`kerndos/kdboot.asm` and sectors 1..n are the kern_dos blob raw, because a FAT
volume's sectors 1..n are its FATs and the blob cannot ride there - and **B:
is an ordinary 360KB FAT12 volume** with one file on it. kern_dos mounts B:,
finds the file by name, walks its cluster chain, and prints the entry count,
the length and a rotate-and-add checksum through the ROM teletype. This
script computes the same two numbers on the host from the same image with
`tools/os88fat.py`, and they have to agree.

THE CHECKSUM IS A ROTATE-AND-ADD AND NOT A SUM, because a plain sum cannot
tell a reordered chain or a zero run from the real bytes - and a chain read is
precisely the thing that gets those wrong. The file is built to span several
clusters for the same reason: a one-cluster file would pass with no chain walk
at all.

VERIFIED TO FAIL: stubbing `dsk_next_clus` to return cluster+1 keeps the
length and breaks the checksum on any fragmented file; making `mem_claim_x`
answer CF=0 with a junk segment takes the mount down with MOUNT FAILED.
"""
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty as M                                          # noqa: E402

BUILD = os.path.join(ROOT, "build", "kerndos")
BLOB = os.path.join(BUILD, "kerndos.bin")
BOOT = os.path.join(BUILD, "kdboot.bin")
DISK_A = os.path.join(BUILD, "kdboot360.img")
DISK_B = os.path.join(BUILD, "kdgate360.img")
NAME = "GATE.TXT"
# Several clusters, so the chain is actually walked, and not a repeating
# pattern, so a cluster read twice or in the wrong order cannot checksum the
# same. 360KB clusters are 512 bytes here.
PAYLOAD = bytes(((i * 37 + (i >> 8) * 11) & 0xFF) for i in range(9000))


def fail(msg):
    print("kerndos: FAIL: %s" % msg)
    sys.exit(1)


def run(*a, **kw):
    r = subprocess.run(a, cwd=ROOT, capture_output=True, text=True, **kw)
    if r.returncode:
        fail("%s\n%s%s" % (" ".join(a), r.stdout[-2000:], r.stderr[-2000:]))
    return r


def checksum(b):
    """The guest's rotate-and-add, in Python."""
    a = 0
    for x in b:
        a = ((a << 1) | (a >> 15)) & 0xFFFF        # rol ax, 1
        a = (a + x) & 0xFFFF
    return a


def build():
    os.makedirs(BUILD, exist_ok=True)
    run("nasm", "-f", "bin", "-w+error", "-DKD_GATE",
        "-I", "kernel/", "-I", "kerndos/", "-o", BLOB, "kerndos/kerndos.asm")
    mp = os.path.join(BUILD, "kdboot.map")
    run("nasm", "-f", "bin", "-w+error", "-l", os.devnull,
        "-o", BOOT, "kerndos/kdboot.asm", "-Ox", "-s")
    # **THE PATCH OFFSET COMES OUT OF THE ASSEMBLER**, not out of arithmetic
    # on the file's tail: a word counted back from the end is a word that
    # moves the day somebody adds a byte of data, and it moves SILENTLY -
    # the loader still assembles and boots and reads the wrong count.
    src = os.path.join(BUILD, "kdboot_mapped.asm")
    open(src, "w").write(open(os.path.join(ROOT, "kerndos/kdboot.asm")).read()
                         + "\n[map all %s]\n" % mp)
    run("nasm", "-f", "bin", "-w+error", "-o", BOOT, src)
    off = None
    for ln in open(mp):
        p_ = ln.split()
        if len(p_) == 3 and p_[2] == "blobsec":
            off = int(p_[0], 16) - 0x7C00
    if off is None:
        fail("nasm's map has no `blobsec` - kerndos/kdboot.asm changed shape")

    blob = open(BLOB, "rb").read()
    boot = bytearray(open(BOOT, "rb").read())
    secs = (len(blob) + 511) // 512
    boot[off:off + 2] = secs.to_bytes(2, "little")
    boot = bytes(boot).ljust(510, b"\0") + b"\x55\xAA"
    img = bytearray(boot + blob)
    img += b"\0" * (360 * 1024 - len(img))
    open(DISK_A, "wb").write(bytes(img))
    print("kerndos: A: loader + %d blob sector(s)" % secs)

    payload = os.path.join(BUILD, NAME)
    open(payload, "wb").write(PAYLOAD)
    run("python3", "tools/os88disk.py", "-o", DISK_B, "--size", "360", payload)
    print("kerndos: B: %s, %d bytes" % (NAME, len(PAYLOAD)))


def host_truth():
    r = run("python3", "tools/os88fat.py", "ls", DISK_B)
    names = [ln for ln in r.stdout.splitlines() if NAME.split(".")[0] in ln]
    if not names:
        fail("%s is not on the image this row just built:\n%s"
             % (NAME, r.stdout))
    out = os.path.join(BUILD, "back.bin")
    run("python3", "tools/os88fat.py", "cat", DISK_B, NAME, "-o", out)
    got = open(out, "rb").read()
    if got != PAYLOAD:
        fail("os88fat.py read back %d bytes and %d went in - the HOST half of "
             "this comparison is wrong, so nothing it says about the guest "
             "means anything" % (len(got), len(PAYLOAD)))
    return len(PAYLOAD), checksum(PAYLOAD)


def main():
    build()
    want_len, want_sum = host_truth()
    print("kerndos: the host says len %04X sum %04X" % (want_len, want_sum))

    # **boot=<seconds> AND NOT THE DEFAULT**: `launch` normally settles on
    # `desktop_up`, and this image has no desktop and never will - A: is a
    # loader and a blob with no file system on it at all. Waiting for one
    # fails after six guest minutes saying the machine never finished
    # booting, which is true and is not the question.
    with M.launch(DISK_A, apps=DISK_B, machine="os8088_5150_cga_gla",
                  boot=2) as m:
        end = time.time() + 120
        text = ""
        while time.time() < end:
            rows = m.screen() or []
            text = "\n".join(r.rstrip() for r in rows)
            if "gate done" in text or "FAILED" in text or "NOT FOUND" in text:
                break
            time.sleep(0.4)
        print("kerndos: the guest's screen:")
        for r in text.splitlines():
            if r.strip():
                print("   | %s" % r)

        for bad in ("MOUNT FAILED", "FILE NOT FOUND", "CHAIN READ FAILED",
                    "kdboot: READ FAILED"):
            if bad in text:
                fail("the guest reported %r" % bad)
        if "gate done" not in text:
            fail("the gate never finished")

        m_len = re.search(r"len ([0-9A-F]{4})", text)
        m_sum = re.search(r"sum ([0-9A-F]{4})", text)
        if not (m_len and m_sum):
            fail("could not read the length and checksum off the screen")
        got_len, got_sum = int(m_len.group(1), 16), int(m_sum.group(1), 16)

        if got_len != want_len:
            fail("the guest read a length of %d and the file is %d - the "
                 "directory entry the mount SYNTHESIZED does not match the "
                 "one on the disk (SPEC.md 19)" % (got_len, want_len))
        if got_sum != want_sum:
            fail("length %d agrees and the checksum does NOT: guest %04X, "
                 "host %04X. The chain walk delivered the right NUMBER of "
                 "bytes and the wrong ones - a mis-stepped FAT, a run "
                 "coalesced wrongly, or a cluster read twice (SPEC.md 18)"
                 % (got_len, got_sum, want_sum))
        print("kerndos: guest len %04X sum %04X - the kernel's disk layer "
              "read a file OUTSIDE the kernel" % (got_len, got_sum))
    return 0


if __name__ == "__main__":
    sys.exit(main())
