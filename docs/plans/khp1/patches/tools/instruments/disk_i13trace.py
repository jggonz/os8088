"""Every int 13h of a boot, from reset to kmain (or further), with registers."""
import os, sys
ROOT = os.getcwd(); sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty, os88sym
img = sys.argv[1]; apps = sys.argv[2] if len(sys.argv) > 2 and sys.argv[2] != "-" else None
machine = sys.argv[3] if len(sys.argv) > 3 else "os8088_5150_cga_gla"
until = sys.argv[4] if len(sys.argv) > 4 else "kmain"
defs = ("KERN_BIG",)
target = 0x600 + os88sym.syms(defs)[until] if until == "kmain" else None
with os88marty.launch(img, apps=apps, machine=machine, boot=False) as m:
    m.bp_exec(0x7C00); m.run(); assert m.wait_stop(200) is not None
    vec = m.read(0x13 * 4, 4)
    rom13 = (int.from_bytes(vec[2:4], "little") << 4) + int.from_bytes(vec[0:2], "little")
    n = 0; prev = None
    while True:
        m.breakpoints([{"type": "exec", "addr": rom13}, {"type": "exec", "addr": target}])
        m.run(); assert m.wait_stop(300) is not None
        st = m.status()
        if st["flat_ip"] == target: break
        r = m.regs()
        ah = r["ax"] >> 8; al = r["ax"] & 255
        cyl = (r["cx"] >> 8) | ((r["cx"] & 0xC0) << 2); sec = r["cx"] & 63; hd = r["dx"] >> 8
        phys = (r["es"] << 4) + r["bx"]
        c = st["cycles"]
        print("%3d cyc %9d (+%8d) AH=%02X AL=%3d C/H/S %2d/%d/%2d drv %02X dest %05X%s" % (
            n, c, c - (prev or c), ah, al, cyl, hd, sec, r["dx"] & 255, phys,
            "  CROSSES 64K" if ah in (2, 3) and (phys & 0xFFFF) + al * 512 > 0x10000 else ""))
        prev = c; n += 1
