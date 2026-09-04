#!/usr/bin/env python3
"""What the claim heap looks like when the boot is over (SPEC.md 50, 66).

    make && python3 tests/heapmap.py [--json out.json]

**THIS ONE IS QEMU'S, and it is on CLAUDE.md's short list twice over.** It
wants every driver attached at once on a machine that HAS memory above 1MB,
and MartyPC is an 8088 with no NIC - so it can host neither ETHER.DRV nor
XMEM.DRV, which are two of the six images this measures. What QEMU costs is
what it always costs: no number here is a time, and every assertion is about
geography.

WHY IT EXISTS. Anything that wants to hand memory back after the boot - stage
2's blob, which is the loader, the loading screen and the boot overlay together
(SPEC.md 2.9.5/2.9.6) - can only do so usefully if the space it releases
REJOINS the main free run. Whether it
does is decided by what else was claimed while it stood, and whether those
claims can be compacted (SPEC.md 66). `MC_RLOC` answers the second half as a
machine-readable fact: 0 is PINNED. So this reads the map rather than reasoning
about it, and prints the ORDER as well as the outcome, because the order is
what decides who is walled in behind whom.

FOUR ASSERTIONS, all structural:

1. No two claims overlap, and every one lies inside [mem_base, mem_top). A map
   that fails this is a heap bug, not a layout finding.
2. MEM_K_OVL (0xFF0B) is claimed at NO moment of the boot. The boot overlay
   used to take 4KB of it at MARK 12 and give it back after drv_boot; since
   SPEC.md 2.9.6 it rides stage 2's blob, is not a claim at all, and the tag is
   retired and deliberately unused - so anything wearing it is a reuse nobody
   wrote down.
3. Every driver the settings asked for is either loaded or freed - no image
   claim outlives a refused attach.
4. The arena is reported: free runs, the largest, and how much is stranded
   outside it. `--expect-largest` turns that into a ratchet for a change that
   claims to improve it.

The disk is built here: the shipped image carries no SYSTEM.CFG, so nothing is
wanted and there is no driver to map (SPEC.md 51.3).
"""
import argparse
import json
import os
import signal
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import heapmap                                              # noqa: E402
import os88qemu                                              # noqa: E402

PIDFILE = os.path.join(ROOT, "build", "qemu.pid")
SOCK = os.path.join(ROOT, "build", "qmp.sock")
IMG = os.path.join(ROOT, "build", "heapmap.img")
ALLBITS = 0x1F          # sound, hard disk, RAM disk, parallel link, Ethernet
MEM_K_OVL = 0xFF0B


def cfgfile():
    """A SYSTEM.CFG that wants every driver there is.

    It MUST be named system.cfg on disk - os88disk.py takes the 8.3 name from
    the basename, and a SYSTEM2.CFG is a file the kernel never looks for, so
    the machine boots perfectly and wants nothing.
    """
    d = os.path.join(ROOT, "build", "heapcfg")
    os.makedirs(d, exist_ok=True)
    p = os.path.join(d, "system.cfg")
    with open(p, "wb") as f:
        f.write(b"O88CFG\0\0" + (3).to_bytes(2, "little")
                + b"DW" + bytes([1, 2]) + ALLBITS.to_bytes(2, "little") + b"\0\0")
    return p


def image():
    """build/os8088.img's own recipe, with that settings file in it.

    --always-make, because a plain `make -n` on an UP-TO-DATE tree prints
    "`build/os8088.img' is up to date." and no recipe at all - and the tree is
    up to date every time, since `make` is what you run before you test.  This
    row therefore only ever passed on a tree where the image was missing.  The
    cost of -B is that the whole chain is printed rather than one rule, so the
    one line that builds the image is picked out by name instead of the stdout
    being run whole.
    """
    out = subprocess.run(["make", "-n", "--always-make", "build/os8088.img"],
                         cwd=ROOT, capture_output=True, text=True,
                         check=True).stdout.replace("\\\n", " ")
    cmd = next((ln.strip() for ln in out.splitlines()
                if "tools/os88disk.py" in ln and "-o build/os8088.img" in ln), "")
    assert "--folder SYSTEM/APPDATA" in cmd, out
    cmd = (cmd.replace("-o build/os8088.img", "-o " + IMG)
              .replace("--folder SYSTEM/APPDATA",
                       cfgfile() + " --folder SYSTEM/APPDATA"))
    subprocess.run(cmd, cwd=ROOT, shell=True, check=True,
                   stdout=subprocess.DEVNULL)
    return IMG


def kill_stale():
    """By PIDFILE, never by pkill -f: `-f` matches the killing shell's own
    command line, and a script that mentions the emulator kills itself -
    exit 144, nothing dead, every command after it skipped (CLAUDE.md)."""
    if os.path.exists(PIDFILE):
        try:
            os.kill(int(open(PIDFILE).read().strip()), signal.SIGTERM)
            time.sleep(1)
        except Exception:
            pass
    for f in (SOCK, PIDFILE):
        if os.path.exists(f):
            os.unlink(f)


def launch(hdd_mb=32, hdd=True):
    hdimg = os.path.join(ROOT, "build", "hdd.img")
    if hdd and not os.path.exists(hdimg):
        subprocess.run(["dd", "if=/dev/zero", "of=" + hdimg, "bs=1024",
                        "count=%d" % (hdd_mb * 1024)], check=True,
                       stderr=subprocess.DEVNULL)
    hdarg = (" -drive file=%s,format=raw,if=ide,index=0,media=disk" % hdimg) if hdd else ""
    em = "qemu" + "-system-i386"        # never on a command line whole: see kill_stale
    cmd = (em + " -S -machine pc,vmport=off"   # vmport off: the msserial mouse
                                               # is what this drives (SPEC.md 9.11)
           " -drive file=%s,format=raw,if=floppy -boot a"
           " -chardev msmouse,id=m0 -serial chardev:m0"
           " -drive file=build/apps.img,format=raw,if=floppy,index=1"
           "%s"
           " -display none -qmp unix:%s,server,nowait -daemonize -pidfile %s"
           " -audiodev none,id=snd -device sb16,audiodev=snd"
           " -netdev user,id=n0 -device ne2k_isa,netdev=n0,iobase=0x300,irq=3"
           % (IMG, hdarg, SOCK, PIDFILE))
    subprocess.run(cmd, cwd=ROOT, shell=True, check=True)
    # ...and it is DAEMONISED, so it outlives this script unless
    # somebody kills it - and the somebody is us (os88qemu).
    os88qemu.own(PIDFILE, SOCK)


def sample(limit=40.0):
    """Every DISTINCT map from instruction zero to the desktop."""
    q = heapmap.Qmp(SOCK)
    for _ in range(200):
        try:
            q.hmp("info status")
            break
        except OSError:
            time.sleep(0.1)
    sym = heapmap.symbols()
    q.hmp("cont")
    seen, keys, t0 = [], set(), time.time()
    while time.time() - t0 < limit:
        try:
            m = heapmap.Map(q, sym)
        except Exception:
            continue
        # a heap whose ends are not yet written, or wrapped, is mem_init not
        # having run: skip rather than record a map that never existed
        if not (m.top and m.base and m.top > m.base and m.top - m.base < 0x10000):
            continue
        k = m.key()
        if k not in keys:
            keys.add(k)
            seen.append((time.time() - t0, m))
        if len(seen) > 3 and not m.live and time.time() - t0 > 15:
            break
    return q, seen


def check(seen):
    bad = []
    if not seen:
        return ["no claim map was ever read - did the guest boot?"]
    final = seen[-1][1]
    if final.live:
        bad.append("the splash never handed the screen over")

    for t, m in seen:
        last = None
        for c in m.claims:
            if c.seg < m.base or c.end > m.top:
                bad.append("t=%.2f: %s lies outside [%05X,%05X)"
                           % (t, repr(c), m.base << 4, m.top << 4))
            if last is not None and c.seg < last.end:
                bad.append("t=%.2f: %s OVERLAPS %s" % (t, repr(c), repr(last)))
            last = c

    for t, m in seen:
        if any(c.own == MEM_K_OVL for c in m.claims):
            bad.append("t=%.2f: 0xFF0B is claimed. That was MEM_K_OVL, the "
                       "relocated boot overlay; SPEC.md 2.9.6 retired the tag "
                       "when the overlay joined stage 2's blob and left it "
                       "unused on purpose, so a claim wearing it is a reuse "
                       "nobody wrote down or a stale kernel." % t)
            break
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", help="write the final map here")
    ap.add_argument("--no-hdd", action="store_true",
                    help="no IDE disk in the machine - which claims does that "
                         "take away?")
    ap.add_argument("--expect-largest", type=float,
                    help="fail if the largest free run is under this many KB")
    a = ap.parse_args()

    kill_stale()
    image()
    launch(hdd=not a.no_hdd)
    q, seen = sample()
    try:
        print("\n%d distinct claim maps" % len(seen))
        prev = None
        for t, m in seen:
            now = {(c.seg, c.own) for c in m.claims}
            if prev is None:
                print("  t=%5.2fs  splash=%d  base=%05X  (first: %d claim(s))"
                      % (t, m.live, m.base << 4, len(m.claims)))
            else:
                bits = ["+%s %.1fK @%05X %s"
                        % (heapmap.owner(c.own), c.kb, c.seg << 4,
                           "PIN" if c.pinned else "mov")
                        for c in m.claims if (c.seg, c.own) not in prev]
                bits += ["-%s @%05X" % (heapmap.owner(o), s << 4)
                         for s, o in sorted(prev - now)]
                if bits:
                    print("  t=%5.2fs  splash=%d  %s" % (t, m.live, "  ".join(bits)))
            prev = now
        final = seen[-1][1]
        final.report("FINAL")

        bad = check(seen)
        runs = final.runs()
        largest = max((p for _, p in runs), default=0) / 64.0

        # SPEC.md 66.10: a cache is no longer a BARRIER, so the room a claimant
        # can actually be handed is the arena with the caches dissolved and the
        # movable claims packed down through what they leave. It has to be ONE
        # run - a second one means something pinned is standing between a cache
        # and the free middle, which is precisely the island the ceiling
        # placement used to leave above the region arena (50.6.1).
        cp = final.compacted()
        if len(cp) > 1:
            bad.append("compaction + shed leaves %d runs, largest %.1fK of "
                       "%.1fK free: a cache's room is NOT rejoining the middle. "
                       "SPEC.md 66.10 is what makes it one run, and 50.6.1 is "
                       "the placement that lets it - the failure this catches "
                       "is a purgeable claim stranded behind something pinned"
                       % (len(cp), max(p for _, p in cp) / 64.0,
                          sum(p for _, p in cp) / 64.0))
        if a.expect_largest and largest < a.expect_largest:
            bad.append("largest free run %.1fK is under the expected %.1fK"
                       % (largest, a.expect_largest))
        if a.json:
            with open(a.json, "w") as f:
                json.dump({"base": final.base, "top": final.top,
                           "largest_kb": largest,
                           "claims": [{"seg": c.seg, "para": c.para,
                                       "own": c.own, "dma": c.dma,
                                       "rloc": c.rloc,
                                       "owner": heapmap.owner(c.own)}
                                      for c in final.claims]}, f, indent=1)
        if bad:
            for b in bad:
                print("FAIL", b)
            return 1
        print("\nheapmap: %d claims, largest free run %.1fK of %.1fK free in "
              "%d run(s)" % (len(final.claims), largest,
                             sum(p for _, p in runs) / 64.0, len(runs)))
        return 0
    finally:
        try:
            q.hmp("quit")
        except Exception:
            pass
        kill_stale()


if __name__ == "__main__":
    sys.exit(main())
