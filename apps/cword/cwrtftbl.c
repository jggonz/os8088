/* ============================================================================
 * os8088 - apps/cword/cwrtftbl.c        the RTF keyword and property tables
 *
 * ---------------------------------------------------------------------------
 * ATTRIBUTION - THIS FILE IS DERIVED FROM MICROSOFT SOURCE CODE
 * ---------------------------------------------------------------------------
 * The two tables below are transcribed from the source code of MICROSOFT WORD
 * FOR WINDOWS 1.1a ("Opus"), released by the COMPUTER HISTORY MUSEUM in March
 * 2014 as a historical artefact.
 *
 *     Copyright (C) Microsoft Corporation.
 *     Source: Microsoft Word for Windows 1.1a source code, Computer History
 *             Museum release, 2014 (https://computerhistory.org/), directory
 *             `Opus/`.
 *
 * Every file in that release is headed "COPYRIGHT (C) 1987 MICROSOFT" and the
 * release carries no licence grant. Microsoft remains the copyright holder of
 * the material these tables derive from.
 *
 * PER TABLE, THE EXACT SOURCE:
 *
 *   cw_rsym[]        Opus/RTFTBL.H:169-467  rgszRtfSym[]  (the names, sorted)
 *                    Opus/RTFTBL.H:995-1330 rgrsymWord[]  (val/fPassVal/rac/arg)
 *                    - 76 of Opus's 287 rows, kept in Opus's own sort order
 *   cw_rrb[]         Opus/RTFTBL.H:1394-1580 rgrrbWord[]  (rrba/pgc/b/w)
 *                    - 40 of Opus's 141 rows; the `w` defaults are Opus's
 *                      values unchanged, the `b` offsets are cword's own
 *                      record layouts (see cwrtftbl.h) except where noted
 *   ovl_rtf_lookup()  Opus/rtfsubs.c:89-127  C_FSearchRgrsym() - ported
 *   the divergence   listed row by row under "WHERE THIS DIVERGES" below
 *   notes
 *
 * The rest of the account - what the tables mean, why the keyword set is
 * frozen at 76, what was narrowed to 16 bits and what that costs, and why
 * Opus's bit-position WARNING cannot bite here - is in cwrtftbl.h. Read that
 * first; this file is the data.
 *
 * ---------------------------------------------------------------------------
 * ONE TRANSLATION UNIT: THIS FILE IS #included, NOT COMPILED
 * ---------------------------------------------------------------------------
 * `nasm -f bin` has no notion of an external symbol (SPEC.md 73.1), so a C
 * package is exactly one .c file. The package's translation unit does
 *
 *       #include "cwrtftbl.h"
 *       #include "cwrtftbl.c"      // ONCE
 *
 * It is still a .c and not a .h because it defines objects and functions: a
 * header that emitted 1 KB of tables would be a header nobody could include
 * twice, and calling it what it is stops someone trying.
 *
 * ---------------------------------------------------------------------------
 * WHERE THIS DIVERGES FROM OPUS, ROW BY ROW
 * ---------------------------------------------------------------------------
 * Every one of these is a deliberate choice, and each is marked again at the
 * row it affects. Nothing else differs: if a row is not in this list, its
 * val / rac / rrba / pgc / w is byte-for-byte what Opus has.
 *
 *  1. \info \footnote \header \footer \stylesheet \pict are CW_RAC_SCANBYDEST
 *     here. Opus enters them as real destinations (racChngDest, with
 *     ipfnRdsNewStd or ipfnRdsHdr) because it implements them. cword discards
 *     the group. This is not an omission - see the two-skip-paths note in the
 *     header: without these six rows their text lands in the document body.
 *  2. \v is CW_RRBA_BIT here; Opus makes it rrbaSpec because its reader has to
 *     coordinate hidden text with field results. cword has no fields, so the
 *     bit is all there is - and it is the SAME bit, ibitCfVanish = 7.
 *  3. \s \fs \cf and the underline family are CW_RRBA_BYTE here, and \f is
 *     CW_RRBA_WORD; Opus makes all five rrbaSpec. Its reasons do not survive
 *     the trip: hps, ico, stc and kul are packed sub-word fields in its
 *     CHP/PAP, and its \f has to map an RTF font number through a font table
 *     cword does not have. cword's records give each of the four a whole byte
 *     and the font a whole word (cwrtftbl.h says where), so the generic store
 *     does the job and the reader has five fewer special cases. WHAT DOES NOT
 *     CHANGE IS THE VALUES: \fs still defaults to 24 half-points (Opus's own
 *     row, with its comment "we use RTF default 24, not Opus hpsDefault (20)"),
 *     \f to ftcDefault, \s to stcNormal, \cf to icoAuto. The \ul family's
 *     kulSingle etc. arrive from the keyword rows exactly as they do in Opus;
 *     the kulSingle in THIS table is a default for a bare \ul with no number,
 *     where Opus's rrbaSpec row carries a 0 it never reads.
 *  4. \chs and \fmc are CW_RRBA_BYTE where Opus has rrbaWord, for the same
 *     reason: they are bytes in CW_RIS and ints in Opus's RIBL.
 *  5. \up and \dn stay CW_RRBA_SPEC, as in Opus, and share one field. They must:
 *     hpsPos is one 2's-complement byte, \up stores +n and \dn stores 256 - n,
 *     and no generic store can express that.
 *  6. \red \green \blue and \deff stay CW_RRBA_SPEC, as in Opus. They
 *     accumulate a colour-table entry that is only complete at the ';' which
 *     ends it, so the row gives the reader the byte to stash the component in
 *     and the reader owns the rest.
 *  7. cw_rsym[].val is a full int where Opus's is `int val:8`. See the
 *     narrowing note in the header - it makes \~ (160) 160 rather than -96.
 *  8. \bin's row is present but its destination is not: \pict is skipped, so
 *     the only thing that ever consumes a \bin is the group-skipper, and it
 *     needs the byte count because raw binary contains braces. Opus's reader
 *     has the same row for the same reason (racBinParm, RTFIN.C:889).
 * ==========================================================================*/

#include "cwrtftbl.h"

/* ===========================================================================
 * THE KEYWORD TABLE
 *
 * SORTED, AND THE SORT IS LOAD-BEARING. ovl_rtf_lookup() is a binary search
 * that compares bytes as UNSIGNED, so the order is plain ASCII with a shorter
 * name before a longer one that begins with it ("f" < "fi" < "fs" < "fswiss",
 * because the NUL sorts below every letter). That is Opus's order in
 * rgszRtfSym and these rows are in it. A row out of place does not fail: it
 * makes ONE keyword unfindable, which reads as that feature quietly not
 * working, which is why the self-check walks the whole table for order.
 *
 * The columns are Opus's, in Opus's order:
 *
 *   sz      the keyword, no backslash
 *   val     the value it carries; meaningless unless PASSVAL or a number
 *           follows the keyword in the file
 *   racf    CW_RAC_* | CW_RSF_PASSVAL
 *   arg     CW_IRRB_* for CHNGPROP, CW_IPFN_* for CHNGDEST/SPECCHARACT,
 *           1 for a SPECCHAR that is one of Word's special characters
 *           (Opus's fSpec - it makes the reader set CW_CHP_F_SPEC), else 0
 * ========================================================================= */

#define CW_P   CW_RSF_PASSVAL       /* keeps the rows one line each */

const struct CW_RSYM cw_rsym[] = {
/*    sz              val                    racf                          arg */
    { "\n",           CW_CH_EOP,             CW_RAC_SPECCHARACT | CW_P,  CW_IPFN_PARAEND   },
    { "\r",           CW_CH_EOP,             CW_RAC_SPECCHARACT | CW_P,  CW_IPFN_PARAEND   },
    { "'",            0,                     CW_RAC_SPECCHARACT,         CW_IPFN_HEXCHAR   },
    { "*",            0,                     CW_RAC_CHNGPROP,            CW_IRRB_ASTERISK  },
    { "-",            CW_CH_NONREQHYPH,      CW_RAC_SPECCHAR    | CW_P,  0                 },
    { "\\",           CW_CH_BACKSLASH,       CW_RAC_SPECCHAR    | CW_P,  0                 },
    { "_",            CW_CH_NONBREAKHYPH,    CW_RAC_SPECCHAR    | CW_P,  0                 },
    { "ansi",         CW_CHS_ANSI,           CW_RAC_CHNGPROP    | CW_P,  CW_IRRB_CHS       },
    { "b",            0,                     CW_RAC_CHNGPROP,            CW_IRRB_B         },
    { "bin",          0,                     CW_RAC_BINPARM,             0                 },
    { "blue",         0,                     CW_RAC_CHNGPROP,            CW_IRRB_BLUE      },
    { "cf",           0,                     CW_RAC_CHNGPROP,            CW_IRRB_CF        },
    { "colortbl",     CW_RDS_COLORTBL,       CW_RAC_CHNGDEST    | CW_P,  CW_IPFN_RDSNEWSTD },
    { "deff",         0,                     CW_RAC_CHNGPROP,            CW_IRRB_DEFF      },
    { "deftab",       0,                     CW_RAC_CHNGPROP,            CW_IRRB_DEFTAB    },
    { "dn",           0,                     CW_RAC_CHNGPROP,            CW_IRRB_DN        },
    { "f",            0,                     CW_RAC_CHNGPROP,            CW_IRRB_F         },
    { "fi",           0,                     CW_RAC_CHNGPROP,            CW_IRRB_FI        },
    { "fmodern",      CW_FMC_MODERN,         CW_RAC_CHNGPROP    | CW_P,  CW_IRRB_FMC       },
    { "fnil",         CW_FMC_NIL,            CW_RAC_CHNGPROP    | CW_P,  CW_IRRB_FMC       },
    { "fonttbl",      CW_RDS_FONTTBL,        CW_RAC_CHNGDEST    | CW_P,  CW_IPFN_RDSNEWSTD },
    { "footer",       0,                     CW_RAC_SCANBYDEST,          0                 }, /* diverges (1) */
    { "footnote",     0,                     CW_RAC_SCANBYDEST,          0                 }, /* diverges (1) */
    { "froman",       CW_FMC_ROMAN,          CW_RAC_CHNGPROP    | CW_P,  CW_IRRB_FMC       },
    { "fs",           0,                     CW_RAC_CHNGPROP,            CW_IRRB_FS        },
    { "fscript",      CW_FMC_SCRIPT,         CW_RAC_CHNGPROP    | CW_P,  CW_IRRB_FMC       },
    { "fswiss",       CW_FMC_SWISS,          CW_RAC_CHNGPROP    | CW_P,  CW_IRRB_FMC       },
    { "green",        0,                     CW_RAC_CHNGPROP,            CW_IRRB_GREEN     },
    { "header",       0,                     CW_RAC_SCANBYDEST,          0                 }, /* diverges (1) */
    { "i",            0,                     CW_RAC_CHNGPROP,            CW_IRRB_I         },
    { "info",         0,                     CW_RAC_SCANBYDEST,          0                 }, /* diverges (1) */
    { "keep",         0,                     CW_RAC_CHNGPROP,            CW_IRRB_KEEP      },
    { "keepn",        0,                     CW_RAC_CHNGPROP,            CW_IRRB_KEEPN     },
    { "li",           0,                     CW_RAC_CHNGPROP,            CW_IRRB_LI        },
    { "line",         CW_CH_CRJ,             CW_RAC_SPECCHAR    | CW_P,  0                 },
    { "margb",        0,                     CW_RAC_CHNGPROP,            CW_IRRB_MARGB     },
    { "margl",        0,                     CW_RAC_CHNGPROP,            CW_IRRB_MARGL     },
    { "margr",        0,                     CW_RAC_CHNGPROP,            CW_IRRB_MARGR     },
    { "margt",        0,                     CW_RAC_CHNGPROP,            CW_IRRB_MARGT     },
    { "page",         CW_CH_SECT,            CW_RAC_SPECCHAR    | CW_P,  0                 },
    { "pagebb",       0,                     CW_RAC_CHNGPROP,            CW_IRRB_PAGEBB    },
    { "paperh",       0,                     CW_RAC_CHNGPROP,            CW_IRRB_PAPERH    },
    { "paperw",       0,                     CW_RAC_CHNGPROP,            CW_IRRB_PAPERW    },
    { "par",          CW_CH_EOP,             CW_RAC_SPECCHARACT | CW_P,  CW_IPFN_PARAEND   },
    { "pard",         0,                     CW_RAC_CHNGPROP,            CW_IRRB_PARD      },
    { "pc",           CW_CHS_PC,             CW_RAC_CHNGPROP    | CW_P,  CW_IRRB_CHS       },
    { "pict",         0,                     CW_RAC_SCANBYDEST,          0                 }, /* diverges (1) */
    { "plain",        0,                     CW_RAC_CHNGPROP,            CW_IRRB_PLAIN     },
    { "qc",           CW_JC_CENTER,          CW_RAC_CHNGPROP    | CW_P,  CW_IRRB_JC        },
    { "qj",           CW_JC_BOTH,            CW_RAC_CHNGPROP    | CW_P,  CW_IRRB_JC        },
    { "ql",           CW_JC_LEFT,            CW_RAC_CHNGPROP    | CW_P,  CW_IRRB_JC        },
    { "qr",           CW_JC_RIGHT,           CW_RAC_CHNGPROP    | CW_P,  CW_IRRB_JC        },
    { "red",          0,                     CW_RAC_CHNGPROP,            CW_IRRB_RED       },
    { "ri",           0,                     CW_RAC_CHNGPROP,            CW_IRRB_RI        },
    { "rtf",          0,                     CW_RAC_CHNGDEST,            CW_IPFN_RDSRTF    },
    { "s",            0,                     CW_RAC_CHNGPROP,            CW_IRRB_S         },
    { "sa",           0,                     CW_RAC_CHNGPROP,            CW_IRRB_SA        },
    { "sb",           0,                     CW_RAC_CHNGPROP,            CW_IRRB_SB        },
    { "sl",           0,                     CW_RAC_CHNGPROP,            CW_IRRB_SL        },
    { "strike",       0,                     CW_RAC_CHNGPROP,            CW_IRRB_STRIKE    },
    { "stylesheet",   0,                     CW_RAC_SCANBYDEST,          0                 }, /* diverges (1) */
    { "tab",          0,                     CW_RAC_SPECCHARACT,         CW_IPFN_TAB       },
    { "tqc",          CW_JC_CENTER,          CW_RAC_CHNGPROP    | CW_P,  CW_IRRB_TJC       },
    { "tqdec",        CW_JC_DECIMAL,         CW_RAC_CHNGPROP    | CW_P,  CW_IRRB_TJC       },
    { "tqr",          CW_JC_RIGHT,           CW_RAC_CHNGPROP    | CW_P,  CW_IRRB_TJC       },
    { "tx",           0,                     CW_RAC_CHNGPROP,            CW_IRRB_TX        },
    { "ul",           CW_KUL_SINGLE,         CW_RAC_CHNGPROP    | CW_P,  CW_IRRB_KUL       },
    { "uld",          CW_KUL_DOTTED,         CW_RAC_CHNGPROP    | CW_P,  CW_IRRB_KUL       },
    { "uldb",         CW_KUL_DOUBLE,         CW_RAC_CHNGPROP    | CW_P,  CW_IRRB_KUL       },
    { "ulnone",       CW_KUL_NONE,           CW_RAC_CHNGPROP    | CW_P,  CW_IRRB_KUL       },
    { "ulw",          CW_KUL_WORD,           CW_RAC_CHNGPROP    | CW_P,  CW_IRRB_KUL       },
    { "up",           0,                     CW_RAC_CHNGPROP,            CW_IRRB_UP        },
    { "v",            0,                     CW_RAC_CHNGPROP,            CW_IRRB_V         },
    { "{",            CW_CH_OPENBRK,         CW_RAC_SPECCHAR    | CW_P,  0                 },
    { "}",            CW_CH_CLOSEBRK,        CW_RAC_SPECCHAR    | CW_P,  0                 },
    { "~",            CW_CH_NONBREAKSP,      CW_RAC_SPECCHAR    | CW_P,  0                 }
};

#undef CW_P

/* ===========================================================================
 * THE PROPERTY-ACTION TABLE
 *
 * INDEXED BY CW_IRRB_*, so the row order IS the constant list in cwrtftbl.h.
 * A row says: which record (pgc), how to write it (rrba), where in it (b), and
 * what value to use when the keyword arrived with no number after it (w).
 *
 * The one irregular column is `w` on a CW_RRBA_BIT row, where it is a WORD
 * INDEX into the record and not a default - Opus's own irregularity (RTF.H:378
 * against RTFIN2.C:73-80), kept because changing it would make every row here
 * uncomparable with the row it came from.
 *
 * `b` on a CW_RRBA_BIT row is a BIT index, likewise.
 * ========================================================================= */

const struct CW_RRB cw_rrb[] = {
/*                              rrba          pgc               b                     w  */
/* CW_IRRB_S        \s      */ { CW_RRB_AP(CW_RRBA_BYTE, CW_PGC_PARA), CW_PAP_O_STC,         CW_STC_NORMAL  }, /* diverges (3) */
/* CW_IRRB_JC       \ql..   */ { CW_RRB_AP(CW_RRBA_BYTE, CW_PGC_PARA), CW_PAP_O_JC,          CW_JC_LEFT     },
/* CW_IRRB_FI       \fi     */ { CW_RRB_AP(CW_RRBA_WORD, CW_PGC_PARA), CW_PAP_O_DXALEFT1,    0              },
/* CW_IRRB_LI       \li     */ { CW_RRB_AP(CW_RRBA_WORD, CW_PGC_PARA), CW_PAP_O_DXALEFT,     0              },
/* CW_IRRB_RI       \ri     */ { CW_RRB_AP(CW_RRBA_WORD, CW_PGC_PARA), CW_PAP_O_DXARIGHT,    0              },
/* CW_IRRB_SB       \sb     */ { CW_RRB_AP(CW_RRBA_UNS,  CW_PGC_PARA), CW_PAP_O_DYABEFORE,   0              },
/* CW_IRRB_SA       \sa     */ { CW_RRB_AP(CW_RRBA_UNS,  CW_PGC_PARA), CW_PAP_O_DYAAFTER,    0              },
/* CW_IRRB_SL       \sl     */ { CW_RRB_AP(CW_RRBA_WORD, CW_PGC_PARA), CW_PAP_O_DYALINE,     0              },
/* CW_IRRB_KEEP     \keep   */ { CW_RRB_AP(CW_RRBA_FLAG, CW_PGC_PARA), CW_PAP_O_FKEEP,       0              },
/* CW_IRRB_KEEPN    \keepn  */ { CW_RRB_AP(CW_RRBA_FLAG, CW_PGC_PARA), CW_PAP_O_FKEEPFOLLOW, 0              },
/* CW_IRRB_PAGEBB   \pagebb */ { CW_RRB_AP(CW_RRBA_FLAG, CW_PGC_PARA), CW_PAP_O_FPAGEBB,     0              },
/* CW_IRRB_TX       \tx     */ { CW_RRB_AP(CW_RRBA_SPEC, CW_PGC_PARA), 0,                    0              },
/* CW_IRRB_TJC      \tq*    */ { CW_RRB_AP(CW_RRBA_SPEC, CW_PGC_PARA), 0,                    0              },
/* CW_IRRB_PARD     \pard   */ { CW_RRB_AP(CW_RRBA_SPEC, CW_PGC_PARA), 0,                    0              },

/* CW_IRRB_B        \b      */ { CW_RRB_AP(CW_RRBA_BIT,  CW_PGC_CHAR), CW_CHP_BIT_BOLD,      CW_CHP_W_GRPF  },
/* CW_IRRB_I        \i      */ { CW_RRB_AP(CW_RRBA_BIT,  CW_PGC_CHAR), CW_CHP_BIT_ITALIC,    CW_CHP_W_GRPF  },
/* CW_IRRB_STRIKE   \strike */ { CW_RRB_AP(CW_RRBA_BIT,  CW_PGC_CHAR), CW_CHP_BIT_STRIKE,    CW_CHP_W_GRPF  },
/* CW_IRRB_V        \v      */ { CW_RRB_AP(CW_RRBA_BIT,  CW_PGC_CHAR), CW_CHP_BIT_VANISH,    CW_CHP_W_GRPF  }, /* diverges (2) */
/* CW_IRRB_F        \f      */ { CW_RRB_AP(CW_RRBA_WORD, CW_PGC_CHAR), CW_CHP_O_FTC,         CW_FTC_DEFAULT },
/* CW_IRRB_FS       \fs     */ { CW_RRB_AP(CW_RRBA_BYTE, CW_PGC_CHAR), CW_CHP_O_HPS,         24             }, /* diverges (3) */
/* CW_IRRB_KUL      \ul*    */ { CW_RRB_AP(CW_RRBA_BYTE, CW_PGC_CHAR), CW_CHP_O_KUL,         CW_KUL_SINGLE  }, /* diverges (3) */
/* CW_IRRB_CF       \cf     */ { CW_RRB_AP(CW_RRBA_BYTE, CW_PGC_CHAR), CW_CHP_O_ICO,         CW_ICO_AUTO    }, /* diverges (3) */
/* CW_IRRB_UP       \up     */ { CW_RRB_AP(CW_RRBA_SPEC, CW_PGC_CHAR), CW_CHP_O_HPSPOS,      6              },
/* CW_IRRB_DN       \dn     */ { CW_RRB_AP(CW_RRBA_SPEC, CW_PGC_CHAR), CW_CHP_O_HPSPOS,      6              },
/* CW_IRRB_PLAIN    \plain  */ { CW_RRB_AP(CW_RRBA_SPEC, CW_PGC_CHAR), 0,                    0              },

/* CW_IRRB_PAPERW   \paperw */ { CW_RRB_AP(CW_RRBA_WORD, CW_PGC_DOC),  CW_DOP_O_XAPAGE,      12240          },
/* CW_IRRB_PAPERH   \paperh */ { CW_RRB_AP(CW_RRBA_WORD, CW_PGC_DOC),  CW_DOP_O_YAPAGE,      15840          },
/* CW_IRRB_MARGL    \margl  */ { CW_RRB_AP(CW_RRBA_WORD, CW_PGC_DOC),  CW_DOP_O_DXALEFT,     1800           },
/* CW_IRRB_MARGR    \margr  */ { CW_RRB_AP(CW_RRBA_WORD, CW_PGC_DOC),  CW_DOP_O_DXARIGHT,    1800           },
/* CW_IRRB_MARGT    \margt  */ { CW_RRB_AP(CW_RRBA_WORD, CW_PGC_DOC),  CW_DOP_O_DYATOP,      1440           },
/* CW_IRRB_MARGB    \margb  */ { CW_RRB_AP(CW_RRBA_WORD, CW_PGC_DOC),  CW_DOP_O_DYABOTTOM,   1440           },
/* CW_IRRB_DEFTAB   \deftab */ { CW_RRB_AP(CW_RRBA_WORD, CW_PGC_DOC),  CW_DOP_O_DXATAB,      720            },

/* CW_IRRB_CHS      \ansi   */ { CW_RRB_AP(CW_RRBA_BYTE, CW_PGC_RIS),  CW_RIS_O_CHS,         0              }, /* diverges (4) */
/* CW_IRRB_FMC      \fnil.. */ { CW_RRB_AP(CW_RRBA_BYTE, CW_PGC_RIS),  CW_RIS_O_FMC,         0              }, /* diverges (4) */
/* CW_IRRB_RED      \red    */ { CW_RRB_AP(CW_RRBA_SPEC, CW_PGC_RIS),  CW_RIS_O_CRED,        0              },
/* CW_IRRB_GREEN    \green  */ { CW_RRB_AP(CW_RRBA_SPEC, CW_PGC_RIS),  CW_RIS_O_CGREEN,      0              },
/* CW_IRRB_BLUE     \blue   */ { CW_RRB_AP(CW_RRBA_SPEC, CW_PGC_RIS),  CW_RIS_O_CBLUE,       0              },
/* CW_IRRB_DEFF     \deff   */ { CW_RRB_AP(CW_RRBA_SPEC, CW_PGC_RIS),  CW_RIS_O_DEFF,        0              },
/* CW_IRRB_ASTERISK \*      */ { CW_RRB_AP(CW_RRBA_BYTE, CW_PGC_RIS),  CW_RIS_O_FNEXTISDEST, 1              },

/* CW_IRRB_IGNORE           */ { CW_RRB_AP(CW_RRBA_SPEC, CW_PGC_PARA), 0,                    0              }
};

/* The two counts, checked where they are cheapest to check: at compile time,
 * against the same constants the array dimensions used. A row added without a
 * constant, or a constant bumped without a row, stops the build here. */
CW_CTASSERT(70, (sizeof(cw_rsym) / sizeof(cw_rsym[0])) == CW_RTF_NAMES);
CW_CTASSERT(71, (sizeof(cw_rrb)  / sizeof(cw_rrb[0]))  == CW_IRRB_MAX);

/* Six bytes and four bytes, as designed. If the compiler ever starts padding,
 * the tables silently grow by a third and the size report stops matching the
 * budget - and this is the line that says so.
 *
 * These two are the ONLY assertions in either file that are about the target
 * machine rather than about the tables, so they are the only two the host
 * self-check build skips: `int` is four bytes there and the rows are eight and
 * eight. Everything else - every mask, every offset, every range, both counts -
 * is a manifest constant and means exactly the same thing on both. */
#ifndef CW_RTF_SELFCHECK
CW_CTASSERT(72, sizeof(struct CW_RSYM) == 6);
CW_CTASSERT(73, sizeof(struct CW_RRB)  == 4);
#endif

/* ===========================================================================
 * THE LOOKUP
 *
 * Ported from Opus/rtfsubs.c:89-127 (C_FSearchRgrsym), which open-codes the
 * string compare inside the binary search rather than calling one - the same
 * reason applies here and more so: a call is 11 us on the target machine
 * (PERFORMANCE.md) and this loop makes seven probes per control word.
 *
 * Two changes from the original: it returns the index rather than a flag plus
 * an out-parameter (an out-parameter would have to point at something, and
 * pointing it at an automatic is refused at build time - SPEC.md 73.5), and
 * the compare is explicitly through `unsigned char`. Opus's `CHAR` is signed
 * on this compiler too, and every keyword in the table is 7-bit ASCII so no
 * live comparison can differ - but the moment someone adds a keyword with a
 * high-bit byte, a signed compare puts it before "'" and the table is
 * unsearchable from that row down. One cast now, or that bug later.
 * ========================================================================= */

int ovl_rtf_lookup(const char *sz)
{
    int lo;
    int hi;
    int m;
    const unsigned char *p;
    const unsigned char *q;

    lo = 0;
    hi = CW_RTF_NAMES;

    while (lo < hi) {
        m = (lo + hi) >> 1;

        p = (const unsigned char *)sz;
        q = (const unsigned char *)cw_rsym[m].sz;

        while (*p == *q) {
            if (*p == 0)
                return m;               /* found */
            p++;
            q++;
        }

        if (*p < *q)
            hi = m;
        else
            lo = m + 1;
    }

    return CW_RTF_NOTFOUND;             /* a NORMAL result - see the header */
}

/* --- the accessors -------------------------------------------------------
 * Every caller goes through these, so the packing of rac with fPassVal, and of
 * rrba with pgc, is known in exactly one place each. None of them range-checks
 * its index: the index came from ovl_rtf_lookup() or from a CW_IRRB_* constant,
 * and a check on every property applied is a cost paid on every character of
 * every document to catch a bug the self-check already caught at build time. */

int ovl_rsym_val(int irsym)      { return cw_rsym[irsym].val; }
int ovl_rsym_rac(int irsym)      { return cw_rsym[irsym].racf & CW_RSF_RACMASK; }
int ovl_rsym_passval(int irsym)  { return (cw_rsym[irsym].racf & CW_RSF_PASSVAL) != 0; }
int ovl_rsym_arg(int irsym)      { return cw_rsym[irsym].arg; }

int ovl_rrb_rrba(int irrb)       { return cw_rrb[irrb].ap & CW_RRB_RRBAMASK; }
int ovl_rrb_pgc(int irrb)        { return (cw_rrb[irrb].ap >> CW_RRB_PGCSHIFT) & CW_RRB_PGCMASK; }
int ovl_rrb_b(int irrb)          { return cw_rrb[irrb].b; }
int ovl_rrb_w(int irrb)          { return cw_rrb[irrb].w; }

/* ===========================================================================
 * THE SELF-CHECK
 *
 * Opus/RTF.H:170-172 asks for exactly this and names the routine that was
 * meant to do it:
 *
 *     "these defines are used in CkDefinesRtf(), a test routine that assures
 *      that the structures referenced do not change and invalidate the values.
 *      Please update that routine (in debug.c) when you add defines."
 *
 * - a test routine, in the debug build, that somebody had to remember to
 * update. This is that idea with the remembering taken out of it. It is in two
 * halves and neither is optional:
 *
 *   COMPILE TIME  the CW_CTASSERTs, in cwrtftbl.h and above. Masks against bit
 *                 indices, masks against the literals props.h publishes for the
 *                 8086, word offsets even, fields inside their records, ranges
 *                 ordered, both table lengths, both struct sizes. They cost the
 *                 package nothing - a typedef reserves no storage - and they
 *                 cannot be skipped, because they are the build.
 *
 *   BUILD HOST    this function, compiled only when CW_RTF_SELFCHECK is
 *                 defined and run by apps/cword/cwrtfchk.c on the machine
 *                 doing the build. It covers what a constant expression
 *                 cannot: that the names are SORTED, that no argument points
 *                 outside the table it indexes, that no offset points outside
 *                 the record it addresses, and that a hand-transcribed
 *                 expectation table taken independently from Opus finds
 *                 exactly what it expects.
 *
 * WHY THE EXPECTATION TABLE IS NOT REDUNDANT. Everything above it checks the
 * table against ITSELF - structurally consistent, and structurally consistent
 * with \b setting italic. The expectation rows are a second transcription of
 * Opus, done from the source and not from the table, and they are the only
 * check here that can catch a value that was copied wrong. They are spot
 * checks and they are honest about it: 26 keyword rows and 12 property rows of
 * 76 and 40. What they cover is every rac, every rrba, every value class
 * (character codes, justification, underline, charset, font family, twips) and
 * every row where Opus's own value is surprising.
 *
 * The shipped package compiles NONE of this and pays nothing for it.
 * ========================================================================= */

#ifdef CW_RTF_SELFCHECK

/* A failure code is check * 1000 + the row, so 4023 is "row 23 failed the
 * lookup round trip". Zero is the only success. */
#define CW_CHK_ORDER        1   /* names not strictly ascending              */
#define CW_CHK_RAC          2   /* rac out of range                          */
#define CW_CHK_ARG          3   /* arg out of range for its rac              */
#define CW_CHK_ROUNDTRIP    4   /* a name does not find its own row          */
#define CW_CHK_RRBA         5   /* rrba out of range                         */
#define CW_CHK_PGC          6   /* pgc out of range or not one cword uses    */
#define CW_CHK_OFFSET       7   /* b (+ width) outside the record            */
#define CW_CHK_BIT          8   /* bit index or word index outside the record*/
#define CW_CHK_ALIGN        9   /* a word-sized row at an odd offset         */
#define CW_CHK_EXPECT_SYM  10   /* a keyword row is not what Opus says       */
#define CW_CHK_EXPECT_RRB  11   /* a property row is not what Opus says      */
#define CW_CHK_ABSENT      12   /* something not in the table was found      */

/* --- the expectation table, transcribed from Opus a second time -----------
 * Taken from Opus/RTFTBL.H:995-1330 directly, NOT from cw_rsym above. Where a
 * row diverges deliberately the comment says which numbered divergence it is
 * and what Opus has instead. */
struct CW_XSYM {
    const char    *sz;
    int            val;
    unsigned char  rac;
    unsigned char  passval;
    unsigned char  arg;
};

static const struct CW_XSYM cw_xsym[] = {
    /* rgrsymWord: {chEop, fTrue, racSpecCharAct, ipfnDoParaEnd} */
    { "par",        13,  CW_RAC_SPECCHARACT, 1, CW_IPFN_PARAEND   },
    { "\n",         13,  CW_RAC_SPECCHARACT, 1, CW_IPFN_PARAEND   },
    /* {0, fFalse, racSpecCharAct, ipfnQuote} - the \'hh escape */
    { "'",           0,  CW_RAC_SPECCHARACT, 0, CW_IPFN_HEXCHAR   },
    /* {0, fFalse, racSpecCharAct, ipfnTab} */
    { "tab",         0,  CW_RAC_SPECCHARACT, 0, CW_IPFN_TAB       },
    /* {chNonReqHyphen, fTrue, racSpecChar, 0} - ch.h: chNonReqHyphen 31 */
    { "-",          31,  CW_RAC_SPECCHAR,    1, 0                 },
    /* {chNonBreakHyphen, fTrue, racSpecChar, 0} - ch.h: 30 */
    { "_",          30,  CW_RAC_SPECCHAR,    1, 0                 },
    /* {chBackslash, fTrue, racSpecChar, 0} - RTF.H:93: 92 */
    { "\\",         92,  CW_RAC_SPECCHAR,    1, 0                 },
    /* {chRTFOpenBrk, fTrue, racSpecChar, 0} - RTF.H:95: '{' */
    { "{",         123,  CW_RAC_SPECCHAR,    1, 0                 },
    { "}",         125,  CW_RAC_SPECCHAR,    1, 0                 },
    /* {chNonBreakSpace, fTrue, racSpecChar, 0} - ch.h:34: 160. Opus's val is
     * an 8-bit field and holds this as -96; widened here, divergence (7). */
    { "~",         160,  CW_RAC_SPECCHAR,    1, 0                 },
    /* {chCRJ, fTrue, racSpecChar, fFalse} - ch.h:9: 11 */
    { "line",       11,  CW_RAC_SPECCHAR,    1, 0                 },
    /* {chSect, fTrue, racSpecChar, fFalse} - ch.h:12: 12 */
    { "page",       12,  CW_RAC_SPECCHAR,    1, 0                 },
    /* {0, fFalse, racBinParm, 0} */
    { "bin",         0,  CW_RAC_BINPARM,     0, 0                 },
    /* {0, fFalse, racChngProp, irrbRAsterisk} */
    { "*",           0,  CW_RAC_CHNGPROP,    0, CW_IRRB_ASTERISK  },
    /* {0, fFalse, racChngProp, irrbRb} and irrbRi */
    { "b",           0,  CW_RAC_CHNGPROP,    0, CW_IRRB_B         },
    { "i",           0,  CW_RAC_CHNGPROP,    0, CW_IRRB_I         },
    /* {jcLeft, fTrue, racChngProp, irrbRjc}; props.h:350-353 jcLeft 0,
     * jcCenter 1, jcRight 2, jcBoth 3 */
    { "ql",          0,  CW_RAC_CHNGPROP,    1, CW_IRRB_JC        },
    { "qj",          3,  CW_RAC_CHNGPROP,    1, CW_IRRB_JC        },
    /* props.h:355 jcDecimal is 3, the same code as jcBoth in another record */
    { "tqdec",       3,  CW_RAC_CHNGPROP,    1, CW_IRRB_TJC       },
    /* {kulSingle, fTrue, racChngProp, irrbRkul}; props.h:119-124 */
    { "ul",          1,  CW_RAC_CHNGPROP,    1, CW_IRRB_KUL       },
    { "ulnone",      0,  CW_RAC_CHNGPROP,    1, CW_IRRB_KUL       },
    { "uldb",        3,  CW_RAC_CHNGPROP,    1, CW_IRRB_KUL       },
    /* {chsAnsi, fTrue, racChngProp, irrbRchs}; RTF.H:143 chsAnsi 2 */
    { "ansi",        2,  CW_RAC_CHNGPROP,    1, CW_IRRB_CHS       },
    /* {fmcRoman, fTrue, racChngProp, irrbRfmc}; RTF.H:147 -> FF_ROMAN, which
     * qwindows.h:1282 defines as (1<<4) = 0x10 */
    { "froman",   0x10,  CW_RAC_CHNGPROP,    1, CW_IRRB_FMC       },
    /* {rdsColortbl, fTrue, racChngDest, ipfnRdsNewStd}; RTFTBL.H:38 rds 6 */
    { "colortbl",    6,  CW_RAC_CHNGDEST,    1, CW_IPFN_RDSNEWSTD },
    /* {0, fFalse, racChngDest, ipfnRdsRtf} */
    { "rtf",         0,  CW_RAC_CHNGDEST,    0, CW_IPFN_RDSRTF    },
    /* DIVERGENCE (1): Opus has {rdsInfoBlock, fTrue, racChngDest,
     * ipfnRdsNewStd} and reads the summary information. cword discards the
     * group, which is what this row asserts. */
    { "info",        0,  CW_RAC_SCANBYDEST,  0, 0                 },
    { "pict",        0,  CW_RAC_SCANBYDEST,  0, 0                 }
};

#define CW_NXSYM  (sizeof(cw_xsym) / sizeof(cw_xsym[0]))

/* --- and the same for the property rows, from Opus/RTFTBL.H:1394-1580 ----- */
struct CW_XRRB {
    int            irrb;
    unsigned char  rrba;
    unsigned char  pgc;
    unsigned char  b;
    int            w;
};

static const struct CW_XRRB cw_xrrb[] = {
    /* rgrrbWord: { rrbaBit, pgcChar, ibitCfBold, iwordCfBold } and RTF.H:176
     * says ibitCfBold 0, iwordCfBold 0 */
    { CW_IRRB_B,        CW_RRBA_BIT,  CW_PGC_CHAR, CW_CHP_BIT_BOLD,      0     },
    { CW_IRRB_I,        CW_RRBA_BIT,  CW_PGC_CHAR, CW_CHP_BIT_ITALIC,    0     },
    /* DIVERGENCE (2): Opus has { rrbaSpec, pgcChar, ibitCfVanish,
     * iwordCfVanish }. Same bit (RTF.H:199 ibitCfVanish 7), written generically
     * here because cword has no fields to coordinate with. */
    { CW_IRRB_V,        CW_RRBA_BIT,  CW_PGC_CHAR, CW_CHP_BIT_VANISH,    0     },
    /* DIVERGENCE (3): Opus has { rrbaSpec, pgcChar, cof(hps), 24 } with the
     * comment "we use RTF default 24, not Opus hpsDefault (20)". The 24 is the
     * part that matters and it is unchanged. */
    { CW_IRRB_FS,       CW_RRBA_BYTE, CW_PGC_CHAR, CW_CHP_O_HPS,         24    },
    /* { rrbaSpec, pgcChar, cof(hpsPos), 6 } - both \up and \dn */
    { CW_IRRB_UP,       CW_RRBA_SPEC, CW_PGC_CHAR, CW_CHP_O_HPSPOS,      6     },
    { CW_IRRB_DN,       CW_RRBA_SPEC, CW_PGC_CHAR, CW_CHP_O_HPSPOS,      6     },
    /* { rrbaFlag, pgcPara, pof(fKeep), 0 } */
    { CW_IRRB_KEEP,     CW_RRBA_FLAG, CW_PGC_PARA, CW_PAP_O_FKEEP,       0     },
    /* { rrbaUns, pgcPara, pof(dyaBefore), 0 } - the row that says "parse the
     * number as unsigned", which is the whole difference between rrbaUns and
     * rrbaWord in Opus's reader */
    { CW_IRRB_SB,       CW_RRBA_UNS,  CW_PGC_PARA, CW_PAP_O_DYABEFORE,   0     },
    /* { rrbaWord, pgcDoc, dof(xaPage), 12240 } and the rest of US Letter */
    { CW_IRRB_PAPERW,   CW_RRBA_WORD, CW_PGC_DOC,  CW_DOP_O_XAPAGE,      12240 },
    { CW_IRRB_PAPERH,   CW_RRBA_WORD, CW_PGC_DOC,  CW_DOP_O_YAPAGE,      15840 },
    { CW_IRRB_MARGL,    CW_RRBA_WORD, CW_PGC_DOC,  CW_DOP_O_DXALEFT,     1800  },
    { CW_IRRB_DEFTAB,   CW_RRBA_WORD, CW_PGC_DOC,  CW_DOP_O_DXATAB,      720   },
    /* { rrbaByte, pgcRibl, riblof(fNextIsDest), 1 } - \* */
    { CW_IRRB_ASTERISK, CW_RRBA_BYTE, CW_PGC_RIS,  CW_RIS_O_FNEXTISDEST, 1     },
    /* { rrbaSpec, 0, 0, 0 } - the ignore row, pgc 0 in Opus too */
    { CW_IRRB_IGNORE,   CW_RRBA_SPEC, CW_PGC_PARA, 0,                    0     }
};

#define CW_NXRRB  (sizeof(cw_xrrb) / sizeof(cw_xrrb[0]))

/* Things that must NOT be in the table. Two kinds, and both have bitten a
 * hand-sorted table before: a keyword that was dropped on purpose (if one
 * reappears, the ignore path stopped being exercised and nobody noticed), and
 * a near-miss of one that IS in it (a binary search over a mis-sorted table
 * answers plausibly rather than not at all). */
static const char * const cw_xabsent[] = {
    "trowd", "cellx", "cell", "row", "intbl",       /* tables: not supported */
    "sect", "sectd", "cols",                        /* sections: not supported */
    "brdrs", "box", "xe", "tc", "field", "fldrslt", /* the RTFRARE.C trap      */
    "outl", "scaps", "mac", "pca",                  /* dropped deliberately    */
    "bb", "par2", "u", "q", "ulx", "fsx", "",       /* near misses             */
    "B", "PAR"                                      /* case: RTF is lower case */
};

#define CW_NXABSENT  (sizeof(cw_xabsent) / sizeof(cw_xabsent[0]))

/* cw_strcmpu - the same unsigned byte compare ovl_rtf_lookup() makes, so the
 * order check tests the order the SEARCH sees and not some other order. */
static int cw_strcmpu(const char *a, const char *b)
{
    const unsigned char *p;
    const unsigned char *q;

    p = (const unsigned char *)a;
    q = (const unsigned char *)b;

    while (*p == *q) {
        if (*p == 0)
            return 0;
        p++;
        q++;
    }
    return (*p < *q) ? -1 : 1;
}

/* cw_recsize - how big the record a pgc names is, so an offset can be checked
 * against it. 0 for a pgc cword does not use; no row may name one. */
static int cw_recsize(int pgc)
{
    if (pgc == CW_PGC_PARA) return CW_CB_PAP;
    if (pgc == CW_PGC_CHAR) return CW_CB_CHP;
    if (pgc == CW_PGC_DOC)  return CW_CB_DOP;
    if (pgc == CW_PGC_RIS)  return CW_CB_RIS;
    return 0;
}

int cw_rtf_selfcheck(void)
{
    int i;
    int j;
    int rac;
    int arg;
    int rrba;
    int pgc;
    int b;
    int cb;

    /* (1) the names ascend strictly. Strictly, because two equal names is a
     * row that can never be reached. */
    for (i = 1; i < CW_RTF_NAMES; i++) {
        if (cw_strcmpu(cw_rsym[i - 1].sz, cw_rsym[i].sz) >= 0)
            return CW_CHK_ORDER * 1000 + i;
    }

    /* (2)(3)(4) every keyword row: a rac in range, an arg that indexes the
     * table its rac says it indexes, and a name that finds its own row. */
    for (i = 0; i < CW_RTF_NAMES; i++) {
        rac = ovl_rsym_rac(i);
        arg = ovl_rsym_arg(i);

        if (rac >= CW_RAC_MAX)
            return CW_CHK_RAC * 1000 + i;

        if (rac == CW_RAC_CHNGPROP) {
            if (arg >= CW_IRRB_MAX)
                return CW_CHK_ARG * 1000 + i;
        } else if (rac == CW_RAC_CHNGDEST || rac == CW_RAC_SPECCHARACT) {
            if (arg >= CW_IPFN_MAX)
                return CW_CHK_ARG * 1000 + i;
        } else if (arg > 1) {
            /* SPECCHAR uses arg as Opus's fSpec flag; the rest ignore it. A
             * stray CW_IRRB_* left in one of those columns is a wrong property
             * applied the moment the rac is ever widened. */
            return CW_CHK_ARG * 1000 + i;
        }

        if (ovl_rtf_lookup(cw_rsym[i].sz) != i)
            return CW_CHK_ROUNDTRIP * 1000 + i;
    }

    /* (5)(6)(7)(8)(9) every property row lands somewhere real. */
    for (i = 0; i < CW_IRRB_MAX; i++) {
        rrba = ovl_rrb_rrba(i);
        pgc  = ovl_rrb_pgc(i);
        b    = ovl_rrb_b(i);
        cb   = cw_recsize(pgc);

        if (rrba >= CW_RRBA_MAX)
            return CW_CHK_RRBA * 1000 + i;
        if (cb == 0)
            return CW_CHK_PGC * 1000 + i;

        if (rrba == CW_RRBA_BIT) {
            /* b is a BIT index and w is a WORD index - Opus's irregularity. */
            if (b >= 16)
                return CW_CHK_BIT * 1000 + i;
            if ((ovl_rrb_w(i) * 2) + 2 > cb)
                return CW_CHK_BIT * 1000 + i;
        } else if (rrba == CW_RRBA_WORD || rrba == CW_RRBA_UNS) {
            if ((b & 1) != 0)
                return CW_CHK_ALIGN * 1000 + i;
            if (b + 2 > cb)
                return CW_CHK_OFFSET * 1000 + i;
        } else if (rrba == CW_RRBA_FLAG || rrba == CW_RRBA_BYTE) {
            if (b + 1 > cb)
                return CW_CHK_OFFSET * 1000 + i;
        } else {
            /* SPEC: b means whatever that special case says. It is still an
             * offset in every row here, or zero for the rows that use none. */
            if (b >= cb)
                return CW_CHK_OFFSET * 1000 + i;
        }
    }

    /* (10) the second transcription of the keyword rows. */
    for (j = 0; j < (int)CW_NXSYM; j++) {
        i = ovl_rtf_lookup(cw_xsym[j].sz);
        if (i < 0)
            return CW_CHK_EXPECT_SYM * 1000 + j;
        if (ovl_rsym_val(i)     != cw_xsym[j].val)     return CW_CHK_EXPECT_SYM * 1000 + j;
        if (ovl_rsym_rac(i)     != cw_xsym[j].rac)     return CW_CHK_EXPECT_SYM * 1000 + j;
        if (ovl_rsym_passval(i) != cw_xsym[j].passval) return CW_CHK_EXPECT_SYM * 1000 + j;
        if (ovl_rsym_arg(i)     != cw_xsym[j].arg)     return CW_CHK_EXPECT_SYM * 1000 + j;
    }

    /* (11) ...and of the property rows. */
    for (j = 0; j < (int)CW_NXRRB; j++) {
        i = cw_xrrb[j].irrb;
        if (ovl_rrb_rrba(i) != cw_xrrb[j].rrba) return CW_CHK_EXPECT_RRB * 1000 + j;
        if (ovl_rrb_pgc(i)  != cw_xrrb[j].pgc)  return CW_CHK_EXPECT_RRB * 1000 + j;
        if (ovl_rrb_b(i)    != cw_xrrb[j].b)    return CW_CHK_EXPECT_RRB * 1000 + j;
        if (ovl_rrb_w(i)    != cw_xrrb[j].w)    return CW_CHK_EXPECT_RRB * 1000 + j;
    }

    /* (12) and what must not be found. */
    for (j = 0; j < (int)CW_NXABSENT; j++) {
        if (ovl_rtf_lookup(cw_xabsent[j]) != CW_RTF_NOTFOUND)
            return CW_CHK_ABSENT * 1000 + j;
    }

    return 0;
}

#endif /* CW_RTF_SELFCHECK */

/* ============================================================================
 * WHAT THIS COSTS - MEASURED, NOT ESTIMATED
 *
 * APP_MAX_SIZE is 61,440 bytes for image and bss TOGETHER (SPEC.md 73.9), and
 * C spends it two to four times faster than assembly does, so a table is a line
 * item and not a rounding error. Taken by compiling this file alone through the
 * real chain and assembling the result under SPEC.md 73.2's four section
 * directives - `nasm -f bin -w+error`, `cpu 8086`:
 *
 *     PATH="$SC:$PATH" $SC/smlrcc -tiny -S -SI $SC/v0100/include \
 *          -I $SC/v0100/include -I apps/cword apps/cword/cwrtftbl.c -o t.asm
 *     python3 tools/cc8086.py t.asm -o t8086.asm
 *
 *   .text     455   ovl_rtf_lookup() and the eight accessors
 *   .rodata   343   the 76 keyword names, NUL terminated, unpadded
 *   .data     616   cw_rsym[] 456 (76 x 6) + cw_rrb[] 160 (40 x 4)
 *   .bss        0   there is no mutable state here at all
 *   ----------------
 *   image   1,416   including 2 bytes of section alignment.  2.30% of the
 *                   61,440-byte ceiling.
 *
 * The gate is clean at that size: 9 functions, largest frame 10 bytes against
 * a --max-frame of 96, 22 non-8086 sites lowered (9 imul-imm, 9 leave, 2 movzx,
 * 1 setcc, 1 shift-imm) and nothing refused.
 *
 * ONE KEYWORD IS 10.5 BYTES on average - six for its row and 4.5 for its name -
 * so the 211 rows of Opus's table that are NOT here would have cost about
 * 2.2 KB before a single line of the code that would have to service them. The
 * frozen set, stated in bytes.
 *
 * The 9 `imul-imm` lowerings are worth knowing about. They are the compiler
 * multiplying an index by 6 and by 4 to reach a row, and cc8086.py turns each
 * into a shift-and-add chain rather than a `mul` - which is the right shape
 * here anyway, since `mul` is 118-133 cycles on an 8088 and a shift is 2. Make
 * a row 8 bytes and they all become a single shift; make it 7 and they get
 * worse. Six was chosen because it packs with no padding, and the arithmetic is
 * two shifts and an add.
 *
 * Everything above is .text, .rodata or .data, which means FILE bytes: a table
 * is paid for twice, once on the floppy and once in the region (SPEC.md 73.9 -
 * bss is the cheap half of the ceiling and there is none of it here). The
 * self-check is in none of that total and never will be: it compiles only
 * under CW_RTF_SELFCHECK, which no target build defines and no target build
 * may.
 * ==========================================================================*/
