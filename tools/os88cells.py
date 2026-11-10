#!/usr/bin/env python3
"""The API table's cells, decoded - and the SDK's CALL SPELLING generated from them.

    python3 tools/os88cells.py              # check the SDK's kind blocks (exit 1 if stale)
    python3 tools/os88cells.py --write      # regenerate them from build/kernel.bin
    python3 tools/os88cells.py --list       # every cell: address, kind, published name

TWO THINGS LIVE HERE, and they are one subject.

1. THE DECODER (SPEC.md 20.3). Every cell shape the table holds, read out of
   the assembled bytes: tests/unit/t_api_abi.py checks each one against the
   SDK and the kernel's map, and this module is where the shapes are taught
   once, so the gate and the generator below cannot disagree about what a
   rare cell is. t_api_abi's header is the essay on WHY the bytes are read.

2. THE DOOR AND ITS BLOCKS (SPEC.md 20.3.3/20.3.4, kernel size pass 19's T6
   and T5). A package calls a cell as `call OSAPI_X` and nothing else, ~8,400
   sites; a RARE cell's call is `int OS88_RARE / db <index>` since T5, the
   index naming the cell's word in the kernel's rd_tab. Rewriting the sites
   would have touched every package and every gate that reads the spelling,
   so `apps/os88api.inc` overloads the mnemonic once (`%imacro call 1`), each
   rare cell's %define says OS88_DOOR, and a generated block re-expands it
   with its INDEX:

       %xdefine OSAPI_WM_SHOW __OSAPI_RARE 0x05   ; RS

   so `call OSAPI_WM_SHOW` arrives at the macro as `__OSAPI_RARE 0x05` and
   the macro's one rare arm, `OS88_RARE_CALL`, emits the door. NASM expands a
   multi-line macro's parameters before the macro sees them, so the NAME is
   gone by then and the mark has to ride in the expansion - which is also
   what makes a macro-wrapped site (`call %2` in os88thunk.asm, os88ui.inc,
   w16pm.inc, stiklib.asm) carry it with no edit.

   Which cells are rare, their kinds and their INDICES are facts about THE
   KERNEL - rd_tab's `RDPUB` lines, whose `rdp_<NAME>` equates are the index a
   site carries - so the blocks are GENERATED from build/kernel.bin's map and
   never typed: `--write` rewrites each SDK file's block between its markers,
   and t_api_abi (fast tier) fails the build when a block is stale. One block
   per file, because a %xdefine must follow the %define it re-expands -
   drivers/os88drv.inc publishes OSAPI_DRV_TASK after including os88api.inc.

   The source of truth is kern_big's map. One package binary serves every
   kernel, so the index of a published cell is the same on all four: rd_tab
   holds the kernel's own entries first and the door adds RD_P to every
   index, and a kernel with no body for a cell holds its index with a refusal
   entry (SPEC.md 20.3.4). `t_api_abi.py --all` checks every kernel's indices
   against this block.
"""
import argparse
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

TABLE_BASE = 0x0010
CELL = 8                # the LONGEST cell: the decode window, not the stride

# THE RARE KINDS (SPEC.md 20.3.4, kernel size pass 19's T5): a rare cell is no
# table cell at all but an INDEX into the kernel's rd_tab, reached by
# `int OS88_RARE / db <index>`, and its kind is the range the index falls in
# (rd_tab is written in kind order). The six-byte `push bp / call api_r<kind>
# / dw target` cells these names once described are gone.
RARE_KINDS = ("RS", "RCS", "RX", "RCX", "RN")
RARE_SECT = {"RS": ".text", "RCS": ".cold", "RX": ".text", "RCX": ".cold", "RN": ".cold"}
LEN = {"SLOT": 8, "YSLOT": 8, "XCELL": 7, "NCELL": 7, "FARCELL": 6, "JSLOT": 3, "FJSLOT": 8}
BODIES = ("api_x", "api_nc")

SHAPES = ("1E0E1F E8.. 1FCB | 9CFA0E E8.. CB90 | 1E0E1F EA.. ssss | 55BD.. E9.. | "
          "E9.. | 9A.. ssss CB | 2E <mov> [98] CB | FIXED")

# The INLINE cells with NO memory operand (kernel size pass 14): a
# protected-mode question kern_mega answers through a rare cell, which every
# other kernel answers with registers alone, in the cell. The ledger is the
# BYTES themselves - this kernel's, which is why a FIXED cell may differ on
# kern_mega (where these are RSLOT cells to bodies of their own).
FIXED = {}                # (kernel size pass 19's T5 moved the last three -
                          # OSAPI_XMEM_SEL, _XMEM_RUN, _MEM_CODESEL - behind the door)


def fixed(c):
    """(length, hex) when the cell is one of FIXED's byte strings, else None."""
    for want in set(FIXED.values()):
        b = bytes.fromhex(want)
        if bytes(c[:len(b)]) == b:
            return len(b), want
    return None


def inline(c):
    """(length, (dir, reg, operand, extra)) for an INLINE cell, else None.

    The operand is ("abs", offset) or ("bx", disp8). Only the encodings the
    table uses are accepted: a CS-prefixed accumulator moffs `mov` (A0-A3),
    a `mov` r/m16 or r/m8 with [disp16] or [bx+disp8], then an optional
    `cbw`, then the `retf` - anything else is not an inline cell.
    """
    if len(c) < 5 or c[0] != 0x2E:
        return None
    op = c[1]
    REG8, REG16 = ("al", "cl", "dl", "bl", "ah", "ch", "dh", "bh"), \
                  ("ax", "cx", "dx", "bx", "sp", "bp", "si", "di")
    if op in (0xA0, 0xA1, 0xA2, 0xA3):
        d = "load" if op in (0xA0, 0xA1) else "store"
        reg = "al" if op in (0xA0, 0xA2) else "ax"
        opd, n = ("abs", struct.unpack_from("<H", c, 2)[0]), 4
    elif op in (0x88, 0x89, 0x8A, 0x8B):
        d = "load" if op in (0x8A, 0x8B) else "store"
        mod, r, rm = c[2] >> 6, (c[2] >> 3) & 7, c[2] & 7
        reg = (REG16 if op & 1 else REG8)[r]
        if mod == 0 and rm == 6:
            opd, n = ("abs", struct.unpack_from("<H", c, 3)[0]), 5
        elif mod == 1 and rm == 7:
            opd, n = ("bx", c[3]), 4
        elif mod == 2 and rm == 7:          # [bx+disp16]: OSAPI_WM_ONWHEEL's
            opd, n = ("bx", struct.unpack_from("<H", c, 3)[0]), 5  # parity
        else:
            return None
    else:
        return None
    extra = ()
    if n < len(c) and c[n] == 0x98:
        extra, n = ("cbw",), n + 1
    elif n < len(c) and c[n] == 0xF8:       # a setter whose answer is CF = 0
        extra, n = ("clc",), n + 1
    if n >= len(c) or c[n] != 0xCB:
        return None
    return n + 1, (d, reg, opd, extra)


def mega_stub(blob, at):
    """kern_mega's per-cell stub at `at`: `mov ds, [cs:k_dsel]` (2E 8E 1E
    lo hi), then a near jump -> ("near", target), or a far one -> ("far",
    offset, segment). None for anything else."""
    s = blob[at:at + 10]
    if len(s) < 10 or s[0:3] != b"\x2e\x8e\x1e":
        return None
    if s[5] == 0xE9:
        return "near", (at + 8 + struct.unpack_from("<h", s, 6)[0]) & 0xFFFF
    if s[5] == 0xEB:
        return "near", (at + 7 + struct.unpack_from("<b", s, 6)[0]) & 0xFFFF
    if s[5] == 0xEA:
        return ("far",) + struct.unpack_from("<HH", s, 6)
    return None


def mega_icw(blob, at):
    """kern_mega's MEGA_ICW/MEGA_ICWB stub at `at` - `push ds / mov ds,
    [cs:k_dsel] / mov <mem>, <reg> / pop ds / retf` - as the INLINE cell's
    (dir, reg, operand, extra) it stands for, or None."""
    s = blob[at:at + 16]
    if len(s) < 16 or s[0:4] != b"\x1e\x2e\x8e\x1e":
        return None
    # the store, read as the inline cell it stands in for: a CS prefix in
    # front of it and the `retf` where the stub has `pop ds / retf`
    for n in (3, 4, 5):
        if s[6 + n:8 + n] == b"\x1f\xcb":
            il = inline(b"\x2e" + s[6:6 + n] + b"\xcb")
            if il is not None and il[0] == n + 2:
                return il[1]
    return None


def decode(blob, addr, bodies, cold_seg):
    """(kind, target, section) for the cell at `addr`, or (None, why, None).

    `bodies` maps the shared bodies' names (BODIES) to their .text offsets;
    `cold_seg` is COLD_SEG's value. The section is the one the shape says the
    target lives in - see tests/unit/t_api_abi.py's header.
    """
    c = blob[addr:addr + CELL]
    if len(c) < CELL:
        return None, None, None

    def rel(at, i):                       # a near displacement at c[i], from `at`
        return (at + struct.unpack_from("<h", c, i)[0]) & 0xFFFF

    def imm(i):
        return struct.unpack_from("<H", c, i)[0]

    if c[0:3] == b"\x1e\x0e\x1f" and c[3] == 0xE8 and c[6:8] == b"\x1f\xcb":
        return "SLOT", rel(addr + 6, 4), ".text"
    if c[0:4] == b"\x9c\xfa\x0e\xe8" and c[6:8] == b"\xcb\x90":
        # OSAPI_TASK_YIELD's own cell (ABI-BREAK-LIST V2, kernel size pass
        # 19): pushf / cli / push cs / call task_yield.save / retf / nop -
        # it builds the yield's int 08h frame under the far return
        return "YSLOT", rel(addr + 6, 4), ".text"
    # kern_mega's arms of the same cells (kernel.asm's OSAPI_SLOT and
    # OSAPI_FJCELL under KERN_MEGA, MEGA_ICW/MEGA_ICWB): `push cs / pop ds`
    # would be a read-only DS there, so each cell reaches a per-cell STUB that
    # loads the kernel's data selector and goes on - and the cell's target is
    # the routine past the stub, which is what the name has to agree with.
    if c[0] == 0x1E and c[1] == 0xE8 and c[4:8] == b"\x1f\xcb\x90\x90":
        t = mega_stub(blob, rel(addr + 4, 2))
        if t is None or t[0] != "near":
            return None, "kern_mega SLOT whose stub is not `mov ds, [cs:k_dsel] / jmp`", None
        return "SLOT", t[1], ".text"
    if c[0] == 0x1E and c[1] == 0xE9 and c[4:8] == b"\x90\x90\x90\x90":
        t = mega_stub(blob, rel(addr + 4, 2))
        if t is None or t[0] != "far" or t[2] != cold_seg:
            return None, "kern_mega FJSLOT whose stub is not a far jump to COLD_SEG", None
        return "FJSLOT", t[1], ".cold"
    if c[0] == 0xE9 and c[3] == 0x90 and (c[4] == 0xCB or c[4:6] == b"\x90\xcb"):
        n = 5 if c[4] == 0xCB else 6
        st = mega_icw(blob, rel(addr + 3, 1))
        if st is None:
            return None, "kern_mega inline cell whose stub is not one store through the data selector", None
        return "INLINE", (n, st), None
    if c[0:3] == b"\x1e\x0e\x1f" and c[3] == 0xEA:
        # the SLOT's DS switch with a FAR JUMP for its call (SPEC.md 20.3):
        # the .cold body pops the DS the cell pushed and returns for it
        seg = imm(6)
        if seg != cold_seg:
            return None, "FJSLOT to segment 0x%04X, which is not COLD_SEG (0x%04X)" % (seg, cold_seg), None
        return "FJSLOT", imm(4), ".cold"
    if c[0] == 0x55 and c[1] == 0xBD and c[4] == 0xE9:
        body = rel(addr + 7, 5)
        if body == bodies.get("api_x"):
            return "XCELL", imm(2), ".text"
        if body == bodies.get("api_nc"):
            # OSAPI_FILE_READ_SEQ's NCELL (kernel size pass 19): the XCELL's
            # shape jumping to the N body's cell entry, a .cold file door
            return "NCELL", imm(2), ".cold"
        return None, "BP-family shape jumping to 0x%04X, which is neither api_x nor api_nc" % body, None
    if c[0] == 0x9A and c[5] == 0xCB:
        seg = imm(3)
        if seg != cold_seg:
            return None, "FARCELL to segment 0x%04X, which is not COLD_SEG (0x%04X)" % (seg, cold_seg), None
        return "FARCELL", imm(1), ".cold"
    if c[0] == 0xE9:
        return "JSLOT", rel(addr + 3, 1), ".text"
    il = inline(c)
    if il is not None:
        return "INLINE", il, None
    # a cell that DOES NOTHING on this kernel, its length spent as NOPs
    # (kern_small's OSAPI_GFX_BLIT1_PEN: no pen to set, and the cell is five
    # bytes on every kernel). t_api_abi holds a per-kernel row for each
    n = 0
    while n < len(c) and c[n] == 0x90:
        n += 1
    if 0 < n < len(c) and c[n] == 0xCB:
        return "NOP", (n + 1, c[:n + 1].hex()), None
    fx = fixed(c)
    if fx is not None:
        return "FIXED", fx, None
    return None, c.hex(), None


def cell_len(kind, tgt):
    return tgt[0] if kind in ("INLINE", "FIXED", "NOP") else LEN[kind]


def walk(blob, bodies, cold_seg, tend):
    """[(addr, kind, target, section)] from TABLE_BASE to `tend`, by length.

    Stops at the first cell of no known shape, which is then the last entry
    with kind None and the bytes as its target - the caller reports it.
    """
    out, a = [], TABLE_BASE
    while a < tend:
        k, t, w = decode(blob, a, bodies, cold_seg)
        out.append((a, k, t, w))
        if k is None:
            break
        a += cell_len(k, t)
    return out


# --- the published side -------------------------------------------------------
SDK_FILES = ["apps/os88api.inc", "drivers/os88drv.inc"]
DEFINE = re.compile(r"^%define\s+(OSAPI_\w+)\s+KERNEL_SEG:(0x[0-9A-Fa-f]+)", re.M)


def sdk_cells(root=ROOT):
    """[(name, address, file)] for every published cell, both SDKs."""
    out = []
    for rel in SDK_FILES:
        with open(os.path.join(root, rel)) as f:
            for name, addr in DEFINE.findall(f.read()):
                out.append((name, int(addr, 16), rel))
    return out


# --- the door (SPEC.md 20.3.4) -------------------------------------------------
DOORDEF = re.compile(r"^%define\s+(OSAPI_\w+)\s+OS88_DOOR\b", re.M)


def sdk_doors(root=ROOT):
    """[(name, file)] for every published DOOR cell, both SDKs."""
    out = []
    for rel in SDK_FILES:
        with open(os.path.join(root, rel)) as f:
            for name in DOORDEF.findall(f.read()):
                out.append((name, rel))
    return out


def door_kind(ix, equ):
    """The kind of ABSOLUTE index `ix` by rd_tab's ranges, or None past RD_N."""
    for k, b in zip(RARE_KINDS, ("RD_B1", "RD_B2", "RD_B3", "RD_B4", "RD_N")):
        if ix < equ[b]:
            return k
    return None


def kernel_doors(blob, off, equ):
    """{name: (index, kind, target, section)} for every published door cell of
    a built kernel - `index` the SDK's (rdp_<NAME>), the target rd_tab's word
    at index + RD_P, the kind and section its range's."""
    out = {}
    rp, base = equ["RD_P"], off["rd_tab"]
    for k, v in equ.items():
        if not k.startswith("rdp_"):
            continue
        ab = v + rp
        kind = door_kind(ab, equ)
        tgt = struct.unpack_from("<H", blob, base + 2 * ab)[0]
        out["OSAPI_" + k[4:]] = (v, kind, tgt, RARE_SECT.get(kind))
    return out


# --- the kind blocks ------------------------------------------------------------
BEGIN = "; ---- BEGIN CELL KINDS"
END = "; ---- END CELL KINDS"
MARK = "__OSAPI_RARE"
KIND_LINE = re.compile(r"^%xdefine\s+(OSAPI_\w+)\s+" + MARK +
                       r"\s+(0x[0-9A-Fa-f]+|\d+)\s*(?:;\s*(\w+))?\s*$")


def block_lines(rel, text=None, root=ROOT):
    """The lines between `rel`'s markers, or None when it has no block."""
    if text is None:
        text = open(os.path.join(root, rel)).read()
    lines = text.splitlines()
    try:
        b = next(i for i, l in enumerate(lines) if l.startswith(BEGIN))
        e = next(i for i, l in enumerate(lines) if l.startswith(END) and i > b)
    except StopIteration:
        return None
    return lines[b + 1:e]


def block_rare(rel, text=None, root=ROOT):
    """{name: (index, kind)} the block in `rel` marks rare, or None with no
    block. A line that is neither a comment nor a well-formed kind line
    raises, so a hand edit that breaks the shape cannot be read as 'not
    rare'."""
    body = block_lines(rel, text, root)
    if body is None:
        return None
    out = {}
    for l in body:
        s = l.strip()
        if not s or s.startswith(";"):
            continue
        m = KIND_LINE.match(s)
        if not m:
            raise ValueError("%s: not a kind line inside the CELL KINDS block: %r" % (rel, l))
        out[m.group(1)] = (int(m.group(2), 0), m.group(3))
    return out


HEADER = [
    "; Generated by tools/os88cells.py --write from build/kernel.bin's rd_tab -",
    "; never edit by hand. tests/unit/t_api_abi.py (fast tier) fails `make`",
    "; when this block and the kernel disagree. Each line routes `call OSAPI_X`",
    "; to the door with the cell's INDEX (OS88_RARE_CALL, above) - SPEC.md",
    "; 20.3.3, 20.3.4. Sorted by index: RS, RCS, RX, RCX, RN.",
]


def render(rare, rel):
    """The block body for `rel`: {name: (index, kind)} for the cells it
    publishes."""
    out = list(HEADER)
    w = max([len(n) for n in rare] or [0])
    for name in sorted(rare, key=lambda n: rare[n][0]):
        ix, kind = rare[name]
        out.append("%%xdefine %-*s %s 0x%02X ; %s" % (w, name, MARK, ix, kind))
    return out


def rewrite(rel, rare, root=ROOT):
    """Replace `rel`'s block with `rare`'s. True if the file changed."""
    path = os.path.join(root, rel)
    text = open(path).read()
    lines = text.splitlines()
    b = next(i for i, l in enumerate(lines) if l.startswith(BEGIN))
    e = next(i for i, l in enumerate(lines) if l.startswith(END) and i > b)
    new = lines[:b + 1] + render(rare, rel) + lines[e:]
    out = "\n".join(new) + ("\n" if text.endswith("\n") else "")
    if out != text:
        with open(path, "w") as f:
            f.write(out)
        return True
    return False


def kernel_table(kbin=None, defines=(), mega=False):
    """(walk, {addr: kind}, doors) for a built kernel - kern_big's
    build/kernel.bin unless told otherwise. Needs tools/os88sym.py's proven
    map. `doors` is kernel_doors()'s answer."""
    import os88build
    import os88layout
    import os88sym
    path = kbin or os88build.at("build/kernel.bin")
    blob = open(path, "rb").read()[os88layout.boot2_pad(ROOT):]
    off = os88sym.syms(defines)
    equ = os88sym.equates(defines)
    cold_seg = equ["COLD_SEG"]
    bodies = {b: off[b] for b in BODIES if b in off}
    cells = walk(blob, bodies, cold_seg, off["osapi_table_end"])
    return cells, {a: k for a, k, _, _ in cells}, kernel_doors(blob, off, equ)


def expected(doors, root=ROOT):
    """{file: {name: (index, kind)}} - every published DOOR cell the kernel
    answers, under the file whose %define says OS88_DOOR."""
    out = {rel: {} for rel in SDK_FILES}
    for name, rel in sdk_doors(root):
        if name in doors:
            out[rel][name] = doors[name][:2]
    return out


def stale(doors, root=ROOT):
    """[complaint] per SDK file whose block disagrees with the kernel's
    doors, and for a door cell published on one side only."""
    bad = []
    pub = {n for n, _ in sdk_doors(root)}
    miss = sorted(pub - set(doors))
    if miss:
        bad.append("the SDK says OS88_DOOR for %s, and the kernel's rd_tab has no "
                   "RDPUB for it" % ", ".join(miss))
    extra = sorted(set(doors) - pub)
    if extra:
        bad.append("the kernel's rd_tab publishes %s, and no SDK %%define says "
                   "OS88_DOOR for it" % ", ".join(extra))
    for rel, want in expected(doors, root).items():
        try:
            have = block_rare(rel, root=root)
        except ValueError as e:
            bad.append(str(e))
            continue
        if have is None:
            bad.append("%s has no '%s' ... '%s' block" % (rel, BEGIN, END))
            continue
        if have != want:
            m = sorted(set(want) - set(have))
            x = sorted(set(have) - set(want))
            mv = sorted(n for n in set(want) & set(have) if want[n] != have[n])
            why = [("in the kernel's door, not marked: %s" % ", ".join(m)) if m else "",
                   ("marked, not in the kernel's door: %s" % ", ".join(x)) if x else "",
                   ("index or kind changed: %s" % ", ".join(mv)) if mv else ""]
            bad.append("%s's CELL KINDS block is stale (%s) - run "
                       "`python3 tools/os88cells.py --write`"
                       % (rel, "; ".join(w for w in why if w)))
    return bad


# --- the surviving-far-call gate (tools/os88pkg.py, tools/os88drv.py) ---------
# A package reaches a cell through `call OSAPI_X` and the macro above it, so
# after T5's flip a RARE cell is never a far call. One that still is - a byte
# string hand-assembled, a macro that built its own `9A` - lands on whatever
# the renumbered table put at that offset, and assembles and loads cleanly.
# So the packers scan every image they are handed for `9A lo hi <KERNEL_SEG>`:
#
#   * ALWAYS: an offset INSIDE the table (up to the last published cell) must
#     be a published cell's first byte - a far call into the middle of a cell
#     is wrong on every kernel. It found nothing in any package, driver or part
#     in this tree when it was armed (kernel size pass 19), so it is armed.
#     PAST the table it judges nothing: kern_dos (a part of DOS.O88) far-calls
#     the kernel's own disk layer at private offsets by design, which
#     tests/unit/t_kdfar.py polices, and a call to a cell newer than this SDK
#     is A4's and The Wire's cell fence's question, not this one's.
#   * once OS88_RARE_DOOR is 1 (T5's switch, in apps/os88api.inc): the offset
#     must not be a cell the CELL KINDS block marks rare. Before the flip every
#     package far-calls rare cells, so this half waits for the switch;
#     `OS88_RARE_GATE=1` in the environment arms it early, which is how it is
#     broken on purpose (tests/unit/t_cellgate.py).
#
# `9A` followed by four chosen bytes is the false positive this can have: data
# that happens to spell a far call to KERNEL_SEG at a non-cell offset. None
# exists in the tree; if one ever does, it fails the build loudly at the
# packer with the offset in the message, which is the place to find it.
def sdk_switch(root=ROOT):
    """(KERNEL_SEG, OS88_RARE_DOOR) as apps/os88api.inc states them."""
    t = open(os.path.join(root, "apps", "os88api.inc")).read()
    seg = int(re.search(r"^KERNEL_SEG\s+equ\s+(0x[0-9A-Fa-f]+|\d+)", t, re.M).group(1), 0)
    m = re.search(r"^%define\s+OS88_RARE_DOOR\s+(\d+)", t, re.M)
    return seg, int(m.group(1)) if m else 0


def farcall_problems(blobs, root=ROOT, armed=None):
    """[complaint] for every far call to KERNEL_SEG in `blobs` ({label: bytes})
    that lands INSIDE the table on no published cell's first byte. Since the
    door (OS88_RARE_DOOR) a rare cell has no table offset at all, so a far
    call that still names one - a byte string hand-assembled, a macro that
    built its own `9A` against an older SDK - lands mid-cell or on some hot
    cell the renumbered table put there; the first is what can be refused.
    Past the table nothing is judged. `armed` is kept for callers and
    ignored."""
    seg, door = sdk_switch(root)
    names = {}
    for name, addr, _ in sdk_cells(root):
        names.setdefault(addr, name)
    top = max(names) if names else 0
    pat = re.compile(re.escape(b"\x9a") + b"(..)" + re.escape(struct.pack("<H", seg)), re.S)
    out = []
    for label, blob in blobs.items():
        for m in pat.finditer(blob):
            off = struct.unpack("<H", m.group(1))[0]
            if names.get(off) is None and TABLE_BASE <= off <= top:
                out.append("%s +0x%04X: a far call to KERNEL_SEG:0x%04X, which is no "
                           "published cell's first byte - it lands in the middle of a "
                           "cell (SPEC.md 20.3.3). A rare cell is reached through the "
                           "door, `call OSAPI_X` and the SDK's macro, never by a far "
                           "call" % (label, m.start(), off))
    return out


def door_sites(blob, vec=0x81):
    """{index: count} for every `CD <vec> nn` in `blob` - a door site's
    INDEX byte (SPEC.md 20.3.4). Like the far-call scan it can count data
    that happens to spell one; it is an upper bound, which is what a fence
    wants."""
    out = {}
    for m in re.finditer(re.escape(bytes([0xCD, vec])) + b"(.)", blob, re.S):
        out[m.group(1)[0]] = out.get(m.group(1)[0], 0) + 1
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--write", action="store_true",
                    help="regenerate the SDK files' CELL KINDS blocks")
    ap.add_argument("--list", action="store_true",
                    help="print every cell: address, kind, published name; then the door")
    a = ap.parse_args()
    cells, kinds, doors = kernel_table()
    if cells and cells[-1][1] is None:
        sys.exit("os88cells: the table does not decode at 0x%04X: %s"
                 % (cells[-1][0], cells[-1][2]))
    if a.list:
        names = {addr: name for name, addr, _ in sdk_cells()}
        for addr, k, _, _ in cells:
            print("0x%04X %-8s %s" % (addr, k, names.get(addr, "-")))
        for name, (ix, k, t, w) in sorted(doors.items(), key=lambda x: x[1][0]):
            print("door 0x%02X %-4s %s -> %s:0x%04X" % (ix, k, name, w, t))
        return 0
    if a.write:
        for rel, rare in expected(doors).items():
            ch = rewrite(rel, rare)
            print("os88cells: %s: %d door cell(s)%s"
                  % (rel, len(rare), "" if ch else ", unchanged"))
        return 0
    bad = stale(doors)
    for b in bad:
        print("os88cells: FAIL: " + b)
    if not bad:
        print("os88cells: %d door cell(s) marked, matching the kernel"
              % sum(len(r) for r in expected(doors).values()))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
