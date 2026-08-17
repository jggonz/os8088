/* ============================================================================
 * os8088 - apps/cword/cwrtfchk.c        the build-time gate over the RTF tables
 *
 * ---------------------------------------------------------------------------
 * ATTRIBUTION
 * ---------------------------------------------------------------------------
 * This file contains no Microsoft material itself, but it exists to check
 * apps/cwrtftbl.c and apps/cwrtftbl.h, which are derived from the source code
 * of MICROSOFT WORD FOR WINDOWS 1.1a ("Opus"):
 *
 *     Copyright (C) Microsoft Corporation.
 *     Source: Microsoft Word for Windows 1.1a source code, Computer History
 *             Museum release, 2014, directory `Opus/`.
 *
 * The idea of a routine that checks the tables is Opus's own. RTF.H:170-172:
 *
 *     "these defines are used in CkDefinesRtf(), a test routine that assures
 *      that the structures referenced do not change and invalidate the values.
 *      Please update that routine (in debug.c) when you add defines."
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS IS AND WHY IT RUNS ON THE HOST
 * ---------------------------------------------------------------------------
 * THIS FILE IS NOT PART OF THE PACKAGE AND IS NEVER COMPILED FOR THE 8086. It
 * is an ordinary host program, built with the host's cc and run during the
 * build. It exits 0 when the tables are sound and nonzero, with a line saying
 * which check and which row, when they are not.
 *
 *     cc -o build/cwrtfchk apps/cword/cwrtfchk.c -I apps/cword && build/cwrtfchk
 *
 * It compiles clean under `-std=c89 -Wall -Wextra -pedantic`, and it needs
 * nothing from this tree but the two table files: cwrtftbl.h and cwrtftbl.c do
 * not include apps/cc/os88.h and do not call the OS. That is deliberate - a
 * table that can only be compiled by the cross toolchain is a table that can
 * only be checked by booting.
 *
 * THE ONE MAKEFILE LINE THIS WANTS, for whoever owns the cword build rules -
 * hang it off the package's .bin so the gate runs before anything is packed,
 * and note that the .bin depends on the checker having passed, not merely on
 * the checker existing:
 *
 *     $(BUILD)/cwrtfchk: apps/cword/cwrtfchk.c apps/cword/cwrtftbl.c \
 *                        apps/cword/cwrtftbl.h | $(BUILD)
 *             $(HOSTCC) -o $@ apps/cword/cwrtfchk.c -I apps/cword
 *
 *     $(BUILD)/cword.raw.asm: $(BUILD)/cwrtfchk
 *             ./$(BUILD)/cwrtfchk
 *             ...the smlrcc line...
 *
 * The tables' self-check is in two halves and this is the second one:
 *
 *   COMPILE TIME, IN THE TARGET BUILD - the CW_CTASSERTs in cwrtftbl.h and
 *       cwrtftbl.c. Every flag mask against its bit index and against the
 *       literal Opus/wordtech/props.h publishes for the 8086; every word-sized
 *       field at an even offset; every field inside its record; both reset
 *       ranges ordered; both table lengths against the rows actually present;
 *       both row sizes. They cost the package nothing (a typedef reserves no
 *       storage) and they cannot be skipped, because they ARE the build. All
 *       verified to fail when made false, not merely to pass.
 *
 *   HERE - the things a constant expression cannot see: that the 76 names are
 *       strictly SORTED (a binary search over a mis-sorted table does not
 *       fail, it answers plausibly), that every argument indexes the table its
 *       rac says it does, that every offset lands inside the record its pgc
 *       names, that a SECOND, independent transcription of 26 keyword rows and
 *       12 property rows from Opus finds exactly what it expects, and that 26
 *       keywords which must NOT be in the table are not.
 *
 * WHY THE HOST AND NOT THE TARGET. The checks are all of manifest constants
 * and table contents, so they mean the same thing on a 64-bit host as on an
 * 8086 - and on the host they are free, instant, and able to stop a build.
 * Running them on the target would cost image bytes against a 60 KB ceiling
 * (SPEC.md 67.9) and milliseconds at every launch, to discover at run time, in
 * front of a user, something that was already true when the file was written.
 *
 * WHAT IT CANNOT CHECK, said plainly: that a value is what Opus has. Nothing
 * mechanical can - there is no machine-readable Opus. The expectation table in
 * cwrtftbl.c is a second human transcription made from the Opus source rather
 * than from the table, which catches a copying error but not a misreading made
 * twice. Its rows carry the Opus line they came from so a third reader can
 * settle it.
 * ==========================================================================*/

#define CW_RTF_SELFCHECK 1      /* compiles cw_rtf_selfcheck() into the tables */

#include <stdio.h>
#include <string.h>

#include "cwrtftbl.h"
#include "cwrtftbl.c"

/* The failure codes cw_rtf_selfcheck() returns, spelled out. It answers
 * check * 1000 + row, because a number is all a function with no output can
 * carry and the row is the half you need. */
static const char *cw_chk_name(int check)
{
    switch (check) {
    case  1: return "the keyword names are not strictly sorted";
    case  2: return "a keyword's rac is out of range";
    case  3: return "a keyword's arg does not index the table its rac names";
    case  4: return "a keyword does not find its own row";
    case  5: return "a property row's rrba is out of range";
    case  6: return "a property row names a pgc cword has no record for";
    case  7: return "a property row's offset is outside its record";
    case  8: return "a bit row's bit or word index is outside its record";
    case  9: return "a word-sized property row is at an odd offset";
    case 10: return "a keyword row is not what the Opus transcription expects";
    case 11: return "a property row is not what the Opus transcription expects";
    case 12: return "a keyword that must not be in the table was found";
    default: return "unknown check";
    }
}

/* What the tables cost, computed from the tables themselves rather than
 * quoted. These are TARGET bytes - six per keyword row, four per property row,
 * one per name character plus its NUL - not the host's, and they are the
 * numbers to watch against APP_MAX_SIZE (61,440 for image and bss together,
 * SPEC.md 67.9). The code that goes with them is measured by assembling the
 * package; see the note at the foot of cwrtftbl.c. */
static void cw_report_size(void)
{
    int i;
    int cbNames;
    int cbSym;
    int cbRrb;
    int cchLongest;
    const char *szLongest;

    cbNames    = 0;
    cchLongest = 0;
    szLongest  = "";

    for (i = 0; i < CW_RTF_NAMES; i++) {
        int cch = (int)strlen(cw_rsym[i].sz);
        cbNames += cch + 1;
        if (cch > cchLongest) {
            cchLongest = cch;
            szLongest  = cw_rsym[i].sz;
        }
    }

    cbSym = CW_RTF_NAMES * 6;       /* sz, val, racf, arg on a 16-bit target */
    cbRrb = CW_IRRB_MAX  * 4;       /* ap, b, w                              */

    printf("cwrtfchk: %d keywords, %d property rows\n",
           CW_RTF_NAMES, CW_IRRB_MAX);
    printf("cwrtfchk: target bytes  cw_rsym[] %4d  names %4d  cw_rrb[] %4d"
           "  = %d data bytes\n",
           cbSym, cbNames, cbRrb, cbSym + cbNames + cbRrb);
    printf("cwrtfchk: %d bytes per keyword average; longest name \"%s\" (%d)\n",
           (cbSym + cbNames) / CW_RTF_NAMES, szLongest, cchLongest);
    printf("cwrtfchk: property records  CHP %d  PAP %d  DOP %d  RIS %d bytes\n",
           CW_CB_CHP, CW_CB_PAP, CW_CB_DOP, CW_CB_RIS);
}

int main(void)
{
    int rc;

    rc = cw_rtf_selfcheck();

    if (rc != 0) {
        int check = rc / 1000;
        int row   = rc % 1000;

        fprintf(stderr,
                "apps/cword/cwrtftbl.c: error: RTF table self-check %d failed "
                "at row %d: %s\n", check, row, cw_chk_name(check));

        /* WHICH TABLE `row` INDEXES DEPENDS ON THE CHECK, and saying the wrong
         * one is worse than saying nothing: a diagnostic that names an
         * innocent keyword sends the reader to the wrong line. Checks 1-4 walk
         * cw_rsym, 5-9 walk cw_rrb, and 10-12 walk the three expectation
         * tables. */
        if (check == 1 && row > 0 && row < CW_RTF_NAMES)
            fprintf(stderr,
                    "  \"%s\" must sort after \"%s\" and does not. The table is "
                    "searched\n"
                    "  by an UNSIGNED byte compare, so the order is plain "
                    "ASCII with a\n"
                    "  shorter name before a longer one that starts with it "
                    "(\"f\" < \"fi\").\n",
                    cw_rsym[row].sz, cw_rsym[row - 1].sz);
        else if ((check == 2 || check == 3 || check == 4)
                 && row < CW_RTF_NAMES)
            fprintf(stderr, "  cw_rsym[%d] is \"%s\"\n", row, cw_rsym[row].sz);
        else if (check >= 5 && check <= 9 && row < CW_IRRB_MAX)
            fprintf(stderr,
                    "  cw_rrb[%d]: rrba %d, pgc %d, b %d, w %d\n",
                    row, ovl_rrb_rrba(row), ovl_rrb_pgc(row),
                    ovl_rrb_b(row), ovl_rrb_w(row));
        else if (check == 10 && row < (int)CW_NXSYM)
            fprintf(stderr,
                    "  the expectation is \"%s\" = val %d, rac %d, passval %d,"
                    " arg %d\n",
                    cw_xsym[row].sz, cw_xsym[row].val, cw_xsym[row].rac,
                    cw_xsym[row].passval, cw_xsym[row].arg);
        else if (check == 11 && row < (int)CW_NXRRB)
            fprintf(stderr,
                    "  the expectation is cw_rrb[%d] = rrba %d, pgc %d, b %d,"
                    " w %d\n",
                    cw_xrrb[row].irrb, cw_xrrb[row].rrba, cw_xrrb[row].pgc,
                    cw_xrrb[row].b, cw_xrrb[row].w);
        else if (check == 12 && row < (int)CW_NXABSENT)
            fprintf(stderr,
                    "  \"%s\" is in the must-not-be-present list. Either it was"
                    " added to the\n"
                    "  keyword table on purpose - in which case take it out of"
                    " cw_xabsent[] and\n"
                    "  say why in the frozen-set note - or the table is"
                    " mis-sorted and the search\n"
                    "  answered plausibly rather than not at all.\n",
                    cw_xabsent[row]);

        fprintf(stderr,
                "  The tables come from Microsoft Word for Windows 1.1a "
                "(Opus); every row\n"
                "  names the Opus file and line it was taken from. Check it "
                "against that.\n");
        return 1;
    }

    printf("cwrtfchk: RTF tables OK - order, ranges, offsets, and the Opus "
           "expectation rows\n");
    cw_report_size();
    return 0;
}
