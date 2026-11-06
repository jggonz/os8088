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
#define PDE_FLSLOT_KB   42      /* 32 KiB history + two 4 KiB predictor rows + 2 KiB saved state */
#define PDE_FLSLOT_PARA 2688    /* ...in paragraphs                               */
#define PDE_FLWIN       32768u
#define PDE_FLROWA      32768u  /* the predictor's rows, in the slot              */
#define PDE_FLROWB      36864u
#define PDE_FLSAVE      40960u  /* the saved decoder state                        */
#define PDE_FLOB        256     /* decoded bytes held between deliveries           */
#define PDE_PRC         128     /* a predictor chunk                               */

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
    pdw state, final, tab;  /* tab: 0 none built, 1 fixed, 2 dynamic              */
    pdw errc;               /* a failure, reported once when the output is drained */
    pdw bb, bn;             /* the bit buffer: bn bits, LSB first                 */
    pdw slen;               /* stored bytes left                                  */
    pdw mleft, mdist;       /* a match not yet fully copied                       */
    pdw wpos, whave;        /* window write position; bytes the window holds     */
    pdw on, oi;             /* ob: decoded bytes / the next one to deliver        */
    pdw pred, prow, bpp, bpc;   /* the predictor: 0 none / 1 PNG / 2 TIFF; a row's bytes; ... */
    pdw ptag, ppos, pcur;
    pdw pi, pe;             /* pout[pi..pe) is the unread decoded output          */
    pdw lcnt[16], dcnt[16];
    pdw lsym[288], dsym[32];
    pdw lut[256];           /* the next 8 bits of a literal/length code: sym | len << 9 */
    pdb ob[PDE_FLOB];
    pdb pout[PDE_PRC + 8];
};

static int fl_get(struct sctx *c);          /* one byte of the Flate stage of c, or -1 */
static pdw fl_parms(pdw num);               /* /DecodeParms of the stream              */
static pdw fl_attach(struct sctx *c);
static void fl_drop(struct sctx *c);
static void fl_init(pdw flags, pdw xbseg);
static pdw g_fseg, g_nfl;                   /* the claim; slots it holds (0: Flate is refused) */
static struct flst fl;                      /* the loaded decoder */

#endif
