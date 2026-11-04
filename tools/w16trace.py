#!/usr/bin/env python3
"""w16trace - read WIN16's call ring out of a running QEMU (SPEC.md 124.17.4).

    make BUILD=build-tr W16DEFS=-DW16_TRACE build-tr/win16.o88      (real mode)
    make BUILD=build-tr KERN_MEGA=1 W16DEFS=-DW16_TRACE \\
         build-tr/megak/apps/win16.o88                              (kern_mega)
    python3 tools/w16trace.py [--bin build-tr/megak/apps/win16.bin] \\
                              [--mega | --real] SOCK PROGRAM.EXE

A `-DW16_TRACE` build of WIN16 records the last W16_TRN things a program
asked for (64 on kern_mega, 256 in real mode, unless -DW16_TRN=n) in a ring in
its bss: every export through the gate as its BIND SLOT, and every int 21h as
80h:AH. This tool names a slot from the slot's own entry in w16_bind - the
part and the handler's offset in it, matched against the symbol map - so a
missing import is named too, out of w16_mtxt ("USER.287"). It answers the
question a program that "just stops" leaves - what did it ask for last, and
what does it keep asking for? - which is how CLOCK was found spinning on
int 21h 2Ch.

The symbols are the ARM'S: kern_mega's bss is laid out differently, so the map
is assembled with -DPKG_MEGA -DDOS_MEGA for it. --bin names the win16.bin that
was booted; the re-assembly must match it byte for byte, and that is also how
the arm is chosen. Without --bin (and without --mega/--real) both arms are
tried against memory. W16_DEFINES adds defines (W16_TRN=128, W16_TRFIRST), as
the build's W16DEFS did. The instance is found by the program's name in
w16_name, anywhere in the first 16MB (kern_mega's claims can be above 1MB).
"""
import argparse
import os
import re
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tests"), os.path.join(ROOT, "tools")]
os.chdir(ROOT)

MEGA = ("PKG_MEGA", "DOS_MEGA")
PARTSECT = {".text": 0, ".w16dlg": 3, ".w16sys": 9, ".w16tsk": 10, ".w16rare": 11}  # W16P_*


def defines(mega):
    d = ["W16_TRACE"] + os.environ.get("W16_DEFINES", "").split()
    return list(dict.fromkeys(d + (list(MEGA) if mega else [])))


def assemble(mega):
    """(bytes, {label: (section, offset)}, {constant: value}) for one arm"""
    with tempfile.TemporaryDirectory() as d:
        b, m = os.path.join(d, "w.bin"), os.path.join(d, "w.map")
        r = subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                            "-I", "apps/win16/", "-I", "apps/dos/",
                            "--before", "[map all %s]" % m]
                           + ["-D" + x for x in defines(mega)]
                           + ["-o", b, "apps/win16/win16.asm"],
                           capture_output=True, text=True)
        if r.returncode:
            sys.exit("w16trace: WIN16 (%s) would not assemble with %s:\n%s"
                     % ("kern_mega" if mega else "real mode", defines(mega),
                        (r.stderr or r.stdout)[-600:]))
        labels, consts, sect = {}, {}, None
        for line in open(m):
            s = re.match(r"---- Section (\S+)", line)
            if s:
                sect = s.group(1)
                continue
            f = line.split()
            hexs = all(all(c in "0123456789ABCDEF" for c in x) for x in f[:-1])
            if len(f) == 3 and hexs:
                labels[f[2]] = (sect, int(f[1], 16))     # VIRTUAL: part-relative
            elif len(f) == 2 and hexs:
                consts[f[1]] = int(f[0], 16)
        return open(b, "rb").read(), labels, consts


def handlers(labels):
    """{(part, offset): [export rows' text]} out of w16api.inc's rows"""
    out = {}
    for line in open("apps/win16/w16api.inc"):
        m = re.match(r"W16EXPORT W16M_(\w+),\s*(\d+),\s*-?\d+,\s*(\w+)\s*(;.*)?", line)
        if not m or m.group(3) not in labels:
            continue
        sect, off = labels[m.group(3)]
        if sect not in PARTSECT:
            continue
        text = "%s.%s %s" % (m.group(1), m.group(2),
                             (m.group(4) or ";" + m.group(3))[1:].strip())
        out.setdefault((PARTSECT[sect], off), []).append(text)
    for name, (sect, off) in labels.items():    # the fixed slots' handlers,
        if sect in PARTSECT and "." not in name:  # which no row names
            out.setdefault((PARTSECT[sect], off), ["(%s)" % name])
    return out


def memory(sock):
    """the first 16MB, or as much as answers, through QMP pmemsave"""
    import xmcheck
    data = b""
    with tempfile.TemporaryDirectory() as d:
        for base, size in ((0, 0xA0000), (0xA0000, 0x60000),
                           (0x100000, 0xF00000)):
            p = os.path.join(d, "m%X" % base)
            try:
                xmcheck.qmp_exec(sock, "pmemsave",
                                 {"val": base, "size": size, "filename": p})
                data += open(p, "rb").read()
            except SystemExit:
                break
    return data


def find(mem, name, S, C):
    """linear bases of live WIN16 instances running `name`: the name where
    this arm's w16_name is, a live w16_state (not a stale copy) and a bind
    count the arm can hold - which is also what tells the arms apart"""
    want, out, i = name.encode() + b"\0", [], -1
    end = max(S[k][1] for k in ("w16_trc", "w16_bind", "w16_mtxt")) + 0x1000
    while True:
        i = mem.find(want, i + 1)
        if i < 0:
            return out
        base = i - S["w16_name"][1]
        if base < 0 or base + end > len(mem):
            continue
        nb = struct.unpack_from("<H", mem, base + S["w16_nbind"][1])[0]
        if 1 <= mem[base + S["w16_state"][1]] <= 4 \
                and C["W16_NFIXED"] <= nb <= C["W16_NBIND"]:
            out.append(base)


def report(mem, base, S, C, H):
    ring = C.get("W16_TRN")
    n = struct.unpack_from("<H", mem, base + S["w16_trn"][1])[0]
    nb = struct.unpack_from("<H", mem, base + S["w16_nbind"][1])[0]
    print("WIN16 at linear %06X: %d calls, %d bind slots, a ring of %d"
          % (base, n, nb, ring))
    tr = struct.unpack_from("<%dH" % ring, mem, base + S["w16_trc"][1])
    bind, mtxt = base + S["w16_bind"][1], base + S["w16_mtxt"][1]
    first = "W16_TRFIRST" in os.environ.get("W16_DEFINES", "")
    for i in (range(min(n, ring)) if first else range(max(0, n - ring), n)):
        r = tr[i & (ring - 1)]
        if r & 0xFF00 == 0x8000:
            what = "int 21h AH=%02Xh" % (r & 0xFF)
        elif r < C["W16_NBIND"]:
            argb, part, proc = struct.unpack_from("<BBH", mem, bind + 4 * r)
            if part & 0x7F == C["W16B_MISS"]:
                t = mem[mtxt + proc * C["W16_MTXT"]:][:C["W16_MTXT"]]
                what = "MISSING %s" % t.split(b"\0")[0].decode("latin-1")
            else:
                rows = H.get((part & 0x0F, proc))
                what = " = ".join(rows) if rows else \
                    "? part %d:%04Xh" % (part & 0x0F, proc)
            what = "[%3d] %s" % (r, what)
        else:
            what = "? %04Xh" % r
        print("%6d  %s" % (i, what))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("sock")
    ap.add_argument("program")
    ap.add_argument("--bin", help="the win16.bin that was booted (checked)")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--mega", action="store_true")
    g.add_argument("--real", action="store_true")
    a = ap.parse_args()
    arms = [True] if a.mega else [False] if a.real else [False, True]
    built = {m: assemble(m) for m in arms}
    if a.bin:
        shipped = open(a.bin, "rb").read()
        arms = [m for m in arms if built[m][0] == shipped]
        if not arms:
            sys.exit("w16trace: %s is not a W16_TRACE build of this source with "
                     "W16_DEFINES=%r on any arm tried - rebuild it, or pass the "
                     "build's W16DEFS here" % (a.bin, os.environ.get("W16_DEFINES", "")))
    mem = memory(a.sock)
    name = a.program.upper()
    for m in arms:
        _, S, C = built[m]
        hits = find(mem, name, S, C)
        if hits:
            print("(the %s arm's symbols)" % ("kern_mega" if m else "real-mode"))
            H = handlers(S)
            for base in hits:
                report(mem, base, S, C, H)
            return
    sys.exit("w16trace: no WIN16 instance running %s (arms tried: %s)"
             % (name, ", ".join("kern_mega" if m else "real" for m in arms)))


if __name__ == "__main__":
    main()
