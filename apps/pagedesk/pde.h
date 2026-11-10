/* ============================================================================
 * os8088 - apps/pagedesk/pde.h        PDENG's private declarations
 * (PAGEDESK-SPEC 20.2). One translation unit: pdeng.c includes pdefile.c,
 * pdelex.c, pdexref.c, pdepage.c, pdestrm.c and pdeverb.c after this.
 *
 * Every pdw/pd_u32 expression here is written so that it means the same on
 * a 16-bit int (the target) and a 32-bit int (the host harness): intermediate
 * results are cast back to pdw before they are compared.
 * ==========================================================================*/
#ifndef PDE_H
#define PDE_H

/* ---- engine limits that are not in pdabi.h (PAGEDESK-SPEC 20.2) --------- */
#define PDE_MAXBLK      32      /* block cache slots                          */
#define PDE_MINBLK      2048    /* smallest cache block                       */
#define PDE_MAXCTX      6       /* open streams: 4 forms + the page + an image */
#define PDE_IB          256     /* a stream's / the lexer's lookahead buffer  */
#define PDE_RB          64      /* a filtered stream's raw lookahead          */
#define PDE_OFFCACHE    512     /* resolved-offset cache, direct mapped       */
#define PDE_OCBYTES     6       /* tag, offset lo, offset hi                  */
#define PDE_TBL_CACHE   8192    /* tables claim: offset cache starts here     */
#define PDE_TBL_BYTES   11264   /* 1024 pages x 8 + 512 x 6                   */
#define PDE_VISBYTES    8192    /* arena: a bit per object number             */
#define PDE_OUTMAX_HI   16      /* decoded stream limit: 16 x 64 KiB = 1 MiB  */
#define PDE_WORK_HI     (PDE_OUTMAX_HI * PD_MAXFILTERS) /* every stage's bytes: 4 MiB */
#define PDE_FILEMAX_HI  0x8000  /* a file of 2 GiB or more is refused         */
#define PDE_TAIL        1024
#define PDE_VT          256     /* a value's kept text                        */
#define PDE_NODEBUDGET  16      /* page-tree nodes a slice visits             */
#define PDE_RESHOPS     8       /* indirect hops followed before PDE_CYCLE    */

#define PDX_PACKED      1       /* PDE_SYNTAX detail: a CZ-packed file        */
#define PDX_NOHEADER    2       /* PDE_SYNTAX detail: no %PDF- header         */
#define PDX_NOPAGES     3       /* PDE_SYNTAX detail: the tree has no page    */
#define PDX_NOROOT      4       /* PDE_SYNTAX detail: no usable /Root         */
#define PDX_NOSTART     5       /* PDE_TRUNC detail: no startxref             */

/* internal value kinds (above the PDK_ range) */
#define PDK_CLOSE       0xFE
#define PDK_WORD        0xFD    /* a keyword / operator in an object context  */
#define PDK_EOF         0xFC

/* a stream filter in a context */
#define PDF_K_HEX       1
#define PDF_K_RLE       2

/* buf, n, i and fpos are at +0, +2, +4 and +8 on the target, and
 * pdelex.inc reads them there (LXS_*): move none of them */
struct lxs {
    pdb *buf;               /* near buffer of PDE_IB bytes                */
    pdw n, i;
    pdw mode;               /* 0 = file, 1 = decoded stream               */
    struct pd_u32 fpos;     /* file mode: file position of buf[n]         */
    pdw gen;                /* refill counter                             */
    pdw smode;              /* 0, 1 = literal string, 2 = hex string      */
    pdw sdepth;
};

struct sctx {
    struct lxs lx;          /* MUST be first                              */
    pdb ib[PDE_IB];
    struct pd_u32 rpos;     /* next raw byte in the file                  */
    struct pd_u32 rrem;     /* raw bytes left                             */
    struct pd_u32 outc;     /* decoded bytes produced                     */
    struct pd_u32 declen;
    pdw nflt;
    pdb flt[PD_MAXFILTERS];
    int fcnt[PD_MAXFILTERS];
    pdw fbyte[PD_MAXFILTERS];
    pdw fdone[PD_MAXFILTERS];
    pdb rb[PDE_RB];
    pdw rbn, rbi;
    pdw depth;              /* TOKEN's array/dict nesting                 */
    pdw wlo, whi;           /* bytes every filter stage produced (sget)   */
    pdw filters;            /* PDFL_* of the chain                        */
    /* any PDF: decryption of the raw bytes (pdecrypt.c, 20.22.5) */
    pdw cmode;              /* 0 plain, else the PDCM_* the raw bytes are in */
    pdw civ;                /* AES: the IV has been read                   */
    pdw cn, cg;             /* the stream's object, for its key            */
    pdb cs[258];            /* RC4: S, i, j; AES: the chaining block in [0..16) */
};

struct pde_tok {
    pdw kind;
    pdw neg, real, range;   /* range: an integer of 2^31 or more           */
    pdw fxr;                /* the value does not fit 16.16 (saturated)    */
    struct pd_u32 iv;       /* integer magnitude                          */
    struct pd_fx fx;
    pdw len, more;
    pdb text[256];
};

/* text[] is at +22 on the target and pdelex.inc's ov_clear zeroes up to it */
struct pde_val {
    pdw kind;               /* PDK_*                                      */
    struct pd_fx fx;
    pdw neg, range, fxr;
    struct pd_u32 iv;
    pdw rnum, rgen;
    pdw len;                /* STR/NAME true length                       */
    pdb text[PDE_VT];
};

/* one xref subsection, or a CHECKPOINT standing for `span` consecutive ones
 * (PAGEDESK-SPEC 20.2.3): a revision keeps at most PDE_REVSUB of them */
struct xsub { pdw first, count; struct pd_u32 off; pdw span; };
#define PDE_REVSUB      (PD_MAXSUBSEC / PD_MAXREV)

struct pw_frame { pdw num, gen; struct pd_u32 kpos; };

/* ---- any PDF: encryption (pdecrypt.c, PAGEDESK-SPEC 20.22.5) ------------- */
static pdw cr_read(struct sctx *c, pdb *buf, pdw want);
static void cr_ctx_open(struct sctx *c);
static void cr_string(void);
static pdw cr_skipchk(void);
static pdw cr_open(void);
static pdw cr_raw_plain(void);
static void cr_reset(void);
static pdw cr_info(void);
static pdw cr_setkey(void);
static pdw cr_crypt(void);

#endif
