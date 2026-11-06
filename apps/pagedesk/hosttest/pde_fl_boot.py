#!/usr/bin/env python3
"""os8088 - apps/pagedesk/hosttest/pde_fl_boot.py

PDENG.MOD's FlateDecode on the TARGET toolchain, run for real in QEMU (a 386-class
CPU: it proves SmallerC's 16-bit code and pdmath.inc compute what the host build
computes, not that they are fast and not what a real 8088 does; the module is held
to 8086 instructions by cc8086.py and nasm cpu 8086). It reuses pde_boot.py's
image building and boot, with pde_fl_boot.c as the harness: a PDF whose objects
10, 11 and 12 are Flate streams (plain text, a PNG-predicted gray image, a
corrupted copy of 10), the CRCs the module delivers at three read sizes and
nested, compared with what this script knows they must be.

  usage: pde_fl_boot.py BUILDDIR [--icount]      (BUILDDIR holds PDENG.MOD)
  --icount runs QEMU with -icount shift=0, so the BIOS tick counts guest
  instructions (1 ns each), and prints instructions a decoded byte.
"""
import os, random, subprocess, sys, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(HERE, "pde_boot.py")).read()
src = src.replace('"pde_boot.c"', '"pde_fl_boot.c"').replace("\nmain()\n", "\n")
ns = {"__file__": os.path.join(HERE, "pde_boot.py"), "__name__": "pde_boot"}
exec(compile(src, "pde_boot.py", "exec"), ns)

sys.path.insert(0, HERE)
import pde_fl_check as fc      # (its main() is guarded)


def crc16(data, crc=0xFFFF):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


PIT_ASM = """
; unsigned pdb_pit(void) - PIT channel 0's down-counter, latched
_pdb_pit:
    xor al, al
    out 0x43, al
    in al, 0x40
    mov ah, al
    in al, 0x40
    xchg al, ah
    ret

"""


def big_text(rng, n):
    words = [b"Page", b"Desk", b"the", b"quick", b"brown", b"fox", b"jumps", b"over", b"lazy", b"dog", b"flate", b"stream", b"of", b"text"]
    out = bytearray()
    while len(out) < n:
        ws = b" ".join(rng.choice(words) for _ in range(rng.randrange(3, 9)))
        out += b"BT /F%d %d Tf %d %d Td (%s) Tj ET\n" % (rng.randrange(1, 4), rng.choice((9, 10, 12)), rng.randrange(20, 560), rng.randrange(20, 760), ws)
    return bytes(out[:n])


def make_pdf():
    rng = random.Random(5)
    text = (b"BT /F1 12 Tf 72 700 Td (Hello, PageDesk) Tj ET\n" * 30 + b"q 1 0 0 1 10 10 cm 0 0 100 100 re f Q\n" * 10 +
            bytes(rng.randrange(256) for _ in range(400)))
    cols, rows = 60, 24
    raw = bytes(rng.randrange(256) if rng.random() < .5 else (i * 3) & 255 for i in range(cols * rows))
    z10 = fc.zl(text)
    z11 = fc.zl(fc.png_encode(raw, cols, 1, (1, 2, 3, 4, 0)))
    bad = bytearray(z10); bad[len(bad) // 2] ^= 0x10; bad = bytes(bad)
    big = big_text(rng, 240000)
    rawt = big_text(rng, 80000)
    icols, irows = 200, 600
    img = bytes(((x // 4 + y // 3) & 255) ^ (rng.randrange(4) if rng.random() < .3 else 0) for y in range(irows) for x in range(icols))
    pdf = fc.build_pdf([(b"/Filter /FlateDecode", z10),
                        (b"/Filter /FlateDecode /DecodeParms << /Predictor 15 /Columns %d >>" % cols, z11),
                        (b"/Filter /FlateDecode", bad),
                        (b"/Filter /FlateDecode", fc.zl(big)), (b"", rawt),
                        (b"/Filter /FlateDecode /DecodeParms << /Predictor 15 /Columns %d >>" % icols,
                         fc.zl(fc.png_encode(img, icols, 1, (1, 2, 4, 3, 0))))])
    return pdf, text, raw, bad, {13: big, 14: rawt, 15: img}


def build_harness(build, root):
    # pde_boot.py's build_harness, with the assembly taken from the build tree
    raw, gen = os.path.join(build, "pde_boot.raw.asm"), os.path.join(build, "pde_boot.gen.asm")
    cc = os.path.join(root, "build", "cc", "SmallerC")
    env = dict(os.environ, PATH=cc + os.pathsep + os.environ["PATH"])
    inc = os.path.join(cc, "v0100", "include")
    sh = ns["sh"]
    sh(os.path.join(cc, "smlrcc"), "-tiny", "-S", "-SI", inc, "-I", inc, "-I", os.path.join(root, "apps", "pagedesk"),
       os.path.join(HERE, "pde_fl_boot.c"), "-o", raw, env=env)
    f = os.path.join(HERE, "pde_fl_boot.i")
    if os.path.exists(f):
        os.remove(f)
    sh(sys.executable, os.path.join(root, "tools", "cc8086.py"), raw, "-o", gen, "--max-frame", "96")
    hb, kb = os.path.join(build, "pde_boot.bin"), os.path.join(build, "pde_kstub.bin")
    sh("nasm", "-f", "bin", "-w+error", "-I", os.path.join(root, "apps") + "/", "-I", build + "/", "-DM_SEG=%d" % ns["M_SEG"], "-o", hb,
       os.path.join(build, "pde_fl_boot.asm"))
    sh("nasm", "-f", "bin", "-w+error", "-o", kb, os.path.join(HERE, "pde_kstub.asm"))
    return open(hb, "rb").read(), bytearray(open(kb, "rb").read())


def expect(z):
    d = zlib.decompressobj(-15)
    out = bytearray()
    try:
        for i in range(2, len(z)):
            out += d.decompress(z[i:i + 1])
    except zlib.error:
        return bytes(out), 1
    return bytes(out), 0 if d.eof else 3


def main():
    build = sys.argv[1]
    icount = "--icount" in sys.argv
    root = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
    os.makedirs(build, exist_ok=True)
    pdf, text, raw, bad, bigs = make_pdf()
    # the harness is pde_boot.asm plus pdb_pit, built from a copy in the build tree
    asm = open(os.path.join(HERE, "pde_boot.asm")).read().replace("section .data\nh_door", PIT_ASM + "section .data\nh_door", 1)
    open(os.path.join(build, "pde_fl_boot.asm"), "w").write(asm)
    harness, kstub = build_harness(build, root)
    module = open(os.path.join(build, "PDENG.MOD"), "rb").read()
    img = ns["make_image"](build, harness, kstub, module, pdf, 2)
    if icount:
        orig = ns["subprocess"].Popen
        def popen(cmd, *a, **k):
            return orig(cmd + ["-icount", "shift=0,sleep=off"], *a, **k)
        ns["subprocess"].Popen = popen
    out = ns["boot"](img)
    bad_n = 0
    if "DONE\n" not in out:
        print("FAIL: no DONE\n" + out[-600:])
        sys.exit(1)
    want_bad, want_rc = expect(bad)
    lines = out.split("\n")
    seen = 0
    timing = {}
    for l in lines:
        f = l.split()
        if not f:
            continue
        tag = f[0]
        kv = dict(x.split("=") for x in f if "=" in x)
        if tag == "READ":
            obj = int(f[1])
            data, rc = {10: (text, 0), 11: (raw, 0), 12: (want_bad, want_rc)}[obj]
            ok = int(kv["rc"]) == rc
            n = int(kv["bytes"].split(":")[0]) * 65536 + int(kv["bytes"].split(":")[1])
            if obj != 12:
                ok = ok and n == len(data) and int(kv["crc"], 16) == crc16(data)
            seen += 1
            if not ok:
                bad_n += 1
                print("FAIL", l, "want rc", rc, "bytes", len(data), "crc %04x" % crc16(data))
            else:
                print("ok  ", l)
        elif tag == "TIME":
            obj = int(f[1])
            data = bigs[obj]
            n = int(kv["bytes"].split(":")[0]) * 65536 + int(kv["bytes"].split(":")[1])
            ok = kv["rc"] == "0" and n == len(data) and int(kv["crc"], 16) == crc16(data)
            seen += 1
            units = int(kv["ticks"]) * 65536 + int(kv["pit0"]) - int(kv["pit1"])
            if not ok:
                bad_n += 1
                print("FAIL", l, "want bytes", len(data), "crc %04x" % crc16(data))
            else:
                timing[obj] = (units * 838, n)       # 838 ns a PIT count; under -icount shift=0 that is instructions
                print("ok  ", l)
        elif tag in ("NESTA", "NESTB"):
            data = text if tag == "NESTA" else raw
            ok = kv["rc"] == "0" and int(kv["bytes"].split(":")[1]) == len(data) and int(kv["crc"], 16) == crc16(data)
            seen += 1
            if not ok:
                bad_n += 1
                print("FAIL", l, "want bytes", len(data), "crc %04x" % crc16(data))
            else:
                print("ok  ", l)
    if icount:
        for obj, what in ((13, 'Flate text'), (14, 'unfiltered text'), (15, 'Flate + PNG predictor, gray image')):
            if obj in timing:
                ns_, n = timing[obj]
                print('instructions per output byte, %-34s %6.1f   (%d bytes)' % (what, ns_ / n, n))
    if seen != 14:
        bad_n += 1
        print("FAIL: %d result lines, expected 14\n%s" % (seen, out[-600:]))
    sys.exit(1 if bad_n else 0)


main()
