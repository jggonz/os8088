/* ============================================================================
 * os8088 - apps/loom/lmatom.c
 *
 * THE ATOM INTERNER (WEAVE-SPEC 2.7, and 2.14 rule 3 is what makes it part of
 * the byte-identity contract rather than an implementation detail).
 *
 * LOOM.OVL's fourth tenant (WEAVE-SPEC 1.2). It runs once per Pack and never
 * on a keystroke, so every function here carries the `ovl_` prefix and its
 * CODE ships in the module; the pool itself is in the scratch claim, so its
 * DATA is one segment word of the resident image (SPEC.md 73.14: only code
 * moves, and that is exactly why the tables may not be C arrays).
 *
 * ---------------------------------------------------------------------------
 * FIRST APPEARANCE WINS, IN ONE PINNED TRAVERSAL
 * ---------------------------------------------------------------------------
 * WEAVE-SPEC 2.14 rule 3: (a) the WML document in document order - element by
 * element, attributes in 3.3's table order for that element, then text
 * content; (b) the WJS source in token order; (c) FX formulas in CELLS order.
 * Duplicate strings intern once, at their first appearance.
 *
 * NOTHING IN THIS FILE ENFORCES THAT ORDER and nothing can: the order is the
 * order the CALLERS call in, which is why lmwml.c, lmwjs.c and lmsheet.c each
 * carry the rule at the point where they walk. What this file guarantees is
 * the other half - that the same call sequence always produces the same pool.
 *
 * ---------------------------------------------------------------------------
 * THE POOL, IN THE SCRATCH CLAIM
 * ---------------------------------------------------------------------------
 *   LMW_ATOFF +0        the count word: how many app atoms exist (0..187)
 *   LMW_ATOFF +2 + 2i   atom (64+i)'s byte offset into LMW_ATTXT
 *                       ...and there are count+1 of them, so the last one is
 *                       the free mark and a length is the difference between
 *                       two neighbours. That is why the strings are packed
 *                       with no separator: 2.7's own layout puts them "packed
 *                       without gaps", and storing the length as well would
 *                       be a second place for it to be wrong.
 *   LMW_ATTXT           the bytes, already FOLDED (3.1) by the builder
 *
 * A LINEAR SEARCH, AND IT IS THE RIGHT ONE. 2.7 caps the pool at 187, so the
 * worst interning walk is 187 compares of a string whose average length is
 * about eight bytes - under 2,000 byte compares, microseconds even at
 * 4.77 MHz, against a hash table's resident bucket array. The pack step's
 * cost is the disk write and the far calls, never this.
 * ==========================================================================*/

char lm_sbuf[LM_SBUF];
int  lm_sbn;
int  lm_sbover;
int  lm_sbwant;                     /* how many bytes were PUSHED, overflow
                                     * included - 2.7's refusal names the
                                     * length that was written, not the
                                     * length that fitted */

void lm_sbclear(void)
{
    lm_sbn = 0;
    lm_sbover = 0;
    lm_sbwant = 0;
    lm_sbuf[0] = 0;
}

void lm_sbputc(int c)
{
    lm_sbwant++;
    if (lm_sbn >= LM_SBUF - 1) {
        lm_sbover = 1;
        return;
    }
    lm_sbuf[lm_sbn] = (char) c;
    lm_sbn++;
    lm_sbuf[lm_sbn] = 0;
}

void lm_sbfold(int c)
{
    int f = lm_fold(c);

    if (f >= 0)
        lm_sbputc(f);
}

/* WEAVE-SPEC 3.1: "leading and trailing whitespace of a content run is
 * dropped". The collapse of interior runs to a single space is the caller's,
 * because it has to happen as the run is read - a text node is not one
 * contiguous span of the file once entities are expanded. */
void lm_sbtrim(void)
{
    int i = 0;

    while (lm_sbn > 0 && lm_sbuf[lm_sbn - 1] == ' ')
        lm_sbn--;
    lm_sbuf[lm_sbn] = 0;
    while (i < lm_sbn && lm_sbuf[i] == ' ')
        i++;
    if (i > 0) {
        int k = 0;
        while (i < lm_sbn) {
            lm_sbuf[k] = lm_sbuf[i];
            k++;
            i++;
        }
        lm_sbn = k;
        lm_sbuf[lm_sbn] = 0;
    }
}

/* --- the pool ------------------------------------------------------------ */

int lm_natom(void)
{
    return (int) lm_ww(LMW_ATOFF);
}

unsigned lm_atom_off(int aid)
{
    return lm_ww(LMW_ATOFF + 2 + 2 * (unsigned) (aid - WA_APP_FIRST));
}

unsigned lm_atom_len(int aid)
{
    unsigned i = (unsigned) (aid - WA_APP_FIRST);

    return lm_ww(LMW_ATOFF + 4 + 2 * i) - lm_ww(LMW_ATOFF + 2 + 2 * i);
}

void lm_atoms_reset(void)
{
    lm_wpw(LMW_ATOFF, 0);
    lm_wpw(LMW_ATOFF + 2, 0);       /* the free mark */
}

/* ovl_intern - lm_sbuf -> an app atom id (64..250).
 *
 * 0 is the failure answer AND WEAVE-SPEC 2.7's "none", which is not a
 * collision: every caller that can fail has already had lm_perr() set, and
 * every caller that cannot is interning a string it just built. The pack step
 * tests lm_failed() and stops.
 *
 * A STRING LITERAL THAT SPELLS A WELL-KNOWN NAME STILL INTERNS AS AN APP
 * ATOM (2.7): ids 1..63 have no string table in the runtime, so a pooled copy
 * is the only way the literal's bytes exist at run time, and every PUSHA
 * operand is therefore 64..250. This function is never given a well-known id
 * to answer and never looks for one. */
int ovl_intern(int slot, int line)
{
    int n;
    int i;
    unsigned base;
    unsigned free_;
    unsigned len;
    int k;

    /* 2.7 bounds an atom at 1..255 bytes, and BOTH ends refuse. The empty one
     * is the end an implementer forgets: `group=""` on a <radio> reaches here
     * with nothing in the builder, and an interner that pooled it would put a
     * zero-length row in a section whose length byte says otherwise. */
    if (lm_sbover) {
        static char msg[LM_ERRMAX];
        msg[0] = 0;
        lm_cat(msg, "string is ");
        lm_catn(msg, lm_sbwant);
        lm_cat(msg, " bytes; the cap is 255 (WEAVE-SPEC 2.7)");
        lm_perr(slot, line, msg);
        return 0;
    }
    if (lm_sbn == 0) {
        lm_perr(slot, line,
                "empty string: an atom is 1..255 bytes (WEAVE-SPEC 2.7)");
        return 0;
    }
    len = (unsigned) lm_sbn;
    n = lm_natom();
    for (i = 0; i < n; i++) {
        if (lm_atom_len(WA_APP_FIRST + i) != len)
            continue;
        base = lm_atom_off(WA_APP_FIRST + i);
        for (k = 0; k < (int) len; k++) {
            if (lm_wb(LMW_ATTXT + base + (unsigned) k)
                != (unsigned) (unsigned char) lm_sbuf[k])
                break;
        }
        if (k == (int) len)
            return WA_APP_FIRST + i;
    }
    if (n >= LM_MAXATOM) {
        static char msg[LM_ERRMAX];
        msg[0] = 0;
        lm_catn(msg, n + 1);
        lm_cat(msg, " app atoms; the cap is 187 - atom ids are one byte");
        lm_perr(slot, line, msg);
        return 0;
    }
    free_ = lm_ww(LMW_ATOFF + 2 + 2 * (unsigned) n);
    if (free_ + len > LM_ATTXTMAX) {
        lm_perrn(slot, line, "the string pool is ", (int) (free_ + len),
                 " bytes; this machine packs 6144 - the pool is staged in "
                 "the pack claim (WEAVE-SPEC 11.4)");
        return 0;
    }
    for (k = 0; k < (int) len; k++)
        lm_wpb(LMW_ATTXT + free_ + (unsigned) k,
               (unsigned) (unsigned char) lm_sbuf[k]);
    lm_wpw(LMW_ATOFF + 4 + 2 * (unsigned) n, free_ + len);
    lm_wpw(LMW_ATOFF, (unsigned) (n + 1));
    return WA_APP_FIRST + n;
}
