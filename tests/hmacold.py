#!/usr/bin/env python3
"""hmacold - kern_big's `.cold` runs from the HMA (docs/plans/HMA-PLAN.md).

On a 286 or better with memory above 1MB, XMEM.DRV verifies A20, claims the
HMA (SPEC.md 41.3) and reads COLDHMA.SYS - the ROM's adapter as a file,
boot/osrom.asm kind 3, cut from this kernel by `tools/os88rom.py hma`. That
copies `.cold` to FFFF:0010, re-points every far reference to it, and
mem_unblob then hands the dead cold rung to the heap: the same 38KB the 5150's
ROM gives (tests/rombig.py), on any AT, with nothing soldered.

WHY QEMU: docs/TESTING.md's closed list, entry 1. Above 1MB is a 286-and-up
place and every MartyPC machine is an 8088. Nothing here is timed.

Three boots of the shipped 1.44MB system disk (QEMU's VGA), A/B/C:

  A. -m 128. [api_coldseg] = FFFF; the 38,6xx bytes at FFFF:COLD_VSTART -
     the top of the HMA, the drivers' pool under them (tests/hmadrv.py) - are
     kernel.bin's `.cold`; [mem_base] = COLD_RAM + VGABUF_PARA - the floor
     fell by the whole cold rung, the planar decoder's buffers moved under
     it, and the decoder's segment word says COLD_RAM; rom_mfp names
     FFFF; and the Control Panel opens, its CTRL.DRV image read off the disk
     AFTER the move with every far reference re-pointed at FFFF by the
     HMA's own copy of rom_modfix. Nothing about a panel opening proves the
     last part - a module still naming the old rung would open too, until
     the heap reused it - so the words themselves are read.
  B. -m 1, no memory above 1MB: no XMEM.DRV, [api_coldseg] = COLD_HOME and
     [mem_base] = HEAP_SEG. The negative control (docs/WRITING-TESTS.md 1):
     without it, A's numbers would say nothing about where they came from.
  C. -m 128 with COLDHMA.SYS one byte away from this kernel's (its `.text`
     hash): refused, and exactly B - a wrong image is no image.

    make && python3 tests/hmacold.py
"""
import os
import shutil
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import os88mouse   # noqa: E402
import os88qemu    # noqa: E402
import os88rom     # noqa: E402
import os88sym     # noqa: E402
import os88ui      # noqa: E402
import vidxms      # noqa: E402
import xmcheck     # noqa: E402

SYS = os.path.join(ROOT, "build", "os8088.img")
APPS = os.path.join(ROOT, "build", "apps.img")
HMA = 0xFFFF
S = os88sym.linear
EQ = os88sym.equates(())


def u16(b, i=0):
    return b[i] | b[i + 1] << 8


class QMouse(os88mouse.Mouse):
    """os88mouse's pointer, READ the same way (the 'MO' block) and MOVED
    through QMP's absolute device (tools/mouse.py), which is what QEMU has.
    Both moves are confirmed off the guest's own cursor, as os88mouse's are."""

    def __init__(self, q):
        super().__init__(marty=q)
        self.q = q

    def to(self, x, y, l=False, **_kw):
        # A PACKET LOST IS A POSITION WRONG: the msmouse line runs at 1200
        # baud and tools/mouse.py's walk is relative, so a dropped packet
        # leaves the pointer short - which the read-back sees, and a second
        # walk (it pins at the clamp first) repairs. `l` is the level a drag
        # carries, and QMP's button state is sticky, so it needs nothing here
        for _ in range(4):
            xmcheck.goto(self.q.sock, x, y)
            if os88qemu.acted(self.q, lambda: self.where()[:2] == (x, y),
                              secs=3, what="the pointer at %d,%d" % (x, y)):
                return
        raise os88ui.UIError("the pointer did not reach %d,%d (at %r)"
                             % (x, y, self.where()))

    def dblclick(self, x, y, **_kw):
        """Two clicks over QMP with nothing between them: the msmouse line
        carries each edge as a packet, so the pair lands well inside the
        kernel's double-click window. Confirmed by whatever the caller waits
        on next - a window, as os88ui's open does."""
        self.to(x, y)
        for down in (1, 0, 1, 0):
            xmcheck.qmp(self.q.sock, "mouse_button %d" % down)

    def _edge(self, down, **_kw):
        xmcheck.qmp(self.q.sock, "mouse_button %d" % (1 if down else 0))
        want = 1 if down else 0
        if not os88qemu.acted(self.q, lambda: (self.where()[2] & 1) == want,
                              secs=5, what="button %s" % want):
            raise os88ui.UIError("the guest never saw the button %s"
                                 % ("press" if down else "release"))


class QUI(os88ui.UI):
    """os88ui's verbs over QEMU: every wait on the GUEST's tick count
    (tests/os88qemu.py) where os88ui's own is on MartyPC's cycle counter."""

    def _wait(self, cond, what, guest=None, snapshot=None):
        if os88qemu.acted(self.m, cond, secs=guest or 10, what=what,
                          poll=0.05):
            return True
        extra = ""
        if snapshot is not None:
            try:
                extra = "\n  what the guest shows: %s" % (snapshot(),)
            except Exception as se:                     # noqa: BLE001
                extra = "\n  (could not read the guest: %s)" % (se,)
        raise os88ui.UIError("timed out waiting for %s%s" % (what, extra))


def boot(tmp, mem, sysimg, panel=True, sites=(), vg=()):
    apps = os.path.join(tmp, "apps.img")
    shutil.copy(APPS, apps)
    q = vidxms.Q(tmp, sysimg, apps, mem)
    for _ in range(150):
        if os.path.exists(q.sock):
            break
        time.sleep(0.2)
    xmcheck.wait_desktop(q.sock, "hmacold")
    # ...AND THE BOOT FINISHED: drive B's zone exists from desk_init, which is
    # before drv_boot and long before xm_boot's attach, so the zone alone can
    # answer while `.cold` is still where stage 2 put it. [mem_base] is raised
    # over the blob until mem_unblob_x, the line after spl_finish, puts it on
    # the floor - one of exactly two floors, by where `.cold` ended up
    floors = (EQ["HEAP_SEG"], EQ["COLD_RAM"] + EQ["VGABUF_PARA"])
    if not os88qemu.acted(q, lambda: u16(q.read(S("mem_base"), 2)) in floors,
                          secs=120, what="the blob to be given back"):
        raise SystemExit("hmacold: the boot never finished (-m %d): "
                         "[mem_base] = %04X" % (mem, u16(q.read(S("mem_base"),
                                                                2))))
    os88qemu.pace(q, 1)
    mfp = q.read(S("rom_mfp"), 4)
    out = {"cs": u16(q.read(S("api_coldseg"), 2)),
           "base": u16(q.read(S("mem_base"), 2)),
           "mfp": (u16(mfp, 2), u16(mfp)),
           "hma": q.read(0xFFFF0 + EQ["COLD_VSTART"], 64),
           "vgab": [u16(q.read((os88sym.KERNEL_SEG << 4) + o, 2)) for o in vg]}
    if panel:
        ui = QUI(q, mouse=QMouse(q), sym=S, verbose=False)
        ui.menu_pick("Apple", "Control Panel")
        out["panel"] = ui.wait_window("Control Panel") is not None
        seg = u16(q.read(S("mod_tab") + EQ["MOD_CTRL"] * EQ["MODR_SIZE"], 2))
        out["ctrlseg"] = seg
        out["ctrlsites"] = [u16(q.readseg(seg, o, 2)) for o in sites] \
            if seg else []
    q.close()
    return out


def main():
    cold_home, cold_ram = EQ["COLD_HOME"], EQ["COLD_RAM"]
    heap, vgap = EQ["HEAP_SEG"], EQ["VGABUF_PARA"]
    fails = []

    def want(cond, what):
        print("   %s  %s" % ("ok  " if cond else "FAIL", what))
        if not cond:
            fails.append(what)

    _img, at, t = os88rom.build_hma(os.path.join(ROOT, "build"), "KERN_BIG")
    sites = t["lists"]["mods"].get(EQ["MOD_CTRL"], [])
    vg = t["lists"]["vgab"]
    cold = bytes(t["cold"])
    hma = open(os.path.join(ROOT, "build", "coldhma.sys"), "rb").read()
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        good = os.path.join(tmp, "sys.img")
        shutil.copy(SYS, good)

        print("A: -m 128")
        a = boot(tmp, 128, good, sites=sites, vg=vg)
        want(a["cs"] == HMA, "[api_coldseg] = %04X, `.cold` runs from the "
             "HMA" % a["cs"])
        want(a["hma"] == cold[:64], "FFFF:%04X holds kernel.bin's `.cold` - "
             "the top of the HMA (SPEC.md 2.10.8)" % EQ["COLD_VSTART"])
        want(a["base"] == cold_ram + vgap, "[mem_base] = %04X: the cold rung "
             "back, the decoder's buffers kept under it (%04X; %04X without)"
             % (a["base"], cold_ram + vgap, heap))
        want(a["mfp"][0] == HMA, "rom_mfp = %04X:%04X" % a["mfp"])
        want(vg and a["vgab"] == [cold_ram] * len(vg), "the decoder's "
             "segment word = %s (COLD_RAM %04X)"
             % (["%04X" % w for w in a["vgab"]], cold_ram))
        want(a.get("panel"), "the Control Panel opened")
        cs = a.get("ctrlsites", [])
        want(cs and all(w == HMA for w in cs), "CTRL.DRV at %04X: all %d of "
             "its far references say FFFF" % (a.get("ctrlseg", 0), len(cs)))

        print("B: -m 1")
        b = boot(tmp, 1, good, panel=False)
        want(b["cs"] == cold_home, "[api_coldseg] = %04X (COLD_HOME)"
             % b["cs"])
        want(b["base"] == heap, "[mem_base] = %04X (HEAP_SEG)" % b["base"])

        print("C: -m 128, COLDHMA.SYS one byte away")
        img = bytearray(open(SYS, "rb").read())
        pos = img.find(hma)
        if pos < 0:
            sys.exit("hmacold: build/coldhma.sys is not on %s - rebuild" % SYS)
        hsh = b"\x81\xFA" + t["hash"].to_bytes(2, "little")   # cmp dx, RT_HASH
        h = hma.find(hsh)
        if h < 0:
            sys.exit("hmacold: no `cmp dx, RT_HASH` in the HMA image")
        img[pos + h + 2] ^= 0x01
        bad = os.path.join(tmp, "bad.img")
        with open(bad, "wb") as f:
            f.write(img)
        c = boot(tmp, 128, bad, panel=False)
        want(c["cs"] == cold_home, "[api_coldseg] = %04X - refused" % c["cs"])
        want(c["base"] == heap, "[mem_base] = %04X" % c["base"])

    if fails:
        sys.exit("hmacold: %d failed" % len(fails))
    print("hmacold: ok")


if __name__ == "__main__":
    main()
