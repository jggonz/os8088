#!/usr/bin/env python3
"""HEADLESS DOS: a shortcut saved `Close when done` (SPEC.md 96.44.20).

    make && make kdostest && python3 tests/doshead.py

A `.LNK` whose Shell Link header says `ShowCommand = 7` is read by DOS.O88's
LOADER, which marks part 1 - the window, the tail of the host (SPEC.md
96.44.19) - lazy before a sector is read. So the region is born without it,
the program's arena is the window's size larger, and the instance ends with
the program: a toast for a non-zero exit, and gone. Two legs, each on a fresh
boot of the shipped 360KB system disk on the 5150 CGA, B: a scratch floppy
this row builds with DOSHELLO.COM and two links to it that differ in that
one field:

  H  HEAD.LNK, ShowCommand 7: [dos_head] is 1; the claim DOS runs in is
     part 0's length and not a paragraph more (no tail); the program reaches
     READY; a key makes it exit 42, and the menu bar's toast says
     `Exit 42: DOSHELLO.COM` and the instance is GONE;
  W  WIN.LNK, ShowCommand 1: [dos_head] is 0; the claim is part 0 + part 1;
     after the exit the instance is still there;

and between them: H's arena is at least part 1's size in KB larger, less one
for the rounding. Each leg prints the guest seconds and `int 13h` calls from
the double-click to READY, which is DOS-HEADLESS-PLAN 7's measurement.

VERIFIED TO FAIL: with the loader's flip taken out (`or byte [op_table ...],
OP_LAZY` in apps/dos/dosload.asm), leg H's claim is the windowed one and the
row goes red on its size and on the arena. OS88_DOSHEAD_SYS names another
system disk to run against, which is how that was done.
"""
import os
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import dosmap                                                  # noqa: E402
import os88geom                                                # noqa: E402
import os88marty                                               # noqa: E402
import os88ui                                                  # noqa: E402

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
SYS = os.environ.get("OS88_DOSHEAD_SYS", "build/os8088-360.img")
PROG = "build/DOSHELLO.COM"
P0 = "build/dosp.bin"
P1 = "build/doswin.bin"
MACHINE = "os8088_5150_cga_gla"
HZ = os88marty.GUEST_HZ
CLSID = bytes([0x01, 0x14, 0x02, 0x00, 0, 0, 0, 0,
               0xC0, 0, 0, 0, 0, 0, 0, 0x46])
fails = []


def fail(msg):
    print("doshead: FAIL: %s" % msg)
    fails.append(msg)


def shell_link(show, target):
    """A Shell Link the way DOS writes one (SPEC.md 96.21): the 76-byte header,
    HasRelativePath alone, `.\\TARGET`, and the terminal ExtraData block"""
    hdr = bytearray(76)
    struct.pack_into("<I", hdr, 0, 0x4C)
    hdr[4:20] = CLSID
    struct.pack_into("<I", hdr, 20, 0x08)          # HasRelativePath
    struct.pack_into("<I", hdr, 0x3C, show)        # ShowCommand
    rel = (".\\" + target).encode("ascii")
    return bytes(hdr) + struct.pack("<H", len(rel)) + rel + bytes(4)


def dos_inst(m):
    """(slot, segment, I_SIZE) of the live DOS instance, or None"""
    it = m.sym("inst_tab")
    for i in range(10):
        r = m.read(it + 32 * i, 32)
        if r[0] == 1 and r[12:16] == b"DOS\0":
            return i, struct.unpack_from("<H", r, 6)[0], \
                struct.unpack_from("<H", r, 8)[0]
    return None


def claim_of(m, seg):
    """(base, bytes) of the heap claim the segment runs in"""
    raw = bytes(m.read(m.sym("mem_tab"), 32 * os88geom.MC_SIZE))
    for i in range(32):
        base, para = struct.unpack_from("<HH", raw, i * os88geom.MC_SIZE)
        if base and 0 <= seg - base < para:
            return base, para * 16
    return None, 0


def leg(apps, name, dm):
    out = {}
    with os88ui.boot(SYS, apps=apps, machine=MACHINE) as ui:
        m = ui.m
        ui.open_drive("B")
        m.disk(reset=True)
        c0 = int(m.status()["cycles"])
        ui.open(name, expect=None)      # the instance is checked by hand below
        got = []

        def up(mm):
            got[:] = [dos_inst(mm)]
            return got[0] is not None
        os88marty.until(m, up, "the DOS instance", poll=0.05, limit=40.0)
        rows = []

        def ready(mm):
            rows[:] = mm.screen() or []
            return any("READY" in r for r in rows)
        os88marty.until(m, ready, "DOSHELLO's READY", poll=0.005, limit=40.0)
        out["secs"] = (int(m.status()["cycles"]) - c0) / HZ
        out["disk"] = m.disk()
        _slot, seg, isize = dos_inst(m)
        base = seg << 4
        out["isize"] = isize
        out["claim"] = claim_of(m, seg)[1]
        out["head"] = m.read(base + dm["dos_head"], 1)[0]
        out["akb"] = struct.unpack("<H", m.read(base + dm["dos_akb"], 2))[0]
        m.key("KeyA")                   # ...and DOSHELLO exits with 42

        def ended(mm):
            st = mm.read(dos_inst(mm)[1] * 16 + dm["dos_state"], 1)[0] \
                if dos_inst(mm) else None
            return st is None or st != 1
        os88marty.pace(m, 4.0)
        os88marty.until(m, ended, "the program's end", poll=0.05, limit=20.0)
        os88marty.pace(m, 2.0)
        out["gone"] = dos_inst(m) is None
        out["toast"] = m.read(m.sym("toast_buf"), 25).split(b"\0")[0] \
            .decode("latin-1")
    return out


def main():
    for p in (SYS, PROG, P0, P1):
        if not os.path.exists(os.path.join(ROOT, p)):
            fail("%s is missing - `make && make kdostest` builds it" % p)
    if fails:
        return 1
    len0 = os.path.getsize(os.path.join(ROOT, P0))
    len1 = os.path.getsize(os.path.join(ROOT, P1))
    tmp = tempfile.mkdtemp(prefix="doshead")
    for nm, show in (("HEAD.LNK", 7), ("WIN.LNK", 1)):
        open(os.path.join(tmp, nm), "wb").write(shell_link(show, "DOSHELLO.COM"))
    apps = os.path.join(tmp, "doshead.img")
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", apps, "--size",
                    "360", PROG, os.path.join(tmp, "HEAD.LNK"),
                    os.path.join(tmp, "WIN.LNK")], cwd=ROOT, check=True,
                   capture_output=True)
    dm = dosmap.package()

    h = leg(apps, "HEAD.LNK", dm)
    print("doshead: H  headless: %.2f guest s and %d int 13h calls to READY, "
          "claim %d bytes (I_SIZE %d), arena %d KB, toast %r, gone %s"
          % (h["secs"], h["disk"]["reads"], h["claim"], h["isize"], h["akb"],
             h["toast"], h["gone"]))
    w = leg(apps, "WIN.LNK", dm)
    print("doshead: W  windowed: %.2f guest s and %d int 13h calls to READY, "
          "claim %d bytes (I_SIZE %d), arena %d KB, gone %s"
          % (w["secs"], w["disk"]["reads"], w["claim"], w["isize"], w["akb"],
             w["gone"]))

    p0 = (len0 + 15) & ~15
    if h["head"] != 1:
        fail("leg H: [dos_head] is %d - the loader did not read ShowCommand "
             "7 or did not hand the byte on (DSLH_HEAD)" % h["head"])
    if w["head"] != 0:
        fail("leg W: [dos_head] is %d for a link saved ShowCommand 1" % w["head"])
    if h["claim"] != p0:
        fail("leg H: DOS runs in a claim of %d bytes and part 0 is %d - the "
             "window is in the region after all (the loader's flip)"
             % (h["claim"], p0))
    if w["claim"] < p0 + len1:
        fail("leg W: the claim is %d bytes, short of part 0 + part 1 (%d)"
             % (w["claim"], p0 + len1))
    if h["akb"] + 1 < w["akb"] + len1 // 1024:
        fail("the headless arena is %d KB and the windowed one %d: it should "
             "be part 1's %d bytes larger" % (h["akb"], w["akb"], len1))
    if not h["gone"]:
        fail("leg H: the instance is still there after the program ended")
    if not h["toast"].startswith("Exit 42: DOSHELLO.COM"):
        fail("leg H: the toast reads %r, not `Exit 42: DOSHELLO.COM`"
             % h["toast"])
    if w["gone"]:
        fail("leg W: the windowed instance closed with its program")
    print("doshead: headless gives the program %+d KB and %+.2f s to READY "
          "(%+d int 13h calls)" % (h["akb"] - w["akb"], h["secs"] - w["secs"],
                                   h["disk"]["reads"] - w["disk"]["reads"]))
    return 1 if fails else 0


if __name__ == "__main__":
    rc = main()
    print("doshead: %s" % ("FAIL" if rc else "ok"))
    sys.exit(rc)
