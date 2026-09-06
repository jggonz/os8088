#!/usr/bin/env python3
"""Does the DMA staging arm work - and does it move the RIGHT bytes?
(SPEC.md 18.4.2.1, and 18.4.1/18.4.2 for the run arithmetic around it)

    make && python3 tests/dskwstage.py

`dskw_runadd` gives THREE answers. `CF=1` is an I/O error, `CF=0` with `CX=0`
means every sector went into the pending run, and `CF=0` with `CX != 0` means
`dskw_runmax` answered 0 - not one sector fits the 64KB DMA page the source
currently sits in, so the caller stages that one sector through `dsk_secbuf`
and calls again.

**Both callers dropped that third answer** from `2e8e292` until SPEC.md
18.4.2.1: a shared `jmp .ioerr` trampoline sat exactly where it fell through,
so `dskw_wdata.stg` and `dskw_rdata.stg` had ZERO incoming jumps and had never
executed, on any machine, in any test, since they were written. What the user
saw was `FERR_IO` on a transfer whose buffer happened to start in the last 512
bytes of a physical 64KB page - a "Disk error" that moves whenever anything
else in the tree changes size. Fixing it did not restore a path; it TURNED ON
a staging routine nobody had ever watched run, which is why this file asserts
the bytes and not the return code.

NOTHING ON A BOOTED DESKTOP REACHES IT, and that is by design: §18.4.1 keeps
every base of the kernel's own making 512-aligned, and 64KB is a whole number
of sectors, so a sector that starts 512-aligned always ends inside its own
page. The condition therefore has to be ARRANGED, and this file arranges it
the way a package would meet it by accident - `mem_claim` a block big enough
to span a 64KB physical boundary (200KB spans three), then hand `dskw_write_x`
a buffer that starts 0xF0 bytes short of one. `dskw_runmax` answers 0 there
and nowhere else.

FIVE CASES, and the two CONTROLS are what stop this file passing on a harness
that is not arranging anything:

  W1  a straddling WRITE            .stg must fire exactly once
  W2  a page-safe write, same bytes .stg must NOT fire            (control)
  R1  a page-safe read of W1's file .stg must not fire; bytes must match
  R2  a straddling read of it       .stg must fire once; bytes must match
  R3  a page-safe read of W2's file .stg must not fire; bytes must match (control)

R1 is the one that prices the write half: the destination is page-safe, so it
comes back on the ordinary run path, and the bytes it delivers are what the
STAGED write actually put on the disk. R2 is the read half's own.

AND THEN THE HOST READS THE DISK. The floppy is flushed and walked by
`tests/unit/t_image.Vol` - an independent FAT12 reader that shares no code
with the kernel - so the last assertion does not go through os8088 at all:
both files are 2,000 bytes, both chains are sane, both contents equal the
pattern, and **the two files are byte-identical to each other**. The same
2,000 bytes written the staged way and the ordinary way must land the same, or
one of the two paths is wrong and the guest cannot be the one to say which.
`os88disk.py --verify` then fsck's the volume, because a staging path that
wrote a good file over somebody else's clusters would satisfy everything
above.

THE PATTERN IS NOT ZEROES and the destination is POISONED first. A staged
sector that never copied, or copied the wrong 512 bytes, has to show up as a
difference rather than as a plausible run of nulls - and the 48 bytes past EOF
in the destination must still be poison afterwards, which is the overrun test:
the file is 2,000 bytes, the last sector is read whole into `dsk_secbuf`, and
only 464 of its 512 bytes may reach the caller.

`--bug` INVERTS W1: the straddling write must be REFUSED with FERR_IO, which
is what every kernel from `2e8e292` to SPEC.md 18.4.2.1 does. That is the A/B
this row exists to make repeatable - point it at a pre-fix image
(`--img`/`--apps`) and it must pass; point it at this one and it must fail.
"""
import argparse
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(HERE, "unit"))
import os88marty                                            # noqa: E402
import os88sym                                              # noqa: E402
import t_image                                              # noqa: E402

KERNEL_SEG = 0x60
KB = KERNEL_SEG << 4
COLD_SEG = os88sym.equates()["COLD_SEG"]
CB = COLD_SEG << 4

CLAIM_KB = 200                  # > 128KB, so it spans at least two 64KB
                                # physical boundaries whatever segment it lands
                                # on - one for the source, one for the
                                # destination, and neither reused
NEAR_END = 0xF0                 # bytes left in the page at the buffer's start.
                                # Anything in 1..511 makes dskw_runmax answer
                                # 0; 0xF0 is a paragraph multiple, so the
                                # buffer is ES:0 and dskw_norm has nothing to
                                # fold - the straddle is the ONLY thing under
                                # test
FSIZE = 2000                    # 3 whole sectors + a 464-byte tail, over two
                                # clusters at spc=2: the staged sector, a run
                                # after it, a cluster hop, and a partial final
                                # sector, in one file
POISON = 0x5A
FERR_IO = 2


def pattern(n):
    """Distinct-looking bytes: never 0, never POISON, and never periodic with
    512, so a sector delivered from the wrong offset differs."""
    return bytes((((i * 7 + (i >> 8) * 31) & 0xFF) or 0x11) for i in range(n))


class Caller(object):
    """Call a near kernel routine on a PAUSED machine (tests/icoclip.py's).

    THE ENTRY IS `park`, NOT `setreg("ip")`: `pc` is the fetch pointer, so a
    bare write leaves the bytes the 8088 already prefetched from the old
    address in front of the new ones. `park` goes through the reset vector,
    which flushes the queue and clears every register - so all of them are
    written after it.

    CS IS `COLD_SEG` HERE, not KERNEL_SEG. `kernel/diskw.inc`'s transfer
    pipelines live in `.cold` (SPEC.md 2.6), which runs at its own segment,
    and so does the return trap - a near `ret` cannot cross one.

    SS IS LEFT ALONE, and that is a requirement rather than an economy:
    `dskw_stage` reaches `dsk_secbuf` as `push ss / pop es`, so the staging
    this file is about only works with SS = LOW_SEG.
    """

    def __init__(self, m, trap="dskw_rmtree"):
        self.m = m
        self.trap = os88sym.linear(trap)
        r = m.regs()
        # 96 bytes clear of the interrupted frame. The call below goes as deep
        # as the BIOS's own int 13h handler, on this task's stack.
        self.ss, self.sp = r["ss"], (r["sp"] - 96) & 0xFFFF

    def call(self, name, watch=(), limit=180.0, **regs):
        """Returns (registers at the trap, {watched symbol: times hit}).

        The watched symbols are exec breakpoints armed alongside the trap, so
        "did this block execute" is answered by the CPU rather than inferred
        from what came back. `.stg` had never fired in the history of this
        kernel; a test that could not count that would be asserting the fix
        from its own return code.
        """
        m = self.m
        wmap = dict((os88sym.linear(w), w) for w in watch)
        m.bp_exec(self.trap, *wmap.keys())
        m.cmd(cmd="park", cs=COLD_SEG, ip=os88sym.linear(name) - CB)
        sp = (self.sp - 2) & 0xFFFF
        off = self.trap - CB
        m.write((self.ss << 4) + sp, bytes((off & 0xFF, off >> 8)))
        m.setreg("ss", self.ss)
        m.setreg("sp", sp)
        m.setreg("ds", KERNEL_SEG)
        m.setreg("es", KERNEL_SEG)
        for r, v in regs.items():
            m.setreg(r, v & 0xFFFF)
        hits = dict((w, 0) for w in watch)
        while True:
            m.run()
            if m.wait_stop(limit) is None:
                raise SystemExit("dskwstage: %s never returned - the machine "
                                 "is still running after %gs" % (name, limit))
            r = m.regs()
            flat = (r["cs"] << 4) + r["ip"]
            if flat in wmap:
                hits[wmap[flat]] += 1
                continue
            if flat != self.trap:
                raise SystemExit("dskwstage: %s stopped at %#07x, which is "
                                 "neither the return trap %#07x nor a watched "
                                 "symbol" % (name, flat, self.trap))
            return r, hits


def blast(m, addr, data, chunk=2048):
    for i in range(0, len(data), chunk):
        m.write(addr + i, data[i:i + chunk])


def name_at(m, s):
    """Put a NUL-terminated 8.3 name where DS:SI can see it.

    `dskw_rt_name` is `dskw_rmtree`'s 12-byte scratch and nothing else's
    (kernel/diskw.inc has three references, all inside that routine), so it is
    idle for the whole of a read or a write - and rmtree is also this file's
    return trap, so it can never run and reclaim it.
    """
    b = s.encode("ascii") + b"\0"
    assert len(b) <= 12
    m.write(os88sym.linear("dskw_rt_name"), b)
    return os88sym.linear("dskw_rt_name") - KB


def run(img, apps, machine, want_bug, verbose):
    bad = []
    src_pat = pattern(FSIZE)
    with os88marty.launch(img, apps=apps, machine=machine,
                          label="dskwstage") as m:
        m.pause()
        if not m.read(os88sym.linear("dsk_mntok"), 1)[0]:
            raise SystemExit("dskwstage: the boot volume is not mounted - "
                             "there is nothing to write to")
        spc = m.read(os88sym.linear("dsk_spc"), 1)[0]
        c = Caller(m)

        r, _ = c.call("mem_claim_x", ax=CLAIM_KB, bx=0xFF09)   # MEM_K_CLONE
        if r["flags"] & 1:
            raise SystemExit("dskwstage: mem_claim refused %dKB - the heap on "
                             "this machine cannot host the straddle"
                             % CLAIM_KB)
        base, end = r["dx"] << 4, (r["dx"] << 4) + CLAIM_KB * 1024

        # The 64KB physical boundaries inside the claim. Two are needed and a
        # 200KB claim has three wherever it lands, so this is a fact about the
        # size rather than a hope about the allocator.
        bounds = [b for b in range(((base + 0xFFFF) & ~0xFFFF), end, 0x10000)
                  if b - NEAR_END >= base and b + 0x1000 <= end]
        if len(bounds) < 2:
            raise SystemExit("dskwstage: the %dKB claim at %#07x spans %d 64KB "
                             "boundaries and this row needs 2"
                             % (CLAIM_KB, base, len(bounds)))
        straddle_src = bounds[0] - NEAR_END
        straddle_dst = bounds[1] - NEAR_END
        # ...and two page-SAFE buffers, a full 4KB clear of any boundary in
        # both directions, for the controls.
        safe_src = bounds[0] + 0x2000
        safe_dst = bounds[0] + 0x4000
        for a in (safe_src, safe_dst):
            if min((a & 0xFFFF), 0x10000 - (a & 0xFFFF)) < 0x1000:
                raise SystemExit("dskwstage: the 'safe' buffer at %#07x is "
                                 "within 4KB of a page edge - the control "
                                 "would be testing the same thing as the case"
                                 % a)
        # DISJOINT, asserted rather than eyeballed. Every buffer is written
        # whole (4KB for a destination, so the poison covers the read), and
        # two that overlap make one case quietly rewrite another's source -
        # which shows up as a byte comparison passing for the wrong reason.
        spans = {"straddle_src": (straddle_src, 4096),
                 "straddle_dst": (straddle_dst, 4096),
                 "safe_src": (safe_src, 4096), "safe_dst": (safe_dst, 4096)}
        for i, (n1, (a1, l1)) in enumerate(sorted(spans.items())):
            if a1 < base or a1 + l1 > end:
                raise SystemExit("dskwstage: %s at %#07x runs outside the "
                                 "claim %#07x..%#07x" % (n1, a1, base, end))
            for n2, (a2, l2) in sorted(spans.items())[i + 1:]:
                if a1 < a2 + l2 and a2 < a1 + l1:
                    raise SystemExit("dskwstage: %s (%#07x) and %s (%#07x) "
                                     "overlap" % (n1, a1, n2, a2))
        if verbose:
            print("  claim %#07x..%#07x  spc=%d" % (base, end, spc))
            print("  straddling src %#07x (page off %#06x), dst %#07x"
                  % (straddle_src, straddle_src & 0xFFFF, straddle_dst))
            print("  page-safe  src %#07x, dst %#07x" % (safe_src, safe_dst))

        blast(m, straddle_src, src_pat)
        blast(m, safe_src, src_pat)

        # --- W1: the straddling write --------------------------------------
        r, hits = c.call("dskw_write_x", watch=("dskw_wdata.stg",),
                         si=name_at(m, "STGW.TST"),
                         es=straddle_src >> 4, bx=0, cx=FSIZE, dx=0)
        cf, ax, n = r["flags"] & 1, r["ax"], hits["dskw_wdata.stg"]
        if want_bug:
            if not (cf and ax == FERR_IO):
                bad.append("W1 --bug: the straddling write returned CF=%d "
                           "AX=%d, and a pre-18.4.2.1 kernel refuses it with "
                           "CF=1 FERR_IO(%d). This image is FIXED."
                           % (cf, ax, FERR_IO))
            if n:
                bad.append("W1 --bug: dskw_wdata.stg executed %d times, and "
                           "in a pre-18.4.2.1 kernel nothing can reach it at "
                           "all" % n)
            return bad
        if cf or ax:
            bad.append("W1: the straddling write was REFUSED, CF=%d AX=%d "
                       "(FERR_IO is %d). dskw_runmax answered 0 and the third "
                       "case went to the error arm - which is exactly the "
                       "defect SPEC.md 18.4.2.1 fixed" % (cf, ax, FERR_IO))
        if n != 1:
            bad.append("W1: dskw_wdata.stg executed %d times, want 1. The "
                       "source starts %d bytes short of a 64KB page, so "
                       "exactly one sector stages and the rest go in runs"
                       % (n, NEAR_END))

        # --- W2: the same bytes, page-safe (control) -----------------------
        r, hits = c.call("dskw_write_x", watch=("dskw_wdata.stg",),
                         si=name_at(m, "SAFEW.TST"),
                         es=safe_src >> 4, bx=0, cx=FSIZE, dx=0)
        if r["flags"] & 1 or r["ax"]:
            bad.append("W2 (control): an ORDINARY write failed, CF=%d AX=%d - "
                       "this row's machinery is broken, not the staging"
                       % (r["flags"] & 1, r["ax"]))
        if hits["dskw_wdata.stg"]:
            bad.append("W2 (control): dskw_wdata.stg fired %d times on a "
                       "PAGE-SAFE buffer, so the .stg counter above is not "
                       "measuring the straddle" % hits["dskw_wdata.stg"])

        # --- R1/R2/R3: read it back ----------------------------------------
        def readback(tag, fname, dst, want_stg):
            m.write(dst, bytes([POISON]) * 4096)
            r, hits = c.call("dskw_read_x", watch=("dskw_rdata.stg",),
                             si=name_at(m, fname),
                             es=dst >> 4, bx=0, cx=4096, dx=0)
            got = m.read(dst, 4096)
            if r["flags"] & 1:
                bad.append("%s: reading %s failed, AX=%d"
                           % (tag, fname, r["ax"]))
                return None
            size = r["ax"] | (r["dx"] << 16)
            if size != FSIZE:
                bad.append("%s: %s came back %d bytes, want %d"
                           % (tag, fname, size, FSIZE))
            n = hits["dskw_rdata.stg"]
            if n != want_stg:
                bad.append("%s: dskw_rdata.stg executed %d times, want %d"
                           % (tag, n, want_stg))
            if got[:FSIZE] != src_pat:
                first = next(i for i in range(FSIZE)
                             if got[i] != src_pat[i])
                bad.append("%s: %s reads back WRONG at byte %d (sector %d, "
                           "offset %d): got %#04x, want %#04x. The staging "
                           "path moved bytes and moved the wrong ones, which "
                           "is worse than the error it replaced"
                           % (tag, fname, first, first // 512, first % 512,
                              got[first], src_pat[first]))
            tail = got[FSIZE:512 * ((FSIZE + 511) // 512)]
            if tail != bytes([POISON]) * len(tail):
                bad.append("%s: %d bytes PAST EOF were overwritten in the "
                           "caller's buffer - the last sector is read whole "
                           "into dsk_secbuf and only %d of its bytes may "
                           "reach the caller"
                           % (tag, sum(1 for b in tail if b != POISON),
                              FSIZE % 512))
            return got[:FSIZE]

        r1 = readback("R1", "STGW.TST", safe_dst, 0)
        r2 = readback("R2", "STGW.TST", straddle_dst, 1)
        readback("R3", "SAFEW.TST", safe_dst, 0)
        if r1 is not None and r2 is not None and r1 != r2:
            bad.append("R1/R2: the same file read into a page-safe buffer and "
                       "into a straddling one differ - the read staging is "
                       "delivering different bytes from the run path")

        # --- and the host reads the disk, with none of the kernel's code ---
        with tempfile.NamedTemporaryFile(suffix=".img", delete=False) as t:
            out = t.name
        try:
            m.flush(0, path=out)
            bad += host_check(out, src_pat, verbose)
        finally:
            os.unlink(out)
    return bad


def host_check(path, src_pat, verbose):
    """Walk the flushed floppy with tests/unit/t_image's own FAT12 reader.

    Independent of os8088 by construction: it shares no code with the kernel,
    so a writer and a reader that agree on the same wrong thing cannot pass
    it. This is where "the bytes are right" is actually settled.
    """
    bad = []
    v = t_image.Vol(t_image.read(path), os.path.basename(path))
    found = {}
    for _path, name11, attr, clus, size in v.walk():
        nm = name11.decode("ascii", "replace")
        if nm in ("STGW    TST", "SAFEW   TST"):
            found[nm.replace(" ", "")] = (clus, size)
    for want in ("STGWTST", "SAFEWTST"):
        if want not in found:
            bad.append("host: %s is not in the flushed volume's root at all"
                       % want)
    if len(found) != 2:
        return bad

    blobs = {}
    for nm, (clus, size) in sorted(found.items()):
        if size != FSIZE:
            bad.append("host: %s's directory entry says %d bytes, want %d"
                       % (nm, size, FSIZE))
        chain, eoc = v.chain(clus)
        if not (0xFF8 <= eoc <= 0xFFF):
            bad.append("host: %s's chain ends at %#05x, which is not an EOC - "
                       "the FAT link the staged sector's cluster needed never "
                       "landed" % (nm, eoc))
        want_clus = -(-FSIZE // (512 * v.spc))
        if len(chain) != want_clus:
            bad.append("host: %s owns %d clusters, want %d"
                       % (nm, len(chain), want_clus))
        blob = b""
        for cl in chain:
            lba = v.cluster_lba(cl)
            blob += v.blob[lba * v.byts: (lba + v.spc) * v.byts]
        blobs[nm] = blob[:FSIZE]
        if blobs[nm] != src_pat:
            first = next((i for i in range(min(len(blobs[nm]), FSIZE))
                          if blobs[nm][i] != src_pat[i]), FSIZE)
            bad.append("host: %s's bytes ON THE DISK are wrong from byte %d "
                       "(sector %d + %d) - read by a FAT12 walker that shares "
                       "no code with the kernel that wrote them"
                       % (nm, first, first // 512, first % 512))
        if verbose:
            print("  host: %-9s %d bytes, clusters %s"
                  % (nm, size, chain))
    if len(blobs) == 2 and blobs["STGWTST"] != blobs["SAFEWTST"]:
        bad.append("host: the staged write and the ordinary write of the SAME "
                   "2,000 bytes produced different files")

    rc = subprocess.run([sys.executable,
                         os.path.join(ROOT, "tools", "os88disk.py"),
                         "--verify", path],
                        capture_output=True, text=True)
    if rc.returncode:
        bad.append("host: os88disk.py --verify refuses the volume after the "
                   "staged write:\n%s" % (rc.stdout + rc.stderr).strip())
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--img", default=os.path.join(ROOT, "build",
                                                  "os8088-360.img"))
    ap.add_argument("--apps", default=os.path.join(ROOT, "build",
                                                   "apps360.img"))
    ap.add_argument("--machine", default="os8088_5150_cga")
    ap.add_argument("--bug", action="store_true",
                    help="assert the PRE-18.4.2.1 behaviour instead: the "
                         "straddling write must be refused FERR_IO and .stg "
                         "must never execute")
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()

    bad = run(a.img, a.apps, a.machine, a.bug, a.verbose)
    for b in bad:
        print("FAIL: %s" % b)
    if bad:
        print("dskwstage: %d failure(s)" % len(bad))
        return 1
    print("dskwstage: %s" % (
        "--bug: PRE-18.4.2.1 behaviour confirmed - the straddling write is "
        "refused FERR_IO and .stg never executes" if a.bug else
        "5 cases, 2 controls, host-side FAT12 read-back - the DMA staging arm "
        "runs and moves the right bytes"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
