#!/usr/bin/env python3
"""os88ne.py - read a Windows 3.x NE executable the way WIN16 (SPEC.md 124)
will, and say what it needs before a line of the layer runs it.

    python3 tools/os88ne.py FILE...           a report per file
    python3 tools/os88ne.py --dir DIR         every .EXE/.DLL/.DRV/.FON under DIR
    python3 tools/os88ne.py --json FILE...    the same, as JSON (what the gates read)
    python3 tools/os88ne.py --imports DIR     the union of imports over DIR,
                                              one MODULE.ordinal name per line

WHAT IT REPORTS (SPEC.md 124.3, 124.4):

  * the NE header, the segment table and the flags S2's loader refuses on
    (a library, a self-loading module, protected mode only, link errors);
  * every import as MODULE.ordinal with its name, read from the vendored
    `.spec` tables in tools/win16spec/ (winevdm's, which are Wine's);
  * the resources: menus item by item, dialogs control by control, bitmaps,
    strings, and a FONT's metrics, which is how D4's System font sizes were
    measured;
  * the OS fixups, the floating-point sites the MS C emulator convention marks;
  * an INSTRUCTION CENSUS PER CODE SEGMENT BY RECURSIVE DESCENT, and the CPU
    verdict that comes from it: 8086, 186, 286 or 386, plus x87 and which
    x87 instructions.

WHY RECURSIVE DESCENT. A Windows code segment carries data: strings, jump
tables, constants. A linear sweep decodes them as instructions, and the bytes
of ASCII text are exactly the range 186 opcodes live in (0x60-0x6F is `` `a-o``)
- Open Watcom's 1,906-byte HELLO16.EXE, which is 8086 code compiled with -0,
sweeps as 25 "186 and 386 instructions", every one of them in a string. So
decoding starts at the places code is KNOWN to start and follows the control
flow:

  1. the program's CS:IP;
  2. every entry-table entry in a code segment (the exported procedures:
     window procedures, dialog procedures, callbacks);
  3. every INTERNAL relocation whose target is a code segment (a far call
     or far pointer the loader would have to fix up);
  4. the entries of a `jmp cs:[bx+table]` switch table, read until an entry
     leaves the segment or the table runs into decoded code;
  5. LAST, the gaps that are still undecoded are searched for the standard
     prologues (`push ds / pop ax / nop`, `mov ax, ds / nop`, `inc bp / push bp
     / mov bp, sp`, `push bp / mov bp, sp`). That is the one heuristic, it is
     counted separately in the report, and it errs the safe way: a procedure
     reached only through a near pointer in data is still counted, so its 186
     instructions still move the verdict.

What is left undecoded is reported as coverage, and the 186-looking bytes in
it are reported too, so a verdict is never quieter than its evidence.

Every byte read off a file is treated as hostile: a malformed file is a
one-line refusal in the report, never a traceback.
"""
import collections
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SPECDIR = os.path.join(HERE, "win16spec")

# --------------------------------------------------------------------------
# The .spec tables
# --------------------------------------------------------------------------

# Wine names a module by the file it is built as; a Windows program names it
# by its module name. KRNL386 is KERNEL to the program.
SPEC_ALIAS = {"KRNL386": "KERNEL"}


def load_specs(specdir=SPECDIR):
    """{MODULE: {ordinal: (name, convention, args)}} from `*.spec` files.

    A line is `ORD CONVENTION [-flags] NAME(ARGS) IMPL` or `ORD stub NAME`;
    comment lines (`#`) and `@`-ordinals are skipped. Where a file names an
    ordinal twice the first stands."""
    specs = {}
    if not os.path.isdir(specdir):
        return specs
    for f in sorted(os.listdir(specdir)):
        if not f.endswith(".spec"):
            continue
        mod = f.split(".")[0].upper()
        mod = SPEC_ALIAS.get(mod, mod)
        tab = specs.setdefault(mod, {})
        with open(os.path.join(specdir, f), encoding="latin-1") as fh:
            for line in fh:
                t = line.split()
                if len(t) < 3 or not t[0].isdigit():
                    continue
                conv = t[1]
                i = 2
                while i < len(t) and t[i].startswith("-"):
                    i += 1
                if i >= len(t):
                    continue
                name, _, args = t[i].partition("(")
                tab.setdefault(int(t[0]), (name, conv, args.rstrip(")").split()))
    return specs


# --------------------------------------------------------------------------
# The NE image
# --------------------------------------------------------------------------

class NEError(Exception):
    pass


RT_NAMES = {1: "CURSOR", 2: "BITMAP", 3: "ICON", 4: "MENU", 5: "DIALOG",
            6: "STRING", 7: "FONTDIR", 8: "FONT", 9: "ACCELERATOR",
            10: "RCDATA", 12: "GROUP_CURSOR", 14: "GROUP_ICON", 16: "VERSION"}

# ne_flags (the word at NE+0Ch)
NEF_DGROUP = 0x0003         # 0 none, 1 single (shared), 2 multiple (instance)
NEF_PROTMODE = 0x0008       # "protected mode only" - Windows 3.0 real mode refuses it
NEF_APPTYPE = 0x0700        # 1 fullscreen, 2 windowcompat, 3 uses Windows API
NEF_LINKERR = 0x2000        # the linker reported errors: not executable
NEF_SELFLOAD = 0x0800       # the module loads its own segments
NEF_LIBRARY = 0x8000        # a DLL, not a program

# segment flags
NSF_DATA = 0x0001
NSF_MOVABLE = 0x0010
NSF_PRELOAD = 0x0040
NSF_RELOC = 0x0100
NSF_DISCARD = 0x1000

# relocation address types and kinds
RA_LOBYTE, RA_SEGMENT, RA_FARPTR, RA_OFFSET = 0, 2, 3, 5
RK_INTERNAL, RK_IMPORDINAL, RK_IMPNAME, RK_OSFIXUP = 0, 1, 2, 3
RK_ADDITIVE = 4


class Seg:
    def __init__(self, n, fpos, length, flags, minalloc, data):
        self.n = n                      # 1-based, as the file numbers them
        self.fpos = fpos
        self.length = length
        self.flags = flags
        self.minalloc = minalloc
        self.data = data
        self.relocs = []                # (addrtype, kind, additive, sites, t1, t2)
        self.site = {}                  # offset -> ("import", mod, ord|name) |
                                        #   ("internal", seg, off) | ("os", type)

    @property
    def code(self):
        return not (self.flags & NSF_DATA)


class NE:
    def __init__(self, data, name="?"):
        self.d = d = bytes(data)
        self.name = name
        if len(d) < 0x40 or d[:2] != b"MZ":
            raise NEError("not an MZ executable")
        ne = self.ne = self._w(0x3C)
        if ne + 0x40 > len(d) or d[ne:ne + 2] != b"NE":
            raise NEError("an MZ executable with no NE header (a DOS program)")
        h = lambda o: self._w(ne + o)
        self.linker = (d[ne + 2], d[ne + 3])
        self.flags = h(0x0C)
        self.autodata = h(0x0E)
        self.heap = h(0x10)
        self.stack = h(0x12)
        self.ip, self.cs = h(0x14), h(0x16)
        self.sp, self.ss = h(0x18), h(0x1A)
        self.cseg, self.cmod = h(0x1C), h(0x1E)
        self.nonres_len = h(0x20)
        segtab, rsrctab, restab = h(0x22), h(0x24), h(0x26)
        modtab, imptab = h(0x28), h(0x2A)
        self.nonres_at = self._dw(ne + 0x2C)
        entab, enlen = h(0x04), h(0x06)
        self.align = h(0x32) or 9
        if self.align > 15:
            raise NEError("alignment shift %d" % self.align)
        self.exetype = d[ne + 0x36]
        self.flagsothers = d[ne + 0x37]
        self.expver = h(0x3E)
        self.imptab = ne + imptab

        self.modules = [self.pstr(self.imptab + self._w(ne + modtab + 2 * i))
                        for i in range(self.cmod)]
        self.resident = self._names(ne + restab)
        self.module = self.resident[0][0] if self.resident else "?"
        self.nonresident = self._names(self.nonres_at) if self.nonres_at else []
        self.description = self.nonresident[0][0] if self.nonresident else ""

        self.segs = []
        for i in range(self.cseg):
            fpos, ln, sf, mn = struct.unpack_from("<HHHH", d, ne + segtab + 8 * i)
            off = fpos << self.align
            ln = ln or (0x10000 if fpos else 0)
            if fpos and off + ln > len(d):
                raise NEError("segment %d runs past the end of the file" % (i + 1))
            data_ = d[off:off + ln] if fpos else b""
            s = Seg(i + 1, off, ln, sf, mn or 0x10000, data_)
            if sf & NSF_RELOC and fpos:
                self._relocs(s, off + ln)
            self.segs.append(s)

        self.entries = self._entries(ne + entab, enlen)
        # No resource table is spelt as the resource offset EQUAL to the
        # resident-name table's: there is nothing between them.
        self.resources = [] if rsrctab == restab else self._resources(ne + rsrctab)

    # -- primitives (every one bounds-checked by struct) --------------------
    def _w(self, o):
        try:
            return struct.unpack_from("<H", self.d, o)[0]
        except struct.error:
            raise NEError("truncated at offset %#x" % o)

    def _dw(self, o):
        try:
            return struct.unpack_from("<I", self.d, o)[0]
        except struct.error:
            raise NEError("truncated at offset %#x" % o)

    def pstr(self, o):
        if o >= len(self.d):
            raise NEError("a name at %#x is past the end of the file" % o)
        n = self.d[o]
        return self.d[o + 1:o + 1 + n].decode("latin-1")

    def _names(self, o):
        out = []
        while o < len(self.d) and self.d[o]:
            nm = self.pstr(o)
            o += 1 + self.d[o]
            out.append((nm, self._w(o)))
            o += 2
        return out

    def _relocs(self, s, o):
        n = self._w(o)
        for k in range(n):
            try:
                at, rt, so, t1, t2 = struct.unpack_from("<BBHHH", self.d, o + 2 + 8 * k)
            except struct.error:
                raise NEError("segment %d's relocations are truncated" % s.n)
            kind, additive = rt & 3, bool(rt & RK_ADDITIVE)
            sites = []
            if kind == RK_OSFIXUP or additive:
                sites = [so]
            else:
                # A chain: each site's word is the offset of the next, 0xFFFF
                # ends it. Bounded, so a looping chain cannot hang the census.
                at_ = so
                while at_ != 0xFFFF and len(sites) < 0x8000:
                    if at_ + 2 > len(s.data):
                        raise NEError("segment %d: a relocation chain leaves it" % s.n)
                    sites.append(at_)
                    at_ = struct.unpack_from("<H", s.data, at_)[0]
                    if at_ in sites:
                        raise NEError("segment %d: a relocation chain loops" % s.n)
            if kind == RK_IMPORDINAL:
                if not 1 <= t1 <= len(self.modules):
                    raise NEError("an import names module %d of %d" % (t1, len(self.modules)))
                tgt = ("import", self.modules[t1 - 1], t2)
            elif kind == RK_IMPNAME:
                if not 1 <= t1 <= len(self.modules):
                    raise NEError("an import names module %d of %d" % (t1, len(self.modules)))
                tgt = ("import", self.modules[t1 - 1], self.pstr(self.imptab + t2))
            elif kind == RK_OSFIXUP:
                tgt = ("os", t1)
            else:
                tgt = ("internal", t1 & 0xFF, t2)   # seg 0xFF: t2 is an entry ordinal
            s.relocs.append((at, kind, additive, sites, tgt))
            for x in sites:
                s.site[x] = (at, tgt)

    def _entries(self, o, length):
        """{ordinal: (seg, offset, flags, movable)}."""
        out, ordn, end = {}, 1, o + length
        while o < end and o < len(self.d):
            cnt = self.d[o]
            if cnt == 0:
                break
            ind = self.d[o + 1]
            o += 2
            if ind == 0:
                ordn += cnt
                continue
            for _ in range(cnt):
                if ind == 0xFF:
                    fl, _int3f, seg, off = struct.unpack_from("<BHBH", self.d, o)
                    o += 6
                    out[ordn] = (seg, off, fl, True)
                elif ind == 0xFE:
                    fl, off = struct.unpack_from("<BH", self.d, o)
                    o += 3
                    out[ordn] = (0, off, fl, False)      # a constant
                else:
                    fl, off = struct.unpack_from("<BH", self.d, o)
                    o += 3
                    out[ordn] = (ind, off, fl, False)
                ordn += 1
        return out

    def resolve(self, tgt):
        """An internal target -> (seg, offset), or None."""
        _, seg, off = tgt
        if seg == 0xFF:
            e = self.entries.get(off)
            return (e[0], e[1]) if e and e[0] else None
        return (seg, off) if 1 <= seg <= len(self.segs) else None

    # -- resources ----------------------------------------------------------
    def _resources(self, o):
        base = o
        shift = self._w(o)
        o += 2
        out = []
        while True:
            tid = self._w(o)
            if tid == 0:
                break
            cnt = self._w(o + 2)
            tname = RT_NAMES.get(tid & 0x7FFF, "type%d" % (tid & 0x7FFF)) \
                if tid & 0x8000 else self.pstr(base + tid)
            for k in range(cnt):
                roff, rlen, rfl, rid = struct.unpack_from("<HHHH", self.d, o + 8 + 12 * k)
                nm = str(rid & 0x7FFF) if rid & 0x8000 else self.pstr(base + rid)
                at, ln = roff << shift, rlen << shift
                out.append({"type": tname, "name": nm, "at": at, "len": ln,
                            "detail": self._resdetail(tname, at, ln)})
            o += 8 + 12 * cnt
        return out

    def _resdetail(self, tname, at, ln):
        d = self.d
        try:
            if tname == "BITMAP":
                w, h, _p, bpp = struct.unpack_from("<iiHH", d, at + 4)
                return {"width": w, "height": h, "bpp": bpp}
            if tname == "DIALOG":
                return _dialog(d, at)
            if tname == "MENU":
                items, _ = _menu(d, at + 4)
                return {"items": items}
            if tname == "STRING":
                n, p = 0, at
                for _ in range(16):
                    n += d[p] != 0
                    p += 1 + d[p]
                return {"strings": n}
            if tname == "ACCELERATOR":
                n, p = 0, at
                while p + 5 <= at + ln:
                    n += 1
                    if d[p] & 0x80:
                        break
                    p += 5
                return {"keys": n}
            if tname == "FONT":
                return _fnt(d, at)
        except (struct.error, IndexError, ValueError):
            return {"error": "unreadable %s resource" % tname}
        return {}


def _cstr(d, p):
    e = d.index(b"\0", p)
    return d[p:e].decode("latin-1"), e + 1


CTL_CLASS = {0x80: "BUTTON", 0x81: "EDIT", 0x82: "STATIC", 0x83: "LISTBOX",
             0x84: "SCROLLBAR", 0x85: "COMBOBOX"}


def _dialog(d, p):
    style, n = struct.unpack_from("<IB", d, p)
    x, y, cx, cy = struct.unpack_from("<hhhh", d, p + 5)
    p += 13
    if d[p] == 0xFF:
        menu, p = "#%d" % struct.unpack_from("<H", d, p + 1)[0], p + 3
    else:
        menu, p = _cstr(d, p)
    cls, p = _cstr(d, p)
    cap, p = _cstr(d, p)
    font = None
    if style & 0x40:                                    # DS_SETFONT
        pt = struct.unpack_from("<H", d, p)[0]
        face, p = _cstr(d, p + 2)
        font = "%s %d" % (face, pt)
    ctls = []
    for _ in range(n):
        cx_, cy_, cw, ch, iid, st = struct.unpack_from("<hhhhHI", d, p)
        p += 14
        if d[p] & 0x80:
            c, p = CTL_CLASS.get(d[p], "class%#x" % d[p]), p + 1
        else:
            c, p = _cstr(d, p)
        if d[p] == 0xFF:
            txt, p = "#%d" % struct.unpack_from("<H", d, p + 1)[0], p + 3
        else:
            txt, p = _cstr(d, p)
        p += 1 + d[p]                                   # creation data
        ctls.append({"class": c, "style": st, "id": iid, "text": txt,
                     "rect": [cx_, cy_, cw, ch]})
    return {"caption": cap, "style": style, "rect": [x, y, cx, cy],
            "menu": menu, "class": cls, "font": font, "controls": ctls}


MF_GRAYED, MF_DISABLED, MF_CHECKED, MF_POPUP, MF_END = 0x01, 0x02, 0x08, 0x10, 0x80


def _menu(d, p, depth=0):
    items = []
    while True:
        fl = struct.unpack_from("<H", d, p)[0]
        p += 2
        it = {"flags": fl}
        if fl & MF_POPUP:
            it["text"], p = _cstr(d, p)
            if depth > 8:
                raise ValueError("menu nests too deep")
            it["items"], p = _menu(d, p, depth + 1)
        else:
            it["id"] = struct.unpack_from("<H", d, p)[0]
            it["text"], p = _cstr(d, p + 2)
        items.append(it)
        if fl & MF_END:
            return items, p


def _fnt(d, p):
    """The FNT header's metrics (Windows 3.x font file, versions 2 and 3)."""
    ver = struct.unpack_from("<H", d, p)[0]
    g = lambda o, f="<H": struct.unpack_from(f, d, p + o)[0]
    face_at = g(105, "<I")
    face = _cstr(d, p + face_at)[0] if face_at else ""
    return {"version": "%d.%02d" % (ver >> 8, ver & 0xFF), "face": face,
            "points": g(68), "vres": g(70), "hres": g(72), "ascent": g(74),
            "intlead": g(76), "extlead": g(78), "weight": g(83),
            "charset": d[p + 85], "pixwidth": g(86), "pixheight": g(88),
            "pitchfamily": d[p + 90], "avgwidth": g(91), "maxwidth": g(93),
            "first": d[p + 95], "last": d[p + 96],
            "proportional": g(86) == 0}


# --------------------------------------------------------------------------
# The decoder: 8086, 80186, the 286's 0F forms, and the x87
# --------------------------------------------------------------------------

T8086, T186, T286, T386 = 0, 1, 2, 3
TIER = ("8086", "186", "286", "386")

PREFIX = {0x26, 0x2E, 0x36, 0x3E, 0xF0, 0xF2, 0xF3}
P186 = {0x60: "pusha", 0x61: "popa", 0x62: "bound", 0x68: "push imm16",
        0x69: "imul r,rm,imm16", 0x6A: "push imm8", 0x6B: "imul r,rm,imm8",
        0x6C: "insb", 0x6D: "insw", 0x6E: "outsb", 0x6F: "outsw",
        0xC0: "shift rm8,imm8", 0xC1: "shift rm16,imm8", 0xC8: "enter",
        0xC9: "leave"}

# opcode -> (modrm, immediate bytes); None = special-cased below
_ALU = {}
for base in range(0x00, 0x40, 8):
    _ALU.update({base: (1, 0), base + 1: (1, 0), base + 2: (1, 0),
                 base + 3: (1, 0), base + 4: (0, 1), base + 5: (0, 2)})
LEN = dict(_ALU)
for op in (0x06, 0x07, 0x0E, 0x16, 0x17, 0x1E, 0x1F, 0x27, 0x2F, 0x37, 0x3F):
    LEN[op] = (0, 0)
for op in range(0x40, 0x62):
    LEN[op] = (0, 0)
LEN.update({0x62: (1, 0), 0x63: (1, 0), 0x68: (0, 2), 0x69: (1, 2), 0x6A: (0, 1),
            0x6B: (1, 1), 0x6C: (0, 0), 0x6D: (0, 0), 0x6E: (0, 0), 0x6F: (0, 0)})
for op in range(0x70, 0x80):
    LEN[op] = (0, 1)
LEN.update({0x80: (1, 1), 0x81: (1, 2), 0x82: (1, 1), 0x83: (1, 1)})
for op in range(0x84, 0x90):
    LEN[op] = (1, 0)
for op in range(0x90, 0xA0):
    LEN[op] = (0, 0)
LEN[0x9A] = (0, 4)
for op in range(0xA0, 0xA4):
    LEN[op] = (0, 2)
for op in range(0xA4, 0xB0):
    LEN[op] = (0, 0)
LEN.update({0xA8: (0, 1), 0xA9: (0, 2)})
for op in range(0xB0, 0xB8):
    LEN[op] = (0, 1)
for op in range(0xB8, 0xC0):
    LEN[op] = (0, 2)
LEN.update({0xC0: (1, 1), 0xC1: (1, 1), 0xC2: (0, 2), 0xC3: (0, 0), 0xC4: (1, 0),
            0xC5: (1, 0), 0xC6: (1, 1), 0xC7: (1, 2), 0xC8: (0, 3), 0xC9: (0, 0),
            0xCA: (0, 2), 0xCB: (0, 0), 0xCC: (0, 0), 0xCD: (0, 1), 0xCE: (0, 0),
            0xCF: (0, 0), 0xD0: (1, 0), 0xD1: (1, 0), 0xD2: (1, 0), 0xD3: (1, 0),
            0xD4: (0, 1), 0xD5: (0, 1), 0xD6: (0, 0), 0xD7: (0, 0)})
for op in range(0xD8, 0xE0):
    LEN[op] = (1, 0)
for op in range(0xE0, 0xE4):
    LEN[op] = (0, 1)
LEN.update({0xE4: (0, 1), 0xE5: (0, 1), 0xE6: (0, 1), 0xE7: (0, 1), 0xE8: (0, 2),
            0xE9: (0, 2), 0xEA: (0, 4), 0xEB: (0, 1), 0xEC: (0, 0), 0xED: (0, 0),
            0xEE: (0, 0), 0xEF: (0, 0), 0xF1: (0, 0), 0xF4: (0, 0), 0xF5: (0, 0),
            0xF6: (1, 0), 0xF7: (1, 0), 0xFE: (1, 0), 0xFF: (1, 0)})
for op in range(0xF8, 0xFE):
    LEN[op] = (0, 0)


def _modrm_len(m):
    mod, rm = m >> 6, m & 7
    if mod == 0:
        return 2 if rm == 6 else 0
    return (0, 1, 2, 0)[mod]


# The x87, by (escape, reg) for memory operands and (escape, modrm) / (escape,
# reg) for register operands. The tier is the coprocessor that first has it.
X87_MEM = {
    0xD8: ("fadd m32", "fmul m32", "fcom m32", "fcomp m32", "fsub m32", "fsubr m32", "fdiv m32", "fdivr m32"),
    0xD9: ("fld m32", None, "fst m32", "fstp m32", "fldenv", "fldcw", "fstenv", "fstcw"),
    0xDA: ("fiadd m32i", "fimul m32i", "ficom m32i", "ficomp m32i", "fisub m32i", "fisubr m32i", "fidiv m32i", "fidivr m32i"),
    0xDB: ("fild m32i", None, "fist m32i", "fistp m32i", None, "fld m80", None, "fstp m80"),
    0xDC: ("fadd m64", "fmul m64", "fcom m64", "fcomp m64", "fsub m64", "fsubr m64", "fdiv m64", "fdivr m64"),
    0xDD: ("fld m64", None, "fst m64", "fstp m64", "frstor", None, "fsave", "fstsw m16"),
    0xDE: ("fiadd m16i", "fimul m16i", "ficom m16i", "ficomp m16i", "fisub m16i", "fisubr m16i", "fidiv m16i", "fidivr m16i"),
    0xDF: ("fild m16i", None, "fist m16i", "fistp m16i", "fbld", "fild m64i", "fbstp", "fistp m64i"),
}
X87_REG = {
    0xD8: ("fadd st,st(i)", "fmul st,st(i)", "fcom st(i)", "fcomp st(i)", "fsub st,st(i)", "fsubr st,st(i)", "fdiv st,st(i)", "fdivr st,st(i)"),
    0xDC: ("fadd st(i),st", "fmul st(i),st", None, None, "fsubr st(i),st", "fsub st(i),st", "fdivr st(i),st", "fdiv st(i),st"),
    0xDD: ("ffree st(i)", None, "fst st(i)", "fstp st(i)", "fucom st(i)", "fucomp st(i)", None, None),
    0xDE: ("faddp st(i),st", "fmulp st(i),st", None, None, "fsubrp st(i),st", "fsubp st(i),st", "fdivrp st(i),st", "fdivp st(i),st"),
}
X87_ONE = {
    (0xD9, 0xD0): "fnop", (0xD9, 0xE0): "fchs", (0xD9, 0xE1): "fabs", (0xD9, 0xE4): "ftst",
    (0xD9, 0xE5): "fxam", (0xD9, 0xE8): "fld1", (0xD9, 0xE9): "fldl2t", (0xD9, 0xEA): "fldl2e",
    (0xD9, 0xEB): "fldpi", (0xD9, 0xEC): "fldlg2", (0xD9, 0xED): "fldln2", (0xD9, 0xEE): "fldz",
    (0xD9, 0xF0): "f2xm1", (0xD9, 0xF1): "fyl2x", (0xD9, 0xF2): "fptan", (0xD9, 0xF3): "fpatan",
    (0xD9, 0xF4): "fxtract", (0xD9, 0xF5): "fprem1", (0xD9, 0xF6): "fdecstp", (0xD9, 0xF7): "fincstp",
    (0xD9, 0xF8): "fprem", (0xD9, 0xF9): "fyl2xp1", (0xD9, 0xFA): "fsqrt", (0xD9, 0xFB): "fsincos",
    (0xD9, 0xFC): "frndint", (0xD9, 0xFD): "fscale", (0xD9, 0xFE): "fsin", (0xD9, 0xFF): "fcos",
    (0xDA, 0xE9): "fucompp", (0xDB, 0xE0): "feni", (0xDB, 0xE1): "fdisi", (0xDB, 0xE2): "fclex",
    (0xDB, 0xE3): "finit", (0xDB, 0xE4): "fsetpm", (0xDE, 0xD9): "fcompp", (0xDF, 0xE0): "fstsw ax",
}
X87_TIER = {"fsetpm": 287, "fstsw ax": 287, "fprem1": 387, "fsincos": 387, "fsin": 387,
            "fcos": 387, "fucom st(i)": 387, "fucomp st(i)": 387, "fucompp": 387}


def x87_name(esc, m):
    mod, reg = m >> 6, (m >> 3) & 7
    if mod != 3:
        return X87_MEM[esc][reg]
    if (esc, m) in X87_ONE:
        return X87_ONE[(esc, m)]
    if esc == 0xD9 and reg == 0:
        return "fld st(i)"
    if esc == 0xD9 and reg == 1:
        return "fxch st(i)"
    row = X87_REG.get(esc)
    return row[reg] if row else None


class Insn:
    __slots__ = ("off", "len", "op", "tier", "what", "flow", "targets", "x87", "intno",
                 "far", "indirect", "table")

    def __init__(self, off):
        self.off = off
        self.tier = T8086
        self.what = None        # a 186/286/386 mnemonic, for the report
        self.flow = "next"      # next | stop | branch (also next) | jump (not next) | call
        self.targets = []       # near targets in this segment
        self.far = None         # a relocated far call/jump's site
        self.x87 = None
        self.intno = None
        self.indirect = False
        self.table = None       # the cs-relative displacement of a jmp cs:[bx+table]


def decode(code, off):
    """One instruction at `off` -> Insn, or None for bytes that are not one."""
    i, n = off, len(code)
    ins = Insn(off)
    seg_cs = False
    while i < n and code[i] in PREFIX:
        seg_cs |= code[i] == 0x2E
        i += 1
    if i >= n:
        return None
    op = code[i]
    ins.op = op
    if op in (0x64, 0x65, 0x66, 0x67):
        ins.tier, ins.what = T386, "386 prefix %02x" % op
        ins.len = i + 1 - off
        ins.flow = "stop"           # not followed: the census has its verdict
        return ins
    if op == 0x0F:
        if i + 1 >= n:
            return None
        op2 = code[i + 1]
        if op2 in (0x00, 0x01, 0x02, 0x03):
            if i + 2 >= n:
                return None
            ins.tier = T286
            ins.what = ("sldt/str/lldt/ltr/verr/verw", "sgdt/sidt/lgdt/lidt/smsw/lmsw",
                        "lar", "lsl")[op2]
            ins.len = i + 3 + _modrm_len(code[i + 2]) - off
            return ins
        if op2 in (0x05, 0x06):
            ins.tier, ins.what = T286, ("loadall", "clts")[op2 - 5]
            ins.len = i + 2 - off
            return ins
        ins.tier, ins.what = T386, "0F %02x" % op2
        ins.len = i + 2 - off
        ins.flow = "stop"
        return ins
    if op not in LEN:
        return None
    modrm, imm = LEN[op]
    p = i + 1
    m = None
    if modrm:
        if p >= n:
            return None
        m = code[p]
        p += 1 + _modrm_len(m)
        if op in (0xF6, 0xF7) and (m >> 3) & 7 in (0, 1):
            imm = 1 if op == 0xF6 else 2
    # The MS C emulator convention: int 34h-3Bh stand for escapes D8-DF and
    # carry the escape's modrm; int 3Ch carries a segment byte first; 3Dh is
    # a bare fwait.
    if op == 0xCD and p < n and 0x34 <= code[p] <= 0x3D:
        v = code[p]
        ins.intno = v
        p += 1
        if v <= 0x3B:
            if p >= n:
                return None
            ins.x87 = (x87_name(0xD8 + v - 0x34, code[p]) or "esc?") + " (int %02xh)" % v
            p += 1 + _modrm_len(code[p])
        elif v == 0x3C:
            if p + 1 >= n:
                return None
            ins.x87 = "segment-override esc (int 3ch)"
            p += 2 + _modrm_len(code[p + 1])
        else:
            ins.x87 = "fwait (int 3dh)"
        ins.len = p - off
        return ins if ins.len <= n - off else None
    p += imm
    if p > n:
        return None
    ins.len = p - off
    if op in P186:
        ins.tier, ins.what = T186, P186[op]
    elif op == 0x63:
        ins.tier, ins.what = T286, "arpl"
    elif 0xD8 <= op <= 0xDF:
        ins.x87 = x87_name(op, m) or "esc %02x/%02x" % (op, m)
    end = off + ins.len
    rel8 = lambda: (end + (code[end - 1] - 256 if code[end - 1] > 127 else code[end - 1])) & 0xFFFF
    rel16 = lambda: (end + struct.unpack_from("<h", code, end - 2)[0]) & 0xFFFF
    if 0x70 <= op <= 0x7F or 0xE0 <= op <= 0xE3:
        ins.flow, ins.targets = "branch", [rel8()]
    elif op == 0xEB:
        ins.flow, ins.targets = "jump", [rel8()]
    elif op == 0xE9:
        ins.flow, ins.targets = "jump", [rel16()]
    elif op == 0xE8:
        ins.flow, ins.targets = "call", [rel16()]
    elif op in (0x9A, 0xEA):
        ins.far = i + 1
        ins.flow = "call" if op == 0x9A else "jump"
    elif op in (0xC2, 0xC3, 0xCA, 0xCB, 0xCF, 0xF4):
        ins.flow = "stop"
    elif op == 0xCD:
        ins.intno = code[end - 1]
    elif op == 0xFF:
        reg = (m >> 3) & 7
        if reg in (4, 5):
            ins.flow, ins.indirect = "jump", True
            # jmp cs:[bx+disp16] / jmp cs:[si+disp16] ... : a switch table
            if seg_cs and reg == 4 and m >> 6 == 2:
                ins.table = struct.unpack_from("<H", code, i + 2)[0]
        elif reg in (2, 3):
            ins.indirect = True
    return ins


# --------------------------------------------------------------------------
# The census
# --------------------------------------------------------------------------

PROLOGUES = (b"\x1e\x58\x90",           # push ds / pop ax / nop   (exported, MS C)
             b"\x8c\xd8\x90",           # mov ax, ds / nop          (exported, Borland)
             b"\x45\x55\x8b\xec",       # inc bp / push bp / mov bp, sp   (far frame)
             b"\x45\x55\x89\xe5",       # the same, Open Watcom's encoding
             b"\x55\x8b\xec",           # push bp / mov bp, sp
             b"\x55\x89\xe5")

# Imports that never return: the instruction after the call is not code.
NORETURN = {("KERNEL", 1), ("KERNEL", "FATALEXIT"), ("KERNEL", 137),
            ("KERNEL", "FATALAPPEXIT")}


class SegCensus:
    def __init__(self, seg):
        self.seg = seg
        self.insns = {}             # off -> Insn
        self.owner = {}             # byte -> instruction start (a byte is in ONE)
        self.data = set()           # bytes known to be a switch table
        self.overlaps = 0
        self.bad = 0                # a target that does not decode
        self.indirect = 0
        self.tables = 0
        self.table_entries = 0
        self.prologue_seeds = 0


def census(ne):
    """Recursive descent over every code segment of `ne`."""
    sc = {s.n: SegCensus(s) for s in ne.segs if s.code and s.data}
    work = []

    def seed(segn, off):
        if segn in sc and off < sc[segn].seg.length:
            work.append((segn, off))

    seed(ne.cs, ne.ip)
    for _ordn, (segn, off, _fl, _mov) in ne.entries.items():
        seed(segn, off)
    for s in ne.segs:
        for at, kind, _add, _sites, tgt in s.relocs:
            if kind == RK_INTERNAL and at in (RA_FARPTR, RA_OFFSET):
                r = ne.resolve(tgt)
                if r:
                    seed(*r)
    pending_tables = []

    def run():
        while work:
            segn, off = work.pop()
            c = sc[segn]
            code = c.seg.data
            while True:
                if off in c.insns:
                    break
                if off in c.owner or off in c.data:
                    c.overlaps += 1
                    break
                ins = decode(code, off)
                if ins is None:
                    c.bad += 1
                    break
                if any((off + k) in c.owner for k in range(ins.len)):
                    c.overlaps += 1
                    break
                c.insns[off] = ins
                for k in range(ins.len):
                    c.owner[off + k] = off
                for t in ins.targets:
                    if t < len(code):
                        work.append((segn, t))
                fall = ins.flow in ("next", "branch", "call")
                if ins.far is not None:
                    at_tgt = c.seg.site.get(ins.far)
                    if at_tgt and at_tgt[1][0] == "internal":
                        r = ne.resolve(at_tgt[1])
                        if r:
                            seed(*r)
                    elif at_tgt and at_tgt[1][0] == "import":
                        _, mod, o = at_tgt[1]
                        key = (mod.upper(), o.upper() if isinstance(o, str) else o)
                        if ins.flow == "call" and key in NORETURN:
                            fall = False
                if ins.indirect and ins.flow == "jump":
                    c.indirect += 1
                    if ins.table is not None:
                        pending_tables.append((segn, ins.table))
                if ins.intno == 0x21 and _ah_before(c, off) == 0x4C:
                    fall = False                         # DOS terminate
                if not fall:
                    break
                off += ins.len
                if off >= len(code):
                    break

    run()
    # switch tables, once the straight-line code is known
    while pending_tables:
        segn, t = pending_tables.pop()
        c = sc[segn]
        code = c.seg.data
        p, got = t, 0
        while p + 2 <= len(code) and got < 256 and p not in c.owner:
            e = struct.unpack_from("<H", code, p)[0]
            if e >= len(code) or t <= e < p + 2:
                break
            c.data.update((p, p + 1))
            work.append((segn, e))
            p += 2
            got += 1
        if got:
            c.tables += 1
            c.table_entries += got
        run()
    # the prologue heuristic, over what is still dark
    for segn, c in sc.items():
        code = c.seg.data
        changed = True
        while changed:
            changed = False
            o = 0
            while o < len(code):
                if o in c.owner or o in c.data:
                    o += 1
                    continue
                for pro in PROLOGUES:
                    if code.startswith(pro, o) and all(
                            (o + k) not in c.owner for k in range(len(pro))):
                        c.prologue_seeds += 1
                        work.append((segn, o))
                        run()
                        changed = True
                        break
                o += 1
    return sc


def _ah_before(c, off):
    """AH as set by the instruction just before `off`, if it is a constant
    load: `mov ah, imm8` or `mov ax, imm16`."""
    for k in (2, 3):
        p = off - k
        ins = c.insns.get(p)
        if ins and p + ins.len == off:
            code = c.seg.data
            if code[p] == 0xB4:
                return code[p + 1]
            if code[p] == 0xB8:
                return code[p + 2]
    return None


def _guarded(c):
    """Byte ranges only a 286 or better ever executes, behind the standard
    probe: `push sp / pop r / cmp r, sp / jnz past`. An 8086 or 186 pushes SP
    AFTER decrementing it, so the compare fails there and the jump is taken;
    the fall-through is the 286 side. A runtime library's CPU check reads
    exactly so (Open Watcom's start-up probes for `smsw` that way), and an
    instruction an 8086 never reaches must not move the verdict."""
    out = []
    code = c.seg.data
    for off in sorted(c.insns):
        if code[off] != 0x54:
            continue
        a = c.insns.get(off + 1)
        if not a or not 0x58 <= code[off + 1] <= 0x5F:
            continue
        r = code[off + 1] - 0x58
        b = c.insns.get(off + 2)
        if not b or code[off + 2:off + 4] not in (bytes((0x3B, 0xC4 | r << 3)),
                                                   bytes((0x39, 0xE0 | r))):
            continue
        j = c.insns.get(off + 4)
        if not j or code[off + 4] != 0x75 or not j.targets or j.targets[0] <= off + 6:
            continue
        out.append((off + 6, j.targets[0]))
    return out


def _linear_186(code, owner, data):
    """186+ opcodes a linear sweep would find in the bytes NOT reached."""
    n, o = 0, 0
    while o < len(code):
        if o in owner or o in data:
            o += 1
            continue
        ins = decode(code, o)
        if ins is None:
            o += 1
            continue
        if ins.tier >= T186:
            n += 1
        o += ins.len
    return n


# --------------------------------------------------------------------------
# The report
# --------------------------------------------------------------------------

def analyse(path_or_bytes, specs=None, name=None):
    """The whole census of one file, as a dict (what --json prints)."""
    if isinstance(path_or_bytes, (bytes, bytearray)):
        data, name = path_or_bytes, name or "?"
    else:
        name = name or os.path.basename(path_or_bytes)
        with open(path_or_bytes, "rb") as f:
            data = f.read()
    specs = load_specs() if specs is None else specs
    out = {"file": name, "bytes": len(data)}
    try:
        ne = NE(data, name)
        out.update(_analyse(ne, specs))
    except (NEError, struct.error, IndexError, ValueError, UnicodeDecodeError) as e:
        out["error"] = str(e) or e.__class__.__name__
    return out


def _analyse(ne, specs):
    r = {
        "module": ne.module, "description": ne.description,
        "linker": "%d.%d" % ne.linker,
        "expver": "%d.%02d" % (ne.expver >> 8, ne.expver & 0xFF) if ne.expver else None,
        "exetype": ne.exetype, "flags": ne.flags,
        "library": bool(ne.flags & NEF_LIBRARY),
        "selfload": bool(ne.flags & NEF_SELFLOAD),
        "pmode_only": bool(ne.flags & NEF_PROTMODE),
        "linkerrors": bool(ne.flags & NEF_LINKERR),
        "dgroup": ("none", "single", "multiple", "?")[ne.flags & NEF_DGROUP],
        "autodata": ne.autodata, "heap": ne.heap, "stack": ne.stack,
        "entry": [ne.cs, ne.ip], "ss_sp": [ne.ss, ne.sp],
        "modules": ne.modules,
        "exports": {str(o): [s, off] for o, (s, off, fl, _m) in ne.entries.items() if fl & 1},
        "resident_names": ne.resident[1:], "nonresident_names": ne.nonresident[1:],
    }
    imports = collections.Counter()
    osfix = collections.Counter()
    for s in ne.segs:
        for _at, kind, _add, sites, tgt in s.relocs:
            if tgt[0] == "import":
                imports[(tgt[1], tgt[2])] += len(sites)
            elif tgt[0] == "os":
                osfix[tgt[1]] += 1
    imp = []
    for (mod, o), n in sorted(imports.items(), key=lambda x: (x[0][0], str(x[0][1]))):
        if isinstance(o, int):
            nm = specs.get(mod.upper(), {}).get(o)
            imp.append({"module": mod, "ordinal": o, "name": nm[0] if nm else None,
                        "args": nm[2] if nm else None, "sites": n})
        else:
            imp.append({"module": mod, "ordinal": None, "name": o, "sites": n})
    r["imports"] = imp
    r["osfixups"] = {str(k): v for k, v in sorted(osfix.items())}

    sc = census(ne)
    segs, tier, x87 = [], T8086, collections.Counter()
    beyond = collections.Counter()
    ints = collections.Counter()
    io = 0
    for s in ne.segs:
        e = {"n": s.n, "kind": "code" if s.code else "data", "length": s.length,
             "minalloc": s.minalloc, "flags": s.flags, "relocs": len(s.relocs)}
        c = sc.get(s.n)
        if c:
            reached = len(c.owner)
            st = collections.Counter()
            guard = _guarded(c)
            for ins in c.insns.values():
                if ins.tier >= T286 and any(a <= ins.off < b for a, b in guard):
                    beyond["guarded %s %s" % (TIER[ins.tier], ins.what)] += 1
                    st[T8086] += 1          # an 8086 never executes it
                    continue
                st[ins.tier] += 1
                if ins.tier > T8086:
                    beyond["%s %s" % (TIER[ins.tier], ins.what)] += 1
                if ins.x87:
                    x87[ins.x87] += 1
                if ins.intno is not None and ins.intno not in range(0x34, 0x3E):
                    ah = _ah_before(c, ins.off)
                    ints["int %02xh" % ins.intno + (" ah=%02xh" % ah if ah is not None else "")] += 1
                if ins.op in (0xE4, 0xE5, 0xE6, 0xE7, 0xEC, 0xED, 0xEE, 0xEF):
                    io += 1
            tier = max([tier] + list(st))
            e.update({
                "instructions": len(c.insns), "reached": reached,
                "coverage": round(100.0 * (reached + len(c.data)) / s.length, 1) if s.length else 100.0,
                "tiers": {TIER[k]: v for k, v in sorted(st.items())},
                "switch_tables": c.tables, "switch_entries": c.table_entries,
                "indirect_jumps": c.indirect, "prologue_seeds": c.prologue_seeds,
                "overlaps": c.overlaps, "undecodable": c.bad,
                "dark_186": _linear_186(s.data, c.owner, c.data),
            })
        segs.append(e)
    r["segments"] = segs
    r["beyond_8086"] = dict(sorted(beyond.items()))
    r["x87"] = dict(sorted(x87.items()))
    r["interrupts"] = dict(sorted(ints.items()))
    r["port_io"] = io
    x87tier = max([87] + [X87_TIER.get(k, 87) for k in x87]) if x87 else None
    r["cpu"] = TIER[tier]
    r["fpu"] = ("8087" if x87tier == 87 else "80%d" % x87tier) if x87tier else None
    r["verdict"] = TIER[tier] + (" + x87" if x87 or osfix else "") if sc else "no code"
    r["resources"] = [{k: v for k, v in x.items() if k != "at"} for x in ne.resources]
    return r


def _fmt_menu(items, depth=0):
    out = []
    for it in items:
        t = it["text"] or "--"
        fl = it["flags"]
        t += "(chk)" if fl & MF_CHECKED else ""
        t += "(gray)" if fl & MF_GRAYED else ""
        if "items" in it:
            t += "[" + ", ".join(_fmt_menu(it["items"], depth + 1)) + "]"
        out.append(t)
    return out


def text_report(r):
    L = ["=== %s (%d bytes)" % (r["file"], r["bytes"])]
    if "error" in r:
        L.append("  NOT READABLE AS A WINDOWS PROGRAM: " + r["error"])
        return "\n".join(L)
    L.append("  module %s - %s" % (r["module"], r["description"]))
    L.append("  VERDICT: %s%s" % (r["verdict"], " (%s)" % r["fpu"] if r["fpu"] else ""))
    flags = [k for k in ("library", "selfload", "pmode_only", "linkerrors") if r[k]]
    L.append("  expects Windows %s, linker %s, DGROUP %s (seg %d), heap %d, stack %d, "
             "CS:IP %d:%04x%s" % (r["expver"], r["linker"], r["dgroup"], r["autodata"],
                                  r["heap"], r["stack"], r["entry"][0], r["entry"][1],
                                  ", " + " ".join(flags).upper() if flags else ""))
    for s in r["segments"]:
        line = "  seg %d %s len %d min %d relocs %d" % (s["n"], s["kind"], s["length"],
                                                       s["minalloc"], s["relocs"])
        if s["kind"] == "code" and "coverage" in s:
            line += " | %d insns, %.1f%% reached, %s" % (
                s["instructions"], s["coverage"],
                " ".join("%s:%d" % kv for kv in s["tiers"].items()))
            extra = [("switch tables", s["switch_tables"]), ("prologue seeds", s["prologue_seeds"]),
                     ("indirect jumps", s["indirect_jumps"]), ("overlaps", s["overlaps"]),
                     ("dark 186-looking", s["dark_186"])]
            line += "".join(" | %s %d" % kv for kv in extra if kv[1])
        L.append(line)
    if r["beyond_8086"]:
        L.append("  beyond 8086: " + ", ".join("%s x%d" % kv for kv in r["beyond_8086"].items()))
    if r["x87"]:
        L.append("  x87 (%d kinds): " % len(r["x87"]) +
                 ", ".join("%s x%d" % kv for kv in r["x87"].items()))
    if r["osfixups"]:
        L.append("  OS fixups: " + ", ".join("type %s x%d" % kv for kv in r["osfixups"].items()))
    if r["interrupts"]:
        L.append("  interrupts: " + ", ".join("%s x%d" % kv for kv in r["interrupts"].items()))
    if r["port_io"]:
        L.append("  port I/O instructions: %d" % r["port_io"])
    bymod = collections.defaultdict(list)
    for i in r["imports"]:
        nm = i["name"] or "?"
        bymod[i["module"]].append("%s.%s" % (nm, i["ordinal"]) if i["ordinal"] is not None else nm)
    L.append("  imports: %d from %s" % (len(r["imports"]), ", ".join(r["modules"]) or "nothing"))
    for m, l in bymod.items():
        L.append("    %s [%d]: %s" % (m, len(l), ", ".join(l)))
    if r["exports"]:
        L.append("  exports: " + ", ".join("%s=%d:%04x" % (o, s, off)
                                            for o, (s, off) in r["exports"].items()))
    if r["resources"]:
        L.append("  resources:")
        for x in r["resources"]:
            d = x["detail"]
            desc = ""
            if x["type"] == "MENU" and "items" in d:
                desc = " | ".join(_fmt_menu(d["items"]))
            elif x["type"] == "DIALOG" and "controls" in d:
                desc = "'%s' %s" % (d["caption"], " ".join(
                    "%s'%s'" % (c["class"], c["text"]) for c in d["controls"]))
            elif x["type"] == "FONT" and "face" in d:
                desc = "%s %dpt %dpx ascent %d avg %d max %d%s" % (
                    d["face"], d["points"], d["pixheight"], d["ascent"], d["avgwidth"],
                    d["maxwidth"], " proportional" if d["proportional"] else " fixed")
            elif d:
                desc = " ".join("%s=%s" % kv for kv in d.items())
            L.append("    %s %s (%d bytes) %s" % (x["type"], x["name"], x["len"], desc))
    return "\n".join(L)


WINEXT = (".EXE", ".DLL", ".DRV", ".FON")


def walk(d):
    for root, dirs, files in os.walk(d):
        dirs.sort()
        for f in sorted(files):
            if f.upper().endswith(WINEXT):
                yield os.path.join(root, f)


def main(argv):
    js = "--json" in argv
    argv = [a for a in argv if a != "--json"]
    if not argv or argv[0] in ("-h", "--help"):
        sys.exit(__doc__)
    specs = load_specs()
    if argv[0] == "--imports":
        need = collections.Counter()
        for p in walk(argv[1]):
            r = analyse(p, specs)
            for i in r.get("imports", []):
                need[(i["module"], i["ordinal"] if i["ordinal"] is not None else -1,
                      i["name"] or "?")] += 1
        for (m, o, n), k in sorted(need.items()):
            print("%s.%s %s %d" % (m, o if o >= 0 else "-", n, k))
        return 0
    paths = list(walk(argv[1])) if argv[0] == "--dir" else argv
    if argv[0] == "--dir" and not paths:
        sys.exit("os88ne: no .EXE/.DLL/.DRV/.FON under %s" % argv[1])
    res = [analyse(p, specs, os.path.relpath(p, argv[1]) if argv[0] == "--dir" else None)
           for p in paths]
    if js:
        json.dump(res, sys.stdout, indent=1)
        print()
    else:
        print("\n\n".join(text_report(r) for r in res))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
