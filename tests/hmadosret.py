#!/usr/bin/env python3
"""hmadosret - the DOS live resume with `.cold` and drivers in the HMA
(SPEC.md 96.49.9; 96.49, docs/plans/HMA-PLAN.md 5).

    make && make build/DOSHELLO.COM build/DOSHMA.COM && python3 tests/hmadosret.py

The live resume puts a DOS trip's session back without a boot: `kd_resume`
reads HIBERNAT.IMG over conventional memory and jumps to `hbm_wake`. On a
kern_big whose `.cold` is in the HMA, that wake goes straight into code above
1MB that the image does not carry, so for a while the route was refused there
and every trip came home by `int 19h`. Now the handoff sums `.cold` and the
HMA image's code, `kd_resume` sums them again before it jumps, and a
difference (a program that wrote up there or shut A20) is `int 19h`.

THE ROUTE IS READ OFF THE BDA's MAILBOX, 0040:00F0, which is the one thing
the two routes write differently and neither image carries (the stub restores
the IVT and never file sector 2). `kd_leave` asks `kd_resume` FIRST and writes
the mailbox (`KDX1`, the code, the arena) only when it refused, and the boot's
`hb_probe` then spends the magic and leaves the code. So after a LIVE return
the mailbox is what it was before the trip, and after an `int 19h` one it
reads 0000 0000 002A. A screen cannot say which: both routes end at the same
desktop, and QEMU's clock is not one to time a POST against.

  A. DOSHELLO.COM, windowed and then Run on the whole machine (kdreturn's
     sequence; `[dos_keepc]` is set to the whole-machine arm through QEMU's
     GDB stub, which is the only way QEMU writes guest memory). Back:
     1. by the LIVE route - the mailbox untouched;
     2. `.cold` still runs from FFFF, and its bytes there are what they were;
     3. the DOS window is up with DST_RAN and exit code 42;
     4. HDD.DRV's row still names its HMA segment and C: lists HELLO.TXT -
        the pool came back out of the image (hbm_wake's step 0b);
     5. no MEM_K_HIB claim is left.
  B. DOSHMA.COM, the NEGATIVE CONTROL (docs/WRITING-TESTS.md 1), on a fresh
     boot: the same program with one word of `.cold` flipped under kern_dos
     (tests/doscom/hello.asm's HMAPOKE). Back by `int 19h` - the mailbox
     spent with 42 behind it - with the session restored, exit code 42, and
     that word of `.cold` the build's again: XMEM.DRV re-installed it.

VERIFIED RED both ways. On the kernel before SPEC.md 96.49.9, A1 fails: the
handoff wrote no wake address with `.cold` in the HMA and the mailbox reads
002A. With `kdr_hma`'s compare taken out, B fails: the live route is taken
over a damaged `.cold`.

WHY QEMU: docs/TESTING.md's closed list, entry 1 - above 1MB is a 286 place
and every MartyPC machine is an 8088. NEEDS QEMU 9 for tests/hmahiber.py's
reason: the resume stub's stack is in the text framebuffer (SPEC.md 87.5) and
QEMU 8 aborts on the first interrupt taken there - both routes run that stub.
The fixture is hmadrv's: the 1.44MB system disk with SOUND, HDD and NET
wanted, an SB16 and an IDE disk, booted from the floppy - so C:, where the
image goes, is HDD.DRV's own volume and HDD.DRV runs from the pool.
"""
import os
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import dosmap      # noqa: E402
import os88build   # noqa: E402
import os88geom    # noqa: E402
import os88qemu    # noqa: E402
import os88sym     # noqa: E402
import hmacold     # noqa: E402
import hmadrv      # noqa: E402
import vidxms      # noqa: E402
import xmcheck     # noqa: E402

S = os88sym.linear
EQ = os88sym.equates(())
B = os.path.join(ROOT, "build")
HMA = 0xFFFF0                       # linear FFFF:0000
KDB = 0x4F0                         # 0040:00F0, kd_bda's mailbox
POKE = 0x8000                       # the Makefile's -DHMAPOKE for DOSHMA.COM
MEM_K_HIB = EQ["MEM_K_HIB"]
DST_RAN = 2
u16 = hmadrv.u16


class Gdb(object):
    """QEMU's GDB stub, for the one thing QMP cannot do: WRITE guest memory.

    Connecting stops the machine, `M` writes linear bytes (real mode, no
    paging: the stub's address is the linear one), and `D` detaches, which
    resumes it. One connection a write, so nothing is held across a wait."""

    def __init__(self, port):
        self.port = port

    def _pkt(self, s, body):
        s.sendall(("$%s#%02x" % (body, sum(body.encode()) & 0xFF)).encode())
        buf = b""
        while True:
            c = s.recv(4096)
            if not c:
                raise RuntimeError("the GDB stub hung up")
            buf += c
            i = buf.find(b"$")
            j = buf.find(b"#", i + 1) if i >= 0 else -1
            if i >= 0 and j >= 0 and len(buf) >= j + 3:
                s.sendall(b"+")
                return buf[i + 1:j].decode()

    def write(self, linear, data):
        with socket.create_connection(("127.0.0.1", self.port), 10) as s:
            r = self._pkt(s, "M%x,%x:%s" % (linear, len(data), data.hex()))
            self._pkt(s, "D")
        if r != "OK":
            raise RuntimeError("GDB stub refused M%x: %r" % (linear, r))


class QM(hmacold.QMouse):
    """hmacold's QEMU pointer, with a click that settles on nothing: the
    confirmed edges are the proof, and os88mouse's own click() then waits on
    MartyPC's cycle counter, which a QEMU has not got."""

    def click(self, x, y, settle=None):
        self.to(x, y)
        self._edge(True)
        self._edge(False)


def qui(q):
    return hmacold.QUI(q, mouse=QM(q), sym=S, verbose=False)


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def boot(tmp, prog):
    sysimg, _rec, hd = hmadrv.disks(tmp)
    apps = os.path.join(tmp, "dos.img")
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", apps,
                    "--size", "1440", os.path.join(B, prog)],
                   check=True, capture_output=True)
    port = free_port()
    extra = ["-audiodev", "none,id=snd", "-device", "sb16,audiodev=snd",
             "-drive", "file=%s,format=raw,if=ide,index=0,media=disk" % hd,
             "-gdb", "tcp:127.0.0.1:%d" % port]
    q = vidxms.Q(tmp, sysimg, apps, 128, extra)
    for _ in range(150):
        if os.path.exists(q.sock):
            break
        time.sleep(0.2)
    xmcheck.wait_desktop(q.sock, "hmadosret")
    floors = (EQ["HEAP_SEG"], EQ["COLD_RAM"] + EQ["VGABUF_PARA"])
    if not os88qemu.acted(q, lambda: u16(q.read(S("mem_base"), 2)) in floors,
                          secs=120, what="the blob to be given back"):
        raise SystemExit("hmadosret: the boot never finished")
    os88qemu.pace(q, 1)
    return q, Gdb(port)


def text_page(q):
    raw = q.read(0xB8000, 4000)
    return "".join(chr(c) if 32 <= c < 127 else " " for c in raw[0::2])


def console(q, dm):
    pseg = dosmap.instance(q)
    scr = q.read((pseg << 4) + dm["con_scr"], dm["CON_SCRSZ"])
    return "".join(chr(c) if 32 <= c < 127 else " " for c in scr[0::2])


def topmem(t):
    k = t.rfind("top of block:")
    try:
        return int(t[k + 13:].split()[0]) if k >= 0 else 0
    except (ValueError, IndexError):
        return 0


def trip(q, gdb, prog, want, label):
    """Run `prog` windowed, then on the whole machine, and wait for it to
    come home. Returns (mailbox before, after) or None if it did not."""
    dm = dosmap.package()
    ui = qui(q)
    if not ui.path("B:/" + prog):
        want(False, "%s: double-clicking %s opened a window" % (label, prog))
        return None
    # IN THE OS the run is the box's console, which on a VGA is the
    # full-screen TEXT one (SPEC.md 96.10) and in a window elsewhere - so the
    # page and the box's own buffer are both asked
    def shown():
        t = text_page(q) if q.read(0x449, 1)[0] in (2, 3, 7) else ""
        return t if "READY" in t else console(q, dm)
    if not os88qemu.acted(q, lambda: "READY" in shown(), secs=60,
                          what="the windowed run's READY"):
        want(False, "%s: the in-OS run printed READY: %r"
             % (label, shown().strip()[-160:]))
        return None
    kb_in = topmem(shown())
    xmcheck.qmp(q.sock, "sendkey x")
    pseg = dosmap.instance(q)
    if not os88qemu.acted(
            q, lambda: q.read((pseg << 4) + dm["dos_state"], 1)[0] == DST_RAN,
            secs=30, what="the windowed run to end"):
        want(False, "%s: the windowed run ended" % label)
        return None
    # THE WINDOWED RUN LEFT ITS ANSWER IN THESE: zero them, so the code found
    # afterwards is the return's (tests/kdreturn.py's reason), and pick the
    # whole-machine arm, which is what Run then takes.
    pseg = dosmap.instance(q)
    gdb.write((pseg << 4) + dm["dos_exit"], bytes([0]))
    gdb.write((pseg << 4) + dm["dos_state"], bytes([0]))
    gdb.write((pseg << 4) + dm["dos_keepc"], bytes([1, 0]))
    mb0 = q.read(KDB, 8)
    ui.mo.click(*dosmap.centre(q, pseg, dm, "dos_rrect"))
    def under_kd():                 # the page READY again, with MORE memory
        if q.read(0x449, 1)[0] not in (2, 3, 7):
            return False
        t = text_page(q)
        return "READY" in t and topmem(t) > kb_in
    if not os88qemu.acted(q, under_kd, secs=180,
                          what="the run under kern_dos"):
        want(False, "%s: the program ran under kern_dos with more than the "
             "in-OS run's %d KB - the text page holds %r"
             % (label, kb_in, text_page(q).strip()[:240]))
        return None
    print("   %s: in the OS %d KB, under kern_dos %d KB"
          % (label, kb_in, topmem(text_page(q))))
    xmcheck.qmp(q.sock, "sendkey x")            # exit with 42

    def home():
        if q.read(0x449, 1)[0] != 0x12:
            return False
        try:
            titles = [w.title for w in os88geom.windows(q, S)]
            if "DOS" not in titles or q.read(S("sch_lock"), 1)[0]:
                return False
            p = dosmap.instance(q)
            return (q.read((p << 4) + dm["dos_state"], 1)[0] == DST_RAN)
        except Exception:                           # noqa: BLE001
            return False
    if not os88qemu.acted(q, home, secs=240, what="the session to come back"):
        want(False, "%s: the session came back - mode %02X, text page %r"
             % (label, q.read(0x449, 1)[0], text_page(q).strip()[:240]))
        return None
    os88qemu.pace(q, 1)
    return mb0, q.read(KDB, 8)


def common(q, want, label, hma0, row0):
    dm = dosmap.package()
    pseg = dosmap.instance(q)
    code = q.read((pseg << 4) + dm["dos_exit"], 1)[0]
    want(code == 42, "%s3. the DOS window is back with DST_RAN and exit code "
         "%d (DOSHELLO exits 42)" % (label, code))
    want(u16(q.read(S("api_coldseg"), 2)) == 0xFFFF,
         "%s2. [api_coldseg] is FFFF: `.cold` still runs from the HMA" % label)
    hma1 = {o: q.read(HMA + o, 64) for o in hma0}
    want(hma1 == hma0, "%s2. ...and FFFF:%s hold what they held before the "
         "trip" % (label, ",".join("%04X" % o for o in sorted(hma0))))
    r1 = hmadrv.rows(q)["HDD"]
    want(r1["seg"] == row0["seg"] and r1["kb"] == 0,
         "%s4. HDD.DRV's row names %04X, its HMA segment before the trip "
         "(%04X)" % (label, r1["seg"], row0["seg"]))
    ui = qui(q)                     # the DOS window is over C:'s zone
    ui.close("DOS")
    ui.open_drive("C")
    names = [n for n, _t in ui.listing()]
    ok = "HELLO.TXT" in names
    want(ok, "%s4. ...and C: lists HELLO.TXT through it: %s" % (label, names))
    hib = [c for c in hmadrv_claims(q) if c[2] == MEM_K_HIB]
    want(not hib, "%s5. no MEM_K_HIB claim is left: %s"
         % (label, ["%04X+%dp" % c[:2] for c in hib]))


def hmadrv_claims(q):
    raw = q.read(S("mem_tab"), 32 * os88geom.MC_SIZE)
    return [struct.unpack_from("<HHH", raw, i * os88geom.MC_SIZE)
            for i in range(32)
            if struct.unpack_from("<H", raw, i * os88geom.MC_SIZE)[0]]


def main():
    os.chdir(ROOT)
    fails = []

    def want(cond, what):
        print("   %s  %s" % ("ok  " if cond else "FAIL", what), flush=True)
        if not cond:
            fails.append(what)

    for need in ("os8088.img", "coldhma.sys", "mbr.bin", "boothd.bin",
                 "kernel.sys", "DOSHELLO.COM", "DOSHMA.COM", "kdos/DOS.O88"):
        if not os.path.exists(os.path.join(B, need)):
            sys.exit("hmadosret: no build/%s - run `make` and `make "
                     "build/DOSHELLO.COM build/DOSHMA.COM`" % need)
    if not os88build.qemu9():
        sys.exit("hmadosret: needs QEMU 9 (tools/setup-qemu9.sh): QEMU 8 "
                 "aborts in the resume stub (SPEC.md 87.5)")
    cold = EQ["COLD_VSTART"]
    end = cold + EQ["COLD_SIZE"] if "COLD_SIZE" in EQ else 0x10000
    if not cold <= POKE < end - 2:
        sys.exit("hmadosret: DOSHMA.COM's word FFFF:%04X is not inside "
                 "`.cold` (%04X..%04X) on this kernel - move -DHMAPOKE"
                 % (POKE, cold, end))
    offs = (cold, POKE)

    for arm, prog, live in (("A", "DOSHELLO.COM", True),
                            ("B", "DOSHMA.COM", False)):
        print("%s: %s, -m 128, `.cold` and HDD.DRV in the HMA" % (arm, prog))
        with tempfile.TemporaryDirectory(dir=B) as tmp:
            q, gdb = boot(tmp, prog)
            try:
                if u16(q.read(S("api_coldseg"), 2)) != 0xFFFF:
                    sys.exit("hmadosret: `.cold` is not in the HMA - "
                             "tests/hmacold.py's premise")
                row0 = hmadrv.rows(q)["HDD"]
                if not 0xF200 <= row0["seg"] < 0xF800:
                    sys.exit("hmadosret: HDD.DRV is not in the pool (%04X) - "
                             "tests/hmadrv.py's premise" % row0["seg"])
                hma0 = {o: q.read(HMA + o, 64) for o in offs}
                got = trip(q, gdb, prog, want, arm)
                if got:
                    mb0, mb1 = got
                    if live:
                        want(mb1 == mb0, "A1. back by the LIVE route: the "
                             "mailbox is untouched (%s, was %s)"
                             % (mb1.hex(), mb0.hex()))
                    else:
                        want(mb1[:4] == bytes(4) and u16(mb1, 4) == 42,
                             "B1. back by int 19h: the mailbox was written "
                             "and spent, 42 behind it (%s)" % mb1.hex())
                    common(q, want, arm, hma0, row0)
            finally:
                q.close()

    if fails:
        sys.exit("hmadosret: %d failed" % len(fails))
    print("hmadosret: ok")


if __name__ == "__main__":
    main()
