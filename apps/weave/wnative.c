/* ============================================================================
 * os8088 - apps/weave/wnative.c
 *
 * THE COMPONENT SURFACE AND THE IMPURE BUILTINS (WEAVE-SPEC 6, 8.1).
 * #included by apps/weave/weave.c after wevent.c.
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS FILE IS
 * ---------------------------------------------------------------------------
 * apps/weave/wvm.inc's one obligation on its includer: `wvm_native`. The
 * bytecode core handles every PURE op itself - arithmetic, the stacks, the
 * arenas, str/len/substr/find/rand/array - and leaves this file exactly the
 * things that need a component, a screen, a speaker or a file, which is
 * WEAVE-SPEC 8.1's own split. That split is not a layering preference: it is
 * what lets the whole core be diffed against the model in a raw boot sector
 * with nothing under it (12.3), and every op that arrives here is one the
 * corpus structurally cannot cover.
 *
 * ---------------------------------------------------------------------------
 * THE THREE ANSWERS
 * ---------------------------------------------------------------------------
 *   0  done, and the answer cell is in WN_REST/WN_RESV
 *   1  a script error - WN_ERRC/WN_ERRA/WN_ERRB, and 10.6.1 is the wording
 *   2  an allocation did not fit: the core collects and RE-EXECUTES the op
 *
 * The third is why nothing here may have a side effect BEFORE it allocates.
 * A `SETP` that marked its component dirty and then asked for a string would
 * mark it twice; a `list.set` that wrote its override and then failed would
 * write it twice. So every allocating path allocates FIRST and commits after,
 * which is the same discipline WEAVE-SPEC 4.8 states about the eval stack one
 * level down.
 *
 * ---------------------------------------------------------------------------
 * NOTHING HERE DRAWS
 * ---------------------------------------------------------------------------
 * A `SETP` marks its component dirty and returns; wevent.c's w_flush repaints
 * the set under one lock hold at the end of the slice. ONWAKE does not hold
 * the gfx lock, and a component painted from in here would be drawing without
 * one. The two exceptions take the lock themselves and say so: alert(), which
 * raises a window, and tone(), which does not draw at all.
 * ==========================================================================*/

#define NB(f)   w_nblk[f]

static int w_nblk[WN_SIZE / 2];

static int w_argt(int k) { return NB(WNW_ARG0 + 2 * k); }
static int w_argv(int k) { return NB(WNW_ARG0 + 2 * k + 1); }

static int w_nres(int tag, int val)
{
    NB(WNW_REST) = tag;
    NB(WNW_RESV) = val;
    return 0;
}

static int w_nerr(int code, int a, int b)
{
    NB(WNW_ERRC) = code;
    NB(WNW_ERRA) = a;
    NB(WNW_ERRB) = b;
    return 1;
}

/* w_nstr - a value cell for a C string, allocating in the VM arena.
 * Answers 0 done, 2 collect-and-retry - which is the whole reason the caller
 * must not have changed anything yet. */
static int w_nstr(const char *s)
{
    int h;

    h = wvm_str_make(s);
    if (h == 0)
        return 2;
    if (h < 0)
        return w_nerr(WE_STRSP, 0, 0);
    return w_nres(WT_STR, h);
}

/* w_natom - the value cell for an atom's string, WITHOUT copying it: PUSHA's
 * own static handle (4.8), which is never collected and never compacted
 * because the bundle claim is pinned. A label that script has never written
 * therefore reads back for free. */
static int w_natom(unsigned atom)
{
    int h;

    if (atom == 0)
        return w_nstr("");
    h = wvm_atom_handle((int)atom);
    if (h == 0)
        return w_nstr("");
    return w_nres(WT_STR, h);
}

/* ============================================================================
 * GETP (WEAVE-SPEC 6's per-component surface)
 * ==========================================================================*/

static int w_getp(int id, int atom)
{
    int i, ct, k;
    unsigned props;

    if (id == 0)                        /* the app carries no readable
                                         * property in v1 (6's SURFACE) */
        return w_nerr(WE_NOPROP, atom, 0);
    i = w_find_lay(id);
    if (i < 0)
        return w_nerr(WE_NOCOMP, id, 0);
    ct = w_lay[i].ctype;
    props = w_lay[i].props;

    if (atom == WA_HIDDEN)              /* get/set on every flow component */
        return w_nres(WT_INT, (w_cflag[id] & CF_HIDDEN) ? 1 : 0);
    if (atom == WA_ENABLED) {
        if (ct != WC_BUTTON && ct != WC_CHECK && ct != WC_RADIO
                && ct != WC_INPUT)
            return w_nerr(WE_NOPROP, atom, 0);
        return w_nres(WT_INT, (w_cflag[id] & CF_DISABLED) ? 0 : 1);
    }

    switch (ct) {
    case WC_LABEL:
    case WC_TEXT:
        if (atom != WA_TEXT)
            break;
        if (w_ctext[id])
            return w_nres(WT_STR, (int)w_ctext[id]);
        return w_natom(w_patom(props, WA_TEXT));

    case WC_INPUT:
        if (atom == WA_COLS)
            return w_nres(WT_INT, (int)w_pint(props, WA_COLS, 20));
        if (atom != WA_TEXT)
            break;
        k = w_iblk(id);
        if (k >= 0)                     /* the LIVE field, which is what
                                         * `who.text` has to mean (6.7) */
            return w_nstr((const char *)w_fld[k][LNW_BUF]);
        if (w_ctext[id])
            return w_nres(WT_STR, (int)w_ctext[id]);
        return w_natom(w_patom(props, WA_TEXT));

    case WC_BUTTON:
        if (atom != WA_LABEL)
            break;
        if (w_ctext[id])
            return w_nres(WT_STR, (int)w_ctext[id]);
        return w_natom(w_patom(props, WA_LABEL));

    case WC_CHECK:
    case WC_RADIO:
        if (atom == WA_CHECKED)
            return w_nres(WT_INT, w_cval[id] ? 1 : 0);
        if (atom != WA_LABEL)
            break;
        if (w_ctext[id])
            return w_nres(WT_STR, (int)w_ctext[id]);
        return w_natom(w_patom(props, WA_LABEL));

    case WC_METER:
        if (atom == WA_VALUE)
            return w_nres(WT_INT, w_cval[id]);
        if (atom == WA_MAX)
            return w_nres(WT_INT, (int)w_pint(props, WA_MAX, 100));
        break;

    case WC_LIST:
        if (atom == WA_SEL)
            return w_nres(WT_INT, (int)w_lsel1[id] - 1);
        if (atom == WA_ROWS)
            return w_nres(WT_INT, (int)w_pint(props, WA_ROWS, 8));
        break;

    case WC_GRID:
        if (atom == WA_ROWS)
            return w_nres(WT_INT, (int)w_pint(props, WA_ROWS, 1));
        if (atom == WA_COLS)
            return w_nres(WT_INT, (int)w_pint(props, WA_COLS, 1));
        if (atom == WA_SELROW)
            return w_nres(WT_INT, w_gsel_r);
        if (atom == WA_SELCOL)
            return w_nres(WT_INT, w_gsel_c);
        break;

    default:
        break;
    }
    return w_nerr(WE_NOPROP, atom, 0);
}

/* ============================================================================
 * SETP
 * ==========================================================================*/

/* w_settext - the one path for `.text` and `.label`.  It is also the one
 * place a component-string ROOT is written (4.8.1). */
static int w_settext(int id, int i)
{
    int k;

    if (w_argt(0) != WT_STR)
        return w_nerr(WE_TYPE, 0, 0);
    k = w_iblk(id);
    if (k >= 0) {                       /* an <input>: the field's own buffer
                                         * IS the state (6.7), so the string
                                         * goes into it and w_ctext is left
                                         * alone - parking a handle there too
                                         * would be a GC root holding a copy
                                         * nothing ever reads */
        wvm_str_read(w_argv(0), w_str, W_STRMAX);
        wd_lset(w_fld[k], w_str);
        if (w_focus == k)
            w_caron = 0;
    } else
        w_ctext[id] = (unsigned)w_argv(0);
    w_touch(id);
    (void)i;
    return w_nres(WT_NULL, 0);
}

static int w_setp(int id, int atom)
{
    int i, ct, v, n, k, other;
    unsigned props, g;

    if (id == 0)
        return w_nerr(WE_NOPROP, atom, 0);
    i = w_find_lay(id);
    if (i < 0)
        return w_nerr(WE_NOCOMP, id, 0);
    ct = w_lay[i].ctype;
    props = w_lay[i].props;

    if (atom == WA_TEXT && (ct == WC_LABEL || ct == WC_TEXT
                            || ct == WC_INPUT))
        return w_settext(id, i);
    if (atom == WA_LABEL && (ct == WC_BUTTON || ct == WC_CHECK
                             || ct == WC_RADIO))
        return w_settext(id, i);

    if (w_argt(0) != WT_INT && w_argt(0) != WT_BOOL)
        return w_nerr(WE_TYPE, 0, 0);
    v = w_argv(0);

    if (atom == WA_HIDDEN) {
        if (v)
            w_cflag[id] |= CF_HIDDEN;
        else
            w_cflag[id] &= ~CF_HIDDEN;
        w_touch(id);
        return w_nres(WT_NULL, 0);
    }
    if (atom == WA_ENABLED) {
        if (ct != WC_BUTTON && ct != WC_CHECK && ct != WC_RADIO
                && ct != WC_INPUT)
            return w_nerr(WE_NOPROP, atom, 0);
        if (v)
            w_cflag[id] &= ~CF_DISABLED;
        else
            w_cflag[id] |= CF_DISABLED;
        w_touch(id);
        return w_nres(WT_NULL, 0);
    }

    switch (ct) {
    case WC_METER:
        if (atom != WA_VALUE)
            break;
        n = (int)w_pint(props, WA_MAX, 100);
        if (v < 0)
            v = 0;                      /* 6.4: CLAMPED, never refused */
        if (v > n)
            v = n;
        if (v != w_cval[id]) {
            w_cval[id] = v;
            w_touch(id);
        }
        return w_nres(WT_NULL, 0);

    case WC_CHECK:
    case WC_RADIO:
        if (atom != WA_CHECKED)
            break;
        v = v ? 1 : 0;
        if (ct == WC_RADIO && v) {
            other = w_radio_holder(i);  /* 6.6: checking one unchecks and
                                         * repaints the previous holder */
            if (other >= 0) {
                w_cval[w_lay[other].id] = 0;
                w_touch(w_lay[other].id);
            }
        }
        if (v != w_cval[id]) {
            w_cval[id] = v;
            w_touch(id);
        }
        return w_nres(WT_NULL, 0);

    case WC_LIST:
        if (atom != WA_SEL)
            break;
        w_items(props);
        n = (int)w_it_n;
        if (v < -1 || v >= n)
            return w_nerr(WE_LIDX, v, n);
        w_lsel1[id] = (unsigned)(v + 1);
        w_touch(id);                    /* programmatic: no onselect fires,
                                         * which is the model's own rule */
        return w_nres(WT_NULL, 0);

    default:
        break;
    }
    (void)k;
    (void)g;
    return w_nerr(WE_NOPROP, atom, 0);
}

/* ============================================================================
 * CALLM (WEAVE-SPEC 6.8's list, 6.9's grid, 6.12's cards)
 * ==========================================================================*/

static int w_callm(int id, int atom, int argc)
{
    int i, ct, idx, n, k, r, c;
    unsigned props, a;

    if (id == 0) {                      /* the app: 6.12's card switch */
        if (atom != WA_GO)
            return w_nerr(WE_NOMETH, atom, 0);
        if (argc < 1 || w_argt(0) != WT_INT)
            return w_nerr(WE_TYPE, 0, 0);
        n = w_argv(0);
        if (n < 1 || n > (int)w_ncard)
            return w_nerr(WE_CARD, n, (int)w_ncard);
        if (n != (int)w_entry) {
            w_entry = (unsigned)n;
            w_lay_cw = -1;              /* the walk is void: a new card is a
                                         * different set of components */
            w_cardpend = 1;             /* ...and ONE full repaint (6.12) */
        }
        return w_nres(WT_NULL, 0);
    }

    i = w_find_lay(id);
    if (i < 0)
        return w_nerr(WE_NOCOMP, id, 0);
    ct = w_lay[i].ctype;
    props = w_lay[i].props;

    if (ct == WC_LIST) {
        w_items(props);
        n = (int)w_it_n;
        if (atom == WA_GET) {
            if (argc < 1 || w_argt(0) != WT_INT)
                return w_nerr(WE_TYPE, 0, 0);
            idx = w_argv(0);
            if (idx < 0 || idx >= n)
                return w_nerr(WE_LIDX, idx, n);
            k = w_lfind(id, idx);
            if (k >= 0)
                return w_nres(WT_STR, (int)w_lseth[k]);
            a = w_item((unsigned)idx);
            return w_natom(a);
        }
        if (atom == WA_SET) {
            if (argc < 2 || w_argt(0) != WT_INT || w_argt(1) != WT_STR)
                return w_nerr(WE_TYPE, 0, 0);
            idx = w_argv(0);
            if (idx < 0 || idx >= n)
                return w_nerr(WE_LIDX, idx, n);
            k = w_lfind(id, idx);
            if (k < 0) {
                if (w_nlset >= W_NLSET)
                    return w_nerr(WE_STRSP, 0, 0);  /* 4.8.1's named bound */
                k = w_nlset++;
                w_lsetc[k] = (unsigned char)id;
                w_lseti[k] = (unsigned char)idx;
            }
            w_lseth[k] = (unsigned)w_argv(1);
            w_touch(id);
            return w_nres(WT_NULL, 0);
        }
        return w_nerr(WE_NOMETH, atom, 0);
    }

    if (ct == WC_GRID) {
        /* WAVE 4's SEAM, and it is deliberately not a refusal.  The cell
         * store, the band composer and the FX VM are WEAVE-SPEC 13.1's next
         * wave; what a grid HAS in this one is its rows and its cols, read
         * straight out of the record, so every range check 6.9 asks for is
         * real. An empty cell reads as int 0 in the model too, so
         * `g.cell(r,c)` answers 0 rather than an error - which is what lets
         * SHEET's handlers run today and lets wave 4 change the answer
         * without changing the surface. */
        r = (int)w_pint(props, WA_ROWS, 1);
        c = (int)w_pint(props, WA_COLS, 1);
        if (atom == WA_CELL || atom == WA_SETCELL || atom == WA_SELECT) {
            if (argc < 2 || w_argt(0) != WT_INT || w_argt(1) != WT_INT)
                return w_nerr(WE_TYPE, 0, 0);
            if (w_argv(0) < 1 || w_argv(0) > r
                    || w_argv(1) < 1 || w_argv(1) > c)
                return w_nerr(WE_GCELL, w_argv(0), w_argv(1));
        }
        if (atom == WA_CELL)
            return w_nres(WT_INT, 0);
        if (atom == WA_SELECT) {
            w_gsel_r = w_argv(0);
            w_gsel_c = w_argv(1);
            return w_nres(WT_NULL, 0);
        }
        if (atom == WA_SETCELL || atom == WA_RECALC || atom == WA_CLEAR)
            return w_nres(WT_NULL, 0);
        return w_nerr(WE_NOMETH, atom, 0);
    }
    return w_nerr(WE_NOMETH, atom, 0);
}

/* ============================================================================
 * THE SIX IMPURE BUILTINS (WEAVE-SPEC 8.2, 8.3, 8.4)
 * ==========================================================================*/

static int w_built(int bi, int argc)
{
    int t;

    switch (bi) {
    case WB_ALERT:
        if (argc < 1 || w_argt(0) != WT_STR)
            return w_nerr(WE_TYPE, 0, 0);
        wvm_str_read(w_argv(0), w_amsg, WD_AMAX + 1);
        w_alertfn = argc > 1 ? w_argv(1) : -1;
        w_alertkind = 0;
        os88_gfx_lock();                /* os88ui_ask wants the lock HELD, and
                                         * ONWAKE does not hold one */
        if (wd_ask(w_amsg, w_win, WD_AOK) == 0)
            w_alerting = 1;
        else
            w_alertfn = -1;             /* refused (one is already up, and it
                                         * has been RAISED): 8.2 says alert()
                                         * returns either way, so the app
                                         * carries on and no event is owed */
        os88_gfx_unlock();
        return w_nres(WT_NULL, 0);

    case WB_TIMER:
        if (argc < 2 || w_argt(0) != WT_INT)
            return w_nerr(WE_TYPE, 0, 0);
        t = w_argv(0);
        if (t < 1)
            t = 1;                      /* 8.2: one-shot at the 18.2 Hz tick,
                                         * which is the 55 ms floor */
        if (!w_tmrok)
            return w_nres(WT_NULL, 0);  /* 10.2: told at load, inert here */
        w_tscript = os88_ticks() + (unsigned)t;
        w_tscript_fn = w_argv(1);
        w_tscript_on = 1;
        w_arm();
        return w_nres(WT_NULL, 0);

    case WB_SAVE:
        return w_nres(WT_BOOL, ovl_savestate() ? 1 : 0);

    case WB_LOAD:
        return w_nres(WT_BOOL, ovl_loadstate() ? 1 : 0);

    case WB_PLAY:
        /* 8.4: v1 ships no clip carriage, so this refuses politely and says
         * so once. The name is specified now so it is not re-invented, and
         * 9.11 is why it would freeze the desktop if it did play. */
        os88_toast("playSound: this bundle carries no clips.", 0);
        return w_nres(WT_NULL, 0);

    case WB_TONE:
        if (argc < 2 || w_argt(0) != WT_INT || w_argt(1) != WT_INT)
            return w_nerr(WE_TYPE, 0, 0);
        os88_snd_tone(w_argv(0), w_argv(1), 0);
        return w_nres(WT_NULL, 0);

    default:
        break;
    }
    return w_nerr(WE_BUILT, bi, 0);
}

/* ============================================================================
 * THE ENTRY POINT wvm.inc CALLS (apps/weave/wui.inc's trampoline)
 * ==========================================================================*/

int w_native(void)
{
    switch (NB(WNW_KIND)) {
    case WNK_GETP:
        return w_getp(NB(WNW_COMP), NB(WNW_ATOM));
    case WNK_SETP:
        return w_setp(NB(WNW_COMP), NB(WNW_ATOM));
    case WNK_CALLM:
        return w_callm(NB(WNW_COMP), NB(WNW_ATOM), NB(WNW_ARGC));
    case WNK_BUILT:
        return w_built(NB(WNW_ATOM), NB(WNW_ARGC));
    default:
        break;
    }
    return w_nerr(WE_OPCODE, 0, 0);
}

/* ============================================================================
 * MENUS (WEAVE-SPEC 2.6.2, 6.11)
 * ==========================================================================*/

/* w_menu_fn - the oncommand function of menu `mi` item `ii`, both 1-based,
 * out of the app block's MENUS blob.  -1 = none (0xFF in the blob means the
 * item is present and inert, which is a thing a bundle may say). */
static int w_menu_fn(int mi, int ii)
{
    unsigned off, pos;
    int m, nit;

    off = w_pfind(w_sextra[W_PROPS], WA_MENUS);
    if (!w_pfound)
        return -1;
    off = w_soff[W_PROPS] + off;
    pos = 1;
    for (m = 0; m < (int)w_b(w_seg, off); m++) {
        nit = (int)w_b(w_seg, off + pos + 1);
        if (m + 1 == mi && ii >= 1 && ii <= nit)
            return (int)w_b(w_seg, off + pos + 2 + 2 * (ii - 1) + 1);
        pos += 2 + 2 * nit;
    }
    return -1;
}

/* ovl_menubuild - the bundle's own menus, out of the app block's MENUS blob
 * (2.6.2), into strings the kernel can letter from.
 *
 * THE BLOB IS IN THE CLAIM AND THE KERNEL READS A NEAR POINTER, so every
 * title and label is COPIED into our segment - the same reason wui.inc's
 * alert message is a static rather than a staged one. It runs once per
 * bundle, over at most 4 x 8 short strings. */
static void ovl_menubuild(void *win)
{
    unsigned off, pos, a, n;
    int m, k, nit, nm;

    w_menus.nmenus = 1;
    if (w_state != W_ST_RUN) {
        os88_menu_set(win, &w_menus);
        return;
    }
    off = w_pfind(w_sextra[W_PROPS], WA_MENUS);
    if (w_pfound) {
        off = w_soff[W_PROPS] + off;
        nm = (int)w_b(w_seg, off);
        pos = 1;
        for (m = 0; m < nm; m++) {
            nit = (int)w_b(w_seg, off + pos + 1);
            if (m < W_APPMENUS) {
                a = w_b(w_seg, off + pos);
                n = w_atom_len(a);
                if (n > W_TITLEMAX - 1)
                    n = W_TITLEMAX - 1;
                w_copy(w_seg, w_atom_off(a), w_mtitle[m], n);
                w_mtitle[m][n] = 0;
                for (k = 0; k < nit && k < W_APPITEMS; k++) {
                    a = w_b(w_seg, off + pos + 2 + 2 * k);
                    n = w_atom_len(a);
                    if (n > W_LABELMAX - 1)
                        n = W_LABELMAX - 1;
                    w_copy(w_seg, w_atom_off(a), w_mlabel[m][k], n);
                    w_mlabel[m][k][n] = 0;
                    w_mitems[m][k] = w_mlabel[m][k];
                }
                w_menus.menu[m + 1].title = w_mtitle[m];
                w_menus.menu[m + 1].items = w_mitems[m];
                w_menus.menu[m + 1].nitems =
                    nit > W_APPITEMS ? W_APPITEMS : nit;
                w_menus.nmenus = m + 2;
            }
            pos += 2 + 2 * nit;
        }
        if (nm > W_APPMENUS)
            os88_toast("This app declares more menus than the bar holds.", 0);
    }
    os88_menu_set(win, &w_menus);
}
