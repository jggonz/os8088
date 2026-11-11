/* ===================================================================== * os8088 - apps/pagedesk/pdabi.h          PageDesk's ONE shared contract
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
/* any PDF (PAGEDESK-SPEC 20.22). PDE_XREFSTM is no longer a refusal: it now
 * means a DAMAGED cross-reference stream. PDE_ENCRYPT now means an encryption
 * PageDesk does not implement (a handler other than /Standard, public-key,
 * an unknown /V or /R); every Standard-handler file opens (20.22.5). */
#define PDE_PASSWORD    23      /* a password is needed, or the one given is
                                   wrong: drives the prompt (20.22.6)      */
#define PDE_NOSLOT      24      /* no RAM for the module slot: the feature is
                                   skipped and the page says Incomplete     */
#define PDE_RANGE       32
#define PDE_LIMIT       33
#define PDE_NOMEM       34
#define PDE_STACK       35      /* the private module stack (20.15) was
                                   overrun: the primary closes the document */
#define PDE_IO          48
#define PDE_DISKFULL    49
#define PDE_WPROT       50
#define PDE_EXISTS      51
#define PDE_CHANGED     52
#define PDE_NOTFOUND    53
#define PDE_CANCEL      64
#define PDE_BUSY        65      /* NOT a failure: CF=0, AX=PDE_BUSY means
                                   the job continues - call again (D11)  */
#define PDE_NEEDMOD     66      /* NOT a failure: CF=0, AX=PDE_NEEDMOD, DX =
                                   the PDX_* module the verb needs in the
                                   slot. The primary loads it, tells the
                                   caller (PDV_REND_SLOT / PDV_ENG_SLOT) and
                                   calls the SAME verb again (20.22.2)      */
#define PDE_NEEDMEM     67      /* NOT a failure: CF=0, AX=PDE_NEEDMEM, CX =
                                   PDMEM_* region, DX = KiB wanted. The
                                   primary claims it (or refuses the open
                                   with PDE_NOMEM), re-INITs the module with
                                   the region in pdxb_init and calls the
                                   SAME verb again (20.22.2)                */

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
#define PDF_INC_MODULE  0x0200  /* a slot module could not be loaded (no
                                   RAM, no slot, a read failed): 20.22.2   */
#define PDF_INC_APPROX  0x0400  /* drawn approximately: no 8-bit band RAM
                                   for transparency (20.22.10)             */

/* ---- the private module stack (PAGEDESK-SPEC 20.15, DECISIONS D23) -----
 * Every module verb runs on a 2 KiB stack carved from claim B, not on the UI
 * task's 512 bytes. Its top 8 bytes are a RECORD the primary writes at each
 * swap down; a module's kernel-call gate (pdkgate.inc) reads it through SS to
 * swap back to the UI stack for the kernel call. Its bottom 4 bytes are a
 * canary the primary checks at every outermost return. */
#define PD_DSTK_BYTES   2048    /* claim B's PDU_DC_DSTK row              */
#define PD_DSTK_UISP    2040    /* record: the UI SP to swap back to      */
#define PD_DSTK_UISS    2042    /*         the UI SS                      */
#define PD_DSTK_SELF    2044    /*         the deep stack's own SS        */
#define PD_DSTK_MAG     2046    /*         PD_DSTK_MAGIC                  */
#define PD_DSTK_MAGIC   0x4B53  /* 'SK'                                   */
#define PD_DSTK_CAN0    0x5350  /* the canary at offset 0 ...             */
#define PD_DSTK_CAN1    0x4B44  /* ... and at offset 2 ('PSDK')           */
#define PD_DSTK_FILL    0xCC    /* everything between, at each carve      */

/* ---- the module header (PAGEDESK-SPEC 3.1) ------------------------------ */
#define PDM_ABI         2      /* 2: any PDF (20.22) grew pdxb_* structs */
#define PDM_MAGIC_ENG   0x4550  /* 'PE' */
#define PDM_MAGIC_REND  0x5250  /* 'PR' */
#define PDM_MAGIC_WRT   0x5750  /* 'PW' */
/* the slot modules (PAGEDESK-SPEC 20.22.2): lazy parts that share ONE slot */
#define PDM_MAGIC_KEY   0x4B50  /* 'PK' PDXK: key derivation, at open     */
#define PDM_MAGIC_GFX   0x4750  /* 'PG' PDXG: colour, functions, shading,
                                   patterns, ExtGState, dashes             */
#define PDM_MAGIC_FNT   0x4650  /* 'PF' PDXF: font programs, CMaps        */
#define PDM_MAGIC_IMG   0x4950  /* 'PI' PDXI: DCT, CCITT, JBIG2           */
#define PDM_MAGIC_JPX   0x4A50  /* 'PJ' PDXJ: JPEG 2000                   */
#define PDX_NONE        0       /* slot module ids (DX of PDE_NEEDMOD)    */
#define PDX_KEY         1
#define PDX_GFX         2
#define PDX_FNT         3
#define PDX_IMG         4
#define PDX_JPX         5
#define PDX_COUNT       6
#define PDMEM_SLOT      0       /* the slot WORKSPACE (grow S, 20.22.2)   */
#define PDMEM_XIDX      1       /* PDENG: flat xref index, 4 B an object  */
#define PDMEM_OSW       2       /* PDENG: the object-stream window        */
#define PDMEM_XC        3       /* PDREND: the extension cache            */
#define PDMEM_X8        4       /* PDREND: 8-bit band, layer and mask     */
#define PDMEM_GLYPH     5       /* PDREND: GROW the glyph claim, DX = KiB */
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
#define PDV_ENG_CRYPTINFO   0x13 /* XB: pdxb_crypt out (the /Encrypt dict) */
#define PDV_ENG_SETKEY      0x14 /* XB: pdxb_crypt in (key, methods); the
                                    open then resumes with PDV_ENG_OPEN    */
#define PDV_ENG_CRYPT       0x15 /* XB: pdxb_cryptop in/out: encrypt or
                                    decrypt a far buffer with an EXPLICIT
                                    key (the writer; no document state)    */
#define PDV_ENG_SLOT        0x16 /* BX = PDX_* resident, CX:DX = its door
                                    (off:seg); 0 = none                    */
/* 0x17-0x1F reserved for eng (20.22.3); 0x20-0x27 for crypt (20.22.5)    */

/* PDREND 0x40-0x7F */
#define PDV_REND_INIT       0x40 /* BX=XB seg. XB: pdxb_init            */
#define PDV_REND_BEGIN      0x41 /* start a page view. XB: pdxb_render   */
#define PDV_REND_STEP       0x42 /* one slice; AX=PDE_BUSY until done, then
                                    PDE_OK. Draws (takes the gfx lock per
                                    band itself)                         */
#define PDV_REND_ABORT      0x43 /* CX=1: XB word 0 out = first row not blitted; CX=0 as before */ /* verify-polish */
#define PDV_REND_TEXTMAP    0x44 /* resumable text pass, no drawing. XB:
                                    pdxb_textmap in/out                  */
#define PDV_REND_THUMB      0x45 /* XB: pdxb_thumb, 1bpp into a seg:off   */
#define PDV_REND_STATUS     0x46 /* XB: pdxb_rstatus out (incomplete bits)*/
#define PDV_REND_FONTBANK   0x47 /* XB: pdxb_fontbank in (D3): the PDRES
                                    index resident, strikes read on demand */
#define PDV_REND_SLOT       0x48 /* BX = PDX_* resident, CX:DX = its door
                                    (off:seg); 0 = none. STEP, TEXTMAP and
                                    THUMB may answer PDE_NEEDMOD (20.22.2) */
/* 0x49-0x4F reserved for rcore (20.22.7)                                  */

/* PDWRT 0x80-0xBF */
#define PDV_WRT_INIT        0x80 /* BX=XB seg. XB: pdxb_init            */
#define PDV_WRT_SAVE        0x81 /* resumable incremental save. XB:
                                    pdxb_save in/out; edits read from the
                                    edit-store segment named there       */
#define PDV_WRT_NEWDOC      0x82 /* resumable fresh document. XB: pdxb_save*/
#define PDV_WRT_EXTRACT     0x83 /* resumable graph copy. XB: pdxb_save   */
#define PDV_WRT_ABORT       0x84 /* delete the temp file, free buffers   */
/* 0x85-0x8F reserved for wrt (20.22.4)                                    */

/* the slot occupants 0xC0-0xFF (PAGEDESK-SPEC 20.22.2). One occupant at a
 * time; each answers only its own range and PDV_X_INIT.                  */
#define PDV_X_INIT          0xC0 /* every occupant: BX = XB seg. XB:
                                    pdxb_init; slot_seg/slot_kb are ITS
                                    workspace, valid until the next swap   */
#define PDV_KEY_AUTH        0xC1 /* resumable (PDE_BUSY). XB: pdxb_crypt
                                    in/out. PDE_PASSWORD = wrong password  */
#define PDV_KEY_COST        0xC2 /* XB: pdxb_crypt in. AX = estimated
                                    seconds for ONE attempt on this CPU    */
/* 0xC3-0xC7 reserved for crypt; 0xC8-0xD7 for gfx and blend (PDXG);
 * 0xD8-0xE7 for fonts (PDXF)                                              */
#define PDV_IMG_OPEN        0xE8 /* PDXI and PDXJ. XB: pdxb_img in/out     */
#define PDV_IMG_ROW         0xE9 /* BX=dest seg, CX=dest off, DX=row (rows
                                    ascend; a lower row needs a new OPEN).
                                    Writes pdxb_img.rowbytes bytes          */
#define PDV_IMG_CLOSE       0xEA
/* 0xEB-0xEF reserved for the image agents (dct, bilevel, jpx)            */

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
    pdw flags;              /* PDXI_*                                     */
    pdw flate_seg, flate_kb;/* PDENG (D17): Flate slots, 43 KiB each, 1-2;
                               valid only with PDXI_FLATE                 */
    /* any PDF (20.22.2); 0 = none. A field the primary has not claimed is 0 */
    pdw slot_seg, slot_kb;  /* the slot occupant's workspace (PDV_X_INIT)  */
    pdw xidx_seg, xidx_kb;  /* PDENG: flat xref index (PDMEM_XIDX)         */
    pdw osw_seg, osw_kb;    /* PDENG: object-stream window (PDMEM_OSW)     */
    pdw xc_seg, xc_kb;      /* PDREND: extension cache (PDMEM_XC)          */
    pdw x8_seg, x8_kb;      /* PDREND: 8-bit band + layer + mask (PDMEM_X8)*/
    pdw cpu;                /* OSAPI_CPU_INFO's class (CPU_8086 = an XT)   */
    pdw rend_off, rend_seg; /* far vector to PDREND's door (occupants)     */
};
#define PDXI_FLATE  0x0001  /* flate_seg/flate_kb are valid (PD-25)       */

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
#define PDO_ENCRYPTED   0x0010  /* /Encrypt: a key was set (20.22.5)     */
#define PDO_XREFSTM     0x0020  /* a cross-reference stream was read      */
#define PDO_OBJSTM      0x0040  /* objects live in object streams         */
#define PDO_REPAIRED    0x0080  /* the xref was rebuilt by scanning       */

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
    pdw  accept;            /* in: PDSA_*; 0 = as before (a codec refuses) */
    pdw  codec;             /* out: PDCD_* the chain stops before, 0 none  */
};
#define PDFL_HEX   0x0001
#define PDFL_RLE   0x0002
#define PDFL_FLATE 0x0004
#define PDFL_A85   0x0008
#define PDFL_LZW   0x0010
#define PDFL_CRYPT 0x0020   /* the stream is decrypted (20.22.5)          */
#define PDSA_CODEC 0x0001   /* stop before DCT/CCITT/JBIG2/JPX and read the
                               CODED bytes (the image modules, 20.22.11) */
#define PDCD_NONE  0
#define PDCD_DCT   1
#define PDCD_CCITT 2
#define PDCD_JBIG2 3
#define PDCD_JPX   4

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
    pdw  mode;              /* in: PDRC_*                                 */
};
#define PDRC_EXACT 0        /* the file's bytes (as before)               */
#define PDRC_PLAIN 1        /* "n g obj ... endobj" synthesised: an object-
                               stream member wrapped, strings and stream
                               decrypted, /Length corrected (20.22.3-5)   */

struct pdxb_trailer {
    pdw  size;              /* /Size of the newest revision               */
    pdw  root_num, root_gen;
    pdw  info_num, info_gen;
    struct pd_u32 startxref;/* of the newest revision                     */
    pdb  id0[16];           /* first /ID element, 0 if absent              */
    pdw  id0_len;           /* up to 32 since ABI 2: bytes 16-31 in id0_hi */
    pdw  version;           /* x10, header or catalog /Version            */
    pdb  id0_hi[16];        /* /ID[0] bytes 16..31 (written back WHOLE)    */
    pdw  encrypt_num, encrypt_gen; /* /Encrypt ref; 0/0 none or direct    */
    pdw  tflags;            /* PDTR_*                                      */
};
#define PDTR_XREFSTM  0x0001  /* the newest section is a cross-reference stream */
#define PDTR_ENCDIRECT 0x0002 /* /Encrypt is a direct dictionary           */

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
    pdw  edits_seg;         /* wave 2 (D15): the edit store (13.1.1), 0=none*/
    pdw  edits_count;
    pdw  rflags;            /* PDRF_*                                      */
    pdw  hide;              /* with PDRF_NOEDITID: the record not drawn    */
};
#define PDRF_ANNOTS   0x0001  /* draw the source's annotations (/AP /N)      */
#define PDRF_EDITS    0x0002  /* draw the edit store's records for this page */
#define PDRF_NOEDITID 0x0004  /* with edits: skip record index `hide` (drag) */

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
    pdw  mods;              /* bit (1 << PDX_*) per slot module the page used */
    pdw  swaps;             /* slot loads this page cost (20.22.2)          */
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
    pdw  src_clus, src_vol; /* in (D14): the source's folder, for a Save As
                               into another folder; 0,0 = the current one  */
};
#define PDSV_INCREMENTAL 0
#define PDSV_FRESH       1
#define PDSV_EXTRACT     2

/* a fresh document's page, PDV_WRT_NEWDOC's page list at pages_seg:0
 * (PAGEDESK-SPEC 14.6, DECISIONS D11) */
struct pd_newpage {
    pdw w, h;            /* MediaBox [0 0 w h], pt16                        */
    pdw rotate;          /* 0 / 90 / 180 / 270                              */
    pdw cseg, clen;      /* content bytes at cseg:0, clen = 0 for a blank   */
};

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

/* ---- any PDF: encryption (PAGEDESK-SPEC 20.22.5) ----------------------- */
struct pdxb_crypt {         /* CRYPTINFO out; KEY_AUTH in/out; SETKEY in  */
    pdw  v, r, length;      /* /V /R, /Length in bits (40..256)           */
    pdw  p_lo, p_hi;        /* /P                                          */
    pdw  stm, str, eff;     /* PDCM_* for streams, strings, embedded files */
    pdw  encmeta;           /* /EncryptMetadata (default 1)                */
    pdw  o_len, u_len;      /* 32 (R2-R4) or 48 (R5-R6)                    */
    pdb  o[48], u[48];      /* /O /U                                       */
    pdb  oe[32], ue[32];    /* /OE /UE (R5-R6)                             */
    pdb  perms[16];         /* /Perms (R5-R6)                              */
    pdb  id0[32];           /* /ID[0], whole                               */
    pdw  id0_len;
    pdw  pw_len;            /* in: password bytes (0 = the empty password) */
    pdb  pw[128];           /* in: as typed; R6 takes it as UTF-8 (SASLprep
                               of ASCII is the identity)                   */
    pdb  key[32];           /* out of KEY_AUTH, in to SETKEY               */
    pdw  key_len;           /* bytes: 5..16, or 32 for AESV3               */
    pdw  auth;              /* out: PDCA_*                                 */
    pdw  state;             /* in/out: 0 on the first call (resumable)     */
    pdw  progress;          /* out: 0..1000 of this attempt                */
};
#define PDCM_NONE     0
#define PDCM_RC4      1
#define PDCM_AESV2    2     /* AES-128-CBC                                 */
#define PDCM_AESV3    3     /* AES-256-CBC                                 */
#define PDCM_IDENTITY 4
#define PDCA_NONE     0
#define PDCA_USER     1
#define PDCA_OWNER    2

struct pdxb_cryptop {       /* PDV_ENG_CRYPT: one chunk, in place          */
    pdb  key[32];           /* the FILE key (from SETKEY's pdxb_crypt)    */
    pdw  key_len;
    pdw  method;            /* PDCM_*                                      */
    pdw  objnum, gen;       /* the object the bytes belong to              */
    pdw  dir;               /* 0 decrypt, 1 encrypt                        */
    pdw  seg, off, len;     /* in: the bytes; out: len = bytes now there   */
    pdw  room;              /* in: bytes the buffer may grow to (AES adds the
                               IV and the padding on the last chunk)       */
    pdw  phase;             /* in: bit0 first chunk, bit1 last chunk       */
    pdb  iv[16];            /* in (encrypt, first chunk): the IV to use;
                               carried between chunks by the engine        */
    pdb  carry[16];         /* engine-private between chunks               */
    pdw  ncarry;
    pdb  rc4[258];          /* engine-private RC4 state between chunks     */
};

/* ---- any PDF: image modules (PAGEDESK-SPEC 20.22.11-13) --------------- */
struct pdxb_img {
    pdw  objnum, gen;       /* in: the image XObject; 0/0 = inline (below) */
    pdw  iseg, ioff, ilen;  /* in: an inline image's CODED bytes           */
    pdw  codec;             /* in: PDCD_* (pdxb_strm.codec)                */
    pdw  w, h;              /* in: /Width /Height                          */
    pdw  bpc, ncomp;        /* in: /BitsPerComponent; components of the
                               /ColorSpace (0 = take the codec's)          */
    pdw  want_w, want_h;    /* in: device pixels the image covers; the
                               module picks the largest reduction that
                               still gives at least this many             */
    pdw  flags;             /* in: PDIMG_*                                 */
    pdw  out_w, out_h;      /* out: what IMG_ROW delivers                  */
    pdw  out_mode;          /* out: PDIM_*                                 */
    pdw  out_ncomp;         /* out: samples a pixel for PDIM_N8            */
    pdw  rowbytes;          /* out: <= 1024                                */
    pdw  scale;             /* out: log2 of the reduction                  */
    pdw  state;             /* module-private                              */
};
#define PDIMG_MASK   0x0001 /* /ImageMask or an SMask: one grey/1-bit plane */
#define PDIMG_INVERT 0x0002 /* /Decode inverts                             */
#define PDIM_GREY8   0      /* one byte a pixel, 0 black .. 255 white       */
#define PDIM_BITS1   1      /* 1 bit a pixel, MSB first, 1 = white          */
#define PDIM_N8      2      /* out_ncomp bytes a pixel in the image's own
                               colour space: the core maps them through
                               the colour-space LUTs (20.22.9)             */

/* ---- reserved blocks: each agent defines its own structs ONLY here ------ */
/* [eng 20.22.3]    */
#define PDRL_DIRECT  0x0001 /* pdxb_reslookup_x.rflags: the resource is written
                               DIRECTLY in another object (an inline dictionary) */
struct pdxb_reslookup_x {   /* RES_LOOKUP's exchange block, extended: the verb
                               reads the pdxb_reslookup and writes all of this */
    struct pdxb_reslookup rl;
    pdw  rflags;            /* out: PDRL_*                                */
    pdw  cnum, cgen;        /* out (PDRL_DIRECT): the object it is written in */
    struct pd_u32 coff;     /* out (PDRL_DIRECT): where the value starts in that
                               object's PDRC_EXACT RAW_COPY bytes (20.22.3.8) */
};
/* [crypt 20.22.5]  */
#define PDRC_STRCIPHER 0x0100 /* OR-ed into pdxb_rawcopy.mode on return from RAW_COPY PDRC_PLAIN of an object of an ENCRYPTED document:
                                 the dictionary's strings are still ciphertext, encrypted under (objnum, gen) with the document's string
                                 method (pdxb_crypt.str of CRYPTINFO); the caller decrypts them (20.22.5). The
                                 flag rides in the HIGH byte, so a caller that feeds the struct back
                                 unchanged is still PDRC_PLAIN: the engine compares (mode & 0xFF) */
#define PDV_KEY_PARAMS 0xC3 /* XB: pdxb_crypt out: the file's /Encrypt parameters, no password tried (tests, the R6 question) */
/* [wrt 20.22.4]    */
#define PDE_WRITE 54        /* a create / write / close / rename of the OUTPUT refused for a reason that is not full, protected or exists (F30) */
#define PDV_WRT_KEYS        0x85 /* BX=XB seg. XB: pdxb_wkeys in. After PDV_WRT_INIT,
                                    before the job: the keys of the ENCRYPTED foreign
                                    files an Insert reads (plat's pre-flight, 20.22.4.5) */
struct pdxb_wkey {
    pdw  slot;              /* the Insert slot (0..2) of the file             */
    pdw  key_len, stm, str, eff, encmeta;   /* what PDV_ENG_SETKEY takes      */
    pdb  key[32];
};
struct pdxb_wkeys { pdw n; struct pdxb_wkey k[3]; };
#define PDRC_ENCDICT 0xFFFE /* PDV_ENG_RAW_COPY objnum: the text of a DIRECT /Encrypt
                               dictionary ("<< ... >>"), when pdxb_trailer.tflags has
                               PDTR_ENCDIRECT (PDRC_EXACT or PLAIN: same bytes)        */
/* [rcore 20.22.7]  */
/* PDV_GFX_DASH (0xCE, PDXG; the walker is rcore's pdxdash.c, 20.22.7): BX:CX =
 * this block (PDREND puts it at XB offset 0), DX unused. The core fills `in`
 * with st = 0 and calls; AX = 1: one piece is in `out` (stroke it as an OPEN
 * polyline and call again), 2: the pattern is too fine to tell from solid
 * (stroke the subpath whole), 0: done. CF = 1 is a failure: stroke solid. */
#define PDXD_NPT    12      /* points in a piece                           */
#define PDXD_NDASH  8       /* a pattern's elements at most (the `d` operator, 20.12.4) */
struct pdxb_dashout {
    pdw  on;                /* points in ox/oy (1 = a dot)                  */
    pdi  ox[12], oy[12];     /* qpx (PDXD_NPT)                           */
};
struct pdxb_dashin {
    pdw  vseg, xs, ys;      /* the subpath: vseg:xs and vseg:ys, pdi, qpx (any segment: PDREND lends its list's top) */
    pdw  n, closed;         /* vertices (>= 1); 1 = closed                  */
    struct pd_fx scale;     /* qpx per user unit (the CTM's)                */
    pdw  dph;               /* GS.dph: phase, quarter points, reduced       */
    pdb  dn, pad_;          /* elements 1..PDXD_NDASH                       */
    pdb  dash[8];           /* lengths, quarter points (PDXD_NDASH)         */
    pdw  st;                /* 0 on the first call; the walker's after      */
};
struct pdxb_dash {
    struct pdxb_dashout out;
    struct pdxb_dashin in;
    pdw  i, didx, don, dpc, dpn;       /* the walker's state between calls */
    pdi  drem, pos, slen, qx, qy, x0, y0, x1, y1;
    pdi  len[8];            /* PDXD_NDASH */
    pdi  dpx[12], dpy[12];  /* PDXD_NPT */
};
typedef char pdabi_sz_dsh[sizeof(struct pdxb_dash) <= PD_XB_BYTES ? 1 : -1];
/* PDV_GFX_EDIT (0xCF provisionally, PDXG; rcore's pdxedit.c, 20.22.7): the
 * marks of ONE edit-store record (13.3) as a list of drawing ops. BX:CX = this
 * block (XB offset 0), DX unused. The core fills it with skip = 0 and st[] = 0
 * at the record's start and hands the block back unchanged between calls; each
 * call writes `n` words of ops at XB offset PDXE_OPS - ending at the first
 * PAINT, so the core may stroke (and dash, which uses the XB) last - with
 * more = 1 while the record goes on. AX 0; CF = 1 a failure (the record is
 * dropped). The ops, a word each and its arguments (page space, pt16): 1
 * MOVE x y, 2 LINE x y, 3 INK x y (a LINE that strokes what is there first
 * when the core's path is near full), 4 CURVE x1 y1 x2 y2 x3 y3, 5 CLOSE,
 * 6 PAINT (bit 8 fill, 9 stroke, 10 the close flag), 7 SETB v (bits 8-15 a
 * field of the graphics state: 0 stroke lum, 1 stroke c4, 2 fill lum, 3 fill
 * c4, 4 dash count, 5-6 dash lengths, 7-8 dash phase low/high, 9 cap, 10
 * join), 8 LW w16. */
/* PDV_REND_CODE (rcore's 0x49, 20.22.7.9): PDREND's second code segment, the
 * part PDRENDC, is bound. BX = its CODE selector (OS88_CODESEL'd under
 * kern_mega; the segment itself on a real-mode kernel), CX = its DATA
 * segment. Sent after the part is read and before any other verb, and again
 * whenever its claim moves. CF = 1 PDE_RANGE: not this build's part, and
 * until a PDV_REND_CODE succeeds every other verb answers that. */
#define PDV_REND_CODE 0x49
#define PDXE_OPS    64      /* XB offset of the ops                        */
struct pdxb_emark {
    pdw  eseg;              /* the edit store's records (pdxb_render)      */
    pdw  rec;               /* this record's index                         */
    pdw  bpp, mono;         /* the page's depth; pdxb_render.mono_mark     */
    pdw  skip;              /* ops the core already has of this record     */
    pdw  st[4];             /* the generator's place, 0 at the start       */
    pdw  n, more;           /* out: words of ops; 1 = call again           */
};
/* [gfx 20.22.9]    */
/* ---- gfx (PAGEDESK-SPEC 20.22.9): PDXG's verbs and its cache records ----- */
#define PDV_GFX_CS      0xC8 /* compile a colour space. XB: pdxb_gfx           */
#define PDV_GFX_COLOR   0xC9 /* one operand set through a MODULE space          */
#define PDV_GFX_LUT     0xCA /* an image colour-space LUT                       */
#define PDV_GFX_SHADE   0xCB /* a shading as flat polygon records (PDGX_REC_*)  */
#define PDV_GFX_PAT     0xCC /* a tiling pattern's parameters                   */
#define PDV_GFX_FN      0xCD /* evaluate one function                           */
#define PDV_GFX_DASH    0xCE /* rcore's dash walker (pdxdash.c)                 */
#define PDV_GFX_EDIT    0xCF /* rcore's edit-store marks (pdxedit.c)            */
#define PDGX_CSBYTES    1264 /* sizeof(struct pdgx_cs): an XCK_CS entry          */
#define PDGX_LUTBYTES   1028 /* sizeof(struct pdgx_lut): an XCK_LUT entry        */
#define PDGX_SRCMAX     480
#define PDGX_K_GRAY 1
#define PDGX_K_RGB 2
#define PDGX_K_CMYK 3
#define PDGX_K_CALGRAY 4
#define PDGX_K_CALRGB 5
#define PDGX_K_LAB 6
#define PDGX_K_INDEXED 7
#define PDGX_K_SEP 8
#define PDGX_K_DEVN 9
#define PDGX_K_PATTERN 10
#define PDGX_F_TABLE    0x01 /* rgb[] is valid: a 1-component space           */
#define PDGX_F_RGBLIKE  0x02 /* operands are device RGB (ICC N=3)              */
#define PDGX_F_CMYKLIKE 0x04 /* operands are device CMYK (ICC N=4)             */
#define PDGX_F_MODULE   0x08 /* convert with PDV_GFX_COLOR                     */
#define PDGX_F_NONE     0x10 /* Separation /None: paints nothing               */
#define PDGX_F_PATTERN  0x20 /* a Pattern space; n = the underlying's (0 = none) */
struct pdgx_cs {
    pdb  kind, n, flags, pad;
    pdw  hival;                 /* Indexed                                    */
    pdw  srcobj, srcgen;        /* the source object, 0 = the text in src[]   */
    pdw  srclen;                /* bytes of src[]                             */
    pdw  rsv[2];
    pdb  src[480];
    pdb  rgb[768];              /* index (or round(c * 255)) -> r, g, b       */
};
#define PDGX_LUT_INVERT 0x01    /* grey = 255 - sat(sum of the tables)         */
struct pdgx_lut {
    pdb  ncomp, flags;
    pdw  pad;
    pdb  t[1024];             /* four tables of 256 */
};
/* PDV_GFX_SHADE paints a shading as FLAT POLYGONS (20.22.9): a gradient is bands, a mesh is triangles cut into
 * flat sub-triangles, so PDREND needs no shading rasteriser - each record is one ordinary fill. The answer is a
 * run of records in the caller's buffer (rseg:roff, rlen bytes, ntri records):
 *     pdb r, g, b;  pdb flags (bit 0: a CLIP polygon, the shading's /BBox - clip to it, fill nothing);
 *     pdb npts;     pdb n1  (0, or the length of the first contour: the rest is a second contour, wound the
 *                            other way - a ring);   then npts x { pdi x, y }  device quarter-pixels,
 *     already through the CTM and clamped to +-16000. Fill rule nonzero. More to come: AX = PDE_BUSY and state. */
#define PDGX_REC_HDR   6
#define PDGX_REC_CLIP  0x01
struct pdxb_gfx {
    pdw  objnum, gen;           /* in: the object; 0 = the text below          */
    pdw  tseg, toff, tlen;      /* in: direct syntax (<= 1,024 bytes)           */
    pdw  rseg, roff, rcap;      /* in: the result buffer                        */
    pdw  rlen;                  /* out: bytes written there                     */
    pdw  flags, bpc, state;
    pdw  cseg, coff;            /* PDV_GFX_COLOR: the pdgx_cs entry             */
    pdw  ncomp, kind, ntri, ok; /* out                                          */
    struct pd_fx comp[8];       /* in: operands or /Decode; out: PDV_GFX_FN     */
    pdb  rgb[4];                /* out: r g b, byte 3 = 1 when nothing is painted */
    struct pd_mat ctm;          /* SHADE: user space -> device quarter-pixels   */
    pdi  clip[4];               /* SHADE: the device clip in pixels, inclusive: x0 y0 x1 y1 */
    pdw  steps;                 /* SHADE: bands over a gradient's range (0 = 32) */
};
/* PDV_GFX_PAT (20.22.9). Step 1 (the pattern's name, as PDGXI_RES with PDGXI_CAT_PAT) writes a struct pdgx_pat
 * to rseg:roff. Step 2 (PDGXI_TILES; the entry at cseg:coff, ctm = the page's DEFAULT matrix, clip = the device
 * clip) answers ctm = the pattern matrix x the default matrix (a tiling pattern: that of its first tile); a tiling pattern also the tile counts ni = ncomp,
 * nj = kind (0: too many, draw nothing) and comp[0..3] = the device step of one tile along i (x y) and along j (x y),
 * comp[4..5] = the first tile's i, j; a shading pattern is painted with PDV_GFX_SHADE on
 * the entry's objnum/gen and that ctm. */
#define PDGX_PATBYTES  56
#define PDGX_PAT_OBJ   4        /* byte offsets of struct pdgx_pat that PDREND reads from the cache entry */
#define PDGX_PAT_BBOX  32
#define PDGX_PAT_DIRECT 0x01    /* a shading pattern whose /Shading is direct: not supported (objnum = 0) */
#define PDGX_TILEMAX   40       /* tiles in one fill */
struct pdgx_pat {
    pdb  ptype, paint, flags, pad;      /* /PatternType (1 tiling, 2 shading), /PaintType (1 coloured, 2 not) */
    pdw  objnum, gen;                   /* tiling: the pattern's stream; shading: the /Shading object          */
    struct pd_fx matrix[6];             /* /Matrix                                                            */
    struct pd_fx bbox[4];               /* tiling: /BBox                                                      */
    struct pd_fx xstep, ystep;          /* tiling                                                             */
};
#define PDGXI_DECODE 0x0001     /* comp[] holds /Decode, 2 per component        */
#define PDGXI_CAT_CS  0x0100     /* with PDGXI_RES: the resource category, flags bits 8-11 (tlen counts the name and its NUL) */
#define PDGXI_CAT_SH  0x0200
#define PDGXI_CAT_PAT 0x0300
#define PDGXI_TILES  0x0004     /* PDV_GFX_PAT step 2 */
#define PDGXI_IMGCS  0x0008     /* the source is the /ColorSpace VALUE of the image dictionary objnum/gen (PDV_GFX_LUT) */
#define PDGXI_RES    0x0002     /* the source is a RESOURCE: objnum/gen = the Form (0 = the page whose
                                   index is bpc), the name is at tseg:toff               */
typedef char pdgx_sz_cs [sizeof(struct pdgx_cs)    == PDGX_CSBYTES  ? 1 : -1];
typedef char pdgx_sz_lut[sizeof(struct pdgx_lut)   == PDGX_LUTBYTES ? 1 : -1];
typedef char pdgx_sz_pat[sizeof(struct pdgx_pat)   == PDGX_PATBYTES ? 1 : -1];
/* [blend 20.22.10] */
/* PDX8, the transparency part (20.22.10.11): loaded into the PDMEM_X8 region at its
 * base, door at x8_seg:8, planes at x8_seg:PDX8_RES. It holds the ExtGState parse, the
 * tuple pool and the record writers (interpretation time) and the compositor (raster
 * time). 0xD0 is free again (PDXG no longer carries pdxgtr.c). */
#define PDM_MAGIC_X8    0x3850  /* 'P8' */
#define PDX8_DIRECT     0xFFFD  /* PDV_X8_GS DX / DICT_GET gen: the dictionary is DIRECT in object CX; XB+0 (GS) or XB+sizeof(pdxb_dictget) (DICT_GET) holds { real gen, coff lo, coff hi } (RES_LOOKUP's PDRL_DIRECT answer) */
#define PDX8_RES        16384   /* the part's image + bss at most; the planes follow it */
#define PDX8_ENGVEC     12      /* header +12: eng_off, +14: eng_seg (plat binds them at load) */
#define PDV_X8_INIT     0xD1    /* BX = the region's segment, CX = its KiB, DX = the XB seg */
#define PDV_X8_CTL      0xD2    /* BX = X8C_*; CX, DX                       */
#define PDV_X8_SPAN     0xD3    /* BX = y, CX = xa, DX = xb (band relative) */
#define PDV_X8_GS       0xD4    /* BX = tuple in force, CX = ExtGState obj, DX = gen */
#define PDV_X8_REC      0xD5    /* BX = DL offset of an XSTATE/GROUP record */
#define PDV_X8_IMG      0xD6    /* BX = X8I_*                               */
#define PDV_X8_DEF      0xD7    /* BX = X8D_*: the record writers           */
#define X8C_BAND        0       /* XB: pdxb_x8band                          */
#define X8C_BEGIN       1       /* CX = y0, DX = y1: a strip of at most 8 rows */
#define X8C_END         2       /* quantise the strip into the band; out CX = X8F_* */
#define X8C_LUM         3       /* CX = the colour of the marks that follow */
#define X8C_MKEND       4       /* the mask block ended; out AX = resume offset */
#define X8C_PASS        5       /* interpretation begins: CX = the viewport's top row */
#define X8C_WANT        6       /* CX..DX = the rows of a band: AX = 1 when any is composed in 8 bits */
#define X8I_SET         0       /* XB: pdxb_x8img                           */
#define X8I_ROW         1       /* CX = y; XB: pdxb_x8row                   */
#define X8I_END         2
#define X8D_STATE       0       /* CX = 0 fill / 1 stroke, DX = the tuple: out AX = bytes of XSTATE record at XB (0 none), DX bit 0 = the mark is transparent, bit 1 = transparency is live */
#define X8D_MARK        1       /* CX = y0, DX = y1: rows a transparent mark touches */
#define X8D_GROUP       2       /* XB: pdxb_x8grp: a Form begins; out AX = bytes of record at XB, CX bit 0 = reset the last STATE, bit 1 = the tuple is now 0 */
#define X8D_GEND        3       /* CX = the frame, DX = DL length: out AX = bytes of record at XB; CX = the MASKB offset to patch (0 none), DX = the length to write there */
#define X8D_NOMASK      4       /* the mask's form would not open: out AX = the tuple without it */
#define X8F_LIMIT       1       /* PDF_INC_LIMIT                            */
#define X8F_TRANSP      2       /* PDF_INC_TRANSP                           */
struct pdxb_x8band {
    pdw  dls, dllen;        /* the display list: segment and length         */
    pdi  bx0, by0;          /* the band's origin in page pixels             */
    pdw  bw;                /* its width (at most 768)                      */
    pdw  bseg, bstride, bpp;
    pdi  pgx0, pgy0, pgx1, pgy1;    /* the page rectangle                   */
};
struct pdxb_x8grp {
    pdw  frame, obj, gen;   /* the Form frame (g_nfr), the Form             */
    pdw  tuple, dllen;      /* the tuple in force; the list's length        */
    pdi  y0, y1;            /* the rows the group's clip covers             */
    pdw  maskpend;          /* the Form is a soft mask's /G                 */
};
struct pdxb_x8img {
    pdw  obj, gen, flags;   /* the image object; the image record's flags   */
    pdi  xl, yt, wq, hq;    /* the image rectangle, quarter pixels          */
    pdi  x0, x1, ry0, ry1;  /* columns and rows of this strip               */
};
struct pdxb_x8row {         /* pdr_ip's x0 .. inv, in that order            */
    pdi  x0, x1;
    pdw  sseg, soff, sw, xlo, xhi, slo, shi, smode, inv;
};
/* [fonts 20.22.8]  */
/* PDXF does everything a font needs from the FONT DICTIONARY: PDREND names the
 * font object and a character code, never a program, an encoding or a CMap. */
#define PDV_FNT_GLYPH   0xD8    /* XB: pdxb_fglyph. One glyph: its bitmap, advance and Latin   */
#define PDV_FNT_PATH    0xD9    /* XB: pdxb_fpath. One glyph's outline as PDFP_* ops           */
#define PDFK_NONE 0             /* program kinds (pdxb_fglyph.kind)                            */
#define PDFK_TT   1
#define PDFK_CFF  2
#define PDFK_T1   3
#define PDFK_T3   4             /* a Type 3 font: PDXF reads its CharProcs, no program                  */
#define PDFG_INFO   0x0001      /* pdxb_fglyph.flags in: no bitmap, only gid/lat/adv           */
#define PDFG_BLOCK  0x0004      /* in: a STRIKE BLOCK request (below): PDXF finds or makes the block of
                                   (font, em64 >> 6), then the code's slot in it, rasterises (em64 != 0)
                                   and records the width and the Latin byte; answers the slot and block */
#define PDFB_SIZE   4096        /* a strike block: 128 glyph records (4 bytes: width, left bearing i8,
                                   bitmap offset u16), the bitmaps from +512 (the PDFB strike layout of
                                   11.2, slots 1..128, the record of slot s at (s-1)*4; the u16 next
                                   free bitmap offset is at +3324),
                                   the keys[128] (u16 code, 0xFFFF empty) at +3328 and info[128] (u16
                                   /W width in 1/1000 em, u8 PD Latin, u8 flags PDFB_F_*) at +3584. The
                                   slot of a code is its index in keys + 1, found from hash(code) =
                                   (code ^ code >> 7) & 127 by linear probing                  */
#define PDFB_HDR    64          /* the 64 bytes just below the blocks: SYN_MAX (PDFB_NBLK, 6) entries of 4 words
                                   (font obj, gen, px, pass last used) at +0, the block count at +60 and
                                   the magic PDFB_MAGIC at +62. PDREND finds, PDXF makes and evicts      */
#define PDFB_MAGIC  0x5342
#define PDFB_NBLK   6           /* blocks at most: (glyph_kb - 24) / 4, so 48 KiB six, 40 four, 32 two, 24 none */
#define PDFB_GLYPHKB 40         /* the glyph claim PDREND asks for (PDMEM_GLYPH) the first time a page
                                   needs a block (its MIN); the primary gives 40, else 32, else refuses
                                   (the font is then drawn in the substitute face, PDF_INC_APPROX)  */
#define PDTM_UNREAD 2            /* pdxb_textmap.done, with 1: a font's codes on the page could not be
                                   read (PDXF refused, the view never drew them): Find says so (QA R17) */
#define PDFB_GLYPHWANT 48       /* its WANT: asked once a pass finds every block in use (a fifth
                                   (font, size) on one page, QA R24); refused, "glyph block full"   */
#define PDFB_KEYS   3328
#define PDFB_INFO   3584
#define PDFB_T3REC  512         /* a Type 3 block (em64 = 0 only, so no bitmaps): CharProc (u16 obj, u16 gen) of slot s at
                                   +512 + (s-1)*4, the FontMatrix as 6 x (u16 lo, i16 hi) 16.16 at +1536   */
#define PDFB_T3FM   1536
#define PDFB_F_BMP  0x01        /* the slot's bitmap is made                                    */
#define PDFB_F_BIG  0x02        /* the glyph does not fit 32 px or the block: not bitmapped      */
#define PDFG_LATIN  0x0002      /* out flags: lat is meaningful (the font has a /ToUnicode)    */
#define PDFG_T0     0x0004      /* out: a Type0 font (codes are 2 bytes)                       */
#define PDFG_VERT   0x0008      /* out: vertical writing (-V)                                  */
#define PDFG_CHOP   0x0010      /* out: the program was larger than the workspace              */
#define PDFE_NOPROG   1         /* pdxb_fglyph.err: the font has no embedded program           */
#define PDFE_FORMAT   2         /* not TrueType, CFF or Type 1 (or a refused variant)          */
#define PDFE_SIZE     3         /* the program does not fit the workspace                      */
#define PDFE_READ     4         /* a stream could not be read                                  */
#define PDFE_CMAP     5         /* a predefined CJK CMap: no data here                         */
struct pdxb_fglyph {
    pdw objnum, gen;        /* in: the font dictionary                                       */
    pdw code;               /* in: the character code (1 byte, or 2 for a Type0 font)       */
    pdw flags;              /* in: PDFG_INFO                                                 */
    pdw em64;               /* in: the size in 1/64 pixel                                    */
    pdw asc, rows;          /* in: the strike's ascent rows and total rows (<= 48)          */
    pdw dseg, doff, cap;    /* in: where the bitmap goes. With PDFG_BLOCK: dseg and cap name the
                               glyph claim PDREND holds (segment, KiB), which must be the one PDXF
                               was handed at PDV_X_INIT (else PDE_RANGE); PDXF puts the header and
                               the blocks at its top and answers doff = the header, cap = blocks */
    pdw w;                  /* out: width px (0 = empty glyph); 0xFFFF = not bitmapped       */
    pdi lb;                 /* out: left bearing px                                          */
    pdw nb;                 /* out: bytes written                                            */
    pdw adv64;              /* out: the program's advance, 1/64 pixel                        */
    pdw adv1000;            /* out: a Type0 font's /W or /DW width, 1/1000 em                */
    pdw lat;                /* out: PD Latin byte of the code's ToUnicode (0 none)           */
    pdw gid;                /* out: the glyph index (0 = none)                               */
    pdw cid;                /* out: a Type0 font's CID                                       */
    pdw kind;               /* out: PDFK_*                                                   */
    pdw oflags;             /* out: PDFG_*                                                   */
    pdw slot;               /* out (PDFG_BLOCK): the slot, 0 = none                            */
    pdw pass;               /* in (PDFG_BLOCK): PDREND's pass number: a block not used in this pass
                               may be reused for a new (font, size)                            */
    pdw blk;                /* out (PDFG_BLOCK): the block index of (font, size), 0xFFFF = none free */
    pdw err;                /* out when the answer is a failure: PDFE_*                      */
};
#define PDFP_M 1                /* PDV_FNT_PATH ops: M x y | L x y | C x1 y1 x2 y2 x y | Z (i16 font units) */
#define PDFP_L 2
#define PDFP_C 3
#define PDFP_Z 4
struct pdxb_fpath {
    pdw objnum, gen, code;  /* in */
    pdw dseg, doff, cap;    /* in: the op buffer (words)                                     */
    pdw len;                /* out: words written                                            */
    pdw upem;               /* out                                                           */
    pdw adv1000;            /* out: advance, 1/1000 em                                       */
    pdw err;
};
/* [img 20.22.11-13]*/
/* bilevel (20.22.12): follows pdxb_img in the XB, filled from /DecodeParms  */
struct pdxb_imgx {
    pdw  k;                 /* /K (CCITT), as a signed 16-bit value        */
    pdw  cols;              /* /Columns (0 = 1728)                         */
    pdw  rows;              /* /Rows (0 = the image's /Height)             */
    pdw  flags;             /* CCF_*                                       */
    pdw  globals;           /* JBIG2: non-zero if /JBIG2Globals exists     */
    pdw  reserved;
};
#define CCF_EOL     1       /* /EndOfLine                                  */
#define CCF_ALIGN   2       /* /EncodedByteAlign                           */
#define CCF_EOB     4       /* /EndOfBlock (default true)                  */
#define CCF_BLACK1  8       /* /BlackIs1                                   */
#define PDIMG_NATIVE 0x0004 /* in: the /ColorSpace is not a device space: deliver
                               PDIM_N8 in the image's own components (20.22.11) */
#define PDJ_ALPHA 0x0100    /* jpx: flags bit - deliver the codestream's own alpha
                               channel as PDIM_GREY8 (/SMaskInData, 20.22.13.7) */
/* [img end]        */

/* ---- pinned sizes: a layout change is a contract change ---------------- */
typedef char pdabi_sz_u32 [sizeof(struct pd_u32)     ==  4 ? 1 : -1];
typedef char pdabi_sz_fx  [sizeof(struct pd_fx)      ==  4 ? 1 : -1];
typedef char pdabi_sz_mat [sizeof(struct pd_mat)     == 24 ? 1 : -1];
typedef char pdabi_sz_view[sizeof(struct pd_view)    == 28 ? 1 : -1];
typedef char pdabi_sz_edit[sizeof(struct pd_edit)    == 48 ? 1 : -1];
typedef char pdabi_sz_tmg [sizeof(struct pdtm_glyph) == 12 ? 1 : -1];
typedef char pdabi_sz_npg [sizeof(struct pd_newpage) == 10 ? 1 : -1];
typedef char pdabi_sz_tok [sizeof(struct pdxb_token) <= PD_XB_BYTES ? 1 : -1];
typedef char pdabi_sz_dg  [sizeof(struct pdxb_dictget) <= PD_XB_BYTES ? 1 : -1];
typedef char pdabi_sz_x8b [sizeof(struct pdxb_x8band) == 24 ? 1 : -1];
typedef char pdabi_sz_x8g [sizeof(struct pdxb_x8grp)  == 16 ? 1 : -1];
typedef char pdabi_sz_x8r [sizeof(struct pdxb_x8row) == 22 ? 1 : -1];
typedef char pdabi_sz_x8i [sizeof(struct pdxb_x8img) == 22 ? 1 : -1];

#endif /* PDABI_H */
