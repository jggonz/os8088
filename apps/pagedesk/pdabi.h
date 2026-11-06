/* ============================================================================
 * os8088 - apps/pagedesk/pdabi.h          PageDesk's ONE shared contract
 *
 * docs/PAGEDESK-SPEC.md is the authority; this file is its C side and
 * apps/pagedesk/pdabi.inc its assembly side. tools/os88pdabi.py --check
 * (run by the build) fails when the two disagree. OWNED BY THE LEAD: an agent
 * that needs a verb, a field or a code asks for it (PAGEDESK-PLAN agent
 * rules) rather than editing this file.
 *
 * Every struct here crosses a segment through the exchange block (XB,
 * PAGEDESK-SPEC 3.2), so every field is a pdw/pdi/pdb and every struct has
 * an EVEN size pinned by a static assertion below. On the host (PD_HOST)
 * pdw is a 16-bit type so the layouts are identical byte for byte.
 * ==========================================================================*/
#ifndef PDABI_H
#define PDABI_H

#ifdef PD_HOST
typedef unsigned short pdw;     /* 16-bit unsigned */
typedef short          pdi;     /* 16-bit signed   */
#else
typedef unsigned pdw;
typedef int      pdi;
#endif
typedef unsigned char pdb;

/* ---- numbers (PAGEDESK-SPEC 4) ------------------------------------------ */
struct pd_u32 { pdw lo; pdw hi; };          /* unsigned 32               */
struct pd_fx  { pdw lo; pdi hi; };          /* signed 16.16              */
typedef pdi pd_pt;                          /* 1/16 point, +/-2047.94 pt */
typedef pdi pd_px;                          /* device pixel              */
struct pd_mat { struct pd_fx a, b, c, d, e, f; };   /* PDF [a b c d e f]  */
struct pd_rect { pd_pt x0, y0, x1, y1; };   /* page space, x0<=x1 y0<=y1 */
struct pd_prect { pd_px x0, y0, x1, y1; };  /* device, inclusive         */

/* ---- the view (PAGEDESK-SPEC 5.3) --------------------------------------- */
struct pd_view {
    struct pd_rect box;     /* effective box: CropBox ^ MediaBox, pt16     */
    pdw rot;                /* 0, 90, 180, 270 (effective, user incl.)    */
    pdw zoom;               /* percent, 25..400                            */
    pdw asp_num, asp_den;   /* sy = sx * asp_num / asp_den                 */
    pd_px ox, oy;           /* displayed page top-left on screen           */
    pd_px clip_x0, clip_y0, clip_x1, clip_y1;   /* canvas, inclusive      */
};

/* ---- limits (PAGEDESK-SPEC 7-10, 13) ------------------------------------ */
#define PD_MAXTOK       1024
#define PD_MAXSTR       4096
#define PD_MAXNAME      127
#define PD_MAXDEPTH     32
#define PD_MAXITEMS     4096
#define PD_MAXREV       8
#define PD_MAXSUBSEC    256
#define PD_MAXPAGES     1024
#define PD_MAXFILTERS   4
#define PD_MAXGSTACK    16
#define PD_MAXFORMDEPTH 4
#define PD_MAXINLINEIMG 4096
#define PD_BANDROWS     16
#define PD_SLICE_OPS    256
#define PD_THUMBS       12
#define PD_MAXEDITS     256
#define PD_MAXTEXT      1024
#define PD_MAXINKPTS    512     /* per stroke, after simplification       */
#define PD_MAXSTROKES   16      /* per ink annotation                     */
#define PD_UNDO         64
#define PD_MAXPT        32767   /* pt16                                    */

/* ---- errors (PAGEDESK-SPEC 3.6) ----------------------------------------- */
#define PDE_OK          0
#define PDE_SYNTAX      1
#define PDE_XREF        2
#define PDE_TRUNC       3
#define PDE_CYCLE       4
#define PDE_DEPTH       5
#define PDE_FLATE       16
#define PDE_FILTER      17
#define PDE_FONT        18
#define PDE_GRAPHICS    19
#define PDE_ENCRYPT     20
#define PDE_XREFSTM     21
#define PDE_SIGNED      22
#define PDE_RANGE       32
#define PDE_LIMIT       33
#define PDE_NOMEM       34
#define PDE_IO          48
#define PDE_DISKFULL    49
#define PDE_WPROT       50
#define PDE_EXISTS      51
#define PDE_CHANGED     52
#define PDE_NOTFOUND    53
#define PDE_CANCEL      64
#define PDE_BUSY        65      /* a job is still running: call again     */

/* ---- incomplete-rendering features (PAGEDESK-SPEC 10.6), a bitmask ------ */
#define PDF_INC_FONT    0x0001  /* a font was substituted                 */
#define PDF_INC_FILTER  0x0002
#define PDF_INC_IMAGE   0x0004
#define PDF_INC_COLOUR  0x0008
#define PDF_INC_SHADE   0x0010
#define PDF_INC_TRANSP  0x0020
#define PDF_INC_OPER    0x0040  /* an unknown operator                    */
#define PDF_INC_LIMIT   0x0080  /* a limit stopped part of the page       */
#define PDF_INC_RANGE   0x0100  /* a transform overflowed                 */

/* ---- the module header (PAGEDESK-SPEC 3.1) ------------------------------ */
#define PDM_ABI         1
#define PDM_MAGIC_ENG   0x4550  /* 'PE' */
#define PDM_MAGIC_REND  0x5250  /* 'PR' */
#define PDM_MAGIC_WRT   0x5750  /* 'PW' */
#define PDM_ENTRY       8

/* ---- verbs (PAGEDESK-SPEC 3.5) ------------------------------------------
 * In: AL verb, BX/CX/DX as listed. Out: AX (DX), CF=1 + AX=PDE_* on failure.
 * "XB:" names the exchange-block struct read and/or written.            */

/* PDENG 0x00-0x3F */
#define PDV_ENG_INIT        0x00 /* BX=XB seg. XB: pdxb_init            */
#define PDV_ENG_OPEN        0x01 /* XB: pdxb_open in/out. Resumable: AX=PDE_BUSY
                                    means call again (one slice a call)  */
#define PDV_ENG_CLOSE       0x02 /* releases every per-document claim use */
#define PDV_ENG_PAGEINFO    0x03 /* BX=page index. XB: pdxb_pageinfo out */
#define PDV_ENG_OBJ_FIND    0x04 /* XB: pdxb_objref in, pdxb_objinfo out  */
#define PDV_ENG_DICT_GET    0x05 /* XB: pdxb_dictget in/out              */
#define PDV_ENG_STRM_OPEN   0x06 /* XB: pdxb_strm in/out (decode chain)  */
#define PDV_ENG_STRM_READ   0x07 /* BX=dest seg, CX=dest off, DX=max; AX=got,
                                    0 = end                              */
#define PDV_ENG_STRM_CLOSE  0x08
#define PDV_ENG_TOKEN       0x09 /* next token of the open stream. XB:
                                    pdxb_token out (content lexer)       */
#define PDV_ENG_RES_LOOKUP  0x0A /* XB: pdxb_reslookup in/out            */
#define PDV_ENG_RAW_COPY    0x0B /* copy object n's bytes to a sink. XB:
                                    pdxb_rawcopy (writer, graph copy)    */
#define PDV_ENG_TRAILER     0x0C /* XB: pdxb_trailer out                 */
#define PDV_ENG_OUTLINE     0x0D /* XB: pdxb_outline in/out (one level)  */
#define PDV_ENG_ANNOTS      0x0E /* BX=page, CX=index. XB: pdxb_annot out */
#define PDV_ENG_SEARCH      0x0F /* resumable. XB: pdxb_search in/out    */
#define PDV_ENG_FINGERPRINT 0x10 /* XB: pdxb_fingerprint out             */
#define PDV_ENG_ARENA_MARK  0x11 /* AX = mark                            */
#define PDV_ENG_ARENA_REL   0x12 /* BX = mark                            */

/* PDREND 0x40-0x7F */
#define PDV_REND_INIT       0x40 /* BX=XB seg. XB: pdxb_init            */
#define PDV_REND_BEGIN      0x41 /* start a page view. XB: pdxb_render   */
#define PDV_REND_STEP       0x42 /* one slice; AX=PDE_BUSY until done, then
                                    PDE_OK. Draws (takes the gfx lock per
                                    band itself)                         */
#define PDV_REND_ABORT      0x43
#define PDV_REND_TEXTMAP    0x44 /* resumable text pass, no drawing. XB:
                                    pdxb_textmap in/out                  */
#define PDV_REND_THUMB      0x45 /* XB: pdxb_thumb, 1bpp into a seg:off   */
#define PDV_REND_STATUS     0x46 /* XB: pdxb_rstatus out (incomplete bits)*/
#define PDV_REND_FONTBANK   0x47 /* XB: pdxb_fontbank in (D3): the PDRES
                                    index resident, strikes read on demand */

/* PDWRT 0x80-0xBF */
#define PDV_WRT_INIT        0x80 /* BX=XB seg. XB: pdxb_init            */
#define PDV_WRT_SAVE        0x81 /* resumable incremental save. XB:
                                    pdxb_save in/out; edits read from the
                                    edit-store segment named there       */
#define PDV_WRT_NEWDOC      0x82 /* resumable fresh document. XB: pdxb_save*/
#define PDV_WRT_EXTRACT     0x83 /* resumable graph copy. XB: pdxb_save   */
#define PDV_WRT_ABORT       0x84 /* delete the temp file, free buffers   */

/* ---- exchange-block structs ---------------------------------------------
 * The XB is 1 KiB. Offset 0 holds the struct of the current verb. */
#define PD_XB_BYTES 1024

struct pdxb_init {
    pdw xb_seg;             /* the exchange block                         */
    pdw eng_off, eng_seg;   /* far vector to PDENG's door (PDREND, PDWRT) */
    pdw cache_seg, cache_kb;/* PDENG: block cache                         */
    pdw arena_seg, arena_kb;/* PDENG: object arena                        */
    pdw tables_seg, tables_kb; /* PDENG: xref subsections + page table   */
    pdw dl_seg, dl_kb;      /* PDREND: display list + gstate              */
    pdw band_seg, band_bytes;  /* PDREND: one band                        */
    pdw glyph_seg, glyph_kb;/* PDREND: glyph cache                        */
    pdw bank_seg, bank_kb;  /* PDREND: PDRES font bank (resident subset)  */
    pdw wbuf_seg, wbuf_kb;  /* PDWRT: writer buffers                      */
    pdw adapter;            /* VID_* of the window's display              */
    pdw bpp;                /* 1 or 4                                     */
    pdw flags;              /* reserved 0                                 */
};

struct pdxb_open {
    char name[14];          /* 8.3 NUL, resolved in the current folder    */
    pdw  state;             /* 0 on first call; the engine's resume state */
    pdw  npages;            /* out                                        */
    pdw  nrev;              /* out: revisions in the /Prev chain          */
    pdw  version;           /* out: header version x10 (13 = 1.3)         */
    pdw  flags;             /* out: PDO_*                                 */
    struct pd_u32 size;     /* out: file size                             */
    pdw  err_obj;           /* out on failure: the object number at fault */
    pdw  err_detail;        /* out on failure: PDE-specific detail         */
};
#define PDO_READONLY    0x0001  /* signed/certified: view only           */
#define PDO_OUTLINE     0x0002  /* has /Outlines                         */
#define PDO_ACROFORM    0x0004  /* has an AcroForm (not editable on XT)  */
#define PDO_TAGGED      0x0008

struct pdxb_pageinfo {
    pdw  objnum, gen;       /* page identity in the source                */
    struct pd_rect media;   /* inherited, pt16                            */
    struct pd_rect crop;    /* inherited (== media when absent)           */
    pdw  rotate;            /* inherited /Rotate normalised               */
    pdw  nannots;           /* entries in /Annots                         */
    pdw  flags;             /* PDP_*                                      */
};
#define PDP_NOCONTENT   0x0001
#define PDP_HASLINKS    0x0002

struct pdxb_objref { pdw objnum, gen; };
struct pdxb_objinfo {
    pdw  objnum, gen;
    pdw  kind;              /* PDK_*                                      */
    struct pd_u32 offset;   /* of "n g obj"                               */
    struct pd_u32 length;   /* object bytes up to and including endobj    */
};
#define PDK_NULL 0
#define PDK_BOOL 1
#define PDK_INT  2
#define PDK_REAL 3
#define PDK_STR  4
#define PDK_NAME 5
#define PDK_ARR  6
#define PDK_DICT 7
#define PDK_STRM 8
#define PDK_REF  9

/* A dictionary query: the object (or the page's resources) and a key path
 * of up to 3 names; the value comes back as a small typed value. */
struct pdxb_dictget {
    pdw  objnum, gen;       /* in; 0/0 = the trailer                      */
    char key[3][32];        /* in: path, "" terminates                    */
    pdw  kind;              /* out: PDK_*                                 */
    struct pd_fx num;       /* out: INT/REAL/BOOL                         */
    pdw  refnum, refgen;    /* out: REF                                   */
    pdw  len;               /* out: STR/NAME bytes                        */
    char text[128];         /* out: STR/NAME (truncated, len is true)     */
    pdw  count;             /* out: ARR/DICT items                        */
};

struct pdxb_strm {
    pdw  objnum, gen;       /* in                                         */
    pdw  index;             /* in: for a /Contents array, the element     */
    pdw  page;              /* in: page index when objnum == 0 (contents) */
    struct pd_u32 declen;   /* out: decoded length when known, else ~0   */
    pdw  filters;           /* out: PDFL_* chain                          */
};
#define PDFL_HEX   0x0001
#define PDFL_RLE   0x0002
#define PDFL_FLATE 0x0004

struct pdxb_token {
    pdw  kind;              /* PDT_*                                      */
    struct pd_fx num;
    pdw  len;
    char text[256];         /* names/operators; strings in pieces         */
    pdw  more;              /* 1 = a string continues in the next call    */
};
#define PDT_EOF   0
#define PDT_NUM   1
#define PDT_STR   2
#define PDT_NAME  3
#define PDT_OP    4
#define PDT_ARRB  5
#define PDT_ARRE  6
#define PDT_DICTB 7
#define PDT_DICTE 8
#define PDT_TRUE  9
#define PDT_FALSE 10
#define PDT_NULL  11
#define PDT_REF   12

struct pdxb_reslookup {
    pdw  page;              /* in: page index, or 0xFFFF with resobj below */
    pdw  resobj, resgen;    /* in: a Form XObject's own /Resources        */
    char category[16];      /* in: Font, XObject, ExtGState, ...          */
    char name[32];          /* in                                         */
    pdw  objnum, gen;       /* out                                        */
    pdw  kind;              /* out: PDK_*                                 */
};

struct pdxb_rawcopy {
    pdw  objnum, gen;       /* in                                         */
    struct pd_u32 done;     /* in/out: bytes already copied (resumable)   */
    pdw  dseg, doff, dmax;  /* in: destination                            */
    pdw  got;               /* out                                        */
};

struct pdxb_trailer {
    pdw  size;              /* /Size of the newest revision               */
    pdw  root_num, root_gen;
    pdw  info_num, info_gen;
    struct pd_u32 startxref;/* of the newest revision                     */
    pdb  id0[16];           /* first /ID element, 0 if absent              */
    pdw  id0_len;
    pdw  version;           /* x10, header or catalog /Version            */
};

struct pdxb_outline {
    pdw  objnum, gen;       /* in: the item (0/0 = the root's /First)     */
    pdw  next_num, next_gen;/* out */
    pdw  first_num, first_gen; /* out: children */
    pdw  count;             /* out: /Count */
    pdw  dest_page;         /* out: page index, 0xFFFF = none/unsupported */
    char title[64];         /* out: PD Latin                              */
};

struct pdxb_annot {
    pdw  objnum, gen;       /* out */
    char subtype[16];
    struct pd_rect rect;
    char nm[24];            /* /NM when present                           */
    pdw  flags;             /* /F */
    pdw  has_ap;
    pdw  irt_num;           /* /IRT */
    pdw  state;             /* PDS_* from a /StateModel /Review reply      */
    pdw  contents_len;
    char contents[160];     /* truncated preview, PD Latin                 */
};
#define PDS_NONE      0
#define PDS_COMPLETED 1

struct pdxb_search {
    char needle[64];        /* in: PD Latin, matched case-insensitively   */
    pdw  page, glyph;       /* in/out: resume point                        */
    pdw  last_page;         /* in: stop after this page                    */
    pdw  state;             /* in/out: matcher state                       */
    pdw  found;             /* out: 1 = a hit at page/glyph..glyph_end      */
    pdw  glyph_end;
};

struct pdxb_fingerprint {
    struct pd_u32 size;
    pdw  crc_head, crc_tail;
};

struct pdxb_render {
    struct pd_view view;
    pdw  page;              /* page index                                  */
    pdw  win_off;           /* the window record (kernel segment offset),  */
                            /* for OSAPI_WM_CLIP_SET per band              */
    pdw  mono_mark;         /* 17.4 style                                  */
};

struct pdxb_textmap {
    struct pd_view view;    /* for device quads; pt16 quads are canonical  */
    pdw  page;
    pdw  state;             /* in/out: 0 = start                           */
    pdw  dseg, doff, dmax;  /* in: where to put pdtm_glyph records         */
    pdw  count;             /* out: records written this call              */
    pdw  done;              /* out: 1 = the page's text is exhausted       */
};
struct pdtm_glyph {         /* 12 bytes                                   */
    pdb  code;              /* PD Latin                                    */
    pdb  flags;             /* bit0 = first glyph of a new line/run        */
    pd_pt x0, y0, x1, y1;   /* axis-aligned in page space (a rotated text
                               matrix gives the bounding box of the quad)  */
    pdw  run;               /* running glyph index on the page             */
};

struct pdxb_thumb {
    pdw  page;
    pdw  w, h;              /* cell in pixels                              */
    pdw  dseg, doff;        /* 1bpp, stride ceil(w/8)                      */
    pdw  state;             /* in/out: resumable                           */
};

struct pdxb_rstatus {
    pdw  incomplete;        /* PDF_INC_* */
    char first[32];         /* the first thing skipped, by name            */
    pdw  ops;               /* operators interpreted                        */
    pdw  passes;            /* display-list passes (10.2)                  */
};

/* PDRES on demand (DECISIONS D3): the primary reads the PDRES directory,
 * the strike records and the width tables (the INDEX, <= 8 KiB) into one
 * claim; PDREND reads each strike's bytes itself, when first needed, from the
 * package file at part_off + the strike's offset, into the glyph cache. */
struct pdxb_fontbank {
    char pkgname[14];       /* the package file, 8.3, in the launch folder */
    pdw  place_clus, place_vol; /* os88_place of the launch folder      */
    struct pd_u32 part_off; /* file offset of the PDRES part's first byte */
    pdw  index_seg;         /* claim holding PDRES bytes [0, index_len)    */
    pdw  index_len;         /* bytes                                       */
};

struct pdxb_save {
    char target[14];        /* 8.3 output name                             */
    pdw  mode;              /* PDSV_*                                      */
    pdw  state;             /* resumable                                   */
    pdw  edits_seg;         /* the edit store (PAGEDESK-SPEC 13.1)          */
    pdw  edits_count;
    pdw  pages_seg;         /* new page order, pdw per page (13.7)          */
    pdw  pages_count;
    struct pd_u32 written;  /* out: bytes so far                           */
    struct pd_u32 total;    /* out: estimate for OSAPI_FS_OP               */
    pdw  err_detail;
    char source[14];        /* in: 8.3 source an incremental save copies  */
    char date[24];          /* in: "D:YYYYMMDDHHmmSS" or "" (no RTC)      */
    char author[48];        /* in: /T for new notes, "" = none            */
};
#define PDSV_INCREMENTAL 0
#define PDSV_FRESH       1
#define PDSV_EXTRACT     2

/* ---- the edit store record (PAGEDESK-SPEC 13.1), 48 bytes -------------- */
struct pd_edit {
    pdb  kind;              /* PDA_*                                       */
    pdb  flags;             /* PDEF_*                                      */
    pdw  page;              /* page id: source index, or 0x8000|n          */
    struct pd_rect rect;    /* pt16                                        */
    pdw  src_num, src_gen;  /* the source annotation replaced, 0 = new     */
    pdw  id;                /* counter part of /NM                         */
    pdb  colour;            /* PDC_* palette index                         */
    pdb  style;             /* text: family<<4 | bold | italic<<1; line: width */
    pdb  size;              /* text: points; ink: stroke width x2          */
    pdb  align;             /* text: 0 left, 1 centre, 2 right; line: ends */
    pdw  text_off, text_len;/* in the text heap                            */
    pdw  pts_off, pts_n;    /* ink/quads/line points in the point heap      */
    pdw  irt;               /* the note this replies to (review state)      */
    pdw  state;             /* PDS_*                                       */
    pdw  reserved[7];
};
/* Colours (DECISIONS D9): pd_edit.colour is a VGA palette index 0..15,
 * mapped to PDF RGB by the EGA 4-level triple (0, 1/3, 2/3, 1 per channel,
 * index 6 brown = 2/3 1/3 0). 1bpp adapters draw by 17.4's patterns. */
#define PDC_BLACK    0
#define PDC_BLUE     1
#define PDC_GREEN    2
#define PDC_CYAN     3
#define PDC_RED      4
#define PDC_MAGENTA  5
#define PDC_BROWN    6
#define PDC_LGRAY    7
#define PDC_DGRAY    8
#define PDC_LBLUE    9
#define PDC_LGREEN  10
#define PDC_LCYAN   11
#define PDC_LRED    12
#define PDC_LMAGENTA 13
#define PDC_YELLOW  14
#define PDC_WHITE   15
#define PD_PENUP    (-32768)    /* ink: a point with x = PD_PENUP ends a stroke */

#define PDA_NOTE      1
#define PDA_HIGHLIGHT 2
#define PDA_AREA      3
#define PDA_TEXT      4
#define PDA_INK       5
#define PDA_LINE      6
#define PDA_RECT      7
#define PDA_ELLIPSE   8
#define PDA_SIGN      9
#define PDA_PAGEOP    16      /* page operations are in pages_seg          */
#define PDEF_DELETED  0x01
#define PDEF_MOVED    0x02
#define PDEF_DIRTY    0x04
#define PDEF_IRTSRC   0x08    /* irt names a SOURCE object number       */

/* ---- pinned sizes: a layout change is a contract change ---------------- */
typedef char pdabi_sz_u32 [sizeof(struct pd_u32)     ==  4 ? 1 : -1];
typedef char pdabi_sz_fx  [sizeof(struct pd_fx)      ==  4 ? 1 : -1];
typedef char pdabi_sz_mat [sizeof(struct pd_mat)     == 24 ? 1 : -1];
typedef char pdabi_sz_view[sizeof(struct pd_view)    == 28 ? 1 : -1];
typedef char pdabi_sz_edit[sizeof(struct pd_edit)    == 48 ? 1 : -1];
typedef char pdabi_sz_tmg [sizeof(struct pdtm_glyph) == 12 ? 1 : -1];
typedef char pdabi_sz_tok [sizeof(struct pdxb_token) <= PD_XB_BYTES ? 1 : -1];
typedef char pdabi_sz_dg  [sizeof(struct pdxb_dictget) <= PD_XB_BYTES ? 1 : -1];

#endif /* PDABI_H */
