#!/usr/bin/env python3
"""os88romfix - what it would take to run the kernel's `.cold` out of ROM.

A MEASUREMENT, for docs/plans/ROM-PLAN.md. Nothing in `make` runs it and it
writes nothing under build/: it copies kernel/ into a temporary directory,
decouples `.cold`'s CODE segment from the ladder rung it occupies in RAM, and
assembles each kernel twice with that segment at two different values. Every
byte that differs between the pair is a place the running kernel names the
segment `.cold` executes in - a FIXUP, in the old relocating-loader sense - and
the pair is the proof that the list is complete, because anything that
depends on the segment value has to change when it changes.

It reports, per kernel:
  * the fixups per section, and whether every one is a clean 16-bit word that
    moved by exactly the difference (a non-linear diff would mean arithmetic
    on the segment that a patch table cannot express - the run refuses then);
  * the fixups INSIDE `.cold`, by symbol, because those decide whether one ROM
    image can sit at any segment or only at the one it was built for;
  * `.text` and `.cold` LZ4-packed on their own, which is roughly what a
    machine with the ROM need not read off the boot floppy.

Needs a tree `make` has already built (it takes the generated includes from
build/). Usage:

    python3 tools/os88romfix.py            # both kernels
    python3 tools/os88romfix.py --json
    python3 tools/os88romfix.py --check    # the gate: exit 1 if any fixup
                                           # lands INSIDE .cold (ROM-PLAN 3.2)
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import os88lz  # noqa: E402

# Two ROM-ish segments a long way from any RAM rung, 0x101 apart so a moved
# word cannot be confused with a moved byte.
SEG_A, SEG_B = 0xF601, 0xF702

# kernel.asm states the code segment as ONE line, apart from the rung it sits
# in (COLD_RAM), since ROM wave 2 - so moving the code and leaving the ladder
# alone is a one-line swap
LADDER_COLD = "COLD_SEG    equ COLD_HOME\n"

# The same flags the Makefile hands the shipped kernel, minus the KZ_* values
# os88kz.py supplies on the second pass - they move no `.text`/`.cold` byte.
KFLAGS = ["-DKZIP", "-DKZ_SECS=0", "-DKZ_RPARA=0", "-DKZ_NBLK=1",
          "-DKZ_HEADSEC=9"]


def fail(msg):
    sys.exit("os88romfix: " + msg)


def decouple(src):
    """The ladder keeps its RAM rung (COLD_RAM); only the CODE segment moves."""
    if LADDER_COLD not in src:
        fail("kernel.asm's ladder no longer reads the way this tool expects - "
             "look for COLD_SEG's equate and update LADDER_COLD")
    return src.replace(LADDER_COLD,
                       "%ifdef COLD_CS\nCOLD_SEG equ COLD_CS\n"
                       "%else\nCOLD_SEG equ COLD_HOME\n%endif\n", 1)


def assemble(tmp, variant, seg, mapkind):
    out = os.path.join(tmp, f"{variant}_{seg:04x}.bin")
    mapf = os.path.join(tmp, f"{variant}_{seg:04x}.map")
    asm = os.path.join(tmp, "kernel", f"_rf_{variant}_{seg:04x}.asm")
    with open(os.path.join(tmp, "kernel", "kernel.asm")) as f:
        body = f.read()
    with open(asm, "w") as f:
        f.write(f"[map {mapkind} {mapf}]\n" + body)
    cmd = ["nasm", "-f", "bin", "-w+error",
           "-I", os.path.join(tmp, "kernel") + "/",
           "-I", os.path.join(ROOT, "apps") + "/",
           "-I", os.path.join(ROOT, "build") + "/",
           f"-DKERN_{variant}", *KFLAGS, f"-DCOLD_CS=0x{seg:04X}",
           "-o", out, asm]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        fail(f"nasm failed for {variant} at {seg:#06x}:\n{r.stderr[-2000:]}")
    with open(out, "rb") as f:
        data = f.read()
    with open(mapf) as f:
        return data, f.read()


def sections(maptext):
    out = []
    for m in re.finditer(r"^\s*([0-9A-F]+)\s+([0-9A-F]+)\s+([0-9A-F]+)\s+"
                         r"[0-9A-F]+\s+progbits\s+(\S+)\s*$", maptext, re.M):
        out.append((int(m[2], 16), int(m[3], 16), m[4]))
    return out


def cold_symbols(maptext):
    t = maptext[maptext.index("-- Symbols"):]
    i = t.index("---- Section .cold")
    blk = t[i:t.index("---- Section", i + 10)]
    syms = []
    for line in blk.splitlines():
        p = line.split()
        if len(p) == 3 and re.match("^[0-9A-F]+$", p[1]):
            syms.append((int(p[1], 16), p[2]))
    return sorted(syms)


def measure(tmp, variant):
    a, amap = assemble(tmp, variant, SEG_A, "all")
    b, _ = assemble(tmp, variant, SEG_B, "sections")
    if len(a) != len(b):
        fail(f"{variant}: the two assemblies differ in LENGTH, so the segment "
             "value reaches an instruction's size - not a patch table")
    secs = sections(amap)

    def owner(off):
        for s, e, n in secs:
            if s <= off < e:
                return n
        return "?"

    delta = SEG_B - SEG_A
    per, sites, bad = {}, [], []
    i = 0
    while i < len(a):
        if a[i] == b[i]:
            i += 1
            continue
        wa = a[i] | a[i + 1] << 8 if i + 1 < len(a) else None
        wb = b[i] | b[i + 1] << 8 if i + 1 < len(b) else None
        if wa is not None and (wb - wa) & 0xFFFF == delta:
            per[owner(i)] = per.get(owner(i), 0) + 1
            sites.append(i)
            i += 2
            continue
        bad.append(i)
        i += 1
    if bad:
        fail(f"{variant}: {len(bad)} byte(s) moved by something other than the "
             f"segment delta, first at file offset {bad[0]:#x}")

    cs, ce = next((s, e) for s, e, n in secs if n == ".cold")
    syms = cold_symbols(amap)
    inside = []
    for off in sites:
        if cs <= off < ce:
            rel = off - cs
            name = max((s for s in syms if s[0] <= rel), default=(0, "?"))
            op = a[off - 3] if off >= 3 else 0
            kind = "far call" if op == 0x9A else "other"
            inside.append({"at": f"{name[1]}+{rel - name[0]:#x}",
                           "kind": kind})

    packed = {}
    for s, e, n in secs:
        if n in (".text", ".cold"):
            raw = a[s:e]
            z = os88lz.compress(raw, os88lz.LZ4, tail=False)
            packed[n] = {"bytes": len(raw), "lz4": len(z),
                         "sectors": -(-len(z) // 512)}
    return {"fixups": sum(per.values()), "per_section": per,
            "inside_cold": inside, "packed": packed}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--check", action="store_true",
                    help="exit 1 if any fixup is inside .cold: one .cold image "
                         "has to run at whatever segment it is found at")
    args = ap.parse_args()
    if not os.path.exists(os.path.join(ROOT, "build", "buildnum.inc")):
        fail("build/ has no generated includes - run `make` first")
    res = {}
    with tempfile.TemporaryDirectory(prefix="os88romfix-") as tmp:
        shutil.copytree(os.path.join(ROOT, "kernel"),
                        os.path.join(tmp, "kernel"))
        kp = os.path.join(tmp, "kernel", "kernel.asm")
        with open(kp) as f:
            src = f.read()
        with open(kp, "w") as f:
            f.write(decouple(src))
        for v in ("BIG", "SMALL"):
            res[v.lower()] = measure(tmp, v)
    if args.json:
        print(json.dumps(res, indent=2))
        return
    if args.check:
        bad = [(v, x) for v, r in res.items() for x in r["inside_cold"]]
        for v, x in bad:
            print(f"os88romfix[{v}]: .cold names its own segment at "
                  f"{x['at']} ({x['kind']}) - use COLDCALL / COLDSEG_TO "
                  f"(kernel.asm), or keep `call COLD_SEG:` only where the "
                  f"line is not .cold on this kernel", file=sys.stderr)
        if bad:
            sys.exit(f"os88romfix: {len(bad)} fixup(s) inside .cold - "
                     "docs/plans/ROM-PLAN.md 3.2")
        print("os88romfix: .cold names no segment of its own on either "
              "kernel (%s)" % ", ".join(f"{v} {r['fixups']} fixups outside"
                                        for v, r in res.items()))
        return
    for v, r in res.items():
        secs = "  ".join(f"{k} {n}" for k, n in
                         sorted(r["per_section"].items(), key=lambda x: -x[1]))
        print(f"os88romfix[{v}]: {r['fixups']} fixups, every one a clean word"
              f"  ({secs})")
        ins = ", ".join(f"{x['at']} ({x['kind']})" for x in r["inside_cold"])
        print(f"os88romfix[{v}]: inside .cold: {len(r['inside_cold'])}"
              + (f" - {ins}" if ins else ""))
        for n, p in r["packed"].items():
            print(f"os88romfix[{v}]: {n} {p['bytes']:,} bytes, "
                  f"{p['lz4']:,} packed alone = {p['sectors']} sectors")


if __name__ == "__main__":
    main()
