/* ============================================================================
 * os8088 - apps/loom/lmerr.c
 *
 * THE PACK-ERROR VOICE (WEAVE-SPEC 10.5) and the two things every compiler in
 * LOOM.OVL needs before it can say anything: the Latin-1 fold (WEAVE-SPEC
 * 3.1) and the source-run comparisons the scanners are written out of.
 *
 * #included by apps/loom/loom.c first, and by apps/loom/hosttest/lmhost.c
 * first, because both halves of the byte-identity gate (WEAVE-SPEC 11.1) run
 * THIS TEXT and not a copy of it.
 *
 * ---------------------------------------------------------------------------
 * RAISE AND UNWIND BY HAND
 * ---------------------------------------------------------------------------
 * There is no setjmp in this toolchain and no exception in this language, so
 * a pack error is a latch: lm_perr() writes the sentence and sets the flag,
 * every parse routine answers an int, and every caller up the chain returns
 * without emitting the moment lm_failed() is true. The FIRST error wins - a
 * second one is dropped rather than overwriting it, because the sidebar jumps
 * the caret to the first (WEAVE-SPEC 11.3) and a later message would move it
 * somewhere the author has not read yet.
 *
 * THE SENTENCES ARE THE SAME TEXT tools/weavesim.py PRINTS. That is not a
 * politeness: WEAVE-SPEC 10.5 pins them so that three implementations refuse
 * identically, and tests/weave/packerr/ is the corpus that diffs the two.
 * ==========================================================================*/

#include "lmfoldc.h"                 /* GENERATED: weavesim --emit-foldtab-c */

/* WEAVE-SPEC 3.1's fold. Everything to ASCII 0x20..0x7E: printables pass,
 * newline/tab/CR become spaces (WML collapses whitespace and an atom carries
 * none), every other control byte and 0x7F become '?', and 0x80..0xFF go
 * through the generated table - whose zero entry means DROP THE BYTE, which
 * is 0xAD, the soft hyphen, and nothing else. */
int lm_fold(int c)
{
    int v;

    c &= 0xFF;
    if (c >= 0x20 && c <= 0x7E)
        return c;
    if (c == '\n' || c == '\t' || c == '\r')
        return ' ';
    if (c < 0x80)
        return '?';
    v = lm_foldtab[c - 0x80];
    if (v == 0)
        return -1;
    return v;
}

/* --- the error latch ----------------------------------------------------- */

static char lm_err[LM_ERRMAX];
static int  lm_errflag;
static int  lm_errln;
static int  lm_errsl;

/* The four project files, by slot, spelled as WEAVE-SPEC 11.2 spells them.
 * The message names the FILE, so a project whose script is FORM.WJS rather
 * than MAIN.WJS has to report FORM.WJS - lm_fname() answers the name the
 * project actually carries, which lmproj.c sets and the host harness sets
 * from its argv. */
static char lm_slotname[LM_NSLOT][13];

const char *lm_fname(int slot)
{
    if (slot < 0 || slot >= LM_NSLOT)
        return "?";
    return lm_slotname[slot];
}

void lm_setfname(int slot, const char *s)
{
    if (slot >= 0 && slot < LM_NSLOT)
        os88_strcpy(lm_slotname[slot], s, 13);
}

void lm_cat(char *dst, const char *s)
{
    unsigned n = os88_strlen(dst);
    unsigned i = 0;

    while (s[i] != 0 && n + 1 < LM_ERRMAX) {
        dst[n] = s[i];
        n++;
        i++;
    }
    dst[n] = 0;
}

void lm_catn(char *dst, int v)
{
    static char buf[8];

    lm_cat(dst, os88_itoa(v, buf));
}

/* ...and the UNSIGNED one, because 2.1's cap is 63,488 and a bundle that
 * overruns it has a size no `int` can print (SPEC.md 73.7: there is no long
 * here, and `unsigned` is the whole of the range this format needs). */
void lm_catu(char *dst, unsigned v)
{
    static char ubuf[8];

    lm_cat(dst, os88_utoa(v, ubuf));
}

int lm_failed(void)
{
    return lm_errflag;
}

void lm_clearerr(void)
{
    lm_errflag = 0;
    lm_errln = 0;
    lm_errsl = 0;
    lm_err[0] = 0;
}

const char *lm_errtext(void)
{
    return lm_err;
}

int lm_errline(void)
{
    return lm_errln;
}

int lm_errslot(void)
{
    return lm_errsl;
}

/* lm_perr - `<file>:<line>: <message>` (WEAVE-SPEC 10.5's format). */
void lm_perr(int slot, int line, const char *msg)
{
    if (lm_errflag)
        return;                     /* the first one wins */
    lm_errflag = 1;
    lm_errsl = slot;
    lm_errln = line;
    lm_err[0] = 0;
    lm_cat(lm_err, lm_fname(slot));
    lm_cat(lm_err, ":");
    lm_catn(lm_err, line);
    lm_cat(lm_err, ": ");
    lm_cat(lm_err, msg);
}

void lm_perrn(int slot, int line, const char *a, int n, const char *b)
{
    if (lm_errflag)
        return;
    lm_perr(slot, line, a);
    lm_catn(lm_err, n);
    lm_cat(lm_err, b);
}

/* --- source-run comparisons ---------------------------------------------
 * The scanners never copy a name out of the file to look at it: a WML element
 * name, a WJS identifier and a .WSP sprite name are all spans, and every
 * comparison in the compilers is one of these two. lm_srceqi() folds ASCII
 * case, which WEAVE-SPEC 3.1 asks for on element and attribute names and 5.1
 * on FX function names; lm_srceq() does not, which is what 4.2's
 * case-sensitive identifiers need. */

int lm_lower(int c)
{
    if (c >= 'A' && c <= 'Z')
        return c + 32;
    return c;
}

int lm_srceq(int sslot, unsigned off, unsigned len, const char *lit)
{
    unsigned i;

    for (i = 0; i < len; i++) {
        if (lit[i] == 0 || (int) lm_sb(sslot, off + i) != (int) lit[i])
            return 0;
    }
    return lit[len] == 0;
}

int lm_srceqi(int sslot, unsigned off, unsigned len, const char *lit)
{
    unsigned i;

    for (i = 0; i < len; i++) {
        if (lit[i] == 0
            || lm_lower((int) lm_sb(sslot, off + i)) != lm_lower(lit[i]))
            return 0;
    }
    return lit[len] == 0;
}

/* lm_isdigit / lm_isalpha / lm_isalnum - the C library is not on this floppy
 * and locale is not a thing here, so these are the whole of what the scanners
 * need and they are ASCII by definition. */
int lm_isdigit(int c)
{
    return c >= '0' && c <= '9';
}

int lm_isalpha(int c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

int lm_isalnum(int c)
{
    return lm_isalpha(c) || lm_isdigit(c);
}

int lm_isspace(int c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}
