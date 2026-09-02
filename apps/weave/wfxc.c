/* ============================================================================
 * os8088 - apps/weave/wfxc.c
 *
 * THE RESIDENT FORMULA COMPILER (WEAVE-SPEC 6.9.2, 9.4's one carve-out).
 * #included by apps/weave/weave.c after wact.c and before wgrid.c, which is
 * its only caller.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS FILE EXISTS AT ALL
 * ---------------------------------------------------------------------------
 * WEAVE-SPEC 9.4 says the runtime never parses text, and it means it: no WML
 * and no WJS is ever parsed on this machine by WEAVE. A spreadsheet whose
 * cells cannot be typed into is not one, though, and a cell's source IS text
 * - so 9.4 carves out exactly one expression language, and this is it.
 *
 * It is a FILE OF ITS OWN so that the carve-out is findable. Buried inside a
 * commit path it would read as an accident; here the exclusion and its one
 * exception are both things you can open.
 *
 * ---------------------------------------------------------------------------
 * IT IS THE WHOLE OF 5.1 AND NOT A SUBSET
 * ---------------------------------------------------------------------------
 * Two grammars for one language is the drift WEAVE-SPEC 11's byte-identity
 * rule exists to prevent, said about a language instead of about a file: a
 * subset would let a formula pack on the host, load on the machine, and
 * refuse the moment its author clicked its cell to look at it. So: 5.1's
 * recursive descent, 5.3's RPN, 5.3's depth cap, 5.4's eight functions, and
 * the same bytes tools/weavesim.py's FxCompiler would have written.
 *
 * IT IS AN OVERLAY TENANT (WEAVE-SPEC 1.2.1's number 7), and the draft of
 * 6.9.2 that said "resident" is corrected in the spec rather than quietly
 * diverged from. It runs once per Enter in the formula bar - a human's
 * gesture, never a script's, because 8.5 gives WJS no way to write a formula
 * at all - and its refusal already has a meaning: 6.9.2 has a `Formula:` line
 * for a formula that will not compile, and a module that will not load is one
 * more reason it did not. What decided it was arithmetic: the compiler and
 * the commit are ~6,000 bytes and wave 4's resident code without them is
 * already at SPEC.md 20.1's ceiling.
 *
 * So every FUNCTION here carries the `ovl_` prefix (SPEC.md 73.14) and every
 * global, literal and bss byte it names stays resident and DS-relative - which
 * is why the buffers below did not move and did not need to.
 *
 * ---------------------------------------------------------------------------
 * IT IS RECURSIVE, AND THAT IS THE ONE THING TO WATCH
 * ---------------------------------------------------------------------------
 * `atom` calls `cmp` for a parenthesised sub-expression, so the C stack depth
 * follows the formula's nesting. Each frame is under a dozen bytes (SPEC.md
 * 73.5 caps a frame at 96 and cc8086.py fails the build over it), and the
 * recursion is bounded by 5.3's stack cap of 16 tested at every push - a
 * formula that would nest deeper than that is refused before the descent gets
 * there. The UI task's stack is 1,024 bytes (SPEC.md 20.6), so 16 frames of a
 * dozen bytes is under 2% of it.
 * ==========================================================================*/

/* 5.3's opcodes, in their pinned order. */
#define FX_END    0x00
#define FX_NUM    0x01
#define FX_CELL   0x02
#define FX_RANGE  0x03
#define FX_ADD    0x04
#define FX_SUB    0x05
#define FX_MUL    0x06
#define FX_DIV    0x07
#define FX_NEG    0x08
#define FX_EQ     0x09
#define FX_NE     0x0A
#define FX_LT     0x0B
#define FX_LE     0x0C
#define FX_GT     0x0D
#define FX_GE     0x0E
#define FX_SUM    0x0F
#define FX_MIN    0x10
#define FX_MAX    0x11
#define FX_AVG    0x12
#define FX_COUNT  0x13
#define FX_IF     0x14
#define FX_ABS    0x15
#define FX_ROUND  0x16

/* The bar holds WG_BARCOLS characters and the densest formula a person can
 * type into that is roughly `1+1+1+...` - thirty FNUMs of five bytes and
 * twenty-nine one-byte operators, 179. 256 is that with room, and a formula
 * that would need more is refused rather than truncated. */
#define W_FXCMAX  256

/* ...and the compiled RPN lives in the MIDDLE of the header probe, for
 * wgrid.c's reason and with the same arithmetic: w_probe[] is a whole
 * 1,024-byte cluster because 10.1's refuse-before-read needs one, the only
 * thing that ever reads it is the 32-byte header at offset 0, and the band
 * buffer already has the last 720. That leaves 32..303 free, and W_FXCMAX is
 * 256. Nothing here can collide with either neighbour: this compiler runs at
 * a formula bar's Enter, long after the header was read, and it stops 16
 * bytes short of the band. */
/* ...and LOOM points it somewhere else. apps/loom #includes THIS FILE as its
 * FX pre-compiler (WEAVE-SPEC 1.2's rule that what the two packages share
 * they share as source), and it has no header probe to borrow - so the buffer
 * is a #define a includer may set first. WEAVE does not, and gets the line
 * below unchanged. */
#ifndef w_fxc_out
#define w_fxc_out (w_probe + W_HDR_SIZE)
#endif
static unsigned      w_fxc_n;
static const char   *w_fxc_err;         /* 0 = it compiled */
static const char   *w_fxc_p;           /* the scanner's cursor */
static int           w_fxc_cols, w_fxc_rows;
static int           w_fxc_depth, w_fxc_peak;

/* The number scanner's answers, shared with 6.9.3's step 3 (ovl_gnumber). */
static int w_gnum_lo, w_gnum_hi;

static void ovl_fxc_emit(unsigned char b)
{
    if (w_fxc_n < W_FXCMAX)
        w_fxc_out[w_fxc_n++] = b;
    else if (w_fxc_err == 0)
        w_fxc_err = "too long for one cell.";
}

/* ovl_fxc_push / ovl_fxc_pop - 5.3's depth cap, checked as the RPN is EMITTED
 * rather than after: the same arithmetic weavesim's check_depth() does over
 * the finished stream, run forwards. */
static void ovl_fxc_push(void)
{
    w_fxc_depth++;
    if (w_fxc_depth > w_fxc_peak)
        w_fxc_peak = w_fxc_depth;
    if (w_fxc_peak > 16 && w_fxc_err == 0)
        w_fxc_err = "too deep - the stack is 16 slots.";
}

static void ovl_fxc_pop(int n)
{
    w_fxc_depth -= n;
}

static void ovl_fxc_ws(void)
{
    while (*w_fxc_p == ' ' || *w_fxc_p == '\t')
        w_fxc_p++;
}

static int ovl_fxc_digit(char c)
{
    return c >= '0' && c <= '9';
}

static int ovl_fxc_alpha(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static char ovl_fxc_upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
}

/* ============================================================================
 * 5.1's `number`, shared by the compiler and by 6.9.3's classification
 *
 * `(digits << 16) + (fraction * 65536 + d/2) / d` with d = 10^len, which is
 * the host packer's parse_number_16_16 to the letter. The fraction is bounded
 * at FOUR digits (5.1) and that bound is what lets the conversion be one
 * 16-bit divide - wfx_frac, in the core that already owns 16.16.
 * ==========================================================================*/

static const unsigned w_fxc_p10[5] = { 1, 10, 100, 1000, 10000 };

static int ovl_fxc_number(void)
{
    unsigned ip, frac, den, digits, t;

    if (!ovl_fxc_digit(*w_fxc_p))
        return 0;
    ip = 0;
    while (ovl_fxc_digit(*w_fxc_p)) {
        if (ip > 3276 || (ip == 3276 && *w_fxc_p > '7')) {
            w_fxc_err = "|value| < 32768.";
            return 0;
        }
        t = ip * 10;
        ip = t + (unsigned)(*w_fxc_p++ - '0');
    }
    frac = 0;
    if (*w_fxc_p == '.') {
        w_fxc_p++;
        digits = 0;
        while (ovl_fxc_digit(*w_fxc_p)) {
            if (digits >= 4) {
                w_fxc_err = "at most 4 decimals - 16.16 stops there.";
                return 0;
            }
            digits++;
            t = frac * 10;
            frac = t + (unsigned)(*w_fxc_p++ - '0');
        }
        den = w_fxc_p10[digits];        /* a table, not a running product: an
                                         * 8086 multiply-by-constant is a
                                         * shift/add chain that writes FLAGS,
                                         * and a loop test right after one is
                                         * what cc8086.py refuses */
        if (digits == 0) {
            w_fxc_err = "a digit must follow the point.";
            return 0;
        }
        frac = wfx_frac(frac, den);
    }
    w_gnum_hi = (int)ip;
    w_gnum_lo = (int)frac;
    return 1;
}

/* ovl_gnumber - 6.9.3 step 3: does the WHOLE of `s` spell a 5.1 number?
 * Answers 1 with the value in w_gnum_lo / w_gnum_hi. */
static int ovl_gnumber(const char *s)
{
    int neg;

    w_fxc_err = 0;
    neg = 0;
    if (*s == '-') {
        neg = 1;
        s++;
    }
    w_fxc_p = s;
    if (!ovl_fxc_number())
        return 0;
    if (*w_fxc_p != 0)
        return 0;                       /* trailing rubbish: it is a label */
    if (neg) {
        w_gnum_lo = -w_gnum_lo;
        w_gnum_hi = -w_gnum_hi;
        if (w_gnum_lo != 0)
            w_gnum_hi--;                /* a 32-bit negate in two words */
    }
    return 1;
}

/* ============================================================================
 * THE DESCENT (WEAVE-SPEC 5.1)
 * ==========================================================================*/

static void ovl_fxc_cmp(void);

/* ovl_fxc_cellref - a column letter and a row number, into (row, col) 0-based.
 * -1 = this is not a cell reference and the cursor has not moved. */
static int w_fxc_r, w_fxc_c;

static int ovl_fxc_cellref(void)
{
    const char *save;
    int c, r, c2;

    save = w_fxc_p;
    if (!ovl_fxc_alpha(*w_fxc_p))
        return 0;
    c = ovl_fxc_upper(*w_fxc_p) - 'A';
    w_fxc_p++;
    if (!ovl_fxc_digit(*w_fxc_p)) {
        w_fxc_p = save;
        return 0;                       /* a name, not a reference */
    }
    r = 0;
    while (ovl_fxc_digit(*w_fxc_p)) {
        c2 = r * 10;
        r = c2 + (*w_fxc_p++ - '0');
        if (r > 999) {
            w_fxc_err = "a row is 1..256.";
            return 0;
        }
    }
    if (ovl_fxc_alpha(*w_fxc_p)) {        /* `SUM(` - a function, not A1 */
        w_fxc_p = save;
        return 0;
    }
    r--;
    if (c < 0 || c >= w_fxc_cols || r < 0 || r >= w_fxc_rows) {
        w_fxc_err = "that cell is outside the grid.";
        return 0;
    }
    w_fxc_r = r;
    w_fxc_c = c;
    return 1;
}

/* ovl_fxc_fn - the function name at the cursor, as its opcode, or 0.
 * 5.4 IS the whole set and a name outside it refuses by name. */
static int w_fxc_argc;                  /* 0 = one range, else the arity */

static char w_fxc_name[8];              /* STATIC: SS != DS, so the address of
                                         * an automatic is a stack offset
                                         * dereferenced through the package
                                         * segment, and tools/cc8086.py fails
                                         * the build over it (SPEC.md 73.5) */

static int ovl_fxc_fn(void)
{
    int k;
#define n w_fxc_name

    k = 0;
    while (ovl_fxc_alpha(*w_fxc_p) && k < 7)
        n[k++] = ovl_fxc_upper(*w_fxc_p++);
    n[k] = 0;
    w_fxc_argc = 0;
    if (n[0] == 'S' && n[1] == 'U' && n[2] == 'M' && n[3] == 0)
        return FX_SUM;
    if (n[0] == 'M' && n[1] == 'I' && n[2] == 'N' && n[3] == 0)
        return FX_MIN;
    if (n[0] == 'M' && n[1] == 'A' && n[2] == 'X' && n[3] == 0)
        return FX_MAX;
    if (n[0] == 'A' && n[1] == 'V' && n[2] == 'G' && n[3] == 0)
        return FX_AVG;
    if (n[0] == 'C' && n[1] == 'O' && n[2] == 'U' && n[3] == 'N'
            && n[4] == 'T' && n[5] == 0)
        return FX_COUNT;
    w_fxc_argc = 3;
    if (n[0] == 'I' && n[1] == 'F' && n[2] == 0)
        return FX_IF;
    w_fxc_argc = 1;
    if (n[0] == 'A' && n[1] == 'B' && n[2] == 'S' && n[3] == 0)
        return FX_ABS;
    if (n[0] == 'R' && n[1] == 'O' && n[2] == 'U' && n[3] == 'N'
            && n[4] == 'D' && n[5] == 0)
        return FX_ROUND;
    w_fxc_err = "SUM MIN MAX AVG COUNT IF ABS ROUND is the whole set.";
    return 0;
#undef n
}

static void ovl_fxc_atom(int range_ok)
{
    int op, k, r1, c1, r2, c2, t;

    ovl_fxc_ws();
    if (w_fxc_err)
        return;
    if (*w_fxc_p == '(') {
        w_fxc_p++;
        ovl_fxc_cmp();
        ovl_fxc_ws();
        if (*w_fxc_p != ')') {
            if (!w_fxc_err)
                w_fxc_err = "a ')' is missing.";
            return;
        }
        w_fxc_p++;
        return;
    }
    if (ovl_fxc_digit(*w_fxc_p)) {
        if (!ovl_fxc_number())
            return;
        ovl_fxc_emit(FX_NUM);
        ovl_fxc_emit((unsigned char)(w_gnum_lo & 0xFF));
        ovl_fxc_emit((unsigned char)((w_gnum_lo >> 8) & 0xFF));
        ovl_fxc_emit((unsigned char)(w_gnum_hi & 0xFF));
        ovl_fxc_emit((unsigned char)((w_gnum_hi >> 8) & 0xFF));
        ovl_fxc_push();
        return;
    }
    if (ovl_fxc_cellref()) {
        r1 = w_fxc_r;
        c1 = w_fxc_c;
        ovl_fxc_ws();
        if (*w_fxc_p == ':') {
            if (!range_ok) {
                w_fxc_err = "a range is legal only in an aggregate.";
                return;
            }
            w_fxc_p++;
            ovl_fxc_ws();
            if (!ovl_fxc_cellref()) {
                if (!w_fxc_err)
                    w_fxc_err = "a cell must follow the ':'.";
                return;
            }
            r2 = w_fxc_r;
            c2 = w_fxc_c;
            ovl_fxc_emit(FX_RANGE);       /* normalized, as the packer's is */
            ovl_fxc_emit((unsigned char)(r1 < r2 ? r1 : r2));
            ovl_fxc_emit((unsigned char)(c1 < c2 ? c1 : c2));
            ovl_fxc_emit((unsigned char)(r1 > r2 ? r1 : r2));
            ovl_fxc_emit((unsigned char)(c1 > c2 ? c1 : c2));
            ovl_fxc_push();
            return;
        }
        ovl_fxc_emit(FX_CELL);
        ovl_fxc_emit((unsigned char)r1);
        ovl_fxc_emit((unsigned char)c1);
        ovl_fxc_push();
        return;
    }
    if (w_fxc_err)
        return;
    if (!ovl_fxc_alpha(*w_fxc_p)) {
        w_fxc_err = "a number, a cell or a function is needed.";
        return;
    }
    op = ovl_fxc_fn();
    if (op == 0)
        return;
    t = w_fxc_argc;
    ovl_fxc_ws();
    if (*w_fxc_p != '(') {
        w_fxc_err = "a '(' must follow the function.";
        return;
    }
    w_fxc_p++;
    if (t == 0) {
        k = (int)w_fxc_n;
        ovl_fxc_atom(1);
        if (w_fxc_err)
            return;
        if ((int)w_fxc_n != k + 5 || w_fxc_out[k] != FX_RANGE) {
            w_fxc_err = "an aggregate takes exactly one range.";
            return;
        }
    } else {
        for (k = 0; k < t; k++) {
            if (k) {
                ovl_fxc_ws();
                if (*w_fxc_p != ',') {
                    w_fxc_err = "the arguments need a ',' between them.";
                    return;
                }
                w_fxc_p++;
            }
            ovl_fxc_cmp();
            if (w_fxc_err)
                return;
        }
        ovl_fxc_pop(t - 1);
    }
    ovl_fxc_ws();
    if (*w_fxc_p != ')') {
        w_fxc_err = "a ')' is missing.";
        return;
    }
    w_fxc_p++;
    ovl_fxc_emit((unsigned char)op);
}

static void ovl_fxc_factor(void)
{
    ovl_fxc_ws();
    if (*w_fxc_p == '-') {
        w_fxc_p++;
        ovl_fxc_atom(0);
        if (w_fxc_err)
            return;
        ovl_fxc_emit(FX_NEG);
        return;
    }
    ovl_fxc_atom(0);
}

static void ovl_fxc_term(void)
{
    char c;

    ovl_fxc_factor();
    for (;;) {
        if (w_fxc_err)
            return;
        ovl_fxc_ws();
        c = *w_fxc_p;
        if (c != '*' && c != '/')
            return;
        w_fxc_p++;
        ovl_fxc_factor();
        if (w_fxc_err)
            return;
        ovl_fxc_emit((unsigned char)(c == '*' ? FX_MUL : FX_DIV));
        ovl_fxc_pop(1);
    }
}

static void ovl_fxc_sum(void)
{
    char c;

    ovl_fxc_term();
    for (;;) {
        if (w_fxc_err)
            return;
        ovl_fxc_ws();
        c = *w_fxc_p;
        if (c != '+' && c != '-')
            return;
        w_fxc_p++;
        ovl_fxc_term();
        if (w_fxc_err)
            return;
        ovl_fxc_emit((unsigned char)(c == '+' ? FX_ADD : FX_SUB));
        ovl_fxc_pop(1);
    }
}

static void ovl_fxc_cmp(void)
{
    int op;

    ovl_fxc_sum();
    if (w_fxc_err)
        return;
    ovl_fxc_ws();
    op = 0;
    if (*w_fxc_p == '=') {
        op = FX_EQ;
        w_fxc_p++;
    } else if (*w_fxc_p == '<' && w_fxc_p[1] == '>') {
        op = FX_NE;
        w_fxc_p += 2;
    } else if (*w_fxc_p == '<' && w_fxc_p[1] == '=') {
        op = FX_LE;
        w_fxc_p += 2;
    } else if (*w_fxc_p == '>' && w_fxc_p[1] == '=') {
        op = FX_GE;
        w_fxc_p += 2;
    } else if (*w_fxc_p == '<') {
        op = FX_LT;
        w_fxc_p++;
    } else if (*w_fxc_p == '>') {
        op = FX_GT;
        w_fxc_p++;
    }
    if (op == 0)
        return;
    ovl_fxc_sum();
    if (w_fxc_err)
        return;
    ovl_fxc_emit((unsigned char)op);
    ovl_fxc_pop(1);
}

/* ovl_fxc - compile `src` (past the '=') for a cols x rows sheet.
 * 1 = the RPN is in w_fxc_out[0 .. w_fxc_n), FEND included.
 * 0 = w_fxc_err holds the sentence, in 10.5's voice with no file or line to
 *     name - which is what 6.9.2 says the runtime's half of it looks like. */
static int ovl_fxc(const char *src, int cols, int rows)
{
    w_fxc_err = 0;
    w_fxc_n = 0;
    w_fxc_p = src;
    w_fxc_cols = cols;
    w_fxc_rows = rows;
    w_fxc_depth = 0;
    w_fxc_peak = 0;
    ovl_fxc_cmp();
    if (w_fxc_err)
        return 0;
    ovl_fxc_ws();
    if (*w_fxc_p != 0) {
        w_fxc_err = "there is something after the formula.";
        return 0;
    }
    if (w_fxc_depth != 1) {
        w_fxc_err = "the formula is incomplete.";
        return 0;
    }
    ovl_fxc_emit(FX_END);
    return w_fxc_err == 0;
}
