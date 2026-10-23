#!/usr/bin/env python3
"""x3xtkern.py - the XT backend's candidate KERNELS, written as the NASM the
target would run, and priced by tools/x3cyc.py's 8088 model (SPEC.md 121.4,
docs/xtmp3/XT-STUDY.md 'Cycle model').

Each kernel is a function returning a list of NASM lines for ONE unit of
work (one output sample, one multiply, one nonzero line ...), unrolled the
way the target unrolls it. Row and table addresses are symbolic 16-bit
displacements (`ROW+...`); the decoder never computes an address it could
have assembled. `cost(kernel)` is the steady-state clock count of one unit
with the 8088's queue and bus modelled, which includes the code-extraction,
index scaling, segment switches, reads, accumulation and normalisation the
brief (section 5) says a product's cost must include.

Register conventions the kernels assume (and phase 2 may change):
  DS   the table segment of the pass (product rows, encoder tables); every
       hot row of a pass lies inside it (x3xttab.py lays the packs out so)
  ES   the pass's data segment (PCM out, scratch), reached by string ops
       or an override, whose byte the model charges
  BH   0 across a pass that indexes by a byte in BL
"""
import x3cyc

# ---------------------------------------------------------------- window


def win_output(n, layout, accum_init=True, out="pcm8"):
    """One output sample of the synthesis window: n taps through product
    rows, then the sample's normalisation.

    layout 'lodsw'  : a per-phase ring of code words in tap order, read
                      with LODSW (SI walks it; contiguous taps only)
    layout 'rel8'   : the same ring, taps at [si+2*age] (any tap subset)
    layout 'abs16'  : the standard 16-slot V-code ring, [si+disp16]
    layout 'imul'   : no table: 16-bit V words and `imul` by the window
                      coefficient, 32-bit accumulation (the 'int' form)
    out 'pcm8'      : Q14 sum -> unsigned 8-bit with an overflow test
    """
    L = []
    if layout == "imul":
        L += ["xor cx,cx", "xor bp,bp"]
        for i in range(n):
            L += ["lodsw", "imul word [bx+%d]" % (2 * i), "add cx,ax", "adc bp,dx"]
        # Q11 * Q14 = Q25 in BP:CX; PCM8 wants bits 18..25: take BP, shift 2
        L += ["mov ax,bp", "sar ax,1", "sar ax,1", "add bx,32",
              "add si,%d" % (64 - 2 * n)]
        L += ["add ax,1", "sar ax,1"]
        L += ["xor al,80h", "stosb"]
        return L
    L.append("mov dx,32")                     # rounding of the >> 6
    for i in range(n):
        op = "add" if i % 3 else "sub"         # sign by add/sub, never data
        if layout == "lodsw":
            L += ["lodsw", "xchg bx,ax", "%s dx,[bx+ROW%d]" % (op, i)]
        elif layout == "rel8":
            L += ["mov bx,[si+%d]" % (2 * i + 2 * (i % 3)), "%s dx,[bx+ROW%d]" % (op, i)]
        elif layout == "abs16":
            L += ["mov bx,[si+%d]" % (-128 * i - 300), "%s dx,[bx+ROW%d]" % (op, i)]
        else:
            raise ValueError(layout)
    if layout in ("lodsw", "rel8"):
        L.append("add si,RSTRIDE")
    else:
        L += ["inc si", "inc si"]
    if out == "pcm8":
        # dx: Q13, +-4.0. Each shl puts the next bit's disagreement with the
        # sign in OF, so two catch |y| >= 1.0; AH is then y >> 6
        L += ["xchg ax,dx", "shl ax,1", "jo clip", "shl ax,1", "jo clip",
              "mov al,ah", "xor al,80h", "stosb"]
    return L


def win_pair(n, layout):
    """TWO output samples, phases j and 32 - j, in one pass. At every tap
    both read the SAME unique V value (V[32-j] = -V[j], V[64-j] = V[32+j])
    and their rows are mirrors (row(32-j, i) = row(j, 15-i)), so one code
    fetch serves two products and one ring serves the pair. DX and CX
    accumulate; each sample is normalised and stored."""
    L = ["mov dx,32", "mov cx,32"]
    for i in range(n):
        op1 = "add" if i % 3 else "sub"
        op2 = "sub" if i % 2 else "add"
        if layout == "lodsw":
            L += ["lodsw", "xchg bx,ax"]
        else:
            L += ["mov bx,[si+%d]" % (2 * i + 2 * (i % 3))]
        L += ["%s dx,[bx+ROW%d]" % (op1, i), "%s cx,[bx+ROW%d]" % (op2, 15 - i)]
    L.append("add si,RSTRIDE")
    for r in ("dx", "cx"):
        L += ["mov ax,%s" % r, "shl ax,1", "jo clip", "shl ax,1", "jo clip",
              "mov al,ah", "xor al,80h",
              "mov [es:di+%d],al" % (0 if r == "dx" else 31)]
    L.append("inc di")
    return L


def vcode_fused(rings):
    """The V code formed as Lee's output is produced (AX already holds it:
    no reload), ENC s = 0 over +-2.0 FS with the clamp, the code word
    stored into the pair ring and its doubled copy."""
    L = ["add ax,4096", "cmp ax,8191", "ja vbig", "xchg bx,ax",
         "mov al,[bx+ENC]", "xor bh,bh", "cbw", "shl ax,1"]
    for r in range(rings):
        L.append("mov [es:si+%d],ax" % (300 + 64 * r))
    return L


def vcode(mode, rings):
    """One V value (a Lee output, int16 Q11 in the scratch) to its code
    word, stored into `rings` places (2 for the standard doubled ring, 4
    for the per-phase transposed one).
    mode 's0clamp': ENC over +-2.0 FS at s = 0 (8 KB of bytes), an unsigned
                    range test sends the rare larger |V| to a saturating
                    slow path.
    mode 's2round': ENC over the whole int16 at s = 2 (16 KB of bytes)."""
    L = ["mov ax,[di+20]"]
    if mode == "s0clamp":
        L += ["add ax,4096", "cmp ax,8191", "ja vbig", "xchg bx,ax",
              "mov al,[bx+ENC]", "xor bh,bh"]
    else:
        L += ["add ax,2", "sar ax,1", "sar ax,1", "xchg bx,ax",
              "mov al,[bx+ENCMID]", "xor bh,bh"]
    L += ["cbw", "shl ax,1"]
    for r in range(rings):
        L.append("mov [es:si+%d],ax" % (300 + 64 * r))
    return L


# ------------------------------------------------------------- multiply
def mul_split(src="ax", dst="dx"):
    """x * c by the byte-split tables: BL indexes four 256-byte planes."""
    return ["mov bl,ah", "mov dl,[bx+HLO]", "mov dh,[bx+HHI]",
            "mov bl,al", "add dl,[bx+LLO]", "adc dh,[bx+LHI]"]


def mul_imul_q15(e=0):
    """x * c by imul, c = 2^e * c' with c' Q15, rounded, scaled back."""
    L = ["imul word [CK]", "add ax,4000h", "adc dx,0", "shl ax,1", "rcl dx,1"]
    L += ["shl dx,1"] * e
    return L


def mul_imul_hi():
    """x * c by imul taking DX alone (the scaled-DCT form: no rounding, the
    factor 2 folded into the window rows)."""
    return ["imul word [CK]"]


def mul_code_tab():
    """x * c through a coded product row: x is ENCoded (s = 2, rounding),
    the row read; the whole 8-bit table path for one multiply."""
    return ["add ax,2", "sar ax,1", "sar ax,1", "xchg bx,ax",
            "mov al,[bx+ENCMID]", "cbw", "shl ax,1", "xchg bx,ax",
            "mov dx,[bx+ROWC]"]


def mul_qsq():
    """x * y by the quarter-square table: Q(n) = floor(n^2/4) over 0..65536
    as 4-byte entries in four 64 KB segments (262,148 bytes). |x+y| and
    |x-y| need 17 bits; the segment is picked by bits 14..16 of the index,
    the entry read as two words with ES loaded each time. AX = x, CX = y;
    result DX:AX."""
    return [
        "mov bx,ax", "add bx,cx", "rcr dx,1",           # 17-bit sum
        "jns qs_p1", "neg bx",                            # |x+y| (+ endpoint)
        "mov si,ax", "sub si,cx", "jns qs_p2", "neg si",  # |x-y|
        # entry address n*4 over 4 segments: seg = n >> 14, off = (n & 3FFFh)*4
        "mov di,bx", "shl di,1", "shl di,1",
        "mov dx,bx", "mov cl,10", "shr dx,cl", "add dx,QSEG",
        "mov es,dx",
        "mov ax,[es:di]", "mov dx,[es:di+2]",
        "mov di,si", "shl di,1", "shl di,1",
        "mov bp,si", "mov cl,10", "shr bp,cl", "add bp,QSEG",
        "mov es,bp",
        "sub ax,[es:di]", "sbb dx,[es:di+2]",
    ]


def mul_imul32():
    """The native reference: AX * CX -> DX:AX."""
    return ["mov ax,[si]", "imul word [si+2]"]


def mul_shift_add(digits):
    """x * c by signed powers of two (x in AX, result in DX). Right shifts
    by `sar` one at a time on a running copy, left shifts likewise; the
    8088 charges every shift its two bytes of fetch."""
    L = ["mov bx,ax", "xor dx,dx"]
    cur = 0
    for sgn, e in sorted(digits, key=lambda d: -d[1]):
        while cur > e:
            L.append("sar bx,1")
            cur -= 1
        while cur < e:
            L.append("shl bx,1")
            cur += 1
        L.append(("add" if sgn > 0 else "sub") + " dx,bx")
    return L


# ------------------------------------------------------------ Lee DCT
def lee_code(N, mul, K=None):
    """Straight-line Lee DCT-II of size N over a 16-bit scratch addressed
    [di+off] (offsets inside +-127, so disp8). A butterfly loads its two
    inputs ONCE and forms the sum (stored) and the difference (into the
    multiply, AX -> DX, stored); the odd outputs' pairwise sums are a load,
    an add and a store. K: only inputs 0..K-1 are nonzero (subbands above
    the granule's last active one), and every node whose inputs are known
    zero is not emitted, nor a sum with one zero input (it IS the other).
    Returns (lines, n_mul, n_add)."""
    lines = []
    counts = {"mul": 0, "add": 0}
    free = list(range(95, -1, -1))
    ZERO = -1

    def alloc():
        return free.pop()

    def off(s):
        return 2 * s - 96

    def rel(v):
        if v is not ZERO and v >= 0:
            free.append(v)

    def rec(xs):
        n = len(xs)
        if n == 1:
            return xs
        a, b = xs[:n // 2], xs[::-1][:n // 2]
        ev_in, od_in = [], []
        for k in range(n // 2):
            za, zb = a[k] is ZERO, b[k] is ZERO
            if za and zb:
                ev_in.append(ZERO)
                od_in.append(ZERO)
                continue
            if zb or za:
                # sum = the live one, difference = +-it: no add at all
                src = a[k] if zb else b[k]
                ev_in.append(src)
                s2 = alloc()
                lines.append("mov ax,[di+%d]" % off(src))
                if za:
                    lines.append("neg ax")
                lines.extend(mul)
                lines.append("mov [di+%d],dx" % off(s2))
                counts["mul"] += 1
                od_in.append(s2)
                continue
            s1, s2 = alloc(), alloc()
            lines.extend(["mov ax,[di+%d]" % off(a[k]), "mov cx,[di+%d]" % off(b[k]),
                          "mov dx,ax", "add dx,cx", "mov [di+%d],dx" % off(s1),
                          "sub ax,cx"])
            lines.extend(mul)
            lines.append("mov [di+%d],dx" % off(s2))
            counts["add"] += 2
            counts["mul"] += 1
            ev_in.append(s1)
            od_in.append(s2)
            rel(a[k])
            rel(b[k])
        ev = rec(ev_in)
        od = rec(od_in)
        out = [None] * n
        for m in range(n // 2):
            out[2 * m] = ev[m]
        for m in range(n // 2):
            y0 = od[m]
            y1 = od[m + 1] if m < n // 2 - 1 else ZERO
            if y1 is ZERO:
                out[2 * m + 1] = y0
            elif y0 is ZERO:
                out[2 * m + 1] = y1
            else:
                s = alloc()
                lines.extend(["mov ax,[di+%d]" % off(y0), "add ax,[di+%d]" % off(y1),
                              "mov [di+%d],ax" % off(s)])
                counts["add"] += 1
                out[2 * m + 1] = s
        return out

    K = N if K is None else K
    inputs = [alloc() if k < K else ZERO for k in range(N)]
    rec(inputs)
    return lines, counts["mul"], counts["add"]


# ---------------------------------------------------------------- IMDCT
def imdct_sparse_line(nrows=18):
    """One nonzero line's contribution to its subband's 18 DCT-IV outputs:
    a jump into the line's unrolled block (by k), then for each n one row
    read and a read-modify-write of z[n]. BX = the line's code word."""
    L = ["mov bp,[cs:si]", "inc si", "inc si", "jmp near bp ; taken"]
    for n in range(nrows):
        op = "add" if n % 2 else "sub"
        L += ["mov ax,[bx+ROW%d]" % n, "%s [es:di+%d],ax" % (op, 2 * n)]
    L += ["jmp near next ; taken"]
    return L


def imdct_subband_tab():
    """Per active subband with the table forms: clear z, then code the 18
    z values (ENC, s = 2) and window-and-overlap the 36 outputs through
    the window rows, writing 18 subband samples and 18 overlap words."""
    L = ["mov cx,9", "xor ax,ax", "rep stosw ; 9"]
    for i in range(18):
        L += ["mov ax,[es:di+%d]" % (2 * i), "add ax,2", "sar ax,1", "sar ax,1",
              "xchg bx,ax", "mov al,[bx+ENCMID]", "cbw", "shl ax,1", "xchg bx,ax",
              "mov ax,[bx+WROW%d]" % i, "add ax,[es:di+%d]" % (40 + 2 * i),
              "mov [es:di+%d],ax" % (80 + 2 * i),
              "mov ax,[bx+WROWB%d]" % i, "mov [es:di+%d],ax" % (40 + 2 * i)]
    return L


def imdct_subband_split():
    """Per active subband, window and overlap with byte-split multiplies
    (36 of them) on 16-bit z."""
    L = ["mov cx,9", "xor ax,ax", "rep stosw ; 9"]
    for i in range(18):
        for half in (0, 1):
            L += ["mov ax,[es:di+%d]" % (2 * i)]
            L += mul_split()
            if half == 0:
                L += ["add dx,[es:di+%d]" % (40 + 2 * i), "mov [es:di+%d],dx" % (80 + 2 * i)]
            else:
                L += ["mov [es:di+%d],dx" % (40 + 2 * i)]
    return L


def imdct_fast_split_ops():
    """A fast 36-point IMDCT's operation count (the 9-point-DCT
    factorisations decoders use: ~43 multiplies, ~115 additions a long
    block). Priced per operation from the Lee code's own per-op cost."""
    return 43, 115


# ------------------------------------------------------------- antialias
def aa_butterfly(form):
    """One butterfly (two lines, four products)."""
    if form == "tab":
        # codes in BX (lo) and SI (hi) as code words; outputs linear,
        # then each recoded (ENC s=2) for the IMDCT rows
        L = ["mov bx,[es:di]", "mov si,[es:di+2]",
             "mov ax,[bx+CS0]", "add ax,[si+CA0]",
             "mov dx,[si+CS0]", "sub dx,[bx+CA0]"]
        for r in ("ax", "dx"):
            L += ["xchg bx,%s" % r if r != "ax" else "xchg bx,ax", "add bx,2", "sar bx,1", "sar bx,1",
                  "mov al,[bx+ENCMID]", "cbw", "shl ax,1", "mov [es:di+4],ax"]
        return L
    if form == "split":
        L = ["mov ax,[es:di]"] + mul_split() + ["mov cx,dx"]
        L += ["mov ax,[es:di+2]"] + mul_split() + ["sub cx,dx", "mov bp,dx"]
        L += ["mov ax,[es:di+2]"] + mul_split() + ["mov si,dx"]
        L += ["mov ax,[es:di]"] + mul_split() + ["add si,dx"]
        L += ["mov [es:di],cx", "mov [es:di+2],si"]
        return L
    if form == "imul":
        L = []
        for _ in range(4):
            L += ["mov ax,[es:di]"] + mul_imul_q15() + ["mov [es:di+4],dx"]
        return L
    raise ValueError(form)


# ---------------------------------------------------------- requantiser
def requant_small():
    """|ix| <= 15: one byte table indexed by (scale exponent row, |ix|)
    gives the code word; the sign bit applied by NEG."""
    return ["mov bl,al", "and bl,15", "mov al,[bx+si]", "cbw", "shl ax,1",
            "test dh,80h", "jz rq_pos", "neg ax", "stosw"]


def requant_large():
    """|ix| > 15: pow43 from a 256-entry mantissa table plus a shift
    (ix up to 8206 reduced by 2^(4/3) steps), then ENC to the code."""
    return ["mov bx,ax", "mov cl,4", "shr bx,cl", "shl bx,1", "mov ax,[bx+P43]",
            "mov cl,[bx+P43E]", "sar ax,cl", "add ax,2", "sar ax,1", "sar ax,1",
            "xchg bx,ax", "mov al,[bx+ENCMID]", "cbw", "shl ax,1", "stosw"]


# -------------------------------------------------------------- Huffman
def huff_codeword(L):
    """One codeword through an L-bit first-level table. DX holds the bit
    window MSB-first with at least 8 valid bits, CH the valid count. The
    entry is a word: AL = x | y << 4, AH = length (a flag for longer)."""
    K = ["mov bl,dh"]
    K += ["shr bl,1"] * (8 - L)
    K += ["shl bx,1", "mov ax,[bx+HT]", "mov cl,ah", "shl dx,cl ; 5",
          "sub ch,cl", "cmp ch,8", "jge hf_ok ; taken",
          "mov bl,al", "and bl,15", "mov ah,0", "shr al,1", "shr al,1",
          "shr al,1", "shr al,1", "stosw", "mov al,bl", "stosw",
          "dec bp", "jnz hf_next ; taken"]
    return K


def huff_refill():
    """One input byte into the window (amortised per 8 bits consumed)."""
    return ["lodsb", "mov cl,8", "sub cl,ch", "xor ah,ah", "shl ax,cl ; 4",
            "or dx,ax", "add ch,8"]


def huff_sign():
    return ["shl dx,1", "jnc hs_pos", "neg word [es:di-2]", "dec ch"]


def huff_second():
    """A longer code: the escape entry names a second table indexed by the
    next bits (a bounded secondary table)."""
    return ["mov bl,dh", "shr bl,1", "shr bl,1", "shl bx,1", "add bx,[bx+HT2]",
            "mov ax,[bx]", "mov cl,ah", "shl dx,cl ; 4", "sub ch,cl"]


# --------------------------------------------------------------- expand
def expand_entry():
    """One product-table entry at OPEN: the running 32-bit sum (CX:BX,
    increment SI:BP pre-doubled, biased by 2^15) and its high word
    stored. x3xttab.py's EXACT recurrence."""
    return ["add bx,bp", "adc ax,si", "stosw"]


def pcm_out():
    return ["lodsw", "shl ax,1", "jo clip", "shl ax,1", "jo clip", "mov al,ah",
            "xor al,80h", "stosb"]


# ----------------------------------------------------------------- cost
_cache = {}


def cost(lines, reps=3, warm=2):
    key = tuple(lines)
    if key not in _cache:
        _cache[key] = (x3cyc.cost(lines, reps=reps, warm=warm), x3cyc.static_bytes(lines))
    return _cache[key]


# ------------------------------------------------- OPEN: table expansion
def expand_kernels():
    """The per-entry kernels of x3xttab.py's recurrences, as the target
    runs them (one unit = one entry or one byte)."""
    return {
        # product row, positive half: the running sum's high word stored
        "prod_pos": ["add bx,bp", "adc ax,si", "stosw"],
        # the negative half: read the mirror entry, negate, store, step back
        "prod_neg": ["mov ax,[es:si]", "neg ax", "stosw", "dec si", "dec si"],
        # byte-split L (and H) entry: the running sum, its two bytes into
        # their planes 256 apart
        "split": ["add bx,bp", "adc ax,si", "mov [es:di],al", "mov [es:di+256],ah", "inc di"],
        # the encoder walk: 2v against the next midpoint sum, advance the
        # code when it passes (rarely), store the code byte
        "enc_walk": ["cmp dx,[bx]", "jbe ew_ok ; taken", "stosb", "add dx,2"],
        "enc_mirror": ["mov al,[es:si]", "neg al", "stosb", "dec si"],
        # an 18-row IMDCT entry: one mul by the Q15 constant, rounded
        "mul_row": ["lodsw", "mul bp", "add ax,4000h", "adc dx,0", "shl ax,1", "rcl dx,1",
                    "xchg ax,dx", "stosw"],
        # fl32 over the expanded words
        "fl32": ["lodsw", "add bx,ax", "add dx,bx"],
    }
