#!/usr/bin/env python3
"""Time a DOS Game Boy emulator on a period machine, against gbclock.

    python3 tools/gbcal.py --dos DOS.IMG --emu NO$GMB.EXE --cfg NO$GMB.CFG \\
        --box 286-525 CLOCK0.GB CLOCK25.GB SML.GB          # 86Box, a 286
    python3 tools/gbcal.py --dos DOS.IMG --emu NO$GMB.EXE --cfg NO$GMB.CFG \\
        --marty os8088_xt_vga_720b CLOCK25.GB              # MartyPC, an 8088

The calibration behind docs/plans/REALDMG-PLAN.md and
docs/reports/GAMEBOY-NOGMB-2026-10-19.md: what an existing assembly Game Boy
emulator (no$gmb's XT/AT build) gets out of the 5150 class and the 286, so
RealDMG's numbers have something to be compared with.

EVERY ROM GETS A FRESH MACHINE, booted from the DOS floppy in A: with a B:
holding the emulator, its config and the ROMs (built here with mtools). After
the date and time prompts it types `B:`, then `<EMU> <ROM>`, presses Enter
once for a start-up card, runs 70 emulated seconds, presses SPACE twice at
+20 s (no$gmb's START), and screenshots every 10 s into OUT/<ROM>_tNNN_<when>.png.
`<when>` is the machine's own clock - 86Box's emulated milliseconds, MartyPC's
guest cycles - so a speed is

    (counter at t070 - counter at t020) / (elapsed guest seconds x 59.7275)

with the counter read off gbclock's row 4 (tools/gbclock.py builds it). A
strip of the counter and the corner of each shot is written to
OUT/<ROM>_strip.png for reading; no OCR is attempted, because the emulator
under test decides where and how big the Game Boy screen is.

The DOS floppy and the emulator are the caller's: neither is in this
repository and neither may be committed. Each run's disks are COPIES.

86Box: tools/os88box.py (docs/86BOX.md). A fresh run directory starts in AMI
BIOS setup; EXIT FOR BOOT is taken automatically. MartyPC: tools/os88marty.py.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)


def make_b(emu, cfg, roms, kb, path):
    if not shutil.which("mcopy"):
        sys.exit("gbcal: needs mtools (apt-get install -y mtools)")
    if os.path.exists(path):
        os.remove(path)
    subprocess.check_call(["mformat", "-C", "-f", str(kb), "-v", "GBCAL",
                           "-i", path, "::"])
    files = [emu] + ([cfg] if cfg else []) + roms
    subprocess.check_call(["mcopy", "-i", path] + files + ["::"])


def run_box(vm, dos, bimg, emu_name, rom, out):
    import os88box
    name = "gbcal_" + os.path.basename(rom).split(".")[0]
    b = os88box.launch(vm, name=name, hold=True, fresh=True, a=dos, b=bimg)

    def text():
        try:
            r = b.cmd("text")
        except SystemExit:                 # a graphics mode
            return ""
        return "\n".join(bytes.fromhex(x).decode("cp437")
                         for x in r.get("chars", []))
    shots = []
    try:
        for _ in range(40):
            b.cmd("step 2000")
            t = text()
            if "EXIT FOR BOOT" in t:
                b.tap("return")
            if "new date" in t:
                break
        else:
            raise SystemExit("gbcal: %s never reached DOS's date prompt" % vm)
        for _ in range(2):
            b.tap("return"); b.cmd("step 1500")
        b.type("B:"); b.tap("return"); b.cmd("step 2000")
        b.type("%s %s" % (emu_name, os.path.basename(rom).upper()))
        b.tap("return"); b.cmd("step 12000")
        b.tap("return")
        for t in range(10, 80, 10):
            if t == 30:
                b.tap("space"); b.cmd("step 2000"); b.tap("space"); b.cmd("step 8000")
            else:
                b.cmd("step 10000")
            when = b.cmd("info")["ms"]
            p = os.path.join(out, "%s_t%03d_%dms.png"
                             % (os.path.basename(rom).split(".")[0], t, when))
            b.shot(p)
            shots.append(p)
            print(rom, "t%d" % t, "%d ms" % when, flush=True)
    finally:
        try:
            b.cmd("quit")
        except BaseException:
            pass
    return shots


def run_marty(machine, dos, bimg, emu_name, rom, out):
    import os88marty
    m = os88marty.launch(dos, apps=bimg, machine=machine, boot=False)
    gs = lambda s: os88marty.guest_sleep(m, s)
    shots = []
    try:
        m.run(); gs(25)
        m.key("Enter"); gs(2); m.key("Enter"); gs(2)
        m.type_text("B:\n"); gs(2)
        m.type_text("%s %s\n" % (emu_name, os.path.basename(rom).upper())); gs(40)
        m.key("Enter")
        for t in range(10, 80, 10):
            if t == 30:
                m.key("Space"); gs(2); m.key("Space"); gs(8)
            else:
                gs(10)
            c = int(m.status()["cycles"])
            p = os.path.join(out, "%s_t%03d_%dcyc.png"
                             % (os.path.basename(rom).split(".")[0], t, c))
            w, h, data = m.fbuf(0)
            os88marty.write_png_rgb(p, w, h, data)
            shots.append(p)
            print(rom, "t%d" % t, "%d cycles" % c, flush=True)
    finally:
        m.close()
    return shots


def strip(shots, out):
    """The top-left 330x90 of every shot, stacked: the emulator's own meter
    and gbclock's counter, for reading."""
    import zlib, struct
    from os88marty import write_png_rgb
    rows = []
    for p in shots:
        w, h, px = read_png_rgb(p)
        for y in range(min(90, h)):
            rows.append(px[y * w * 3:(y * w + min(330, w)) * 3].ljust(990, b"\0"))
        rows.append(b"\xff\0\0" * 330)
    write_png_rgb(out, 330, len(rows), b"".join(rows))


def read_png_rgb(path):
    import zlib, struct
    d = open(path, "rb").read()
    i, idat = 8, b""
    while i < len(d):
        n = struct.unpack(">I", d[i:i + 4])[0]
        t, body = d[i + 4:i + 8], d[i + 8:i + 8 + n]
        i += 12 + n
        if t == b"IHDR":
            w, h, _, ct = struct.unpack(">IIBB", body[:10])
        elif t == b"IDAT":
            idat += body
    bpp = {2: 3, 6: 4}[ct]
    raw, st, prev, out = zlib.decompress(idat), w * bpp, bytearray(w * bpp), []
    for y in range(h):
        f = raw[y * (st + 1)]
        ln = bytearray(raw[y * (st + 1) + 1:(y + 1) * (st + 1)])
        for x in range(st):
            a = ln[x - bpp] if x >= bpp else 0
            b, c = prev[x], (prev[x - bpp] if x >= bpp else 0)
            if f == 1: ln[x] = (ln[x] + a) & 255
            elif f == 2: ln[x] = (ln[x] + b) & 255
            elif f == 3: ln[x] = (ln[x] + (a + b) // 2) & 255
            elif f == 4:
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                ln[x] = (ln[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out.append(bytes(ln[k] for x in range(w) for k in range(x * bpp, x * bpp + 3)))
        prev = ln
    return w, h, b"".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--dos", required=True, help="a bootable DOS floppy image (yours)")
    ap.add_argument("--emu", required=True, help="the DOS emulator's .EXE")
    ap.add_argument("--cfg", help="its settings file, copied beside it")
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--box", metavar="VM", help="an 86Box vm/ profile (a 286: 286-525)")
    g.add_argument("--marty", metavar="MACHINE", help="a MartyPC machine with a 720KB B:")
    ap.add_argument("--out", default="build/gbcal")
    ap.add_argument("roms", nargs="+")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="gbcal-")
    try:
        bimg = os.path.join(tmp, "b.img")
        make_b(a.emu, a.cfg, a.roms, 1200 if a.box else 720, bimg)
        emu_name = os.path.basename(a.emu).upper()
        for rom in a.roms:
            shots = (run_box(a.box, a.dos, bimg, emu_name, rom, a.out) if a.box
                     else run_marty(a.marty, a.dos, bimg, emu_name, rom, a.out))
            s = os.path.join(a.out, os.path.basename(rom).split(".")[0] + "_strip.png")
            strip(shots, s)
            print("strip:", s)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    main()
