#!/usr/bin/env python3
"""x3cyc.py - an 8088 cycle MODEL for costing XTMP3's XT kernels (SPEC.md 121.4).

This is a model, not a measurement. It exists so that the XT study
(tools/x3xtstudy.py, docs/xtmp3/XT-STUDY.md) can price a candidate kernel as
it would really be written in NASM -- signal-code extraction, index scaling,
segment switches, the reads, the accumulation and the output normalisation --
instead of pricing a table read alone and calling it the product. Phase 2
(SPEC.md 121.11) replaces every figure it produces with MartyPC's cycle
counter.

How it models the 8088
----------------------
A clock-stepped simulation of the two halves of the CPU:

* The EU runs each instruction for its 8086 book time (Intel's iAPX 86/88
  User's Manual, the 8086 column), split into INTERNAL clocks and BUS
  transfers: the book's 4 clocks per 8086 bus cycle are taken out and the
  8088's transfers are put back one BYTE at a time, so a word operand costs
  two 8-bit bus cycles (the book's "+4 for the 8088").
* The BIU owns the 8-bit bus. A bus cycle is 4 clocks. It prefetches
  instruction bytes into a 4-byte queue whenever the bus is idle and the
  queue has a free byte; an EU memory transfer waits for the bus cycle in
  progress and then has priority over the next prefetch. A taken branch
  flushes the queue (a fetch already on the bus completes and is discarded).
* DRAM refresh: on the 5150, PIT channel 1 asks DMA channel 0 for a refresh
  cycle every 72 CPU clocks, and the bus is unavailable while it runs.
  `REFRESH_STEAL` is fitted to PERFORMANCE.md Part 2's measured
  4.34 clocks per instruction byte (a `nop` run), and nothing else is fitted.
* An instruction may not start until its first byte is in the queue, and it
  may not finish its internal work until its LAST byte is: a long
  instruction behind an empty queue pays the fetch, which is the
  `max(clocks, 4.34 x bytes)` floor PERFORMANCE.md measured, falling out of
  the simulation rather than being imposed on it.

`--calibrate` runs PERFORMANCE.md Part 2's measured table (the 5150, 16
instruction shapes) through the model and prints model against measurement.

Data-dependent instructions (`mul`, `imul`, `div`, shifts by CL) take a fixed
representative time; `IMUL16` is a parameter because the 8086's `imul` takes
128-154 clocks by operand (book), and the study quotes kernels with it.

Kernels are written as NASM source lines (the subset the XT kernels use).
`cost(lines, reps)` runs the block `reps` times back to back and returns the
steady-state clocks of the LAST repetition, so the queue state at entry is
the state the loop itself leaves.
"""
import re
import sys

CPU_HZ = 4772727            # the 5150's 14.31818 MHz / 3
BUS_CYCLE = 4
REFRESH_PERIOD = 72         # PIT ch1 divisor 18 x 4 CPU clocks a PIT clock
REFRESH_STEAL = 6           # fitted: nop -> 4.34 clk a byte (PERFORMANCE.md)
IMUL16 = 141                # book 128-154; middle of the range
IMUL8 = 89                  # book 80-98
MUL16 = 126                 # book 118-133 (PERFORMANCE.md measured ~128)
DIV16 = 150                 # book 144-162

R16 = ("ax", "cx", "dx", "bx", "sp", "bp", "si", "di")
R8 = ("al", "cl", "dl", "bl", "ah", "ch", "dh", "bh")
SREG = ("es", "cs", "ss", "ds")


def _num(t):
    t = t.strip().lower()
    try:
        if t.endswith("h") and re.match(r"^[0-9][0-9a-f]*h$", t):
            return int(t[:-1], 16)
        return int(t, 0)
    except ValueError:
        return None


class Op:
    __slots__ = ("kind", "size", "reg", "ea", "disp", "dsize", "seg", "imm",
                 "text")

    def __init__(self, text):
        self.text = text
        self.kind = None    # 'r', 's', 'm', 'i'
        self.size = None    # 1, 2 or None
        self.seg = None
        t = text.strip()
        low = t.lower()
        size = None
        for kw, sz in (("byte ", 1), ("word ", 2)):
            if low.startswith(kw):
                size = sz
                t = t[len(kw):].strip()
                low = t.lower()
        m = re.match(r"^(es|cs|ss|ds):\s*(\[.*\])$", low)
        if m:
            self.seg = m.group(1)
            t = t[t.index("["):]
            low = t.lower()
        if low in R16:
            self.kind, self.size, self.reg = "r", 2, low
        elif low in R8:
            self.kind, self.size, self.reg = "r", 1, low
        elif low in SREG:
            self.kind, self.size, self.reg = "s", 2, low
        elif low.startswith("["):
            self.kind = "m"
            self.size = size
            inner = low[1:-1].strip()
            m = re.match(r"^(es|cs|ss|ds)\s*:\s*(.*)$", inner)
            if m:
                self.seg = m.group(1)
                inner = m.group(2)
            regs = []
            disp = 0
            dsym = False
            for term in re.findall(r"[+-]?\s*[^+-]+", inner):
                term = term.replace(" ", "")
                sign = -1 if term.startswith("-") else 1
                body = term.lstrip("+-")
                if body in ("bx", "bp", "si", "di"):
                    regs.append(body)
                else:
                    v = _num(body)
                    if v is None:
                        dsym = True     # a symbol: a 16-bit displacement
                    else:
                        disp += sign * v
            regs.sort()
            self.ea = tuple(regs)
            if dsym:
                self.dsize = 2
            elif not regs:
                self.dsize = 2
            elif disp == 0 and regs != ["bp"]:
                self.dsize = 0
            elif -128 <= disp <= 127:
                self.dsize = 1
            else:
                self.dsize = 2
            self.disp = disp
        else:
            self.kind, self.size = "i", size
            self.imm = _num(t)      # None for a symbol: 16 bits unless a byte op

    def ea_clocks(self):
        r = self.ea
        if not r:
            c = 6
        elif len(r) == 1:
            c = 5 if self.dsize == 0 else 9
        else:
            fast = r in (("bx", "si"), ("bp", "di"))
            c = (7 if fast else 8) if self.dsize == 0 else (11 if fast else 12)
        return c + (2 if self.seg else 0)

    def modrm_bytes(self):
        return 1 + (self.dsize or 0) + (1 if self.seg else 0)


class Insn:
    """One decoded instruction: length, internal EU clocks, bus transfers."""
    __slots__ = ("text", "mn", "length", "internal", "reads", "writes",
                 "taken", "flush")

    def __repr__(self):
        return "<%s len=%d int=%d r=%d w=%d>" % (self.text, self.length,
                                                self.internal, self.reads,
                                                self.writes)


ALU = ("add", "sub", "and", "or", "xor", "adc", "sbb", "cmp", "test")
SHIFT = ("shl", "sal", "shr", "sar", "rol", "ror", "rcl", "rcr")
JCC = ("jz", "jnz", "je", "jne", "jc", "jnc", "jb", "jnb", "jae", "ja",
       "jbe", "jna", "js", "jns", "jl", "jge", "jle", "jg", "jo", "jno",
       "jcxz", "jnae", "jnbe", "jnl", "jng", "jnle", "jnge")


def _imm_bytes(op, size):
    if size == 1:
        return 1
    return 2


def decode(text):
    """Decode one NASM line into an Insn. `taken` marks a branch as taken by
    the suffix `; taken` in the source line."""
    src = text
    comment = text.split(";", 1)[1] if ";" in text else ""
    taken = "taken" in comment
    m = re.search(r"\b(\d+)\b", comment)
    count = int(m.group(1)) if m else None   # shift count by CL, rep count
    text = text.split(";", 1)[0].strip()
    rep = False
    if text.lower().startswith("rep "):
        rep = True
        text = text[4:].strip()
    prefix_seg = None
    m = re.match(r"^(es|cs|ss|ds)\s+(.*)$", text, re.I)
    if m:
        prefix_seg = m.group(1).lower()
        text = m.group(2)
    parts = text.split(None, 1)
    mn = parts[0].lower()
    ops = []
    if len(parts) > 1:
        depth = 0
        cur = ""
        for ch in parts[1]:
            if ch == "[":
                depth += 1
            elif ch == "]":
                depth -= 1
            if ch == "," and depth == 0:
                ops.append(cur)
                cur = ""
            else:
                cur += ch
        ops.append(cur)
    ops = [Op(o) for o in ops]
    for o in ops:
        if prefix_seg and o.kind == "m":
            o.seg = prefix_seg
    # operand size inference
    sz = None
    for o in ops:
        if o.kind in ("r", "s"):
            sz = o.size
    for o in ops:
        if o.size:
            sz = sz or o.size
    for o in ops:
        if o.kind in ("m", "i") and o.size is None:
            o.size = sz
    I = Insn()
    I.text = src.strip()
    I.mn = mn
    I.taken = taken
    I.flush = False
    I.reads = I.writes = 0
    seg_extra = 1 if (prefix_seg and not any(o.kind == "m" for o in ops)) else 0
    w = (sz or 2)

    def mem(o):
        return o.kind == "m"

    def ret(length, book, reads=0, writes=0):
        # book = 8086 clocks including 4 per 8086 bus cycle; strip those and
        # let the bus model charge the 8088's byte transfers.
        n86 = (1 if reads else 0) + (1 if writes else 0)
        if reads > 2 or writes > 2:
            n86 = (reads + 1) // 2 + (writes + 1) // 2
        I.length = length + seg_extra
        I.internal = max(1, book - 4 * n86) + (2 if seg_extra else 0)
        I.reads, I.writes = reads, writes
        return I

    if mn == "mov":
        d, s = ops
        if d.kind == "r" and s.kind == "r":
            return ret(2, 2)
        if d.kind == "s" or s.kind == "s":
            if mem(d):
                return ret(1 + d.modrm_bytes(), 9 + d.ea_clocks(), 0, 2)
            if mem(s):
                return ret(1 + s.modrm_bytes(), 8 + s.ea_clocks(), 2, 0)
            return ret(2, 2)
        if d.kind == "r" and s.kind == "i":
            return ret(1 + d.size, 4)
        if d.kind == "r" and mem(s):
            if d.reg in ("ax", "al") and not s.ea:
                return ret(3 + (1 if s.seg else 0), 10 + (2 if s.seg else 0),
                           d.size, 0)
            return ret(1 + s.modrm_bytes(), 8 + s.ea_clocks(), d.size, 0)
        if mem(d) and s.kind == "r":
            if s.reg in ("ax", "al") and not d.ea:
                return ret(3 + (1 if d.seg else 0), 10 + (2 if d.seg else 0),
                           0, s.size)
            return ret(1 + d.modrm_bytes(), 9 + d.ea_clocks(), 0, s.size)
        if mem(d) and s.kind == "i":
            return ret(1 + d.modrm_bytes() + d.size, 10 + d.ea_clocks(), 0,
                       d.size)
    if mn in ALU:
        d, s = ops
        wr = mn not in ("cmp", "test")
        if d.kind == "r" and s.kind == "r":
            return ret(2, 3)
        if d.kind == "r" and mem(s):
            return ret(1 + s.modrm_bytes(), 9 + s.ea_clocks(), d.size, 0)
        if mem(d) and s.kind == "r":
            if wr:
                return ret(1 + d.modrm_bytes(), 16 + d.ea_clocks(), s.size,
                           s.size)
            return ret(1 + d.modrm_bytes(), 9 + d.ea_clocks(), s.size, 0)
        if d.kind == "r" and s.kind == "i":
            if d.reg in ("ax", "al"):
                return ret(1 + d.size, 4)
            if mn == "test":
                return ret(2 + d.size, 5)
            small = d.size == 1 or (s.imm is not None and -128 <= s.imm <= 127)
            return ret(3 if small else 4, 4)
        if mem(d) and s.kind == "i":
            small = d.size == 1 or (s.imm is not None and -128 <= s.imm <= 127)
            ib = 1 if small else 2
            if wr:
                return ret(1 + d.modrm_bytes() + ib, 17 + d.ea_clocks(),
                           d.size, d.size)
            return ret(1 + d.modrm_bytes() + ib, 10 + d.ea_clocks(), d.size, 0)
    if mn in ("inc", "dec"):
        (d,) = ops
        if d.kind == "r":
            return ret(1, 2) if d.size == 2 else ret(2, 3)
        return ret(1 + d.modrm_bytes(), 15 + d.ea_clocks(), d.size, d.size)
    if mn in ("neg", "not"):
        (d,) = ops
        if d.kind == "r":
            return ret(2, 3)
        return ret(1 + d.modrm_bytes(), 16 + d.ea_clocks(), d.size, d.size)
    if rep and mn in ("stosw", "stosb", "movsw", "movsb", "lodsw", "lodsb"):
        n = count or 1
        w_ = 2 if mn.endswith("w") else 1
        per = {"stos": 10, "movs": 17, "lods": 13}[mn[:4]]
        rd = n * w_ if mn[:4] in ("movs", "lods") else 0
        wr = n * w_ if mn[:4] in ("movs", "stos") else 0
        I.length = 2
        I.internal = max(1, 9 + per * n - 4 * ((rd + 1) // 2 // max(w_, 1) * 0 + (n if rd else 0) + (n if wr else 0)))
        I.reads, I.writes = rd, wr
        return I
    if mn in SHIFT:
        d, s = ops
        bycl = s.kind == "r"
        n = count if count is not None else 4
        if d.kind == "r":
            if bycl:
                return ret(2, 8 + 4 * n)
            return ret(2, 2)
        if bycl:
            return ret(1 + d.modrm_bytes(), 20 + d.ea_clocks() + 4 * n,
                       d.size, d.size)
        return ret(1 + d.modrm_bytes(), 15 + d.ea_clocks(), d.size, d.size)
    if mn in ("imul", "mul", "idiv", "div"):
        (s,) = ops
        big = {"imul": IMUL16, "mul": MUL16, "idiv": DIV16 + 15,
               "div": DIV16}[mn]
        small = {"imul": IMUL8, "mul": 73, "idiv": 108, "div": 85}[mn]
        base = big if s.size == 2 else small
        if s.kind == "r":
            return ret(2, base)
        return ret(1 + s.modrm_bytes(), base + 6 + s.ea_clocks(), s.size, 0)
    if mn == "xchg":
        d, s = ops
        if d.kind == "r" and s.kind == "r":
            if "ax" in (d.reg, s.reg) and d.size == 2:
                return ret(1, 3)
            return ret(2, 4)
        m_ = d if mem(d) else s
        return ret(1 + m_.modrm_bytes(), 17 + m_.ea_clocks(), m_.size, m_.size)
    if mn in ("lodsb", "lodsw"):
        return ret(1, 12, 1 if mn == "lodsb" else 2, 0)
    if mn in ("stosb", "stosw"):
        return ret(1, 11, 0, 1 if mn == "stosb" else 2)
    if mn in ("movsb", "movsw"):
        n = 1 if mn == "movsb" else 2
        return ret(1, 18, n, n)
    if mn == "xlat" or mn == "xlatb":
        return ret(1, 11, 1, 0)
    if mn in ("cbw",):
        return ret(1, 2)
    if mn in ("cwd",):
        return ret(1, 5)
    if mn in ("clc", "stc", "cld", "std", "cmc", "nop", "sahf", "lahf",
              "cli", "sti"):
        return ret(1, {"nop": 3, "sahf": 4, "lahf": 4}.get(mn, 2))
    if mn == "lea":
        d, s = ops
        return ret(1 + s.modrm_bytes(), 2 + s.ea_clocks())
    if mn in ("les", "lds"):
        d, s = ops
        return ret(1 + s.modrm_bytes(), 16 + s.ea_clocks(), 4, 0)
    if mn == "push":
        (s,) = ops
        if s.kind in ("r", "s"):
            return ret(1, 11, 0, 2)
        return ret(1 + s.modrm_bytes(), 16 + s.ea_clocks(), 2, 2)
    if mn == "pop":
        (d,) = ops
        if d.kind in ("r", "s"):
            return ret(1, 8, 2, 0)
        return ret(1 + d.modrm_bytes(), 17 + d.ea_clocks(), 2, 2)
    if mn in JCC or mn in ("loop", "jmp", "loopz", "loopnz"):
        I.flush = I.taken or mn == "jmp"
        if mn == "jmp":
            t0 = ops[0].text.strip().lower() if ops else ""
            if t0.startswith("near ") and t0[5:].strip() in R16:
                return ret(2, 11)
            near = t0.startswith("near")
            return ret(3 if near else 2, 15)
        if mn == "loop":
            return ret(2, 17 if I.taken else 5)
        if mn == "jcxz":
            return ret(2, 18 if I.taken else 6)
        return ret(2, 16 if I.taken else 4)
    if mn == "call":
        I.flush = True
        return ret(3, 19, 0, 2)
    if mn == "ret":
        I.flush = True
        return ret(1, 16, 2, 0)
    if mn == "retf":
        I.flush = True
        return ret(1, 26, 4, 0)
    raise ValueError("x3cyc: cannot decode %r" % src)


def simulate(insns, refresh_phase=0):
    """Clock-stepped BIU/EU simulation. Returns the clock at which each
    instruction finished (list), starting from an EMPTY queue at clock 0."""
    q = 0               # bytes in the queue
    biu = None          # ('f', remaining) fetch, ('m', remaining) EU transfer
    refresh = 0         # clocks of refresh left
    t = 0
    ends = []
    i = 0
    n = len(insns)
    # EU state for the current instruction
    need = 0            # instruction bytes still to take from the queue
    work = 0            # internal clocks still to run
    xfer = 0            # bus bytes the EU still wants (reads then writes)
    eu_wait_bus = False
    if n:
        need, work, xfer = insns[0].length, insns[0].internal, \
            insns[0].reads + insns[0].writes
    discard = False
    while i < n:
        # ---- the bus ----
        # A bus cycle is four clocks, T1..T4; the next may start on the clock
        # after T4. Refresh DMA holds the bus off for REFRESH_STEAL clocks
        # every REFRESH_PERIOD, granted at the next free clock.
        if (t + refresh_phase) % REFRESH_PERIOD == 0:
            refresh += REFRESH_STEAL
        if biu is None:
            if refresh:
                refresh -= 1
            elif eu_wait_bus and xfer:
                biu = ["m", BUS_CYCLE]
            elif q < 4:
                biu = ["f", BUS_CYCLE]
        if biu is not None:
            biu[1] -= 1
            if biu[1] == 0:
                if biu[0] == "f":
                    if not discard:
                        q += 1
                    discard = False
                else:
                    xfer -= 1
                    eu_wait_bus = False
                biu = None
        # ---- the EU ----
        # take instruction bytes as they arrive
        if need and q:
            take = min(need, q)
            need -= take
            q -= take
        cur = insns[i]
        if need == cur.length:
            pass                     # nothing of this instruction yet
        elif work > 0:
            if need == 0 or work > 1:
                work -= 1            # internal work overlaps the tail fetch
        if need == 0 and work <= 0:
            if xfer > 0:
                if biu is None or biu[0] != "m":
                    eu_wait_bus = True
            else:
                ends.append(t + 1)
                if cur.flush:
                    q = 0
                    if biu is not None and biu[0] == "f":
                        discard = True
                i += 1
                if i < n:
                    nx = insns[i]
                    need, work, xfer = nx.length, nx.internal, \
                        nx.reads + nx.writes
        t += 1
    return ends


def cost(lines, reps=4, warm=2):
    """Steady-state clocks of one pass of `lines` (NASM, one per entry)."""
    body = [decode(l) for l in lines if l.split(";", 1)[0].strip()]
    if not body:
        return 0.0
    seq = body * (warm + reps)
    ends = simulate(seq)
    k = len(body)
    a = ends[warm * k - 1]
    b = ends[(warm + reps) * k - 1]
    return (b - a) / reps


def static_bytes(lines):
    return sum(decode(l).length for l in lines if l.split(";", 1)[0].strip())


# PERFORMANCE.md Part 2, "The 8088's real instruction cost is a straight
# line": the 5150's measured clocks for each shape (the loop overhead taken
# out by the bench that measured them).
CALIBRATION = [
    (["nop"], 4.34),
    (["inc ax"], 4.34),
    (["xchg ax,bx"], 4.34),
    (["mov ax,bx"], 8.69),
    (["add ax,bx"], 8.69),
    (["cmp ax,bx"], 8.69),
    (["shl ax,1"], 8.69),
    (["mov al,[si]"], 15.22),
    (["mov al,[es:si]"], 19.00),
    (["jmp short $+2 ; taken"], 18.19),
    (["mov ax,[1234h]"], 21.61),
    (["mov [1234h],ax"], 24.08),
    (["push ax", "pop ax"], 29.70),
    (["call near x ; taken", "ret"], 52.13),
    (["mov ax,1234h", "mul bx"], 132.53),
    (["xor dx,dx", "mov ax,1234h", "div bx"], 162.85),
]


def calibrate(verbose=True):
    rows = []
    for lines, meas in CALIBRATION:
        m = cost(lines, reps=16, warm=4)
        rows.append((" / ".join(lines), meas, m))
    if verbose:
        print("%-34s %9s %9s %7s" % ("shape (PERFORMANCE.md Part 2)",
                                     "5150", "model", "err"))
        for s, meas, m in rows:
            print("%-34s %9.2f %9.2f %+6.1f%%" % (s, meas, m,
                                                  100 * (m - meas) / meas))
        errs = [abs(m - meas) / meas for _, meas, m in rows]
        print("mean |err| %.1f%%, worst %.1f%%" % (100 * sum(errs) / len(errs),
                                                  100 * max(errs)))
    return rows


if __name__ == "__main__":
    if "--calibrate" in sys.argv:
        calibrate()
    elif len(sys.argv) > 1 and sys.argv[1] == "--cost":
        lines = open(sys.argv[2]).read().splitlines()
        print("%.1f clocks/pass, %d bytes" % (cost(lines), static_bytes(lines)))
    else:
        print(__doc__)
