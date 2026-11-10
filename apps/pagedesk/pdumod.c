/* ============================================================================
 * os8088 - apps/pagedesk/pdumod.c   THE MODULES AND THE CLAIMS
 *                                   (PAGEDESK-SPEC 3.1-3.4, 16.2, 20.6.2)
 *
 * Only the primary claims memory (3.4, SPEC.md 20.12.5), so every claim the
 * modules work in is made HERE and handed down by segment, and every one is
 * given back here: at document close, on every failed open, after a save, and
 * - for whatever the kernel would have freed anyway - when the instance dies.
 *
 *   launch   op_load has already carved PDENG + PDREND (eager parts). Check
 *            each module's 8-byte header against its row, bind its door,
 *            claim the 1 KiB exchange block, INIT both with no document
 *            claims.
 *   open     claim the document set of 16.2 (cache, arena, tables, display
 *            list, band, glyph cache), re-INIT both with it (20.6.2: the
 *            one way pdxb_init can carry it), read PDRES's font bank once.
 *   close    PDV_ENG_CLOSE, PDV_REND_ABORT, free the document set.
 *   save     PDWRT into the slot (pduslot.c, 20.22.2: claim S, its buffers
 *            the occupant's workspace), INIT; afterwards the slot is freed.
 * ==========================================================================*/

/* written by pdstk.inc's pdu_mcall: NOT static, an nasm label names them */
int pdu_mcf;                    /* 1 = the last verb failed (CF) */
unsigned pdu_mdx;               /* ...and its DX */
unsigned pdu_mcx;               /* ...and its CX (PDE_NEEDMEM's region) */
/* THE PRIVATE MODULE STACK (PAGEDESK-SPEC 20.15, D23): the PDU_DC_DSTK row's
 * segment while it is armed (0 = none: pdu_mcall calls flat), and its two
 * flags - the canary was found broken (sticky until the claims go back) and
 * the wake that closes the document has been posted. pdstk.inc reads and
 * writes all three by name. */
unsigned pdu_dstkseg;
int pdu_stkbad;
int pdu_stktold;

static unsigned pdu_xb;         /* the exchange block, 1 KiB (3.2) */
static struct pdxb_init pdu_xinit;

/* the document set (16.2), in this order; KB except the band.
 *
 * TWO CLAIMS, CARVED (20.6.2). MEM_OWNER_MAX is 8 claims per owner on
 * kern_big (kernel/memory.inc, SPEC.md 50), and 16.2's rows one claim each
 * come to 13 at a save's peak. So the six document rows ride two claims cut
 * with os88_seg_sub (a selector on kern_mega, seg + para on every real-mode
 * kernel): A = cache, arena, tables, glyph cache (50 KiB) and B = display
 * list + PDREND's workspace, band (<= 34 KiB, D13). Every cut is on a KiB,
 * so every sub-segment is 512-aligned for a disk transfer (SPEC.md 2.1.1); the cache is first in A,
 * at the claim's own base. The peak is then the carve, the exchange block,
 * the font bank, A, B, the overlay, PDWRT and its buffers: 8. */
#define PDU_DC_CACHE  0
#define PDU_DC_ARENA  1
#define PDU_DC_TABLES 2
#define PDU_DC_GLYPH  3
#define PDU_DC_DL     4
#define PDU_DC_BAND   5
/* wave 2's rows (DECISIONS D15, carved by the lead so no two agents edit
 * this table): the edit store (13.1, owner `edit`), its undo history (13.6,
 * `edit`), the page order (13.7, `pages`), thumbnails (16.2, `nav`) and the
 * text/search scratch (16.2, `nav`). Reach them ONLY through pdu_dcseg(). */
#define PDU_DC_EDITS  6
#define PDU_DC_UNDO   7
#define PDU_DC_PAGES  8
#define PDU_DC_THUMB  9
#define PDU_DC_SCRATCH 10
/* D23: the private module stack every verb runs on (pdstk.inc, 20.15), last
 * in B. 2 KiB = PD_DSTK_BYTES: the record at its top, the canary at its foot */
#define PDU_DC_DSTK   11
#define PDU_NDC       12
#define PDU_DC_SPLIT  4                 /* rows 0..3 in claim A, 4.. in B */
/* DECISIONS D13: PDREND keeps its gstates and font cache in a 6,912-byte
 * workspace at the tail of dl_seg, so the display-list row is 22 + 7 = 29 */
/* the arena is 11 (crypt, 20.22.5): an encrypted document's cipher state
 * lives at its offset 0, and SETKEY answers PDE_NOMEM below 11 KiB. Raised for
 * every document: A cannot grow in place once PDENG holds its rows */
/* the edit store (QA F26, the lead's claim-B ruling) is NOT carved: it is a
 * claim of its own, taken at the FIRST edit (ovl_ed_need, pduedit.c), so a
 * document only read leaves its 16 KiB to an XT's font occupant. Its row is
 * 0 KiB here, and pdu_dc[PDU_DC_EDITS] is the claim's base once it is held */
const unsigned pdu_dckb[PDU_NDC] = { 16, 11, 11, 12, 29, 0,
                                            0, 4, 1, 4, 2, 2 };
static unsigned pdu_dc[PDU_NDC];        /* sub-segments, 0 = not held */
static unsigned pdu_dclaim[3];          /* A, B, and the edit store */
static unsigned pdu_flslots;            /* D17: Flate slots at B's tail, 0-2 */
/* a document row's segment, 0 when no document is open (D15) */
static unsigned pdu_dcseg(int row) { return (row >= 0 && row < PDU_NDC) ? pdu_dc[row] : 0; }

static unsigned pdu_bankraw;            /* the PDRES INDEX claim... */
static unsigned pdu_bank;               /* ...and the part's first byte in it */
static unsigned pdu_bankkb;
static unsigned pdu_banklen;            /* the INDEX's bytes (kind 7) */
#define PDU_INDEX_MAX 8192u             /* D3: the INDEX is at most 8 KiB */
#define PDU_KIT_MAX   PK_KIT_MAX        /* 20.21.2: ...and the kit after it */
#define PDU_BAND_BYTES 2304u            /* pdkit.inc's band: 96 x 24 */
#define PDU_RES_KIND_INDEX 7            /* 20.4.1's directory kinds */
#define PDU_RES_KIND_KIT 9
static struct pdxb_fontbank pdu_xfb;

#define PDU_WBUF_KB 18                  /* D9/D18: 18 KiB a save (4 out + 4.7 tail tables + 1,068 objects); an Extract 16 + /Size/128 + /Size/2048, at most 40 (pdovlpage.c) */  /* pages */
static unsigned pdu_wkb = PDU_WBUF_KB;  /* pages: this job's writer buffer (20.11.4) */

static int pdu_adapter, pdu_bpp, pdu_scrw, pdu_scrh;

/* the two eager modules (and PDREND's code part) are checked and bound by
 * pduslot.inc's pdu_mod_launch; PDWRT and the rest are the slot's */

static void pdu_cat(char *d, const char *s, int cap)
{
    int n;
    n = os88_strlen(d);
    os88_strcpy(d + n, s, cap - n);
}

/* INIT module m (verb: its INIT) with pdu_xinit; 0 or a PDE_*. The doors
 * it hands down are the BOUND vectors (code selectors on kern_mega), 0 when
 * a module is not bound (20.22.2.4 step 8) */
/* int pdu_minit(int m, unsigned verb) is pduslot.inc's: pdu_xinit with both
 * doors and the exchange block filled in, copied to the block, and module m's
 * INIT verb called - 0, or the module's PDE_* (PDE_LIMIT if it said none) */

/* int pdu_mod_launch(void) is pduslot.inc's: launch - both eager modules'
 * headers checked and bound (pdu_mcheck), the exchange block, INIT with no
 * document set, then the INDEX and the face kit (pdu_bank_load). 0 = ready;
 * else a toast has said why and the launch is refused. */
int pdu_bank_load(void);
static void pdu_mod_quit(void);

/* give back everything the primary holds - the instance's death does this
 * too (SPEC.md 50), but a refused launch and a failed open must not leak
 * into the session that goes on */
/* void pdu_doc_free(void) is pduslot.inc's: both claims given back, every
 * row and pdxb_init's document fields 0 (D2: never a freed segment at the
 * next INIT), the module stack off (D23), no Flate slots (D17), and S and X
 * with them (20.22.2) */

/* the writer is done: the slot goes back (20.22.2.2, as PDWRT's claim did) */
#define pdu_wrt_out() pdu_slot_free()

static void pdu_mod_quit(void)
{
    pdu_doc_free();
    pdu_cache_drop();
    if (pdu_bankraw) {
        pk_init(0, 0);                  /* no band: nothing more is drawn */
        os88_mem_free(pdu_bankraw);
        pdu_bankraw = 0;
        pdu_bank = 0;
    }
    if (pdu_xb) {
        os88_mem_free(pdu_xb);
        pdu_xb = 0;
    }
    pdu_mbind(PDU_M_ENG, 0);
    pdu_mbind(PDU_M_REND, 0);
}

/* int pdu_bank_load(void) is pduslot.inc's: the PDRES INDEX and the face's
 * kit, once per instance (DECISIONS D3, PAGEDESK-SPEC 20.4.1, 20.21.2), read
 * by offset out of PAGEDESK.O88 from the launch folder, at most pdu_rescap
 * bytes, with the band after them and pdu_xfb filled in. 0, or a PDE_*; a
 * missing bank is not fatal (the renderer substitutes the kernel face) */
static unsigned pdu_rescap = PDU_INDEX_MAX + PDU_KIT_MAX;

/* open: the document set. 0, or PDE_NOMEM with pdu_needkb set */
static unsigned pdu_needkb;
/* D17 / PAGEDESK-SPEC 20.13.3: the Flate slots, 43 KiB (2,752 paragraphs)
 * each, at claim B's tail. Chosen by RAM, never by CPU: two when the largest
 * free run holds B, both slots and the reserve, one when one fits, none
 * otherwise (PDENG then refuses Flate with PDE_FLATE, as before). The first
 * slot keeps PD-29's 32 KiB after it; 32 KiB plus a save's peak (D28) left a
 * 640 KiB XT with 120 KiB free and no slot at all (QA F22). The second keeps
 * 48 KiB more, and a save or an occupant that wants it back has it by
 * release-before-refuse (20.22.2.4) */
/* pduslot.inc: PDU_FL_PARA_A 2752, PDU_FL_RESV_A 2048 (32 KiB), PDU_FL_RESV2_A 3072 (48 KiB) */
/* int pdu_doc_claim(void) is pduslot.inc's: the rows above carved into the
 * two claims, the Flate slots at B's tail, the module stack armed and both
 * modules INITed - 0, PDE_NOMEM with pdu_needkb set, or PDE_LIMIT */

/* close the document in the modules and give its claims back */
static void pdu_doc_release(void)
{
    pdu_ed_gone();                      /* edit */
    pdu_rabort = 0;                     /* told here, unconditionally */
    pdu_mcall(PDU_M_REND, PDV_REND_ABORT, 0, 0, 0);
    pdu_mcall(PDU_M_ENG, PDV_ENG_CLOSE, 0, 0, 0);
    pdu_doc_free();
}

/* D23: the private module stack was overrun (pdu_mcall found its canary
 * broken or its SP off, and has refused every verb since but the three that
 * let go). Called first thing in every wake - pdu_mcall posted one: close the
 * document, which gives the claims and the stack back and so re-arms it for
 * the next open, and say why on the status line. */
static void pdu_stk_wake(void)
{
    if (!pdu_stkbad)
        return;
    pdu_close_doc();                    /* ...pdu_doc_free clears the flags */
    pdu_wsay(pdu_errtext(PDE_STACK));
}

