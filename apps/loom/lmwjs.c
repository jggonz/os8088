/* ============================================================================
 * os8088 - apps/loom/lmwjs.c
 *
 * THE WJS COMPILER (WEAVE-SPEC 4), LOOM.OVL's second tenant (WEAVE-SPEC 1.2):
 * the tokenizer, the top-level collector and the code generator.
 *
 * ---------------------------------------------------------------------------
 * IT IS WRITTEN FROM 4.6's NORMATIVE TEMPLATES AND FROM NOTHING ELSE
 * ---------------------------------------------------------------------------
 * WEAVE-SPEC 4.6 pins emission per production precisely because two
 * independent compilers must emit identical bytes: "no optimisation of any
 * kind: no constant folding, no dead-code elimination, no peephole, no
 * strength-reduction. Left operands compile before right operands; arguments
 * compile left to right." Every template in 4.6.1 to 4.6.6 appears below as a
 * comment beside the code that emits it, so the two can be read against each
 * other line for line. Where this file and 4.6 disagree, 4.6 is right.
 *
 * Three things a second implementer gets wrong, each stated where it lands:
 *
 *   - A FUNCTION WITH NO `return` ENDS `PUSHN` `RET`, ALWAYS - emitted at the
 *     close of every body whether or not the last statement was a return.
 *     There is no reachability analysis anywhere in this family (4.5).
 *   - A `for` LOOP'S STEP IS COMPILED AFTER THE BODY (4.6.3's `Lstep`), so
 *     the step's tokens are SKIPPED on the way in and revisited on the way
 *     out. A compiler that emits the step where it reads it produces a
 *     correct loop with different bytes.
 *   - `&&` AND `||` YIELD THE DECIDING VALUE, not a bool (4.4), which is why
 *     4.6.4's template is `DUP JZ Lend POP [b]` and not a branch.
 *
 * ---------------------------------------------------------------------------
 * THE TOKENS LIVE IN THE SCRATCH CLAIM
 * ---------------------------------------------------------------------------
 * A token is eight bytes at LMW_TOKS (loom.h's LMT_* row) and an identifier's
 * TEXT is never copied - the token keeps a span of the source, which stays
 * resident in the project slot for the whole pack. So every name comparison
 * in this file is a span compare, which is also why there is no symbol table:
 * 4.2 caps a program at 128 globals, 128 functions and 16 locals, and a
 * linear walk of 128 spans is microseconds against the 400 ms this pack will
 * spend on one `int 13h` (CLAUDE.md's cost table).
 * ==========================================================================*/

/* --- 4.2's keywords, in the order their indices are used ----------------- */
#define LMK_VAR       0
#define LMK_FUNCTION  1
#define LMK_IF        2
#define LMK_ELSE      3
#define LMK_WHILE     4
#define LMK_FOR       5
#define LMK_BREAK     6
#define LMK_CONTINUE  7
#define LMK_RETURN    8
#define LMK_TRUE      9
#define LMK_FALSE    10
#define LMK_NULL     11
#define LMK_N        12

static const char *lmj_kw[LMK_N] = {
    "var", "function", "if", "else", "while", "for", "break", "continue",
    "return", "true", "false", "null"
};

/* ...and the punctuators, LONGEST FIRST, which is the order weavesim's
 * JS_PUNCT list is written in and the reason `==` never scans as two `=`. */
#define LMP_AND    0
#define LMP_OR     1
#define LMP_EQ     2
#define LMP_NE     3
#define LMP_LE     4
#define LMP_GE     5
#define LMP_INC    6
#define LMP_DEC    7
#define LMP_LBRACE 8
#define LMP_RBRACE 9
#define LMP_LPAR  10
#define LMP_RPAR  11
#define LMP_LBRK  12
#define LMP_RBRK  13
#define LMP_SEMI  14
#define LMP_COMMA 15
#define LMP_PLUS  16
#define LMP_MINUS 17
#define LMP_STAR  18
#define LMP_SLASH 19
#define LMP_PCT   20
#define LMP_LT    21
#define LMP_GT    22
#define LMP_ASSIGN 23
#define LMP_BANG  24
#define LMP_DOT   25
#define LMP_N     26

static const char *lmj_punct[LMP_N] = {
    "&&", "||", "==", "!=", "<=", ">=", "++", "--", "{", "}", "(", ")",
    "[", "]", ";", ",", "+", "-", "*", "/", "%", "<", ">", "=", "!", "."
};

/* --- 8.1's builtins: name, min argc, max argc ---------------------------- */
static const char *lmj_bname[12] = {
    "alert", "timer", "saveState", "loadState", "playSound", "tone",
    "str", "len", "substr", "find", "rand", "array"
};
static const unsigned char lmj_blo[12] = { 1, 2, 0, 0, 1, 2, 1, 1, 3, 2, 1, 1 };
static const unsigned char lmj_bhi[12] = { 2, 2, 0, 0, 1, 2, 1, 1, 3, 2, 1, 1 };

/* --- 4.5's opcodes ------------------------------------------------------- */
#define OP_HALT  0x00
#define OP_PUSHI 0x01
#define OP_PUSHA 0x02
#define OP_PUSHN 0x03
#define OP_PUSHB 0x04
#define OP_PUSHC 0x05
#define OP_LDG   0x06
#define OP_STG   0x07
#define OP_LDL   0x08
#define OP_STL   0x09
#define OP_POP   0x0A
#define OP_DUP   0x0B
#define OP_ADD   0x0C
#define OP_SUB   0x0D
#define OP_MUL   0x0E
#define OP_DIV   0x0F
#define OP_MOD   0x10
#define OP_NEG   0x11
#define OP_EQ    0x12
#define OP_NE    0x13
#define OP_LT    0x14
#define OP_LE    0x15
#define OP_GT    0x16
#define OP_GE    0x17
#define OP_NOT   0x18
#define OP_JMP   0x19
#define OP_JZ    0x1A
#define OP_JNZ   0x1B
#define OP_CALL  0x1C
#define OP_RET   0x1D
#define OP_GETP  0x1E
#define OP_SETP  0x1F
#define OP_CALLM 0x20
#define OP_BUILT 0x21
#define OP_INCG  0x22
#define OP_DECG  0x23
#define OP_AGET  0x24
#define OP_ASET  0x25

/* --- 2.7.1's property and method names, and 6's per-ctype surface --------
 * PROP_ATOMS is every well-known name with an id of 1..24; METHOD names are
 * the ten with an arity. The surfaces are NUL-terminated atom lists, indexed
 * by ctype, and `hidden` is on every flow component's get AND set because 6.12
 * makes hide/show the dynamic UI. */
static const char *lmj_pname[24] = {
    "text", "value", "label", "enabled", "checked", "hidden", "x", "y",
    "vx", "vy", "frame", "shown", "min", "max", "rows", "cols", "sel",
    "editing", "group", "card", "selrow", "selcol", "walls", "tick"
};

#define LMJ_NMETH 10
static const char *lmj_mname[LMJ_NMETH] = {
    "cell", "setCell", "recalc", "select", "stop", "go", "set", "get",
    "start", "clear"
};
static const unsigned char lmj_matom[LMJ_NMETH] = {
    WA_CELL, WA_SETCELL, WA_RECALC, WA_SELECT, WA_STOP, WA_GO, WA_SET,
    WA_GET, WA_START, WA_CLEAR
};
static const unsigned char lmj_marity[LMJ_NMETH] = {
    2, 3, 0, 2, 0, 1, 2, 1, 1, 0
};

static const unsigned char sfg_label[]  = { WA_TEXT, WA_HIDDEN, 0 };
static const unsigned char sfs_label[]  = { WA_TEXT, WA_HIDDEN, 0 };
static const unsigned char sf_none[]    = { WA_HIDDEN, 0 };
static const unsigned char sfg_meter[]  = { WA_VALUE, WA_MAX, WA_HIDDEN, 0 };
static const unsigned char sfs_meter[]  = { WA_VALUE, WA_HIDDEN, 0 };
static const unsigned char sf_button[]  = { WA_LABEL, WA_ENABLED,
                                            WA_HIDDEN, 0 };
static const unsigned char sfg_check[]  = { WA_CHECKED, WA_ENABLED,
                                            WA_LABEL, WA_HIDDEN, 0 };
static const unsigned char sfs_check[]  = { WA_CHECKED, WA_ENABLED,
                                            WA_HIDDEN, 0 };
static const unsigned char sfg_input[]  = { WA_TEXT, WA_COLS, WA_ENABLED,
                                            WA_HIDDEN, 0 };
static const unsigned char sfs_input[]  = { WA_TEXT, WA_ENABLED,
                                            WA_HIDDEN, 0 };
static const unsigned char sfg_list[]   = { WA_SEL, WA_ROWS, WA_HIDDEN, 0 };
static const unsigned char sfs_list[]   = { WA_SEL, WA_HIDDEN, 0 };
static const unsigned char sfg_grid[]   = { WA_SELROW, WA_SELCOL, WA_ROWS,
                                            WA_COLS, WA_HIDDEN, 0 };
static const unsigned char sf_sprite[]  = { WA_X, WA_Y, WA_VX, WA_VY,
                                            WA_FRAME, WA_SHOWN, 0 };
static const unsigned char sf_empty[]   = { 0 };
static const unsigned char sfm_list[]   = { WA_SET, WA_GET, 0 };
static const unsigned char sfm_grid[]   = { WA_CELL, WA_SETCELL, WA_RECALC,
                                            WA_SELECT, WA_CLEAR, 0 };
static const unsigned char sfm_canvas[] = { WA_START, WA_STOP, 0 };
static const unsigned char sfm_app[]    = { WA_GO, 0 };

/* Indexed by ctype 0x01..0x0E; index 0 is the APP (comp_id 0). */
static const unsigned char *lmj_get[15] = {
    sf_empty, sfg_label, sfg_label, sf_none, sf_none, sf_none, sfg_meter,
    sf_button, sfg_check, sfg_check, sfg_input, sfg_list, sfg_grid,
    sf_none, sf_sprite
};
static const unsigned char *lmj_set[15] = {
    sf_empty, sfs_label, sfs_label, sf_none, sf_none, sf_none, sfs_meter,
    sf_button, sfs_check, sfs_check, sfs_input, sfs_list, sf_none,
    sf_none, sf_sprite
};
static const unsigned char *lmj_meth[15] = {
    sfm_app, sf_empty, sf_empty, sf_empty, sf_empty, sf_empty, sf_empty,
    sf_empty, sf_empty, sf_empty, sf_empty, sfm_list, sfm_grid, sfm_canvas,
    sf_empty
};
static const char *lmj_ctname[15] = {
    "app", "label", "text", "rule", "box", "spacer", "meter", "button",
    "check", "radio", "input", "list", "grid", "canvas", "sprite"
};

/* --- the tokenizer's state ----------------------------------------------- */
static int      lmj_ntok;
static unsigned lmj_i;              /* the scanner's cursor over the WJS */
static int      lmj_line;

static unsigned lmj_trow(int k)
{
    return LMW_TOKS + (unsigned) k * LM_TOKSZ;
}

/* EVERY TOKEN READ IS BOUNDS-CHECKED, and it is a rule about hostile bytes
 * rather than about correctness. A `.WJS` is a file somebody typed, so a
 * malformed one can walk the cursor past the end - and past the end of
 * LMW_TOKS is the next region of the scratch claim, whose bytes would decode
 * as tokens and keep the parser going. An index past the last token reads as
 * EOF, which every loop in this file already terminates on. WEAVE-SPEC
 * 4.5.1's argument, made one level earlier. */
static int lmj_kind(int k)
{
    if (k < 0 || k >= lmj_ntok)
        return LMTK_EOF;
    return (int) lm_wb(lmj_trow(k) + LMT_KIND);
}

static int lmj_aux(int k)
{
    if (k < 0 || k >= lmj_ntok)
        return 0;
    return (int) lm_wb(lmj_trow(k) + LMT_AUX);
}

static int lmj_tline(int k)
{
    if (k < 0 || k >= lmj_ntok)
        return 0;
    return (int) lm_ww(lmj_trow(k) + LMT_LINE);
}

static int lmj_val(int k)
{
    if (k < 0 || k >= lmj_ntok)
        return 0;
    return (int) lm_ww(lmj_trow(k) + LMT_VAL);
}

static unsigned lmj_toff(int k)
{
    if (k < 0 || k >= lmj_ntok)
        return 0;
    return lm_ww(lmj_trow(k) + LMT_OFF);
}

static void lmj_err(int tok, const char *msg)
{
    lm_perr(LM_SLOT_WJS, lmj_tline(tok), msg);
}

/* The message scratch, shared with lmwml.c's lmw_msg through the same
 * appenders - one buffer in the whole overlay, because lm_perr() keeps the
 * first message and SPEC.md 73.5 forbids a 160-byte automatic. */
static void lmj_m0(const char *s)
{
    lmw_msg[0] = 0;
    lm_cat(lmw_msg, s);
}

static void lmj_mtok(int tok)
{
    unsigned n = os88_strlen(lmw_msg);
    unsigned k;
    unsigned len = (unsigned) lmj_val(tok);
    unsigned off = lmj_toff(tok);

    for (k = 0; k < len && n + 1 < LM_ERRMAX; k++) {
        lmw_msg[n] = (char) lm_sb(LM_SLOT_WJS, off + k);
        n++;
    }
    lmw_msg[n] = 0;
}

/* --- the tokenizer -------------------------------------------------------
 * 2.14 rule 3b: STRING LITERALS INTERN HERE, in token order. That single
 * sentence is why the tokenizer runs before code generation rather than
 * inside it. */
static unsigned lmj_at(unsigned k)
{
    return lm_sb(LM_SLOT_WJS, k);
}

static void lmj_adv(unsigned n)
{
    unsigned k;

    for (k = 0; k < n; k++) {
        if (lmj_at(lmj_i) == '\n')
            lmj_line++;
        lmj_i++;
    }
}

static int lmj_push(int kind, int aux, int line, int val, unsigned off)
{
    unsigned r;

    if (lmj_ntok >= LM_MAXTOK) {
        lm_perr(LM_SLOT_WJS, line, "the script is over 1280 tokens; this "
                "machine packs that many (WEAVE-SPEC 11.4)");
        return 0;
    }
    r = lmj_trow(lmj_ntok);
    lm_wpb(r + LMT_KIND, (unsigned) kind);
    lm_wpb(r + LMT_AUX, (unsigned) aux);
    lm_wpw(r + LMT_LINE, (unsigned) line);
    lm_wpw(r + LMT_VAL, (unsigned) val & 0xFFFF);
    lm_wpw(r + LMT_OFF, off);
    lmj_ntok++;
    return 1;
}

static int ovl_tokenize(void)
{
    unsigned n = lm_srclen(LM_SLOT_WJS);

    lmj_ntok = 0;
    lmj_i = 0;
    lmj_line = 1;
    while (lmj_i < n) {
        int c = (int) lmj_at(lmj_i);

        if (c == '\n' || c == ' ' || c == '\t' || c == '\r') {
            lmj_adv(1);
            continue;
        }
        if (c == '/' && lmj_at(lmj_i + 1) == '/') {
            while (lmj_i < n && lmj_at(lmj_i) != '\n')
                lmj_adv(1);
            continue;
        }
        if (c == '/' && lmj_at(lmj_i + 1) == '*') {
            unsigned k = lmj_i + 2;
            for (;;) {
                if (k + 1 >= n) {
                    /* The literal is split so that the two bytes of a
                     * block-comment opener never stand together in this
                     * file: tests/unit/t_swallow.py reads C with a scanner
                     * rather than a C parser, and its own header says the
                     * fix is to write the source differently rather than to
                     * except the file. */
                    lm_perr(LM_SLOT_WJS, lmj_line,
                            "unterminated /" "* comment");
                    return 0;
                }
                if (lm_sb(LM_SLOT_WJS, k) == '*'
                    && lm_sb(LM_SLOT_WJS, k + 1) == '/')
                    break;
                k++;
            }
            lmj_adv(k + 2 - lmj_i);
            continue;
        }
        if (c == '"') {
            int start = lmj_line;
            unsigned k = lmj_i + 1;
            int a;

            lm_sbclear();
            for (;;) {
                int ch;
                if (k >= n || lm_sb(LM_SLOT_WJS, k) == '\n') {
                    lm_perr(LM_SLOT_WJS, start, "unterminated string");
                    return 0;
                }
                ch = (int) lm_sb(LM_SLOT_WJS, k);
                if (ch == '"') {
                    k++;
                    break;
                }
                if (ch == '\\') {
                    int e = (int) lm_sb(LM_SLOT_WJS, k + 1);
                    if (e == 'n')
                        ch = '\n';
                    else if (e == '"' || e == '\\')
                        ch = e;
                    else {
                        lmj_m0("\\");
                        {
                            unsigned m = os88_strlen(lmw_msg);
                            lmw_msg[m] = (char) (e ? e : ' ');
                            lmw_msg[m + 1] = 0;
                        }
                        lm_cat(lmw_msg, ": the escapes are \\\" \\\\ \\n "
                               "(WEAVE-SPEC 4.2)");
                        lm_perr(LM_SLOT_WJS, start, lmw_msg);
                        return 0;
                    }
                    k += 2;
                } else {
                    k++;
                }
                {
                    int f = lm_fold(ch);
                    if (f >= 0)
                        lm_sbputc(f);
                }
            }
            if (lm_sbn == 0) {
                lm_perr(LM_SLOT_WJS, start, "empty string: an atom is 1..255 "
                        "bytes (WEAVE-SPEC 2.7)");
                return 0;
            }
            a = ovl_intern(LM_SLOT_WJS, start);
            if (a == 0)
                return 0;
            if (!lmj_push(LMTK_STR, 0, start, a, lmj_i + 1))
                return 0;
            lmj_adv(k - lmj_i);
            continue;
        }
        if (lm_isdigit(c)) {
            unsigned v = 0;
            int over = 0;
            unsigned s = lmj_i;
            unsigned k = lmj_i;
            /* No `long` (SPEC.md 73.7 - SmallerC refuses the TOKEN), so the
             * accumulator STOPS at a value no bound can accept rather than
             * growing past 16 bits. 4.2 caps a literal at 32767 and the
             * message quotes the text that was written. */
            while (k < n && lm_isdigit((int) lm_sb(LM_SLOT_WJS, k))) {
                if (v > 3276)
                    over = 1;
                else
                    v = v * 10 + (unsigned) (lm_sb(LM_SLOT_WJS, k) - '0');
                k++;
            }
            if (over || v > 32767) {
                lmj_m0("");
                {
                    unsigned m = os88_strlen(lmw_msg);
                    unsigned q;
                    for (q = s; q < k && m + 1 < LM_ERRMAX; q++) {
                        lmw_msg[m] = (char) lm_sb(LM_SLOT_WJS, q);
                        m++;
                    }
                    lmw_msg[m] = 0;
                }
                lm_cat(lmw_msg, ": numbers are 0..32767; 16-bit int is THE "
                       "number type (WEAVE-SPEC 4.1)");
                lm_perr(LM_SLOT_WJS, lmj_line, lmw_msg);
                return 0;
            }
            if (!lmj_push(LMTK_NUM, 0, lmj_line, (int) v, s))
                return 0;
            lmj_adv(k - lmj_i);
            continue;
        }
        if (lm_isalpha(c) || c == '_') {
            unsigned s = lmj_i;
            unsigned k = lmj_i;
            int kw = -1;
            int q;

            while (k < n && (lm_isalnum((int) lm_sb(LM_SLOT_WJS, k))
                             || lm_sb(LM_SLOT_WJS, k) == '_'))
                k++;
            if (k - s > 31) {
                lmj_m0("");
                {
                    unsigned m = os88_strlen(lmw_msg);
                    unsigned j;
                    for (j = s; j < k && m + 1 < LM_ERRMAX; j++) {
                        lmw_msg[m] = (char) lm_sb(LM_SLOT_WJS, j);
                        m++;
                    }
                    lmw_msg[m] = 0;
                }
                lm_cat(lmw_msg, ": identifiers are 31 chars at most");
                lm_perr(LM_SLOT_WJS, lmj_line, lmw_msg);
                return 0;
            }
            for (q = 0; q < LMK_N; q++)
                if (lm_srceq(LM_SLOT_WJS, s, k - s, lmj_kw[q]))
                    kw = q;
            if (!lmj_push(kw >= 0 ? LMTK_KW : LMTK_ID, kw >= 0 ? kw : 0,
                          lmj_line, (int) (k - s), s))
                return 0;
            lmj_adv(k - lmj_i);
            continue;
        }
        {
            int q;
            for (q = 0; q < LMP_N; q++) {
                unsigned l = os88_strlen(lmj_punct[q]);
                if (lm_srceq(LM_SLOT_WJS, lmj_i, l, lmj_punct[q])) {
                    if (!lmj_push(LMTK_PUNCT, q, lmj_line, (int) l, lmj_i))
                        return 0;
                    lmj_adv(l);
                    break;
                }
            }
            if (q == LMP_N) {
                lmj_m0("cannot read '");
                {
                    unsigned m = os88_strlen(lmw_msg);
                    lmw_msg[m] = (char) c;
                    lmw_msg[m + 1] = 0;
                }
                lm_cat(lmw_msg, "'");
                lm_perr(LM_SLOT_WJS, lmj_line, lmw_msg);
                return 0;
            }
        }
    }
    return lmj_push(LMTK_EOF, 0, lmj_line, 0, lmj_i);
}

/* --- name comparisons ---------------------------------------------------- */

static int lmj_toklit(int k, const char *lit)
{
    if (lmj_kind(k) != LMTK_ID && lmj_kind(k) != LMTK_KW)
        return 0;
    return lm_srceq(LM_SLOT_WJS, lmj_toff(k), (unsigned) lmj_val(k), lit);
}

static int lmj_tokeq(int a, int b)
{
    unsigned la = (unsigned) lmj_val(a);
    unsigned lb = (unsigned) lmj_val(b);
    unsigned k;

    if (la != lb)
        return 0;
    for (k = 0; k < la; k++)
        if (lm_sb(LM_SLOT_WJS, lmj_toff(a) + k)
            != lm_sb(LM_SLOT_WJS, lmj_toff(b) + k))
            return 0;
    return 1;
}

/* A WJS identifier against a WML span (a component or card id). */
static int lmj_tokwml(int t, unsigned woff, unsigned wlen)
{
    unsigned la = (unsigned) lmj_val(t);
    unsigned k;

    if (la != wlen)
        return 0;
    for (k = 0; k < la; k++)
        if (lm_sb(LM_SLOT_WJS, lmj_toff(t) + k) != lm_sb(LM_SLOT_WML,
                                                         woff + k))
            return 0;
    return 1;
}

static int lmj_builtin(int t)
{
    int i;

    if (lmj_kind(t) != LMTK_ID)
        return -1;
    for (i = 0; i < 12; i++)
        if (lmj_toklit(t, lmj_bname[i]))
            return i;
    return -1;
}

/* --- the collector (4.1: top level is declarations only) ----------------- */

static unsigned lmj_grow(int i)
{
    return LMW_GLOBS + (unsigned) i * LM_GLOBSZ;
}

static unsigned lmj_frow(int i)
{
    return LMW_FUNCS + (unsigned) i * LM_FUNCSZ;
}

static int lmj_gfind(int t)
{
    int i;

    for (i = 0; i < lm_nglob; i++) {
        unsigned r = lmj_grow(i);
        unsigned off = lm_ww(r + LMG_NAMEOFF);
        unsigned len = lm_wb(r + LMG_NAMELEN);
        unsigned k;

        if (len != (unsigned) lmj_val(t))
            continue;
        for (k = 0; k < len; k++)
            if (lm_sb(LM_SLOT_WJS, off + k) != lm_sb(LM_SLOT_WJS,
                                                     lmj_toff(t) + k))
                break;
        if (k == len)
            return i;
    }
    return -1;
}

static int lmj_ffind(int t)
{
    int i;

    for (i = 0; i < lm_nfunc; i++) {
        unsigned r = lmj_frow(i);
        unsigned off = lm_ww(r + LMF_NAMEOFF);
        unsigned len = lm_wb(r + LMF_NAMELEN);
        unsigned k;

        if (len != (unsigned) lmj_val(t))
            continue;
        for (k = 0; k < len; k++)
            if (lm_sb(LM_SLOT_WJS, off + k) != lm_sb(LM_SLOT_WJS,
                                                     lmj_toff(t) + k))
                break;
        if (k == len)
            return i;
    }
    return -1;
}

static int lmj_declare(int t)
{
    if (lmj_builtin(t) >= 0) {
        lmj_m0("");
        lmj_mtok(t);
        lm_cat(lmw_msg, ": shadows a builtin (WEAVE-SPEC 4.6.5)");
        lmj_err(t, lmw_msg);
        return 0;
    }
    if (lmj_gfind(t) >= 0 || lmj_ffind(t) >= 0) {
        lmj_m0("");
        lmj_mtok(t);
        lm_cat(lmw_msg, ": declared twice");
        lmj_err(t, lmw_msg);
        return 0;
    }
    return 1;
}

/* js_initexpr: number | string | true | false | null | array(n). Answers the
 * next token index, or -1 on a pack error; the initializer itself comes back
 * in lmj_ikind / lmj_ival, because SS != DS makes `&local` a stack offset
 * dereferenced through the package segment (SPEC.md 73.5). */
static int lmj_ikind;
static int lmj_ival;

static int lmj_initexpr(int i)
{
    int t = i;

    if (lmj_kind(t) == LMTK_NUM) {
        lmj_ikind = LMGK_INT;
        lmj_ival = lmj_val(t);
        return i + 1;
    }
    if (lmj_kind(t) == LMTK_STR) {
        lmj_ikind = LMGK_STR;
        lmj_ival = lmj_val(t);
        return i + 1;
    }
    if (lmj_kind(t) == LMTK_PUNCT && lmj_aux(t) == LMP_MINUS
        && lmj_kind(t + 1) == LMTK_NUM) {
        lmj_ikind = LMGK_INT;
        lmj_ival = -lmj_val(t + 1);
        return i + 2;
    }
    if (lmj_kind(t) == LMTK_KW && lmj_aux(t) == LMK_TRUE) {
        lmj_ikind = LMGK_BOOL;
        lmj_ival = 1;
        return i + 1;
    }
    if (lmj_kind(t) == LMTK_KW && lmj_aux(t) == LMK_FALSE) {
        lmj_ikind = LMGK_BOOL;
        lmj_ival = 0;
        return i + 1;
    }
    if (lmj_kind(t) == LMTK_KW && lmj_aux(t) == LMK_NULL) {
        lmj_ikind = LMGK_NULL;
        lmj_ival = 0;
        return i + 1;
    }
    if (lmj_toklit(t, "array")) {
        int n;
        if (!(lmj_kind(t + 1) == LMTK_PUNCT && lmj_aux(t + 1) == LMP_LPAR)
            || lmj_kind(t + 2) != LMTK_NUM
            || !(lmj_kind(t + 3) == LMTK_PUNCT
                 && lmj_aux(t + 3) == LMP_RPAR)) {
            lmj_err(t, "array(<size>) expected");
            return -1;
        }
        n = lmj_val(t + 2);
        if (n < 1 || n > 2048) {
            lmj_m0("array(");
            lm_catn(lmw_msg, n);
            lm_cat(lmw_msg, "): size is 1..2048 (WEAVE-SPEC 4.2)");
            lmj_err(t, lmw_msg);
            return -1;
        }
        lmj_ikind = LMGK_ARRAY;
        lmj_ival = n;
        return i + 4;
    }
    lmj_err(t, "an initializer is a constant: number, string, true/false, "
            "null, array(n) (WEAVE-SPEC 4.2)");
    return -1;
}

static int lmj_ispunct(int t, int p)
{
    return lmj_kind(t) == LMTK_PUNCT && lmj_aux(t) == p;
}

static int lmj_iskw(int t, int k)
{
    return lmj_kind(t) == LMTK_KW && lmj_aux(t) == k;
}

static int ovl_collect(void)
{
    int i = 0;

    lm_nglob = 0;
    lm_nfunc = 0;
    while (lmj_kind(i) != LMTK_EOF) {
        if (lmj_iskw(i, LMK_VAR)) {
            int nt = i + 1;
            unsigned r;

            if (lmj_kind(nt) != LMTK_ID) {
                lmj_err(nt, "var: a name is expected");
                return 0;
            }
            if (!lmj_declare(nt))
                return 0;
            i = nt + 1;
            lmj_ikind = LMGK_ZERO;
            lmj_ival = 0;
            if (lmj_ispunct(i, LMP_ASSIGN)) {
                i = lmj_initexpr(i + 1);
                if (i < 0)
                    return 0;
            }
            if (!lmj_ispunct(i, LMP_SEMI)) {
                lmj_err(i, "var: ';' expected");
                return 0;
            }
            i++;
            if (lm_nglob >= LM_MAXGLOB) {
                lmj_err(nt, "129 globals; the table is 128 "
                        "(WEAVE-SPEC 4.2)");
                return 0;
            }
            r = lmj_grow(lm_nglob);
            lm_wpw(r + LMG_NAMEOFF, lmj_toff(nt));
            lm_wpb(r + LMG_NAMELEN, (unsigned) lmj_val(nt));
            lm_wpb(r + LMG_KIND, (unsigned) lmj_ikind);
            lm_wpw(r + LMG_VAL, (unsigned) lmj_ival & 0xFFFF);
            lm_nglob++;
            continue;
        }
        if (lmj_iskw(i, LMK_FUNCTION)) {
            int nt = i + 1;
            int nargs = 0;
            int depth = 0;
            int start;
            unsigned r;

            if (lmj_kind(nt) != LMTK_ID) {
                lmj_err(nt, "function: a name is expected");
                return 0;
            }
            if (!lmj_declare(nt))
                return 0;
            i = nt + 1;
            if (!lmj_ispunct(i, LMP_LPAR)) {
                lmj_m0("function ");
                lmj_mtok(nt);
                lm_cat(lmw_msg, ": '(' expected");
                lmj_err(i, lmw_msg);
                return 0;
            }
            i++;
            while (!lmj_ispunct(i, LMP_RPAR)) {
                int q;
                if (nargs) {
                    if (!lmj_ispunct(i, LMP_COMMA)) {
                        lmj_err(i, "',' expected");
                        return 0;
                    }
                    i++;
                }
                if (lmj_kind(i) != LMTK_ID) {
                    lmj_err(i, "a parameter name is expected");
                    return 0;
                }
                for (q = 0; q < nargs; q++) {
                    if (lmj_tokeq(i, (int) lm_ww(LMW_NAMES + 1024
                                                 + (unsigned) q * 2))) {
                        lmj_m0("");
                        lmj_mtok(i);
                        lm_cat(lmw_msg, ": parameter written twice");
                        lmj_err(i, lmw_msg);
                        return 0;
                    }
                }
                lm_wpw(LMW_NAMES + 1024 + (unsigned) nargs * 2,
                       (unsigned) i);
                nargs++;
                if (nargs > 8) {
                    lmj_m0("");
                    lmj_mtok(nt);
                    lm_cat(lmw_msg, ": ");
                    lm_catn(lmw_msg, nargs);
                    lm_cat(lmw_msg, " parameters; the cap is 8 "
                           "(WEAVE-SPEC 4.2)");
                    lmj_err(nt, lmw_msg);
                    return 0;
                }
                i++;
            }
            i++;
            if (!lmj_ispunct(i, LMP_LBRACE)) {
                lmj_m0("function ");
                lmj_mtok(nt);
                lm_cat(lmw_msg, ": '{' expected");
                lmj_err(i, lmw_msg);
                return 0;
            }
            start = i;
            for (;;) {
                if (lmj_kind(i) == LMTK_EOF) {
                    lmj_m0("function ");
                    lmj_mtok(nt);
                    lm_cat(lmw_msg, " is never closed");
                    lmj_err(start, lmw_msg);
                    return 0;
                }
                if (lmj_ispunct(i, LMP_LBRACE))
                    depth++;
                else if (lmj_ispunct(i, LMP_RBRACE)) {
                    depth--;
                    if (depth == 0)
                        break;
                }
                i++;
            }
            i++;
            if (lm_nfunc >= LM_MAXFUNC) {
                lmj_err(nt, "129 functions; the table is 128 "
                        "(WEAVE-SPEC 4.2)");
                return 0;
            }
            r = lmj_frow(lm_nfunc);
            lm_wfill(r, 0, LM_FUNCSZ);
            lm_wpw(r + LMF_NAMEOFF, lmj_toff(nt));
            lm_wpb(r + LMF_NAMELEN, (unsigned) lmj_val(nt));
            lm_wpb(r + LMF_NARGS, (unsigned) nargs);
            lm_wpb(r + LMF_NLOCALS, (unsigned) nargs);
            lm_wpw(r + LMF_BODY0, (unsigned) start);
            lm_wpw(r + LMF_BODY1, (unsigned) (i - 1));
            lm_nfunc++;
            continue;
        }
        lmj_err(i, "top level is declarations only - handlers are the only "
                "entry points (WEAVE-SPEC 4.1)");
        return 0;
    }
    return 1;
}

/* ==========================================================================
 * THE CODE GENERATOR (WEAVE-SPEC 4.6)
 * ========================================================================*/

static int      lmc_fn;             /* the function being compiled */
static int      lmc_i;              /* the token cursor */
static int      lmc_end;            /* one past its last token */
static unsigned lmc_base;           /* where its code starts in LMW_CODE */
static unsigned lmc_cur;            /* ...and the write cursor */
static int      lmc_nops;
static int      lmc_flags;
static int      lmc_fail;

/* 4.2: 16 locals, parameters included. Slots are token indices of the
 * declaring name, so a comparison is a span compare and nothing is copied. */
static int lmc_locn;
static int lmc_loctok[16];
static int lmc_locdecl[16];

/* The loop stack: 4.6.3's Lend patches and its Lstep. A `while` continues at
 * a known offset (`at`); a `for` continues at a label that does not exist
 * yet, so its continues are patches. */
#define LMC_MAXLOOP 8
#define LMC_MAXPAT  16
static int lmc_nloop;
static int lmc_brkn[LMC_MAXLOOP];
static unsigned lmc_brk[LMC_MAXLOOP][LMC_MAXPAT];
static int lmc_contn[LMC_MAXLOOP];
static unsigned lmc_cont[LMC_MAXLOOP][LMC_MAXPAT];
static int lmc_contat[LMC_MAXLOOP];         /* -1 = patches, else an offset */

static void lmc_emit(unsigned v)
{
    if (lmc_cur >= LM_CODEMAX) {
        if (!lm_failed())
            lm_perr(LM_SLOT_WJS, lmj_tline(lmc_i),
                    "the compiled script is over 6144 bytes; this machine "
                    "packs that much (WEAVE-SPEC 11.4)");
        lmc_fail = 1;
        return;
    }
    lm_wpb(LMW_CODE + lmc_cur, v);
    lmc_cur++;
}

static void lmc_op0(int op)
{
    lmc_emit((unsigned) op);
    lmc_nops++;
}

static void lmc_opb(int op, int b)
{
    lmc_emit((unsigned) op);
    lmc_emit((unsigned) b & 0xFF);
    lmc_nops++;
}

static void lmc_opw(int op, int w)
{
    lmc_emit((unsigned) op);
    lmc_emit((unsigned) w & 0xFF);
    lmc_emit(((unsigned) w >> 8) & 0xFF);
    lmc_nops++;
}

static void lmc_opbb(int op, int a, int b)
{
    lmc_emit((unsigned) op);
    lmc_emit((unsigned) a & 0xFF);
    lmc_emit((unsigned) b & 0xFF);
    lmc_nops++;
}

static unsigned lmc_jfwd(int op)
{
    lmc_emit((unsigned) op);
    lmc_emit(0);
    lmc_emit(0);
    lmc_nops++;
    return lmc_cur - 2;
}

static void lmc_patch_at(unsigned pos, unsigned target)
{
    int rel = (int) target - (int) pos - 2;

    if (rel < 0)
        lmc_flags |= LMFF_BACKJMP;
    lm_wpb(LMW_CODE + pos, (unsigned) rel & 0xFF);
    lm_wpb(LMW_CODE + pos + 1, ((unsigned) rel >> 8) & 0xFF);
}

static void lmc_patch(unsigned pos)
{
    lmc_patch_at(pos, lmc_cur);
}

static void lmc_jback(int op, unsigned target)
{
    unsigned pos;

    lmc_emit((unsigned) op);
    pos = lmc_cur;
    lmc_emit(0);
    lmc_emit(0);
    lmc_nops++;
    lmc_patch_at(pos, target);
}

/* --- token plumbing ------------------------------------------------------ */

static int lmc_pv(int k)
{
    int t = lmc_i + k;

    if (lmj_kind(t) == LMTK_PUNCT)
        return lmj_aux(t);
    return -1;
}

/* 4.6.5: a KEYWORD in operator position is `else`, `true`, `false`, `null`.
 * pv() answers punctuation only; this is its keyword twin, and the pair is
 * weavesim's `pv()` split in two because C has no "either kind" value. */
static int lmc_kw(int k)
{
    int t = lmc_i + k;

    if (lmj_kind(t) == LMTK_KW)
        return lmj_aux(t);
    return -1;
}

static int lmc_take(void)
{
    return lmc_i++;
}

static int lmc_expect(int p, const char *what)
{
    if (!lmj_ispunct(lmc_i, p)) {
        lmj_m0("'");
        lm_cat(lmw_msg, what);
        lm_cat(lmw_msg, "' expected, found '");
        {
            unsigned m = os88_strlen(lmw_msg);
            unsigned q;
            unsigned len = (unsigned) lmj_val(lmc_i);
            unsigned off = lmj_toff(lmc_i);
            if (lmj_kind(lmc_i) == LMTK_PUNCT)
                len = os88_strlen(lmj_punct[lmj_aux(lmc_i)]);
            for (q = 0; q < len && m + 1 < LM_ERRMAX; q++) {
                lmw_msg[m] = (char) lm_sb(LM_SLOT_WJS, off + q);
                m++;
            }
            lmw_msg[m] = 0;
        }
        lm_cat(lmw_msg, "'");
        lmj_err(lmc_i, lmw_msg);
        lmc_fail = 1;
        return 0;
    }
    lmc_i++;
    return 1;
}

/* --- 4.2's pinned resolution order: local, global, component id, function -*/
#define LMR_LOCAL   0
#define LMR_GLOBAL  1
#define LMR_COMP    2
#define LMR_CARD    3
#define LMR_FN      4
#define LMR_BUILTIN 5
#define LMR_NONE    6

/* The resolved VALUE comes back in lmc_rv, not through a pointer
 * (SPEC.md 73.5). */
static int lmc_rv;

static int lmc_resolve(int t)
{
    int i;

    for (i = 0; i < lmc_locn; i++) {
        if (lmj_tokeq(t, lmc_loctok[i])) {
            if (!lmc_locdecl[i]) {
                lmj_m0("");
                lmj_mtok(t);
                lm_cat(lmw_msg, ": used before its declaration");
                lmj_err(t, lmw_msg);
                lmc_fail = 1;
                return LMR_NONE;
            }
            lmc_rv = i;
            return LMR_LOCAL;
        }
    }
    i = lmj_gfind(t);
    if (i >= 0) {
        lmc_rv = i;
        return LMR_GLOBAL;
    }
    if (lmj_toklit(t, "app")) {
        lmc_rv = 0;
        return LMR_COMP;
    }
    for (i = 0; i < lm_ncomp; i++) {
        unsigned r = LMW_COMPS + (unsigned) i * LM_COMPSZ;
        unsigned off = lm_ww(r + LMC_IDOFF);
        unsigned len = (unsigned) lm_wb(r + LMC_IDLEN);
        if (off == 0 || len == 0)
            continue;
        if (lmj_tokwml(t, off, len)) {
            lmc_rv = (int) lm_wb(r + LMC_ID);
            return LMR_COMP;
        }
    }
    for (i = 0; i < lm_ncard; i++) {
        if (lmj_tokwml(t, lmw_cardoff[i], (unsigned) lmw_cardlen[i])) {
            lmc_rv = i + 1;
            return LMR_CARD;
        }
    }
    i = lmj_ffind(t);
    if (i >= 0) {
        lmc_rv = i;
        return LMR_FN;
    }
    i = lmj_builtin(t);
    if (i >= 0) {
        lmc_rv = i;
        return LMR_BUILTIN;
    }
    lmj_m0("");
    lmj_mtok(t);
    lm_cat(lmw_msg, ": not a local, global, component id or function "
           "(WEAVE-SPEC 4.2)");
    lmj_err(t, lmw_msg);
    lmc_fail = 1;
    return LMR_NONE;
}

static int lmc_push_value(int t)
{
    int k = lmc_resolve(t);
    int v = lmc_rv;

    switch (k) {
    case LMR_LOCAL:
        lmc_opb(OP_LDL, v);
        break;
    case LMR_GLOBAL:
        lmc_opb(OP_LDG, v);
        break;
    case LMR_COMP:
        lmc_opb(OP_PUSHC, v);
        break;
    case LMR_CARD:
        lmc_opw(OP_PUSHI, v);
        break;
    case LMR_FN:
        lmj_m0("");
        lmj_mtok(t);
        lm_cat(lmw_msg, ": a function is not a value; it is named only as a "
               "callback argument (WEAVE-SPEC 4.6.6)");
        lmj_err(t, lmw_msg);
        lmc_fail = 1;
        break;
    case LMR_BUILTIN:
        lmj_m0("");
        lmj_mtok(t);
        lm_cat(lmw_msg, ": a builtin is called, not read");
        lmj_err(t, lmw_msg);
        lmc_fail = 1;
        break;
    default:
        break;
    }
    return k;
}

/* The ctype a comp_id names, for prop_atom's static check (4.4). 0 is the
 * app, which SURFACE gives `go` and nothing else. */
static int lmc_ctype_of(int cid)
{
    int i;

    if (cid == 0)
        return 0;
    for (i = 0; i < lm_ncomp; i++) {
        unsigned r = LMW_COMPS + (unsigned) i * LM_COMPSZ;
        if ((int) lm_wb(r + LMC_ID) == cid)
            return (int) lm_wb(r + LMC_CTYPE);
    }
    return 0;
}

static int lmc_inlist(const unsigned char *list, int atom)
{
    int i;

    for (i = 0; list[i]; i++)
        if ((int) list[i] == atom)
            return 1;
    return 0;
}

/* 4.4's static check: the atom for `c.<name>`, verified against 6's surface
 * where the base ctype is known. */
static int lmc_prop_atom(int ptok, int basekind, int basev, int writing,
                         int method)
{
    int atom = 0;
    int i;

    if (method) {
        for (i = 0; i < LMJ_NMETH; i++)
            if (lmj_toklit(ptok, lmj_mname[i]))
                atom = (int) lmj_matom[i];
    } else {
        for (i = 0; i < 24; i++)
            if (lmj_toklit(ptok, lmj_pname[i]))
                atom = i + 1;
    }
    if (atom == 0) {
        lmj_m0("no such ");
        lm_cat(lmw_msg, method ? "method \"" : "property \"");
        lmj_mtok(ptok);
        lm_cat(lmw_msg, "\" (WEAVE-SPEC 2.7.1)");
        lmj_err(ptok, lmw_msg);
        lmc_fail = 1;
        return 0;
    }
    if (basekind == LMR_COMP) {
        int ct = lmc_ctype_of(basev);
        const unsigned char *surf = method ? lmj_meth[ct]
                                  : (writing ? lmj_set[ct] : lmj_get[ct]);
        if (!lmc_inlist(surf, atom)) {
            lmj_m0("");
            lmj_mtok(ptok);
            lm_cat(lmw_msg, ": a ");
            lm_cat(lmw_msg, lmj_ctname[ct]);
            lm_cat(lmw_msg, " has no ");
            lm_cat(lmw_msg, method ? "method \""
                          : (writing ? "writable property \"" : "property \""));
            lmj_mtok(ptok);
            lm_cat(lmw_msg, "\" (WEAVE-SPEC 6)");
            lmj_err(ptok, lmw_msg);
            lmc_fail = 1;
            return 0;
        }
    }
    return atom;
}

/* --- forward declarations ------------------------------------------------ */
static void lmc_statement(void);
static void lmc_expr(void);
static int  lmc_try_assign(void);

static void lmc_push_init(int kind, int val)
{
    switch (kind) {
    case LMGK_INT:
        lmc_opw(OP_PUSHI, val);
        break;
    case LMGK_STR:
        lmc_opb(OP_PUSHA, val);
        break;
    case LMGK_BOOL:
        lmc_opb(OP_PUSHB, val);
        break;
    case LMGK_NULL:
        lmc_op0(OP_PUSHN);
        break;
    default:                        /* array(n) */
        lmc_opw(OP_PUSHI, val);
        lmc_opbb(OP_BUILT, 11, 1);
        lm_used |= 1 << 11;
        lmc_flags |= LMFF_CALL;
        break;
    }
}

static void lmc_incdec(int t, int isinc)
{
    int k = lmc_resolve(t);
    int v = lmc_rv;

    if (k == LMR_GLOBAL) {
        lmc_opb(isinc ? OP_INCG : OP_DECG, v);
    } else if (k == LMR_LOCAL) {
        lmc_opb(OP_LDL, v);
        lmc_opw(OP_PUSHI, 1);
        lmc_op0(isinc ? OP_ADD : OP_SUB);
        lmc_opb(OP_STL, v);
    } else if (k != LMR_NONE) {
        lmj_m0("");
        lmj_mtok(t);
        lm_cat(lmw_msg, ": ++/-- takes a variable");
        lmj_err(t, lmw_msg);
        lmc_fail = 1;
    }
}

static void lmc_local_var(int semi)
{
    int nt;
    int slot = -1;
    int i;

    lmc_i++;                        /* `var` */
    nt = lmc_take();
    for (i = 0; i < lmc_locn; i++)
        if (lmj_tokeq(nt, lmc_loctok[i]))
            slot = i;
    if (slot < 0) {
        lmj_err(nt, "var: a name is expected");
        lmc_fail = 1;
        return;
    }
    lmc_locdecl[slot] = 1;
    if (lmj_ispunct(lmc_i, LMP_ASSIGN)) {
        int nx;
        lmc_i++;
        nx = lmj_initexpr(lmc_i);
        if (nx < 0) {
            lmc_fail = 1;
            return;
        }
        lmc_i = nx;
        lmc_push_init(lmj_ikind, lmj_ival);
        lmc_opb(OP_STL, slot);
    }
    if (semi)
        lmc_expect(LMP_SEMI, ";");
}

/* --- 4.6.5's call --- */
static void lmc_call(int t)
{
    int b = lmj_builtin(t);
    int argc = 0;

    if (!lmc_expect(LMP_LPAR, "("))
        return;
    if (b >= 0) {
        if (b == 11) {              /* array() (8.1) */
            lmj_err(t, "array: legal only as a var initializer "
                    "(WEAVE-SPEC 8.1)");
            lmc_fail = 1;
            return;
        }
        while (!lmj_ispunct(lmc_i, LMP_RPAR)) {
            if (lmc_fail)
                return;
            if (argc && !lmc_expect(LMP_COMMA, ","))
                return;
            if ((b == 0 || b == 1) && argc == 1) {
                int cb = lmc_take();
                int fi = (lmj_kind(cb) == LMTK_ID) ? lmj_ffind(cb) : -1;
                if (fi < 0) {
                    lmj_m0(lmj_bname[b]);
                    lm_cat(lmw_msg, ": the callback names a top-level "
                           "function (WEAVE-SPEC 4.6.6)");
                    lmj_err(cb, lmw_msg);
                    lmc_fail = 1;
                    return;
                }
                lmc_opw(OP_PUSHI, fi);
            } else {
                lmc_expr();
            }
            argc++;
            if (argc > 8)
                break;
        }
        if (!lmc_expect(LMP_RPAR, ")"))
            return;
        if (argc < (int) lmj_blo[b] || argc > (int) lmj_bhi[b]) {
            lmj_m0(lmj_bname[b]);
            lm_cat(lmw_msg, ": takes ");
            if (lmj_blo[b] == lmj_bhi[b]) {
                lm_catn(lmw_msg, (int) lmj_blo[b]);
            } else {
                lm_catn(lmw_msg, (int) lmj_blo[b]);
                lm_cat(lmw_msg, "..");
                lm_catn(lmw_msg, (int) lmj_bhi[b]);
            }
            lm_cat(lmw_msg, " arguments; ");
            lm_catn(lmw_msg, argc);
            lm_cat(lmw_msg, " written");
            lmj_err(t, lmw_msg);
            lmc_fail = 1;
            return;
        }
        lmc_opbb(OP_BUILT, b, argc);
        lm_used |= 1 << b;
        lmc_flags |= LMFF_CALL;
        return;
    }
    {
        int k = lmc_resolve(t);
        int v = lmc_rv;
        int want;
        if (k != LMR_FN) {
            if (k != LMR_NONE) {
                lmj_m0("");
                lmj_mtok(t);
                lm_cat(lmw_msg, ": not a function");
                lmj_err(t, lmw_msg);
                lmc_fail = 1;
            }
            return;
        }
        while (!lmj_ispunct(lmc_i, LMP_RPAR)) {
            if (lmc_fail)
                return;
            if (argc && !lmc_expect(LMP_COMMA, ","))
                return;
            lmc_expr();
            argc++;
            if (argc > 32)
                break;
        }
        if (!lmc_expect(LMP_RPAR, ")"))
            return;
        want = (int) lm_wb(lmj_frow(v) + LMF_NARGS);
        if (argc != want) {
            lmj_m0("");
            lmj_mtok(t);
            lm_cat(lmw_msg, ": takes ");
            lm_catn(lmw_msg, want);
            lm_cat(lmw_msg, " arguments; ");
            lm_catn(lmw_msg, argc);
            lm_cat(lmw_msg, " written");
            lmj_err(t, lmw_msg);
            lmc_fail = 1;
            return;
        }
        lmc_opb(OP_CALL, v);
        lmc_flags |= LMFF_CALL;
    }
}

/* --- 4.6.5's suffixes: indexing, properties, methods --- */
static void lmc_suffixes(int basekind, int basev)
{
    for (;;) {
        if (lmc_fail)
            return;
        if (lmj_ispunct(lmc_i, LMP_LBRK)) {
            lmc_i++;
            lmc_expr();
            if (!lmc_expect(LMP_RBRK, "]"))
                return;
            lmc_op0(OP_AGET);
            basekind = LMR_NONE;
        } else if (lmj_ispunct(lmc_i, LMP_DOT)) {
            int ptok;
            lmc_i++;
            ptok = lmc_take();
            if (lmj_kind(ptok) != LMTK_ID) {
                lmj_err(ptok, "a property name is expected after '.'");
                lmc_fail = 1;
                return;
            }
            if (lmj_ispunct(lmc_i, LMP_LPAR)) {
                int atom = lmc_prop_atom(ptok, basekind, basev, 0, 1);
                int argc = 0;
                int want = 0;
                int q;
                if (lmc_fail)
                    return;
                lmc_i++;
                while (!lmj_ispunct(lmc_i, LMP_RPAR)) {
                    if (lmc_fail)
                        return;
                    if (argc && !lmc_expect(LMP_COMMA, ","))
                        return;
                    lmc_expr();
                    argc++;
                    if (argc > 32)
                        break;
                }
                if (!lmc_expect(LMP_RPAR, ")"))
                    return;
                for (q = 0; q < LMJ_NMETH; q++)
                    if ((int) lmj_matom[q] == atom)
                        want = (int) lmj_marity[q];
                if (argc != want) {
                    lmj_m0("");
                    lmj_mtok(ptok);
                    lm_cat(lmw_msg, ": takes ");
                    lm_catn(lmw_msg, want);
                    lm_cat(lmw_msg, " arguments; ");
                    lm_catn(lmw_msg, argc);
                    lm_cat(lmw_msg, " written");
                    lmj_err(ptok, lmw_msg);
                    lmc_fail = 1;
                    return;
                }
                lmc_opbb(OP_CALLM, atom, argc);
            } else {
                int atom = lmc_prop_atom(ptok, basekind, basev, 0, 0);
                if (lmc_fail)
                    return;
                lmc_opb(OP_GETP, atom);
            }
            basekind = LMR_NONE;
        } else if (lmj_ispunct(lmc_i, LMP_LPAR)) {
            lmj_err(lmc_i, "only a function or builtin name is called "
                    "(WEAVE-SPEC 4.6.5)");
            lmc_fail = 1;
            return;
        } else {
            return;
        }
    }
}

static void lmc_primary(void)
{
    int t = lmc_take();

    if (lmj_kind(t) == LMTK_NUM) {
        lmc_opw(OP_PUSHI, lmj_val(t));
    } else if (lmj_kind(t) == LMTK_STR) {
        lmc_opb(OP_PUSHA, lmj_val(t));
    } else if (lmj_kind(t) == LMTK_KW && lmj_aux(t) == LMK_TRUE) {
        lmc_opb(OP_PUSHB, 1);
    } else if (lmj_kind(t) == LMTK_KW && lmj_aux(t) == LMK_FALSE) {
        lmc_opb(OP_PUSHB, 0);
    } else if (lmj_kind(t) == LMTK_KW && lmj_aux(t) == LMK_NULL) {
        lmc_op0(OP_PUSHN);
    } else if (lmj_kind(t) == LMTK_PUNCT && lmj_aux(t) == LMP_LPAR) {
        lmc_expr();
        lmc_expect(LMP_RPAR, ")");
    } else if (lmj_kind(t) == LMTK_ID) {
        lmc_push_value(t);
    } else {
        lmj_m0("cannot read '");
        lmj_mtok(t);
        lm_cat(lmw_msg, "' here");
        lmj_err(t, lmw_msg);
        lmc_fail = 1;
    }
}

static void lmc_postfix(void)
{
    int t = lmc_i;

    if (lmj_kind(t) == LMTK_ID && lmc_pv(1) == LMP_LPAR) {
        lmc_i++;
        lmc_call(t);
    } else if (lmj_kind(t) == LMTK_ID && lmc_pv(1) == LMP_DOT) {
        int v;
        int k;
        lmc_i++;
        k = lmc_resolve(t);
        v = lmc_rv;
        if (lmc_fail)
            return;
        lmc_push_value(t);
        lmc_suffixes(k, v);
        return;
    } else {
        lmc_primary();
    }
    lmc_suffixes(LMR_NONE, 0);
}

static void lmc_unary(void)
{
    if (lmc_pv(0) == LMP_MINUS) {
        lmc_i++;
        lmc_unary();
        lmc_op0(OP_NEG);
    } else if (lmc_pv(0) == LMP_BANG) {
        lmc_i++;
        lmc_unary();
        lmc_op0(OP_NOT);            /* 4.6.4: !a -> [a] NOT */
    } else {
        lmc_postfix();
    }
}

static void lmc_mulexpr(void)
{
    lmc_unary();
    for (;;) {
        int p = lmc_pv(0);
        if (lmc_fail)
            return;
        if (p == LMP_STAR) {
            lmc_i++;
            lmc_unary();
            lmc_op0(OP_MUL);
        } else if (p == LMP_SLASH) {
            lmc_i++;
            lmc_unary();
            lmc_op0(OP_DIV);
        } else if (p == LMP_PCT) {
            lmc_i++;
            lmc_unary();
            lmc_op0(OP_MOD);
        } else {
            return;
        }
    }
}

static void lmc_addexpr(void)
{
    lmc_mulexpr();
    for (;;) {
        int p = lmc_pv(0);
        if (lmc_fail)
            return;
        if (p == LMP_PLUS) {
            lmc_i++;
            lmc_mulexpr();
            lmc_op0(OP_ADD);
        } else if (p == LMP_MINUS) {
            lmc_i++;
            lmc_mulexpr();
            lmc_op0(OP_SUB);
        } else {
            return;
        }
    }
}

static void lmc_relexpr(void)
{
    lmc_addexpr();
    for (;;) {
        int p = lmc_pv(0);
        if (lmc_fail)
            return;
        if (p == LMP_LT) {
            lmc_i++;
            lmc_addexpr();
            lmc_op0(OP_LT);
        } else if (p == LMP_LE) {
            lmc_i++;
            lmc_addexpr();
            lmc_op0(OP_LE);
        } else if (p == LMP_GT) {
            lmc_i++;
            lmc_addexpr();
            lmc_op0(OP_GT);
        } else if (p == LMP_GE) {
            lmc_i++;
            lmc_addexpr();
            lmc_op0(OP_GE);
        } else {
            return;
        }
    }
}

static void lmc_eqexpr(void)
{
    lmc_relexpr();
    for (;;) {
        int p = lmc_pv(0);
        if (lmc_fail)
            return;
        if (p == LMP_EQ) {
            lmc_i++;
            lmc_relexpr();
            lmc_op0(OP_EQ);
        } else if (p == LMP_NE) {
            lmc_i++;
            lmc_relexpr();
            lmc_op0(OP_NE);
        } else {
            return;
        }
    }
}

/* 4.6.4: a && b -> [a] DUP JZ Lend POP [b] Lend: - the deciding VALUE, not a
 * bool, which is what 4.4's short-circuit semantics say. */
static void lmc_andexpr(void)
{
    lmc_eqexpr();
    while (lmc_pv(0) == LMP_AND) {
        unsigned j;
        if (lmc_fail)
            return;
        lmc_i++;
        lmc_op0(OP_DUP);
        j = lmc_jfwd(OP_JZ);
        lmc_op0(OP_POP);
        lmc_eqexpr();
        lmc_patch(j);
    }
}

static void lmc_orexpr(void)
{
    lmc_andexpr();
    while (lmc_pv(0) == LMP_OR) {
        unsigned j;
        if (lmc_fail)
            return;
        lmc_i++;
        lmc_op0(OP_DUP);
        j = lmc_jfwd(OP_JNZ);
        lmc_op0(OP_POP);
        lmc_andexpr();
        lmc_patch(j);
    }
}

static void lmc_expr(void)
{
    lmc_orexpr();
}

/* --- 4.6.1's assignment forms ------------------------------------------- */
static int lmc_try_assign(void)
{
    int t = lmc_i;

    if (lmj_kind(t) != LMTK_ID)
        return 0;
    if (lmc_pv(1) == LMP_ASSIGN && lmc_pv(2) != LMP_ASSIGN) {
        int v;
        int k;
        lmc_i += 2;
        k = lmc_resolve(t);
        v = lmc_rv;
        if (k == LMR_FN) {
            lmj_m0("");
            lmj_mtok(t);
            lm_cat(lmw_msg, ": assignment to a function name");
            lmj_err(t, lmw_msg);
            lmc_fail = 1;
            return 1;
        }
        if (k != LMR_LOCAL && k != LMR_GLOBAL) {
            if (k != LMR_NONE) {
                lmj_m0("");
                lmj_mtok(t);
                lm_cat(lmw_msg, ": not assignable");
                lmj_err(t, lmw_msg);
                lmc_fail = 1;
            }
            return 1;
        }
        lmc_expr();
        lmc_opb(k == LMR_GLOBAL ? OP_STG : OP_STL, v);
        return 1;
    }
    if (lmc_pv(1) == LMP_DOT && lmj_kind(lmc_i + 2) == LMTK_ID
        && lmc_pv(3) == LMP_ASSIGN && lmc_pv(4) != LMP_ASSIGN) {
        int v;
        int k;
        int ptok;
        int atom;
        lmc_i++;
        k = lmc_resolve(t);
        v = lmc_rv;
        if (lmc_fail)
            return 1;
        lmc_i++;                    /* '.' */
        ptok = lmc_take();
        lmc_i++;                    /* '=' */
        atom = lmc_prop_atom(ptok, k, v, 1, 0);
        if (lmc_fail)
            return 1;
        /* 4.6.1: c.p = e -> [c] [e] SETP atom */
        lmc_push_value(t);
        lmc_expr();
        lmc_opb(OP_SETP, atom);
        return 1;
    }
    if (lmc_pv(1) == LMP_LBRK) {
        int j = lmc_i + 2;
        int depth = 1;
        for (;;) {
            if (lmj_kind(j) == LMTK_EOF) {
                lmj_err(lmc_i + 1, "']' expected");
                lmc_fail = 1;
                return 1;
            }
            if (lmj_ispunct(j, LMP_LBRK))
                depth++;
            else if (lmj_ispunct(j, LMP_RBRK)) {
                depth--;
                if (depth == 0)
                    break;
            }
            j++;
        }
        if (lmj_ispunct(j + 1, LMP_ASSIGN)
            && !lmj_ispunct(j + 2, LMP_ASSIGN)) {
            lmc_i++;
            lmc_push_value(t);   /* 4.6.1: a[i] = e -> [a] [i] [e] ASET */
            if (!lmc_expect(LMP_LBRK, "["))
                return 1;
            lmc_expr();
            if (!lmc_expect(LMP_RBRK, "]"))
                return 1;
            if (!lmc_expect(LMP_ASSIGN, "="))
                return 1;
            lmc_expr();
            lmc_op0(OP_ASET);
            return 1;
        }
    }
    return 0;
}

/* --- 4.6.2 and 4.6.3 ----------------------------------------------------- */
static void lmc_if(void)
{
    unsigned jz;

    lmc_i++;                        /* `if` */
    if (!lmc_expect(LMP_LPAR, "("))
        return;
    lmc_expr();
    if (!lmc_expect(LMP_RPAR, ")"))
        return;
    jz = lmc_jfwd(OP_JZ);
    lmc_statement();
    if (lmc_kw(0) == LMK_ELSE) {
        unsigned jend;
        lmc_i++;
        jend = lmc_jfwd(OP_JMP);
        lmc_patch(jz);
        lmc_statement();
        lmc_patch(jend);
    } else {
        lmc_patch(jz);
    }
}

static void lmc_while(void)
{
    unsigned top;
    unsigned jz;
    int k;

    lmc_i++;
    top = lmc_cur;
    if (!lmc_expect(LMP_LPAR, "("))
        return;
    lmc_expr();
    if (!lmc_expect(LMP_RPAR, ")"))
        return;
    jz = lmc_jfwd(OP_JZ);
    if (lmc_nloop >= LMC_MAXLOOP) {
        lmj_err(lmc_i, "loops nest 8 deep in one function (WEAVE-SPEC 11.4)");
        lmc_fail = 1;
        return;
    }
    lmc_brkn[lmc_nloop] = 0;
    lmc_contn[lmc_nloop] = 0;
    lmc_contat[lmc_nloop] = (int) top;
    lmc_nloop++;
    lmc_statement();
    lmc_nloop--;
    lmc_jback(OP_JMP, top);
    lmc_patch(jz);
    for (k = 0; k < lmc_brkn[lmc_nloop]; k++)
        lmc_patch(lmc_brk[lmc_nloop][k]);
}

static void lmc_for(void)
{
    unsigned top;
    unsigned jz;
    int step_start, step_end;
    int save;
    int depth = 0;
    int k;

    lmc_i++;
    if (!lmc_expect(LMP_LPAR, "("))
        return;
    if (!lmj_ispunct(lmc_i, LMP_SEMI)) {
        if (lmc_kw(0) == LMK_VAR) {
            lmc_local_var(0);
        } else if (!lmc_try_assign()) {
            lmj_err(lmc_i, "for: an assignment is expected");
            lmc_fail = 1;
            return;
        }
    }
    if (lmc_fail || !lmc_expect(LMP_SEMI, ";"))
        return;
    top = lmc_cur;
    if (lmj_ispunct(lmc_i, LMP_SEMI))
        lmc_opb(OP_PUSHB, 1);       /* 4.6.3: an omitted condition */
    else
        lmc_expr();
    if (lmc_fail || !lmc_expect(LMP_SEMI, ";"))
        return;
    jz = lmc_jfwd(OP_JZ);
    /* 4.6.3's Lstep: the step is compiled AFTER the body, so its tokens are
     * skipped here and revisited below. */
    step_start = lmc_i;
    while (!(depth == 0 && lmj_ispunct(lmc_i, LMP_RPAR))) {
        if (lmj_kind(lmc_i) == LMTK_EOF) {
            lmj_err(step_start, "for: ')' expected");
            lmc_fail = 1;
            return;
        }
        if (lmj_ispunct(lmc_i, LMP_LPAR))
            depth++;
        else if (lmj_ispunct(lmc_i, LMP_RPAR))
            depth--;
        lmc_i++;
    }
    step_end = lmc_i;
    lmc_i++;                        /* ')' */
    if (lmc_nloop >= LMC_MAXLOOP) {
        lmj_err(lmc_i, "loops nest 8 deep in one function (WEAVE-SPEC 11.4)");
        lmc_fail = 1;
        return;
    }
    lmc_brkn[lmc_nloop] = 0;
    lmc_contn[lmc_nloop] = 0;
    lmc_contat[lmc_nloop] = -1;
    lmc_nloop++;
    lmc_statement();
    lmc_nloop--;
    for (k = 0; k < lmc_contn[lmc_nloop]; k++)
        lmc_patch(lmc_cont[lmc_nloop][k]);
    save = lmc_i;
    lmc_i = step_start;
    if (lmc_i < step_end) {
        if (lmj_kind(lmc_i) == LMTK_ID
            && (lmc_pv(1) == LMP_INC || lmc_pv(1) == LMP_DEC)) {
            int t = lmc_take();
            int inc = lmj_ispunct(lmc_i, LMP_INC);
            lmc_i++;
            lmc_incdec(t, inc);
        } else if (!lmc_try_assign()) {
            lmj_err(step_start, "for: the step is an assignment or ++/--");
            lmc_fail = 1;
            return;
        }
        if (!lmc_fail && lmc_i != step_end) {
            lmj_err(lmc_i, "for: cannot read the step");
            lmc_fail = 1;
            return;
        }
    }
    lmc_i = save;
    lmc_jback(OP_JMP, top);
    lmc_patch(jz);
    for (k = 0; k < lmc_brkn[lmc_nloop]; k++)
        lmc_patch(lmc_brk[lmc_nloop][k]);
}

static void lmc_statement(void)
{
    int t = lmc_i;

    if (lmc_fail)
        return;
    if (lmj_ispunct(t, LMP_LBRACE)) {
        lmc_i++;
        while (!lmj_ispunct(lmc_i, LMP_RBRACE)) {
            if (lmc_fail || lmj_kind(lmc_i) == LMTK_EOF)
                return;
            lmc_statement();
        }
        lmc_i++;
        return;
    }
    if (lmc_kw(0) == LMK_VAR) {
        lmc_local_var(1);
        return;
    }
    if (lmc_kw(0) == LMK_IF) {
        lmc_if();
        return;
    }
    if (lmc_kw(0) == LMK_WHILE) {
        lmc_while();
        return;
    }
    if (lmc_kw(0) == LMK_FOR) {
        lmc_for();
        return;
    }
    if (lmc_kw(0) == LMK_BREAK || lmc_kw(0) == LMK_CONTINUE) {
        int isbrk = (lmc_kw(0) == LMK_BREAK);
        lmc_i++;
        if (!lmc_expect(LMP_SEMI, ";"))
            return;
        if (lmc_nloop == 0) {
            lmj_err(t, isbrk ? "break outside a loop"
                             : "continue outside a loop");
            lmc_fail = 1;
            return;
        }
        if (isbrk) {
            int L = lmc_nloop - 1;
            if (lmc_brkn[L] >= LMC_MAXPAT) {
                lmj_err(t, "16 breaks in one loop (WEAVE-SPEC 11.4)");
                lmc_fail = 1;
                return;
            }
            lmc_brk[L][lmc_brkn[L]] = lmc_jfwd(OP_JMP);
            lmc_brkn[L]++;
        } else {
            int L = lmc_nloop - 1;
            if (lmc_contat[L] >= 0) {
                lmc_jback(OP_JMP, (unsigned) lmc_contat[L]);
            } else {
                if (lmc_contn[L] >= LMC_MAXPAT) {
                    lmj_err(t, "16 continues in one loop (WEAVE-SPEC 11.4)");
                    lmc_fail = 1;
                    return;
                }
                lmc_cont[L][lmc_contn[L]] = lmc_jfwd(OP_JMP);
                lmc_contn[L]++;
            }
        }
        return;
    }
    if (lmc_kw(0) == LMK_RETURN) {
        lmc_i++;
        if (lmj_ispunct(lmc_i, LMP_SEMI))
            lmc_op0(OP_PUSHN);      /* 4.6.1: return ; -> PUSHN RET */
        else
            lmc_expr();
        if (!lmc_expect(LMP_SEMI, ";"))
            return;
        lmc_op0(OP_RET);
        return;
    }
    if (lmj_kind(t) == LMTK_ID
        && (lmc_pv(1) == LMP_INC || lmc_pv(1) == LMP_DEC)) {
        int inc = (lmc_pv(1) == LMP_INC);
        lmc_i += 2;
        lmc_incdec(t, inc);
        lmc_expect(LMP_SEMI, ";");
        return;
    }
    if (lmc_try_assign()) {
        if (!lmc_fail)
            lmc_expect(LMP_SEMI, ";");
        return;
    }
    lmc_expr();
    if (lmc_fail)
        return;
    if (!lmc_expect(LMP_SEMI, ";"))
        return;
    lmc_op0(OP_POP);                /* 4.6.1: expr ; -> [expr] POP */
}

/* --- one function -------------------------------------------------------- */
static int lmc_compile_fn(int fi)
{
    unsigned r = lmj_frow(fi);
    int body0 = (int) lm_ww(r + LMF_BODY0);
    int body1 = (int) lm_ww(r + LMF_BODY1);
    int nargs = (int) lm_wb(r + LMF_NARGS);
    int j;
    int k;

    lmc_fn = fi;
    lmc_base = lmc_cur;
    lmc_nops = 0;
    lmc_flags = 0;
    lmc_fail = 0;
    lmc_nloop = 0;
    lmc_locn = 0;

    /* 4.2: "args are locals 0..nargs-1". Their tokens are the run between the
     * '(' and the ')' just before the body's '{' - `f ( a , b ) {` - so the
     * k'th parameter is at lpar + 1 + 2k. The tokens are found rather than
     * banked because a function row is sixteen bytes and eight parameter
     * spans would be thirty-two more, in a claim, for a walk of four tokens. */
    j = body0 - 1;                  /* the ')' */
    while (j > 0 && !lmj_ispunct(j, LMP_LPAR))
        j--;
    for (k = 0; k < nargs; k++) {
        lmc_loctok[lmc_locn] = j + 1 + 2 * k;
        lmc_locdecl[lmc_locn] = 1;
        lmc_locn++;
    }

    /* 4.2's pre-scan: `var` declares a LOCAL with function scope, so every
     * one in the body gets its slot before a line is compiled - which is what
     * makes "used before its declaration" a distinct error from "not a
     * local". */
    for (j = body0; j < body1; j++) {
        if (!lmj_iskw(j, LMK_VAR))
            continue;
        if (lmj_kind(j + 1) != LMTK_ID) {
            lmj_err(j + 1, "var: a name is expected");
            return 0;
        }
        for (k = 0; k < lmc_locn; k++) {
            if (lmj_tokeq(j + 1, lmc_loctok[k])) {
                lmj_m0("");
                lmj_mtok(j + 1);
                lm_cat(lmw_msg, ": declared twice");
                lmj_err(j + 1, lmw_msg);
                return 0;
            }
        }
        if (lmj_builtin(j + 1) >= 0) {
            lmj_m0("");
            lmj_mtok(j + 1);
            lm_cat(lmw_msg, ": shadows a builtin (WEAVE-SPEC 4.6.5)");
            lmj_err(j + 1, lmw_msg);
            return 0;
        }
        if (lmc_locn >= 16) {
            lmc_locn++;
            continue;               /* counted, reported below */
        }
        lmc_loctok[lmc_locn] = j + 1;
        lmc_locdecl[lmc_locn] = 0;
        lmc_locn++;
        j++;
    }
    if (lmc_locn > 16) {
        lmj_m0("");
        {
            unsigned m = os88_strlen(lmw_msg);
            unsigned q;
            unsigned len = lm_wb(r + LMF_NAMELEN);
            unsigned off = lm_ww(r + LMF_NAMEOFF);
            for (q = 0; q < len && m + 1 < LM_ERRMAX; q++) {
                lmw_msg[m] = (char) lm_sb(LM_SLOT_WJS, off + q);
                m++;
            }
            lmw_msg[m] = 0;
        }
        lm_cat(lmw_msg, ": ");
        lm_catn(lmw_msg, lmc_locn);
        lm_cat(lmw_msg, " locals; the cap is 16, parameters included "
               "(WEAVE-SPEC 4.2)");
        lmj_err(body0, lmw_msg);
        return 0;
    }
    lm_wpb(r + LMF_NLOCALS, (unsigned) lmc_locn);

    lmc_i = body0;
    lmc_end = body1;
    if (!lmc_expect(LMP_LBRACE, "{"))
        return 0;
    while (!lmj_ispunct(lmc_i, LMP_RBRACE)) {
        if (lmc_fail || lmj_kind(lmc_i) == LMTK_EOF)
            return 0;
        lmc_statement();
    }
    if (lmc_fail)
        return 0;
    lmc_i++;
    /* 4.5: a function with no `return` falls off its end - PUSHN + RET,
     * emitted ALWAYS, because no reachability analysis exists. */
    lmc_op0(OP_PUSHN);
    lmc_op0(OP_RET);
    lm_wpw(r + LMF_CODEOFF, lmc_base);
    lm_wpw(r + LMF_CODELEN, lmc_cur - lmc_base);
    lm_wpw(r + LMF_NOPS, (unsigned) lmc_nops);
    lm_wpb(r + LMF_FLAGS, (unsigned) lmc_flags);
    return 1;
}

/* --- the writer's two questions ------------------------------------------ */

static unsigned ovl_fnlen(int i)
{
    return lm_ww(lmj_frow(i) + LMF_CODELEN);
}

/* 4.11.1: <= 64 emitted ops, no backward jump, no CALL - statically
 * checkable and packer-rejected, because per-frame JS does not fit
 * 10-30k ops/s. */
static int ovl_ontick_ok(int fi, int line)
{
    unsigned r = lmj_frow(fi);
    int nops = (int) lm_ww(r + LMF_NOPS);
    int fl = (int) lm_wb(r + LMF_FLAGS);

    if (nops > 64) {
        lmj_m0("ontick handler is ");
        lm_catn(lmw_msg, nops);
        lm_cat(lmw_msg, " ops; the cap is 64 - per-frame JS does not fit "
               "10-30k ops/s");
        lm_perr(LM_SLOT_WML, line, lmw_msg);
        return 0;
    }
    if (fl & LMFF_BACKJMP) {
        lmj_m0("ontick handler ");
        {
            unsigned m = os88_strlen(lmw_msg);
            unsigned q;
            unsigned len = lm_wb(r + LMF_NAMELEN);
            unsigned off = lm_ww(r + LMF_NAMEOFF);
            for (q = 0; q < len && m + 1 < LM_ERRMAX; q++) {
                lmw_msg[m] = (char) lm_sb(LM_SLOT_WJS, off + q);
                m++;
            }
            lmw_msg[m] = 0;
        }
        lm_cat(lmw_msg, " has a backward jump; the cap is 64 straight-line "
               "ops - per-frame JS does not fit 10-30k ops/s");
        lm_perr(LM_SLOT_WML, line, lmw_msg);
        return 0;
    }
    if (fl & LMFF_CALL) {
        lmj_m0("ontick handler ");
        {
            unsigned m = os88_strlen(lmw_msg);
            unsigned q;
            unsigned len = lm_wb(r + LMF_NAMELEN);
            unsigned off = lm_ww(r + LMF_NAMEOFF);
            for (q = 0; q < len && m + 1 < LM_ERRMAX; q++) {
                lmw_msg[m] = (char) lm_sb(LM_SLOT_WJS, off + q);
                m++;
            }
            lmw_msg[m] = 0;
        }
        lm_cat(lmw_msg, " makes a call; the cap is 64 straight-line ops - "
               "per-frame JS does not fit 10-30k ops/s");
        lm_perr(LM_SLOT_WML, line, lmw_msg);
        return 0;
    }
    return 1;
}

/* --- the door ------------------------------------------------------------ */

/* ovl_wjs - 4.2's TOKENS and 4.1's top-level declarations, and nothing else.
 *
 * IT STOPS BEFORE THE BODIES ON PURPOSE. tools/weavesim.py's pack_project()
 * tokenizes and collects, then reads the sheet and the sprite art, and only
 * then calls compile_wjs() - so a project with a broken handler AND a broken
 * `.WSP` refuses about the art, not about the handler. The atom pool does not
 * care (2.14 rule 3b interns in the TOKENIZER, which is here), but 10.5's
 * "which error wins" does, and that is the whole of why this is two doors. */
int ovl_wjs(void)
{
    lm_used = 0;
    lm_startfn = -1;
    lm_nfunc = 0;
    lm_nglob = 0;
    lmj_ntok = 0;
    lmc_cur = 0;
    if (!lm_hasscript)
        return 1;
    if (lm_srclen(LM_SLOT_WJS) == 0) {
        lmw_msg[0] = 0;
        lm_cat(lmw_msg, "script: src=\"");
        lm_cat(lmw_msg, lm_scriptsrc);
        lm_cat(lmw_msg, "\" not found beside the .WML");
        lm_perr(LM_SLOT_WML, lm_scriptline, lmw_msg);
        return 0;
    }
    if (!ovl_tokenize())
        return 0;
    return ovl_collect();
}

/* ...and the second door: 4.6's code generation, plus 2.6.2's synthesized
 * module-init function. */
int ovl_wjs_gen(void)
{
    int i;
    int ninit = 0;

    if (!lm_hasscript)
        return 1;
    for (i = 0; i < lm_nfunc; i++)
        if (!lmc_compile_fn(i))
            return 0;

    /* 2.6.2: synthesized when any global carries an initializer other than
     * int 0, appended as the LAST table entry, its body the initializer
     * stores in declaration order ending PUSHN/RET. */
    for (i = 0; i < lm_nglob; i++) {
        unsigned r = lmj_grow(i);
        int k = (int) lm_wb(r + LMG_KIND);
        int v = (int) lm_ww(r + LMG_VAL);
        if (k == LMGK_ZERO)
            continue;
        if (k == LMGK_INT && v == 0)
            continue;
        ninit++;
    }
    if (ninit) {
        unsigned r;
        if (lm_nfunc >= LM_MAXFUNC) {
            lm_perr(LM_SLOT_WJS, 1, "129 functions; the table is 128 "
                    "(WEAVE-SPEC 4.2)");
            return 0;
        }
        lmc_base = lmc_cur;
        lmc_nops = 0;
        lmc_flags = 0;
        lmc_fail = 0;
        for (i = 0; i < lm_nglob; i++) {
            unsigned g = lmj_grow(i);
            int k = (int) lm_wb(g + LMG_KIND);
            int v = (int) lm_ww(g + LMG_VAL);
            if (k == LMGK_ZERO)
                continue;
            if (k == LMGK_INT && v == 0)
                continue;
            /* No sign fix-up and no 65536: lmc_push_init() masks the
             * immediate to sixteen bits, which is the only width 4.5's
             * PUSHI has. */
            lmc_push_init(k, v);
            lmc_opb(OP_STG, i);
        }
        lmc_op0(OP_PUSHN);
        lmc_op0(OP_RET);
        r = lmj_frow(lm_nfunc);
        lm_wfill(r, 0, LM_FUNCSZ);
        lm_wpw(r + LMF_CODEOFF, lmc_base);
        lm_wpw(r + LMF_CODELEN, lmc_cur - lmc_base);
        lm_startfn = lm_nfunc;
        lm_nfunc++;
    }
    return !lm_failed();
}
