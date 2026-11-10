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

2. THE KIND BLOCKS (SPEC.md 20.3.3, kernel size pass 19's T6). A package calls
   a cell as `call OSAPI_X` and nothing else, ~8,400 sites; T5 turns a RARE
   cell's call into `int <door> / db <index>`. Rewriting the sites would have
   touched every package and every gate that reads the spelling, so instead
   `apps/os88api.inc` overloads the mnemonic once (`%imacro call 1`) and each
   rare cell carries a KIND MARK in its expansion:

       %xdefine OSAPI_WM_SHOW __OSAPI_RARE OSAPI_WM_SHOW   ; RSLOT

   so `call OSAPI_WM_SHOW` arrives at the macro as
   `__OSAPI_RARE KERNEL_SEG:0x007C`, which the macro routes to the ONE rare
   arm, `OS88_RARE_CALL`. Today that arm is the same far call, so every image
   is byte-identical; T5 flips it by editing that one macro. NASM expands a
   multi-line macro's parameters before the macro sees them, so the NAME is
   gone by then and the mark has to ride in the expansion - which is also
   what makes a macro-wrapped site (`call %2` in os88thunk.asm, os88ui.inc,
   w16pm.inc, stiklib.asm) carry it with no edit.

   Which cells are rare is a fact about THE KERNEL, so the blocks are
   GENERATED from build/kernel.bin's decoded table and never typed: `--write`
   rewrites each SDK file's block between its markers, and t_api_abi (fast
   tier) fails the build when a block is stale. One block per file, because a
   %xdefine must follow the %define it re-expands - drivers/os88drv.inc
   publishes OSAPI_DRV_TASK after including os88api.inc.

   The source of truth is kern_big's table. One SDK serves every kernel, so a
   cell rare on kern_big and something else on another kernel is a fact T5
   has to settle; `t_api_abi.py --kernel <k>` lists those disagreements.
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

# The rare shapes: `push bp / call api_r<kind> / dw target`, 6 bytes, the
# body behind the call (SPEC.md 20.3). These are the cells T5 moves behind
# the software-interrupt door.
RARE_KINDS = ("RSLOT", "RXCELL", "RCXCELL", "RNCELL", "RCSLOT")
LEN = {"SLOT": 8, "YSLOT": 8, "RSLOT": 6, "RXCELL": 6, "RCXCELL": 6, "RNCELL": 6, "RCSLOT": 6,
       "XCELL": 7, "FARCELL": 6, "JSLOT": 3, "FJSLOT": 8}
BODIES = ("api_x", "api_rs", "api_rx", "api_rxc", "api_rn", "api_rsc")

SHAPES = ("1E0E1F E8.. 1FCB | 9CFA0E E8.. CB90 | 1E0E1F EA.. ssss | 55E8.. tttt | 55BD.. E9.. | "
          "E9.. | 9A.. ssss CB | 2E <mov> [98] CB | FIXED")

# The INLINE cells with NO memory operand (kernel size pass 14): a
# protected-mode question kern_mega answers through a rare cell, which every
# other kernel answers with registers alone, in the cell. The ledger is the
# BYTES themselves - this kernel's, which is why a FIXED cell may differ on
# kern_mega (where these are RSLOT cells to bodies of their own).
FIXED = {
    "OSAPI_XMEM_SEL":    "b801003c02cb",   # mov ax, 1 / cmp al, 2: AX = 1, CF = 1
    "OSAPI_XMEM_RUN":    "b801003c02cb",   # ...the same refusal
    "OSAPI_MEM_CODESEL": "89d083f801cb",   # mov ax, dx / cmp ax, 1: CF iff DX = 0
}


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
    if c[0] == 0x55 and c[1] == 0xE8:
        # the 6-byte RARE cell (SPEC.md 20.3): push bp / call <kind> / dw target
        body = rel(addr + 4, 2)
        kind = {bodies.get("api_rs"): ("RSLOT", ".text"),
                bodies.get("api_rx"): ("RXCELL", ".text"),
                bodies.get("api_rxc"): ("RCXCELL", ".cold"),
                bodies.get("api_rn"): ("RNCELL", ".cold"),
                bodies.get("api_rsc"): ("RCSLOT", ".cold")}.get(body)
        if kind is None:
            return None, "rare cell calling 0x%04X, which is no rare body" % body, None
        return kind[0], imm(4), kind[1]
    if c[0] == 0x55 and c[1] == 0xBD and c[4] == 0xE9:
        body = rel(addr + 7, 5)
        if body != bodies.get("api_x"):
            return None, "BP-family shape jumping to 0x%04X, which is not api_x" % body, None
        return "XCELL", imm(2), ".text"
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


# --- the kind blocks ------------------------------------------------------------
BEGIN = "; ---- BEGIN CELL KINDS"
END = "; ---- END CELL KINDS"
MARK = "__OSAPI_RARE"
KIND_LINE = re.compile(r"^%xdefine\s+(OSAPI_\w+)\s+" + MARK + r"\s+(OSAPI_\w+)\s*(?:;\s*(\w+))?\s*$")


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
    """{name: kind} the block in `rel` marks rare, or None with no block.

    A line that is neither a comment nor a well-formed kind line raises, so a
    hand edit that breaks the shape cannot be read as 'not rare'.
    """
    body = block_lines(rel, text, root)
    if body is None:
        return None
    out = {}
    for l in body:
        s = l.strip()
        if not s or s.startswith(";"):
            continue
        m = KIND_LINE.match(s)
        if not m or m.group(1) != m.group(2):
            raise ValueError("%s: not a kind line inside the CELL KINDS block: %r" % (rel, l))
        out[m.group(1)] = m.group(3)
    return out


HEADER = [
    "; Generated by tools/os88cells.py --write from build/kernel.bin's table -",
    "; never edit by hand. tests/unit/t_api_abi.py (fast tier) fails `make`",
    "; when this block and the kernel disagree. Each line routes `call OSAPI_X`",
    "; to the rare arm (OS88_RARE_CALL, above) - SPEC.md 20.3.3.",
]


def render(rare, rel):
    """The block body for `rel`: {name: kind} for the cells it publishes."""
    out = list(HEADER)
    w = max([len(n) for n in rare] or [0])
    for name in sorted(rare):
        out.append("%%xdefine %-*s %s %-*s ; %s" % (w, name, MARK, w, name, rare[name]))
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
    """(walk, {addr: kind}) for a built kernel - kern_big's build/kernel.bin
    unless told otherwise. Needs tools/os88sym.py's proven map."""
    import os88build
    import os88layout
    import os88sym
    path = kbin or os88build.at("build/kernel.bin")
    blob = open(path, "rb").read()[os88layout.boot2_pad(ROOT):]
    off = os88sym.syms(defines)
    cold_seg = os88sym.equates(defines)["COLD_SEG"]
    bodies = {b: off[b] for b in BODIES if b in off}
    cells = walk(blob, bodies, cold_seg, off["osapi_table_end"])
    return cells, {a: k for a, k, _, _ in cells}


def expected(kinds, root=ROOT):
    """{file: {name: kind}} - every published RARE cell, under the file that
    publishes it."""
    out = {rel: {} for rel in SDK_FILES}
    for name, addr, rel in sdk_cells(root):
        k = kinds.get(addr)
        if k in RARE_KINDS:
            out[rel][name] = k
    return out


def stale(kinds, root=ROOT):
    """[complaint] per SDK file whose block disagrees with `kinds`."""
    bad = []
    for rel, want in expected(kinds, root).items():
        try:
            have = block_rare(rel, root=root)
        except ValueError as e:
            bad.append(str(e))
            continue
        if have is None:
            bad.append("%s has no '%s' ... '%s' block" % (rel, BEGIN, END))
            continue
        if have != want:
            miss = sorted(set(want) - set(have))
            extra = sorted(set(have) - set(want))
            moved = sorted(n for n in set(want) & set(have) if want[n] != have[n])
            why = [("rare on the kernel, not marked: %s" % ", ".join(miss)) if miss else "",
                   ("marked, not rare on the kernel: %s" % ", ".join(extra)) if extra else "",
                   ("kind changed: %s" % ", ".join(moved)) if moved else ""]
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
    that the gate above refuses. `armed` overrides the switch (None = read
    OS88_RARE_DOOR, then $OS88_RARE_GATE)."""
    seg, door = sdk_switch(root)
    if armed is None:
        armed = bool(door) or os.environ.get("OS88_RARE_GATE", "") not in ("", "0")
    names = {}
    for name, addr, _ in sdk_cells(root):
        names.setdefault(addr, name)
    top = max(names) if names else 0
    rare = set()
    if armed:
        for rel in SDK_FILES:
            rare |= set(block_rare(rel, root=root) or {})
    pat = re.compile(re.escape(b"\x9a") + b"(..)" + re.escape(struct.pack("<H", seg)), re.S)
    out = []
    for label, blob in blobs.items():
        for m in pat.finditer(blob):
            off = struct.unpack("<H", m.group(1))[0]
            name = names.get(off)
            if name is None and TABLE_BASE <= off <= top:
                out.append("%s +0x%04X: a far call to KERNEL_SEG:0x%04X, which is no "
                           "published cell's first byte - it lands in the middle of a "
                           "cell (SPEC.md 20.3.3)" % (label, m.start(), off))
            elif name in rare:
                out.append("%s +0x%04X: a far call to %s (KERNEL_SEG:0x%04X), a RARE cell. "
                           "Since the door (OS88_RARE_DOOR) a rare cell is reached by "
                           "`call %s` through the SDK's macro and never by a far call - "
                           "something built this call by hand (SPEC.md 20.3.3)"
                           % (label, m.start(), name, off, name))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--write", action="store_true",
                    help="regenerate the SDK files' CELL KINDS blocks")
    ap.add_argument("--list", action="store_true",
                    help="print every cell: address, kind, published name")
    a = ap.parse_args()
    cells, kinds = kernel_table()
    if cells and cells[-1][1] is None:
        sys.exit("os88cells: the table does not decode at 0x%04X: %s"
                 % (cells[-1][0], cells[-1][2]))
    if a.list:
        names = {addr: name for name, addr, _ in sdk_cells()}
        for addr, k, _, _ in cells:
            print("0x%04X %-8s %s" % (addr, k, names.get(addr, "-")))
        return 0
    if a.write:
        for rel, rare in expected(kinds).items():
            ch = rewrite(rel, rare)
            print("os88cells: %s: %d rare cell(s)%s"
                  % (rel, len(rare), "" if ch else ", unchanged"))
        return 0
    bad = stale(kinds)
    for b in bad:
        print("os88cells: FAIL: " + b)
    if not bad:
        print("os88cells: %d rare cell(s) marked, matching the kernel"
              % sum(len(r) for r in expected(kinds).values()))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
