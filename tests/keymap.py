#!/usr/bin/env python3
"""OSAPI_KEY_MAP (0x0239) copies the whole key-state map (SPEC.md 9.7.3).

    make && python3 tests/keymap.py [TREE ...]

Each built kernel's OWN bytes run under Unicorn - the jump cell at 0x0239,
`kbd_track` (the int 09h ISR's writer, SPEC.md 9.7) and the `kbd_down` slot
at 0x016A - so what is checked is the code the machine boots, not a model of
it. Per kernel:

  (a) THE CELL: 0x0239 is `jmp near osapi_key_map` and the table ends at
      0x023C, where the SDK (apps/os88api.inc) and the Wire's fence
      (tools/os88wire.py WIRE_TABLE_END) say it does.
  (b) THE ARM: with the map unarmed and its 16 bytes poisoned, one call
      clears and arms it (kbd_armed = 1) and answers 16 zero bytes - the
      contract's "the first answer is all up".
  (c) THE COPY: scancodes fed through kbd_track - makes, breaks, and the E0
      prefix of the grey keys, which must land on the keypad key's bit - and
      the call's 16 bytes are bit (s & 7) of byte (s >> 3) for exactly the
      keys still held. Into an ODD offset of a foreign DS, with sentinels on
      both sides untouched.
  (d) THE CONTRACT: CF = 0 out (entered with CF = 1), and AX BX CX DX SI DI
      BP DS ES SS SP all as they went in.
  (e) AGREEMENT: for all 128 make codes, kbd_down's CF and the copy's bit say
      the same thing - one map, two readers.
  (f) kern_mega only: `fsx_rkeep` (kernel/fsx.inc) carries the cell's row,
      so a FSXF_REAL bracket keeps it rather than answering CF = 1.

kern_big (build/) is required. build/smallk, build/megak and build/emuk are
checked when they hold a kernel that HAS the cell, and named as not checked
otherwise; TREE arguments add or replace the list (a private `make BUILD=`).

A NEGATIVE CONTROL runs in every pass: the body's `mov cx, 8` is patched to 7
in Unicorn's copy of the image and (c) must then FAIL - so a check that
cannot see a short copy is reported rather than trusted.

THE RED ARM: drop the `call kbd_down` from osapi_key_map (kernel/mouse.inc)
and (b) goes red - the poisoned bytes come back; make the count KBD_MAPSZ/2 -
1 and (c) does; take the row out of fsx_rkeep and (f) does.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))

from unicorn import Uc, UC_ARCH_X86, UC_MODE_16             # noqa: E402
from unicorn import x86_const as X                           # noqa: E402

KERNEL_SEG = 0x0060
CELL_MAP, CELL_DOWN, TABLE_END = 0x0239, 0x016A, 0x023C
MAPSZ = 16
NEAR_RET = 0xFFF0               # KERNEL_SEG:FFF0 - past every kernel's .bss
FAR_SEG, FAR_IP = 0x9000, 0x0000
USEG, BUF = 0x3000, 0x0101      # the caller's DS and an ODD offset in it
REGS = (("AX", X.UC_X86_REG_AX, 0x1234), ("BX", X.UC_X86_REG_BX, 0x2345),
        ("CX", X.UC_X86_REG_CX, 0x3456), ("DX", X.UC_X86_REG_DX, 0x4567),
        ("SI", X.UC_X86_REG_SI, 0x5678), ("DI", X.UC_X86_REG_DI, BUF),
        ("BP", X.UC_X86_REG_BP, 0x789A), ("DS", X.UC_X86_REG_DS, USEG),
        ("ES", X.UC_X86_REG_ES, 0x4000), ("SS", X.UC_X86_REG_SS, 0x5000),
        ("SP", X.UC_X86_REG_SP, 0xFF00))
CF, DF = 0x0001, 0x0400

# what kbd_track is fed, and what must be held after it. 0xE0 is the grey
# keys' prefix: E0 4B is grey Left (the keypad's 4B bit), E0 C8 grey Up's
# BREAK, which must clear keypad Up's 48 set by the plain make before it
FEED = [0x1E, 0x11, 0xE0, 0x4B, 0x48, 0x39, 0x01, 0x7F, 0x2A, 0x1D, 0x47,
        0x9E, 0xE0, 0xC8, 0xC7]
HELD = {0x11, 0x4B, 0x39, 0x01, 0x7F, 0x2A, 0x1D}

FAILS = []


def check(ok, what):
    print("  %s  %s" % ("ok  " if ok else "FAIL", what))
    if not ok:
        FAILS.append(what)
    return ok


class Kernel(object):
    """One built kernel: its map's .text, its symbols, its image."""

    def __init__(self, tree):
        self.tree = tree
        self.mapf = os.path.join(tree, "kernel.map")
        self.ok = os.path.isfile(self.mapf)
        if not self.ok:
            return
        text = open(self.mapf).read()
        m = re.search(r"Output file:\s+(\S+)", text)
        out = m.group(1) if m else os.path.join(tree, "kernel-full.bin")
        if not os.path.isabs(out):
            out = os.path.join(ROOT, out)
        if not os.path.isfile(out):
            out = os.path.join(tree, "kernel-full.bin")
        self.image = open(out, "rb").read()
        self.sec = {}           # name -> (file start, length)
        for mm in re.finditer(r"^\s+[0-9A-F]+\s+([0-9A-F]+)\s+[0-9A-F]+\s+"
                              r"([0-9A-F]+)\s+(?:progbits|nobits)\s+(\S+)",
                              text, re.M):
            self.sec[mm.group(3)] = (int(mm.group(1), 16), int(mm.group(2), 16))
        self.sym = {}           # name -> (file offset, virtual)
        for mm in re.finditer(r"^\s+([0-9A-F]+)\s+([0-9A-F]+)\s+(\S+)$",
                              text, re.M):
            self.sym.setdefault(mm.group(3), (int(mm.group(1), 16),
                                              int(mm.group(2), 16)))

    def v(self, name):
        return self.sym[name][1]


class Machine(object):
    def __init__(self, k, patch=None):
        self.k = k
        u = self.u = Uc(UC_ARCH_X86, UC_MODE_16)
        u.mem_map(0, 0x110000)
        start, length = k.sec[".text"]
        text = bytearray(k.image[start:start + length])
        if patch:
            patch(text)
        u.mem_write(KERNEL_SEG * 16, bytes(text))
        u.mem_write(KERNEL_SEG * 16 + NEAR_RET, b"\xF4")       # hlt
        u.mem_write(FAR_SEG * 16 + FAR_IP, b"\xF4")

    def kread(self, off, n):
        return bytes(self.u.mem_read(KERNEL_SEG * 16 + off, n))

    def kwrite(self, off, data):
        self.u.mem_write(KERNEL_SEG * 16 + off, bytes(data))

    def regs(self, flags):
        for _, r, val in REGS:
            self.u.reg_write(r, val)
        self.u.reg_write(X.UC_X86_REG_EFLAGS, 0x0002 | flags)

    def far(self, ip, flags=0, al=None):
        """Far-call KERNEL_SEG:ip as a package does; return the registers."""
        u = self.u
        self.regs(flags)
        if al is not None:
            u.reg_write(X.UC_X86_REG_AX, 0x1200 | al)
        ss, sp = u.reg_read(X.UC_X86_REG_SS), u.reg_read(X.UC_X86_REG_SP)
        sp -= 4
        u.mem_write(ss * 16 + sp, bytes([FAR_IP & 255, FAR_IP >> 8,
                                         FAR_SEG & 255, FAR_SEG >> 8]))
        u.reg_write(X.UC_X86_REG_SP, sp)
        u.reg_write(X.UC_X86_REG_CS, KERNEL_SEG)
        u.emu_start(KERNEL_SEG * 16 + ip, FAR_SEG * 16 + FAR_IP, count=20000)
        return self.out()

    def near(self, ip, al):
        """kbd_track as kbm_isr calls it: AL = the byte, DS = KERNEL_SEG."""
        u = self.u
        self.regs(0)
        u.reg_write(X.UC_X86_REG_AX, al)
        u.reg_write(X.UC_X86_REG_DS, KERNEL_SEG)
        ss, sp = u.reg_read(X.UC_X86_REG_SS), u.reg_read(X.UC_X86_REG_SP)
        sp -= 2
        u.mem_write(ss * 16 + sp, bytes([NEAR_RET & 255, NEAR_RET >> 8]))
        u.reg_write(X.UC_X86_REG_SP, sp)
        u.reg_write(X.UC_X86_REG_CS, KERNEL_SEG)
        u.emu_start(KERNEL_SEG * 16 + ip, KERNEL_SEG * 16 + NEAR_RET,
                    count=20000)

    def out(self):
        u = self.u
        r = {n: u.reg_read(reg) for n, reg, _ in REGS}
        r["IP"] = u.reg_read(X.UC_X86_REG_IP)
        r["CS"] = u.reg_read(X.UC_X86_REG_CS)
        r["FL"] = u.reg_read(X.UC_X86_REG_EFLAGS)
        return r

    def ubuf(self, n=MAPSZ + 2):
        return bytes(self.u.mem_read(USEG * 16 + BUF - 1, n))

    def ufill(self, b):
        self.u.mem_write(USEG * 16 + BUF - 1, bytes([0x5A]) + bytes([b] * MAPSZ)
                         + bytes([0xA5]))


def expect_map(held):
    m = bytearray(MAPSZ)
    for s in held:
        m[s >> 3] |= 1 << (s & 7)
    return bytes(m)


def returned(r):
    return r["CS"] == FAR_SEG and r["IP"] == FAR_IP


def kept(r, what):
    bad = ["%s %04X->%04X" % (n, val, r[n]) for n, _, val in REGS
           if r[n] != val]
    check(not bad, "%s: every register kept%s" % (
        what, "" if not bad else " - " + ", ".join(bad)))
    check(not (r["FL"] & CF), "%s: CF = 0 out (entered with CF = 1)" % what)
    check(not (r["FL"] & DF), "%s: DF still 0" % what)


def copy_body(k, text):
    """The body's `mov cx, 8` - the negative control's patch site."""
    off = k.v("osapi_key_map")
    i = text.find(b"\xB9\x08\x00", off, off + 48)
    return i


def exercise(k, patch=None, quiet=False):
    """(b)-(e) on one kernel; returns the failures it would add."""
    global FAILS
    saved, FAILS = FAILS, []
    out = sys.stdout
    if quiet:
        sys.stdout = open(os.devnull, "w")
    try:
        m = Machine(k, patch)
        armed, dn = k.v("kbd_armed"), k.v("kbd_dnmap")
        # (b) the arm: unarmed, the map poisoned
        m.kwrite(armed, b"\x00")
        m.kwrite(dn, b"\xA5" * MAPSZ)
        m.ufill(0xEE)
        r = m.far(CELL_MAP, flags=CF)
        check(returned(r), "(b) the call returns through its far frame "
              "(%04X:%04X)" % (r["CS"], r["IP"]))
        got = m.ubuf()
        check(got[1:-1] == bytes(MAPSZ),
              "(b) the first answer after the arm is all up: %s"
              % got[1:-1].hex())
        check(m.kread(armed, 1) == b"\x01", "(b) kbd_armed = 1 after the call")
        check(m.kread(dn, MAPSZ) == bytes(MAPSZ),
              "(b) the kernel's map was cleared by the arm, not just the copy")
        kept(r, "(b)")
        # (c) the copy, through the ISR's own writer
        for b in FEED:
            m.near(k.v("kbd_track"), b)
        want = expect_map(HELD)
        check(m.kread(dn, MAPSZ) == want,
              "(c) kbd_track built the map the feed implies: %s"
              % m.kread(dn, MAPSZ).hex())
        m.ufill(0xEE)
        r = m.far(CELL_MAP, flags=CF)
        got = m.ubuf()
        check(got[1:-1] == want,
              "(c) the copy is bit (s & 7) of byte (s >> 3) for exactly the "
              "held keys %s: got %s, want %s"
              % (" ".join("%02X" % s for s in sorted(HELD)),
                 got[1:-1].hex(), want.hex()))
        check(got[0] == 0x5A and got[-1] == 0xA5,
              "(c) the bytes either side of the caller's 16 untouched "
              "(%02X .. %02X)" % (got[0], got[-1]))
        check(m.kread(armed, 1) == b"\x01" and m.kread(dn, MAPSZ) == want,
              "(c) an armed call leaves the kernel's map as it was")
        kept(r, "(c)")
        # (e) one map, two readers
        bad = []
        for s in range(0x80):
            r = m.far(CELL_DOWN, al=s)
            down = bool(r["FL"] & CF)
            if down != bool(got[1 + (s >> 3)] & (1 << (s & 7))):
                bad.append("%02X" % s)
        check(not bad, "(e) kbd_down agrees with the copy on all 128 make "
              "codes%s" % ("" if not bad else ": not on " + " ".join(bad)))
    except Exception as e:                      # a crash in the guest code
        check(False, "the guest code faulted: %s" % e)
    finally:
        if quiet:
            sys.stdout.close()
            sys.stdout = out
        mine, FAILS = FAILS, saved
    FAILS.extend([] if quiet else mine)
    return mine


def one(tree, required):
    k = Kernel(tree)
    name = os.path.relpath(tree, ROOT)
    if not k.ok:
        if required:
            check(False, "%s: no kernel.map - run make" % name)
        else:
            print("%s: not checked (no kernel built there)" % name)
        return
    if "osapi_key_map" not in k.sym:
        if required:
            check(False, "%s: the kernel has no osapi_key_map - a build from "
                  "before SPEC.md 9.7.3" % name)
        else:
            print("%s: not checked (its kernel predates the cell)" % name)
        return
    print("%s:" % name)
    # (a) the cell
    start = k.sec[".text"][0]
    cell = k.image[start + CELL_MAP:start + CELL_MAP + 3]
    target = (CELL_MAP + 3 + (cell[1] | cell[2] << 8)) & 0xFFFF
    check(cell[0] == 0xE9 and target == k.v("osapi_key_map"),
          "(a) 0x0239 is `jmp near osapi_key_map` (%s -> %04X, the body at "
          "%04X)" % (cell.hex(), target, k.v("osapi_key_map")))
    check(k.v("osapi_table_end") == TABLE_END,
          "(a) the table ends at 0x%04X (osapi_table_end %04X)"
          % (TABLE_END, k.v("osapi_table_end")))
    check(k.v("kbd_dnmap") - k.v("kbd_armed") == 1,
          "(a) kbd_armed is the byte below kbd_dnmap, as the body reads it")
    exercise(k)
    # the negative control: a copy one word short must be SEEN
    text = bytearray(k.image[start:start + k.sec[".text"][1]])
    site = copy_body(k, text)
    if check(site > 0, "control: the body's `mov cx, 8` found at %04X" % site):
        def short(t):
            t[site + 1] = 7
        caught = exercise(k, patch=short, quiet=True)
        check(any(c.startswith("(c) the copy") for c in caught),
              "control: a copy one word SHORT is caught by (c) (%d failure(s))"
              % len(caught))
    # (f) kern_mega's real-mode bracket keeps the cell
    if "fsx_rkeep" in k.sym:
        a, e = k.sym["fsx_rkeep"][0], k.sym["fsx_rkeep_end"][0]
        rows = [tuple(k.image[i + j] | k.image[i + j + 1] << 8
                      for j in (0, 2, 4)) for i in range(a, e, 6)]
        check((CELL_MAP, 3, 0) in rows,
              "(f) fsx_rkeep keeps 0x0239 in a FSXF_REAL bracket (rows: %s)"
              % " ".join("%04X/%d" % (c, n) for c, n, _ in rows))


def main(argv):
    trees = [os.path.abspath(a) for a in argv[1:]]
    if trees:
        for t in trees:
            one(t, True)
    else:
        b = os.path.join(ROOT, "build")
        one(b, True)
        for sub in ("smallk", "megak", "emuk"):
            one(os.path.join(b, sub), False)
    if FAILS:
        print("keymap: %d FAILED" % len(FAILS))
        return 1
    print("keymap: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
