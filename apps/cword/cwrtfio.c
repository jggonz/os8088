/* ============================================================================
 * os8088 - apps/cword/cwrtfio.c        the document, and RTF in and out
 *
 * ---------------------------------------------------------------------------
 * ATTRIBUTION - THE TABLES THIS FILE DRIVES ARE DERIVED FROM MICROSOFT SOURCE
 * ---------------------------------------------------------------------------
 * The reader below is the interpreter loop of MICROSOFT WORD FOR WINDOWS 1.1a
 * ("Opus"), released by the COMPUTER HISTORY MUSEUM in March 2014 as a
 * historical artefact, re-expressed for this toolchain.
 *
 *     Copyright (C) Microsoft Corporation.
 *     Source: Microsoft Word for Windows 1.1a source code, Computer History
 *             Museum release, 2014 (https://computerhistory.org/), directory
 *             `Opus/`.
 *
 * That release carries NO licence grant and every file in it is headed
 * "COPYRIGHT (C) 1987 MICROSOFT". Microsoft remains the copyright holder of
 * the material this is derived from; the repository owner made the call to
 * ship it, explicitly and in full knowledge of that.
 *
 * WHICH OPUS FILE EACH PART CAME FROM:
 *
 *   Opus/RTFIN.C:790-905     the tokenizer and dispatch loop - ovl_rtf_parse()
 *                            and ovl_rtf_control() are that loop, with Opus's
 *                            rac switch kept case for case
 *   Opus/RTFIN2.C:20-95      ApplyPropChange - ovl_apply() is that routine, one
 *                            arm per rrba, over cwrtftbl.h's byte-array records
 *   Opus/RTFIN.C:395-470     the group stack (SavePropsRtf / RestorePropsRtf) -
 *                            ovl_push() / ovl_pop()
 *   Opus/RTFOUT.C            the writer's shape: a header group, a \fonttbl,
 *                            document geometry, then \pard\plain and the text
 *                            with a control word only where something CHANGES
 *
 * apps/cword/cwrtftbl.[ch] carry the tables themselves and the full
 * attribution for those; this file is the code that walks them.
 *
 * ---------------------------------------------------------------------------
 * WHAT IS IN HERE, AND WHY IT IS ITS OWN FILE
 * ---------------------------------------------------------------------------
 * The document (the text, the per-character formatting, and the two counts
 * that bound them) and the two conversions between it and RTF. Nothing here
 * calls the operating system - not one os88_* function, not one drawing call,
 * no file I/O. The UI in cword.c hands it a buffer that is already in memory
 * and takes back a buffer to write.
 *
 * That is a deliberate split and it buys the one thing this program could not
 * otherwise have: **the RTF engine is testable on the build host.**
 * apps/cword/cwrtfrt.c is a host program that includes this file and round
 * trips documents through it, so a defect in the writer or the reader is a
 * failed build on a Mac rather than a corrupted file discovered in an
 * emulator. `nasm -f bin` has no notion of an external symbol, so a C package
 * is exactly one translation unit (SPEC.md 73.1) - this file is #included by
 * cword.c, once, and by the host harness, once, and is not compiled alone.
 *
 * ---------------------------------------------------------------------------
 * THE DOCUMENT MODEL, AND ITS TWO HARD CAPS
 * ---------------------------------------------------------------------------
 * Two parallel arrays, one byte each per character:
 *
 *   cw_buf[i]   the character. Printable ASCII 32..126, or '\n' which ends a
 *               paragraph. Nothing else is ever stored - the reader maps
 *               every special character it meets onto one of those.
 *   cw_att[i]   its formatting: CW_A_BOLD | CW_A_ITAL | CW_A_ULINE.
 *
 * A parallel byte array rather than a run list because every access in the
 * layout and redraw paths is then O(1) with no search, and because bss is the
 * cheap half of the 60KB ceiling (SPEC.md 73.9): 4,000 characters cost 8,000
 * bytes of bss and NO file bytes, where the equivalent run list would cost
 * code, which is paid for twice.
 *
 *   CW_DOC_MAX  4,000 characters, about two typed pages. HARD, ENFORCED, AND
 *               REPORTED: ovl_put() refuses past it and raises cw_trunc, the
 *               reader stops filling and says so, and the editor greys and
 *               toasts rather than dropping keys silently (SPEC.md 47).
 *               It is far below the 65,535 that cwrtftbl.h's 16-bit CP
 *               narrowing forces, so that narrowing can never be reached from
 *               here - which is the point of picking a small number.
 *   CW_RTF_MAX  12,000 bytes of RTF, in one static buffer used for BOTH
 *               directions. A file bigger than that is refused BEFORE it is
 *               read, off the size the Standard File dialog already knows
 *               (SPEC.md 38.6) - reading 100KB off a floppy to then say no is
 *               about ten seconds of motor the user cannot tell from a load
 *               that works. A document whose RTF would not fit is refused at
 *               save time with the count that did fit.
 *
 * ---------------------------------------------------------------------------
 * WHAT IT COSTS (PERFORMANCE.md; the target is a 4.77 MHz 8088)
 * ---------------------------------------------------------------------------
 * Neither conversion is on a redraw path: both happen once, inside a menu
 * command, and both are dwarfed by the int 13h calls around them - ~400 ms
 * EACH, whatever they move. A 12,000-byte document is ~24 sectors, and the
 * disk is two orders of magnitude more expensive than the parse.
 *
 *   the parse   one pass, one byte at a time, ~30-60 8088 cycles a byte plus
 *               ~0.2 ms per control word for cwrtftbl.c's binary search
 *               (7 probes over 76 names). 12KB with ~1,500 control words is
 *               roughly 0.4 s of CPU against ~10 s of floppy.
 *   the write   one pass over the characters, a few bytes emitted each.
 *
 * So this file is written for clarity and for the refusal paths, not for
 * speed, and that is a decision with a number behind it rather than a hope.
 * ==========================================================================*/

/* --- the document --------------------------------------------------------- */

#define CW_DOC_MAX   4000               /* characters; see the caps above */
#define CW_RTF_MAX   6000               /* bytes of RTF, both directions, and
                                         * the tightest number in the package:
                                         * see the note in apps/cword/cword.c
                                         * about what the 60KB segment holds */

#define CW_A_BOLD      1                /* cw_att[] bits. They are the app's, */
#define CW_A_ITAL      2                /* not Opus's - the mapping onto      */
#define CW_A_ULINE     4                /* CW_CHP_* happens in ovl_attr_now()  */
#define CW_A_WUL      8                 /* word underline    (Ctrl-W) */
#define CW_A_DUL     16                 /* double underline  (Ctrl-D) */
#define CW_A_SCAP    32                 /* small caps        (Ctrl-K) */
#define CW_A_HID     64                 /* hidden text                */
#define CW_A_CHP    127                 /* every character bit        */
#define CW_A_ALL     127                /* every bit the writer carries */

/* --- the paragraph-format dictionary --------------------------------------
 * Paragraph formatting lives on the paragraph MARK: a '\n' carries, in its
 * cw_att[] byte, an index into this dictionary (apps/cword/cword.c says why).
 * It is here and not in cword.c because the reader makes entries and the
 * writer reads them, and this file is what the host harness compiles. */
#define CW_PAP_MAX   64                 /* unique paragraph formats. Word's own
                                         * dictionary is 256; 64 x 4 bytes is
                                         * what this ceiling affords, and the
                                         * 65th unique format is a refusal with
                                         * a reason rather than a silent
                                         * flattening (SPEC.md 47) */
#define CW_AL_LEFT    0
#define CW_AL_CENTER  1
#define CW_AL_RIGHT   2
#define CW_AL_JUST    3

/* The dictionary, four parallel byte arrays rather than an array of structs
 * so that a careless assignment cannot become a `rep movsb`. */
unsigned char cw_pap_al[CW_PAP_MAX];    /* CW_AL_*                     */
unsigned char cw_pap_sp[CW_PAP_MAX];    /* 0 single, 1 one-and-a-half,
                                                * 2 double                    */
unsigned char cw_pap_op[CW_PAP_MAX];    /* space before the paragraph  */
unsigned char cw_pap_li[CW_PAP_MAX];    /* left indent, in cells       */
signed char   cw_pap_fi[CW_PAP_MAX];    /* first line, relative to it  */
unsigned char cw_pap_ri[CW_PAP_MAX];    /* right indent                */
int cw_pap_n = 1;                       /* entry 0 is the default and
                                                * is never reclaimed          */
int cw_pap_now;                         /* the PAP new paragraphs get. NOT
                                         * `cw_pap`: cwrtfio.c owns that name
                                         * for the RTF reader's property
                                         * record, and one translation unit
                                         * means one namespace (SPEC.md 73.1) */


char          cw_buf[CW_DOC_MAX];       /* the text */
unsigned char cw_att[CW_DOC_MAX];       /* its formatting, one byte a character */
int           cw_len;                   /* characters in use */
int           cw_trunc;                 /* the last fill hit CW_DOC_MAX */

char          cw_rtf[CW_RTF_MAX];       /* the RTF, read or written */
int           cw_rtfn;                  /* bytes in it */
int           cw_ovf;                   /* the writer ran out of cw_rtf */

/* --- the reader's property records (cwrtftbl.h owns the layouts) ----------
 * Byte arrays with manifest offsets, exactly as Opus's ApplyPropChange treats
 * them, so no compiler layout decision can disagree with a table row. cword
 * keeps four of the nine pgc records; ovl_record() answers 0 for the five it
 * does not (tables, pictures, borders, sections, date fields), and a property
 * aimed at one of those is dropped where it arrives rather than tested for at
 * every call site. */
unsigned char cw_chp[CW_CB_CHP];        /* character properties */
unsigned char cw_pap[CW_CB_PAP];        /* paragraph properties */
unsigned char cw_dop[CW_CB_DOP];        /* document properties */
unsigned char cw_ris[CW_CB_RIS];        /* the reader's own state */

int cw_rds;                             /* the destination, CW_RDS_* */
int cw_tjc;                             /* the pending tab stop's jc */

/* The group stack (Opus SavePropsRtf, RTFIN.C:395). Parallel arrays and not
 * an array of structs, because a struct assignment is a `rep movsb` and ES is
 * the kernel's (SPEC.md 73.5.1) - the same rule that shapes the document
 * model shapes this. Only the state cword can actually act on is stacked:
 * character formatting, the destination, and the \* flag. */
#define CW_STK_MAX  16                  /* {\rtf{\fonttbl{\f0 x;}}} is 3 */
int cw_stk_grpf[CW_STK_MAX];
int cw_stk_kul[CW_STK_MAX];
int cw_stk_rds[CW_STK_MAX];
int cw_stk_n;

/* --- parse results -------------------------------------------------------- */
#define CW_RTF_OK       0
#define CW_RTF_NOTRTF   1               /* it does not begin {\rtf */
#define CW_RTF_TRUNC    2               /* the text did not fit CW_DOC_MAX */
#define CW_RTF_DEEP     3               /* more than CW_STK_MAX nested groups */

/* ===========================================================================
 * SMALL THINGS, WRITTEN HERE RATHER THAN CALLED
 *
 * This file has no operating system under it (see the head of the file), so
 * it carries its own three-line helpers instead of os88_memset() and friends.
 * They are also the reason the host harness needs no stubs.
 * ========================================================================= */

void ovl_zero(unsigned char *p, int n)
{
    int i;
    for (i = 0; i < n; i++)
        p[i] = 0;
}

/* A property word, read and written BYTE BY BYTE rather than through a cast.
 * cwrtftbl.h publishes CW_PROP_W() for the target, where `int` is the two
 * bytes the record was designed around; this file is also compiled on the
 * BUILD HOST, where `int` is four and a cast at offset 0 of an 8-byte record
 * would reach across the field at offset 2. Two byte accesses mean the host
 * harness exercises the same arithmetic the 8088 will, which is the whole
 * value of having a host harness. It costs a few instructions in a path that
 * runs once per document. */
int ovl_pw_get(const unsigned char *p, int o)
{
    int v;
    v = p[o];
    v = v | (p[o + 1] << 8);
    return v;                           /* 0..65535 on the host, the same 16
                                         * bits on the target */
}

void ovl_pw_set(unsigned char *p, int o, int v)
{
    p[o]     = (unsigned char)v;
    p[o + 1] = (unsigned char)(v >> 8);
}

/* ===========================================================================
 * THE DOCUMENT
 * ========================================================================= */

/* cw_doc_clear - an empty document. */
void cw_doc_clear(void)
{
    cw_len = 0;
    cw_trunc = 0;
}

/* ==========================================================================
 * THE PARAGRAPH DICTIONARY
 *
 * ovl_pap_find() is the only way an entry is made, and it DEDUPLICATES: a
 * document where every paragraph is left-aligned and unindented uses one
 * entry, which is what keeps a 64-entry dictionary sufficient for a real
 * document rather than a toy one.
 * ========================================================================*/

/* out: the index of an entry with these properties, making one if needed, or
 * -1 if the dictionary is full - a refusal the caller reports (SPEC.md 47). */
int ovl_pap_find(int al, int sp, int op, int li, int fi, int ri)
{
    int i;

    for (i = 0; i < cw_pap_n; i++) {
        if (cw_pap_al[i] == al && cw_pap_sp[i] == sp && cw_pap_op[i] == op &&
            cw_pap_li[i] == li && cw_pap_fi[i] == fi && cw_pap_ri[i] == ri)
            return i;
    }
    if (cw_pap_n >= CW_PAP_MAX)
        return -1;
    i = cw_pap_n;
    cw_pap_al[i] = (unsigned char)al;
    cw_pap_sp[i] = (unsigned char)sp;
    cw_pap_op[i] = (unsigned char)op;
    cw_pap_li[i] = (unsigned char)li;
    cw_pap_fi[i] = (signed char)fi;
    cw_pap_ri[i] = (unsigned char)ri;
    cw_pap_n++;
    return i;
}

/* ovl_attr_now - the editor attribute the reader's CHP currently means.
 *
 * This is the ONE place Opus's character properties and cword's seven
 * formatting bits meet. Bold, italic, small caps and hidden are four of the
 * ten flags in the CHP flag word; the underline is the kul byte, and cword
 * draws three of Word's four kinds - single, word and double - so kulDotted
 * arrives as a single rule. The other flags, the font, the size, the colour
 * and the super/subscript are READ AND DISCARDED. They are parsed - the
 * records above hold them, correctly - so adding a renderer for one is a
 * change in cword.c and not here.
 *
 * It used to fold every underline into one and drop small caps and hidden
 * text, so a document saved and opened again had lost them (SPEC.md 73.12.4). */
int ovl_attr_now(void)
{
    int a;
    int g;
    int k;

    a = 0;
    g = ovl_pw_get(cw_chp, CW_CHP_O_GRPF);
    if (g & CW_CHP_F_BOLD)
        a = a | CW_A_BOLD;
    if (g & CW_CHP_F_ITALIC)
        a = a | CW_A_ITAL;
    if (g & CW_CHP_F_SMALLCAPS)
        a = a | CW_A_SCAP;
    if (g & CW_CHP_F_VANISH)
        a = a | CW_A_HID;
    k = cw_chp[CW_CHP_O_KUL];
    if (k == CW_KUL_WORD)
        a = a | CW_A_WUL;
    else if (k == CW_KUL_DOUBLE)
        a = a | CW_A_DUL;
    else if (k != CW_KUL_NONE)
        a = a | CW_A_ULINE;
    return a;
}

/* ovl_tw2c - twips to the ruler's cells (1/10 inch = 144 twips), rounded to
 * the nearest and SIGNED: a first-line indent is negative for a hanging one.
 * The word arrives as 16 bits from ovl_pw_get(), which is 0..65535 on the
 * build host and the same bits on the target; the OR below sign-extends it on
 * the host and changes nothing on the 8088, where an int is those 16 bits. */
int ovl_tw2c(int v)
{
    if (v & 0x8000)
        v = v | ~0x7FFF;
    if (v < 0)
        return 0 - ((72 - v) / 144);
    return (v + 72) / 144;
}

/* ovl_pap_now - the dictionary entry the reader's PAP currently means, which
 * is what a paragraph mark read from the file carries. The mapping is the
 * writer's below, run backwards: \qc \qr \qj, a line height over 1.75 lines
 * is double and over 1.25 one and a half, any space before is Word's Open
 * Space, and the three indents are rounded to the ruler's cells. A dictionary
 * that is full answers entry 0 rather than refusing the file: the text is
 * kept and the paragraph is flush left, which is visible rather than silent. */
int ovl_pap_now(void)
{
    int al;
    int sl;
    int sp;
    int li;
    int fi;
    int ri;
    int i;

    al = cw_pap[CW_PAP_O_JC];
    if (al > CW_AL_JUST)
        al = CW_AL_LEFT;
    sl = ovl_pw_get(cw_pap, CW_PAP_O_DYALINE);
    if (sl & 0x8000)
        sl = 0 - (sl | ~0x7FFF);        /* \sl-480 is "exactly 480" */
    sp = 0;
    if (sl > 420)
        sp = 2;
    else if (sl > 300)
        sp = 1;
    li = ovl_tw2c(ovl_pw_get(cw_pap, CW_PAP_O_DXALEFT));
    fi = ovl_tw2c(ovl_pw_get(cw_pap, CW_PAP_O_DXALEFT1));
    ri = ovl_tw2c(ovl_pw_get(cw_pap, CW_PAP_O_DXARIGHT));
    if (li < 0)
        li = 0;
    if (li > 120)
        li = 120;
    if (ri < 0)
        ri = 0;
    if (ri > 120)
        ri = 120;
    if (fi < 0 - li)
        fi = 0 - li;
    if (fi > 120)
        fi = 120;
    i = ovl_pap_find(al, sp, ovl_pw_get(cw_pap, CW_PAP_O_DYABEFORE) != 0,
                     li, fi, ri);
    return (i < 0) ? 0 : i;
}

/* ovl_put - append one character in the current formatting.
 * Refuses at CW_DOC_MAX and raises cw_trunc; a refusal is a normal path here
 * and the caller reports it (SPEC.md 47). */
void ovl_put(int c, int a)
{
    if (cw_len >= CW_DOC_MAX) {
        cw_trunc = 1;
        return;
    }
    cw_buf[cw_len] = (char)c;
    /* A PARAGRAPH MARK CARRIES A PARAGRAPH FORMAT, NOT CHARACTER BITS
     * (apps/cword/cword.c): its attribute byte is an index into the format
     * dictionary, and the entry is the one the reader's PAP means at the mark
     * - which is where Word's RTF puts a paragraph's properties in force. The
     * alternative, leaving the character formatting there, would be read back
     * as a dictionary index and centre a paragraph because the word before it
     * happened to be bold. */
    cw_att[cw_len] = (unsigned char)((c == '\n') ? ovl_pap_now() : a);
    cw_len++;
}

/* ovl_put_special - one of Word's special characters (Opus racSpecChar), by
 * the character code the keyword table carries.
 *
 * WHERE cword NARROWS, AND IT IS VISIBLE RATHER THAN SILENT: the document
 * model holds printable ASCII and '\n' only, so a soft line break (\line) and
 * a page break (\page) both become paragraph ends, a tab becomes one space, a
 * non-breaking space becomes a space, a non-breaking hyphen becomes a hyphen,
 * and an optional hyphen - which is a hyphen that is NOT to be printed unless
 * the line breaks there - becomes nothing at all. Every one of those keeps
 * the text readable and loses a distinction cword cannot draw. */
void ovl_put_special(int val, int a)
{
    if (val == CW_CH_EOP || val == CW_CH_CRJ || val == CW_CH_SECT) {
        ovl_put('\n', a);
        return;
    }
    if (val == CW_CH_TAB || val == CW_CH_NONBREAKSP) {
        ovl_put(' ', a);
        return;
    }
    if (val == CW_CH_NONBREAKHYPH) {
        ovl_put('-', a);
        return;
    }
    if (val == CW_CH_NONREQHYPH)
        return;                         /* an optional hyphen: print nothing */
    if (val >= 32 && val <= 126)
        ovl_put(val, a);
}

/* ===========================================================================
 * APPLYING A PROPERTY - Opus/RTFIN2.C:20-95, one arm per rrba
 * ========================================================================= */

/* ovl_record - the byte array a pgc names, or 0 for one cword does not keep. */
unsigned char *ovl_record(int pgc)
{
    if (pgc == CW_PGC_CHAR)
        return cw_chp;
    if (pgc == CW_PGC_PARA)
        return cw_pap;
    if (pgc == CW_PGC_DOC)
        return cw_dop;
    if (pgc == CW_PGC_RIS)
        return cw_ris;
    return 0;                           /* SECT, PIC, BRC, TABLE, DTR */
}

/* ovl_pard / ovl_plain - the two resets.
 *
 * THESE ARE WRITTEN OUT AND NOT DRIVEN OFF THE TABLE, and that is the single
 * most easily-got-wrong thing in this file, so here is the reason in full.
 *
 * It is tempting to reset a range by walking cw_rrb[] from CW_IRRB_PAP_FIRST
 * to CW_IRRB_PAP_LIM and storing each row's `w`. That is WRONG, because `w`
 * is not a reset value: it is THE VALUE THE KEYWORD CARRIES WHEN IT ARRIVES
 * WITH NO NUMBER. The underline row is the proof - cw_rrb[CW_IRRB_KUL].w is
 * kulSingle, because a bare \ul means single underline. Reset the range from
 * the table and \plain switches underlining ON.
 *
 * So the resets are explicit. Every PAP field is zero at rest (jcLeft is 0,
 * stcNormal is 0, no indents, no tab stops), so \pard is a zero fill. Every
 * CHP field is zero at rest EXCEPT the size, which is RTF's default of 24
 * half-points - the same 24 the \fs row carries, and the one value the two
 * meanings of that column agree on. */
void ovl_pard(void)
{
    ovl_zero(cw_pap, CW_CB_PAP);
    cw_tjc = CW_JC_LEFT;
}

void ovl_plain(void)
{
    ovl_zero(cw_chp, CW_CB_CHP);
    cw_chp[CW_CHP_O_HPS] = 24;          /* RTF's default, not Opus's hpsDefault */
}

/* ovl_apply_spec - the rrbaSpec rows, which are the ones no generic store can
 * express. Opus keeps a switch here too (RTFIN2.C:88) for exactly the rows
 * whose meaning is not "put this value at this offset". */
void ovl_apply_spec(int irrb, int val, int fval, int b)
{
    int n;

    if (irrb == CW_IRRB_PARD) {
        ovl_pard();
        return;
    }
    if (irrb == CW_IRRB_PLAIN) {
        ovl_plain();
        return;
    }
    if (irrb == CW_IRRB_TJC) {
        cw_tjc = val;                   /* the NEXT \tx's justification */
        return;
    }
    if (irrb == CW_IRRB_TX) {
        n = cw_pap[CW_PAP_O_ITBDMAC];
        if (n >= CW_ITBD_MAX)
            return;                     /* 12 stops, where Opus has 50: the
                                         * extra ones are dropped, and that is
                                         * visible in the ruler rather than
                                         * silent (cwrtftbl.h's narrowing) */
        ovl_pw_set(cw_pap, CW_PAP_O_RGDXATAB + n + n, val);
        cw_pap[CW_PAP_O_RGTBD + n] = (unsigned char)CW_TBD(cw_tjc, CW_TLC_NONE);
        cw_pap[CW_PAP_O_ITBDMAC] = (unsigned char)(n + 1);
        cw_tjc = CW_JC_LEFT;            /* the stop consumed it */
        return;
    }
    if (irrb == CW_IRRB_UP) {
        cw_chp[CW_CHP_O_HPSPOS] = (unsigned char)(fval ? val : 6);
        return;
    }
    if (irrb == CW_IRRB_DN) {
        /* hpsPos is ONE 2's-complement byte and \dn is the negative half of
         * it, which is why Opus keeps both rows rrbaSpec (divergence 5 in
         * cwrtftbl.c) - no generic store can write 256 - n. */
        n = fval ? val : 6;
        cw_chp[CW_CHP_O_HPSPOS] = (unsigned char)(0 - n);
        return;
    }
    if (irrb == CW_IRRB_RED || irrb == CW_IRRB_GREEN || irrb == CW_IRRB_BLUE) {
        cw_ris[b] = (unsigned char)val; /* a \colortbl entry, completed at the
                                         * ';' that ends it. cword has one
                                         * palette and does not use the
                                         * result, but the components must be
                                         * consumed or their numbers would be
                                         * read as text */
        return;
    }
    if (irrb == CW_IRRB_DEFF) {
        ovl_pw_set(cw_ris, CW_RIS_O_DEFF, val);
        return;
    }
    /* CW_IRRB_IGNORE and anything else: recognised, does nothing. */
}

/* ovl_apply - Opus's ApplyPropChange.
 *
 * in:  irrb - a CW_IRRB_* row index
 *      val  - the number that followed the keyword, or the keyword's own val
 *      fval - 1 if a number (or a fPassVal keyword value) was actually given
 * out: the property record named by the row is updated */
void ovl_apply(int irrb, int val, int fval)
{
    unsigned char *p;
    int rrba;
    int b;
    int o;
    int v;
    int m;

    rrba = ovl_rrb_rrba(irrb);
    b    = ovl_rrb_b(irrb);

    if (rrba == CW_RRBA_SPEC) {
        ovl_apply_spec(irrb, val, fval, b);
        return;
    }

    p = ovl_record(ovl_rrb_pgc(irrb));
    if (p == 0)
        return;                         /* a record cword does not keep */

    if (rrba == CW_RRBA_FLAG) {
        /* Presence sets it; \keep0 clears it. */
        p[b] = (unsigned char)(fval ? (val != 0) : 1);
        return;
    }

    if (rrba == CW_RRBA_BIT) {
        /* On a BIT row `b` is a BIT INDEX and `w` is a WORD INDEX into the
         * record - Opus's own irregularity in that column (cwrtftbl.c says
         * so), kept so every row stays comparable with the row it came from.
         * `w` is therefore NOT a default, which is why this arm is ABOVE the
         * "no number: use the row's value" line and not below it: a bit row
         * has no default value, only "set" and the "\b0" that clears it. */
        m = 1;
        while (b > 0) {
            m = m + m;
            b--;
        }
        o = ovl_rrb_w(irrb);
        o = o + o;                      /* a word index -> a byte offset */
        v = ovl_pw_get(p, o);
        if (fval && val == 0)
            v = v & (0xFFFF - m);       /* ~m, without relying on the width of
                                         * `int`: this file compiles on the
                                         * host too, where ~m has 16 more bits
                                         * set than the record has */
        else
            v = v | m;
        ovl_pw_set(p, o, v);
        return;
    }

    if (!fval)
        val = ovl_rrb_w(irrb);           /* no number: the row's own value */

    if (rrba == CW_RRBA_BYTE) {
        p[b] = (unsigned char)val;
        return;
    }

    /* CW_RRBA_WORD and CW_RRBA_UNS. The difference is how the NUMBER was
     * scanned (a leading '-' is legal for one and not the other), which the
     * tokenizer has already dealt with by the time it gets here. */
    ovl_pw_set(p, b, val);
}

/* ===========================================================================
 * THE READER
 *
 * Opus/RTFIN.C:790-905. One pass, one byte at a time. Four things can appear:
 * a '{' or '}' that moves the group stack, a '\' that starts a control word,
 * and anything else, which is text.
 * ========================================================================= */

int cw_skipd;                           /* group depth being discarded, -1 = no */
int cw_depth;                           /* current group depth */

/* ovl_push / ovl_pop - the group stack. Opus saves the whole property state; the
 * three things cword can act on are the character flags, the underline and
 * the destination. On overflow the parse stops with CW_RTF_DEEP rather than
 * running off the array: a malformed file is not allowed to be a memory bug,
 * and every byte read off a disk is hostile (CLAUDE.md). */
int ovl_push(void)
{
    if (cw_stk_n >= CW_STK_MAX)
        return -1;
    cw_stk_grpf[cw_stk_n] = ovl_pw_get(cw_chp, CW_CHP_O_GRPF);
    cw_stk_kul[cw_stk_n]  = cw_chp[CW_CHP_O_KUL];
    cw_stk_rds[cw_stk_n]  = cw_rds;
    cw_stk_n++;
    return 0;
}

void ovl_pop(void)
{
    if (cw_stk_n <= 0)
        return;                         /* an unbalanced '}': ignore it */
    cw_stk_n--;
    ovl_pw_set(cw_chp, CW_CHP_O_GRPF, cw_stk_grpf[cw_stk_n]);
    cw_chp[CW_CHP_O_KUL] = (unsigned char)cw_stk_kul[cw_stk_n];
    cw_rds = cw_stk_rds[cw_stk_n];
}

int ovl_isalpha(int c)
{
    if (c >= 'a' && c <= 'z')
        return 1;
    if (c >= 'A' && c <= 'Z')
        return 1;
    return 0;
}

int ovl_hexval(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* The scanned control word, in file scope because an out-parameter would have
 * to point at something and pointing it at an automatic is refused at build
 * time (SPEC.md 73.5). */
char cw_kw[34];
int  cw_kw_val;
int  cw_kw_fval;

/* ovl_rtf_control - scan and act on the control word at cw_rtf[i], where
 * cw_rtf[i] is the backslash.
 * out: the offset of the first byte after it (and after the one optional
 *      space that delimits it, which is part of the control word and not
 *      text - the classic way to lose every space in a document). */
int ovl_rtf_control(int i, int n)
{
    int j;
    int k;
    int c;
    int neg;
    int irsym;
    int rac;
    int arg;
    int val;
    int a;

    j = i + 1;
    if (j >= n)
        return n;

    cw_kw_val  = 0;
    cw_kw_fval = 0;
    c = (unsigned char)cw_rtf[j];

    if (!ovl_isalpha(c)) {
        /* A control SYMBOL: exactly one character, and it is its own name.
         * \n and \r - a backslash followed by a line break - are two of them,
         * and cwrtftbl.c carries both as one-character rows for that reason
         * (Opus's iszRTFLF / iszRTFCR). */
        cw_kw[0] = (char)c;
        cw_kw[1] = 0;
        j++;
    } else {
        k = 0;
        while (j < n && ovl_isalpha((unsigned char)cw_rtf[j])) {
            if (k < 32) {
                cw_kw[k] = cw_rtf[j];
                k++;
            }
            j++;
        }
        cw_kw[k] = 0;

        neg = 0;
        if (j < n && cw_rtf[j] == '-') {
            neg = 1;
            j++;
        }
        val = 0;
        while (j < n && cw_rtf[j] >= '0' && cw_rtf[j] <= '9') {
            val = val * 10 + (cw_rtf[j] - '0');
            if (val > 30000)
                val = 30000;            /* clamp: RRB.w is a signed 16-bit
                                         * word (cwrtftbl.h's narrowing), and
                                         * a page size that wrapped would be
                                         * silently wrong where a clamped one
                                         * is merely visible */
            cw_kw_fval = 1;
            j++;
        }
        if (neg)
            val = 0 - val;
        cw_kw_val = val;

        /* ONE space after a control word is the delimiter and belongs to the
         * control word. A second space is text. */
        if (j < n && cw_rtf[j] == ' ')
            j++;
    }

    irsym = ovl_rtf_lookup(cw_kw);

    if (irsym < 0) {
        /* An unknown control word. If \* preceded it, the writer has told us
         * the whole group is safe to throw away; otherwise the control word
         * alone is ignored and its group's text still counts. The two skip
         * paths are not the same path (cwrtftbl.h's contract). */
        if (cw_ris[CW_RIS_O_FNEXTISDEST]) {
            cw_ris[CW_RIS_O_FNEXTISDEST] = 0;
            if (cw_skipd < 0)
                cw_skipd = cw_depth;
        }
        return j;
    }

    rac = ovl_rsym_rac(irsym);
    arg = ovl_rsym_arg(irsym);

    if (ovl_rsym_passval(irsym)) {       /* the keyword IS the value: \qc, \ansi */
        cw_kw_val  = ovl_rsym_val(irsym);
        cw_kw_fval = 1;
    }

    /* Any recognised keyword other than \* clears the flag \* set. The row for
     * \* is an ordinary CHNGPROP that stores 1 in CW_RIS_O_FNEXTISDEST, so it
     * must not be cleared before it is applied. */
    if (arg != CW_IRRB_ASTERISK || rac != CW_RAC_CHNGPROP)
        cw_ris[CW_RIS_O_FNEXTISDEST] = 0;

    if (cw_skipd >= 0) {
        /* Inside a discarded group nothing is applied, but \bin still has to
         * be measured or its raw bytes would be read as RTF. */
        if (rac == CW_RAC_BINPARM && cw_kw_fval && cw_kw_val > 0)
            j = j + cw_kw_val;
        return j;
    }

    if (rac == CW_RAC_CHNGPROP) {
        ovl_apply(arg, cw_kw_val, cw_kw_fval);
        return j;
    }

    if (rac == CW_RAC_SPECCHAR) {
        if (cw_rds == CW_RDS_MAIN)
            ovl_put_special(ovl_rsym_val(irsym), ovl_attr_now());
        return j;
    }

    if (rac == CW_RAC_SPECCHARACT) {
        if (arg == CW_IPFN_PARAEND) {
            if (cw_rds == CW_RDS_MAIN)
                ovl_put('\n', ovl_attr_now());
            return j;
        }
        if (arg == CW_IPFN_TAB) {
            if (cw_rds == CW_RDS_MAIN)
                ovl_put_special(CW_CH_TAB, ovl_attr_now());
            return j;
        }
        if (arg == CW_IPFN_HEXCHAR) {
            /* \'hh - two hex digits, and they are NOT preceded by the space
             * rule above, so they are read straight off j. */
            val = -1;
            if (j + 1 < n) {
                a = ovl_hexval((unsigned char)cw_rtf[j]);
                k = ovl_hexval((unsigned char)cw_rtf[j + 1]);
                if (a >= 0 && k >= 0) {
                    val = (a << 4) | k;
                    j = j + 2;
                }
            }
            if (val >= 0 && cw_rds == CW_RDS_MAIN) {
                if (val >= 32 && val <= 126)
                    ovl_put(val, ovl_attr_now());
                else
                    ovl_put_special(val, ovl_attr_now());
            }
            return j;
        }
        return j;
    }

    if (rac == CW_RAC_CHNGDEST) {
        if (arg == CW_IPFN_RDSRTF)
            cw_rds = CW_RDS_MAIN;
        else if (arg == CW_IPFN_RDSNEWSTD)
            cw_rds = ovl_rsym_val(irsym); /* \fonttbl, \colortbl */
        return j;
    }

    if (rac == CW_RAC_SCANBYDEST) {
        /* \info \footnote \header \footer \stylesheet \pict: ordinary
         * destinations, NOT marked \*, whose text would otherwise flow into
         * page one. Discard the group. */
        cw_skipd = cw_depth;
        return j;
    }

    if (rac == CW_RAC_BINPARM) {
        if (cw_kw_fval && cw_kw_val > 0)
            j = j + cw_kw_val;
        return j;
    }

    return j;                           /* CW_RAC_IGNORE */
}

/* ovl_rtf_parse - read cw_rtf[0..n) into the document.
 *
 * The document is REPLACED, and only after the file has been recognised: a
 * file that does not begin `{\rtf` is refused with the old document intact,
 * because the alternative is a text editor that empties itself when you open
 * the wrong icon.
 *
 * out: CW_RTF_OK, or a CW_RTF_* reason. TRUNC and DEEP both leave a partial
 *      document on purpose - what was read is readable, and the caller says
 *      what was lost (SPEC.md 47). */
int ovl_rtf_parse(int n)
{
    int i;
    int c;
    int rc;

    if (n < 5)
        return CW_RTF_NOTRTF;
    if (cw_rtf[0] != '{' || cw_rtf[1] != '\\' ||
        cw_rtf[2] != 'r' || cw_rtf[3] != 't' || cw_rtf[4] != 'f')
        return CW_RTF_NOTRTF;

    cw_doc_clear();
    cw_pap_n = 1;                       /* a new document, a new dictionary */
    ovl_zero(cw_ris, CW_CB_RIS);
    ovl_zero(cw_dop, CW_CB_DOP);
    ovl_pard();
    ovl_plain();
    cw_rds   = CW_RDS_MAIN;
    cw_stk_n = 0;
    cw_depth = 0;
    cw_skipd = -1;
    rc = CW_RTF_OK;

    i = 0;
    while (i < n) {
        c = (unsigned char)cw_rtf[i];

        if (c == '{') {
            if (ovl_push() != 0)
                return CW_RTF_DEEP;
            cw_depth++;
            i++;
            continue;
        }
        if (c == '}') {
            cw_depth--;
            ovl_pop();
            if (cw_skipd >= 0 && cw_depth < cw_skipd)
                cw_skipd = -1;          /* the discarded group CLOSED - and it
                                         * is `<` and not `<=`, because
                                         * cw_skipd is the depth INSIDE the
                                         * group being discarded. With `<=`
                                         * the first nested group to close
                                         * ends the skip, and {\info{\title
                                         * X}} puts X in the document: the
                                         * exact failure the fixture in
                                         * apps/cword/cwrtfrt.c caught */
            i++;
            continue;
        }
        if (c == '\\') {
            i = ovl_rtf_control(i, n);
            continue;
        }
        i++;
        if (c == 13 || c == 10)
            continue;                   /* a raw line break in the FILE is
                                         * nothing; \<line break> is \par, and
                                         * that one arrives through the table */
        if (cw_skipd >= 0)
            continue;
        if (cw_rds != CW_RDS_MAIN)
            continue;                   /* \fonttbl / \colortbl text */
        if (c == 9)
            c = ' ';
        if (c < 32 || c > 126)
            continue;                   /* the model holds printable ASCII */
        ovl_put(c, ovl_attr_now());
    }

    /* ...and the last paragraph, which has no mark of its own: it takes the
     * typing format, and the format in force at the end of the file is its. */
    cw_pap_now = ovl_pap_now();

    if (cw_trunc)
        rc = CW_RTF_TRUNC;
    return rc;
}

/* ===========================================================================
 * THE WRITER
 *
 * A control word only where something CHANGES, which is both what Opus's
 * writer does and the only way a 4,000-character document stays inside
 * CW_RTF_MAX: a file that re-stated the formatting per character would be
 * five times the size of the text.
 * ========================================================================= */

void ovl_emitc(int c)
{
    if (cw_rtfn >= CW_RTF_MAX) {
        cw_ovf = 1;
        return;
    }
    cw_rtf[cw_rtfn] = (char)c;
    cw_rtfn++;
}

void ovl_emit(const char *s)
{
    int i;

    i = 0;
    while (s[i] != 0) {
        ovl_emitc((unsigned char)s[i]);
        i++;
    }
}

/* The digit stack is STATIC, not a local array: the address of an automatic is
 * a bare offset that every later dereference resolves through DS while the
 * bytes live on the LOW_SEG stack, and tools/cc8086.py refuses the
 * bp-relative `lea` that would produce it (SPEC.md 73.5). A local `char d[8]`
 * here is the most natural thing to write and it does not build. */
char cw_dig[8];

void ovl_emitnum(int v)
{
    int i;

    if (v < 0) {
        ovl_emitc('-');
        v = 0 - v;
    }
    i = 0;
    do {
        cw_dig[i] = (char)('0' + (v % 10));
        i++;
        v = v / 10;
    } while (v != 0 && i < 7);
    while (i > 0) {
        i--;
        ovl_emitc(cw_dig[i]);
    }
}

/* ovl_ukind - which of Word's underlines an attribute byte wears, as the
 * kul code the file carries. A character can hold more than one of the three
 * bits - Format Character has a box for each - and RTF has one underline, so
 * the writer picks the most distinct: double, then word, then single. */
int ovl_ukind(int a)
{
    if (a & CW_A_DUL)
        return CW_KUL_DOUBLE;
    if (a & CW_A_WUL)
        return CW_KUL_WORD;
    if (a & CW_A_ULINE)
        return CW_KUL_SINGLE;
    return CW_KUL_NONE;
}

/* ovl_emitkw - a control word and a number, e.g. "\li720". */
void ovl_emitkw(const char *kw, int v)
{
    ovl_emit(kw);
    ovl_emitnum(v);
}

/* ovl_emit_pap - the paragraph properties of dictionary entry p, after a
 * \pard so that nothing carries over from the paragraph before. The order
 * and the numbers are tools/os88rtf.py's, so a file it generates and a file
 * this saves are the same shape: an indent is in the ruler's cells of 1/10
 * inch, 144 twips each; one and a half lines is \sl360 and double \sl480;
 * Word's Open Space is 12 points before, \sb240. */
void ovl_emit_pap(int p)
{
    ovl_emit("\\pard");
    if (cw_pap_al[p] == CW_AL_CENTER)
        ovl_emit("\\qc");
    else if (cw_pap_al[p] == CW_AL_RIGHT)
        ovl_emit("\\qr");
    else if (cw_pap_al[p] == CW_AL_JUST)
        ovl_emit("\\qj");
    if (cw_pap_li[p] != 0)
        ovl_emitkw("\\li", cw_pap_li[p] * 144);
    if (cw_pap_fi[p] != 0)
        ovl_emitkw("\\fi", cw_pap_fi[p] * 144);
    if (cw_pap_ri[p] != 0)
        ovl_emitkw("\\ri", cw_pap_ri[p] * 144);
    if (cw_pap_sp[p] == 1)
        ovl_emit("\\sl360");
    else if (cw_pap_sp[p] == 2)
        ovl_emit("\\sl480");
    if (cw_pap_op[p] != 0)
        ovl_emit("\\sb240");
    ovl_emitc(' ');
}

/* ovl_para_pap - the dictionary entry of the paragraph that starts at s: the
 * one its mark carries, or the typing format for a last paragraph with no
 * mark. Each call walks only its own paragraph, so the writer stays one pass. */
int ovl_para_pap(int s)
{
    int j;

    for (j = s; j < cw_len; j++) {
        if (cw_buf[j] == '\n')
            return (cw_att[j] < CW_PAP_MAX) ? cw_att[j] : 0;
    }
    return (cw_pap_now < CW_PAP_MAX) ? cw_pap_now : 0;
}

/* ovl_rtf_build - write the document into cw_rtf.
 *
 * The header is the smallest one a reader can rely on: the version and
 * charset, a one-entry font table (cword has one face - the kernel's 8x8, and
 * saying \fmodern says so honestly), the page geometry from cwrtftbl.c's own
 * defaults, and \pard\plain to start from a known state. Those five numbers
 * are the row defaults in cw_rrb[], read out of the table rather than written
 * again here, so the file cword writes and the file cword reads agree by
 * construction.
 *
 * EVERY FORMAT THE EDITOR CAN SET IS WRITTEN (SPEC.md 73.12.4): the seven
 * character attributes, and each paragraph's alignment, line spacing, space
 * before and three indents - a \pard and the properties wherever the format
 * CHANGES from one paragraph to the next. It used to write bold, italic and
 * one underline only, so a document saved and opened again came back with
 * every paragraph flush left and its word underline, double underline, small
 * caps and hidden text gone.
 *
 * out: the byte count, or -1 if the document does not fit CW_RTF_MAX. The
 *      caller must refuse the save on -1 and say so - a truncated RTF file is
 *      unreadable, where a refusal costs nothing. */
int ovl_rtf_build(void)
{
    int i;
    int a;
    int cur;
    int c;
    int k;
    int pcur;
    int p;

    cw_rtfn = 0;
    cw_ovf  = 0;

    ovl_emit("{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\fmodern os8088 8x8;}}\r\n");

    ovl_emitkw("\\paperw", ovl_rrb_w(CW_IRRB_PAPERW));
    ovl_emitkw("\\paperh", ovl_rrb_w(CW_IRRB_PAPERH));
    ovl_emitkw("\\margl", ovl_rrb_w(CW_IRRB_MARGL));
    ovl_emitkw("\\margr", ovl_rrb_w(CW_IRRB_MARGR));
    ovl_emitkw("\\margt", ovl_rrb_w(CW_IRRB_MARGT));
    ovl_emitkw("\\margb", ovl_rrb_w(CW_IRRB_MARGB));
    ovl_emitkw("\\deftab", ovl_rrb_w(CW_IRRB_DEFTAB));
    ovl_emitkw("\r\n\\pard\\plain\\f0\\fs", ovl_rrb_w(CW_IRRB_FS));
    ovl_emit("\r\n");

    cur = 0;
    pcur = 0;                           /* entry 0 is what \pard means */
    for (i = 0; i <= cw_len; i++) {
        /* A PARAGRAPH STARTS HERE: say its format if it is not the one in
         * force. i == cw_len is the empty paragraph after a final mark, and
         * the empty document - whose format is still the typing one. */
        if (i == 0 || cw_buf[i - 1] == '\n') {
            p = ovl_para_pap(i);
            if (p != pcur) {
                ovl_emit_pap(p);
                pcur = p;
            }
        }
        if (i == cw_len)
            break;

        /* A PARAGRAPH MARK'S ATTRIBUTE BYTE IS NOT CHARACTER FORMATTING. It
         * is an index into the paragraph-format dictionary (apps/cword/cword.c
         * - Word's own arrangement, where paragraph properties live on the
         * mark), so reading CHP bits out of it would emit \b and \i from a
         * number that means "centred, indented five". The mark takes the
         * formatting in force, which is also what makes the run either side of
         * it come out as one run rather than three.
         *
         * Found by writing a centred paragraph and reading the file: the
         * dictionary index was 1, bit 0 is bold, and every centred paragraph
         * in the document ended with a spurious \b. */
        a = (cw_buf[i] == '\n') ? cur : (cw_att[i] & CW_A_ALL);

        if ((a & CW_A_BOLD) != (cur & CW_A_BOLD))
            ovl_emit((a & CW_A_BOLD) ? "\\b " : "\\b0 ");
        if ((a & CW_A_ITAL) != (cur & CW_A_ITAL))
            ovl_emit((a & CW_A_ITAL) ? "\\i " : "\\i0 ");
        k = ovl_ukind(a);
        if (k != ovl_ukind(cur)) {
            if (k == CW_KUL_DOUBLE)
                ovl_emit("\\uldb ");
            else if (k == CW_KUL_WORD)
                ovl_emit("\\ulw ");
            else if (k == CW_KUL_SINGLE)
                ovl_emit("\\ul ");
            else
                ovl_emit("\\ulnone ");
        }
        if ((a & CW_A_SCAP) != (cur & CW_A_SCAP))
            ovl_emit((a & CW_A_SCAP) ? "\\scaps " : "\\scaps0 ");
        if ((a & CW_A_HID) != (cur & CW_A_HID))
            ovl_emit((a & CW_A_HID) ? "\\v " : "\\v0 ");
        cur = a;

        c = (unsigned char)cw_buf[i];
        if (c == '\n') {
            ovl_emit("\\par\r\n");
            continue;
        }
        if (c == '\\' || c == '{' || c == '}')
            ovl_emitc('\\');
        ovl_emitc(c);
    }

    /* Close what is open, then the document group. Turning the attributes off
     * explicitly is not needed - the '}' ends their scope - but a reader that
     * concatenates files is a real thing, and the keywords cost a few bytes. */
    if (cur & CW_A_BOLD)
        ovl_emit("\\b0 ");
    if (cur & CW_A_ITAL)
        ovl_emit("\\i0 ");
    if (ovl_ukind(cur) != CW_KUL_NONE)
        ovl_emit("\\ulnone ");
    if (cur & CW_A_SCAP)
        ovl_emit("\\scaps0 ");
    if (cur & CW_A_HID)
        ovl_emit("\\v0 ");
    ovl_emit("\r\n}\r\n");

    if (cw_ovf)
        return -1;
    return cw_rtfn;
}
