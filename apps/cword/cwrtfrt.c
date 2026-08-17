/* ============================================================================
 * os8088 - apps/cword/cwrtfrt.c        the RTF engine's host test
 *
 * A BUILD-HOST PROGRAM. It never runs on the 8086, it is not in the package,
 * and nothing under apps/cword/ links it: apps/cword/build.sh compiles and
 * runs it with the host cc BEFORE the target build starts, and a failure
 * stops the build with the number of the check that failed.
 *
 *     cc -o build/cwrtfrt apps/cword/cwrtfrt.c && build/cwrtfrt
 *
 * WHY IT CAN EXIST AT ALL. apps/cword/cwrtfio.c - the document model, the RTF
 * reader and the RTF writer - calls no operating system: no os88_* function,
 * no drawing, no file I/O. That was the point of splitting it out of the UI.
 * So the whole of the interesting half of this program is ordinary portable C
 * that a Mac can run, and a defect in the reader or the writer is a failed
 * build here rather than a corrupted document found by hand in an emulator.
 *
 * WHAT IT CANNOT SEE, stated so nobody reads more into a pass than is there:
 * `int` is four bytes on the host and two on the target, so this proves the
 * LOGIC and not the arithmetic widths. The widths are pinned separately -
 * cwrtftbl.c's CW_CTASSERT 72/73 assert the two table row sizes in the target
 * build, and cwrtfio.c reads and writes every property word one byte at a
 * time for exactly this reason, so the host runs the same arithmetic the 8088
 * will.
 *
 * THE CHECKS, in order. Each prints its own name on failure and returns its
 * own number, so a build log says which one.
 *
 *   1  round trip     a document with every construct the editor can make -
 *                     three attributes in overlapping runs, the three
 *                     characters RTF has to escape, empty paragraphs - is
 *                     written, read back, and compared CHARACTER AND
 *                     ATTRIBUTE for character and attribute
 *   2  a real file    a hand-written RTF fixture that exercises the paths the
 *                     round trip cannot reach, because cword's own writer
 *                     never emits them: \info and \fonttbl text that must NOT
 *                     appear in the document, {\*\unknown} that must be
 *                     discarded whole, an unknown control word that must be
 *                     ignored WITHOUT discarding its group, \'41, \~ \_ \-,
 *                     \line, \tab, \bin, and group nesting that restores
 *                     formatting on the way out
 *   3  refusals       a file that is not RTF leaves the document untouched;
 *                     a document too big to write is refused rather than
 *                     truncated; text past CW_DOC_MAX raises cw_trunc
 *   4  properties     ovl_apply() against each rrba: a bit set and cleared, a
 *                     byte, a word, a flag, and the two resets - including
 *                     the one that catches the mistake cwrtfio.c warns about,
 *                     that \plain must NOT come out underlined
 * ==========================================================================*/

#include <stdio.h>
#include <string.h>

/* Defined for one reason: cwrtftbl.c's CW_CTASSERT 72 and 73 assert that a
 * table row is 6 and 4 bytes, which is true of the target and false of a
 * 64-bit host, and that #ifndef is how the file says "skip the two assertions
 * that are about the machine". Nothing here calls cw_rtf_selfcheck(); that is
 * apps/cword/cwrtfchk.c's job and build.sh runs it first. */
#define CW_RTF_SELFCHECK 1

#include "cwrtftbl.h"
#include "cwrtftbl.c"
#include "cwrtfio.c"

static int fails;

static void fail(const char *what)
{
    printf("cwrtfrt: FAIL %s\n", what);
    fails++;
}

/* --- a document built by hand, the way the editor would build one --------- */

static const char *rt_text =
    "The quick brown fox.\n"
    "\n"
    "Braces {like this} and a backslash \\ and a percent %.\n"
    "Bold and italic and underline, overlapping.\n";

static void build_doc(void)
{
    int i;
    int a;

    cw_doc_clear();
    for (i = 0; rt_text[i] != 0; i++) {
        /* Three overlapping attribute runs, chosen so that the writer has to
         * emit every one of the six toggles and has to emit them in a
         * changing order. */
        a = 0;
        if (i >= 4 && i < 30)
            a |= CW_A_BOLD;
        if (i >= 20 && i < 50)
            a |= CW_A_ITAL;
        if (i >= 40 && i < 44)
            a |= CW_A_ULINE;
        ovl_put((unsigned char)rt_text[i], a);
    }
}

static int check_roundtrip(void)
{
    static char  keep[CW_DOC_MAX];
    static unsigned char keepa[CW_DOC_MAX];
    int keepn;
    int n;
    int i;
    int bad;

    build_doc();
    keepn = cw_len;
    memcpy(keep, cw_buf, keepn);
    memcpy(keepa, cw_att, keepn);

    n = ovl_rtf_build();
    if (n < 0) {
        fail("1 round trip: the writer overflowed on a 150-character document");
        return 1;
    }

    cw_doc_clear();
    if (ovl_rtf_parse(n) != CW_RTF_OK) {
        fail("1 round trip: the reader refused cword's own output");
        return 1;
    }

    if (cw_len != keepn) {
        printf("cwrtfrt: round trip length %d, expected %d\n", cw_len, keepn);
        fail("1 round trip: length");
        return 1;
    }
    bad = 0;
    for (i = 0; i < keepn; i++) {
        if (cw_buf[i] != keep[i]) {
            printf("cwrtfrt: char %d is '%c', expected '%c'\n",
                   i, cw_buf[i], keep[i]);
            bad = 1;
            break;
        }
        if (cw_att[i] != keepa[i]) {
            printf("cwrtfrt: attr %d is %d, expected %d ('%c')\n",
                   i, cw_att[i], keepa[i], keep[i]);
            bad = 1;
            break;
        }
    }
    if (bad) {
        fail("1 round trip: content");
        return 1;
    }
    printf("cwrtfrt: ok 1 round trip      %d chars -> %d bytes of RTF -> %d chars\n",
           keepn, n, cw_len);
    return 0;
}

/* --- a file cword did not write ------------------------------------------- */

static const char *rt_file =
    "{\\rtf1\\ansi\\deff0"
    "{\\fonttbl{\\f0\\froman Tms Rmn;}{\\f1\\fswiss Helv;}}"
    "{\\colortbl\\red0\\green0\\blue0;\\red255\\green0\\blue0;}"
    "{\\info{\\author Somebody}{\\title Not the document text}}"
    "{\\stylesheet{\\s0 Normal;}}"
    "\\paperw11907\\paperh16840\\margl1701\\margr1701\r\n"
    "\\pard\\plain\\fs24 Plain. "
    "{\\b Bold {\\i both} back to bold}"
    " plain again.\\par\r\n"
    "{\\*\\unknowndest this must vanish}"
    "\\unknownword kept.\\par\r\n"
    "Escapes: \\{ \\} \\\\ \\'41 \\~ \\_ \\- end.\\par\r\n"
    "\\ul under\\ulnone off.\\line next.\\par\r\n"
    "Tab:\\tab done.\\par\r\n"
    "{\\pict\\wbitmap0 \\bin4 {}{} tail of the pict group}"
    "Last.\\par\r\n"
    "}";

/* What the reader must produce from it, character for character. Written out
 * rather than computed, because a computed expectation is the same mistake
 * made twice. */
static const char *rt_want =
    "Plain. Bold both back to bold plain again.\n"
    "kept.\n"
    /* \'41 is 'A'; the space after it is text. \~ is a non-breaking space and
     * becomes an ordinary one; \_ is a non-breaking hyphen and becomes '-';
     * \- is an OPTIONAL hyphen and prints nothing at all. A space after a
     * control SYMBOL is text, where a space after a control WORD is the
     * delimiter and disappears - which is why this line has the run of
     * spaces it has, and why the next line has none between "under" and
     * "off." even though the file does. Getting that rule backwards is how a
     * reader loses every space in a document. */
    "Escapes: { } \\ A   -  end.\n"
    "underoff.\nnext.\n"
    "Tab: done.\n"
    "Last.\n";

static int check_file(void)
{
    int n;
    int i;
    int want;
    int got;

    n = (int)strlen(rt_file);
    memcpy(cw_rtf, rt_file, n);
    if (ovl_rtf_parse(n) != CW_RTF_OK) {
        fail("2 fixture: the reader refused it");
        return 2;
    }

    want = (int)strlen(rt_want);
    if (cw_len != want) {
        cw_buf[cw_len] = 0;
        printf("cwrtfrt: got  [%s]\ncwrtfrt: want [%s]\n", cw_buf, rt_want);
        fail("2 fixture: length");
        return 2;
    }
    for (i = 0; i < want; i++) {
        if (cw_buf[i] != rt_want[i]) {
            cw_buf[cw_len] = 0;
            printf("cwrtfrt: got  [%s]\ncwrtfrt: want [%s]\n", cw_buf, rt_want);
            printf("cwrtfrt: first difference at %d\n", i);
            fail("2 fixture: content");
            return 2;
        }
    }

    /* The formatting the groups implied. "Bold both back to bold" starts at 7
     * and runs 22 characters; "both" inside it is also italic; everything
     * after the group's '}' is plain again - which is the group stack doing
     * its job, and the one thing a flat reader gets wrong. */
    for (i = 0; i < want; i++) {
        got = cw_att[i];
        if (i >= 7 && i < 29) {
            if (!(got & CW_A_BOLD)) {
                printf("cwrtfrt: char %d ('%c') is not bold\n", i, cw_buf[i]);
                fail("2 fixture: bold run");
                return 2;
            }
        } else if (got & CW_A_BOLD) {
            printf("cwrtfrt: char %d ('%c') is bold and should not be\n",
                   i, cw_buf[i]);
            fail("2 fixture: bold leaked out of its group");
            return 2;
        }
        if (i >= 12 && i < 16) {
            if (!(got & CW_A_ITAL)) {
                printf("cwrtfrt: char %d ('%c') is not italic\n", i, cw_buf[i]);
                fail("2 fixture: italic run");
                return 2;
            }
        } else if (got & CW_A_ITAL) {
            printf("cwrtfrt: char %d ('%c') is italic and should not be\n",
                   i, cw_buf[i]);
            fail("2 fixture: italic leaked out of its group");
            return 2;
        }
    }

    /* \ul ... \ulnone, which is not a group: it must underline exactly the
     * five characters of "under". */
    {
        const char *p = strstr(rt_want, "under");
        int at = (int)(p - rt_want);
        for (i = 0; i < 5; i++) {
            if (!(cw_att[at + i] & CW_A_ULINE)) {
                fail("2 fixture: \\ul run");
                return 2;
            }
        }
        if (cw_att[at + 5] & CW_A_ULINE) {
            fail("2 fixture: \\ulnone did not stop the underline");
            return 2;
        }
    }

    printf("cwrtfrt: ok 2 fixture         %d bytes -> %d chars, "
           "destinations discarded\n", n, cw_len);
    return 0;
}

/* --- the refusals --------------------------------------------------------- */

static int check_refusals(void)
{
    int i;
    int n;
    int rc;

    /* Not RTF: the document must survive it. */
    build_doc();
    n = cw_len;
    memcpy(cw_rtf, "Once upon a time, in a plain text file.", 39);
    rc = ovl_rtf_parse(39);
    if (rc != CW_RTF_NOTRTF) {
        fail("3 refusals: a plain text file was accepted as RTF");
        return 3;
    }
    if (cw_len != n) {
        fail("3 refusals: a refused open emptied the document");
        return 3;
    }

    /* Too much text: ovl_put refuses and says so. */
    cw_doc_clear();
    for (i = 0; i < CW_DOC_MAX + 100; i++)
        ovl_put('x', 0);
    if (cw_len != CW_DOC_MAX) {
        fail("3 refusals: ovl_put ran past CW_DOC_MAX");
        return 3;
    }
    if (!cw_trunc) {
        fail("3 refusals: ovl_put overran without raising cw_trunc");
        return 3;
    }

    /* A document whose RTF cannot fit: refused, not truncated. Every
     * character alternating bold is the worst case the writer has - eight
     * bytes of control word for one byte of text. */
    cw_doc_clear();
    for (i = 0; i < CW_DOC_MAX; i++)
        ovl_put('x', (i & 1) ? CW_A_BOLD : 0);
    if (ovl_rtf_build() != -1) {
        fail("3 refusals: an over-long document was written anyway");
        return 3;
    }

    /* ...and the ordinary case still fits, or the cap is useless. */
    cw_doc_clear();
    for (i = 0; i < CW_DOC_MAX; i++)
        ovl_put((i % 64) == 63 ? '\n' : 'x', 0);
    n = ovl_rtf_build();
    if (n < 0) {
        fail("3 refusals: a plain full document does not fit CW_RTF_MAX");
        return 3;
    }
    printf("cwrtfrt: ok 3 refusals        a full plain document is %d "
           "of %d RTF bytes\n", n, CW_RTF_MAX);
    return 0;
}

/* --- the property machinery ----------------------------------------------- */

static int check_props(void)
{
    ovl_pard();
    ovl_plain();

    /* rrbaBit, the row class Opus stamps a seven-fold warning over. */
    ovl_apply(CW_IRRB_B, 0, 0);
    if (!(ovl_pw_get(cw_chp, CW_CHP_O_GRPF) & CW_CHP_F_BOLD)) {
        fail("4 properties: \\b did not set the bold bit");
        return 4;
    }
    if (ovl_pw_get(cw_chp, CW_CHP_O_GRPF) & CW_CHP_F_ITALIC) {
        fail("4 properties: \\b set the italic bit as well");
        return 4;
    }
    ovl_apply(CW_IRRB_B, 0, 1);          /* \b0 */
    if (ovl_pw_get(cw_chp, CW_CHP_O_GRPF) & CW_CHP_F_BOLD) {
        fail("4 properties: \\b0 did not clear the bold bit");
        return 4;
    }

    /* rrbaByte with a number, and with none. */
    ovl_apply(CW_IRRB_FS, 28, 1);
    if (cw_chp[CW_CHP_O_HPS] != 28) {
        fail("4 properties: \\fs28");
        return 4;
    }
    ovl_apply(CW_IRRB_FS, 0, 0);
    if (cw_chp[CW_CHP_O_HPS] != 24) {
        fail("4 properties: a bare \\fs is not the row's 24");
        return 4;
    }

    /* rrbaWord, signed. */
    ovl_apply(CW_IRRB_FI, -360, 1);
    if (ovl_pw_get(cw_pap, CW_PAP_O_DXALEFT1) != 0xFE98) {
        printf("cwrtfrt: \\fi-360 stored %04X\n",
               ovl_pw_get(cw_pap, CW_PAP_O_DXALEFT1));
        fail("4 properties: \\fi-360 (a negative twip count)");
        return 4;
    }

    /* rrbaFlag: presence sets, \keep0 clears. */
    ovl_apply(CW_IRRB_KEEP, 0, 0);
    if (cw_pap[CW_PAP_O_FKEEP] != 1) {
        fail("4 properties: \\keep");
        return 4;
    }
    ovl_apply(CW_IRRB_KEEP, 0, 1);
    if (cw_pap[CW_PAP_O_FKEEP] != 0) {
        fail("4 properties: \\keep0");
        return 4;
    }

    /* rrbaSpec: a tab stop, with the pending justification the previous
     * keyword left. */
    ovl_apply(CW_IRRB_TJC, CW_JC_CENTER, 1);
    ovl_apply(CW_IRRB_TX, 1440, 1);
    if (cw_pap[CW_PAP_O_ITBDMAC] != 1 ||
        ovl_pw_get(cw_pap, CW_PAP_O_RGDXATAB) != 1440 ||
        CW_TBD_JC(cw_pap[CW_PAP_O_RGTBD]) != CW_JC_CENTER) {
        fail("4 properties: \\tqc\\tx1440");
        return 4;
    }

    /* THE ONE THAT CATCHES THE OBVIOUS IMPLEMENTATION. \plain must not leave
     * the text underlined - and it would, if the reset walked cw_rrb[] and
     * stored each row's `w`, because the \ul row's w is kulSingle. */
    ovl_apply(CW_IRRB_KUL, 0, 0);        /* a bare \ul */
    if (cw_chp[CW_CHP_O_KUL] != CW_KUL_SINGLE) {
        fail("4 properties: a bare \\ul is not kulSingle");
        return 4;
    }
    ovl_apply(CW_IRRB_PLAIN, 0, 0);
    if (cw_chp[CW_CHP_O_KUL] != CW_KUL_NONE) {
        fail("4 properties: \\plain left the text underlined");
        return 4;
    }
    if (cw_chp[CW_CHP_O_HPS] != 24) {
        fail("4 properties: \\plain did not restore the default size");
        return 4;
    }

    /* \pard clears the paragraph, tab stops and all. */
    ovl_apply(CW_IRRB_PARD, 0, 0);
    if (cw_pap[CW_PAP_O_ITBDMAC] != 0 ||
        ovl_pw_get(cw_pap, CW_PAP_O_DXALEFT1) != 0) {
        fail("4 properties: \\pard");
        return 4;
    }

    /* A property aimed at a record cword does not keep must be dropped, not
     * written somewhere. There is no CW_IRRB_* for one, so this checks the
     * gate itself. */
    if (ovl_record(CW_PGC_TABLE) != 0 || ovl_record(CW_PGC_BRC) != 0) {
        fail("4 properties: a record cword does not keep answered a pointer");
        return 4;
    }

    printf("cwrtfrt: ok 4 properties      bit, byte, word, flag, spec, "
           "\\pard and \\plain\n");
    return 0;
}

int main(void)
{
    int rc;

    rc = check_roundtrip();
    if (rc == 0)
        rc = check_file();
    if (rc == 0)
        rc = check_refusals();
    if (rc == 0)
        rc = check_props();

    if (rc != 0 || fails != 0) {
        printf("cwrtfrt: FAILED (check %d)\n", rc);
        return rc ? rc : 1;
    }
    printf("cwrtfrt: all checks passed\n");
    return 0;
}
