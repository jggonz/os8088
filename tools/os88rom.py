#!/usr/bin/env python3
"""os88rom - build os8088's ROM for an IBM 5150's five spare sockets.

docs/plans/ROM-PLAN.md is the design and section 1.3 is the layout. The window
is U28-U32, F4000-FDFFF, 40,960 bytes, laid out as ONE option ROM:

    0x0000  55 AA 40  E9 rel16  'OS'  fmt kind  dd rom_patch  bal1 0   16 bytes
    0x0010  the payload (the kernel's .cold, or the socket-check pattern)
     ...    0xFF fill
    TAIL    boot/osrom.asm, assembled at its own offset
    0x9FFF  bal2

The header declares 32KB, and bal1 makes F4000-FBFFF sum to zero for the
option-ROM check. bal2 makes FC000-FDFFF sum to zero for the 10/27/82 BIOS's
one remaining BASIC-module check. GLaBIOS steps on through FC000-FDFFF in 2KB
looking for further option ROMs, so no 2KB boundary past the declared length
may begin 55 AA. The build refuses one that does rather than ship a ROM whose
POST behaviour depends on a coincidence in the payload.

Kinds:
    socket   the socket-check ROM - a pattern a stuck address line, a crossed
             chip select or a dropped byte cannot reproduce, and an init that
             prints `U28 ok ... U32 ok` at POST. No os8088 code is involved.
             It proves a ROM board before the kernel is asked to run on it.

Outputs, into --out (default build/rom/):
    <name>.bin               the 40KB window - MartyPC's custom ROM, at 0xF4000
    <name>-U28.bin ... -U32.bin   one 8KB file per socket, for One ROM's
                             tooling (or an EPROM burner)

Usage:
    python3 tools/os88rom.py socket
    python3 tools/os88rom.py --selfcheck
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

ROM_BASE = 0xF4000
ROM_SIZE = 40960
SOCK_SIZE = 8192
SOCKETS = ("U28", "U29", "U30", "U31", "U32")
DECL_BLOCKS = 0x40                  # 32KB - ROM-PLAN 1.3 point 2, NOT 0x50
DECL_SIZE = DECL_BLOCKS * 512
HDR_SIZE = 16
BAL1 = 14                           # the header's balance byte
FMT = 2                             # 2: +10 is a far pointer to rom_patch
KIND_SOCK, KIND_KERNEL, KIND_HMA = 1, 2, 3
HMA_SEG = 0xFFFF                    # the one segment above 1MB; 0010 is 0x100000

# the 1981 BIOSes never run rom_init, so int 18h still lands at F600:0000 on
# them; the socket ROM can afford a far jump there, the kernel ROM cannot
# (its .cold runs straight through F6000)
INT18_AT = 0xF6000 - ROM_BASE


def fail(msg):
    sys.exit("os88rom: " + msg)


def build_str(build=None):
    try:
        with open(os.path.join(build or os.path.join(ROOT, "build"),
                               "buildnum.inc")) as f:
            m = re.search(r"BUILD_STR\s+'([^']*)'", f.read())
            return m.group(1) if m else "?"
    except OSError:
        return "?"


def assemble_tail(kind, at, extra=(), bstr=None):
    """boot/osrom.asm assembled at `at`; the bytes."""
    with tempfile.TemporaryDirectory(prefix="os88rom-") as tmp:
        out = os.path.join(tmp, "tail.bin")
        cmd = ["nasm", "-f", "bin", "-w+error", f"-DTAIL_AT={at}",
               f"-DROM_KIND={kind}", f"-DBUILD_STR='{bstr or build_str()}'",
               *extra, "-o", out, os.path.join(ROOT, "boot", "osrom.asm")]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            fail("nasm refused boot/osrom.asm:\n" + r.stderr[-2000:])
        with open(out, "rb") as f:
            return f.read()


def sock_byte(o):
    """The socket-check pattern at window offset o - boot/osrom.asm's
    rom_sock_check computes the same: lo + 3*hi + 0x5A, mod 256."""
    return ((o & 0xFF) + 3 * (o >> 8) + 0x5A) & 0xFF


def layout(kind, payload_len, fill, extra=(), bstr=None):
    """The window with its header, payload and tail placed, balances unset.
    `fill(img, start, end)` writes the payload bytes."""
    tail0 = assemble_tail(kind, 0, extra, bstr)
    tail_at = ROM_SIZE - 1 - len(tail0)        # the last byte is bal2
    if HDR_SIZE + payload_len > tail_at:
        fail(f"the payload is {payload_len:,} bytes and the window holds "
             f"{tail_at - HDR_SIZE:,} after the header and the tail - "
             f"ROM-PLAN 3.6 is what to do about it")
    tail = assemble_tail(kind, tail_at, extra, bstr)
    if len(tail) != len(tail0):
        fail("boot/osrom.asm changed length with its origin")
    img = bytearray(b"\xFF" * ROM_SIZE)
    fill(img, HDR_SIZE, tail_at)
    img[tail_at:tail_at + len(tail)] = tail
    init = tail_at + 32                         # rom_init follows rom_id
    hdr = bytearray(HDR_SIZE)
    hdr[0:3] = bytes((0x55, 0xAA, DECL_BLOCKS))
    rel = (init - 6) & 0xFFFF                   # jmp near at +3, next ip +6
    hdr[3:6] = bytes((0xE9, rel & 0xFF, rel >> 8))
    hdr[6:8] = b"OS"
    hdr[8] = FMT                                # fmt and kind ADJACENT: the
    hdr[9] = kind                               # doorbell tests them as one word
    # +10: rom_patch, as a FAR POINTER the kernel's doorbell calls through
    # in one instruction (`call far [10]`, ROM_FMT 2). The offset is the
    # assembler's, out of rom_id+24; the segment is the window's
    hdr[10:12] = tail[24:26]
    hdr[12:14] = (ROM_BASE >> 4).to_bytes(2, "little")
    hdr[14] = 0                                 # bal1, below
    img[0:HDR_SIZE] = hdr
    # rom_id's tool-written fields (boot/osrom.asm: +10, +12, +14)
    img[tail_at + 10:tail_at + 12] = HDR_SIZE.to_bytes(2, "little")
    img[tail_at + 12:tail_at + 14] = payload_len.to_bytes(2, "little")
    img[tail_at + 14:tail_at + 16] = len(tail).to_bytes(2, "little")
    # +24 is rom_patch's offset (the assembler's); +26 its SEGMENT, which only
    # the window knows - stage 2 does `call far [id+24]`, so a zero here is a
    # far call into the interrupt table (it was, on the first kernel ROM)
    img[tail_at + 26:tail_at + 28] = (ROM_BASE >> 4).to_bytes(2, "little")
    return img, tail_at


def balance(img):
    """Set bal1 and bal2; then prove both sums and the GLaBIOS boundaries."""
    img[BAL1] = 0
    img[BAL1] = (-sum(img[0:DECL_SIZE])) & 0xFF
    img[-1] = 0
    img[-1] = (-sum(img[DECL_SIZE:])) & 0xFF
    assert sum(img[0:DECL_SIZE]) & 0xFF == 0
    assert sum(img[DECL_SIZE:]) & 0xFF == 0
    for o in range(DECL_SIZE, ROM_SIZE, 2048):
        if img[o] == 0x55 and img[o + 1] == 0xAA:
            fail(f"F{(ROM_BASE + o) >> 4:04X}:0000 begins 55 AA - GLaBIOS "
                 f"would take it for a second option ROM. Re-cut the payload "
                 f"(ROM-PLAN 1.3 point 4)")
    return img


def build_socket():
    def fill(img, a, b):
        for o in range(a, b):
            img[o] = sock_byte(o)
    img, tail_at = layout(KIND_SOCK, 0, fill)
    # ...the pattern runs to the tail, so payload_len is reported as what it
    # covers rather than zero
    img[tail_at + 12:tail_at + 14] = (tail_at - HDR_SIZE).to_bytes(2, "little")
    # F6000: jmp far F400:rom_stub, for a BIOS that never calls rom_init.
    # rom_stub is the 2nd routine in the tail; find it by its first bytes,
    # `sti / push cs / pop ds`, rather than trusting a hand count
    stub = img.find(bytes((0xFB, 0x0E, 0x1F)), tail_at)
    if stub < 0:
        fail("cannot find rom_stub in the tail")
    img[INT18_AT:INT18_AT + 5] = bytes((0xEA, stub & 0xFF, stub >> 8,
                                        (ROM_BASE >> 4) & 0xFF,
                                        ROM_BASE >> 12))
    return balance(img)


# --- THE KERNEL ROM (ROM-PLAN 3.4.3) ------------------------------------------
# A ROM_COLD kernel's `.cold` plus the tables boot/osrom.asm's rom_patch and
# rom_modfix need: every word the kernel uses to name `.cold`'s segment, by
# the segment it is an offset into; a hash over `.text` with the build
# number's words left out; and where rom_mfp is. All of it is derived from the
# kernel ASSEMBLY rather than from the binary alone, and the assembly is
# proved to be the binary's first.

FIX_A, FIX_B = 0xF601, 0xF702       # tools/os88romfix.py's pair
BN_SKIP_MAX = 16       # `.text` words the build number may own (kernel_tables)
KFLAGS = ["-DKZIP", "-DKZ_SECS=0", "-DKZ_RPARA=0", "-DKZ_NBLK=1",
          "-DKZ_HEADSEC=9"]          # the Makefile's first-pass kernel-full


def kflags(build):
    """The defines the build's kernel-full.bin was LAST assembled with. A
    packed kernel's is the Makefile's PASS 2 (SPEC.md 2.9.13), which overwrites
    kernel-full.bin with the packed file's numbers - and those reach more than
    `.boot2`: the overlay names stage-2 labels through CS (driver.inc's
    `and al, [cs:b2_cylok]`), and `.boot2` grows with them. kernel.kz.json is
    what pass 2 wrote, and os88kz.py --defines is the line it was given."""
    js = os.path.join(build, "kernel.kz.json")
    if not os.path.exists(js):
        return KFLAGS
    import json
    n = json.load(open(js))
    return ["-DKZIP", f"-DKZ_SECS={n['ksecs']}", f"-DKZ_RPARA={n['rpara']}",
            f"-DKZ_HEADSEC={n['headsecs']}", f"-DKZ_NBLK={n['nblk']}"]


def _sections(maptext):
    out = {}
    for m in re.finditer(r"^\s*([0-9A-F]+)\s+([0-9A-F]+)\s+([0-9A-F]+)\s+"
                         r"[0-9A-F]+\s+progbits\s+(\S+)\s*$", maptext, re.M):
        out[m[4]] = (int(m[2], 16), int(m[3], 16))
    return out


def _symbol(maptext, section, name):
    t = maptext[maptext.index("-- Symbols"):]
    i = t.index("---- Section " + section + " ")
    j = t.find("---- Section", i + 10)
    for line in t[i:j if j > 0 else None].splitlines():
        p = line.split()
        if len(p) == 3 and p[2] == name:
            return int(p[1], 16)
    fail(f"{name} is not a symbol of {section} in this kernel")


def _assemble(tmp, build, variant, defs=(), inc_first=None, mapkind=None):
    import hashlib  # noqa: F401  (kept local: the socket path never needs it)
    tag = "_".join(d.strip("-D").replace("=", "") for d in defs) or "plain"
    out = os.path.join(tmp, f"k_{tag}.bin")
    src = os.path.join(tmp, "kernel", "kernel.asm")
    if mapkind:
        mapf = os.path.join(tmp, f"k_{tag}.map")
        asm = os.path.join(tmp, "kernel", f"_rom_{tag}.asm")
        with open(src) as f, open(asm, "w") as g:
            g.write(f"[map {mapkind} {mapf}]\n" + f.read())
        src = asm
    incs = ["-I", os.path.join(tmp, "kernel") + "/",
            "-I", os.path.join(ROOT, "apps") + "/"]
    if inc_first:
        incs += ["-I", inc_first + "/"]
    incs += ["-I", build + "/"]
    vdefs = variant.split() if variant.startswith("-D") else [f"-D{variant}"]
    cmd = ["nasm", "-f", "bin", "-w+error", *incs, *vdefs,
           *kflags(build), *defs, "-o", out, src]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        fail("nasm refused the kernel:\n" + r.stderr[-2000:])
    with open(out, "rb") as f:
        data = f.read()
    if mapkind:
        with open(mapf) as f:
            return data, f.read()
    return data, None


def _section_symbols(maptext, section):
    t = maptext[maptext.index("-- Symbols"):]
    i = t.index("---- Section " + section + " ")
    j = t.find("---- Section", i + 10)
    out = []
    for line in t[i:j if j > 0 else None].splitlines():
        p = line.split()
        if len(p) == 3 and re.match("^[0-9A-F]+$", p[1]):
            out.append((int(p[1], 16), p[2]))
    return sorted(out)


DATA_DIR = re.compile(r"^\s*(?:times\s+[^;]+?\s+)?(?:db|dw|dd|dq|resb|resw|"
                      r"resd|incbin)\b", re.I)


def _data_labels():
    """Every kernel label whose line - or the first code line after it - is a
    data directive. Read off the source, so a new `.text` variable is left out
    of the hash without anybody listing it."""
    names = set()
    files = [os.path.join(ROOT, "kernel", f) for f in
             sorted(os.listdir(os.path.join(ROOT, "kernel")))
             if f.endswith((".inc", ".asm"))]
    for f in files:
        pending = None
        for line in open(f, errors="replace"):
            code = line.split(";", 1)[0].rstrip()
            if not code.strip():
                continue
            m = re.match(r"^([A-Za-z_]\w*):?(\s+.*)?$", code)
            if m and not code[0].isspace():
                rest = (m.group(2) or "").strip()
                if not rest:
                    if pending:         # a label that EMITS NOTHING (mod.inc's
                        names.add(pending)  # mod_tab_end, a pure marker) is
                    pending = m.group(1)    # not code either - left in, it
                    continue                # takes the next data's span
                if pending and re.match(r"equ\b", rest):
                    names.add(pending)
                    pending = None
                    continue
                if DATA_DIR.match(" " + rest):
                    names.add(m.group(1))
                pending = None
                continue
            if pending and (DATA_DIR.match(code) or
                            re.match(r"\s*section\b", code)):
                names.add(pending)
            pending = None
    return names


def _symbol_any(maptext, name):
    """A KERNEL_SEG offset: `.text` or `.bss` (vfollows `.text`, so its
    virtual addresses are offsets into the same segment)."""
    for sec in (".text", ".bss"):
        try:
            for v, n in _section_symbols(maptext, sec):
                if n == name:
                    return v
        except ValueError:
            pass
    fail(f"{name} is in neither .text nor .bss")


VGAB_LINE = "VGABUF_SEG  equ VGABUF_RAM\n"


def decouple_vgab(src):
    """os88romfix.decouple's shape for the planar decoder's segment."""
    if VGAB_LINE not in src:
        fail("kernel.asm's ladder no longer reads `VGABUF_SEG  equ VGABUF_RAM`"
             " - update VGAB_LINE")
    return src.replace(VGAB_LINE, "%ifdef VGAB_CS\nVGABUF_SEG equ VGAB_CS\n"
                       "%else\nVGABUF_SEG equ VGABUF_RAM\n%endif\n", 1)


def kernel_tables(build, variant):
    """Everything the ROM needs to know about the kernel `build` holds."""
    sys.path.insert(0, HERE)
    import os88mod
    import os88romfix
    full_path = os.path.join(build, "kernel-full.bin")
    if not os.path.exists(full_path):
        fail(f"no {full_path} - build that kernel first (`make small`)")
    with open(full_path, "rb") as f:
        full = f.read()
    with tempfile.TemporaryDirectory(prefix="os88rom-k-") as tmp:
        shutil.copytree(os.path.join(ROOT, "kernel"),
                        os.path.join(tmp, "kernel"))
        kp = os.path.join(tmp, "kernel", "kernel.asm")
        with open(kp) as f:
            src = f.read()
        with open(kp, "w") as f:
            f.write(decouple_vgab(os88romfix.decouple(src)))
        # THE OTHER BUILD NUMBER IS THE SAME LENGTH, every digit different.
        # BUILD_STR is spliced into `.text` (apps.inc's About line), so a
        # number one digit shorter moves every byte after it - and the diff
        # below then skips nearly all of `.text` as "the build number". That
        # is what build 1000 did with a fixed '999': the hash covered almost
        # nothing, its span list grew COLDHMA.SYS 2,919 -> 8,322 bytes, and
        # XMEM.DRV's 4KB read of it refused - `.cold` and every HMA driver
        # back in the heap, with nothing on the glass to say so
        # Build 0 is the exception: apps.inc's `%if BUILD_NUM` drops the line
        # altogether, so any other number ADDS it - the same move again. With
        # no number in `.text` there is nothing to leave out, so it is 0 too
        bnum = build_str(build)
        if not bnum.isdigit() or int(bnum) == 0:
            other = "0"
        else:
            other = "".join("2" if d == "1" else "1" for d in bnum)
        bn = os.path.join(tmp, "bn")
        os.makedirs(bn)
        with open(os.path.join(bn, "buildnum.inc"), "w") as f:
            f.write(f"%define BUILD_NUM {other}\n"
                    f"%define BUILD_STR '{other}'\n")
        # SIX ASSEMBLIES AND NONE NEEDS ANOTHER, so they run side by side:
        # each writes its own file, and the HMA image is built on every kern_big
        # tree's disks, where six in a row were fifteen seconds of every build
        from concurrent.futures import ThreadPoolExecutor
        jobs = [dict(mapkind="all"),
                dict(defs=(f"-DCOLD_CS=0x{FIX_A:04X}",)),
                dict(defs=(f"-DCOLD_CS=0x{FIX_B:04X}",)),
                dict(defs=(f"-DVGAB_CS=0x{FIX_A:04X}",)),
                dict(defs=(f"-DVGAB_CS=0x{FIX_B:04X}",)),
                dict(defs=("-DROMBN",), inc_first=bn)]
        with ThreadPoolExecutor(len(jobs)) as ex:
            res = list(ex.map(lambda kw: _assemble(tmp, build, variant, **kw),
                              jobs))
        (a, amap), (f1, _), (f2, _), (v1, _), (v2, _), (c, _) = res
    secs = _sections(amap)
    for need in (".text", ".cold", ".ovl", ".boot2"):
        if need not in secs:
            fail(f"the kernel has no {need} section")

    # THE ASSEMBLY IS THE BINARY: every section the ROM reasons about must be
    # byte-identical to the build's own kernel-full.bin. `.boot2` is not
    # compared - the Makefile's second pass re-assembles it with the packed
    # file's numbers - and nothing the ROM touches is in it
    if len(a) != len(full):
        fail(f"{full_path} is {len(full):,} bytes and this tree assembles "
             f"{len(a):,} - rebuild that kernel from this tree first")
    for name, (st, en) in secs.items():
        if name == ".boot2":
            continue
        if a[st:en] != full[st:en]:
            fail(f"{name} differs between this tree's assembly and "
                 f"{full_path} - rebuild that kernel from this tree first")

    tst, ten = secs[".text"]
    cst, cen = secs[".cold"]
    wst, wen = secs[".ovl"]          # the WINDOW half (SPEC.md 2.9.14.1)
    if cst < wst:
        fail("`.cold` sits below `.ovl` in this kernel's file - it was not "
             "built with ROM_COLD (kernel.asm), so nothing re-points it")
    with open(os.path.join(ROOT, "kernel", "kernel.asm")) as f:
        ksrc = f.read()
    kseg = int(re.search(r"^KERNEL_SEG\s+equ\s+(0x[0-9A-Fa-f]+)",
                         ksrc, re.M).group(1), 16)
    vst = cold_vstart(ksrc, variant)
    for what, off in (("`.cold`", cst - tst), ("`.ovl`", wst - tst)):
        if off % 16:
            fail(f"{what} does not start on a paragraph past `.text`")
    coldram = kseg + (cst - tst) // 16
    coldseg = coldram - vst // 16       # what every far reference names
    fatseg = kseg + (wst - tst) // 16

    _mapoff, rows = os88mod.read_map(a)
    lists = {"text": [], "win": [], "blob": [], "mods": {}}
    i = 0
    while i < len(f1):
        if f1[i] == f2[i]:
            i += 1
            continue
        w1 = f1[i] | f1[i + 1] << 8
        w2 = f2[i] | f2[i + 1] << 8
        if (w2 - w1) & 0xFFFF != FIX_B - FIX_A or w1 != FIX_A:
            fail(f"file offset {i:#x} moves with `.cold`'s segment but is not "
                 f"a plain word naming it - not a patchable site")
        if a[i] | a[i + 1] << 8 != coldseg:
            fail(f"file offset {i:#x} is a site but the plain kernel does not "
                 f"hold COLD_SEG ({coldseg:#06x}) there")
        if tst <= i < ten:
            lists["text"].append(i - tst)
        elif wst <= i < wen:
            lists["win"].append(i - wst)
        elif i < secs[".boot2"][1]:
            lists["blob"].append(i)         # `.boot2`, the blob: blob offsets
        elif cst <= i < cen:
            fail(f"`.cold` names its own segment at +{i - cst:#x} - "
                 f"`tools/os88romfix.py --check` (ROM-PLAN 3.2)")
        else:
            for n, (st, sz, _ne) in enumerate(rows):
                if st <= i < st + sz:
                    lists["mods"].setdefault(n, []).append(i - st)
                    break
            else:
                fail(f"a site at file offset {i:#x} is in no section the ROM "
                     f"can reach")
        i += 2

    # THE PLANAR DECODER'S BUFFERS (kern_big; ROM-PLAN 3.4.2): the words in
    # `.text` that name the `.vgabuf` rung, which the ROM moves to the bottom
    # of the dead cold rung so the heap floor falls past both. The same
    # two-assembly diff, one equate along; there is one site today
    # (vga12.inc's `mov ax, VGABUF_SEG`), and a NOPLANE kernel has none
    vseen = set()
    vgabram = coldram + (cen - cst + 511) // 512 * 32
    lists["vgab"] = []
    if len(v1) != len(a) or len(v2) != len(a):
        fail("moving `.vgabuf`'s segment changed the kernel's size")
    for i in range(len(v1) - 1):
        if v1[i] == v2[i]:
            continue
        if i and v1[i - 1] != v2[i - 1] and (i - 1) in vseen:
            continue
        w1 = v1[i] | v1[i + 1] << 8
        w2 = v2[i] | v2[i + 1] << 8
        if (w2 - w1) & 0xFFFF != FIX_B - FIX_A or w1 != FIX_A:
            fail(f"file offset {i:#x} moves with `.vgabuf`'s segment but is "
                 f"not a plain word naming it")
        if a[i] | a[i + 1] << 8 != vgabram:
            fail(f"file offset {i:#x} is a `.vgabuf` site but the plain kernel "
                 f"does not hold VGABUF_RAM ({vgabram:#06x}) there")
        if not tst <= i < ten:
            fail(f"a `.vgabuf` site at file offset {i:#x} is outside `.text` - "
                 f"the ROM moves only `.text`'s")
        lists["vgab"].append(i - tst)
        vseen.add(i)

    # the build number's words, out of the hash
    skip = set()
    for i in range(tst, ten):
        if a[i] != c[i]:
            skip.add((i - tst) // 2)
    # ...which is a handful of words - the About string's digits and
    # BUILD_NUM's word or two. Anything more is the build number having
    # MOVED code, and a hash over what is left would guard nothing
    if len(skip) > BN_SKIP_MAX:
        fail(f"{len(skip)} `.text` words change with the build number "
             f"(at most {BN_SKIP_MAX} expected) - the two assemblies are "
             f"not laid out alike, so the hash would cover almost nothing")
    # ...and `.text`'s DATA, which is out of it too: the boot writes it before
    # the ROM is asked (the boot timer, [spl_fseg], every vid_* the splash's
    # adapter probe fills in - measured on the first kernel ROM: 41 bytes, the
    # same on no two machines). The hash is about CODE - a routine that kept
    # its address and changed its contract - so a label declared with a data
    # directive is left out to the next label. One this misses makes the hash
    # disagree and the ROM REFUSE, which is the safe way round, and
    # tests/romsmall.py's leg A is what would show it.
    # THE ROWS, which are not the images: kern_big's settings core
    # (MOD_SETS, SPEC.md 2.8.7) is a row over CTRL.DRV's image. The ROM is
    # asked per ROW (mod_need hands it BX = the row), so every table it
    # indexes by row has one entry per row - mod_tab to mod_tab_end - and a
    # row's image is the one whose file name it points at. The first W3 ROM
    # sized its tables by image, and refused the settings core outright.
    modtab = _symbol_any(amap, "mod_tab")
    nrows = (_symbol_any(amap, "mod_tab_end") - modtab) // 4
    text_b = a[tst:ten]
    def cstr(off):
        e = text_b.index(0, off)
        return text_b[off:e].decode("ascii")
    fileof = [text_b[modtab + 4 * r + 2] | text_b[modtab + 4 * r + 3] << 8
              for r in range(nrows)]
    if nrows < len(rows):
        fail(f"mod_tab has {nrows} rows and the kernel cuts {len(rows)} "
             f"images - a row per image is the least there can be")
    rowimg = []
    for r in range(nrows):
        if r < len(rows):
            rowimg.append(r)
            continue
        same = [j for j in range(len(rows)) if fileof[j] == fileof[r]]
        if len(same) != 1:
            fail(f"mod_tab row {r} names {cstr(fileof[r])!r}, which is no "
                 f"one image's file")
        rowimg.append(same[0])
    names = [cstr(fileof[j]) for j in range(len(rows))]
    images = [bytes(a[st:st + sz]) for st, sz, _ne in rows]

    tsyms = _section_symbols(amap, ".text")
    data = _data_labels()
    addrs = [v for v, _ in tsyms]
    for k, (v, name) in enumerate(tsyms):
        if name.split(".")[0] in data and "." not in name:
            end = addrs[k + 1] if k + 1 < len(addrs) else ten - tst
            for w in range(v // 2, (end + 1) // 2):
                skip.add(w)
    nwords = (ten - tst) // 2
    spans, w = [], 0
    while w < nwords:
        if w in skip:
            w += 1
            continue
        s0 = w
        while w < nwords and w not in skip:
            w += 1
        spans.append((s0 * 2, w - s0))
    text = a[tst:ten]
    h = text_hash(text, spans)
    return {"coldram": coldram, "coldseg": coldseg, "fatseg": fatseg,
            "kseg": kseg, "vst": vst,
            "vgabram": vgabram,
            "cold": a[cst:cen], "hash": h, "spans": spans, "lists": lists,
            "nmods": len(rows), "mfp": _symbol(amap, ".text", "rom_mfp"),
            "fseg": _symbol(amap, ".text", "spl_fseg"),
            # ROMF_PKG ends in a near jump to it (osrom.asm's rom_pkg): its
            # offset in `.cold`, which is the ROM's own segment on kern_small
            "ldfpkg": _symbol(amap, ".cold", "ldf_ld_pkg_start"),
            "uidesc": _symbol(amap, ".text", "ui_desc"),   # ...and whose name
            # mod_check's header, whose build word rom_modfix compares a held
            # image's against: a kernel one commit newer reads the disk
            "hdrt": _symbol(amap, ".text", "mod_hdrt"),
            "modfp": _symbol_any(amap, "mod_fp"),
            "modfpend": _symbol_any(amap, "mod_fp_end"),
            "modtab": modtab, "rowimg": rowimg, "names": names,
            "images": images,
            "text": text, "skipwords": sorted(skip)}


def cold_vstart(ksrc, variant):
    """kernel.asm's COLD_VSTART for this variant: kern_big's is HMA_COLD_AT
    (the top of the HMA, docs/plans/HMA-PLAN.md 5), kern_small's 0x10."""
    if "KERN_SMALL" in variant:
        vst = int(re.search(r"^COLD_VSTART\s+equ\s+(0x[0-9A-Fa-f]+)",
                            ksrc, re.M).group(1), 16)
    else:
        # TWO lines: a knob kernel's first (inside %ifdef KERN_KNOB), the
        # shipped one's second - kernel.asm says why
        knob, ship = [int(v, 16) for v in re.findall(
            r"^HMA_COLD_AT\s+equ\s+(0x[0-9A-Fa-f]+)", ksrc, re.M)]
        vst = knob if "KERN_KNOB" in variant else ship
    if vst < 0x10 or vst % 16:
        fail(f"COLD_VSTART is {vst:#x} - it must be a paragraph, at least "
             f"0x10: FFFF:0000-000F is the BIOS's, and in ROM the header is "
             f"the sixteen bytes under `.cold`'s first")
    return vst


def text_hash(text, spans):
    """boot/osrom.asm's rom_patch step 2, in Python: h = rol(h, 1) ^ word."""
    h = 0
    for st, n in spans:
        for k in range(n):
            wd = text[st + 2 * k] | text[st + 2 * k + 1] << 8
            h = ((h << 1) | (h >> 15)) & 0xFFFF
            h ^= wd
    return h


def write_romtab(t, path, held=None, pkg=None, hdrv=(), drvorg=0):
    """`held` = {image index: offset in the window}; `pkg` = (8.3 name,
    offset, bytes) or None. Neither changes the tail's LENGTH except a pkg
    of None, which takes rom_pkg's body out (shorter, never longer). `hdrv`
    is the HMA image's driver tables (hma_driver), `drvorg` their origin."""
    held = held or {}
    L = t["lists"]
    out = [f"; generated by tools/os88rom.py - ROM-PLAN 3.4.3. Do not edit.",
           f"RT_COLDVST  equ 0x{t['vst']:04X}",
           f"RT_DRVORG   equ 0x{drvorg:04X}",
           f"RT_KSEG     equ 0x{t['kseg']:04X}",
           f"RT_FATSEG   equ 0x{t['fatseg']:04X}",
           f"RT_COLDRAM  equ 0x{t['coldram']:04X}",
           f"RT_COLDSEG  equ 0x{t['coldseg']:04X}",
           f"RT_COLDLEN  equ {len(t['cold'])}",
           f"RT_HASH     equ 0x{t['hash']:04X}",
           f"RT_MFP      equ 0x{t['mfp']:04X}",
           f"RT_FSEG     equ 0x{t['fseg']:04X}",
           f"RT_MODTAB   equ 0x{t['modtab']:04X}",
           f"RT_MODFP    equ 0x{t['modfp']:04X}",
           f"RT_NSLOT    equ {(t['modfpend'] - t['modfp']) // 4}",
           f"RT_MODRSZ   equ 4",
           f"RT_NTEXT    equ {len(L['text'])}",
           f"RT_NWIN     equ {len(L['win'])}",
           f"RT_NBLOB    equ {len(L['blob'])}",
           f"RT_VGABRAM  equ 0x{t['vgabram']:04X}",
           f"RT_NVGAB    equ {len(L['vgab'])}",
           f"RT_NMODS    equ {len(t['rowimg'])}",
           f"RT_PKGOFF   equ 0x{pkg[1] if pkg else 0:04X}",
           f"RT_PKGLEN   equ {pkg[2] if pkg else 0}",
           f"RT_LDFPKG   equ 0x{t['ldfpkg']:04X}",
           f"RT_UIDESC   equ 0x{t['uidesc']:04X}",
           f"RT_HDRT     equ 0x{t['hdrt']:04X}"]

    def words(label, ws):
        out.append(f"{label}:")
        for k in range(0, len(ws), 8):
            out.append("    dw " + ", ".join(f"0x{x:04X}" for x in ws[k:k + 8]))

    # TWO MACROS, which boot/osrom.asm places: what the installer reads once,
    # and what the module door and the HMA's pool read for the session - the
    # HMA image copies only the second up (rom_res)
    out.append("%macro RT_INSTALL_DATA 0")
    out.append("rt_spans:")
    for st, n in t["spans"]:
        out.append(f"    dw 0x{st:04X}, {n}")
    out.append("rt_spans_end:")
    words("rt_text", L["text"])
    words("rt_win", L["win"])
    words("rt_blob", L["blob"])
    words("rt_vgab", L["vgab"])
    out.append("%endmacro")
    out.append("%macro RT_RES_DATA 0")
    out.append("rt_mods:")                  # per ROW (see kernel_tables)
    for n in t["rowimg"]:
        out.append(f"    dw rt_mod{n}, {len(L['mods'].get(n, []))}")
    for n in range(t["nmods"]):
        words(f"rt_mod{n}", L["mods"].get(n, []))
    out.append("rt_img:")                   # ...and the image each row has
    for n in t["rowimg"]:
        if n in held:
            out.append(f"    dw 0x{held[n]:04X}, {len(t['images'][n])}")
        else:
            out.append("    dw 0, 0")
    out.append("rt_pkgname:")
    out.append(f"    db '{pkg[0] if pkg else ''}', 0")
    # THE HMA'S DRIVERS (docs/plans/HMA-PLAN.md 5): per driver, the hash of
    # the org-0 image the kernel loads, its image and image + bss lengths, and
    # its relocation stream - each byte a step from the last relocated byte, 0
    # meaning an absolute word follows - with the count of bytes it moves
    out.append("rt_hdrv:")
    for k, d in enumerate(hdrv):
        out.append(f"    dw 0x{d['hash']:04X}, {d['img']}, {d['total']}, "
                   f"rt_hrel{k}, {len(d['rel'])}   ; {d['name']}")
    out.append("rt_hdrv_end:")
    for k, d in enumerate(hdrv):
        out.append(f"rt_hrel{k}:")
        enc = rel_stream(d["rel"])
        for j in range(0, len(enc), 16):
            out.append("    db " + ", ".join(str(x) for x in enc[j:j + 16]))
    out.append("%endmacro")
    with open(path, "w") as f:
        f.write("\n".join(out) + "\n")


# WHAT ELSE RIDES IN THE WINDOW (ROM-PLAN 4.3, SPEC.md 2.10.5), in the order
# it is worth having. The first two are why the rest is worth doing at all: on
# kern_small they are the Standard File dialog and Cut/Copy/Paste, which are
# read off the system disk and so refuse a Save As onto a data floppy in a
# one-drive machine. Whatever fits after `.cold` and the tail goes in, greedily
# in this order, and whatever does not is read off the disk as it always was -
# so a `.cold` that grows takes a module out of the ROM rather than failing it.
PRIORITY = ("FDLG.DRV", "FILECP.DRV", "CTRL.DRV", "*PKG*", "FORMAT.DRV",
            "CLONE.DRV", "EXTD.DRV", "DOCK.DRV", "HIBER.DRV")


def pkg_image(path):
    """A package for ROMF_PKG: the IMAGE the loader runs (unpacked), never a
    package with parts - OSAPI_PKG_START's image arm refuses those."""
    sys.path.insert(0, HERE)
    import os88pkg
    img = os88pkg.image_unwrap(open(path, "rb").read())
    if img[3] & 4:
        fail(f"{path} has parts (SPEC.md 20.12) and the image arm a ROM "
             f"launches through refuses them")
    name = os.path.basename(path).upper()
    if len(name) > 12 or not re.fullmatch(r"[A-Z0-9_]{1,8}\.O88", name):
        fail(f"{name} is not an 8.3 package name")
    return name, img


def build_kernel(build, variant, pkg=None):
    t = kernel_tables(build, variant)
    cold = t["cold"]
    p = pkg_image(pkg) if pkg else None
    extra_of = lambda tab: (f'-DROMTAB="{tab}"',)
    with tempfile.TemporaryDirectory(prefix="os88rom-t-") as tmp:
        tab = os.path.join(tmp, "romtab.inc")
        # the tail's length does not depend on WHICH images are held (every
        # row has its entry either way), so measure it once at its largest
        write_romtab(t, tab, {n: 1 for n in range(t["nmods"])},
                     (p[0], 1, len(p[1])) if p else None)
        tail0 = assemble_tail(KIND_KERNEL, 0, extra_of(tab),
                              build_str(build))
        room = ROM_SIZE - 1 - len(tail0) - HDR_SIZE - len(cold)
        byname = {nm: n for n, nm in enumerate(t["names"])}
        held, payload, pk = {}, bytearray(cold), None
        for what in PRIORITY:
            if what == "*PKG*":
                if p and len(p[1]) <= room:
                    pk = (p[0], HDR_SIZE + len(payload), len(p[1]))
                    payload += p[1]
                    room -= len(p[1])
                continue
            n = byname.get(what)
            if n is None or len(t["images"][n]) > room:
                continue
            held[n] = HDR_SIZE + len(payload)
            payload += t["images"][n]
            room -= len(t["images"][n])
        write_romtab(t, tab, held, pk)

        def fill(img, a, b):
            img[a:a + len(payload)] = payload
        img, tail_at = layout(KIND_KERNEL, len(payload), fill,
                              extra=extra_of(tab), bstr=build_str(build))
    t["held"] = held
    t["pkg"] = pk
    t["pkgwant"] = p[0] if p else None
    return balance(img), t


def model_adopt(img, kernel, blob, t):
    """rom_patch, in Python, against an expanded kernel - the ROM's three
    tests and its patch. `kernel` is the bytes at KERNEL_SEG (patched in
    place); returns True when the ROM would adopt it."""
    cold = img[HDR_SIZE:HDR_SIZE + len(t["cold"])]
    off = (t["coldram"] - t["kseg"]) * 16
    if kernel[off:off + len(cold)] != cold:
        return False
    if text_hash(kernel, t["spans"]) != t["hash"]:
        return False
    fat = (t["fatseg"] - t["kseg"]) * 16
    sites = ([(kernel, o) for o in t["lists"]["text"]]
             + [(kernel, fat + o) for o in t["lists"]["win"]]
             + [(blob, o) for o in t["lists"]["blob"]])
    if any(b[o] | b[o + 1] << 8 != t["coldseg"] for b, o in sites):
        return False
    if any(kernel[o] | kernel[o + 1] << 8 != t["vgabram"]
           for o in t["lists"]["vgab"]):
        return False
    rcs = (ROM_BASE >> 4) - (t["vst"] - 0x10) // 16  # `.cold` at F4010
    for b, o in sites:
        b[o:o + 2] = rcs.to_bytes(2, "little")
    for o in t["lists"]["vgab"]:
        kernel[o:o + 2] = t["coldram"].to_bytes(2, "little")
    return True


def verify_model(build, img, t):
    """Run rom_patch's Python model against the kernel this ROM was cut from,
    as stage 2 leaves it: it must ADOPT that one, and REFUSE it with one code
    byte of `.text` changed and with one byte of `.cold` changed. Each answer
    is a property of the tables the ROM carries, asked before it ships."""
    probs = []
    kb = open(os.path.join(build, "kernel.bin"), "rb").read()
    # the blob is the file's head up to `.text`, which starts where the
    # kernel's first .text byte sits; find it from the cold image instead of
    # restating BOOT2_PAD: `.cold`'s RAM offset is known from the tables
    off = (t["coldram"] - t["kseg"]) * 16
    pos = kb.find(bytes(t["cold"][:64]))
    if pos < 0:
        return ["the ROM's .cold is not in kernel.bin at all"]
    b2 = pos - off
    kern, blob = bytearray(kb[b2:]), bytearray(kb[:b2])
    if not model_adopt(img, bytearray(kern), bytearray(blob), t):
        probs.append("the model refuses the kernel this ROM was cut from")
    skip = set(t["skipwords"])
    code = next(w for w in range(len(t["text"]) // 2) if w not in skip)
    k2 = bytearray(kern)
    k2[code * 2] ^= 0x01
    if model_adopt(img, k2, bytearray(blob), t):
        probs.append("the model adopts a kernel with a code byte changed")
    k3 = bytearray(kern)
    k3[off + 100] ^= 0x01
    if model_adopt(img, k3, bytearray(blob), t):
        probs.append("the model adopts a kernel whose .cold differs")
    # RT_HDRT IS mod_hdrt: rom_modfix answers SIZE and COPY only when a held
    # image's build word matches the one there, so a wrong offset would
    # send every module to the disk (or worse, accept a stale one)
    hd = t["hdrt"]
    if bytes(t["text"][hd:hd + 2]) != b"\x4F\x38":
        probs.append(f"RT_HDRT ({hd:#06x}) does not name mod_hdrt's magic")
    for n in t["held"]:
        if bytes(t["images"][n][4:6]) != bytes(t["text"][hd + 4:hd + 6]):
            probs.append(f"the ROM's {t['names'][n]} is not this build's - "
                         f"rom_modfix would never hand it out")
    # A HELD MODULE IS THE DISK'S, BYTE FOR BYTE: what ROMF_COPY hands
    # mod_need must be what dskw_read_x would have, or a ROM machine runs a
    # different Control Panel from the one on the floppy
    sys.path.insert(0, HERE)
    import os88drv
    for n, at in t["held"].items():
        f = os.path.join(build, t["names"][n].lower())
        if not os.path.exists(f):
            probs.append(f"no {f} to compare the ROM's {t['names'][n]} with")
            continue
        disk = os88drv.image_unwrap(open(f, "rb").read())
        if bytes(img[at:at + len(t["images"][n])]) != disk:
            probs.append(f"the ROM's {t['names'][n]} is not {f}'s image")
    return probs


# --- THE HMA IMAGE (docs/plans/HMA-PLAN.md) -----------------------------------
# The same adapter, for a 286 or better: XMEM.DRV reads this file at attach,
# once it has verified A20 and claimed the HMA, and far-calls it. It carries
# no `.cold` - the kernel's own is in RAM already and the tail copies it up to
# FFFF:0010 - so it is the tail alone, assembled where it will sit: straight
# after `.cold` in the HMA, on a paragraph, because XMEM.DRV calls it from the
# claim it read it into with CS = that claim less TAIL_AT/16.

def img_hash(b):
    """boot/osrom.asm's hb_place hash over a driver image: h = rol(h, 1) ^
    word, and an odd last byte as a word of its own."""
    h = 0
    for k in range(0, len(b) - 1, 2):
        h = (((h << 1) | (h >> 15)) & 0xFFFF) ^ (b[k] | b[k + 1] << 8)
    if len(b) & 1:
        h = (((h << 1) | (h >> 15)) & 0xFFFF) ^ b[-1]
    return h


def rel_stream(rel):
    """hb_place's relocation stream: a byte is a step of 1..255 from the
    last byte moved (the first from 0), 0 is an absolute word after it."""
    out, last = [], 0
    for p in rel:
        d = p - last
        if 0 < d < 256:
            out.append(d)
        else:
            out += [0, p & 0xFF, p >> 8]
        last = p
    return out


def hma_driver(build, name, org):
    """One driver the HMA image may place: <name>.bin (org 0, as nasm left
    it), <name>.hma.bin (org `org`) and <name>.drv (what the kernel loads).
    The relocation is every byte that differs - and each must differ by
    exactly org's high byte, or the driver is not relocatable this way."""
    sys.path.insert(0, HERE)
    import os88drv
    paths = [os.path.join(build, f"{name}{x}") for x in
             (".bin", ".hma.bin", ".drv")]
    for p in paths:
        if not os.path.exists(p):
            fail(f"no {p} - the HMA image needs all three of {name}'s")
    b0, bh = open(paths[0], "rb").read(), open(paths[1], "rb").read()
    img = os88drv.image_unwrap(open(paths[2], "rb").read())
    if len(b0) != len(bh):
        fail(f"{name}: its org-{org:#x} assembly is {len(bh):,} bytes against "
             f"{len(b0):,} - something in it depends on where it is")
    hi = org >> 8
    rel = []
    for k in range(len(b0)):
        if b0[k] == bh[k]:
            continue
        if (bh[k] - b0[k]) & 0xFF != hi:
            fail(f"{name}: byte {k:#x} moves with the origin by "
                 f"{(bh[k] - b0[k]) & 0xFF:#04x}, not {hi:#04x} - a byte "
                 f"relocation the tail cannot make")
        rel.append(k)
    bssp = img[31]
    total = len(img) + bssp * 16
    if total != len(b0):
        fail(f"{name}: {name}.drv loads {total:,} bytes and {name}.bin is "
             f"{len(b0):,} - not the same driver")
    if total > 0x10000 - org:
        fail(f"{name}: {total:,} bytes do not fit a block of "
             f"{0x10000 - org:,} (offsets from {org:#x} to FFFF)")
    # what the tail does, done here, must be the org-`org` driver: the loaded
    # image, bss zeroed, with every listed byte moved - against nasm's copy,
    # bar the two header fields os88drv.py rewrites (+8 the file, +31 the bss)
    run = bytearray(img) + bytes(bssp * 16)
    for k in rel:
        run[k] = (run[k] + hi) & 0xFF
    bad = [k for k in range(total) if k not in (8, 9, 31) and run[k] != bh[k]]
    if bad:
        fail(f"{name}: the relocated image differs from its org-{org:#x} "
             f"assembly at {len(bad)} byte(s), first {bad[0]:#x}")
    return {"name": name.upper() + ".DRV", "hash": img_hash(img),
            "img": len(img), "total": total, "rel": rel}


def build_hma(build, variant, drivers=(), org=0xE000):
    """`variant` is KERN_BIG, or the Makefile's whole $(VIDDEF) - a knob
    tree's kernel is only reproduced by the defines it was built with.
    `drivers` are the names whose relocation tables the tail carries."""
    t = kernel_tables(build, variant)
    with open(os.path.join(ROOT, "kernel", "kernel.asm")) as f:
        korg = int(re.search(r"^DRV_HMA_ORG\s+equ\s+(0x[0-9A-Fa-f]+)",
                             f.read(), re.M).group(1), 16)
    if drivers and org != korg:
        fail(f"the drivers were assembled at {org:#x} and kernel.asm's "
             f"DRV_HMA_ORG is {korg:#x}")
    hdrv = [hma_driver(build, n, korg) for n in drivers]
    if len({d["hash"] for d in hdrv}) != len(hdrv):
        fail("two of the HMA's drivers hash alike - hb_place could not tell "
             "them apart")
    # THE HALF THAT STAYS (rom_res) goes on the paragraph after `.cold`; the
    # installer half before it lands wherever that puts it, `.cold`'s own
    # offsets included, because it only ever runs from XMEM.DRV's claim. So
    # the file is assembled once at 0 to learn where rom_res falls in it, and
    # again with its ORIGIN that far below the paragraph after `.cold`
    res_at = (t["vst"] + len(t["cold"]) + 15) & ~15
    with tempfile.TemporaryDirectory(prefix="os88rom-h-") as tmp:
        tab = os.path.join(tmp, "romtab.inc")
        write_romtab(t, tab, {}, None, hdrv, korg)
        ex = (f'-DROMTAB="{tab}"',)
        img0 = assemble_tail(KIND_HMA, 0, ex, build_str(build))
        res_off = int.from_bytes(img0[26:28], "little")
        at = res_at - res_off
        if at < 0 or at % 16 or res_off % 16:
            fail(f"the HMA image's resident half is {res_off:#x} into the file "
                 f"and `.cold` ends at {res_at:#x} - no paragraph origin fits")
        img = assemble_tail(KIND_HMA, at, ex, build_str(build))
    if len(img) != len(img0):
        fail("boot/osrom.asm changed length with its origin")
    if at + len(img) > 0x10000:
        fail(f"`.cold` ({len(t['cold']):,} bytes) from {t['vst']:#06x} and the "
             f"HMA tail's resident half ({len(img) - res_off:,}) run "
             f"{at + len(img) - 0x10000:,} bytes past FFFF - lower "
             f"kernel.asm's HMA_COLD_AT, which gives the drivers' pool to "
             f"`.cold` (docs/plans/HMA-PLAN.md 5)")
    t["hdrv"] = hdrv
    t["res"] = (res_at, len(img) - res_off)
    return bytearray(img), at, t


def model_hma(img, at, kernel, blob, t):
    """The HMA kind of rom_patch, in Python: its tests and its patch, against
    an expanded kernel. True when it would install."""
    if img[0:8] != b"OS88ROM\0" or img[8] != KIND_HMA:
        return False
    if int.from_bytes(img[10:12], "little") != at:
        return False
    if text_hash(kernel, t["spans"]) != t["hash"]:
        return False
    sites = ([(kernel, o) for o in t["lists"]["text"]]
             + [(blob, o) for o in t["lists"]["blob"]])
    if any(b[o] | b[o + 1] << 8 != t["coldseg"] for b, o in sites):
        return False
    if any(kernel[o] | kernel[o + 1] << 8 != t["vgabram"]
           for o in t["lists"]["vgab"]):
        return False
    for b, o in sites:
        b[o:o + 2] = HMA_SEG.to_bytes(2, "little")
    for o in t["lists"]["vgab"]:
        kernel[o:o + 2] = t["coldram"].to_bytes(2, "little")
    return True


def verify_hma(build, img, at, t):
    probs = []
    kb = open(os.path.join(build, "kernel.bin"), "rb").read()
    off = (t["coldram"] - t["kseg"]) * 16
    pos = kb.find(bytes(t["cold"][:64]))
    if pos < 0:
        return ["the tables' .cold is not in kernel.bin at all"]
    b2 = pos - off
    kern, blob = bytearray(kb[b2:]), bytearray(kb[:b2])
    if not model_hma(img, at, bytearray(kern), bytearray(blob), t):
        probs.append("the model refuses the kernel this image was cut from")
    skip = set(t["skipwords"])
    code = next(w for w in range(len(t["text"]) // 2) if w not in skip)
    k2 = bytearray(kern)
    k2[code * 2] ^= 0x01
    if model_hma(img, at, k2, bytearray(blob), t):
        probs.append("the model installs over a kernel with a code byte "
                     "changed")
    if t["lists"]["win"] and not t["lists"]["text"]:
        probs.append("no `.text` site at all - the lists are not this kernel's")
    return probs


def write(img, out, name):
    os.makedirs(out, exist_ok=True)
    whole = os.path.join(out, name + ".bin")
    with open(whole, "wb") as f:
        f.write(img)
    for i, sock in enumerate(SOCKETS):
        with open(os.path.join(out, f"{name}-{sock}.bin"), "wb") as f:
            f.write(img[i * SOCK_SIZE:(i + 1) * SOCK_SIZE])
    return whole


def check(img):
    """What the two BIOSes will ask of the window, asked here."""
    probs = []
    if img[0:2] != b"\x55\xAA":
        probs.append("no 55 AA at F4000")
    if img[2] != DECL_BLOCKS:
        probs.append(f"declares {img[2]} blocks, not {DECL_BLOCKS}")
    if sum(img[0:DECL_SIZE]) & 0xFF:
        probs.append("F4000-FBFFF does not sum to zero")
    if sum(img[DECL_SIZE:]) & 0xFF:
        probs.append("FC000-FDFFF does not sum to zero")
    if img[3] != 0xE9:
        probs.append("+3 is not a near jmp")
    init = (int.from_bytes(img[4:6], "little") + 6) & 0xFFFF
    rid = init - 32                             # rom_init follows rom_id
    if not 0 <= rid < ROM_SIZE or img[rid:rid + 8] != b"OS88ROM\0":
        probs.append("the init jmp does not land 32 bytes past rom_id")
    if img[6:8] != b"OS" or img[8] != FMT:
        probs.append("the header is not 'OS' in format %d" % FMT)
    if (img[10:12] != img[rid + 24:rid + 26]
            or int.from_bytes(img[12:14], "little") != ROM_BASE >> 4):
        probs.append("+10 is not a far pointer to rom_patch in the window - "
                     "the kernel's doorbell far-calls it")
    return probs


def selfcheck():
    img = build_socket()
    probs = check(img)
    # the pattern the ROM's own loop recomputes, re-derived independently
    tail_at = (int.from_bytes(img[4:6], "little") + 6 - 32) & 0xFFFF
    bad = [o for o in range(HDR_SIZE, tail_at)
           if not (INT18_AT <= o < INT18_AT + 5) and img[o] != sock_byte(o)]
    if bad:
        probs.append(f"{len(bad)} pattern byte(s) wrong, first at {bad[0]:#x}")
    # THE DOOR'S FUNCTION NUMBERS are typed out twice, kernel/kernel.asm's
    # ROMF_* and boot/osrom.asm's, and tests/unit/t_mirror.py does not read
    # osrom.asm - so a renumbering on one side (kernel size pass 12 moved FIX
    # to 3, which the kernel's rom_none depends on) would assemble on both
    def romf(path):
        with open(os.path.join(ROOT, path)) as f:
            return dict(re.findall(r"^(ROMF_\w+)\s+equ\s+(\w+)", f.read(), re.M))
    k, r = romf("kernel/kernel.asm"), romf("boot/osrom.asm")
    if not k or k != r:
        probs.append(f"ROMF_* differ: kernel.asm {k}, osrom.asm {r}")
    if probs:
        fail("selfcheck: " + "; ".join(probs))
    print(f"os88rom: selfcheck ok - socket ROM {len(img):,} bytes, tail at "
          f"{tail_at:#06x} ({ROM_SIZE - tail_at - 1} bytes), both sums zero")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("kind", nargs="?", choices=("socket", "kernel", "hma"))
    ap.add_argument("--build", help="the kernel tree (kernel kind): its "
                    "kernel-full.bin and generated includes")
    ap.add_argument("--small", action="store_true",
                    help="the tree holds kern_small (default kern_big)")
    ap.add_argument("--pkg", help="a package (.o88) for the ROM to carry, "
                    "launched through OSAPI_PKG_START's image arm - kern_small "
                    "alone has that arm (ROM_PKG, SPEC.md 2.10.5)")
    ap.add_argument("--name", help="output name (default osrom-<variant>)")
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "rom"))
    ap.add_argument("--defines", help="hma: the defines the tree's kernel was "
                    "assembled with ($(VIDDEF)), so a knob tree's image is cut "
                    "from the kernel it has (default -DKERN_BIG)")
    ap.add_argument("--drivers", help="hma: the drivers whose relocation "
                    "tables it carries - <name>.bin, <name>.hma.bin and "
                    "<name>.drv in --build (docs/plans/HMA-PLAN.md 5)")
    ap.add_argument("--drv-org", default="0xE000",
                    help="hma: the origin the .hma.bin copies were "
                    "assembled at (kernel.asm's DRV_HMA_ORG)")
    ap.add_argument("--selfcheck", action="store_true")
    a = ap.parse_args()
    if a.selfcheck:
        selfcheck()
        return
    if a.kind == "socket":
        img = build_socket()
        probs = check(img)
        if probs:
            fail("; ".join(probs))
        path = write(img, a.out, "osrom-socket")
        print(f"os88rom: {os.path.relpath(path, ROOT)} and its five sockets - "
              f"the socket-check ROM, build {build_str()}")
        return
    if a.kind == "kernel":
        if not a.build:
            ap.error("kernel needs --build <tree>")
        variant = "KERN_SMALL" if a.small else "KERN_BIG"
        if a.pkg and not a.small:
            ap.error("--pkg is kern_small's: kern_big has no ROM_PKG arm")
        img, t = build_kernel(os.path.abspath(a.build), variant, a.pkg)
        probs = check(img) + verify_model(os.path.abspath(a.build), img, t)
        if probs:
            fail("; ".join(probs))
        name = a.name or ("osrom-small" if a.small else "osrom-big")
        path = write(img, a.out, name)
        tail_at = (int.from_bytes(img[4:6], "little") + 6 - 32) & 0xFFFF
        L = t["lists"]
        print(f"os88rom: {os.path.relpath(path, ROOT)} and its five sockets - "
              f"{variant}'s .cold ({len(t['cold']):,} bytes) at F400, build "
              f"{build_str(a.build)}; {len(L['text'])}+{len(L['win'])}+"
              f"{len(L['blob'])} kernel sites + {len(L['vgab'])} `.vgabuf`, "
              f"{sum(len(v) for v in L['mods'].values())} in "
              f"{t['nmods']} modules")
        held = [t["names"][n] for n in sorted(t["held"], key=t["held"].get)]
        if t["pkg"]:
            held.append(t["pkg"][0])
        used = HDR_SIZE + len(t["cold"]) + sum(
            len(t["images"][n]) for n in t["held"]) + (
            t["pkg"][2] if t["pkg"] else 0)
        print(f"os88rom: it also holds {', '.join(held) or 'nothing else'}"
              f"{'' if not t['pkgwant'] or t['pkg'] else ' - and NOT ' + t['pkgwant'] + ', which did not fit'}"
              f"; {tail_at - used:,} bytes of the window spare")
        return
    if a.kind == "hma":
        if not a.build:
            ap.error("hma needs --build <tree>")
        if a.small:
            ap.error("hma is kern_big's: kern_small has no XMEM.DRV and no "
                     "286 to run on")
        drivers = [d for d in re.split(r"[\s,]+", a.drivers or "") if d]
        img, at, t = build_hma(os.path.abspath(a.build),
                               a.defines.strip() if a.defines else "KERN_BIG",
                               drivers, int(a.drv_org, 0))
        probs = verify_hma(os.path.abspath(a.build), img, at, t)
        if probs:
            fail("; ".join(probs))
        # XMEM.DRV reads the file into a claim of XM_HMAF_KB and calls it
        # there; a file past that is a read that refuses at attach, and the
        # machine boots with `.cold` and every HMA driver in the heap
        with open(os.path.join(ROOT, "drivers", "xmem", "xmem.asm")) as f:
            hkb = int(re.search(r"^XM_HMAF_KB\s+equ\s+(\d+)", f.read(),
                                re.M).group(1))
        if len(img) > hkb * 1024:
            fail(f"the HMA image is {len(img):,} bytes and XMEM.DRV reads "
                 f"it into {hkb * 1024:,} (xmem.asm's XM_HMAF_KB) - it "
                 f"would never be installed")
        os.makedirs(a.out, exist_ok=True)
        path = os.path.join(a.out, a.name or "coldhma.sys")
        with open(path, "wb") as f:
            f.write(img)
        L = t["lists"]
        res_at, res_len = t["res"]
        print(f"os88rom: {os.path.relpath(path, ROOT)} - the HMA image for "
              f"build {build_str(a.build)}: {len(img):,} bytes, of which "
              f"{res_len:,} stay at FFFF:{res_at:04X}, after .cold's "
              f"{len(t['cold']):,} from {t['vst']:04X}; "
              f"{len(L['text'])}+{len(L['blob'])} kernel sites + "
              f"{len(L['vgab'])} `.vgabuf`, "
              f"{sum(len(v) for v in L['mods'].values())} in {t['nmods']} "
              f"modules; {0x10000 - res_at - res_len:,} bytes over it spare")
        pool = t["vst"] - 0x10
        print(f"os88rom: ...and the pool under .cold, FFFF:0010-"
              f"{t['vst'] - 1:04X}, {pool:,} bytes for "
              + (", ".join(f"{d['name']} ({d['total']:,}, "
                           f"{len(d['rel'])} relocations)"
                           for d in t["hdrv"]) or "no driver")
              + f" and their buffers")
        return
    ap.error("say which ROM to build")


if __name__ == "__main__":
    main()
