/* ============================================================================
 * os8088 - apps/pagedesk/pdeng.c                PDENG.MOD (PAGEDESK-SPEC 6-8)
 *
 * The PDF reader segment: file layer, lexer, cross-reference, page tree,
 * streams and filters, and the verbs PDV_ENG_* (pdabi.h). One translation
 * unit - this file includes the rest - compiled for the target by
 * tools/cc8086.py's gates behind pdeng.asm's door, and for the host by
 * hosttest/pde_test.c against hosttest/pdhost.c's arena.
 *
 * The rules that bind it (CLAUDE.md, SPEC.md 73): nothing addressable lives on
 * the stack (every struct is a static), no struct is assigned or passed by
 * value, no long/float, frames under 96 bytes. 32-bit values are pd_u32 and
 * move only through pdm_*.
 * ==========================================================================*/
#include "pdabi.h"
#include "pdmath.h"
#ifdef PD_HOST
#include "pde_os88.h"
#else
#include "os88.h"
#endif
#include "pde.h"
#include "pdeflate.h"

/* ---- the claims PDV_ENG_INIT received (and nothing else) ---------------- */
static pdw g_xb;
static pdw g_cseg, g_ckb, g_aseg, g_akb, g_tseg, g_tkb;
static pdw g_ready;             /* INIT done                                  */
static pdw g_open;              /* a document is open                         */
static pdw g_filed;             /* the file layer has a file                  */

/* ---- verb plumbing ------------------------------------------------------ */
unsigned pde_fail;                 /* the current verb's PDE_* (0 = success)     */
static pdw pde_ax;              /* ...or its answer                           */
unsigned pde_dx;                /* DX out (read by the door)                  */
static pdw pde_ioerr;           /* sticky I/O error of the current verb       */
static pdw err_obj, err_detail; /* what OPEN reports on failure               */

/* ---- scratch u32s: never addressed across a call that reuses them ------- */
static struct pd_u32 s_a, s_b, s_c, s_k;
static struct pd_u32 m_a, m_b, m_c;    /* number-building scratch               */
static struct pd_u32 f_a, f_b;          /* the file layer's scratch              */

/* ---- file layer (pdefile.c) --------------------------------------------- */
static char  fl_name[14];
static pdw   fl_cur[8];         /* the READ_SEQ cursor                        */
static struct pd_u32 fl_size;
static pdw   fl_blk, fl_shift, fl_nblk, fl_clock;
static pdw   fl_blo[PDE_MAXBLK], fl_bhi[PDE_MAXBLK];
static pdw   fl_valid[PDE_MAXBLK];      /* valid bytes in the slot, 0 = empty */
static pdw   fl_stamp[PDE_MAXBLK];
static pdb   tailbuf[PDE_TAIL];
static pdw   fl_reads;          /* READ_SEQ calls: a counter, for the harness */

/* ---- lexer (pdelex.c) ---------------------------------------------------- */
static struct lxs fobj;         /* the object parser's lexer                  */
static pdb   fo_buf[PDE_IB];
static struct lxs *L;           /* the lexer in use                           */
static pdw   lx_err;            /* a fill's failure                           */
static struct pde_tok tk;
static struct pde_val ov;
static struct pd_u32 ob_pos;    /* tell() scratch (ob_value's lookahead)      */
static struct pd_u32 on_pos;    /* ob_num's saved place                       */
static struct pd_u32 ob_hdr;    /* offset of the current object's "n g obj"   */
static pdw   ob_null;           /* the last ob_enter found a free/absent obj  */

/* ---- xref (pdexref.c) ---------------------------------------------------- */
static struct xsub xs[PD_MAXSUBSEC];
static pdw   nxs;
static pdw   rv_lo[PD_MAXREV], rv_n[PD_MAXREV], rv_thin[PD_MAXREV];
static struct pd_u32 xr_seen[PD_MAXREV];
static pdw   nrev;
static struct pd_u32 xr_next;   /* the /Prev to follow, 0 = none              */
static pdw   xo_state;          /* 0 absent, 1 free, 2 in use                 */
static pdw   xr_rev;            /* the revision xr_find answered from (err_detail) */
static struct pd_u32 xo_off;
static pdw   xo_gen;
static pdw   tr_size, tr_root_n, tr_root_g, tr_info_n, tr_info_g, tr_has_root;
static struct pd_u32 tr_start;  /* newest startxref                           */
static struct pd_u32 tr_dict;   /* newest trailer dict, just after "<<"       */
static pdb   tr_id[16];
static pdw   tr_idlen, tr_hasid;
static pdw   doc_ver, doc_flags;

/* ---- pages (pdepage.c) --------------------------------------------------- */
static pdw   npages;
static struct pw_frame pw_st[PD_MAXDEPTH];
static pdw   pw_sp, pw_visits, pw_root_n, pw_root_g;
static pdw   arena_top;

/* ---- streams (pdestrm.c) ------------------------------------------------- */
static struct sctx ctxs[PDE_MAXCTX];
static pdw   nctx;
static pdb   tmpb[128];
/* the stream header parse (ob_stream) */
static pdw   sp_kind;
static struct pd_u32 sp_off, sp_dstart, sp_len;
static pdw   sp_nflt, sp_filters;
static pdb   sp_flt[PD_MAXFILTERS];

/* ---- the document's open state ------------------------------------------ */
static pdw   op_state;
static struct pdxb_open xo;

/* ---- helpers shared by every part --------------------------------------- */
static pdw eof_err(void) { return pde_ioerr ? PDE_IO : PDE_TRUNC; }

static void u32_zero(struct pd_u32 *r) { r->lo = 0; r->hi = 0; }
static void u32_cp(struct pd_u32 *r, const struct pd_u32 *a) { r->lo = a->lo; r->hi = a->hi; }
static void u32_sub16(struct pd_u32 *r, pdw n)
{
    pdm_u32_set(&s_k, 0, n);
    pdm_u32_sub(r, r, &s_k);
}
static pdw u32_is0(const struct pd_u32 *a) { return a->lo == 0 && a->hi == 0; }

#ifdef PD_HOST
static int str_eq(const pdb *a, const char *b)
{
    while (*a && *b) {
        if (*a != (pdb)*b) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}
#else
int str_eq(const pdb *a, const char *b);    /* pdelex.inc (perf) */
#endif
static int upc(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

#include "pdefile.c"
#include "pdelex.c"
#include "pdexref.c"
#include "pdepage.c"
#include "pdestrm.c"
#include "pdeflate.c"
#include "pdesrch.c"             /* nav: SEARCH (20.10.1) */
#include "pdeverb.c"
