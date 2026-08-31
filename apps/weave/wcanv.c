/* ============================================================================
 * os8088 - apps/weave/wcanv.c
 *
 * The `<canvas>`/`<sprite>` component's UI-TASK half (WEAVE-SPEC 6.10). The
 * other half - the frame loop, the composer, the collision pass, the key poll
 * and the staging ring - is WEAVE.WSM, a second RESIDENT segment beside the
 * package (1.2.2), because every byte of it runs on a worker and SPEC.md
 * 73.14's overlay refuses a worker at its first instruction.
 *
 * So what is HERE is only the seams, and that is the point: the load, the
 * paint arm, the native surface's two arms, the drain and the free. Every
 * counter and every sprite field the C side wants is read and written with
 * wblob.inc's w_b/w_w/w_pb/w_pw against the module's own state block and the
 * canvas claim, which needs no verb and no resident byte at all - wave 5
 * opened with 946 bytes (WEAVE-PLAN 2.9) and this file is why that was
 * enough.
 *
 * #included by apps/weave/weave.c: `nasm -f bin` has no notion of an external
 * symbol, so a C package is ONE compilation (SPEC.md 73.1).
 * ==========================================================================*/

/* --- the state, all of it per INSTANCE and none of it re-entrant ---------- */
static unsigned w_cseg;                 /* the canvas claim, 0 = none */
static unsigned w_ckb;
static unsigned w_cmseg;                /* WEAVE.WSM's claim, 0 = not loaded */
static unsigned w_cmkb;
static unsigned w_cso;                  /* ...and its state block's offset */
static int      w_cid;                  /* the canvas's comp_id, 0 = no canvas */
static int      w_cnspr;
static int      w_credraw;              /* a sprite changed between frames */
static int      w_cparm[WSMP_NW];       /* WSMV_BIND's block */
static int      w_crec[4];              /* one drained staging record */
static int      w_cldstate;             /* ovl_canvasload's answer - see there */
static int      w_cworker;              /* the worker is hired (SPEC.md 20.6
                                         * rule 1: one per instance, ever) */

/* ============================================================================
 * THE SPRITE TABLE
 *
 * A sprite is NOT in w_lay[]: the flow walk skips it (7.2) because it is not
 * a flow component, so w_find_lay() answers -1 for one and every GETP/SETP
 * would raise `no component`. It does not go in w_lay either - that struct is
 * ten bytes x 250 and an eleventh field costs 500 of them (weave.c says so).
 *
 * It needs no table at all. Sprite records follow their canvas DIRECTLY in
 * UISTREAM and comp_ids are 1..n in document order (2.5, and wval.c refuses
 * anything else), so sprite k's comp_id is the canvas's + 1 + k and the
 * arithmetic is the table.
 * ==========================================================================*/
static int w_find_spr(int id)
{
    int k;

    k = id - w_cid - 1;
    if (w_cid == 0 || (unsigned)k >= (unsigned)w_cnspr)
        return -1;                      /* one UNSIGNED compare for both ends,
                                         * which is the idiom and not a trick:
                                         * a negative k wraps above any
                                         * possible w_cnspr */
    return k;
}

static unsigned w_sprget(int k, int field)
{
    return wcv_call(WSMV_SPRITE, (unsigned)k, (unsigned)field, 0);
}

static int w_sprset(int k, int field, int v)
{
    return (int)wcv_call(WSMV_SPRITE | 0x0100, (unsigned)k,
                         (unsigned)field, (unsigned)v);
}

/* ============================================================================
 * THE NATIVE SURFACE (WEAVE-SPEC 6.10)
 *
 * sprite: x, y (px), vx, vy (1/16-px per frame), frame, shown - get and set.
 * canvas: start(fps), stop(), and `hidden` off the common surface. A canvas
 * has no readable property of its own, which is 6's SURFACE table and not an
 * omission: everything a script wants to know about a game is in its sprites.
 * ==========================================================================*/
static int w_cspr_get(int k, int atom)
{
    /* WSMF_X..WSMF_SHOWN are 0..5 and 2.7.1's `x`..`shown` are atoms 7..12,
     * so the map is a SUBTRACTION. That is not a coincidence to be relied on
     * quietly - wsmabi.inc's field ids were chosen this way, and weave.asm's
     * drift guard is what keeps the two in step - and it is worth about 400
     * bytes of resident image against the twelve compares it replaces. */
    if (atom < WA_X || atom > WA_SHOWN)
        return w_nerr(WE_NOPROP, atom, 0);
    return w_nres(WT_INT, (int)w_sprget(k, atom - WA_X));
}

static int w_cspr_set(int k, int atom, int v)
{
    if (atom < WA_X || atom > WA_SHOWN)
        return w_nerr(WE_NOPROP, atom, 0);
    if (w_sprset(k, atom - WA_X, v) == 0)   /* the ONE refusal a sprite write
                                             * has: 10.6.1's frame %d of %d. */
        return w_nerr(WE_FRAME, v, (int)w_sprget(k, WSMF_NFRAME));
    /* 6.10: writes from JS land BETWEEN frames - the worker reads the record
     * once a frame - so a RUNNING canvas needs no repaint here at all. A
     * stopped one does, and it is one composed run rather than a card. */
    if (w_b(w_cmseg, w_cso + WSS_RUN) == 0)
        w_credraw = 1;
    return w_nres(WT_NULL, 0);
}

/* w_chire - the worker, hired ONCE per instance and parked between runs.
 *
 * SPEC.md 20.6 rule 2 has no un-spawn: a worker that exited would leave the
 * kernel holding a dead task slot and the instance would leak for the
 * session. So start()/stop() raise and clear a flag the loop reads, and the
 * task itself lives from the first start() to the close box.
 *
 * A REFUSAL IS NORMAL AND TRANSIENT (SPEC.md 20.6): MAX_TASKS is 8 and the
 * Timers, the Task Manager and a transient SB task draw from the same seven
 * slots. So this RETRIES on every start() and latches only on success -
 * apps/fractal's fr_hire is the reference - and a canvas that cannot have one
 * says so through 10.6.1 rather than half-running. */
static int w_chire(void)
{
    if (w_cmseg == 0 || w_cseg == 0)
        return 0;
    if (w_cworker)
        return 1;
    if (os88_task_spawn(w_win) != 0)
        return 0;
    w_cworker = 1;
    return 1;
}

/* ============================================================================
 * OVERLAY TENANT 8: THE CANVAS'S LOAD PATH (WEAVE-SPEC 1.2.1, 1.2.2)
 *
 * It runs exactly once per bundle, at open, on the UI task, from inside
 * tenant 4's own body; it draws nothing and no handler can reach it; and its
 * refusals already have meanings, because a claim that cannot be had is
 * 10.1's sentence and a module that is not on the disk is 10.3's. That is the
 * paragraph 1.2.1 asks a new tenant to be able to write, and it is the same
 * one tenant 6 wrote about the grid.
 *
 * IT ANSWERS THROUGH w_cldstate AND NOT THROUGH A RETURN VALUE, for 1.2's
 * rule: a refused overlay returns 0 and 0 is also a perfectly good "no canvas
 * in this bundle".
 *
 *   0 fine   1 no room for a claim   2 the OVERLAY never ran
 *   3 WEAVE.WSM is not on this disk  4 WEAVE.WSM does not match
 * ==========================================================================*/
static void ovl_canvasload(void)
{
    unsigned s, n, i, rec, id, ct, props, got, need;
    int k;

    w_cldstate = 0;
    w_cid = 0;
    w_cnspr = 0;
    if ((w_flags & WABF_CANVAS) == 0)
        return;                         /* no canvas: not a byte of heap, not
                                         * a disk revolution, not a KB of
                                         * 10.1's ask (1.2.2) */

    /* Which component IS the canvas, and how many sprites follow it?  wval.c
     * has already refused a bundle with two canvases or with w/h out of
     * 3.3's range, so this walk believes what it reads. */
    s = w_soff[W_UISTREAM];
    n = w_sextra[W_UISTREAM];
    for (i = 0; i < n; i++) {
        rec = s + i * W_REC_SIZE;
        if (w_b(w_seg, rec) != W_REC_COMP)
            continue;
        ct = w_b(w_seg, rec + W_R_CTYPE);
        if (ct == WC_SPRITE && w_cid) {
            w_cnspr++;
            continue;
        }
        if (ct != WC_CANVAS)
            continue;
        id = w_b(w_seg, rec + W_R_ID);
        props = w_w(w_seg, rec + W_R_PROPS);
        w_cid = (int)id;
        w_cparm[WSMP_W] = (int)w_b(w_seg, rec + W_R_W) << 3;
        w_cparm[WSMP_H] = (int)w_b(w_seg, rec + W_R_H) << 3;
        w_cparm[WSMP_WALLS] = (int)w_pint(props, WA_WALLS, 0x0F);
        w_cparm[WSMP_TICK] = (int)w_pint(props, WA_TICK, 0);
        w_cparm[WSMP_CID] = (int)id;
    }
    if (w_cid == 0)
        return;                         /* WABF_CANVAS with no <canvas> is a
                                         * malformed bundle wval.c already
                                         * refused; belt and braces */
    if (w_cnspr > WSM_MAXSPR)
        w_cnspr = WSM_MAXSPR;
    w_cparm[WSMP_NSPR] = w_cnspr;
    w_cparm[WSMP_SPOFF] = (int)w_soff[W_SPRITES];

    /* --- the module, read ONCE and stamped three ways (1.2.2) ------------- */
    if (w_cmseg == 0) {
        w_cmkb = (unsigned)wcv_kb();
        w_cmseg = os88_mem_claim((int)w_cmkb);
        if (w_cmseg == 0) {
            w_cldstate = 1;
            return;
        }
        got = os88_file_read_seg("WEAVE.WSM", w_cmseg, w_cmkb << 10);
        if (got < wcv_bytes()) {
            os88_mem_free(w_cmseg);
            w_cmseg = 0;
            w_cldstate = 3;
            return;
        }
        k = wcv_stamp(w_cmseg);         /* the three words, in the assembly
                                         * that owns their constants */
        if (k) {
            os88_mem_free(w_cmseg);     /* a half-believed module is worse
                                         * than none (crt0.asm's rule) */
            w_cmseg = 0;
            w_cldstate = k == 1 ? 3 : 4;
            return;
        }
        w_cso = wcv_stateoff(w_cmseg);
        wcv_bindmod(w_cmseg);
    }

    /* --- the canvas claim, zeroed: 6.10.4's birth state ------------------- */
    w_cseg = os88_mem_claim((int)w_canvaskb);
    if (w_cseg == 0) {
        w_cldstate = 1;
        return;
    }
    w_ckb = w_canvaskb;
    need = 16 + 24 * (unsigned)w_cnspr
           + ((unsigned)w_cparm[WSMP_W] >> 3) * (unsigned)w_cparm[WSMP_H];
    if (need > (w_ckb << 10))
        need = w_ckb << 10;             /* 6.10.4 sizes the ask so this cannot
                                         * fire; a hostile header is why it is
                                         * a clamp and not an assertion */
    for (i = 0; i < need; i += 2)
        w_pw(w_cseg, i, 0);
    wcv_call(WSMV_BIND, w_cseg, w_seg, (unsigned)w_cparm);

    /* --- and every sprite's descriptor and birth state -------------------- */
    k = 0;
    for (i = 0; i < n && k < w_cnspr; i++) {
        rec = s + i * W_REC_SIZE;
        if (w_b(w_seg, rec) != W_REC_COMP)
            continue;
        if (w_b(w_seg, rec + W_R_CTYPE) != WC_SPRITE)
            continue;
        props = w_w(w_seg, rec + W_R_PROPS);
        /* 3.3: `img` compiles to a PK_SPRITE record named by atom 11, and the
         * record DOUBLES as frame's initial value - there is no img atom */
        w_sprset(k, WSMF_DESC, (int)w_pint(props, WA_FRAME, 0));
        w_sprset(k, WSMF_X, (int)w_pint(props, WA_X, 0));
        w_sprset(k, WSMF_Y, (int)w_pint(props, WA_Y, 0));
        w_sprset(k, WSMF_SHOWN, (int)w_pint(props, WA_SHOWN, 1));
        k++;
    }
}

/* ============================================================================
 * THE PAINT ARM (WEAVE-SPEC 6.10.2)
 * ==========================================================================*/
static void w_cpaint(int i)
{
    if (w_cseg == 0) {
        wd_box(w_rect[0], w_rect[1], w_rect[2], w_rect[3]);
        return;                         /* WABF_CANVAS refuses the bundle when
                                         * GFX_BLIT1 is absent (10.2), so this
                                         * frame is only ever the no-canvas
                                         * case a malformed record could make */
    }
    /* Where the canvas sits in the CONTENT GRID, in cells: the worker adds it
     * to what OSAPI_WM_CONTENT answers every frame, so a window the user has
     * dragged is right on the next FRAME rather than on the next repaint. */
    wcv_call(WSMV_PLACE, (unsigned)w_lay[i].cx, (unsigned)w_lay[i].cy, 0);
    wcv_call(WSMV_PAINT, (unsigned)w_rect[0], (unsigned)w_rect[1], 0);
}

/* w_cflush - a sprite changed from SCRIPT while the loop was not running, so
 * the picture is behind. One composed run, not a card repaint. */
static void w_cflush(void)
{
    int i;

    w_credraw = 0;
    i = w_find_lay(w_cid);
    if (i < 0)
        return;
    w_rectof(i);
    w_cpaint(i);
}

/* ============================================================================
 * THE DRAIN - the UI task's half of 6.10.6's handshake
 *
 * The worker STAGES and this COMMITS, because a worker may not write the VM's
 * ring: wvm_enq runs with DS on the VM claim and its head and count are two
 * unlocked words, and SPEC.md 20.6 rule 3 forbids a worker to take a lock.
 * Every record goes through w_enq, so the whole of 4.9's policy is applied
 * once, by the core, where it already lives.
 * ==========================================================================*/
static void w_cdrain(void)
{
    int guard;

    if (w_cmseg == 0)
        return;
    guard = 40;
    while (guard-- > 0) {
        if (wcv_call(WSMV_DRAIN, 0, 0, (unsigned)w_crec) == 0)
            return;
        w_enq(w_crec[0], w_crec[1], w_crec[2], w_crec[3]);
    }
}

/* ============================================================================
 * THE FREE - and the ORDER is the whole of it
 * ==========================================================================*/
static void w_cfree(void)
{
    /* UNBIND first, and it does not return until the worker has acknowledged:
     * a frame half-composed into freed memory does not fault on this machine,
     * it writes over whatever the heap has handed to somebody else (the same
     * reason w_gfree unbinds the FX VM before it frees the grid claim). */
    wcv_call(WSMV_UNBIND, 0, 0, 0);
    if (w_cseg) {
        os88_mem_free(w_cseg);
        w_cseg = 0;
    }
    w_ckb = 0;
    w_cid = 0;
    w_cnspr = 0;
    w_credraw = 0;
    /* THE MODULE IS NOT FREED. It is resident for the life of the instance
     * (1.2.2) - a reload re-binds it, and re-reading a floppy on every ^R
     * would put ~400 ms of int 13h on the edit-run loop 1.7 exists to keep
     * short. It goes back with the region when the instance does. */
}
