/* ============================================================================
 * os8088 - apps/cword/cwmenu.c        Word's nine menus, and the bar they hang
 *                                     from
 *
 * #included by cword.c - one translation unit (SPEC.md 70.1).
 *
 * EVERY STRING BELOW IS VERBATIM FROM `Opus/resource/menus.cmd` (Microsoft
 * Word for Windows 1.1a, Computer History Museum release, 2014), MW_MENU - the
 * full-menus bar. The '&' that marked the mnemonic in the source is not in the
 * display string; its cell index rides beside it, because this port underlines
 * the letter rather than printing an ampersand. The right-justified captions
 * are `Opus/resource/keys.cmd`'s bindings for exactly the commands these items
 * invoke, and no caption is invented.
 *
 * MENU_APPMAX is five and Word's bar is nine, so this bar is the app's own
 * (SPEC.md 68.2): the kernel bar carries only the app-name pull-down. That is
 * a constraint of this operating system reproducing, by accident, the reason
 * the real product drew its own menus inside its own frame.
 *
 * AN ARRAY OF STRUCTS, WHICH IS ALLOWED AND IS THE SAFER CHOICE. The C rule
 * here is `no struct assignment, no struct by value, no struct return`
 * (SPEC.md 70.5.1) - because ES is the kernel's and SmallerC copies a struct
 * with `rep movsb` - and INDEXING one copies nothing: `cw_it[i].label` is
 * address arithmetic and a word load. The first draft of this file was five
 * parallel arrays and it was wrong on the day it was written: four items had
 * been dropped from the middles of four different menus and every attribute
 * below each of them was one row out of step, silently. One table per item is
 * a row you can read across, and the counts below are checked by the compiler.
 * ==========================================================================*/

/* --- what an item does. 0 = nothing: a separator, or a fact this platform
 * cannot do, which is present and greyed rather than absent (SPEC.md 47) --- */
#define CWA_NONE    0
#define CWA_NEW     1
#define CWA_OPEN    2
#define CWA_CLOSE   3
#define CWA_SAVE    4
#define CWA_SAVEAS  5
#define CWA_EXIT    6
#define CWA_UNDO    7
#define CWA_CUT     8
#define CWA_COPY    9
#define CWA_PASTE   10
#define CWA_SEARCH  11
#define CWA_REPL    12
#define CWA_GOTO    13
#define CWA_DRAFT   14
#define CWA_PAGE    15
#define CWA_VRIB    16
#define CWA_VRUL    17
#define CWA_VSTA    18
#define CWA_CHAR    19
#define CWA_PARA    20
#define CWA_SORT    21
#define CWA_RENUM   22
#define CWA_TOC     23
#define CWA_WIN1    24
#define CWA_ABOUT   25
#define CWA_SHOWP   26

/* --- item flags ---------------------------------------------------------- */
#define CWF_DIS     1                   /* drawn with the disabled pen (47) */
#define CWF_SEP     2                   /* a separator rule, not an item */
#define CWF_CHK     4                   /* can carry a check mark */

struct cw_item {
    const char   *label;                /* menus.cmd, with the '&' removed */
    const char   *cap;                  /* keys.cmd's caption, or 0 */
    unsigned char flag;
    unsigned char mn;                   /* the mnemonic's cell in the label */
    unsigned char key;                  /* ...and its letter, upper case */
    unsigned char act;                  /* CWA_* */
};

#define CWSEP { 0, 0, CWF_SEP, 0, 0, CWA_NONE }

static const struct cw_item cw_it[] = {
/* --- &File, 14 ---------------------------------------------------------- */
/*  0 */ { "New...",           0,                 0,       0, 'N', CWA_NEW    },
         { "Open...",          "Ctrl+F12",        0,       0, 'O', CWA_OPEN   },
         { "Close",            0,                 0,       0, 'C', CWA_CLOSE  },
         { "Save",             "Shift+F12",       0,       0, 'S', CWA_SAVE   },
         { "Save As...",       "F12",             0,       5, 'A', CWA_SAVEAS },
         { "Save All",         0,                 CWF_DIS, 3, 'E', CWA_NONE   },
         { "Find...",          0,                 CWF_DIS, 0, 'F', CWA_NONE   },
         CWSEP,
         { "Print...",         "Ctrl+Shift+F12",  CWF_DIS, 0, 'P', CWA_NONE   },
         { "Print Preview",    0,                 CWF_DIS, 9, 'V', CWA_NONE   },
         { "Print Merge...",   0,                 CWF_DIS, 6, 'M', CWA_NONE   },
         { "Printer Setup...", 0,                 CWF_DIS, 1, 'R', CWA_NONE   },
         CWSEP,
         { "Exit",             "Alt+F4",          0,       1, 'X', CWA_EXIT   },

/* --- &Edit, 15 ---------------------------------------------------------- */
/* 14 */ { "Undo",             "Alt+BkSp",        0,       0, 'U', CWA_UNDO   },
         { "Repeat",           "F4",              CWF_DIS, 0, 'R', CWA_NONE   },
         { "Cut",              "Shift+Del",       0,       2, 'T', CWA_CUT    },
         { "Copy",             "Ctrl+Ins",        0,       0, 'C', CWA_COPY   },
         { "Paste",            "Shift+Ins",       0,       0, 'P', CWA_PASTE  },
         { "Paste Link...",    0,                 CWF_DIS, 6, 'L', CWA_NONE   },
         CWSEP,
         { "Search...",        0,                 0,       0, 'S', CWA_SEARCH },
         { "Replace...",       0,                 0,       1, 'E', CWA_REPL   },
         { "Go To...",         "F5",              0,       0, 'G', CWA_GOTO   },
         CWSEP,
         { "Header/Footer...", 0,                 CWF_DIS, 0, 'H', CWA_NONE   },
         { "Summary Info...",  0,                 CWF_DIS, 8, 'I', CWA_NONE   },
         { "Glossary...",      0,                 CWF_DIS, 2, 'O', CWA_NONE   },
         { "Table...",         0,                 CWF_DIS, 1, 'A', CWA_NONE   },

/* --- &View, 13 ---------------------------------------------------------- */
/* 29 */ { "Outline",          0,                 CWF_DIS, 0, 'O', CWA_NONE   },
         { "Draft",            0,                 CWF_CHK, 0, 'D', CWA_DRAFT  },
         /* Draft wraps to the WINDOW and Page wraps to the SHEET - a 60-cell
          * column centred in the content, its edges drawn and a tick where a
          * page begins (SPEC.md 70.12.1, and SPEC.md 68.11 for the same mode in
          * the assembly port). They are a LIVE PAIR: exactly one of the two
          * carries a check, which cw_item_chk() answers from cw_page. */
         { "Page",             0,                 CWF_CHK, 0, 'P', CWA_PAGE   },
         CWSEP,
         { "Ribbon",           0,                 CWF_CHK, 2, 'B', CWA_VRIB   },
         { "Ruler",            0,                 CWF_CHK, 0, 'R', CWA_VRUL   },
         { "Status Bar",       0,                 CWF_CHK, 0, 'S', CWA_VSTA   },
         { "Footnotes",        0,                 CWF_DIS, 0, 'F', CWA_NONE   },
         { "Annotations",      0,                 CWF_DIS, 0, 'A', CWA_NONE   },
         CWSEP,
         { "Field Codes",      0,                 CWF_DIS, 6, 'C', CWA_NONE   },
         { "Preferences...",   0,                 CWF_DIS, 2, 'E', CWA_NONE   },
         { "Short Menus",      0,                 CWF_DIS, 6, 'M', CWA_NONE   },

/* --- &Insert, 13 -------------------------------------------------------- */
/* 42 */ { "Break...",         0,                 CWF_DIS, 0, 'B', CWA_NONE   },
         { "Footnote...",      0,                 CWF_DIS, 4, 'N', CWA_NONE   },
         { "File...",          0,                 CWF_DIS, 0, 'F', CWA_NONE   },
         { "Bookmark...",      0,                 CWF_DIS, 4, 'M', CWA_NONE   },
         { "Page Numbers...",  0,                 CWF_DIS, 6, 'U', CWA_NONE   },
         { "Table...",         0,                 CWF_DIS, 0, 'T', CWA_NONE   },
         CWSEP,
         { "Annotation",       0,                 CWF_DIS, 0, 'A', CWA_NONE   },
         { "Picture...",       0,                 CWF_DIS, 0, 'P', CWA_NONE   },
         { "Field...",         0,                 CWF_DIS, 4, 'D', CWA_NONE   },
         { "Index Entry...",   0,                 CWF_DIS, 6, 'E', CWA_NONE   },
         { "Index...",         0,                 CWF_DIS, 0, 'I', CWA_NONE   },
         { "Table of Contents...", 0,             0,       9, 'C', CWA_TOC    },

/* --- Forma&t, 12 -------------------------------------------------------- */
/* 55 */ { "Character...",     0,                 0,       0, 'C', CWA_CHAR   },
         { "Paragraph...",     0,                 0,       0, 'P', CWA_PARA   },
         { "Section...",       0,                 CWF_DIS, 0, 'S', CWA_NONE   },
         { "Document...",      0,                 CWF_DIS, 0, 'D', CWA_NONE   },
         CWSEP,
         { "Tabs...",          0,                 CWF_DIS, 0, 'T', CWA_NONE   },
         { "Styles...",        0,                 CWF_DIS, 2, 'Y', CWA_NONE   },
         { "Position...",      0,                 CWF_DIS, 1, 'O', CWA_NONE   },
         CWSEP,
         { "Define Styles...", 0,                 CWF_DIS, 2, 'F', CWA_NONE   },
         { "Picture...",       0,                 CWF_DIS, 5, 'R', CWA_NONE   },
         { "Table...",         0,                 CWF_DIS, 1, 'A', CWA_NONE   },

/* --- &Utilities, 11 ----------------------------------------------------- */
/* 67 */ { "Spelling...",      0,                 CWF_DIS, 0, 'S', CWA_NONE   },
         { "Thesaurus...",     0,                 CWF_DIS, 0, 'T', CWA_NONE   },
         { "Hyphenate...",     0,                 CWF_DIS, 0, 'H', CWA_NONE   },
         CWSEP,
         { "Renumber...",      0,                 0,       0, 'R', CWA_RENUM  },
         { "Revision Marks...", 0,                CWF_DIS, 9, 'M', CWA_NONE   },
         { "Compare Versions...", 0,              CWF_DIS, 8, 'V', CWA_NONE   },
         { "Sort...",          0,                 0,       1, 'O', CWA_SORT   },
         { "Calculate",        0,                 CWF_DIS, 0, 'C', CWA_NONE   },
         { "Repaginate Now",   0,                 CWF_DIS, 2, 'P', CWA_NONE   },
         { "Customize...",     0,                 CWF_DIS, 1, 'U', CWA_NONE   },

/* --- &Macro, 5 ---------------------------------------------------------- */
/* 78 */ { "Record...",        0,                 CWF_DIS, 2, 'C', CWA_NONE   },
         { "Run...",           0,                 CWF_DIS, 0, 'R', CWA_NONE   },
         { "Edit...",          0,                 CWF_DIS, 0, 'E', CWA_NONE   },
         { "Assign to Key...", 0,                 CWF_DIS,10, 'K', CWA_NONE   },
         { "Assign to Menu...", 0,                CWF_DIS,10, 'M', CWA_NONE   },

/* --- &Window, 4 --------------------------------------------------------- */
/* 83 */ { "New Window",       0,                 CWF_DIS, 0, 'N', CWA_NONE   },
         { "Arrange All",      0,                 CWF_DIS, 0, 'A', CWA_NONE   },
         CWSEP,
         { "1 Document",       0,                 CWF_CHK, 0, '1', CWA_WIN1   },

/* --- &Help, 9 ----------------------------------------------------------- */
/* 87 */ { "Index",            0,                 CWF_DIS, 0, 'I', CWA_NONE   },
         CWSEP,
         { "Keyboard",         0,                 CWF_DIS, 0, 'K', CWA_NONE   },
         { "Active Window",    0,                 CWF_DIS, 7, 'W', CWA_NONE   },
         CWSEP,
         { "Tutorial",         0,                 CWF_DIS, 0, 'T', CWA_NONE   },
         { "Using Help",       0,                 CWF_DIS, 6, 'H', CWA_NONE   },
         CWSEP,
         { "About...",         0,                 0,       0, 'A', CWA_ABOUT  }
};

/* --- the nine menus ------------------------------------------------------
 * The bar is ONE string and one opaque run: 56 cells, and the cell each title
 * starts at falls out of the string rather than being counted twice. */
#define CW_NMENU 9

static const char cw_mbar[] =
    "File Edit View Insert Format Utilities Macro Window Help";

static const unsigned char cw_mn_cell[]  = { 0, 5, 10, 15, 22, 29, 39, 45, 52 };
static const unsigned char cw_mn_cells[] = { 4, 4,  4,  6,  6,  9,  5,  6,  4 };
static const unsigned char cw_mn_tmn[]   = { 0, 0,  0,  0,  5,  0,  0,  0,  0 };
static const unsigned char cw_mn_first[] = { 0, 14, 29, 42, 55, 67, 78, 83, 87 };
static const unsigned char cw_mn_count[] = { 14, 15, 13, 13, 12, 11, 5, 4, 9 };
static const unsigned char cw_mn_width[] = {
    208, 144, 128, 176, 144, 168, 152, 128, 120
};

/* Alt+letter arrives as ASCII 0 with the LETTER KEY's scan code (int 16h), so
 * these are the scan codes of F, E, V, I, T, U, M, W, H in bar order - T and
 * not F for Format, because menus.cmd marks it `Forma&t`. */
static const unsigned char cw_mn_alt[] = {
    0x21, 0x12, 0x2F, 0x17, 0x14, 0x16, 0x32, 0x11, 0x23
};

/* THE TABLE AND THE INDEX AGREE, checked by the compiler rather than by
 * counting rows twice. A negative array size is a build failure naming this
 * line, which is what caught the four dropped items described at the head of
 * the file. 14+15+13+13+12+11+5+4+9 = 96, and cw_mn_first[8] + 9 must be the
 * same number. */
static char cw_ct_items[(sizeof(cw_it) / sizeof(cw_it[0])) == 96 ? 1 : -1];
static char cw_ct_last[(87 + 9) == 96 ? 1 : -1];
