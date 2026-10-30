#!/usr/bin/env python3
"""t_w16xsec - nothing in WIN16 crosses a part boundary by accident
(SPEC.md 124.19.3).

WIN16 is one nasm run that emits the image and its code parts (the library
part, `section .w16dlg`; the 8087 emulator, `.w16fp`; the loader, `.w16ld`),
each part at `vstart=0`, cut apart afterwards by tools/os88ovl.py. In
`-f bin` every section's addresses start at 0, so a near call from one
section to a label in another assembles cleanly and lands on whatever the
CALLER's segment holds at that offset. Nothing in the toolchain refuses it.
This row does, off WIN16's own listing and symbol map:

  1. a near `call`, `jmp`, `jcc` or `loop` whose target is a label in
     another section. Into the image from a part is ICALL; into the library
     part from the image is LCALL (or DCALL for a routine that ends in retf);
  2. a reference to a part's label from outside that part - an address
     handed to the image is a part offset with no segment - other than the
     DCALL/LCALL vector store, `mov word [w16_pfv]/[w16_lfn]/[w16_lbv], name`;
  3. inside a part, a memory operand naming the part's own DATA without
     `cs:`: DS is WIN16's image, so `[table]` reads the image at that offset.
     The same for an immediate address of the part's own data (`mov si,
     string`), which the code then reads through DS.

  4. a macro naming the wrong side: ICALL's routine must be in the image,
     LCALL's and DCALL's in the library part (DCONT defines its own);
  5. an export row whose part byte or handler offset is not where the map
     puts its handler: a W16LIB forgotten, or one too many.

A line may carry `XSEC-OK: <reason>` in its comment to be passed. Macro
bodies (lines naming %1) are not resolved; the four macros that carry part
calls (DCALL, ICALL, LCALL, DCONT) are written to this rule by hand.

    python3 tests/unit/t_w16xsec.py          the check
    python3 tests/unit/t_w16xsec.py --list   every cross-section reference,
                                             passed or not
"""
import glob
import hashlib
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PARTS = (".w16dlg", ".w16fp", ".w16ld")
DSELF = (".w16fp",)     # parts that run with DS = their OWN segment: the 8087
                        # emulator (w16fp.inc), so rule 3 does not apply
MACROS = ("dcall", "lcall", "icall")   # the sanctioned crossings (DCONT
DEFINES = ("dcont", "w16export", "w16fixed")   # define a label or record a
                                        #   row - checked by rule 5 instead
NEAR = re.compile(r"^(call|jmp|j[a-z]{1,4}|loop[a-z]{0,2})$")
VSTORE = re.compile(r"^\s*mov\s+word\s+\[\s*(w16_pfv|w16_lfn|w16_lbv)\s*\]\s*,")
DATA = re.compile(r"^\s*(db|dw|dd|times|resb|resw|incbin)\b", re.I)
IDENT = re.compile(r"(?<![\w.$])(\.?[A-Za-z_][\w.]*)")
LST = re.compile(r"^\s*(\d+)\s(?:\s*([0-9A-F]{8})\s+([0-9A-F\[\]()]+)-?)?\s*(?:<(\d+)>)?\s?(.*)$")


def assemble():
    """WIN16's listing and map, cached under a hash of every source file"""
    h = hashlib.sha256()
    for f in sorted(glob.glob(os.path.join(ROOT, "apps", "win16", "*.*"))
                    + glob.glob(os.path.join(ROOT, "apps", "*.inc"))):
        h.update(f.encode() + b"\0" + open(f, "rb").read())
    cache = os.path.join(ROOT, "build", "win16", "xsec-%s" % h.hexdigest()[:16])
    if not all(os.path.exists(cache + x) for x in (".lst", ".map", ".bin")):
        os.makedirs(os.path.dirname(cache), exist_ok=True)
        for old in glob.glob(os.path.join(os.path.dirname(cache), "xsec-*")):
            os.remove(old)
        with tempfile.TemporaryDirectory() as d:
            w, m, o, l = (os.path.join(d, n) for n in ("w.asm", "w.map", "w.bin", "w.lst"))
            with open(w, "w") as f:
                f.write('%%include "win16/win16.asm"\n[map symbols %s]\n' % m)
            subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                            "-I", "apps/win16/", "-o", o, "-l", l, w],
                           check=True, cwd=ROOT)
            open(cache + ".map", "w").write(open(m).read())
            open(cache + ".lst", "w").write(open(l, errors="replace").read())
            open(cache + ".bin", "wb").write(open(o, "rb").read())
    return cache + ".lst", cache + ".map", cache + ".bin"


def symbols(mapfile, where=None):
    """name -> section, for every label the map places in a section (and,
    into `where`, name -> (real file offset, virtual address))"""
    sec, out = None, {}
    for line in open(mapfile):
        m = re.match(r"^---- Section (\S+)", line)
        if m:
            sec = m.group(1)
            continue
        if line.startswith("---- No Section"):
            sec = None
            continue
        f = line.split()
        if sec and len(f) == 3 and re.fullmatch(r"[0-9A-F]+", f[0]):
            out[f[2]] = sec if sec in PARTS else ".text"
            if where is not None:
                where[f[2]] = (int(f[0], 16), int(f[1], 16))
    return out


def code_of(src):
    """the source without its comment (a ';' outside quotes)"""
    q = None
    for i, c in enumerate(src):
        if q:
            if c == q:
                q = None
        elif c in "'\"`":
            q = c
        elif c == ";":
            return src[:i], src[i + 1:]
    return src, ""


def walk(lstfile, syms):
    """(problems, crossings): every line's references, judged"""
    datalabels = set()
    sec, scope, pending = ".text", "", None
    lines, indef = [], False
    for raw in open(lstfile, errors="replace"):
        m = LST.match(raw.rstrip("\n"))
        if not m:
            continue
        src = m.group(5) or ""
        code, comment = code_of(src)
        if re.match(r"^\s*%i?macro\b", code):        # a DEFINITION's body is
            indef = True                               # text, not code: its
            continue                                   # `section` lines switch
        if re.match(r"^\s*%endmacro\b", code):        # nothing until expanded
            indef = False
            continue
        if not indef:
            lines.append((m.group(1), m.group(2), code, comment))
    # pass 1: which labels name data, with the scope their locals take
    for _, _, code, _ in lines:
        s = re.match(r"^\s*\[?section\s+(\.\w+)", code)
        if s:
            continue
        lab = re.match(r"^\s*(\.?[A-Za-z_][\w.]*)\s*:?\s*(.*)$", code)
        if lab and (code.lstrip().split()[0].endswith(":") or DATA.match(lab.group(2) or "")):
            name, rest = lab.group(1), lab.group(2)
            full = scope + name if name.startswith(".") else name
            if not name.startswith(".") and full in syms:
                scope = full
            if full in syms:
                if DATA.match(rest):
                    datalabels.add(full)
                else:
                    pending = full if not rest.strip() else None
                continue
        if pending and code.strip():
            if DATA.match(code):
                datalabels.add(pending)
            pending = None
    problems, crossings = [], []
    sec, scope, blockok = ".text", "", False
    for num, addr, code, comment in lines:
        s = re.match(r"^\s*\[?section\s+(\.\w+)", code)
        if s:
            sec = s.group(1) if s.group(1) in PARTS else ".text"   # .cold and
            continue                                              # the rest: the image
        if "%" in code or not code.strip():
            continue
        body = code
        lab = re.match(r"^\s*(\.?[A-Za-z_][\w.]*):(.*)$", code)
        if lab:
            name = lab.group(1)
            if not name.startswith(".") and name in syms:
                scope = name
            body = lab.group(2)
        ok = "XSEC-OK" in comment
        if lab and not lab.group(1).startswith("."):
            blockok = ok                     # XSEC-OK on a table's label
        elif lab:                            # covers its data lines
            blockok = blockok and ok
        if not lab and blockok and DATA.match(body):
            ok = True
        elif not lab and not DATA.match(body):
            blockok = False
        toks = body.split(None, 1)
        if not toks:
            continue
        mnem = toks[0].lower()
        if mnem in ("rep", "repe", "repz", "repne", "repnz", "lock") and len(toks) > 1:
            toks = toks[1].split(None, 1)
            mnem = toks[0].lower()
        ops = toks[1] if len(toks) > 1 else ""
        if mnem in ("equ", "%define", "%assign", "global", "extern", "align", "cpu",
                    "bits", "org", "section", "segment", "absolute", "struc", "endstruc"):
            continue
        if re.match(r"^\s*[A-Za-z_]\w*\s+equ\b", body):
            continue
        for mm in IDENT.finditer(ops):
            name = mm.group(1)
            full = scope + name if name.startswith(".") else name
            tsec = syms.get(full)
            if tsec is None:
                continue
            here = "%s:%s %s" % (num, sec, code.strip())
            inmem = "[" in ops[:mm.start()] and "]" in ops[mm.end():] and \
                ops[:mm.start()].rfind("[") > ops[:mm.start()].rfind("]")
            cs = inmem and "cs:" in ops[ops[:mm.start()].rfind("["):mm.start()]
            far = "far" in ops.split()
            why = None
            if mnem in MACROS and mm.start() == len(ops) - len(ops.lstrip()):
                want = ".text" if mnem == "icall" else ".w16dlg"
                if tsec != want:
                    why = "%s names %s, which is in %s, not %s" % (
                        mnem.upper(), full, tsec, want)
            elif NEAR.match(mnem) and not inmem and not far:
                if tsec != sec:
                    why = "a near %s into %s from %s" % (mnem, tsec, sec)
            elif tsec in PARTS and tsec != sec:
                if not VSTORE.match(body) and mnem not in MACROS + DEFINES:
                    why = "%s's label %s named from %s" % (tsec, full, sec)
            elif tsec in PARTS and tsec == sec and full in datalabels \
                    and tsec not in DSELF:
                if inmem and not cs:
                    why = "%s's own data %s read through DS" % (tsec, full)
                elif not inmem and mnem not in ("db", "dw", "dd") and not cs:
                    why = "%s's own data %s taken as a DS address" % (tsec, full)
            if tsec != sec or (tsec in PARTS and full in datalabels):
                crossings.append((here, full, tsec, why, ok))
            if why and not ok:
                problems.append("%s\n        %s" % (here, why))
    return problems, crossings


def rows(binfile, syms, where):
    """rule 5: every export row's part byte and handler offset, read out of
    the assembled table, against where the map puts its handler"""
    import struct
    names = []
    for line in open(os.path.join(ROOT, "apps", "win16", "w16api.inc")):
        if line.startswith("W16EXPORT"):
            f = [x.strip() for x in line.split(";")[0][len("W16EXPORT"):].split(",")]
            names.append(f[3])
    data = open(binfile, "rb").read()
    at = where["w16_exports"][0]
    out = []
    for k, h in enumerate(names):
        part, proc = struct.unpack_from("<BH", data, at + 7 * k + 4)
        sec = syms.get(h)
        want = {".text": 0, ".w16dlg": 3}.get(sec)
        if want is None:
            out.append("row %d (%s): its handler is in %s, which no row can name" % (k, h, sec))
        elif part & 0x7F != want or proc != where[h][1]:
            out.append("row %d (%s): the row says part %d at %04X, the map %s at %04X"
                       % (k, h, part & 0x7F, proc, sec, where[h][1]))
    return out, len(names)


def main():
    lst, mp, binf = assemble()
    where = {}
    syms = symbols(mp, where)
    problems, crossings = walk(lst, syms)
    bad, nrows = rows(binf, syms, where)
    problems += ["w16_exports\n        " + b for b in bad]
    if "--list" in sys.argv:
        for here, full, tsec, why, ok in crossings:
            print("%-5s %-60s -> %s (%s)%s" % ("ok" if not why else ("PASS" if ok else "FAIL"),
                                               here[:60], full, tsec, "  " + why if why else ""))
    nparts = sum(1 for v in syms.values() if v in PARTS)
    print("t_w16xsec: %d labels, %d in parts, %d cross-section references, %d export rows"
          % (len(syms), nparts, sum(1 for c in crossings if c[2] != c[0].split(":")[1].split()[0]),
             nrows))
    if problems:
        print("t_w16xsec: %d FAILED (SPEC.md 124.19.3):" % len(problems))
        for p in problems:
            print("  FAIL: " + p)
        return 1
    print("t_w16xsec: passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
