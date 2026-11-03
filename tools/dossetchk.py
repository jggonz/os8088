#!/usr/bin/env python3
"""Cut DOS.O88's SETUP PART out of its assembly, and check it (SPEC.md 96.32.2.3).

    dossetchk.py <emit.bin> <dosp.bin> <out.bin>

`apps/dos/dos.asm` assembled with -DDOS_SETEMIT is the host, byte for byte,
followed by `section .setp vstart=0 follows=.text align=16` - the Setup page's
code (`apps/dos/dosset.inc`), which runs at offset 0 of a claim of its own. So:

  1. the first len(dosp.bin) bytes of <emit.bin> must BE dosp.bin. A difference
     means the part's assembly is not the shipped host's, and every host
     offset the part was assembled against is somebody else's;
  2. the part starts at the next paragraph, and its word at +2 says where its
     CODE ends (`sp_code_end`; the Shell Link template follows it);
  3. **no near branch in that code may leave it.** The part has a CS of its
     own, and a near `call`/`jmp`/`jcc`/`loop` naming a HOST label assembles
     without a word from nasm - its bin format resolves the displacement
     against the other section - and at run time lands inside the claim at the
     host routine's offset. Every host call is meant to go through a `SPH()`
     stub and a far call; this is what proves none was missed.

Writes the part to <out.bin>. Needs `ndisasm`, which ships with nasm.
"""
import re
import shutil
import subprocess
import sys
import tempfile
import os


def fail(msg):
    sys.exit("dossetchk: FAIL: " + msg)


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    emit = open(sys.argv[1], "rb").read()
    host = open(sys.argv[2], "rb").read()
    if emit[:len(host)] != host:
        n = next(i for i in range(len(host)) if i >= len(emit) or emit[i] != host[i])
        fail("the -DDOS_SETEMIT assembly is not %s for its first %d bytes: it "
             "differs at offset 0x%X, so the Setup part was assembled against a "
             "DIFFERENT host" % (sys.argv[2], len(host), n))
    at = (len(host) + 15) & ~15
    if any(emit[len(host):at]):
        fail("the paragraph pad before the part is not zeros")
    part = emit[at:]
    if len(part) < 16:
        fail("there is no part after the host (%d bytes)" % len(part))
    end = part[2] | (part[3] << 8)
    if not 8 <= end <= len(part):
        fail("the part's code end word says %d of a %d-byte part" % (end, len(part)))
    nd = shutil.which("ndisasm")
    if not nd:
        fail("ndisasm is not installed (it ships with nasm: `make deps`)")
    fd, tmp = tempfile.mkstemp(prefix="dossetp", suffix=".bin")
    try:
        os.write(fd, part[:end])
        os.close(fd)
        # bytes 2..7 are the head's two data words, not instructions
        out = subprocess.run([nd, "-b", "16", "-k", "2,6", tmp],
                             capture_output=True, text=True, check=True).stdout
    finally:
        os.unlink(tmp)
    bad = []
    nbr = 0
    br = re.compile(r"^(call|jmp|j[a-z]+|loop[a-z]*)\s+(short\s+|near\s+)?(0x[0-9a-fA-F]+)$")
    for line in out.splitlines():
        p = line.split(None, 2)
        if len(p) < 3:
            continue
        m = br.match(p[2].strip())
        if not m:
            continue
        nbr += 1
        tgt = int(m.group(3), 16)
        if not 0 <= tgt < end:
            bad.append("%s  %s" % (p[0], p[2].strip()))
    if bad:
        fail("%d near branch(es) leave the part's %d bytes of code - each is a "
             "call into the HOST spelled without SPH() (apps/dos/dosset.inc's "
             "header):\n  %s" % (len(bad), end, "\n  ".join(bad)))
    open(sys.argv[3], "wb").write(part)
    print("dossetchk: the Setup part is %d bytes (%d of code, %d near branches, "
          "none leaving it)" % (len(part), end, nbr))


main()
