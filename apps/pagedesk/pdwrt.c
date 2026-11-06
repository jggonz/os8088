/* ============================================================================
 * os8088 - apps/pagedesk/pdwrt.c                     PDWRT.MOD (PAGEDESK-SPEC 20.5)
 *
 * The writer module: serialiser, appearance streams for every annotation kind
 * of 13.3, the incremental save (14.2) and the fresh document (14.6). One
 * translation unit (SPEC.md 73.1) - the pdw*.c files are #included in order -
 * compiled twice: by cc8086 for the target (pdwrt.asm is the door) and by the
 * host `cc -DPD_HOST` for apps/pagedesk/hosttest/pdw_test.c, which #includes
 * THIS file so it can reach the statics.
 *
 * Verbs (pdabi.h 0x80-0x84), calling convention PAGEDESK-SPEC 3.1: the door
 * (pdwrt.asm) pushes AL, BX, CX, DX and calls pdw_verb; the answer is AX, with
 * CF = 1 when it is a failure (every PDE_* but PDE_OK and PDE_BUSY).
 * ==========================================================================*/
#ifdef PD_HOST
#include "pdw.h"
#define PDW_HOST_SCRATCH 0x3000u        /* the harness's raw-copy landing zone */
#else
#include "os88.h"
#include "pdw.h"
#endif

pdw pdw_engcf;                          /* CF of the last PDENG call          */
pdw pdw_cf;                             /* CF of this door's answer           */
pdw pdw_engvec[2];                      /* PDENG's door, offset then segment  */

#include "pdwio.c"
#include "pdwser.c"
#include "pdwap.c"
#include "pdwscan.c"
#include "pdwsave.c"

/* ---- the verbs ------------------------------------------------------------ */
static pdw v_init(pdw bx)
{
    static struct pdxb_init in;
    g_xb = bx;
    pd_fcopy_in(bx, 0, &in, sizeof in);
    pdw_engvec[0] = in.eng_off; pdw_engvec[1] = in.eng_seg;
    g_wseg = in.wbuf_seg; g_wkb = in.wbuf_kb;
    if (sv_active || g_created) pdw_io_delete(g_tmp);   /* 20.5.1: an INIT drops a job */
    job_clear();
    return PDE_OK;
}

static pdw v_abort(void)
{
    if (sv_active || g_created) pdw_io_delete(g_tmp);
    job_clear();
    return PDE_OK;
}

static pdw v_job(pdw fresh)
{
    pdw r, replace = 0, keep = 0, st;
    pd_fcopy_in(g_xb, 0, &g_sv, sizeof g_sv);
    st = g_sv.state;
    if (st == 0) {
        if (sv_active) v_abort();
        r = job_begin(fresh);
        if (r) { pd_fcopy_out(&g_sv, g_xb, 0, sizeof g_sv); return job_fail(r); }
    } else if (!sv_active || sv_fresh != fresh) {
        return PDE_RANGE;
    } else if (st == PDW_ST_REPLACE) {
        if (sv_phase != PH_RENAME) return PDE_RANGE;
        replace = 1;
    }
    r = PDE_BUSY;
    if (sv_phase != PH_RENAME) r = job_slice();
    else r = PDE_OK;
    if (r == PDE_OK && sv_phase == PH_RENAME) {
        r = job_rename(replace);
        if (r == PDE_EXISTS) {                         /* ask, then call again */
            g_sv.state = PDW_ST_CONFIRM;
        } else if (r == PDE_OK) {
            g_sv.state = PDW_ST_DONE; job_clear();
        } else {
            keep = sv_tgone;                           /* delete done, rename failed */
            if (keep) { g_sv.err_detail = 1; sv_active = 0; sv_phase = PH_IDLE; g_created = 0; }
            else r = job_fail(r);
        }
    } else if (r == PDE_BUSY) {
        g_sv.state = (pdw)(sv_phase + 1);
    } else {
        r = job_fail(r);
    }
    if (sv_active) { u32cp(&g_sv.written, &g_pos); u32cp(&g_sv.total, &sv_total); }
    else if (r == PDE_OK) u32cp(&g_sv.written, &g_pos);
    pd_fcopy_out(&g_sv, g_xb, 0, sizeof g_sv);
    return r;
}

unsigned pdw_verb(unsigned verb, unsigned bx, unsigned cx, unsigned dx)
{
    pdw r;
    (void)cx; (void)dx;
    switch (verb) {
    case PDV_WRT_INIT:    r = v_init(bx); break;
    case PDV_WRT_SAVE:    r = v_job(0); break;
    case PDV_WRT_NEWDOC:  r = v_job(1); break;
    case PDV_WRT_EXTRACT: r = PDE_LIMIT; break;     /* graph copy: wave 2 (20.5.12) */
    case PDV_WRT_ABORT:   r = v_abort(); break;
    default:              r = PDE_RANGE; break;
    }
    pdw_cf = (pdw)(r != PDE_OK && r != PDE_BUSY);
    return r;
}
