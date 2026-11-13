#!/usr/bin/env python3
"""os8088 - apps/pagedesk/hosttest/pde_boot.py

Runs PDENG.MOD for real: builds a bootable floppy that carries the module, a
kernel stub (READ_SEQ with its contract enforced, FILE_FIND, DFREE) and a PDF,
boots it in QEMU with no display, reads the harness's serial output and compares
it with `pde_test dump` on the host, line for line. QEMU is a 386-class CPU, so
this proves the module computes the same answers as the host build (16-bit int,
SmallerC's code, pdmath.inc) - not that it is fast, and not what a real 8088
does (the module is held to 8086 instructions by cc8086.py and nasm cpu 8086).

usage: pde_boot.py PDE_TEST BUILDDIR PDF... [-c SECTORS ...]
  BUILDDIR holds PDENG.MOD, pde_boot.gen.asm's inputs are rebuilt here.
The PDF must fit in 345,600 bytes (the 640 KiB map above the module).
"""
import os, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
H_SEG, K_SEG, M_SEG, P_SEG = 0x0100, 0x0060, 0x2C00, 0x3C00
P_MAX = (0x9000 - P_SEG) * 16
QEMU = "qemu-system-i386"


def sh(*a, **k):
    p = subprocess.run(a, capture_output=True, text=True, **k)
    if p.returncode:
        raise SystemExit("%s failed:\n%s%s" % (" ".join(a), p.stdout, p.stderr))
    return p


def sectors(b):
    return (len(b) + 511) // 512


def build_harness(build, root):
    """Compile pde_boot.c with the target toolchain, assemble the harness and the stub."""
    raw, gen = os.path.join(build, "pde_boot.raw.asm"), os.path.join(build, "pde_boot.gen.asm")
    cc = os.path.join(root, "build", "cc", "SmallerC")
    env = dict(os.environ, PATH=cc + os.pathsep + os.environ["PATH"])
    inc = os.path.join(cc, "v0100", "include")
    sh(os.path.join(cc, "smlrcc"), "-tiny", "-S", "-SI", inc, "-I", inc, "-I", os.path.join(root, "apps", "pagedesk"),
       os.path.join(HERE, "pde_boot.c"), "-o", raw, env=env)
    for f in (os.path.join(HERE, "pde_boot.i"),):
        if os.path.exists(f):
            os.remove(f)
    sh(sys.executable, os.path.join(root, "tools", "cc8086.py"), raw, "-o", gen, "--max-frame", "96")
    hb, kb = os.path.join(build, "pde_boot.bin"), os.path.join(build, "pde_kstub.bin")
    sh("nasm", "-f", "bin", "-w+error", "-I", os.path.join(root, "apps") + "/", "-I", build + "/", "-DM_SEG=%d" % M_SEG, "-o", hb,
       os.path.join(HERE, "pde_boot.asm"))
    sh("nasm", "-f", "bin", "-w+error", "-o", kb, os.path.join(HERE, "pde_kstub.asm"))
    return open(hb, "rb").read(), bytearray(open(kb, "rb").read())


def make_image(build, harness, kstub, module, pdf, cluster):
    k = bytearray(kstub)
    k[0x500:0x502] = (len(pdf) & 0xFFFF).to_bytes(2, "little")
    k[0x502:0x504] = (len(pdf) >> 16).to_bytes(2, "little")
    k[0x504:0x506] = cluster.to_bytes(2, "little")
    k[0x508:0x50A] = P_SEG.to_bytes(2, "little")
    parts = [(H_SEG, harness), (K_SEG, bytes(k)), (M_SEG, module), (P_SEG, pdf)]
    lba, defs = 1, []
    for name, (seg, data) in zip("HKMP", parts):
        defs += ["-D%s_SEG=%d" % (name, seg), "-D%s_LBA=%d" % (name, lba), "-D%s_SECT=%d" % (name, sectors(data))]
        lba += sectors(data)
    boot = os.path.join(build, "pde_bootsec.bin")
    sh("nasm", "-f", "bin", "-w+error", *defs, "-o", boot, os.path.join(HERE, "pde_bootsec.asm"))
    img = bytearray(open(boot, "rb").read())
    for seg, data in parts:
        img += data + bytes(sectors(data) * 512 - len(data))
    img += bytes(1474560 - len(img))
    path = os.path.join(build, "pde_boot.img")
    open(path, "wb").write(img)
    return path


def boot(img, timeout=180):         # 180: n_hexws_bomb is 1.6x slower since sget lost its recursion (verify-flate2)
    q = subprocess.Popen([QEMU, "-display", "none", "-monitor", "none", "-serial", "stdio", "-m", "16", "-no-reboot", "-drive",
                          "file=%s,format=raw,if=floppy" % img, "-boot", "a"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    out, t0 = b"", time.time()
    try:
        os.set_blocking(q.stdout.fileno(), False)
        while time.time() - t0 < timeout:
            try:
                d = q.stdout.read(65536)
            except BlockingIOError:
                d = None
            if d:
                out += d
            if b"DONE\n" in out or b"!CONTRACT" in out:
                break
            time.sleep(0.02)
    finally:
        q.terminate()
        try:
            q.wait(5)
        except subprocess.TimeoutExpired:
            q.kill()
    return out.decode("latin-1").replace("\r\n", "\n")


def main():
    args = sys.argv[1:]
    pde, build = args[0], args[1]
    clusters, pdfs, i = [], [], 2
    while i < len(args):
        if args[i] == "-c":
            clusters.append(int(args[i + 1])); i += 2
        else:
            pdfs.append(args[i]); i += 1
    clusters = clusters or [2]
    root = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
    os.makedirs(build, exist_ok=True)
    harness, kstub = build_harness(build, root)
    module = open(os.path.join(build, "PDENG.MOD"), "rb").read()
    bad = 0
    for pdf_path in pdfs:
        pdf = open(pdf_path, "rb").read()
        if len(pdf) > P_MAX:
            print("skip %s: %d bytes does not fit the harness's %d" % (os.path.basename(pdf_path), len(pdf), P_MAX))
            continue
        for c in clusters:
            img = make_image(build, harness, kstub, module, pdf, c)
            tgt = boot(img)
            host = subprocess.run([pde, "dump", pdf_path, "-c", str(c)], capture_output=True).stdout.decode("latin-1")
            if "DONE\n" not in tgt:
                print("FAIL %s c=%d: no DONE\n%s" % (os.path.basename(pdf_path), c, tgt[-400:]))
                bad += 1
                continue
            tl = tgt.replace("DONE\n", "").rstrip("\n").split("\n")
            hl = host.rstrip("\n").split("\n")
            if tl != hl:
                bad += 1
                print("FAIL %s c=%d: target and host differ" % (os.path.basename(pdf_path), c))
                n = 0
                for a, b in zip(tl, hl):
                    if a != b:
                        print("  target: %s\n  host:   %s" % (a, b))
                        n += 1
                        if n >= 4:
                            break
                if len(tl) != len(hl):
                    print("  %d target lines, %d host lines" % (len(tl), len(hl)))
            else:
                print("ok   %-16s c=%d  %d lines identical on the target" % (os.path.basename(pdf_path), c, len(tl)))
    sys.exit(1 if bad else 0)


main()
