/* ============================================================================
 * os8088 - apps/pagedesk/pdeflate.h        PDENG's FlateDecode (PAGEDESK-SPEC 20.13)
 * Included by pdeng.c after pde.h; pdeflate.c follows pdestrm.c.
 *
 * A stage of the filter chain, like ASCIIHex and RunLength: pull one decoded
 * byte at a time, resumable at any byte, so every PDENG work bound (PD-28's
 * 1 MiB of output, 4 MiB of every stage's work) holds without a change.
 * The 32 KiB history, the predictor's two rows and a stream's saved decoder
 * state live in a SLOT of the Enhanced tier's flate claim, handed in by the
 * primary (a module claims nothing, PAGEDESK-SPEC 3.4); a machine without it
 * refuses Flate as before. ONE decoder is resident in near memory; reading a
 * stream other than the one loaded swaps the loaded state out to its slot and
 * the reader's in (a form opened inside a page's content, then closed).
 * ==========================================================================*/
#ifndef PDEFLATE_H
#define PDEFLATE_H

#define PDF_K_FLATE     3       /* a chain stage, beside PDF_K_HEX / PDF_K_RLE */

#define PDXI_FLATE      0x0001  /* pdxb_init.flags: the two words AFTER the struct hold flate_seg, flate_kb */

#define PDE_NFL         2       /* slots: the page's stream and one form inside it */
#define PDE_FLSLOT_KB   43      /* 32 KiB history + two 4 KiB predictor rows + 3 KiB of tables and saved state */
#define PDE_FLSLOT_PARA 2752    /* ...in paragraphs                               */
#define PDE_FLWIN       32768u
#define PDE_FLROWA      32768u  /* the predictor's rows, in the slot              */
#define PDE_FLROWB      36864u
#define PDE_FLTAB       40960u  /* the Huffman tables, in the slot (they never move):      */
#define FT_LCNT         0       /*   codes of each length, literal/length (16 words)       */
#define FT_DCNT         32      /*   ...distance (16 words)                                */
#define FT_LSYM         64      /*   symbols by code order, literal/length (288 words)     */
#define FT_DSYM         640     /*   ...distance (32 words)                                */
#define FT_LUT          704     /*   the next 8 bits of a lit/len code: sym | len << 9 (256 words) */
#define FT_DLUT         1216    /*   ...and of a distance code (256 words)                  */
#define PDE_FLSAVE      42688u  /* the saved decoder state, after the tables (<= 3 KiB all told) */
#define PDE_FLOB        256     /* decoded bytes held between deliveries           */
#define PDE_PRC         64      /* a predictor chunk                               */

/* the state machine */
#define FS_HDR          0       /* the zlib header                                 */
#define FS_BLK          1       /* a block header                                  */
#define FS_STORED       2       /* inside a stored block                           */
#define FS_CODES        3       /* inside a Huffman block                          */
#define FS_END          4       /* done, or failed: errc says which                */

/* the LOADED decoder: everything that must survive between two reads of one
 * stream, in one block so that a swap is one copy each way (<= 2 KiB). */
struct flst {
    pdw seg;                /* the slot's segment: window at 0, rows at 32768     */
    pdw stage;              /* the chain index of the Flate stage                 */
    pdw zero_from;          /* (the first word a fresh decoder zeroes: always 0)  */
    pdw state, final, tab;  /* tab: 0 none built, 1 fixed, 2 dynamic              */
    pdw errc;               /* a failure, reported once when the output is drained */
    pdw bb, bn;             /* the bit buffer: bn bits, LSB first                 */
    pdw slen;               /* stored bytes left                                  */
    pdw mleft, mdist;       /* a match not yet fully copied                       */
    pdw wpos, whave;        /* window write position; bytes the window holds     */
    pdw on, oi;             /* ob: decoded bytes / the next one to deliver        */
    pdw pi, pe;             /* pout[pi..pe) is the unread decoded output          */
    pdw ptag, ppos, pcur;
    pdw zero_to;            /* (the end of what a fresh decoder zeroes)           */
    pdw pred, prow, bpp, bpc;   /* the predictor: 0 none / 1 PNG / 2 TIFF; a row's bytes; ... */
    pdb ob[PDE_FLOB];
    pdb pout[PDE_PRC + 8];
};

static int fl_get(struct sctx *c);          /* one byte of the Flate stage of c, or -1 */
static pdw fl_take(struct sctx *c, pdb *dst, pdw max);   /* the bulk of what is ready */
static pdw fl_parms(pdw num);               /* /DecodeParms of the stream              */
static pdw fl_attach(struct sctx *c);
static void fl_drop(struct sctx *c);
static void fl_init(pdw flags, pdw xbseg);
static pdw g_fseg, g_nfl;                   /* the claim; slots it holds (0: Flate is refused) */
static struct flst fl;                      /* the loaded decoder */

#endif
