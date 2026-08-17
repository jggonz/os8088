/* ============================================================================
 * os8088 - apps/cword/cwrtftbl.h        the RTF keyword and property tables
 *
 * ---------------------------------------------------------------------------
 * ATTRIBUTION - THIS FILE IS DERIVED FROM MICROSOFT SOURCE CODE
 * ---------------------------------------------------------------------------
 * The tables below are transcribed from the source code of MICROSOFT WORD FOR
 * WINDOWS 1.1a ("Opus"), released by the COMPUTER HISTORY MUSEUM in March 2014
 * as a historical artefact.
 *
 *     Copyright (C) Microsoft Corporation.
 *     Source: Microsoft Word for Windows 1.1a source code, Computer History
 *             Museum release, 2014 (https://computerhistory.org/), directory
 *             `Opus/`.
 *
 * Every file header in that release reads "COPYRIGHT (C) 1987 MICROSOFT" and
 * the release carries NO licence grant. Microsoft remains the copyright holder
 * of the material these tables are derived from. This file exists because the
 * repository owner made that call explicitly and in full knowledge of it.
 *
 * WHICH OPUS FILE EACH THING CAME FROM - check any value against these:
 *
 *   Opus/RTFTBL.H:169-467   rgszRtfSym[]     the sorted keyword name list
 *   Opus/RTFTBL.H:995-1330  rgrsymWord[]     val / fPassVal / rac / arg
 *   Opus/RTFTBL.H:1394-1580 rgrrbWord[]      rrba / pgc / b / w
 *   Opus/RTFTBL.H:86-106    ipfn* codes      the ChngDest / SpecCharAct actions
 *   Opus/RTFTBL.H:31-79     rds* codes       the RTF destinations
 *   Opus/RTF.H:52-75        rac / rrba / pgc codes
 *   Opus/RTF.H:90-97        chRTF* / chBackslash
 *   Opus/RTF.H:141-151      chs* charset and fmc* font-family codes
 *   Opus/RTF.H:176-245      ibit* / iword* bit and word indices
 *   Opus/RTF.H:304-315      struct RSYM     (the shape re-expressed here)
 *   Opus/RTF.H:378-384      struct RRB      (ditto)
 *   Opus/ch.h               chEop, chTab, chSect, chCRJ, chNonReqHyphen,
 *                           chNonBreakHyphen, chNonBreakSpace
 *   Opus/wordtech/props.h   struct CHP, struct PAP, struct SEP, the maskf*
 *                           masks, and jc* / kul* / tlc* / stc* / ico* codes
 *   Opus/wordwin.h:486-540  struct DOP
 *   Opus/lib/qwindows.h:1280-1290  FF_* GDI font families (fmc* resolve here)
 *   Opus/RTFIN.C:790-905    the interpreter loop these tables drive
 *   Opus/RTFIN2.C:20-95     ApplyPropChange - the exact meaning of each rrba
 *   Opus/rtfsubs.c:89-127   C_FSearchRgrsym - the binary search ported below
 *   Opus/fontwin.h:22       ftcDefault
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS IS
 * ---------------------------------------------------------------------------
 * RTF reading, in this program, is a table walk and not a parser: the reader
 * takes the control word it just scanned, looks it up here, and does the ONE
 * thing the table says. That is Opus's design and it is the reason RTF support
 * fits in a package that must also hold an editor. Two tables:
 *
 *   cw_rsym[]  - THE KEYWORD TABLE. 76 entries, sorted by name, searched by
 *                ovl_rtf_lookup(). Each says what kind of thing the keyword is
 *                (its `rac`), what value it carries, and an index into...
 *   cw_rrb[]   - THE PROPERTY-ACTION TABLE. 40 entries, indexed by an
 *                CW_IRRB_* constant. Each says which property record to touch,
 *                where in it, how wide, and what the value is when the
 *                keyword arrives with no number after it.
 *
 * ---------------------------------------------------------------------------
 * THE KEYWORD SET IS FROZEN AT 76 AND THE REST IS AN EXPLICIT IGNORE PATH
 * ---------------------------------------------------------------------------
 * Opus's rgrsymWord has 287 entries and RTFRARE.C is 1,700 further lines of
 * tables, borders, footnotes, sections, columns, index entries and fields. The
 * set here is the one a plain formatted document actually uses: document
 * geometry, paragraph and character formatting, tab stops, the font and colour
 * tables, and the common special characters. EVERYTHING ELSE IS IGNORED, and
 * ignoring it is a designed path rather than an omission - see the contract in
 * "HOW A READER USES THIS" below. Adding a keyword means adding a row to both
 * tables and to the expectation table in cwrtftbl.c; that is deliberately
 * three edits, because a keyword is a feature.
 *
 * Deliberately absent, and why:
 *   tables (\trowd \cellx \cell \row \intbl \clbrdr*)   there is no table
 *                                                       layout engine
 *   borders and shading (\brdr* \box \brsp \bkmk*)      nothing draws them
 *   sections and columns (\sect \sectd \cols \sbk*)     one flow, one column
 *   footnotes/annotations/index/TOC (\footnote \xe \tc) skipped as groups
 *   pictures (\pict \wmetafile \wbitmap \bin)           skipped as a group;
 *                                                       \bin is in the table
 *                                                       ONLY so its byte count
 *                                                       can be consumed, since
 *                                                       raw binary can contain
 *                                                       braces and would
 *                                                       otherwise desynchronise
 *                                                       the brace counter
 *   fields (\field \fldinst \fldrslt)                   ignored, which leaves
 *                                                       the result text - the
 *                                                       behaviour you want
 *   revision marks, outline/small caps (\revised \outl  no renderer for them;
 *   \scaps)                                             the text still reads
 *   \mac and \pca charsets                              read as the default
 *
 * ---------------------------------------------------------------------------
 * WHAT WAS NARROWED, AND WHAT IT COSTS
 * ---------------------------------------------------------------------------
 * Opus's `CP` (character position) and `FC` (file character offset) are
 * `typedef long`. There is no `long` in this toolchain (SPEC.md 67.7), so:
 *
 *   CP -> 16 bits, unsigned.  A DOCUMENT IS CAPPED AT 65,535 CHARACTERS. At
 *         ~2,000 characters a page that is about 32 pages, and it is a hard
 *         cap the reader must enforce and report, not one it may run past:
 *         a wrapped CP addresses the wrong text with no fault.
 *   FC -> not carried at all. Nothing in these tables holds a file offset;
 *         the picture and binary paths that needed one are skipped.
 *   `struct RRB.w` -> `int`, i.e. SIGNED 16 bits, as in Opus. Every default in
 *         the table below fits: the largest is 15,840 twips (yaPage, an 11in
 *         page). A page over 32,767 twips (22.75in) cannot be represented,
 *         where Opus's DOP fields are `uns`. A file asking for one must be
 *         clamped by the reader, and the value is a page size, so clamping is
 *         visible rather than silent.
 *   `struct RSYM.val` -> WIDENED from Opus's `int val:8` to a full `int`.
 *         Opus's 8-bit field stores chNonBreakSpace (160) as -96 and gets away
 *         with it because the value only ever reaches a `char`. A 16-bit field
 *         costs 76 bytes and removes the question.
 *   Tab stops per paragraph -> 12, where Opus's itbdMax is 50. Fifty stops is
 *         150 bytes of PAP; twelve is 36. A paragraph with more loses the
 *         extra stops, which is visible in the ruler.
 *
 * ---------------------------------------------------------------------------
 * BIT POSITIONS: WHY OPUS WARNS, AND WHY THIS FILE CANNOT MAKE THAT MISTAKE
 * ---------------------------------------------------------------------------
 * Opus/RTFTBL.H:1397-1405 carries a seven-fold WARNING over rgrrbWord:
 *
 *     "Watch out for rrbaBit entries. The bit offset is hard coded and may
 *      vary from machine to machine. [...] the compiler can't do it.
 *      Example: bit 15 on the Mac is bit 0 on the 8086!"
 *
 * and props.h stamps "WARNING: Machine-dependent" on every mask. The hazard is
 * real and silent: an rrbaBit row names a bit NUMBER, the property word is a
 * C bit-field, and which bit of the word a bit-field occupies is the
 * compiler's business. Get it wrong and \b sets fVanish - the file loads, the
 * text disappears, and nothing reports anything.
 *
 * THIS FILE REMOVES THE HAZARD RATHER THAN DOCUMENTING IT. There is not one
 * bit-field here (SmallerC has none anyway, SPEC.md 67.7). A property record
 * is a byte array with manifest offsets, and a flag is a manifest MASK. The
 * bit numbers are still Opus's - CW_CHP_BIT_BOLD is 0 because ibitCfBold is 0 -
 * so every row stays checkable against the source, but nothing derives a bit
 * position from a struct layout, so no compiler can disagree with the table.
 *
 * What is left is transcription error, and that is what the self-check is for:
 * every mask is asserted at COMPILE TIME against 1 << its bit index AND against
 * the literal mask props.h publishes for the 8086 build, which are two
 * independent Opus files that have to agree. See cwrtftbl.c for the rest of it.
 *
 * ---------------------------------------------------------------------------
 * HOW A READER USES THIS (the contract, from Opus/RTFIN.C:812-905)
 * ---------------------------------------------------------------------------
 *   i = ovl_rtf_lookup(name);
 *   if (i < 0) {
 *       if (ris.fNextIsDest)  skip the whole group;   <-- \* said so
 *       else                  ignore the control word;
 *   } else {
 *       ris.fNextIsDest = 0;
 *       if (ovl_rsym_passval(i)) { val = ovl_rsym_val(i); have_val = 1; }
 *       switch (ovl_rsym_rac(i)) {
 *       case CW_RAC_CHNGDEST:
 *       case CW_RAC_SPECCHARACT: do action ovl_rsym_arg(i) with val;  break;
 *       case CW_RAC_CHNGPROP:    apply cw_rrb[ovl_rsym_arg(i)];       break;
 *       case CW_RAC_SPECCHAR:    insert the character val;           break;
 *       case CW_RAC_BINPARM:     consume `val` raw bytes;            break;
 *       case CW_RAC_SCANBYDEST:  skip the whole group;               break;
 *       }
 *   }
 *
 * THE TWO SKIP PATHS ARE NOT THE SAME PATH AND BOTH ARE NEEDED. `\*` marks a
 * destination the writer knows a reader may not understand, and an unknown
 * keyword after it means "throw the group away". But \info, \footnote,
 * \header, \footer, \stylesheet and \pict are NOT written with `\*`: they are
 * ordinary destinations whose text would flow straight into the document if
 * they were merely unknown. They are in the table with CW_RAC_SCANBYDEST for
 * exactly that reason, and leaving one out is not a missing feature, it is the
 * summary information appearing in the middle of page one.
 *
 * ---------------------------------------------------------------------------
 * ONE TRANSLATION UNIT (SPEC.md 67.1)
 * ---------------------------------------------------------------------------
 * `nasm -f bin` has no notion of an external symbol, so a C package is exactly
 * one .c file. cwrtftbl.c is therefore #included by the package's single
 * translation unit, ONCE, near the top:
 *
 *       #include "cwrtftbl.h"
 *       ...
 *       #include "cwrtftbl.c"       // the tables; include exactly once
 *
 * This header carries no code and may be included freely.
 *
 * ---------------------------------------------------------------------------
 * WHAT IT COSTS (APP_MAX_SIZE is 61,440 for image + bss together, SPEC.md 67.9)
 * ---------------------------------------------------------------------------
 * Measured, not estimated - see the note at the foot of cwrtftbl.c for the
 * command and the exact figures. The tables are `.data`, so they are file bytes
 * on the floppy AND bytes in the region: they are paid for twice, which is why
 * the keyword set is frozen and the strings are not padded to a fixed width.
 *
 * ovl_rtf_lookup() is a binary search: 7 probes over 76 names, each probe a
 * compare of a few bytes, so a control word costs roughly 700-1,000 8088
 * cycles - about 0.2 ms on the 4.77 MHz target. A 20 KB document holding
 * ~2,000 control words spends ~0.4 s in here, against the ~400 ms EACH that
 * PERFORMANCE.md prices its int 13h calls at. The lookup is not the thing to
 * optimise; the number of disk calls is.
 * ==========================================================================*/

#ifndef CWRTFTBL_H
#define CWRTFTBL_H

/* ---------------------------------------------------------------------------
 * A COMPILE-TIME ASSERTION, WHICH IS THE ONLY KIND THAT CANNOT BE SKIPPED
 * ---------------------------------------------------------------------------
 * There is no static_assert here and no assert.h worth having. An array whose
 * dimension is -1 when the condition is false is: SmallerC's diagnostic is
 *
 *     Error in "apps/cword/cwrtftbl.h" (NNN:MM): Array dimension less than 1
 *
 * which names the line, and a typedef reserves no storage, so a hundred of
 * these cost the package nothing. Verified against the pinned compiler both
 * ways round - it builds when true and refuses when false.
 *
 * Every use needs a distinct name, so they are numbered. Do not renumber them
 * to close a gap; a stable number is what a build log refers to.
 * ------------------------------------------------------------------------- */
#define CW_CTASSERT(n, cond) typedef char cw_ctassert_##n[(cond) ? 1 : -1]

/* ---------------------------------------------------------------------------
 * rac - WHAT KIND OF THING A KEYWORD IS         (Opus/RTF.H:52-58, verbatim)
 * ------------------------------------------------------------------------- */
#define CW_RAC_CHNGDEST     0   /* racChngDest    - enter a destination      */
#define CW_RAC_CHNGPROP     1   /* racChngProp    - arg indexes cw_rrb[]     */
#define CW_RAC_SPECCHAR     2   /* racSpecChar    - insert the character val */
#define CW_RAC_SPECCHARACT  3   /* racSpecCharAct - arg is a CW_IPFN_*       */
#define CW_RAC_BINPARM      4   /* racBinParm     - \bin: val raw bytes next */
#define CW_RAC_IGNORE       5   /* racIgnore      - recognised, does nothing */
#define CW_RAC_SCANBYDEST   6   /* racScanByDest  - discard the whole group  */
#define CW_RAC_MAX          7

/* ---------------------------------------------------------------------------
 * rrba - HOW A PROPERTY IS WRITTEN              (Opus/RTF.H:60-65, verbatim)
 *
 * The meaning of each is Opus/RTFIN2.C:64-91 exactly, with `pprop` the base of
 * the record chosen by `pgc` and `val`/`have_val` from the control word:
 *
 *   FLAG  pprop[b] = have_val ? (val != 0) : 1          a one-byte boolean
 *   BIT   word w of pprop, mask 1 << b, set or cleared by the same test
 *   BYTE  pprop[b]        = have_val ? val : rrb.w
 *   WORD  *(int *)(pprop + b) = have_val ? val : rrb.w  b MUST be even
 *   UNS   as WORD, but the number after the keyword is parsed unsigned
 *   SPEC  the reader special-cases this irrb; b and w mean whatever that
 *         case says they mean, and every one of them is listed in cwrtftbl.c
 *
 * Note the collision that Opus lives with and this file keeps: for rrbaBit,
 * `w` is a WORD INDEX into the record, not a default value. rrbaBit rows never
 * need a default because the absent-parameter case is "set the bit".
 * ------------------------------------------------------------------------- */
#define CW_RRBA_FLAG        0
#define CW_RRBA_BIT         1
#define CW_RRBA_BYTE        2
#define CW_RRBA_WORD        3
#define CW_RRBA_UNS         4
#define CW_RRBA_SPEC        7
#define CW_RRBA_MAX         8

/* ---------------------------------------------------------------------------
 * pgc - WHICH PROPERTY RECORD                   (Opus/RTF.H:67-75, verbatim
 * numbering; cword emits only four of the nine, and the gaps are kept so a row
 * stays comparable with Opus's.)
 * ------------------------------------------------------------------------- */
#define CW_PGC_PARA         0   /* pgcPara  - the CW_PAP byte array          */
#define CW_PGC_CHAR         1   /* pgcChar  - the CW_CHP byte array          */
#define CW_PGC_SECT         2   /* pgcSect  - not used: no section support   */
#define CW_PGC_DOC          3   /* pgcDoc   - the CW_DOP byte array          */
#define CW_PGC_PIC          4   /* pgcPic   - not used: pictures are skipped */
#define CW_PGC_RIS          5   /* pgcRibl  - the reader's own CW_RIS state  */
#define CW_PGC_BRC          6   /* pgcBrc   - not used: no borders           */
#define CW_PGC_TABLE        7   /* pgcTable - not used: no tables            */
#define CW_PGC_DTR          8   /* pgcDtr   - not used: no date/time fields  */
#define CW_PGC_MAX          9

/* ---------------------------------------------------------------------------
 * CW_IPFN_* - the actions a CW_RAC_CHNGDEST or CW_RAC_SPECCHARACT row names.
 * Opus indexes rgpfnRtfActions[] (RTFTBL.H:86-106, 126-137) with 21 of these;
 * cword keeps five. The reader switches on the number - a table of function
 * pointers would cost a relocation this OS does not have (SPEC.md 67.2: there
 * is no relocation of any kind).
 * ------------------------------------------------------------------------- */
#define CW_IPFN_PARAEND     0   /* ipfnDoParaEnd    - end the paragraph      */
#define CW_IPFN_HEXCHAR     1   /* ipfnQuote        - \'hh, two hex digits   */
#define CW_IPFN_TAB         2   /* ipfnTab          - insert a tab stop      */
#define CW_IPFN_RDSRTF      3   /* ipfnRdsRtf       - the \rtf group itself  */
#define CW_IPFN_RDSNEWSTD   4   /* ipfnRdsNewStd    - enter val's destination*/
#define CW_IPFN_MAX         5

/* rds - the destinations cword enters. Opus's numbering (RTFTBL.H:31-45) is
 * kept for the two it keeps; every other destination is either the body or is
 * discarded by CW_RAC_SCANBYDEST, so it needs no code. */
#define CW_RDS_MAIN         0   /* rdsMain     - the document body           */
#define CW_RDS_FONTTBL      5   /* rdsFonttbl  - {\fonttbl ...}              */
#define CW_RDS_COLORTBL     6   /* rdsColortbl - {\colortbl ...}             */
#define CW_RDS_MAX          7

/* ---------------------------------------------------------------------------
 * CHARACTER CODES the tables hand to the reader   (Opus/ch.h, Opus/RTF.H:90-97)
 *
 * chEop is 13 because Opus's WIN build does not define CRLF (ch.h:25-31): one
 * character ends a paragraph. Keeping it at 13 is what makes \par and a bare
 * CR the same thing here as there.
 * ------------------------------------------------------------------------- */
#define CW_CH_TABLE          7  /* chTable          end of a table cell      */
#define CW_CH_TAB            9  /* chTab                                     */
#define CW_CH_CRJ           11  /* chCRJ            \line, a soft return     */
#define CW_CH_SECT          12  /* chSect           \page, a page break      */
#define CW_CH_EOP           13  /* chEop            \par, end of paragraph   */
#define CW_CH_NONBREAKHYPH  30  /* chNonBreakHyphen \_                       */
#define CW_CH_NONREQHYPH    31  /* chNonReqHyphen   \-, an optional hyphen   */
#define CW_CH_BACKSLASH     92  /* chBackslash      \\                       */
#define CW_CH_OPENBRK      123  /* chRTFOpenBrk     \{                       */
#define CW_CH_CLOSEBRK     125  /* chRTFCloseBrk    \}                       */
#define CW_CH_NONBREAKSP   160  /* chNonBreakSpace  \~                       */

/* charset codes (Opus/RTF.H:141-144). \ansi and \pc are in the table; \mac and
 * \pca are not, and read as the default. */
#define CW_CHS_MAC           0
#define CW_CHS_PC            1
#define CW_CHS_ANSI          2
#define CW_CHS_PCA           3

/* font family codes (Opus/RTF.H:146-151 -> Opus/lib/qwindows.h:1280-1289).
 * These are Windows GDI FF_* values, which are (family << 4). The numbers are
 * meaningless to this program - it has one bitmap font - but they are kept
 * exactly so a row stays checkable, and so \fdecor and \ftech (absent from the
 * keyword set) would slot in without renumbering. */
#define CW_FMC_NIL        0x00  /* FF_DONTCARE   */
#define CW_FMC_ROMAN      0x10  /* FF_ROMAN      */
#define CW_FMC_SWISS      0x20  /* FF_SWISS      */
#define CW_FMC_MODERN     0x30  /* FF_MODERN     */
#define CW_FMC_SCRIPT     0x40  /* FF_SCRIPT     */
#define CW_FMC_DECOR      0x50  /* FF_DECORATIVE */

/* justification (props.h:349-356), underline (props.h:119-124), tab leader
 * (props.h:384-389), style (props.h:392, 475), colour index (props.h:137-148),
 * default font (fontwin.h:22). jcDecimal and jcBoth are both 3 in Opus: the
 * same field means "justified" in a paragraph and "align on the point" in a
 * tab stop, and the two never share a record. */
#define CW_JC_LEFT           0
#define CW_JC_CENTER         1
#define CW_JC_RIGHT          2
#define CW_JC_BOTH           3
#define CW_JC_DECIMAL        3

#define CW_KUL_NONE          0
#define CW_KUL_SINGLE        1
#define CW_KUL_WORD          2
#define CW_KUL_DOUBLE        3
#define CW_KUL_DOTTED        4

#define CW_TLC_NONE          0
#define CW_TLC_DOT           1
#define CW_TLC_HYPHEN        2
#define CW_TLC_SINGLE        3

#define CW_STC_NORMAL        0  /* stcNormal   */
#define CW_FTC_DEFAULT       0  /* ftcDefault  */

#define CW_ICO_AUTO          0  /* icoAuto - the WIN colour table starts here */
#define CW_ICO_MAX           9  /* icoWhite + 1                               */

/* ===========================================================================
 * THE PROPERTY RECORDS
 *
 * A record is an array of `unsigned char` with manifest offsets, exactly as
 * Opus's ApplyPropChange treats one (`pprop[rrb.b]`, `*(int *)(pprop + rrb.b)`
 * - RTFIN2.C:69-91). It is NOT a C struct, and that is the whole point: a
 * struct's layout is the compiler's to choose, the table's offsets are not,
 * and a disagreement between them is the silent defect Opus's WARNING is
 * about. With no struct there is nothing to disagree.
 *
 * Read a word out of one with CW_PROP_W(p, off) and a byte with p[off].
 * Every _O_ constant naming a word is asserted EVEN at compile time below,
 * because an odd word offset on an 8086 is not a fault - it is a slow,
 * correct access on this CPU and a wrong one on the next.
 * ========================================================================= */

#define CW_PROP_W(p, off)   (*(int *)((p) + (off)))

/* --- CW_CHP - character properties (from Opus struct CHP, props.h:8-81) ----
 * The first three offsets are Opus's own: cof(ftc) is 2, cof(hps) is 4 and
 * cof(hpsPos) is 5 there too. kul and ico DIVERGE: Opus packs them into word 3
 * as 3- and 4-bit fields, and this file gives each a whole byte, because a
 * bit-field is not available and a byte is clearer than a shift. Two bytes for
 * one instance of a record is not a cost worth a mask. */
#define CW_CHP_O_GRPF        0  /* int   the flag word, masks below          */
#define CW_CHP_W_GRPF        0  /*       ...as a WORD index, for rrbaBit     */
#define CW_CHP_O_FTC         2  /* int   font code           (Opus cof(ftc)) */
#define CW_CHP_O_HPS         4  /* byte  size in half-points (Opus cof(hps)) */
#define CW_CHP_O_HPSPOS      5  /* byte  super/subscript, 2's complement:
                                 *       > 0 raised, and 256 - v is the drop
                                 *       for a negative      (Opus cof(hpsPos))
                                 */
#define CW_CHP_O_KUL         6  /* byte  underline code, CW_KUL_*            */
#define CW_CHP_O_ICO         7  /* byte  colour index, CW_ICO_*              */
#define CW_CB_CHP            8

/* The flag bits. The INDICES are Opus/RTF.H:176-204 (ibitCf*) and the MASKS are
 * what props.h:106-111 publishes for the 8086 build. Two independent files in
 * the Opus tree that have to agree, and the compile-time assertions below make
 * them agree here or stop the build.
 *
 * Bit 4 is reserved and unnamed on purpose: it is fShadow on the Mac and
 * fFldVanish on the 8086 (props.h:13-18), which is precisely the kind of
 * per-machine difference that makes a bit number the wrong thing to guess at.
 * Bits 5, 6, 8 and 9 are named but unreachable from the frozen keyword set;
 * they are here so that adding \scaps or \caps later is one table row and not
 * a renumbering. */
#define CW_CHP_BIT_BOLD      0  /* ibitCfBold      */
#define CW_CHP_BIT_ITALIC    1  /* ibitCfItalic    */
#define CW_CHP_BIT_STRIKE    2  /* ibitCfStrike    */
#define CW_CHP_BIT_OUTLINE   3  /* ibitCfOutline   */
                                /* bit 4: fFldVanish on 8086, fShadow on Mac */
#define CW_CHP_BIT_SMALLCAPS 5  /* ibitCfSmallCaps */
#define CW_CHP_BIT_CAPS      6  /* ibitCfCaps      */
#define CW_CHP_BIT_VANISH    7  /* ibitCfVanish    */
#define CW_CHP_BIT_RMARK     8  /* ibitCfRMark     */
#define CW_CHP_BIT_SPEC      9  /* fSpec, props.h word 0 bit 9              */

#define CW_CHP_F_BOLD      0x0001   /* maskfBold      props.h:106 */
#define CW_CHP_F_ITALIC    0x0002   /* maskfItalic    props.h:107 */
#define CW_CHP_F_STRIKE    0x0004
#define CW_CHP_F_OUTLINE   0x0008
#define CW_CHP_F_SMALLCAPS 0x0020   /* maskfSmallCaps props.h:108 */
#define CW_CHP_F_CAPS      0x0040
#define CW_CHP_F_VANISH    0x0080   /* maskfVanish    props.h:109 */
#define CW_CHP_F_RMARK     0x0100
#define CW_CHP_F_SPEC      0x0200   /* maskFSpec      props.h:103 */

/* --- CW_PAP - paragraph properties (from Opus struct PAP, props.h:226-307) --
 * Opus's PAP is 250 bytes and most of it is border, numbering, side-by-side and
 * table state this program does not have. The offsets below are cword's, NOT
 * Opus's, and every one of them is named in the table so a row stays readable.
 * The tab plex is capped at 12 stops (Opus: itbdMax 50) - see the narrowing
 * note at the head of this file. */
#define CW_PAP_O_STC         0  /* byte  style code                          */
#define CW_PAP_O_JC          1  /* byte  justification, CW_JC_*              */
#define CW_PAP_O_FKEEP       2  /* byte  \keep                               */
#define CW_PAP_O_FKEEPFOLLOW 3  /* byte  \keepn                              */
#define CW_PAP_O_FPAGEBB     4  /* byte  \pagebb                             */
#define CW_PAP_O_ITBDMAC     5  /* byte  tab stops in use, 0..CW_ITBD_MAX    */
#define CW_PAP_O_DXARIGHT    6  /* int   \ri  right indent, twips            */
#define CW_PAP_O_DXALEFT     8  /* int   \li  left indent                    */
#define CW_PAP_O_DXALEFT1   10  /* int   \fi  first line, RELATIVE to dxaLeft*/
#define CW_PAP_O_DYALINE    12  /* int   \sl  line spacing                   */
#define CW_PAP_O_DYABEFORE  14  /* int   \sb  space before  (parsed unsigned)*/
#define CW_PAP_O_DYAAFTER   16  /* int   \sa  space after   (parsed unsigned)*/
#define CW_PAP_O_RGDXATAB   18  /* int[12]  \tx stop positions               */
#define CW_PAP_O_RGTBD      42  /* byte[12] each stop's jc and tlc, packed
                                 *          exactly as Opus's Tbd() macro
                                 *          does on the 8086 (props.h:344):
                                 *          jc | (tlc << 3)                  */
#define CW_CB_PAP           54
#define CW_ITBD_MAX         12

/* Opus/wordtech/props.h:341-344. The Mac spells this ((jc << 5) + (tlc << 2))
 * and the 8086 spells it this way; getting the wrong one gives every tab stop
 * a leader nobody asked for. */
#define CW_TBD(jc, tlc)     ((jc) | ((tlc) << 3))
#define CW_TBD_JC(t)        ((t) & 7)
#define CW_TBD_TLC(t)       (((t) >> 3) & 7)

/* --- CW_DOP - document properties (from Opus struct DOP, wordwin.h:486-540) -
 * Seven fields of the forty. Opus's are `uns`; these are signed - see the
 * narrowing note. The defaults in the table (12240 x 15840 twips, 1in top and
 * bottom, 1.25in left and right, a 0.5in default tab) are Opus's own, from the
 * rgrrbWord rows, and they are US Letter. */
#define CW_DOP_O_XAPAGE      0  /* int  \paperw                              */
#define CW_DOP_O_YAPAGE      2  /* int  \paperh                              */
#define CW_DOP_O_DXALEFT     4  /* int  \margl                               */
#define CW_DOP_O_DXARIGHT    6  /* int  \margr                               */
#define CW_DOP_O_DYATOP      8  /* int  \margt                               */
#define CW_DOP_O_DYABOTTOM  10  /* int  \margb                               */
#define CW_DOP_O_DXATAB     12  /* int  \deftab                              */
#define CW_CB_DOP           14

/* --- CW_RIS - the reader's own state, where a pgcRibl row writes ----------
 * Opus's RIBL (RTF.H:443-616) is the whole RTF input block, several hundred
 * bytes of it. What a TABLE ROW addresses is only these six fields, so only
 * these are pinned. The reader is free to carry whatever else it needs; what it
 * may not do is move these, because the table's `b` values point at them.
 * Embed this record in the larger state rather than scattering the fields. */
#define CW_RIS_O_CHS         0  /* byte  charset, CW_CHS_*                   */
#define CW_RIS_O_FMC         1  /* byte  font family being defined, CW_FMC_* */
#define CW_RIS_O_CRED        2  /* byte  \red   - accumulating a \colortbl   */
#define CW_RIS_O_CGREEN      3  /* byte  \green   entry; the entry is emitted
                                 *       on the ';' that ends it            */
#define CW_RIS_O_CBLUE       4  /* byte  \blue                               */
#define CW_RIS_O_FNEXTISDEST 5  /* byte  \* seen: an unknown keyword next
                                 *       means discard the group            */
#define CW_RIS_O_DEFF        6  /* int   \deff  the document's default font  */
#define CW_CB_RIS            8

/* ===========================================================================
 * CW_IRRB_* - the index of every row in cw_rrb[]
 *
 * THE ORDER IS THE TABLE. Renumbering one of these without moving the matching
 * row moves every property in the file, and the failure is that \b makes text
 * italic - which is why cwrtftbl.c holds an expectation table naming the
 * keyword, the row and the value it must find, and why the self-check walks it.
 *
 * The _FIRST/_LIM pairs are Opus's idea (RTFTBL.H:328-350): a range of rows
 * that one reset keyword clears. \pard resets CW_IRRB_PAP_FIRST up to but not
 * including CW_IRRB_PAP_LIM; \plain does the same for the CHP range. Note that
 * the CHP range stops before DN and PLAIN exactly as Opus's irrbChpLim does,
 * because DN shares hpsPos with UP and PLAIN is the reset itself.
 * ========================================================================= */
#define CW_IRRB_PAP_FIRST    0

#define CW_IRRB_S            0  /* \s      style                             */
#define CW_IRRB_JC           1  /* \ql \qc \qr \qj                           */
#define CW_IRRB_FI           2  /* \fi     first-line indent                 */
#define CW_IRRB_LI           3  /* \li     left indent                       */
#define CW_IRRB_RI           4  /* \ri     right indent                      */
#define CW_IRRB_SB           5  /* \sb     space before                      */
#define CW_IRRB_SA           6  /* \sa     space after                       */
#define CW_IRRB_SL           7  /* \sl     line spacing                      */
#define CW_IRRB_KEEP         8  /* \keep                                     */
#define CW_IRRB_KEEPN        9  /* \keepn                                    */
#define CW_IRRB_PAGEBB      10  /* \pagebb                                   */
#define CW_IRRB_TX          11  /* \tx     add a tab stop                    */

#define CW_IRRB_PAP_LIM     12

#define CW_IRRB_TJC         12  /* \tqc \tqr \tqdec - the PENDING stop's jc  */
#define CW_IRRB_PARD        13  /* \pard   reset the range above             */

#define CW_IRRB_CHP_FIRST   14

#define CW_IRRB_B           14  /* \b                                        */
#define CW_IRRB_I           15  /* \i                                        */
#define CW_IRRB_STRIKE      16  /* \strike                                   */
#define CW_IRRB_V           17  /* \v      hidden text                       */
#define CW_IRRB_F           18  /* \f      font                              */
#define CW_IRRB_FS          19  /* \fs     size in half-points               */
#define CW_IRRB_KUL         20  /* \ul \uld \uldb \ulnone \ulw               */
#define CW_IRRB_CF          21  /* \cf     foreground colour                 */
#define CW_IRRB_UP          22  /* \up     superscript                       */

#define CW_IRRB_CHP_LIM     23

#define CW_IRRB_DN          23  /* \dn     subscript - shares hpsPos with UP */
#define CW_IRRB_PLAIN       24  /* \plain  reset the range above             */

#define CW_IRRB_PAPERW      25  /* \paperw                                   */
#define CW_IRRB_PAPERH      26  /* \paperh                                   */
#define CW_IRRB_MARGL       27  /* \margl                                    */
#define CW_IRRB_MARGR       28  /* \margr                                    */
#define CW_IRRB_MARGT       29  /* \margt                                    */
#define CW_IRRB_MARGB       30  /* \margb                                    */
#define CW_IRRB_DEFTAB      31  /* \deftab                                   */

#define CW_IRRB_CHS         32  /* \ansi \pc                                 */
#define CW_IRRB_FMC         33  /* \fnil \froman \fswiss \fmodern \fscript   */
#define CW_IRRB_RED         34  /* \red                                      */
#define CW_IRRB_GREEN       35  /* \green                                    */
#define CW_IRRB_BLUE        36  /* \blue                                     */
#define CW_IRRB_DEFF        37  /* \deff                                     */
#define CW_IRRB_ASTERISK    38  /* \*      the next unknown keyword skips    */

#define CW_IRRB_IGNORE      39  /* recognised, deliberately does nothing     */

#define CW_IRRB_MAX         40

/* ===========================================================================
 * THE TABLES
 * ========================================================================= */

/* One keyword. Opus's struct RSYM (RTF.H:304-315) is
 *
 *     int val : 8;  int fPassVal : 1;  int rac : 7;
 *     union { int ipfnAction; int irrbProp; int fSpec; };
 *
 * - a bit-field and an anonymous union, and SmallerC has neither (SPEC.md
 * 67.7). Re-expressed:
 *
 *   val     widened to a full int, so 160 is 160 (see the narrowing note)
 *   racf    rac in bits 0-6 and fPassVal in bit 7, which is the same byte
 *           Opus's two adjacent fields occupied - unpacked by ovl_rsym_rac()
 *           and ovl_rsym_passval(), the only two places that know the layout
 *   arg     the union, discriminated by rac: an CW_IRRB_* for CW_RAC_CHNGPROP,
 *           a CW_IPFN_* for CW_RAC_CHNGDEST and CW_RAC_SPECCHARACT, a
 *           "this is a special character" flag for CW_RAC_SPECCHAR, and
 *           unused for the rest
 *
 * Six bytes, verified: SmallerC lays this out as dw/dw/db/db with no padding.
 */
struct CW_RSYM {
    const char    *sz;      /* the keyword, without its backslash */
    int            val;
    unsigned char  racf;
    unsigned char  arg;
};

#define CW_RSF_PASSVAL    0x80  /* fPassVal: use val, ignore any number */
#define CW_RSF_RACMASK    0x7f

/* One property action. Opus's struct RRB (RTF.H:378-384) is
 *
 *     int rrba : 3;  int pgc : 7;  int b : 6;  int w;
 *
 * - one packed word plus a value word. Same shape, no bit-fields: rrba and pgc
 * share a byte, `b` gets its own (it needs 6 bits and there is nothing to save
 * by splitting it), and `w` is the value word unchanged. Four bytes, as Opus's.
 */
struct CW_RRB {
    unsigned char  ap;      /* rrba | (pgc << CW_RRB_PGCSHIFT) */
    unsigned char  b;       /* byte offset, or a BIT index when rrba is BIT */
    int            w;       /* default value, or a WORD index when rrba is BIT */
};

#define CW_RRB_RRBAMASK   0x07
#define CW_RRB_PGCSHIFT   3
#define CW_RRB_PGCMASK    0x0f
#define CW_RRB_AP(rrba, pgc)  ((rrba) | ((pgc) << CW_RRB_PGCSHIFT))

/* rrba and pgc must both survive the packing. Three bits for rrba (max value
 * CW_RRBA_SPEC = 7) and four for pgc (max CW_PGC_DTR = 8) is seven bits of the
 * eight, and bit 7 is spare. If a pgc is ever added past 15 this stops the
 * build here rather than aliasing pgcPara onto something else at run time. */
CW_CTASSERT(01, CW_RRBA_SPEC <= CW_RRB_RRBAMASK);
CW_CTASSERT(02, CW_PGC_DTR   <= CW_RRB_PGCMASK);
CW_CTASSERT(03, (CW_RRB_PGCMASK << CW_RRB_PGCSHIFT) <= 0xff);
CW_CTASSERT(04, CW_RAC_MAX   <= CW_RSF_RACMASK);

/* --- the bit positions, which are the thing that fails silently -----------
 * Each mask against 1 << its index, and then against the literal props.h
 * publishes for the 8086. Both halves matter: the first catches a typo in a
 * mask, the second catches a wrong BIT INDEX, and neither catches the other. */
CW_CTASSERT(10, CW_CHP_F_BOLD      == (1 << CW_CHP_BIT_BOLD));
CW_CTASSERT(11, CW_CHP_F_ITALIC    == (1 << CW_CHP_BIT_ITALIC));
CW_CTASSERT(12, CW_CHP_F_STRIKE    == (1 << CW_CHP_BIT_STRIKE));
CW_CTASSERT(13, CW_CHP_F_OUTLINE   == (1 << CW_CHP_BIT_OUTLINE));
CW_CTASSERT(14, CW_CHP_F_SMALLCAPS == (1 << CW_CHP_BIT_SMALLCAPS));
CW_CTASSERT(15, CW_CHP_F_CAPS      == (1 << CW_CHP_BIT_CAPS));
CW_CTASSERT(16, CW_CHP_F_VANISH    == (1 << CW_CHP_BIT_VANISH));
CW_CTASSERT(17, CW_CHP_F_RMARK     == (1 << CW_CHP_BIT_RMARK));
CW_CTASSERT(18, CW_CHP_F_SPEC      == (1 << CW_CHP_BIT_SPEC));

/* ...and against Opus/wordtech/props.h:103-110, the 8086 (#else) arm. */
CW_CTASSERT(20, CW_CHP_F_BOLD      == 0x0001);  /* maskfBold      */
CW_CTASSERT(21, CW_CHP_F_ITALIC    == 0x0002);  /* maskfItalic    */
CW_CTASSERT(22, CW_CHP_F_SMALLCAPS == 0x0020);  /* maskfSmallCaps */
CW_CTASSERT(23, CW_CHP_F_VANISH    == 0x0080);  /* maskfVanish    */
CW_CTASSERT(24, CW_CHP_F_SPEC      == 0x0200);  /* maskFSpec      */
/* props.h:110 maskMajorityBits is 0x00A3 on the 8086 = bold|italic|smallcaps|
 * vanish. A fifth, independent witness that those four bits are 0, 1, 5 and 7. */
CW_CTASSERT(25, 0x00A3 == (CW_CHP_F_BOLD | CW_CHP_F_ITALIC |
                           CW_CHP_F_SMALLCAPS | CW_CHP_F_VANISH));
/* Bit 4 stays unnamed and unused: it is fShadow on the Mac and fFldVanish on
 * the 8086. Nothing here may claim it. */
CW_CTASSERT(26, 0 == ((CW_CHP_F_BOLD | CW_CHP_F_ITALIC | CW_CHP_F_STRIKE |
                       CW_CHP_F_OUTLINE | CW_CHP_F_SMALLCAPS | CW_CHP_F_CAPS |
                       CW_CHP_F_VANISH | CW_CHP_F_RMARK | CW_CHP_F_SPEC)
                      & 0x0010));
/* Every named bit inside the one word an rrbaBit row can reach. */
CW_CTASSERT(27, CW_CHP_BIT_SPEC < 16);
CW_CTASSERT(28, (CW_CHP_W_GRPF * 2) + 2 <= CW_CB_CHP);

/* --- every word-sized field is at an even offset --------------------------
 * An odd word offset is not a fault on an 8086, it is a correct slow access,
 * so nothing at run time will ever tell you. */
CW_CTASSERT(30, (CW_CHP_O_GRPF      & 1) == 0);
CW_CTASSERT(31, (CW_CHP_O_FTC       & 1) == 0);
CW_CTASSERT(32, (CW_PAP_O_DXARIGHT  & 1) == 0);
CW_CTASSERT(33, (CW_PAP_O_DXALEFT   & 1) == 0);
CW_CTASSERT(34, (CW_PAP_O_DXALEFT1  & 1) == 0);
CW_CTASSERT(35, (CW_PAP_O_DYALINE   & 1) == 0);
CW_CTASSERT(36, (CW_PAP_O_DYABEFORE & 1) == 0);
CW_CTASSERT(37, (CW_PAP_O_DYAAFTER  & 1) == 0);
CW_CTASSERT(38, (CW_PAP_O_RGDXATAB  & 1) == 0);
CW_CTASSERT(39, (CW_DOP_O_XAPAGE    & 1) == 0);
CW_CTASSERT(40, (CW_DOP_O_YAPAGE    & 1) == 0);
CW_CTASSERT(41, (CW_DOP_O_DXALEFT   & 1) == 0);
CW_CTASSERT(42, (CW_DOP_O_DXARIGHT  & 1) == 0);
CW_CTASSERT(43, (CW_DOP_O_DYATOP    & 1) == 0);
CW_CTASSERT(44, (CW_DOP_O_DYABOTTOM & 1) == 0);
CW_CTASSERT(45, (CW_DOP_O_DXATAB    & 1) == 0);
CW_CTASSERT(46, (CW_RIS_O_DEFF      & 1) == 0);

/* --- every field lands inside its record ---------------------------------- */
CW_CTASSERT(50, CW_CHP_O_ICO         + 1 <= CW_CB_CHP);
CW_CTASSERT(51, CW_CHP_O_FTC         + 2 <= CW_CB_CHP);
CW_CTASSERT(52, CW_PAP_O_DYAAFTER    + 2 <= CW_CB_PAP);
CW_CTASSERT(53, CW_PAP_O_RGDXATAB + (CW_ITBD_MAX * 2) <= CW_PAP_O_RGTBD);
CW_CTASSERT(54, CW_PAP_O_RGTBD    +  CW_ITBD_MAX      <= CW_CB_PAP);
CW_CTASSERT(55, CW_DOP_O_DXATAB      + 2 <= CW_CB_DOP);
CW_CTASSERT(56, CW_RIS_O_DEFF        + 2 <= CW_CB_RIS);
/* `b` is six bits in Opus's RRB and one byte here; either way no offset may
 * exceed 255, and the tab plex is what would get there first. */
CW_CTASSERT(57, CW_CB_PAP <= 255);

/* --- the reset ranges are ranges ------------------------------------------ */
CW_CTASSERT(60, CW_IRRB_PAP_FIRST <  CW_IRRB_PAP_LIM);
CW_CTASSERT(61, CW_IRRB_PAP_LIM   <= CW_IRRB_CHP_FIRST);
CW_CTASSERT(62, CW_IRRB_CHP_FIRST <  CW_IRRB_CHP_LIM);
CW_CTASSERT(63, CW_IRRB_CHP_LIM   <= CW_IRRB_MAX);
CW_CTASSERT(64, CW_IRRB_IGNORE    == CW_IRRB_MAX - 1);
/* \pard and \plain are the resets, so neither may be inside the range it
 * resets - Opus keeps both outside for the same reason. */
CW_CTASSERT(65, CW_IRRB_PARD  >= CW_IRRB_PAP_LIM);
CW_CTASSERT(66, CW_IRRB_PLAIN >= CW_IRRB_CHP_LIM);

/* ===========================================================================
 * THE INTERFACE
 *
 * Defined in cwrtftbl.c, which is #included by the package's one translation
 * unit. Nothing here takes the address of an automatic, uses a string
 * instruction, passes or returns a struct by value, or wants a `long` - the
 * four rules of apps/cc/os88.h, and cwrtftbl.c is written to all four.
 * ========================================================================= */

#define CW_RTF_NAMES     76     /* rows in cw_rsym[] */
#define CW_RTF_NOTFOUND  (-1)

/* DECLARED WITHOUT A DIMENSION ON PURPOSE, and the tables are DEFINED without
 * one too. Write `cw_rsym[CW_RTF_NAMES]` at the definition and a missing row is
 * not an error: C fills the shortfall with zeros, sizeof still reports
 * CW_RTF_NAMES rows, the compile-time count assertion still passes, and the
 * binary search walks into a row whose name pointer is NULL. Measured, not
 * feared - the count was bumped to 77 with 76 rows present and the build was
 * clean. With no dimension, sizeof is the truth and CW_CTASSERT 70/71 in
 * cwrtftbl.c compare the truth against the constant. */
extern const struct CW_RSYM cw_rsym[];
extern const struct CW_RRB  cw_rrb[];

/* ovl_rtf_lookup - find a control word.
 * in:  sz - the keyword with its leading backslash and any trailing number
 *           already stripped, NUL terminated. Case matters: RTF keywords are
 *           lower case and Opus does not fold either.
 * out: its index in cw_rsym[], or CW_RTF_NOTFOUND. A miss is a NORMAL result -
 *      it is 211 of Opus's 287 keywords plus everything a later writer
 *      invented, and the caller's ignore path is where they go. */
int ovl_rtf_lookup(const char *sz);

/* The four accessors. rac and fPassVal share a byte; these are the only places
 * that know it, so a change to the packing is a change to two lines. */
int ovl_rsym_val(int irsym);
int ovl_rsym_rac(int irsym);
int ovl_rsym_passval(int irsym);
int ovl_rsym_arg(int irsym);

/* ...and the four for a property row. Same reason. */
int ovl_rrb_rrba(int irrb);
int ovl_rrb_pgc(int irrb);
int ovl_rrb_b(int irrb);
int ovl_rrb_w(int irrb);

/* cw_rtf_selfcheck - the run-time half of the self-check (the compile-time half
 * is the CW_CTASSERTs above and costs nothing).
 *
 * COMPILED ONLY WHEN CW_RTF_SELFCHECK IS DEFINED, so the shipped package pays
 * nothing for it. apps/cword/cwrtfchk.c defines it, includes these tables and
 * runs this on the BUILD HOST, where the checks are free and a failure stops
 * the build. The checks are all of manifest constants and table contents, so
 * they mean the same thing on a 64-bit host as on an 8086.
 *
 * out: 0 if every check passed, else a nonzero code identifying the check and
 *      the row - see CW_CHK_* in cwrtftbl.c. */
#ifdef CW_RTF_SELFCHECK
int cw_rtf_selfcheck(void);
#endif

#endif /* CWRTFTBL_H */
