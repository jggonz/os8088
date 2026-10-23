#!/usr/bin/env python3
"""Write RealDMG's recompiler TEMPLATES: build/rdgtmpl.bin, from the handlers.

    python3 apps/realdmg/rdgtmpl.py build/rdgcore.bin build/rdgtmpl.bin build/rdgcore.inc

docs/REALDMG-SPEC.md section 16. A template is a handler's body as
apps/realdmg/rdggen.py's template_lines() writes it: no dispatch, the operands
as IMM8 / IMM16. The translator (apps/realdmg/rdgjit.inc) copies one per
Game Boy instruction into the core segment and patches four kinds of word or
byte, which this script FINDS rather than being told, by assembling every
template three times and comparing the bytes:

  A  at its origin, IMM8 = A5h, IMM16 = A53Ch
  B  0x2000 further on, the same operands  - a word that moved by +2000h is a
                                             reference to the template itself
                                             (S_RET = .k), one that moved by
                                             -2000h a relative jump OUT of it,
                                             to a core helper (rd_slow ...)
  C  at A's origin, IMM8 = 5Ah, IMM16 = 5AC3h - a byte that changed is an
                                             operand

So nothing about the handlers is restated here, and a handler that changes
shape changes its template with it.

The table: 544 words, the record offsets for opcodes 00-FF, CB 00-FF and
rdggen.EXTRA's chosen-by-operand templates (0: no template), then the offset and count of the FREE RANGES, the TAIL SLOTS and the
interpreter's JDISPATCH SITES (six words; a site is a word, and with no
recompiler its first five bytes become the plain dispatch), then the records, then the two lists. The translated code goes into
a 64KB cache segment of its own, which holds copies of the core's helper
tails - the slots rdggen.py's TAILS live in - at their own offsets, so a
template reaches every helper with a near jump (REALDMG-SPEC section 16.2).
A free range is a start and an end word; a tail slot is a byte. The
records:

  +0 word  the origin variant A was assembled at
  +2 byte  its length          +3 byte  the Game Boy instruction's length
  +4 byte  flags (bit 0: may store anywhere; bit 1: stores to FF00+IMM8;
           bit 2: stores to IMM16; bit 3: PURE - its last three bytes are
           `sub bp,n`, the only thing in it that names BP, and nothing in it
           leaves it, so the translator may add n to the block's pending
           M-cycles instead of copying them; bit 4: GUARDS ITSELF - IMM16
           is the next PC, and the translator adds no guard, rdggen.self_guard;
           bit 5: DEFERRABLE - not pure, but it ends in `sub bp,n` and names
           BP elsewhere only in a `sub bp,m` straight before a jump out to
           tr_enter or tr_int, and reads the time nowhere else: BP exact at
           its start is enough, so with no guard the translator adds n to
           the pending M-cycles after it rather than copying the sub)
  +5 byte  operand positions: IMM8 count, then IMM16, ABS, REL counts (4)
  +9       the bytes, then the positions in that order
"""
import os
import re
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import rdggen  # noqa: E402

ORG = 0x2000            # variant A's first template; one 256-byte slot each
STEP = 0x2000           # variant B's displacement
SLOT = 256
CHUNK = 96              # templates a file: 96 x 256 + ORG + STEP < 64KB
V = {"A": (0xA5, 0xA53C), "C": (0x5A, 0x5AC3)}


def core_labels(mapfile):
    out = {}
    for ln in open(mapfile):
        m = re.match(r"\s+([0-9A-F]+)\s+([0-9A-F]+)\s+([A-Za-z_][A-Za-z0-9_]*)\s*$", ln)
        if m:
            out[m.group(3)] = int(m.group(1), 16)
    return out


def assemble(chunk, labels, org, imm8, imm16, tmp):
    src = ["cpu 8086", "bits 16", "org %d" % org, '%include "rdgdefs.inc"']
    used = set()
    for _, lines in chunk:
        for ln in lines:
            m = re.search(r"jmp near ([A-Za-z_]\w*)", ln)
            if m:
                used.add(m.group(1))
    for n in sorted(used):
        src.append("%s equ 0x%04X" % (n, labels[n]))
    src.append("%%define IMM8 0x%02X" % imm8)
    src.append("%%define IMM16 0x%04X" % imm16)
    for k, (key, lines) in enumerate(chunk):
        src.append("    times %d-($-$$) db 0" % (k * SLOT))
        src.append("T%d:" % k)
        src += lines
        src.append("T%d_end:" % k)
    asm = os.path.join(tmp, "t.asm")
    out = os.path.join(tmp, "t.bin")
    mp = os.path.join(tmp, "t.map")
    with open(asm, "w") as f:
        f.write("[map symbols %s]\n" % mp + "\n".join(src) + "\n")
    subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", HERE + "/", "-o", out, asm],
                   check=True)
    data = open(out, "rb").read()
    ends = {}
    for ln in open(mp):
        m = re.match(r"\s+([0-9A-F]+)\s+([0-9A-F]+)\s+T(\d+)_end\s*$", ln)
        if m:
            ends[int(m.group(3))] = int(m.group(1), 16) - org
    return [data[k * SLOT:ends[k]] for k in range(len(chunk))]


def classify(a, b, c, org, name):
    """The positions of IMM8, IMM16, ABS and REL in template a."""
    if not (len(a) == len(b) == len(c)):
        raise SystemExit("rdgtmpl: %s assembles to different lengths" % name)
    imm8, imm16, ab, rel = [], [], [], []
    i = 0
    while i < len(a):
        if a[i] != c[i]:
            if a[i] == 0xA5 and c[i] == 0x5A:
                imm8.append(i)
            elif a[i:i + 2] == bytes([0x3C, 0xA5]) and c[i:i + 2] == bytes([0xC3, 0x5A]):
                imm16.append(i)
                i += 1
            else:
                raise SystemExit("rdgtmpl: %s: operand byte at %d is neither" % (name, i))
        elif a[i] != b[i]:
            hit = None
            for j in (i - 1, i):
                if j < 0 or j + 2 > len(a) or (j in ab or j in rel) or (ab + rel and j <= max(ab + rel) + 1):
                    continue
                d = (struct.unpack_from("<H", b, j)[0] - struct.unpack_from("<H", a, j)[0]) & 0xFFFF
                if d == STEP:
                    hit = (ab, j)
                elif d == (-STEP) & 0xFFFF:
                    hit = (rel, j)
                if hit:
                    break
            if not hit:
                raise SystemExit("rdgtmpl: %s: a byte at %d moved with the origin "
                                 "by neither +-%04X" % (name, i, STEP))
            hit[0].append(hit[1])
            i = hit[1] + 1
        i += 1
    return imm8, imm16, ab, rel


def pure(a, pos, lines):
    """bit 3: the template ends in `sub bp,n`, names BP nowhere else, has no
    tail and jumps nowhere outside itself - so nothing in it reads the time
    (rdg_now reads BP) and its M-cycles can be counted with its neighbours'"""
    if pos[2] or pos[3] or len(a) < 3 or a[-3:-1] != bytes([0x83, 0xED]):
        return False
    code = [re.sub(r";.*", "", ln).strip() for ln in lines]
    code = [c for c in code if c]
    if not code or not re.match(r"sub bp,\d+$", code[-1]) or int(code[-1][7:]) != a[-1]:
        return False
    if any("Jend" in c or re.search(r"\bbp\b", c) for c in code[:-1]):
        return False
    return True


def deferrable(a, lines):
    """bit 5 (above): what is not pure but needs BP exact only at its start"""
    if len(a) < 3 or a[-3:-1] != bytes([0x83, 0xED]):
        return False
    code = [re.sub(r";.*", "", ln).strip() for ln in lines]
    code = [c for c in code if c]
    if not code or not re.match(r"sub bp,\d+$", code[-1]) or int(code[-1][7:]) != a[-1]:
        return False
    if any("force_ev" in c for c in code):              # EI: it reads BP
        return False
    for k, c in enumerate(code[:-1]):
        if re.search(r"\bbp\b", c):
            if not (re.match(r"sub bp,\d+$", c) and
                    re.match(r"jmp (near )?(tr_enter|tr_int)$", code[k + 1])):
                return False
    return True


# bit 6: the template may leave the block before its end - for the
# interpreter, the event entry, rdgsys (rdgjit.inc's rt_deadf looks past
# nothing that can, REALDMG-SPEC 16.1.2)
LEAVES = re.compile(r"\b(tr_\w+|force_ev|S_EVVEC|S_MBCVEC|S_DISP|idle_\w+)\b|jmp far")


def flags_for(op, cb, lines):
    text = "\n".join(lines)
    stores = re.search(r"\b(wr_\w+|io_wr|mbc_wr)\b", text) is not None   # any write helper
    if not stores:
        return 0
    if not cb and op in rdggen.SELFGUARD:
        return 16
    if not cb and op == 0xE0:
        return 2
    if not cb and op in (0xEA, 0x08):
        return 4
    return 1


NEXTRA = 56             # record slots after the 512 for rdggen.EXTRA
HEAD = 2 * (512 + NEXTRA) + 16   # then the core's holes', the free ranges',
                                 #   tail slots' and JDISPATCH sites' offset
                                 #   and count words;
                                 #   then, at HEAD, the dead-flag table
                                 #   (rdggen.flag_masks): 256 bytes of
                                 #   writes | reads << 4
# The longest template the table carries. With its guards (32 bytes at most,
# REALDMG-SPEC section 16.5) it must fit a gap, and the median gap is ~106
# bytes. What is over it is a read-modify-write of (HL) - INC/DEC (HL), the
# CB group on (HL), LD (nn),SP - whose read and write slow paths are both
# inline. rdggen.EXTRA carries them in a form that takes RAM only and leaves
# the rest to the interpreter (rdggen.rmw_lines). LD (C),A is 83 bytes since
# it guards itself (rdggen.self_guard), and a store that does takes no guard.
TMAX = 84
EXPORT = ("tr_enter", "tr_link", "tr_lsite", "tr_stale", "tr_int", "idle_yes", "cnt_ff", "add_ff", "cpy_ff", "stat_ff", "RDG_DISPATCH",
          "tr_hm0", "tr_hm1", "tr_hm2")


def free_ranges(tails):
    """The cache segment's free space: everything but the slots holding the
    core's helper tails (REALDMG-SPEC section 16.2), which the translator
    copies into it at the same offsets; the block table has a segment of its
    own. No block is ever at offset 0, which means 'no block' (tr_back).
    The last range stops at LOG: the first segment's FE00-FFEF is the link
    log (REALDMG-SPEC 16.2.3), and the second's is left unused with it."""
    out, start = [], 0x0010            # (offset 0 is 'no block')
    for slot in range(256):
        if slot in tails:
            if slot * 256 > start:
                out.append((start, slot * 256))
            start = (slot + 1) * 256
    if start < LOG:
        out.append((start, LOG))
    return out


LOG = 0xFE00                            # rdgjit.inc's RT_LOG


# The core's holes (REALDMG-SPEC 16.2.2): the 0CCh fill after each half-slot's
# code, which the recompiler writes translated code into as its second tier.
# One under HMIN is not listed - a gap rt_room cannot use is a jump through
# it on every run of the block that crossed it.
HMIN = 48


def core_holes(corebin, mapfile):
    """512 bytes, one a half-slot: where its fill begins, as an offset into
    the half-slot (the fill runs to the half-slot's end, capped at FFF0h as
    free_ranges' last), or FFh when there is less than HMIN of it. From each
    half-slot's ..@hole_N label (rdggen.py), every byte of the fill checked to
    be the generator's 0CCh - a byte of code listed here would be overwritten
    by translated code, so this refuses rather than trusts the label."""
    core = open(corebin, "rb").read()
    tab = bytearray(b"\xff" * 512)
    seen = set()
    for ln in open(mapfile):
        m = re.match(r"\s+([0-9A-F]+)\s+[0-9A-F]+\s+\.\.@hole_(\d+)\s*$", ln)
        if not m:
            continue
        a, n = int(m.group(1), 16), int(m.group(2))
        end = (n + 1) * 128
        if not (n * 128 < a <= end):
            raise SystemExit("rdgtmpl: hole %d at %04Xh is outside its half-slot" % (n, a))
        if core[a:end].strip(b"\xcc"):
            raise SystemExit("rdgtmpl: hole %d (%04Xh-%04Xh) is not all fill" % (n, a, end))
        seen.add(n)
        if min(end, 0xFFF0) - a >= HMIN:
            tab[n] = a - n * 128
    if seen != set(range(512)):
        raise SystemExit("rdgtmpl: %d half-slots have no ..@hole label" % (512 - len(seen)))
    return bytes(tab)


def main():
    corebin, out, inc = sys.argv[1], sys.argv[2], sys.argv[3]
    mapfile = corebin[:-4] + ".map"
    labels = core_labels(mapfile)
    todo = []
    for cb in (False, True):
        for op in range(256):
            t = rdggen.template_lines(op, cb)
            if t is not None:
                todo.append(((cb, op), t))
    assert len(rdggen.EXTRA) <= NEXTRA, len(rdggen.EXTRA)
    for k, e in enumerate(rdggen.EXTRA):
        if e[1] is not None:                # (a slot with no record)
            todo.append((("x", k), e[1]))
    recs = {}
    npure = [0]
    with tempfile.TemporaryDirectory(prefix="rdgtmpl") as tmp:
        for c0 in range(0, len(todo), CHUNK):
            chunk = todo[c0:c0 + CHUNK]
            A = assemble(chunk, labels, ORG, *V["A"], tmp)
            B = assemble(chunk, labels, ORG + STEP, *V["A"], tmp)
            C = assemble(chunk, labels, ORG, *V["C"], tmp)
            for k, ((cb, op), lines) in enumerate(chunk):
                name = ("X%d" if cb == "x" else "CB %02X" if cb else "%02X") % op
                a, b, c = A[k], B[k], C[k]
                pos = classify(a, b, c, ORG + k * SLOT, name)
                if len(a) > TMAX:           # the interpreter's: see TMAX
                    if cb == "x":           # (an EXTRA is chosen by operand:
                        raise SystemExit(   #   dropped, it would quietly be
                            "rdgtmpl: %s (%s) is %d bytes, over TMAX"   # the
                            % (name, rdggen.EXTRA[op][0], len(a)))      # slow way)
                    continue
                glen = rdggen.EXTRA[op][2] if cb == "x" else rdggen.template_glen(op, cb)
                fl = ((rdggen.EXTRA[op][3] if len(rdggen.EXTRA[op]) > 3 else 0)
                      if cb == "x" else flags_for(op, cb, lines))
                if pure(a, pos, lines):
                    fl |= 8
                    npure[0] += 1
                elif deferrable(a, lines):
                    fl |= 32
                if LEAVES.search("\n".join(lines)):
                    fl |= 64
                rec = struct.pack("<HBBB", ORG + k * SLOT, len(a), glen, fl)
                rec += bytes(len(ps) for ps in pos) + a
                for ps in pos:
                    rec += bytes(ps)
                recs[(cb, op)] = rec
    head = bytearray(HEAD)
    body = bytearray()
    for op in range(256):
        w, r = rdggen.flag_masks(op)
        body.append(w | r << 4)
    for op in range(256):                   # (rt_deadf's own rule for CB)
        w, r = rdggen.flag_masks_cb(op)
        assert w | r << 4 == (0 if op >= 0x80 else 7 if op >= 0x40 else
                              0x8F if op >= 0x10 and op < 0x20 else 0x0F), op
    for (cb, op), rec in sorted(recs.items(), key=lambda kv: (str(kv[0][0]), kv[0][1])):
        idx = 512 + op if cb == "x" else op + (256 if cb else 0)
        struct.pack_into("<H", head, 2 * idx, len(head) + len(body))
        body += rec
    holes = core_holes(corebin, mapfile)
    struct.pack_into("<HH", head, HEAD - 16, len(head) + len(body), len(holes))
    body += holes
    nhole = sum(1 for b in holes if b != 0xFF)
    hbytes = sum(min((n + 1) * 128, 0xFFF0) - (n * 128 + b)
                 for n, b in enumerate(holes) if b != 0xFF)
    tails = sorted(rdggen.TAILS)
    gl = free_ranges(set(tails))
    struct.pack_into("<HH", head, HEAD - 12, len(head) + len(body), len(gl))
    for a, b in gl:
        body += struct.pack("<HH", a, b)
    struct.pack_into("<HH", head, HEAD - 8, len(head) + len(body), len(tails))
    body += bytes(tails)
    jd = sorted(int(m.group(1), 16) for m in
                (re.match(r"\s+([0-9A-F]+)\s+[0-9A-F]+\s+\.\.@jd_\d+\s*$", ln) for ln in open(mapfile))
                if m)
    struct.pack_into("<HH", head, HEAD - 4, len(head) + len(body), len(jd))
    for a in jd:
        body += struct.pack("<H", a)
    with open(out, "wb") as f:
        f.write(bytes(head) + bytes(body))
    with open(inc, "w") as f:
        f.write("; GENERATED by apps/realdmg/rdgtmpl.py from %s - the core's offsets\n"
                "; the translator writes jumps to (REALDMG-SPEC section 16)\n" % corebin)
        for n in EXPORT:
            f.write("RDGC_%s equ 0x%04X\n" % (n.upper(), labels[n]))
        f.write("RDGT_HFREE equ %d\nRDGT_HTAIL equ %d\nRDGT_HJD equ %d\n"
                "RDGT_XBANK equ %d\nRDGT_XNOF equ %d\nRDGT_FLG equ %d\n"
                "RDGT_CFREE equ %d\n"
                % (HEAD - 12, HEAD - 8, HEAD - 4, 512 + rdggen.RDG_XBANK,
                   512 + rdggen.RDG_XNOF, HEAD, HEAD - 16))
        # the JDISPATCH sites again, for the package itself: an 8088 never
        # loads the recompiler's part (REALDMG-SPEC 16.9) and still makes
        # them plain DISPATCH (rw_jdoff)
        f.write("RDGC_JDN equ %d\n%%macro RDGC_JDTAB 0\n" % len(jd))
        for i in range(0, len(jd), 8):
            f.write("    dw %s\n" % ", ".join("0x%04X" % a for a in jd[i:i + 8]))
        f.write("%endmacro\n")
    print("rdgtmpl: %d templates (%d pure), %d free ranges of %d bytes, %d core holes "
          "of %d bytes, %d bytes"
          % (len(recs), npure[0], len(gl), sum(b - a for a, b in gl), nhole,
             hbytes, len(head) + len(body)))


if __name__ == "__main__":
    main()
