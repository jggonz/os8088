#!/usr/bin/env python3
"""
os8088 - tools/os88pdabi.py        PageDesk's two ABI mirrors, held to each other

apps/pagedesk/pdabi.h is the C side of the PageDesk contract (PAGEDESK-SPEC 3-4)
and apps/pagedesk/pdabi.inc its assembly side.  pdabi.inc is GENERATED from the
header; --check is what every `make` runs, and it fails with a precise message
when the two disagree:

    os88pdabi.py --write   regenerate apps/pagedesk/pdabi.inc from pdabi.h
    os88pdabi.py --check   parse both, compare every name and value, and also
                           * that each `pdabi_sz_*` pinned size equals the size
                             computed here (C alignment rules, 16-bit pdw/pdi),
                           * that no struct holds an implicit padding hole,
                           * that pdmath.inc's mirrored constants (PDMI_*) equal
                             the header's, and
                           * that pdmath.inc defines a `_name:` label for every
                             function pdmath.h declares (outside PDM_SKIP_*)
    os88pdabi.py --dump    print what the header means (names, offsets, sizes)

Names in pdabi.inc:
    <NAME> equ <value>                      every integer #define
    PDXB_<STRUCT>_<FIELD> equ <offset>      every struct field; STRUCT is the tag
                                            without a leading `pdxb_` / `pd_`
    PDXB_<STRUCT>_BYTES equ <bytes>      (not _SIZE: pdxb_open has a field `size`)
    PDXB_<STRUCT>_<FIELD>_N equ <count>     arrays: the outer count
    PDXB_<STRUCT>_<FIELD>_ESZ equ <bytes>   arrays: bytes per outer element
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
H = os.path.join(ROOT, "apps/pagedesk/pdabi.h")
INC = os.path.join(ROOT, "apps/pagedesk/pdabi.inc")
MATH_H = os.path.join(ROOT, "apps/pagedesk/pdmath.h")
MATH_INC = os.path.join(ROOT, "apps/pagedesk/pdmath.inc")

# scalar types: name -> (size, align)
SCALARS = {"pdw": (2, 2), "pdi": (2, 2), "pdb": (1, 1), "char": (1, 1),
           "pd_pt": (2, 2), "pd_px": (2, 2)}


def strip_comments(t):
    t = re.sub(r"/\*.*?\*/", " ", t, flags=re.S)
    return re.sub(r"//[^\n]*", " ", t)


def parse_int(s):
    s = s.strip()
    return int(s, 16) if s.lower().startswith("0x") else int(s)


LITERALS = {}


def parse_header(path=H):
    raw = open(path).read()
    t = strip_comments(raw)
    defines = {}
    for m in re.finditer(r"^[ \t]*#define[ \t]+([A-Za-z_]\w*)[ \t]+(0[xX][0-9A-Fa-f]+|\d+)[ \t]*$", t, flags=re.M):
        if m.group(1) in defines:
            raise SystemExit("pdabi: %s defined twice" % m.group(1))
        defines[m.group(1)] = parse_int(m.group(2))
        LITERALS[m.group(1)] = m.group(2)
    structs = {}                       # tag -> dict(fields=[...], size, align)
    order = []
    for m in re.finditer(r"\bstruct\s+(\w+)\s*\{(.*?)\}\s*;", t, flags=re.S):
        tag, body = m.group(1), m.group(2)
        fields = []
        for stmt in body.split(";"):
            stmt = " ".join(stmt.split())
            if not stmt:
                continue
            mt = re.match(r"^(struct\s+\w+|\w+)\s+(.*)$", stmt)
            if not mt:
                raise SystemExit("pdabi: cannot parse field '%s' in struct %s" % (stmt, tag))
            ty = mt.group(1)
            for decl in mt.group(2).split(","):
                md = re.match(r"^\s*(\w+)((?:\s*\[\s*\d+\s*\])*)\s*$", decl)
                if not md:
                    raise SystemExit("pdabi: cannot parse declarator '%s' in struct %s" % (decl, tag))
                dims = [int(x) for x in re.findall(r"\[\s*(\d+)\s*\]", md.group(2))]
                fields.append((md.group(1), ty, dims))
        structs[tag] = {"raw": fields}
        order.append(tag)
    # layout, in declaration order (a struct may only use earlier ones)
    holes = []
    for tag in order:
        off = 0; al_max = 1; laid = []
        for name, ty, dims in structs[tag]["raw"]:
            if ty.startswith("struct"):
                sub = ty.split()[1]
                if sub not in structs or "size" not in structs[sub]:
                    raise SystemExit("pdabi: struct %s uses struct %s before it is defined" % (tag, sub))
                size, al = structs[sub]["size"], structs[sub]["align"]
            elif ty in SCALARS:
                size, al = SCALARS[ty]
            else:
                raise SystemExit("pdabi: unknown type '%s' (field %s.%s)" % (ty, tag, name))
            n = 1
            for d in dims:
                n *= d
            pad = (-off) % al
            if pad:
                holes.append("%s.%s at %d needs %d byte(s) of padding" % (tag, name, off, pad))
            off += pad
            esz = size * (n // dims[0] if dims else 1)
            laid.append({"name": name, "off": off, "size": size * n, "dims": dims, "esz": esz})
            off += size * n
            al_max = max(al_max, al)
        pad = (-off) % al_max
        if pad:
            holes.append("%s ends with %d byte(s) of tail padding" % (tag, pad))
        structs[tag].update(fields=laid, size=off + pad, align=al_max)
    pinned = {}
    for m in re.finditer(r"typedef\s+char\s+(\w+)\s*\[\s*sizeof\s*\(\s*struct\s+(\w+)\s*\)\s*(==|<=)\s*([\w]+)\s*\?\s*1\s*:\s*-1\s*\]\s*;", t):
        pinned[m.group(1)] = (m.group(2), m.group(3), m.group(4))
    return defines, structs, order, pinned, holes


def struct_key(tag):
    for pre in ("pdxb_", "pd_"):
        if tag.startswith(pre):
            return tag[len(pre):].upper()
    return tag.upper()


def expected_symbols(defines, structs, order):
    syms = {}
    for k, v in defines.items():
        syms[k] = v
    seen = {}
    for tag in order:
        key = struct_key(tag)
        if key in seen:
            raise SystemExit("pdabi: structs %s and %s both map to PDXB_%s_*" % (seen[key], tag, key))
        seen[key] = tag
        for f in structs[tag]["fields"]:
            base = "PDXB_%s_%s" % (key, f["name"].upper())
            for nm, val in ((base, f["off"]), (base + "_N", f["dims"][0] if f["dims"] else None),
                            (base + "_ESZ", f["esz"] if f["dims"] else None)):
                if val is None:
                    continue
                if nm in syms:
                    raise SystemExit("pdabi: the symbol %s is generated twice (struct %s)" % (nm, tag))
                syms[nm] = val
        syms["PDXB_%s_BYTES" % key] = structs[tag]["size"]
    return syms


def render(defines, structs, order):
    syms = expected_symbols(defines, structs, order)
    out = [
        "; ============================================================================",
        "; os8088 - apps/pagedesk/pdabi.inc          GENERATED - DO NOT EDIT",
        ";",
        "; The assembly mirror of apps/pagedesk/pdabi.h: every integer #define and",
        "; every struct field offset and size.  Written by",
        ";     python3 tools/os88pdabi.py --write",
        "; and held to the header by `python3 tools/os88pdabi.py --check`, which every",
        "; `make` runs.  PDXB_<STRUCT>_<FIELD> is a byte offset; arrays also get",
        "; _N (outer count) and _ESZ (bytes per outer element).",
        "; ============================================================================",
        "%ifndef PDABI_INC",
        "%define PDABI_INC 1",
        "",
        "; ---- #define ---------------------------------------------------------------",
    ]
    for k, v in defines.items():
        out.append("%-24s equ %s" % (k, LITERALS.get(k, v)))
    for tag in order:
        key = struct_key(tag)
        out.append("")
        out.append("; ---- struct %s (%d bytes) %s" % (tag, structs[tag]["size"], "-" * max(0, 52 - len(tag))))
        for f in structs[tag]["fields"]:
            base = "PDXB_%s_%s" % (key, f["name"].upper())
            out.append("%-30s equ %d" % (base, f["off"]))
            if f["dims"]:
                out.append("%-30s equ %d" % (base + "_N", f["dims"][0]))
                out.append("%-30s equ %d" % (base + "_ESZ", f["esz"]))
        out.append("%-30s equ %d" % ("PDXB_%s_BYTES" % key, structs[tag]["size"]))
    out += ["", "%endif ; PDABI_INC", ""]
    return "\n".join(out), syms


def parse_inc(path=INC):
    syms = {}
    for n, line in enumerate(open(path), 1):
        line = line.split(";", 1)[0].strip()
        m = re.match(r"^(\w+)\s+equ\s+(0[xX][0-9A-Fa-f]+|\d+)$", line)
        if m:
            if m.group(1) in syms:
                raise SystemExit("pdabi.inc:%d: %s defined twice" % (n, m.group(1)))
            syms[m.group(1)] = parse_int(m.group(2))
    return syms


def check():
    defines, structs, order, pinned, holes = parse_header()
    want = expected_symbols(defines, structs, order)
    errs = []
    errs += ["pdabi.h: implicit padding: " + h for h in holes]
    # the header's own pinned sizes
    for name, (tag, op, rhs) in pinned.items():
        if tag not in structs:
            errs.append("pdabi.h: %s pins unknown struct %s" % (name, tag)); continue
        lim = defines.get(rhs)
        if lim is None:
            lim = parse_int(rhs)
        sz = structs[tag]["size"]
        if (op == "==" and sz != lim) or (op == "<=" and sz > lim):
            errs.append("pdabi.h: struct %s is %d bytes by the layout rules here but %s pins it %s %d" % (tag, sz, name, op, lim))
    # the assembly mirror
    if not os.path.exists(INC):
        errs.append("pdabi.inc is missing - run: python3 tools/os88pdabi.py --write")
    else:
        have = parse_inc()
        for k in sorted(want):
            if k not in have:
                errs.append("pdabi.inc: missing %s (pdabi.h says %d)" % (k, want[k]))
            elif have[k] != want[k]:
                errs.append("pdabi.inc: %s = %d but pdabi.h says %d" % (k, have[k], want[k]))
        for k in sorted(have):
            if k not in want:
                errs.append("pdabi.inc: %s is not in pdabi.h (a stale name; regenerate)" % k)
    # pdmath.inc's mirrored constants and its coverage of pdmath.h
    if os.path.exists(MATH_INC):
        mi = open(MATH_INC).read()
        mconst = {m.group(1): parse_int(m.group(2)) for m in re.finditer(r"^(PDMI_\w+)\s+equ\s+(0[xX][0-9A-Fa-f]+|\d+)", strip_asm(mi), flags=re.M)}
        links = {"PDMI_E_OK": "PDE_OK", "PDMI_E_SYNTAX": "PDE_SYNTAX", "PDMI_E_RANGE": "PDE_RANGE",
                 "PDMI_V_ROT": "PDXB_VIEW_ROT", "PDMI_V_ZOOM": "PDXB_VIEW_ZOOM",
                 "PDMI_V_ASPN": "PDXB_VIEW_ASP_NUM", "PDMI_V_ASPD": "PDXB_VIEW_ASP_DEN",
                 "PDMI_V_OX": "PDXB_VIEW_OX", "PDMI_V_OY": "PDXB_VIEW_OY"}
        rect = {f["name"]: f["off"] for f in structs["pd_rect"]["fields"]}
        box = next(f for f in structs["pd_view"]["fields"] if f["name"] == "box")["off"]
        for e in ("X0", "Y0", "X1", "Y1"):
            links["PDMI_V_" + e] = box + rect[e.lower()]
        for k, v in sorted(mconst.items()):
            tgt = links.get(k)
            if tgt is None:
                errs.append("pdmath.inc: %s has no counterpart in pdabi.h that this checker knows" % k)
                continue
            want_v = tgt if isinstance(tgt, int) else want.get(tgt)
            if want_v is None:
                errs.append("pdmath.inc: %s mirrors %s, which pdabi.h no longer defines" % (k, tgt))
            elif want_v != v:
                errs.append("pdmath.inc: %s = %d but pdabi.h says %d (%s)" % (k, v, want_v, tgt))
        labels = set(re.findall(r"^(_\w+):", strip_asm(mi), flags=re.M))
        for fn in math_functions():
            if "_" + fn not in labels:
                errs.append("pdmath.inc: no label _%s for the function pdmath.h declares" % fn)
    return errs


def strip_asm(t):
    return "\n".join(l.split(";", 1)[0] for l in t.splitlines())


def math_functions():
    t = strip_comments(open(MATH_H).read())
    return re.findall(r"^[ \t]*[\w\s\*]*?\b(pd_?[a-z]\w*)\s*\(", t, flags=re.M)


def main(argv):
    mode = argv[1] if len(argv) > 1 else "--check"
    if mode == "--write":
        defines, structs, order, pinned, holes = parse_header()
        text, _ = render(defines, structs, order)
        open(INC, "w").write(text)
        print("os88pdabi: wrote %s (%d names)" % (os.path.relpath(INC, ROOT), len(expected_symbols(defines, structs, order))))
        return 0
    if mode == "--dump":
        defines, structs, order, pinned, holes = parse_header()
        for tag in order:
            print("struct %s  %d bytes" % (tag, structs[tag]["size"]))
            for f in structs[tag]["fields"]:
                print("    +%-4d %-14s %s" % (f["off"], f["name"], ("x".join(map(str, f["dims"])) if f["dims"] else "")))
        return 0
    if mode == "--check":
        errs = check()
        if errs:
            print("os88pdabi: the PageDesk ABI mirrors disagree (%d):" % len(errs))
            for e in errs:
                print("  " + e)
            print("  (pdabi.inc is generated: python3 tools/os88pdabi.py --write)")
            return 1
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
