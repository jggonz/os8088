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
 *   save     fetch PDWRT (lazy part, from HOME), check, bind, claim its
 *            buffers, INIT; afterwards free the buffers, unbind, drop it.
 * ==========================================================================*/

/* written by pdstk.inc's pdu_mcall: NOT static, an nasm label names them */
int pdu_mcf;                    /* 1 = the last verb failed (CF) */
unsigned pdu_mdx;               /* ...and its DX */
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
 * kernel): A = cache, arena, tables, glyph cache (47 KiB) and B = display
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
static const unsigned pdu_dckb[PDU_NDC] = { 16, 8, 11, 12, 29, 0,
                                            16, 4, 1, 4, 2, 2 };
static unsigned pdu_dc[PDU_NDC];        /* sub-segments, 0 = not held */
static unsigned pdu_dclaim[2];          /* the two real claims */
static unsigned pdu_flslots;            /* D17: Flate slots at B's tail, 0-2 */
/* a document row's segment, 0 when no document is open (D15) */
static unsigned pdu_dcseg(int row) { return (row >= 0 && row < PDU_NDC) ? pdu_dc[row] : 0; }
static unsigned pdu_bandb;              /* the band's bytes */

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

static unsigned pdu_wbuf;               /* PDWRT's buffers (16.2, D9) */
#define PDU_WBUF_KB 18                  /* D9/D18: 18 KiB a save (4 out + 4.7 tail tables + 1,068 objects); an Extract 16 + /Size/128 + /Size/2048, at most 40 (pdovlpage.c) */  /* pages */
static int pdu_wrt_in;                  /* PDWRT fetched and bound */
static unsigned pdu_wkb = PDU_WBUF_KB;  /* pages: this job's writer buffer (20.11.4) */

static int pdu_adapter, pdu_bpp, pdu_scrw, pdu_scrh;

static const char *pdu_modname[3] = { "PDENG", "PDREND", "PDWRT" };
static const unsigned pdu_modmagic[3] = { PDM_MAGIC_ENG, PDM_MAGIC_REND,
                                          PDM_MAGIC_WRT };
static const int pdu_modpart[3] = { PDU_PART_ENG, PDU_PART_REND,
                                    PDU_PART_WRT };

static char pdu_mmsg[48];               /* a refusal's toast (copied) */

static unsigned pdu_peekw(unsigned seg, unsigned off)
{
    return (os88_peek(seg, off) & 0xFF) | ((os88_peek(seg, off + 1) & 0xFF) << 8);
}

static void pdu_cat(char *d, const char *s, int cap)
{
    int n;
    n = os88_strlen(d);
    os88_strcpy(d + n, s, cap - n);
}

/* check module m's header in its segment against the parts row; bind it.
 * 0 = bound, else a PDE_* and a toast that names the module. */
static int pdu_mcheck(int m, unsigned seg)
{
    unsigned img, bss, len;

    if (seg == 0)
        goto bad;
    if (pdu_peekw(seg, 0) != pdu_modmagic[m] || pdu_peekw(seg, 2) != PDM_ABI)
        goto bad;
    img = pdu_peekw(seg, 4);
    bss = pdu_peekw(seg, 6);
    len = pdu_part_len(pdu_modpart[m]);
    if (img < PDM_ENTRY + 1 || img + bss < img || img + bss != len)
        goto bad;
    pdu_mbind(m, seg);
    return 0;
bad:
    os88_strcpy(pdu_mmsg, pdu_modname[m], sizeof(pdu_mmsg));
    pdu_cat(pdu_mmsg, ": wrong version", sizeof(pdu_mmsg));  /* <= 21: a 6-letter name + 15 (TOAST_MAX 24) */
    os88_toast(pdu_mmsg, 0);
    return PDE_LIMIT;
}

/* the far vector's segment of a bound module (for pdxb_init.eng_seg) */
static unsigned pdu_mvseg(int m)
{
    return os88_part_seg(pdu_modpart[m]) ? pdu_mvecseg(m) : 0;
}

/* INIT module m with pdu_xinit; 0 or a PDE_* */
static int pdu_minit(int m)
{
    unsigned r;

    pdu_xinit.xb_seg = pdu_xb;
    pdu_xinit.eng_off = PDM_ENTRY;
    pdu_xinit.eng_seg = pdu_mvseg(PDU_M_ENG);
    pd_fcopy_out(&pdu_xinit, pdu_xb, 0, sizeof(pdu_xinit));
    r = pdu_mcall(m, m == PDU_M_ENG ? PDV_ENG_INIT
                     : (m == PDU_M_REND ? PDV_REND_INIT : PDV_WRT_INIT),
                  pdu_xb, 0, 0);
    if (pdu_mcf)
        return r ? r : PDE_LIMIT;
    return 0;
}

/* launch: header checks, the exchange block, INIT with no document set.
 * 0 = ready; else a toast has said why and the launch is refused. */
static int pdu_bank_load(void);
static int pdu_mod_launch(void)
{
    struct os88_video *v;
    int m;

    v = &pdu_vid;
    pdu_adapter = v->kind;
    pdu_bpp = v->bpp;
    pdu_scrw = v->w;
    pdu_scrh = v->h;
    for (m = PDU_M_ENG; m <= PDU_M_REND; m++)
        if (pdu_mcheck(m, os88_part_seg(pdu_modpart[m])) != 0)
            return -1;
    pdu_xb = os88_mem_claim_p(PD_XB_BYTES / 16);
    if (pdu_xb == 0) {
        os88_toast("No memory for PageDesk", 0);
        return -1;
    }
    os88_memset(&pdu_xinit, 0, sizeof(pdu_xinit));
    pdu_xinit.adapter = pdu_adapter;
    pdu_xinit.bpp = pdu_bpp >= 4 ? 4 : 1;
    if (pdu_minit(PDU_M_ENG) != 0 || pdu_minit(PDU_M_REND) != 0) {
        os88_toast("A PageDesk module failed", 0);
        pdu_mod_quit();
        return -1;
    }
    /* 20.21.2: the INDEX and the face kit, at launch - the bars are drawn
     * before any document is. Only a refused claim refuses the launch: the
     * band lives in it */
    if (pdu_bank_load() == PDE_NOMEM) {
        os88_toast("No memory for PageDesk", 0);
        pdu_mod_quit();
        return -1;
    }
    return 0;
}

/* give back everything the primary holds - the instance's death does this
 * too (SPEC.md 50), but a refused launch and a failed open must not leak
 * into the session that goes on */
static void pdu_doc_free(void)
{
    int i;
    for (i = 0; i < 2; i++)
        if (pdu_dclaim[i]) {
            os88_mem_free(pdu_dclaim[i]);
            pdu_dclaim[i] = 0;
        }
    for (i = 0; i < PDU_NDC; i++)
        pdu_dc[i] = 0;
    pdu_dstkseg = 0;                    /* D23: verbs run flat again */
    pdu_stkbad = 0;
    pdu_stktold = 0;
    /* D2: a field of 0 is "no claim of that kind now" - the next INIT must
     * never hand a module a segment that has been given back */
    pdu_xinit.cache_seg = 0;
    pdu_xinit.arena_seg = 0;
    pdu_xinit.tables_seg = 0;
    pdu_xinit.dl_seg = 0;
    pdu_xinit.band_seg = 0;
    pdu_xinit.glyph_seg = 0;
    pdu_xinit.flags = pdu_xinit.flags & ~PDXI_FLATE;   /* D17: no slots now */
    pdu_xinit.flate_seg = 0;
    pdu_xinit.flate_kb = 0;
    pdu_flslots = 0;
}

static void pdu_wrt_out(void)
{
    if (pdu_wbuf) {
        os88_mem_free(pdu_wbuf);
        pdu_wbuf = 0;
    }
    pdu_xinit.wbuf_seg = 0;             /* D2: never a freed segment */
    pdu_xinit.wbuf_kb = 0;
    if (pdu_wrt_in) {
        pdu_mbind(PDU_M_WRT, 0);
        os88_part_drop(PDU_PART_WRT);
        pdu_wrt_in = 0;
    }
}

static void pdu_mod_quit(void)
{
    pdu_doc_free();
    pdu_wrt_out();
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

/* the PDRES INDEX, once per instance (DECISIONS D3, PAGEDESK-SPEC 20.4.1).
 * The part is read BY OFFSET out of PAGEDESK.O88 itself (an OP_SELF row,
 * stored unpacked so the offsets are real), from the folder the package was
 * launched from - and ONLY its INDEX: the directory, the PDFB strike records,
 * the widths and the glyph names, at most 8 KiB. PDREND reads each strike's
 * bitmaps itself on first use, at part_off + the record's offset.
 *
 * READ_AT wants a cluster-aligned offset and capacity (SPEC.md 18.4.4), so
 * the claim covers the clusters the INDEX spans and the bank is the part's
 * first paragraph inside it - a paragraph offset, because os88pkg.py lays
 * parts on paragraphs. 0, or a PDE_*; a missing bank is not fatal (the
 * renderer then substitutes the kernel face) and is reported. */
static int pdu_bank_load(void)
{
    static struct os88_place here;
    unsigned para, len, clus, slack, cap, got, n, i, e;
    unsigned off_lo, off_hi;
    int r;

    if (pdu_bankraw)
        return 0;
    len = pdu_part_len(PDU_PART_RES);
    para = pdu_part_off(PDU_PART_RES);
    if (len == 0 || para == 0)
        return PDE_NOTFOUND;
    /* the second row must be the first row's next byte in the file, or a
     * strike past 65,024 would be read from the wrong place (pagedesk.asm) */
    if ((len & 15) != 0
        || pdu_part_off(PDU_PART_RES2) != para + (len >> 4))
        return PDE_LIMIT;
    if (len > PDU_INDEX_MAX + PDU_KIT_MAX)  /* the INDEX and the kit (20.21.2) */
        len = PDU_INDEX_MAX + PDU_KIT_MAX;
    clus = os88_disk_cluster_sectors();
    if (clus <= 0)
        clus = 1;
    clus = clus * 512;
    /* the part's byte offset is para*16; align it down to a cluster.
     * clus is a power of two <= 32768, and para*16 may pass 64 KiB, so the
     * division is done in paragraphs */
    {
        unsigned cpara, bpara;
        cpara = clus >> 4;
        bpara = para - para % cpara;            /* the cluster's paragraph */
        slack = (para - bpara) << 4;            /* bytes into that cluster */
        off_lo = bpara << 4;
        off_hi = bpara >> 12;
    }
    cap = slack + len;
    cap = (cap + clus - 1) & ~(clus - 1);
    if (cap < slack + len)
        return PDE_LIMIT;
    /* the face's band rides after what is read (20.21.3) */
    pdu_bankraw = os88_mem_claim_p((cap + PDU_BAND_BYTES) >> 4);
    if (pdu_bankraw == 0)
        return PDE_NOMEM;
    pk_init(0, os88_seg_sub(pdu_bankraw, cap >> 4, PDU_BAND_BYTES >> 4));
    os88_file_here(&here);
    r = 0;
    if (os88_file_goto_q_mark(pdu_home.clus, pdu_home.vol) != 0)
        r = PDE_IO;
    else {
        got = pdu_read_at_seg(pdu_pkgname(), pdu_bankraw, cap, off_lo, off_hi);
        if (got < slack + len)
            r = os88_ferr() ? pdu_ferr_pde(os88_ferr()) : PDE_TRUNC;
        os88_file_goto_q_mark(here.clus, here.vol);
    }
    if (r == 0) {
        pdu_bank = os88_seg_sub(pdu_bankraw, slack >> 4, (len + 15) >> 4);
        /* the directory: 'PDRS', version 1, n entries of 12 bytes; kind 7 is
         * the INDEX, and it must lie inside what was read */
        r = PDE_FONT;                   /* the bank itself is not right */
        if (pdu_bank && pdu_peekw(pdu_bank, 0) == 0x4450       /* "PD" */
            && pdu_peekw(pdu_bank, 2) == 0x5352                 /* "RS" */
            && pdu_peekw(pdu_bank, 4) == 1) {
            n = pdu_peekw(pdu_bank, 6);
            for (i = 0; i < n && 8 + i * 12 + 12 <= len; i++) {
                e = 8 + i * 12;
                if (pdu_peekw(pdu_bank, e + 6) != 0
                    || pdu_peekw(pdu_bank, e + 10) != 0)
                    continue;           /* an offset or length past 64 KiB */
                if (pdu_peekw(pdu_bank, e) == PDU_RES_KIND_INDEX
                    && pdu_peekw(pdu_bank, e + 4) == 0) {
                    pdu_banklen = pdu_peekw(pdu_bank, e + 8);
                    r = pdu_banklen && pdu_banklen <= len ? 0 : PDE_LIMIT;
                }
                /* the kit: on a paragraph, inside what was read */
                if (pdu_peekw(pdu_bank, e) == PDU_RES_KIND_KIT
                    && (pdu_peekw(pdu_bank, e + 4) & 15) == 0
                    && pdu_peekw(pdu_bank, e + 8) != 0
                    && pdu_peekw(pdu_bank, e + 4) <= len
                    && pdu_peekw(pdu_bank, e + 8) <= len - pdu_peekw(pdu_bank, e + 4))
                    pk_init(os88_seg_sub(pdu_bankraw,
                                         (slack + pdu_peekw(pdu_bank, e + 4)) >> 4,
                                         (pdu_peekw(pdu_bank, e + 8) + 15) >> 4),
                            os88_seg_sub(pdu_bankraw, cap >> 4,
                                         PDU_BAND_BYTES >> 4));
            }
        }
    }
    if (r) {
        /* the claim stays for the face's band: no fonts, no pictures and no
         * bank, which the renderer survives (D3) */
        pk_init(0, os88_seg_sub(pdu_bankraw, cap >> 4, PDU_BAND_BYTES >> 4));
        pdu_bank = 0;
        return r;
    }
    pdu_bankkb = (pdu_banklen + 1023) >> 10;
    /* D3's pdxb_fontbank: where PDREND finds the strikes it reads itself */
    os88_memset(&pdu_xfb, 0, sizeof(pdu_xfb));
    os88_strcpy(pdu_xfb.pkgname, pdu_pkgname(), sizeof(pdu_xfb.pkgname));
    pdu_xfb.place_clus = pdu_home.clus;
    pdu_xfb.place_vol = pdu_home.vol;
    pdu_xfb.part_off.lo = para << 4;
    pdu_xfb.part_off.hi = para >> 12;
    pdu_xfb.index_seg = pdu_bank;
    pdu_xfb.index_len = pdu_banklen;
    return 0;
}

/* open: the document set. 0, or PDE_NOMEM with pdu_needkb set */
static unsigned pdu_needkb;
static unsigned pdu_dcpara[PDU_NDC];    /* each row's size in paragraphs */
/* D17 / PAGEDESK-SPEC 20.13.3: the Flate slots, 43 KiB (2,752 paragraphs)
 * each, at claim B's tail. Chosen by RAM, never by CPU: two when the largest
 * free run holds B, both slots and the reserve, one when one fits, none
 * otherwise (PDENG then refuses Flate with PDE_FLATE, as before). The reserve
 * is PD-29's 32 KiB PLUS a save's peak: PDWRT and its buffers measured 47.5
 * KiB on a 640 KiB XT (pagedeskxt, D28). With 32 KiB alone, a Save As on that
 * machine left 5.5 KiB unclaimed */
#define PDU_FL_PARA   2752
#define PDU_FL_RESV   5120              /* 32 + 48 KiB, in paragraphs */
static int pdu_doc_claim(void)
{
    int i, c, first, last;
    unsigned w, tot, at, big, fl;

    pdu_doc_free();
    w = pdu_scrw;
    if (pdu_bpp >= 4)
        pdu_bandb = ((w + 1) >> 1) << 4;        /* PD_BANDROWS = 16 */
    else
        pdu_bandb = ((w + 7) >> 3) << 4;
    pdu_needkb = 0;
    for (i = 0; i < PDU_NDC; i++) {
        if (i == PDU_DC_BAND)                   /* the band to a whole KiB */
            pdu_dcpara[i] = ((pdu_bandb + 1023) >> 10) << 6;
        else
            pdu_dcpara[i] = pdu_dckb[i] << 6;
        pdu_needkb = pdu_needkb + (pdu_dcpara[i] >> 6);
    }
    for (c = 0; c < 2; c++) {
        first = c ? PDU_DC_SPLIT : 0;
        last = c ? PDU_NDC : PDU_DC_SPLIT;
        tot = 0;
        for (i = first; i < last; i++)
            tot = tot + pdu_dcpara[i];
        fl = 0;
        if (c) {                        /* B: the Flate slots ride its tail */
            big = os88_mem_largest_p();
            if (pdp_cfg[9])             /* prefs: Flate cache Off */
                big = 0;
            if (big >= tot + 2 * PDU_FL_PARA + PDU_FL_RESV)
                fl = 2;
            else if (big >= tot + PDU_FL_PARA + PDU_FL_RESV)
                fl = 1;
            tot = tot + fl * PDU_FL_PARA;
        }
        pdu_dclaim[c] = os88_mem_claim_p(tot);
        if (pdu_dclaim[c] == 0) {
            pdu_doc_free();
            return PDE_NOMEM;
        }
        at = 0;
        for (i = first; i < last; i++) {
            pdu_dc[i] = at ? os88_seg_sub(pdu_dclaim[c], at, pdu_dcpara[i])
                           : pdu_dclaim[c];
            if (pdu_dc[i] == 0) {
                pdu_doc_free();
                return PDE_NOMEM;
            }
            at = at + pdu_dcpara[i];
        }
        if (fl) {                       /* PDENG finds slot k at flate_seg + k
                                         * * 2752 paragraphs: true on every
                                         * real-mode kernel; where the second
                                         * slot's selector is not that, one */
            pdu_xinit.flate_seg = os88_seg_sub(pdu_dclaim[c], at, PDU_FL_PARA);
            if (fl == 2 && os88_seg_sub(pdu_dclaim[c], at + PDU_FL_PARA, PDU_FL_PARA)
                           != pdu_xinit.flate_seg + PDU_FL_PARA)
                fl = 1;
            if (pdu_xinit.flate_seg == 0)
                fl = 0;
        }
        if (c) {
            pdu_flslots = fl;
            pdu_xinit.flate_kb = fl * 43;
            if (fl)
                pdu_xinit.flags = pdu_xinit.flags | PDXI_FLATE;
        }
    }
    /* D23: arm the private module stack. 0xCC everywhere, so its high-water
     * mark can be read off it (a debugger's `xp`), and the canary at its foot
     * that pdu_mcall checks at every outermost return; the record at its top
     * is written by pdu_mcall itself at each call. */
    pd_ffill(pdu_dc[PDU_DC_DSTK], 0, PD_DSTK_FILL, PD_DSTK_BYTES);
    pd_fpokew(pdu_dc[PDU_DC_DSTK], 0, PD_DSTK_CAN0);
    pd_fpokew(pdu_dc[PDU_DC_DSTK], 2, PD_DSTK_CAN1);
    pdu_dstkseg = pdu_dc[PDU_DC_DSTK];
    pdu_xinit.cache_seg = pdu_dc[PDU_DC_CACHE];
    pdu_xinit.cache_kb = pdu_dckb[PDU_DC_CACHE];
    pdu_xinit.arena_seg = pdu_dc[PDU_DC_ARENA];
    pdu_xinit.arena_kb = pdu_dckb[PDU_DC_ARENA];
    pdu_xinit.tables_seg = pdu_dc[PDU_DC_TABLES];
    pdu_xinit.tables_kb = pdu_dckb[PDU_DC_TABLES];
    pdu_xinit.dl_seg = pdu_dc[PDU_DC_DL];
    pdu_xinit.dl_kb = pdu_dckb[PDU_DC_DL];
    pdu_xinit.band_seg = pdu_dc[PDU_DC_BAND];
    pdu_xinit.band_bytes = pdu_bandb;      /* the claim is a whole KiB */
    pdu_xinit.glyph_seg = pdu_dc[PDU_DC_GLYPH];
    pdu_xinit.glyph_kb = pdu_dckb[PDU_DC_GLYPH];
    pdu_xinit.bank_seg = pdu_bank;
    pdu_xinit.bank_kb = pdu_bank ? pdu_bankkb : 0;
    if (pdu_minit(PDU_M_ENG) != 0 || pdu_minit(PDU_M_REND) != 0) {
        pdu_doc_free();
        return PDE_LIMIT;
    }
    if (pdu_bank) {
        pd_fcopy_out(&pdu_xfb, pdu_xb, 0, sizeof(pdu_xfb));
        pdu_mcall(PDU_M_REND, PDV_REND_FONTBANK, 0, 0, 0);
    }
    return 0;
}

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

/* the writer, for one save: fetch (from HOME), check, bind, buffers, INIT.
 * 0 or a PDE_*; on failure nothing is held. */
static int pdu_wrt_in_now(void)
{
    unsigned seg;

    if (pdu_wrt_in)
        return 0;
    if (w_partfetch(PDU_PART_WRT) != 0)
        return PDE_NOMEM;               /* op_fetch has toasted why */
    pdu_wrt_in = 1;
    seg = os88_part_seg(PDU_PART_WRT);
    if (pdu_mcheck(PDU_M_WRT, seg) != 0) {
        pdu_wrt_out();
        return PDE_LIMIT;
    }
    pdu_wbuf = os88_mem_claim_p(pdu_wkb << 6);
    if (pdu_wbuf == 0) {
        pdu_wrt_out();
        return PDE_NOMEM;
    }
    pdu_xinit.wbuf_seg = pdu_wbuf;
    pdu_xinit.wbuf_kb = pdu_wkb;
    if (pdu_minit(PDU_M_WRT) != 0) {
        pdu_wrt_out();
        return PDE_LIMIT;
    }
    return 0;
}
