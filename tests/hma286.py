#!/usr/bin/env python3
"""hma286 - XMEM.DRV and the HMA's pool on a REAL 286, under 86Box.

    make && python3 tests/hma286.py [--arm nomix|mixer|both] [--keep]

What QEMU cannot show about SPEC.md 41.12.6, 41.13 and 51.1.3.1, because its
CPUs are 386-class and its one Sound Blaster has a mixer. 86Box's 286 runs
LOADALL (0F 05) and its "Sound Blaster v2.0" has no mixer unless asked for
one, so vm/286-hma - the owner's mr286 board, 16 MHz, 4MB, an SB 2.0 at
220h/5/1 and an IDE disk - boots hmadrv's fixture: the 1.44MB system disk
with a SYSTEM.CFG wanting SOUND.DRV, HDD.DRV and NET.DRV, RECORDER.O88 in B:,
and HELLO.TXT on C:. Per arm (nomix: the SB 2.0; mixer: an SB Pro 2, whose
CT1345 is a mixer):

  1. `.cold` runs from the HMA, XMEM.DRV is up in a 2KB claim, and its
     LOADALL transport VERIFIED at attach on this CPU - [xm_lall] 1, so
     xm_lainit's two round trips through apps/os88la.inc's borrow came back
     word for word against the BIOS's AH=87h;
  2. the pool's table: SOUND.DRV at its foot, cut to snd_keep (nomix) or
     snd_keepmix (mixer) by its own MEMHMA_KEEP call, HDD.DRV and NET.DRV at
     its top, and one free run between;
  3. RECORDER's demo played on the card: the 8KB double buffer in the POOL
     on the SB 2.0 (the free run holds it) and in the heap on the SB Pro,
     both halves the demo's samples, the card playing the stream out - its
     8237 reading from above 1MB - and the block given back with the stream;
  4. the fault log charges nothing to the 286's run (os88box `faults`).

The reload's order - a hibernate's sweep, the Drivers page - is
tests/hmapool.py's, against the same tail under unicorn; this row is about
what only the CPU, the BIOS and the card can say.

Broken on purpose: OS88_DRV_FOOT taken out of sound.asm puts SOUND.DRV at
the pool's top and fails 2 on both arms, and its MEMHMA_KEEP call taken out
leaves 11,952 bytes standing and the buffer in the heap, failing 2 and 3 on
nomix.

Needs a built 86Box (python3 tools/os88box.py build) and its ROM set.
"""
import argparse
import os
import shutil
import struct
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import os88box      # noqa: E402
import os88geom     # noqa: E402
import os88mouse    # noqa: E402
import os88sym      # noqa: E402
import os88ui       # noqa: E402
import hmadrv       # noqa: E402
import recorder     # noqa: E402
from cycweb import pkg_syms  # noqa: E402

S = os88sym.linear
EQ = os88sym.equates(())
B = os.path.join(ROOT, "build")
ORG = EQ["DRV_HMA_ORG"]
HB_LO, HB_HI = 0x0010, EQ["HMA_COLD_AT"]
DRVR_SIZE = EQ["DRVR_SIZE"]
ARMS = {"nomix": "sb2.0", "mixer": "sbprov2"}
FAILS = []


def u16(b, i=0):
    return b[i] | b[i + 1] << 8


def want(cond, what):
    print("   %s  %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        FAILS.append(what)


# --- os88ui over 86Box -------------------------------------------------------
class BoxM(object):
    """What os88ui, os88mouse and os88geom read through: guest memory"""

    def __init__(self, b):
        self.b = b

    def read(self, addr, n):
        return bytes(self.b.memr(addr, n))

    def readseg(self, seg, off, n):
        return self.read((seg << 4) + off, n)


class BoxMouse(os88mouse.Mouse):
    """The pointer read as os88mouse reads it (the 'MO' block) and moved by
    os88box's closed loop on the kernel's own mouse_x/mouse_y"""

    def __init__(self, b):
        super().__init__(marty=BoxM(b))
        self.b = b

    def to(self, x, y, l=False, **_kw):
        got = self.b.to(x, y, buttons=1 if l else 0)
        if tuple(got) != (x, y):
            raise os88ui.UIError("the pointer did not reach %d,%d (at %r)"
                                 % (x, y, got))

    def dblclick(self, x, y, **_kw):
        self.to(x, y)
        self.b.click(2)

    def _edge(self, down, **_kw):
        self.b.mouse(0, 0, 1 if down else 0)
        self.b.until(lambda: (self.where()[2] & 1) == (1 if down else 0),
                     "the button %s" % ("press" if down else "release"),
                     guest_ms=3000)


class BoxUI(os88ui.UI):
    """os88ui's verbs, every wait on 86Box's emulated clock"""

    def _wait(self, cond, what, guest=None, snapshot=None):
        if self.b.until(cond, what, guest_ms=int((guest or 10) * 1000),
                        need=False) is not None:
            return True
        raise os88ui.UIError("timed out waiting for %s" % what)


def ui_for(b):
    ui = BoxUI(BoxM(b), mouse=BoxMouse(b), sym=S, verbose=False)
    ui.b = b
    return ui


# --- the machine -------------------------------------------------------------
def mr_setup(b):
    """MR BIOS 1.65 from a fresh CMOS: both floppies 3.5" 1.44MB
    (docs/86BOX.md; tests/megacdrom.py's sequence)"""
    def row(r):
        return bytes.fromhex(b.cmd("text")["chars"][r]).decode("cp437")
    b.wait_text("Press Any Key", 20000)
    b.tap("return"); b.cmd("step 1500")
    for _ in range(4):
        b.tap("right"); b.cmd("step 300")
    b.tap("down"); b.cmd("step 300")
    for r in (10, 11):
        n = 0
        while "1.4" not in row(r) and n < 8:
            b.tap("space"); b.cmd("step 300"); n += 1
        b.tap("down"); b.cmd("step 300")
    b.tap("esc"); b.cmd("step 500")
    b.tap("f10")


def rows(b):
    tab = b.memr(S("drv_tab"), DRVR_SIZE * 8)
    out = {}
    for name, r, _f in hmadrv.DRVS:
        rec = tab[r * DRVR_SIZE:(r + 1) * DRVR_SIZE]
        out[name] = {"seg": u16(rec, EQ["DRVR_SEG"]),
                     "kb": u16(rec, EQ["DRVR_KB"])}
    return out


def blocks(b):
    hma = open(os.path.join(B, "coldhma.sys"), "rb").read()
    t = b.memr(0xFFFF0 + u16(hma, 12), 8 * 6)
    return sorted((u16(t, k), u16(t, k + 2), u16(t, k + 4))
                  for k in range(0, len(t), 6) if u16(t, k))


def boot(arm, tmp, keep):
    sysimg, apps, hd = hmadrv.disks(tmp)
    b = os88box.launch("286-hma", name="hma286-" + arm, hold=True, fresh=True,
                       a=sysimg, b=apps, c=hd, temp=not keep,
                       sets=("Sound.sndcard=" + ARMS[arm],))
    mr_setup(b)
    b.kernel_up(guest_ms=120000)
    return b


def play(b, snd_seg, buffer_hma):
    """RECORDER's demo, played on the card (hmadrv.record_play's steps, on
    86Box's held clock): the buffer where the pool says, the samples in it,
    the stream played out by the card's DMA and IRQs, the block given back"""
    ui = ui_for(b)
    m = BoxM(b)
    syms, _ = pkg_syms("apps/recorder/recorder.asm")
    sb = hmadrv.drv_syms("sound")

    def drv(name, n=1):
        return b.memr((snd_seg << 4) + sb[name], n)
    ui.path("B:/RECORDER.O88")
    seg = None
    for w in os88geom.windows(m, S):
        if w.title == "Recorder":
            seg = u16(m.read(os88geom.winptr(m, w.i, S) + os88geom.W_SEG, 2))
    if not seg:
        want(False, "3. RECORDER.O88 opened a window")
        return

    def msg():
        at = u16(m.readseg(seg, syms["rc_msg"], 2))
        return m.readseg(seg, at, 32).split(b"\0")[0].decode("latin-1")

    def bar():
        try:
            return any(c[0] == "Sound" for c in ui.menus())
        except Exception:                           # noqa: BLE001
            return False
    b.until(bar, "Recorder's Sound menu", guest_ms=10000)
    ui.menu_pick("Sound", "Demo")
    b.until(lambda: msg().startswith("DEMO STAGED"), "the demo staged",
            guest_ms=20000)
    under0 = u16(drv("sbl_under", 2))
    ui.menu_pick("Sound", "Play")
    b.until(lambda: msg().startswith("PLAYING") and drv("sbl_str_act")[0],
            "the stream", guest_ms=5000, need=False)
    want(msg().startswith("PLAYING") and drv("sbl_str_act")[0] == 1,
         "3. Play opened a stream on the card (%r)" % msg())
    sbase = u16(drv("sbl_sbase", 2))
    page = drv("sbl_page")[0]
    blk = [x for x in blocks(b) if x[2] == snd_seg and x[1] == 8192]
    if buffer_hma:
        want(len(blk) == 1 and sbase == ORG and page == 0x10,
             "3. the 8KB double buffer is a pool block: %s, [sbl_sbase] "
             "%04X, 8237 page %02X" % (["%04X+%d" % x[:2] for x in blk],
                                       sbase, page))
    else:
        want(not blk and sbase == 0 and page < 0x0A,
             "3. the 8KB double buffer is in the heap: [sbl_sbase] %04X, "
             "8237 page %02X" % (sbase, page))
    sw = recorder.sweep()
    half = u16(drv("sbl_half", 2))

    def locate(chunk):                  # hmadrv's: the last half is the
        p = sw.find(chunk)              # stream's tail, then silence
        if p >= 0:
            return p
        p = len(sw) - len(sw) % half if half else 0
        n = len(sw) - p
        if half and 0 < n < half and chunk[:n] == sw[p:] \
                and chunk[n:] == b"\x80" * (half - n):
            return p
        return -1
    at = (u16(drv("sbl_seg", 2)) << 4) + sbase       # (held: one snapshot)
    pos = [locate(bytes(b.memr(at + k * half, half))) for k in (0, 1)]
    want(half and all(p >= 0 and p % half == 0 for p in pos)
         and (pos[1] - pos[0]) % (2 * half) == half,
         "3. its two halves are the demo's samples at %s, in order" % pos)
    done = b.until(lambda: drv("sbl_str_state")[0] != 0
                   and u16(drv("sbl_under", 2)) != under0,
                   "the stream's last half", guest_ms=30000, need=False)
    want(done is not None,
         "3. the card played it out: state %d, %d underrun(s)"
         % (drv("sbl_str_state")[0], u16(drv("sbl_under", 2)) - under0))
    ui.menu_pick("Sound", "Stop")
    b.until(lambda: drv("sbl_str_act")[0] == 0, "the stream closed",
            guest_ms=10000, need=False)
    blk = [x for x in blocks(b) if x[2] == snd_seg and x[1] == 8192]
    want(not blk and u16(drv("sbl_seg", 2)) == 0,
         "3. ...and the block went back with the stream")


def faults(run):
    """Every exception at a CS that is not the BIOS's own F000 - the only
    ones a healthy run has are its AH=87h's way back out of protected mode.
    Strict on purpose: the HMA's drivers run at F401-F9FF"""
    out = []
    for line in open(os.path.join(run, "stderr.log"), errors="replace"):
        f = os88box.FAULT.search(line)
        if f and f.group(2) != "F000":
            out.append(line.strip())
    return out


def arm_run(arm, keep):
    print("hma286: %s - vm/286-hma with an %s" % (arm, ARMS[arm]), flush=True)
    import xmhwalk
    with tempfile.TemporaryDirectory() as t:
        _img, xs = xmhwalk.xmem_syms(t)
    sb = hmadrv.drv_syms("sound")
    keepb = (sb["snd_keep" if arm == "nomix" else "snd_keepmix"] - ORG + 15) \
        & ~15
    tmp = tempfile.mkdtemp(dir=B)
    b = None
    try:
        b = boot(arm, tmp, keep)
        want(u16(b.memr(S("api_coldseg"), 2)) == 0xFFFF,
             "1. `.cold` runs from the HMA")
        row = S("xm_row")
        xseg = u16(b.memr(row + EQ["DRVR_SEG"], 2))
        xkb = u16(b.memr(row + EQ["DRVR_KB"], 2))
        lall = b.memr((xseg << 4) + xs["xm_lall"], 1)[0]
        want(xseg and xkb == 2 and lall == 1,
             "1. XMEM.DRV at %04X in %d KB, its LOADALL transport %s on "
             "this 286" % (xseg, xkb, "VERIFIED" if lall == 1 else
                           "refused ([xm_lall] %d)" % lall))
        r = rows(b)
        blk = blocks(b)
        snd = r["SOUND"]["seg"]
        own = {x[2]: x for x in blk}
        sblk = own.get(snd)
        want(sblk is not None and sblk[0] == HB_LO and sblk[1] == keepb,
             "2. SOUND.DRV at the pool's foot, cut to %d: %s"
             % (keepb, None if sblk is None else "%04X+%d" % sblk[:2]))
        tops = [own.get(r[n]["seg"]) for n in ("HDD", "NET")]
        want(all(tops) and max(x[0] + x[1] for x in tops) == HB_HI
             and min(x[0] for x in tops) + sum(x[1] for x in tops)
             == HB_HI,
             "2. HDD.DRV and NET.DRV at its top, back to back: %s"
             % [None if x is None else "%04X+%d" % x[:2] for x in tops])
        free = (min(x[0] for x in tops if x) - (HB_LO + keepb)) \
            if all(tops) else 0
        print("      one free run of %d bytes between" % free)
        play(b, snd, free >= 8192)
        if arm == "nomix":
            want(free >= 8192, "3. (the SB 2.0's run holds the buffer)")
        f = faults(b.run)
        want(not f, "4. no exception outside the BIOS: %d%s"
             % (len(f), "".join("\n        " + x for x in f[:4])))
    except (SystemExit, os88ui.UIError) as e:
        want(False, "the run stopped: %s" % e)
    finally:
        if b is not None and not keep:
            b.quit(remove=True)
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--arm", choices=("nomix", "mixer", "both"),
                    default="both")
    ap.add_argument("--keep", action="store_true",
                    help="leave the machine running (hma286-<arm>)")
    a = ap.parse_args()
    for f in ("os8088.img", "coldhma.sys", "recorder.o88", "mbr.bin",
              "boothd.bin", "kernel.sys", "sound.hma.map", "xmem.bin"):
        if not os.path.exists(os.path.join(B, f)):
            sys.exit("hma286: no build/%s - run `make`" % f)
    for arm in (("nomix", "mixer") if a.arm == "both" else (a.arm,)):
        arm_run(arm, a.keep)
    if FAILS:
        print("hma286: %d FAILED" % len(FAILS))
        return 1
    print("hma286: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
