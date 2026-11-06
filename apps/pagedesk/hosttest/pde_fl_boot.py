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
import os, random, re, subprocess, sys, zlib

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
    # the corpus: every block type, far distances, then (from object 100) the malformed ones
    cs = []
    t2 = text[:2000]
    far = bytes(rng.randrange(256) for _ in range(32768))
    valid = [("L6", fc.zl(t2, 6)), ("L1", fc.zl(t2, 1)), ("L9", fc.zl(t2, 9)), ("stored", fc.zl(t2, 0)),
             ("fixed", fc.zl(t2, 6, zlib.Z_FIXED)), ("huff", fc.zl(t2, 6, zlib.Z_HUFFMAN_ONLY)),
             ("rle", fc.zl(t2, 6, zlib.Z_RLE)), ("zeros", fc.zl(bytes(40000))), ("w9", fc.zl(t2, 6, wbits=9))]
    vdata = [t2] * 7 + [bytes(40000), t2]
    far2 = far + far[:3000] + far[100:200]
    valid.append(("far", fc.zl(far2, 9))); vdata.append(far2)
    allb = bytes(range(256)) * 20
    valid.append(("allbytes", fc.zl(allb))); vdata.append(allb)
    low = bytes(rng.randrange(4) for _ in range(30000))
    valid.append(("lowent", fc.zl(low))); vdata.append(low)
    c = zlib.compressobj(6); part = b""
    for i in range(0, len(t2), 300):
        part += c.compress(t2[i:i + 300]) + c.flush(zlib.Z_FULL_FLUSH)
    valid.append(("flushed", part + c.flush())); vdata.append(t2)
    streams = [(b"/Filter /FlateDecode", z) for _, z in valid]
    pad = 100 - 10 - 6 - len(streams)
    mal = []
    base = fc.zl(t2[:600], 6)
    for k in range(20):
        b = bytearray(base)
        for _ in range(rng.choice((1, 1, 2, 4))):
            b[rng.randrange(len(b))] ^= 1 << rng.randrange(8)
        mal.append(bytes(b))
    fixed = fc.zl(t2[:300], 6, zlib.Z_FIXED)
    for k in range(8):
        b = bytearray(fixed); b[rng.randrange(len(b))] ^= 1 << rng.randrange(8); mal.append(bytes(b))
    for cut in (3, 10, 40, 90, 200, len(base) - 10):
        mal.append(base[:cut])
    mal.append(bytes([0x78, 0x9C, 0x07])); mal.append(bytes([0x78, 0x9C, 0x01, 5, 0, 0, 0, 1, 2, 3, 4, 5]))
    pdf = fc.build_pdf([(b"/Filter /FlateDecode", z10),
                        (b"/Filter /FlateDecode /DecodeParms << /Predictor 15 /Columns %d >>" % cols, z11),
                        (b"/Filter /FlateDecode", bad),
                        (b"/Filter /FlateDecode", fc.zl(big)), (b"", rawt),
                        (b"/Filter /FlateDecode /DecodeParms << /Predictor 15 /Columns %d >>" % icols,
                         fc.zl(fc.png_encode(img, icols, 1, (1, 2, 4, 3, 0))))] + streams +
                       [(None, b"null")] * pad + [(b"/Filter /FlateDecode", m) for m in mal])
    corpus = {16 + i: (vdata[i], 0) for i in range(len(valid))}
    for i, m in enumerate(mal):
        corpus[100 + i] = expect(m)
    return pdf, text, raw, bad, {13: big, 14: rawt, 15: img}, corpus


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
    out, how, _ = fc.partial_zlib(z)
    return out, {"done": 0, "trunc": 3, "error": 1}[how]


def run_boot(build, module, pdf, icount):
    harness, kstub = build_harness(build, root_dir())
    img = ns["make_image"](build, harness, kstub, module, pdf, 2)
    if icount:
        orig = ns["subprocess"].Popen
        def popen(cmd, *a, **k):
            return orig(cmd + ["-icount", "shift=0,sleep=off"], *a, **k)
        ns["subprocess"].Popen = popen
    out = ns["boot"](img)
    if icount:
        ns["subprocess"].Popen = orig
    return out


def root_dir():
    return os.path.abspath(os.path.join(HERE, "..", "..", ".."))


def judge(tag, out, want, bigs, corpus):
    """-> (problems, results {key: line without ticks}, timing, first)"""
    bad_n, res, timing, first, seen = [], {}, {}, {}, 0
    if "DONE\n" not in out:
        return ["%s: no DONE\n%s" % (tag, out[-500:])], res, timing, first
    text, raw, want_bad, want_rc = want
    for l in out.split("\n"):
        f = l.split()
        if not f:
            continue
        kv = dict(x.split("=") for x in f if "=" in x)
        n = (int(kv["bytes"].split(":")[0]) * 65536 + int(kv["bytes"].split(":")[1])) if "bytes" in kv else 0
        crc = int(kv["crc"], 16) if "crc" in kv else 0
        t = f[0]
        if t == "READ":
            obj = int(f[1])
            data, rc = {10: (text, 0), 11: (raw, 0), 12: (want_bad, want_rc)}[obj]
            ok = int(kv["rc"]) == rc and (obj == 12 or (n == len(data) and crc == crc16(data)))
            res[("READ", obj, f[2])] = (kv["rc"], n, crc)
        elif t in ("NESTA", "NESTB"):
            data = text if t == "NESTA" else raw
            ok = kv["rc"] == "0" and n == len(data) and crc == crc16(data)
            res[(t,)] = (kv["rc"], n, crc)
        elif t == "FIRST":
            ok = kv["got"] == "1"
            first[int(f[1])] = ((int(kv["pit0"]) - int(kv["pit1"])) % 65536) * 838
        elif t == "TIME":
            obj = int(f[1])
            data = bigs[obj]
            ok = kv["rc"] == "0" and n == len(data) and crc == crc16(data)
            timing[obj] = ((int(kv["ticks"]) * 65536 + int(kv["pit0"]) - int(kv["pit1"])) * 838, n)
        elif t == "CORP":
            obj = int(f[1])
            data, rc = corpus[obj]
            ok = int(kv["rc"]) == rc and n == len(data) and crc == crc16(data)
            res[("CORP", obj)] = (kv["rc"], n, crc)
        else:
            continue
        seen += 1
        if not ok:
            bad_n.append("%s: %s" % (tag, l))
    if seen != 16 + len(corpus):
        bad_n.append("%s: %d result lines, expected %d" % (tag, seen, 16 + len(corpus)))
    return bad_n, res, timing, first


def main():
    build = sys.argv[1]
    icount = "--icount" in sys.argv
    os.makedirs(build, exist_ok=True)
    pdf, text, raw, bad, bigs, corpus = make_pdf()
    want = (text, raw) + expect(bad)

    mods = {"asm": open(os.path.join(build, "PDENG.MOD"), "rb").read(),
            "c": open(os.path.join(build, "flc", "PDENG.MOD"), "rb").read()}
    # the harness is pde_boot.asm plus pdb_pit, built from a copy in the build tree
    asmtxt = open(os.path.join(HERE, "pde_boot.asm")).read().replace("section .data\nh_door", PIT_ASM + "section .data\nh_door", 1)
    open(os.path.join(build, "pde_fl_boot.asm"), "w").write(asmtxt)
    problems, results = [], {}
    for tag, mod in mods.items():
        out = run_boot(build, mod, pdf, icount)
        p, res, timing, first = judge(tag, out, want, bigs, corpus)
        problems += p
        results[tag] = res
        print("%-4s module: %d result lines, %d problems" % (tag, len(res), len(p)))
        if icount:
            for obj, what in ((13, "Flate text"), (14, "unfiltered text"), (15, "Flate + PNG predictor, gray image")):
                if obj in timing:
                    print("  instructions per output byte, %-34s %6.1f   (%d bytes)" % (what, timing[obj][0] / timing[obj][1], timing[obj][1]))
            for obj, ins in sorted(first.items()):
                print("  first byte of object %d: %d instructions" % (obj, ins))
    # the asm and the C twin must agree line for line, including on the malformed corpus
    diff = [k for k in results["c"] if results["c"][k] != results["asm"].get(k)]
    print("asm against its C twin: %d of %d results differ" % (len(diff), len(results["c"])))
    problems += ["asm and C differ at %r: %r vs %r" % (k, results["asm"].get(k), results["c"][k]) for k in diff]
    # NEGATIVE CONTROL: break one instruction of the asm (the literal store) and the comparison must bite
    lst = open(os.path.join(build, "PDENG.lst")).read().split("\n")
    start = next(i for i, l in enumerate(lst) if l.rstrip().endswith("_fl_codes:"))
    off = None
    for l in lst[start:start + 120]:
        m = re.match(r"\s*\d+\s+([0-9A-F]{8})\s+8805\s", l)
        if m:
            off = int(m.group(1), 16)
            break
    broken = bytearray(mods["asm"])
    assert broken[off:off + 2] == b"\x88\x05", "listing and image disagree"
    broken[off + 1] = 0x25                                  # mov [di], al  ->  mov [di], ah
    out = run_boot(build, bytes(broken), pdf, False)
    p, res, _, _ = judge("broken", out, want, bigs, corpus)
    dd = [k for k in results["c"] if results["c"][k] != res.get(k)]
    print("negative control (the literal store broken at image offset 0x%X): %d expected-value problems, %d differences from the C twin"
          % (off, len(p), len(dd)))
    if not p or not dd:
        problems.append("NEGATIVE CONTROL DID NOT BITE: the comparison cannot see a broken asm")
    for q in problems:
        print("FAIL", q)
    print("FAILURES: %d" % len(problems))
    sys.exit(1 if problems else 0)


main()
