/* ============================================================================
 * os8088 - apps/weave/wload.c
 *
 * THE WAY IN (WEAVE-SPEC 1.5, 10.1-10.4). #included by
 * apps/weave/weave.c.
 *
 * ---------------------------------------------------------------------------
 * TWO WAYS IN, ONE DECISION
 * ---------------------------------------------------------------------------
 * A bundle arrives by a double-click on the `.WAB` (the association route) or
 * through File -> Open (the dialog route), and WEAVE-SPEC 1.6's Deck will be
 * the third. All of them meet at w_open(), which is Frotz's zi_dlg_story rule:
 * reusing a loader means reproducing its CALL, not just its preconditions, and
 * a loader that reads its STATE rather than its registers is the shape that
 * never had the bug (SPEC.md 54.8's second trap).
 *
 * The association route has an extra step in front - the GOTO - and that is
 * the whole of the first trap: OSAPI_ARG_FILE hands back a name AND the pair
 * that locates it, and the kernel is standing in the PROGRAM's directory, not
 * the document's. An app that banks only the name works perfectly for every
 * document in the same folder as the program - which is the entire root of a
 * disk, and the way every test disk gets built - and reports "missing" for
 * every document in a folder. In C the banking half is closed by construction,
 * because os88_arg_file() cannot give you the name without the locator; the
 * SPENDING half is not, and is w_openpend()'s first three lines.
 *
 * ---------------------------------------------------------------------------
 * REFUSING BEFORE THE READ (10.1)
 * ---------------------------------------------------------------------------
 * The ask is `ceil(filesize/1024) + vmKB + gridKB + canvasKB` - the file size
 * from the directory entry, the three claim-KB bytes from the bundle's own
 * 32-byte header. That is what the header carries them FOR (2.2), and it is
 * why the format is never compressed: the directory size must stand for the
 * resident requirement, or the refusal cannot happen before the read.
 *
 * Which needs a file's first sector without reading the file, and there is
 * exactly one door to that: os88_file_read_at(). os88_file_read() given a
 * capacity smaller than the file answers FERR_BIG - decided from the directory
 * entry before any data I/O - and leaves the buffer alone.
 *
 * The read is NOT free, and the comment this idiom was copied from says it is.
 * apps/frotz/zio.inc claims OSAPI_FILE_FIND "walks the mount snapshot, which
 * is in RAM, so the walk costs no motor time" - it does not: names resolve
 * through the raw directory SECTORS, re-read per ordinal. It is still far
 * cheaper than reading a 24KB bundle into a claim that may not be returnable,
 * so 10.1's rule stands; what does not stand is the reasoning. The DIALOG
 * route genuinely is free, because the dialog hands the size over having read
 * it out of the snapshot.
 * ==========================================================================*/

/* w_free - give the bundle claim back.  Called BEFORE a new one is taken on a
 * reload, never after: otherwise both have to fit at once, which on the 256KB
 * XT is the difference between a reload that works and one that refuses
 * (apps/frotz/zio.inc's zi_free_all rule). */
static void w_free(void)
{
    /* THE VM GOES FIRST, and wvm_unbind before the free rather than after:
     * a slice that ran between the two would read a segment the heap has
     * given to somebody else, and on this machine that does not fault
     * (LESSONS.md 4). Unbinding is one word - the core answers WR_IDLE to
     * everything once the claim is 0 - and it costs nothing to do it in the
     * order that cannot be wrong. */
    wvm_unbind();
    w_gfree();                          /* ...and the grid claim, for the same
                                         * reason and in the same order: the
                                         * FX VM is unbound before the memory
                                         * it was pointed at goes back */
    w_cfree();                          /* ...and the canvas claim, which has
                                         * a SECOND TASK in it: w_cfree's
                                         * unbind does not return until the
                                         * worker has acknowledged, because a
                                         * frame half-composed into freed
                                         * memory does not fault here, it
                                         * writes over somebody else's claim
                                         * (WEAVE-SPEC 6.10.6) */
    if (w_vmseg) {
        os88_mem_free(w_vmseg);
        w_vmseg = 0;
    }
    if (w_seg) {
        os88_mem_free(w_seg);
        w_seg = 0;
        w_claimkb = 0;
    }
    w_state = W_ST_DECK;
    w_nlay = 0;
    w_nrow = 0;
    w_lay_cw = -1;                      /* the cached walk is void */
    w_alertfn = -1;
    w_alertkind = 0;
    w_tscript_on = 0;
    w_bdown = -1;
}

/* w_fail - a refusal, complete: the state, the sentence on the glass, the
 * status row that keeps it, and the toast (10.1's shape, C64-SPEC 1.4's). */
static void w_fail(const char *s)
{
    w_free();
    w_state = W_ST_ERR;
    w_say(s);
    w_menusync();
}

/* w_bad - 10.4's sentence, with the field named.  The runtime never guesses
 * past a bad header: every byte off a disk is hostile (SPEC.md 19), and a
 * `.WAB` on a disk need never have been through a packer. */
static void w_bad(const char *field)
{
    w_l0();
    w_ls(w_name);
    w_ls(" is not a Weave bundle (");
    w_ls(field);
    w_ls(").");
    w_fail(w_line);
}

/* w_short - 10.1's sentence, naming BOTH figures.  Refuse when the total free
 * is short OR when the largest free run cannot hold the largest single claim:
 * a heap with 90KB free in three pieces cannot host a 24KB bundle claim, and
 * saying only the total would be a refusal the user cannot act on. */
static void w_short(unsigned ask, unsigned largest)
{
    w_l0();
    w_ls("This app needs ");
    w_ln(ask);
    w_ls("KB; the largest free run is ");
    w_ln(largest);
    w_ls("KB.");
    w_fail(w_line);
}

/* w_missing - 10.3.  A double-click that reaches a deleted file and an empty
 * Deck directory both land here. */
static void w_missing(void)
{
    w_l0();
    w_ls(w_name);
    w_ls(" missing");
    w_fail(w_line);
}

/* --- the capability test (10.2) ----------------------------------------- */

/* One byte of paper, and the only band this program ever blits in wave 2.
 * `bits` is 1 = lit, so a zero byte is eight pixels of background. */
static const unsigned char w_blitprobe = 0;

/* ovl_can_canvas - does THIS kernel carry GFX_BLIT1?
 *
 * 10.2 says the flags are checked against machine capabilities BY TESTING THE
 * FACT - the slot's own answer - and never by a guess about machine size
 * (SPEC.md 47: grey a fact). kern_small carries the slot and not the body, and
 * answers -1 with nothing drawn.
 *
 * There is no side-effect-free probe: a call that succeeds draws. So the probe
 * is eight pixels of PAPER at the content origin, made from inside the first
 * W_PAINT, at the moment the kernel has just whitened the content and before
 * this program has drawn anything - and row 0 is overwritten by the app-name
 * run a few microseconds later whichever way the answer goes. One primitive
 * call, once per instance, over a cell that was about to be painted anyway.
 * It is not a double-draw of anything, and it is the honest alternative to
 * inferring the kernel from the machine. */
static int ovl_can_canvas(void)
{
    return os88_gfx_blit1(&w_blitprobe, 1, w_ox, w_oy, 8, 1) == 0;
}

/* --- the header probe (10.1) -------------------------------------------- */

/* ovl_probe_read - one cluster of `name` at offset 0, into w_probe[].
 * 1 = the header is in the buffer; 0 = we could not take a probe at all.
 *
 * BOTH the offset and the capacity must be a whole number of clusters, or the
 * slot answers FERR_NAME and reads nothing (SPEC.md 18.4.4). The final chunk
 * is the exception the arithmetic makes for itself - a file shorter than a
 * cluster arrives whole - which is why a 736-byte bundle is readable with a
 * 1024-byte capacity.
 *
 * THE HONEST DEGRADATION, and it is apps/frotz/zpic.inc's: a volume whose
 * cluster is bigger than this buffer (a large hard-disk partition) cannot be
 * probed, because reading LESS than a whole cluster is refused rather than
 * mis-read. We do not guess a smaller read and we do not pretend the header
 * said something; we answer 0, and w_open() says which fact was tested. */
static int ovl_probe_read(const char *name)
{
    unsigned cap, n;

    if (w_clus == 0)
        w_clus = os88_disk_cluster_sectors();
    if (w_clus < 1)
        return 0;
    if (w_clus > W_PROBE / 512)
        return 0;                       /* a cluster we cannot read a whole
                                         * one of - see above */
    cap = (unsigned)w_clus * 512;
    n = os88_file_read_at(name, w_probe, cap, 0, 0);
    if (n == 0)
        return 0;
    w_probelen = n;
    return 1;
}

/* --- the one load path -------------------------------------------------- */

/* w_open - the whole of WEAVE-SPEC 10, in the order the contract gives.
 *
 * `size_lo`/`size_hi` are the DIRECTORY's, from the dialog's completion or
 * from the FIND the association route did. They are what the refusal is
 * computed from, and the header's total-size word is checked against them
 * rather than the other way round.
 *
 * The claim order matters on a small machine: the bundle claim is the one that
 * can fail for size, so it is taken first and everything optional after it
 * (apps/frotz/zio.inc's zi_load rule). Wave 2 takes only that one - the VM,
 * grid and canvas claims arrive with the pieces that use them - but the ASK is
 * already the whole instance's, because 10.1 refuses on the total and a
 * refusal that only counted what wave 2 happens to claim would let a bundle
 * start and then run out. */
static int ovl_open(void *win, const char *name, unsigned size_lo,
                    unsigned size_hi)
{
    const char *e;
    unsigned kb, ask, biggest, got;
    int probed;

    (void)win;
    os88_strcpy(w_name, name, sizeof(w_name));
    w_free();
    w_status[0] = 0;   /* one array, and the error screen's row 0 draws
                        * from it too (weave.c) */
    w_vmkb = 0;                         /* so that a refusal taken before the
                                         * header was read names an ask made
                                         * only of what IS known - the file
                                         * size - rather than of the last
                                         * bundle's claim bytes */
    w_gridkb = 0;
    w_canvaskb = 0;

    /* 1. The size, from the entry.  Over 64KB is not a bundle: 2.1 caps the
     *    total at 0xF800 so that every internal offset is a 16-bit word within
     *    one segment. This is a MALFORMED bundle, not a memory refusal - the
     *    file cannot be what it claims to be at any heap size. */
    if (size_hi != 0 || size_lo > W_CAP || size_lo < W_HDR_SIZE) {
        w_bad(w_f_total);
        return 1;
    }
    w_fsize = size_lo;

    /* 2. The first cluster, and the header out of it (10.1). */
    probed = ovl_probe_read(name);
    if (probed) {
        e = w_val_header();
        if (e) {
            w_bad(e);
            return 1;
        }

        /* 3. The ask, and BOTH halves of the refusal. os88_mem_avail answers
         *    the largest free run and the total free separately, and 10.1
         *    needs both: a heap with room overall but no run big enough for
         *    the largest single claim cannot host this app either. */
        kb = (w_fsize + 1023) >> 10;
        ask = kb + w_vmkb + w_gridkb + w_canvaskb;
        if (w_flags & WABF_CANVAS)
            ask += (unsigned)wcv_kb();  /* 10.1: WEAVE.WSM's claim, and ONLY
                                         * when the bundle declares a canvas
                                         * (1.2.2 - a bundle without one pays
                                         * nothing at all) */
        biggest = kb;
        if (w_vmkb > biggest)
            biggest = w_vmkb;
        if (w_gridkb > biggest)
            biggest = w_gridkb;
        if (w_canvaskb > biggest)
            biggest = w_canvaskb;
        if (ask > os88_mem_total_kb() || biggest > os88_mem_largest_kb()) {
            w_short(ask, os88_mem_largest_kb());
            return 1;
        }

        /* 4. Capability, by the slot's own answer (10.2). WABF_CANVAS refuses
         *    the LOAD; WABF_TIMER loads with its sentence and a static caret -
         *    and that second test is not made in this wave, because
         *    OSAPI_WM_TIMER has no C thunk and the alternative is guessing the
         *    kernel from the machine, which SPEC.md 47 forbids. See the
         *    report; it is a missing thunk, not a missing decision. */
        if ((w_flags & WABF_CANVAS) && !ovl_can_canvas()) {
            w_fail("This app draws on a canvas (GFX_BLIT1); "
                   "this kernel does not carry it.");
            return 1;
        }
    } else {
        /* The probe could not be taken - the volume's cluster is larger than
         * the buffer. 10.1's refusal is not computable here, so it is not
         * attempted: we take the claim the FILE SIZE justifies, read, and
         * validate the header out of the claim like everything else. What is
         * lost is the pre-read refusal, and the fact that was tested is the
         * cluster size, not a guess about the machine. */
        kb = (w_fsize + 1023) >> 10;
    }

    /* 5. The claim, then the read.  A claim base is KB-aligned and therefore
     *    512-aligned, which is what int 13h needs to avoid answering a
     *    transfer that straddles a 64KB physical boundary with error 09h
     *    (SPEC.md 2.1.1) - the symptom of getting that wrong is "Disk error"
     *    on a large read and QEMU never shows it. */
    w_seg = os88_mem_claim((int)kb);
    if (w_seg == 0) {
        w_short((kb + w_vmkb + w_gridkb + w_canvaskb), os88_mem_largest_kb());
        return 1;
    }
    w_claimkb = kb;
    got = os88_file_read_seg(name, w_seg, kb << 10);
    if (got != w_fsize) {
        w_missing();                    /* 10.3: missing OR unreadable - the
                                         * file went away between the FIND and
                                         * here, or the read failed */
        return 1;
    }

    if (!probed) {
        /* The header, from the claim this time. Everything w_val_header()
         * reads lives in the first 32 bytes, so the probe buffer is filled
         * from the claim rather than the routine being written twice. */
        w_copy(w_seg, 0, (char *)w_probe, W_HDR_SIZE);
        w_probelen = W_HDR_SIZE;
        e = w_val_header();
        if (e) {
            w_bad(e);
            return 1;
        }
        if ((w_flags & WABF_CANVAS) && !ovl_can_canvas()) {
            w_fail("This app draws on a canvas (GFX_BLIT1); "
                   "this kernel does not carry it.");
            return 1;
        }
    }

    /* 6. ...and only now is a single offset in it believed (10.4). */
    e = w_validate();
    if (e) {
        w_bad(e);
        return 1;
    }

    w_state = W_ST_RUN;
    w_pstate();                         /* the LAST bundle's scroll positions
                                         * and selections are not this one's,
                                         * and they are indexed by comp_id -
                                         * which the new bundle reuses (2.5) */
    w_istate();
    ovl_comp_init();                      /* every component's birth state, and
                                         * a field block for every <input> */
    /* 5.6's cell store, in a claim of its own, built by overlay tenant 6.
     *
     * A REFUSED OVERLAY RETURNS 0 (apps/cc/crt0.asm), so "it ran" and "what
     * it decided" have to be two different answers - WEAVE-SPEC 1.2's rule,
     * and wave 3's lesson said again. w_glderr is set to 2 BEFORE the call
     * and the body overwrites it, so a 2 coming back means the body never
     * ran at all and the sentence names the module rather than the memory. */
    w_glderr = 2;
    ovl_gridload();
    if (w_glderr == 1) {
        w_short(w_claimkb + w_vmkb + w_gridkb, os88_mem_largest_kb());
        return 1;
    }
    if (w_glderr == 2) {
        w_fail("WEAVE.OVL is missing or stale; no bundle can open.");
        return 1;
    }

    /* 6.10's canvas, in a claim of its own and beside a MODULE of its own,
     * built by overlay tenant 8 - and it answers through w_cldstate for
     * tenant 6's reason exactly: a refused overlay returns 0 and 0 is also a
     * perfectly good "this bundle has no canvas". */
    w_cldstate = 2;
    ovl_canvasload();
    if (w_cldstate == 1) {
        w_short(w_claimkb + w_vmkb + w_gridkb + w_canvaskb
                + (unsigned)wcv_kb(), os88_mem_largest_kb());
        return 1;
    }
    if (w_cldstate == 2) {
        w_fail("WEAVE.OVL is missing or stale; no bundle can open.");
        return 1;
    }
    if (w_cldstate == 3) {
        w_fail("WEAVE.WSM is not on this disk.");
        return 1;
    }
    if (w_cldstate == 4) {
        w_fail("WEAVE.WSM does not match this program.");
        return 1;
    }
    w_lay_cw = -1;                      /* force the walk on the next paint */
    if (w_grid(win))
        w_flow();
    w_menusync();
    ovl_menubuild(win);                   /* 6.11: the bundle's own menus */
    ovl_vmstart(win);                     /* ...and the VM, last: it is the one
                                         * thing that can run a line of the
                                         * app's own code, and 2.6.2's
                                         * module-init function must not run
                                         * against a half-built card */
    return 1;
}

/* ============================================================================
 * THE COMPONENTS' BIRTH STATE, AND THE VM (WEAVE-SPEC 2.5, 4.7)
 * ==========================================================================*/

/* ovl_comp_init - walk the WHOLE display list, not just the entry card.
 *
 * The flow walk lays out ONE card (7.2); this fills in every component the
 * bundle declares, on any card, because `app.go(2)` makes the second card's
 * components live without reloading anything - and because `hidden` and
 * `checked` are birth state that a card switch must not reset. */
static void ovl_comp_init(void)
{
    unsigned s, n, i, rec, id, ct, props;

    s = w_soff[W_UISTREAM];
    n = w_sextra[W_UISTREAM];
    for (i = 0; i < n; i++) {
        rec = s + i * W_REC_SIZE;
        if (w_b(w_seg, rec) != W_REC_COMP)
            continue;
        id = w_b(w_seg, rec + W_R_ID);
        ct = w_b(w_seg, rec + W_R_CTYPE);
        props = w_w(w_seg, rec + W_R_PROPS);
        w_cflag[id] = (unsigned char)(w_b(w_seg, rec + W_R_CFLAGS)
                                      & (CF_HIDDEN | CF_DISABLED));
        if (props == W_NOPROPS)
            continue;
        if (ct == WC_METER) {
            w_cval[id] = (int)w_pint(props, WA_VALUE, 0);
            w_cvold[id] = w_cval[id];   /* 6.4's delta starts from what the
                                         * first card paint will draw */
        }
        else if (ct == WC_CHECK || ct == WC_RADIO)
            w_cval[id] = w_pint(props, WA_CHECKED, 0) ? 1 : 0;
        else if (ct == WC_INPUT) {
            w_ialloc((int)id, (int)w_pint(props, WA_COLS, 20));
            ovl_iload((int)id, props);
        }
    }
}

/* ovl_iload - an <input>'s initial contents, out of its `text` attribute. */
static void ovl_iload(int id, unsigned props)
{
    int k;

    k = w_iblk(id);
    if (k < 0)
        return;
    if (w_pstr(props, WA_TEXT) == 0)
        return;
    wd_lset(w_fld[k], w_str);
}

/* ovl_vmstart - the VM claim, the bind and 2.6.2's module-init function.
 *
 * THE CLAIM'S SIZE IS THE BUNDLE'S OWN ASK (2.2's vm KB byte), which 10.1
 * already counted before any I/O - so a failure here is a heap that moved
 * under us between the refusal and now, and it is reported as the same
 * sentence rather than as a new kind of trouble. */
static void ovl_vmstart(void *win)
{
    unsigned start;

    w_vmseg = os88_mem_claim((int)w_vmkb);
    if (w_vmseg == 0) {
        w_short(w_claimkb + w_vmkb, os88_mem_largest_kb());
        return;
    }
    wvm_bind(w_vmseg, w_vmkb << 10, w_seg,
             w_soff[W_CODE], w_slen[W_CODE], w_nfunc,
             w_soff[W_ATOMS], w_natoms, w_nblk, (unsigned)os88_rand());

    /* 10.2's second capability test, which wave 2 could not make because
     * OSAPI_WM_TIMER had no C thunk. It has one now, so the fact is TESTED
     * rather than guessed from the machine's size (SPEC.md 47). */
    w_tmrok = os88_wm_timer(win, 0) == 0;
    if ((w_flags & WABF_TIMER) && !w_tmrok)
        w_saysav("This app uses timers (WM_TIMER); "
                 "this kernel does not carry them.");

    start = w_pfind(w_sextra[W_PROPS], WA_START);
    if (w_pfound) {
        wvm_begin((int)start);          /* 2.6.2: the globals are zeroed, then
                                         * the initializers are applied - by
                                         * exactly this carriage */
        w_kick();
    }
}

/* --- the association route (1.5) ---------------------------------------- */

/* w_find_open - find `w_name` in the directory we are standing in, take its
 * size off the entry, and open it.
 *
 * FIND is by ORDINAL with no cursor anywhere (SPEC.md 19.7.1): ask for 0, then
 * for what it returns. Every step between two of them walks directories
 * itself, which is exactly why there is no find-first state to destroy - and
 * why it is directory SECTOR reads rather than a free walk of a RAM snapshot.
 *
 * The name compare is case-insensitive because the two sources disagree:
 * tools/os88disk.py uppercases every name it writes, and a host OS that
 * rewrote the disk may not have. */
static struct os88_find w_find;

static int w_samename(const char *a, const char *b)
{
    int i, ca, cb;

    for (i = 0; i < 13; i++) {
        ca = a[i];
        cb = b[i];
        if (ca >= 'a' && ca <= 'z')
            ca -= 32;
        if (cb >= 'a' && cb <= 'z')
            cb -= 32;
        if (ca != cb)
            return 0;
        if (ca == 0)
            return 1;
    }
    return 1;
}

static int ovl_find_open(void *win)
{
    int ord;

    ord = 0;
    while (ord >= 0) {
        ord = os88_file_find(ord, &w_find);
        if (ord < 0)
            break;
        if (!w_samename(w_find.name, w_name))
            continue;
        if (w_find.type >= OS88_FT_DIR)
            continue;                   /* a folder with the bundle's name is
                                         * not it, and neither is the `..` row
                                         * (OS88_FT_UP) */
        ovl_open(win, w_name, w_find.size_lo, w_find.size_hi);
        return 1;   /* the overlay is HERE, so it ran */
    }
    w_missing();
    return 1;
}

/* ============================================================================
 * THE RESIDENT SEAM (WEAVE-SPEC 1.2.1, tenant 4)
 *
 * Everything above between the birth-state walk and here is `ovl_*` and ships
 * in WEAVE.OVL: the size probe, the capability tests, the directory search,
 * the claim and the read, the component birth state, the field pool, the menu
 * build and the VM bind. It runs once per open and once per `^R`, which is a
 * COMMAND keystroke and not an editing one, and it is not on any path a
 * running handler takes.
 *
 * THE TWO WRAPPERS EXIST FOR ONE REASON and it is the validator's, said again
 * one level up: A REFUSED OVERLAY LOAD RETURNS 0 (apps/cc/crt0.asm), and the
 * load path's natural answer is `void` - so a missing WEAVE.OVL would have
 * been a double-click that opened a window, drew the Deck, and said nothing
 * at all. `ovl_open` and `ovl_find_open` therefore answer 1 at every exit
 * INCLUDING their own refusals (which have already put their own sentence on
 * the glass), so a 0 can only mean the module is not there.
 * ==========================================================================*/

static void w_ovlgone(void)
{
    w_fail("WEAVE.OVL is missing; a bundle cannot be opened without it.");
}

static void w_open(void *win, const char *name, unsigned size_lo,
                   unsigned size_hi)
{
    if (!ovl_open(win, name, size_lo, size_hi))
        w_ovlgone();
}

static void w_find_open(void *win)
{
    if (!ovl_find_open(win))
        w_ovlgone();
}

/* w_openpend - spend the banked launch document.  Called from the FIRST
 * W_PAINT and exactly once.
 *
 * THE GOTO GOES FIRST. It is a REMOUNT - real floppy I/O - and CF=1 is the
 * folder no longer being listable, a disk swapped between the double-click and
 * this first paint. Searching the wrong directory would report the wrong
 * reason, so a refused GOTO takes the not-found exit rather than falling
 * through to a search where we stand.
 *
 * Only when a locator actually came with the name: 0,0 is a real locator, so
 * w_arghave is what says whether there was one. */
static void w_openpend(void *win)
{
    if (w_arghave) {
        if (os88_file_goto(&w_place) != 0) {
            w_missing();
            return;
        }
    }
    w_find_open(win);
}

/* w_reload - WEAVE-SPEC 1.7's edit-run loop: re-read the current bundle from
 * disk into a FRESH claim and re-run the walk. Two keystrokes and a click per
 * iteration, and zero kernel bytes. The app's .SAV file is untouched.
 *
 * It goes back through w_find_open() rather than remembering a size, because
 * the whole point of a reload is that the file on the disk CHANGED - Loom just
 * packed it - and the size it had last time is exactly the wrong number. */
static void w_reload(void *win)
{
    if (w_name[0] == 0)
        return;
    if (w_arghave && os88_file_goto(&w_place) != 0) {
        w_missing();
        return;
    }
    w_find_open(win);
}
