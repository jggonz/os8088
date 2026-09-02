/* ============================================================================
 * os8088 - apps/weave/wstate.c
 *
 * saveState / loadState (WEAVE-SPEC 8.3) - THE APP'S ENTIRE FILE ACCESS.
 * #included by apps/weave/weave.c after wnative.c.
 *
 * ---------------------------------------------------------------------------
 * WHAT IS WRITTEN, AND BY WHOM
 * ---------------------------------------------------------------------------
 * The 128 globals, serialized by 8.3's rules - and the routine that does the
 * serializing is apps/weave/wvm.inc's `wvm_save`, which is ALSO what the
 * weavevm gate compares (12.3). That is not code sharing for its own sake: it
 * is what makes the differential a differential. If saveState wrote its own
 * image, the gate would be checking a routine no user ever runs.
 *
 * ---------------------------------------------------------------------------
 * WHERE, AND WHY IT MOVED
 * ---------------------------------------------------------------------------
 * `SYSTEM/APPDATA/<stem>.SAV`, reached by SPEC.md 19.9's bank / GOTO / act /
 * GOTO-back. 8.3 said "beside the bundle" until wave 3 went to write one: a
 * `.WAB` is a USER's document (wave 7 puts bundles in a writable PROJECTS/
 * folder), and 19.9's whole subject is that a program's own state does not go
 * where the user keeps things. The amendment is in 8.3, with the stem
 * collision named as the cost.
 *
 * A DISK WITHOUT THE FOLDER IS NOT AN ERROR. It is a user's own disk, or one
 * written by something else; the volume goes back where it was and the call
 * answers false, which is exactly what a refused write already does (8.3:
 * "Returns false - never a crash"). Cyclone's cy_data_enter carries that rule
 * in its own header and this is the same rule in C.
 *
 * IT RUNS ON THE UI TASK, INSIDE THE ONWAKE SLICE, which is where file slots
 * are legal (4.10) - and it is the reason WEAVE has no worker in this wave: a
 * worker may not touch a file (SPEC.md 20.6 rule 7).
 * ==========================================================================*/

/* The staging claim (1.4's sixth, transient): the image is 6 + 128 cells plus
 * whatever strings and arrays the globals hold, so 2KB covers a state with no
 * long strings in it and 8KB the worst case a 4KB arena can produce. It is
 * taken for the call and given back at the end of it - never held, because
 * the 256KB XT's whole slack with a Weave app open is ~30KB (1.4). */
#define W_SAVKB   8

/* THE WHOLE FILE IS `ovl_*` (WEAVE-SPEC 1.2.1, tenant 5) - the one body in
 * this runtime that is mid-run and still in the overlay, and 1.2.1 carries
 * the three reasons at length. The short one: a refused module load returns
 * 0, `saveState()` answers false, and 8.3 already says false is a normal
 * answer with the status row saying why. Every other mid-run body would have
 * had to invent a meaning for a refusal; this one already had it. */

static struct os88_place w_savhere;
static struct os88_find  w_savf;
static char              w_savname[16];

/* ovl_dive - find the visible directory `name` where we stand and go into it.
 * 1 = we are in it, 0 = it is not here and nothing moved.
 *
 * SPEC.md 19.9 is explicit that SYSTEM/ is FOUND BY WALKING and not assumed,
 * and that it is OSAPI_FILE_GOTO rather than the quiet twin: GOTO_Q moves the
 * global cwd and deliberately not the instance's, so the quiet move is undone
 * by the very next FILE_WRITE. Cyclone's own header records the save that
 * wrote nothing at all while the load path appeared to work. */
static int ovl_dive(const char *name)
{
    int ord;

    ord = 0;
    while (ord >= 0) {
        ord = os88_file_find(ord, &w_savf);
        if (ord < 0)
            break;
        if (w_savf.type != OS88_FT_DIR)
            continue;
        if (!w_samename(w_savf.name, name))
            continue;
        w_savhere2.clus = w_savf.clus;
        w_savhere2.vol = w_savhere.vol;
        return os88_file_goto(&w_savhere2) == 0;
    }
    return 0;
}

/* ovl_data_enter - bank where we stand and go to SYSTEM/APPDATA on OUR volume.
 * 1 = we are there, 0 = we are not and nothing was moved. */
static int ovl_data_enter(void)
{
    os88_file_here(&w_savhere);
    w_savhere2.clus = 0;                /* the ROOT of that same volume */
    w_savhere2.vol = w_savhere.vol;
    if (os88_file_goto(&w_savhere2) != 0)
        return 0;
    if (!ovl_dive("SYSTEM") || !ovl_dive("APPDATA")) {
        os88_file_goto(&w_savhere);
        return 0;
    }
    return 1;
}

static void ovl_data_leave(void)
{
    os88_file_goto(&w_savhere);         /* leaving the instance elsewhere would
                                         * move where every unqualified name it
                                         * passes the file API resolves, and
                                         * where its next dialog opens (19.9) */
}

/* ovl_savstem - `<bundle stem>.SAV`, 8.3's name.  The stem is the bundle's own
 * 8.3 name with its extension replaced, so FORM.WAB saves to FORM.SAV. */
static void ovl_savstem(void)
{
    int i;

    for (i = 0; i < 8 && w_name[i] && w_name[i] != '.'; i++)
        w_savname[i] = w_name[i];
    w_savname[i] = '.';
    w_savname[i + 1] = 'S';
    w_savname[i + 2] = 'A';
    w_savname[i + 3] = 'V';
    w_savname[i + 4] = 0;
}

/* ============================================================================
 * saveState / loadState
 * ==========================================================================*/

static int ovl_savestate(void)
{
    unsigned seg, n;
    int ok;

    if (w_state != W_ST_RUN || w_name[0] == 0)
        return 0;
    seg = os88_mem_claim(W_SAVKB);
    if (seg == 0)
        return 0;                       /* no room: false, and the status row
                                         * says why (8.3) */
    n = wvm_save(seg, 0, W_SAVKB << 10);
    if (n == 0) {
        os88_mem_free(seg);
        w_saysav("saveState: the state does not fit its claim.");
        return 0;
    }
    ok = 0;
    if (ovl_data_enter()) {
        ovl_savstem();
        ok = os88_file_write_seg(w_savname, seg, n) == 0;
        ovl_data_leave();
    } else
        w_saysav("saveState: this disk has no SYSTEM/APPDATA.");
    os88_mem_free(seg);
    if (!ok && w_status[0] == 0)
        w_saysav("saveState: the disk refused the write.");
    return ok;
}

static int ovl_loadstate(void)
{
    unsigned seg, n;
    int ok, g, t, v;

    if (w_state != W_ST_RUN || w_name[0] == 0)
        return 0;
    seg = os88_mem_claim(W_SAVKB);
    if (seg == 0)
        return 0;
    n = 0;
    if (ovl_data_enter()) {
        ovl_savstem();
        n = os88_file_read_seg(w_savname, seg, W_SAVKB << 10);
        ovl_data_leave();
    }
    ok = 0;
    if (n)
        ok = wvm_load(seg, 0, n);
    os88_mem_free(seg);
    if (!ok)
        return 0;

    /* 8.3's rehydration rule, and it is the RUNTIME's half: "a handle that no
     * longer resolves loads as null". wvm.inc may not know which comp_ids
     * this bundle declares - its header forbids it naming the runtime - so it
     * loads the component cells as they stand and this walk nulls the ones
     * that are not ours. A .SAV written by another bundle with the same stem
     * is exactly the case (8.3 names that collision as the cost of 19.9). */
    for (g = 0; g < 128; g++) {
        t = wvm_gtag(g);
        if (t != WT_COMP)
            continue;
        v = wvm_gval(g);
        if (v == 0 || w_find_lay(v) >= 0)
            continue;
        wvm_gset(g, WT_NULL, 0);
    }
    /* Every component may now be showing something the globals disagree with,
     * but nothing on the CARD changed - loadState writes globals and not
     * properties (8.3). So there is nothing to repaint, and repainting the
     * card "to be safe" would be a full-card repaint (0.3-1.2 s on CGA,
     * WEAVE-SPEC 14) for no pixel. */
    return 1;
}
