/* ============================================================================
 * os8088 - apps/weave/wact.c
 *
 * INTERACTION (WEAVE-SPEC 6.5-6.8, 1.2). #included by apps/weave/weave.c
 * after wpaint.c, whose rect table and component state it moves.
 *
 * ---------------------------------------------------------------------------
 * THE RULE THIS WHOLE FILE OBEYS
 * ---------------------------------------------------------------------------
 * WEAVE-SPEC 1.2: kernel callbacks are handled ENTIRELY NATIVELY - hit test,
 * widget arm/fire, caret, selection XOR, scroll - and only ENQUEUE an 8-byte
 * record into the VM's ring (4.9), post OSAPI_WM_WAKE, and return. Not one
 * line below runs a bytecode op, and that is not a layering preference: a
 * callback holds the gfx lock, a handler is bounded by nothing but 4.11's
 * 90 ticks, and a script that looped inside a click would freeze the desktop
 * rather than its own window.
 *
 * So every gesture here is two halves that do not touch: the PICTURE changes
 * now, natively and cheaply (a button press is ~2 gfx calls, a check toggle
 * is one glyph, a selection move is 2 XOR rects - WEAVE-SPEC 14), and the
 * SCRIPT hears about it a slice later, or never, if nothing is bound.
 *
 * ---------------------------------------------------------------------------
 * THE FIELD POOL, AND ITS BOUND
 * ---------------------------------------------------------------------------
 * apps/os88line.inc declares no storage: every routine takes a block the
 * caller owns, which is what lets one window have two fields. WEAVE has as
 * many as the bundle declares, so the blocks and their text come out of a
 * fixed pool - W_MAXIN blocks over W_IPOOL bytes of text, assigned in
 * UISTREAM order at load. WEAVE-SPEC 6.7 states the bound and its sentence;
 * a bundle past it paints every field and edits the first eight.
 * ==========================================================================*/

/* os88line.inc's block, in WORDS - a C file may not name an nasm equ, so
 * these are that file's LN_* halved, and wui.inc carries a %if that fails the
 * build if OS88LINE_SZ ever moves. */
#define LNW_X1     0
#define LNW_Y1     1
#define LNW_X2     2
#define LNW_Y2     3
#define LNW_BUF    4
#define LNW_MAX    5
#define LNW_LEN    6
#define LNW_CAR    7
#define LNW_VIEW   8
#define LNW_FOCUS  9            /* a BYTE at +18; the word above it is padding
                                 * in os88line.inc's own layout, so writing the
                                 * whole word is safe and is what C can do */

static int  w_fld[W_MAXIN][10];          /* the blocks */
static char w_ipool[W_IPOOL];           /* ...and the text they point into */
static unsigned char w_iof[W_MAXIN];    /* which comp_id owns block k */
static int  w_nin;                      /* blocks in use */
static int  w_ipos;                     /* bytes of the pool spent */
static int  w_focus = -1;               /* the armed block, -1 = none */
static int  w_caron;                    /* is the caret drawn right now? */
static int  w_tmrok = 1;                /* did WM_TIMER arm? (8.2, 10.2) */

/* w_iblk - the block index for comp_id, or -1. */
static int w_iblk(int id)
{
    int k;

    for (k = 0; k < w_nin; k++)
        if (w_iof[k] == id)
            return k;
    return -1;
}

/* w_ialloc - give this input a block and its share of the pool.  Called once
 * per <input> at load, in UISTREAM order, and never again: the text a field
 * holds has to survive a resize, and the walk does not. */
static void w_ialloc(int id, int cols)
{
    int need;

    if (w_nin >= W_MAXIN)
        return;                         /* 6.7's bound: painted, not editable */
    need = cols + 2;
    if (w_ipos + need > W_IPOOL)
        return;
    w_iof[w_nin] = (unsigned char)id;
    w_fld[w_nin][LNW_BUF] = (int)(w_ipool + w_ipos);
    w_fld[w_nin][LNW_MAX] = need;
    w_fld[w_nin][LNW_LEN] = 0;
    w_fld[w_nin][LNW_CAR] = 0;
    w_fld[w_nin][LNW_VIEW] = 0;
    w_fld[w_nin][LNW_FOCUS] = 0;
    w_ipool[w_ipos] = 0;
    w_ipos += need;
    w_nin++;
}

static void w_istate(void)              /* a new bundle: forget the old one's */
{
    w_nin = 0;
    w_ipos = 0;
    w_focus = -1;
    w_caron = 0;
    os88_memset(w_fld, 0, sizeof(w_fld));
    os88_memset(w_ipool, 0, sizeof(w_ipool));
}

/* ============================================================================
 * THE FIELD ON THE GLASS (WEAVE-SPEC 6.7)
 * ==========================================================================*/

/* w_infield - paint one <input>, called from w_paint_comp.
 *
 * TWO PAINTERS, AND THE SPLIT IS SPEC.md 47's. os88line_draw forces CBLACK
 * for its frame and its text, so a GREYED field would come out solid-framed
 * with dithered letters - two halves of one control disagreeing, which is
 * rule 2's own failure. That is os88line.inc's bug and not WEAVE's (it has
 * no other caller that can grey a field), so the disabled case keeps
 * wdraw.inc's wd_input, which honours the pen the caller set, and the live
 * case gets the real editor. Recorded in WEAVE-PLAN 4.4.2 rather than fixed
 * in the shared file, for the reason 4.4.1 gives about the scroll bar. */
static void w_infield(int i, int x1, int y1, int x2, int y2, int dis)
{
    int k, id;

    id = w_lay[i].id;
    k = w_iblk(id);
    if (k < 0 || dis) {
        w_cstr(id, w_lay[i].props, WA_TEXT);
        wd_input(x1, y1, x2, y2, w_str);
        return;
    }
    w_fld[k][LNW_X1] = x1;
    w_fld[k][LNW_Y1] = y1;
    w_fld[k][LNW_X2] = x2;
    w_fld[k][LNW_Y2] = y2;
    wd_ldraw(w_fld[k]);
    w_caron = w_fld[k][LNW_FOCUS] ? 1 : 0;
}

/* w_caret - the blink, re-armed from the timer (6.7).
 *
 * THE STATIC FALLBACK IS A FACT AND NOT A GUESS (SPEC.md 47): on kern_small
 * OSAPI_WM_TIMER answers CF=1, os88_wm_timer() hands that back as -1, and the
 * caret is simply left ON. The app was told at load, once, because its bundle
 * declared WABF_TIMER (10.2) - and this is the other half of that sentence. */
static void w_caret(void *win)
{
    if (w_focus < 0 || w_state != W_ST_RUN)
        return;
    if (w_fld[w_focus][LNW_FOCUS] == 0)
        return;
    if (w_caron)
        wd_lcaroff(w_fld[w_focus]);
    else
        wd_lcaron(w_fld[w_focus]);
    w_caron = !w_caron;
    (void)win;
}

/* w_setfocus - arm one field and disarm the last, repainting only those two.
 * -1 disarms everything, which is what a click on anything else means. */
static void w_setfocus(int k)
{
    int old;

    old = w_focus;
    if (old == k)
        return;
    if (old >= 0) {
        if (w_caron) {
            wd_lcaroff(w_fld[old]);
            w_caron = 0;
        }
        w_fld[old][LNW_FOCUS] = 0;
        wd_ldraw(w_fld[old]);
    }
    w_focus = k;
    if (k >= 0) {
        w_fld[k][LNW_FOCUS] = 1;
        wd_ldraw(w_fld[k]);
        w_caron = 1;
    }
}

/* w_tab - move to the next editable field in UISTREAM order (6.7). */
static void w_tab(void)
{
    int k;

    if (w_nin == 0)
        return;
    k = w_focus < 0 ? 0 : w_focus + 1;
    if (k >= w_nin)
        k = 0;
    w_setfocus(k);
}

/* ============================================================================
 * THE PRESS AND THE RELEASE (SPEC.md 13.7, 13.8)
 * ==========================================================================*/

/* w_press - a press landed on component `i`.  Draw the down state, arm.
 *
 * A BUTTON IS DRAWN DOWN FROM A VARIABLE, never XOR-ed: SPEC.md 13.8 is
 * explicit, and calc's cal_restore rule is the same thing said about a rect.
 * w_bdown is that variable and the painter reads it, so a repaint arriving
 * mid-gesture - a window uncovered under the finger - draws the control in
 * the state it is actually in. */
static void w_press(int i, int x, int y)
{
    int id, k;

    id = w_lay[i].id;
    switch (w_lay[i].ctype) {
    case WC_BUTTON:
        w_bdown = id;
        w_repaint_one(i);
        wd_arm(i + 1);
        break;

    case WC_CHECK:
    case WC_RADIO:
        wd_arm(i + 1);          /* the toggle happens on the RELEASE: 6.6 says
                                 * `onchange` fires after the state settles,
                                 * and a press that slides off is a cancel */
        break;

    case WC_INPUT:
        k = w_iblk(id);
        if (k < 0)
            return;
        if (w_focus != k)
            w_setfocus(k);
        if (w_caron) {
            wd_lcaroff(w_fld[k]);
            w_caron = 0;
        }
        wd_lclick(w_fld[k], x, y);   /* the caret lands where the click did,
                                     * from the same pen the painter used */
        wd_lcaron(w_fld[k]);
        w_caron = 1;
        break;

    case WC_LIST:
        w_setfocus(-1);
        w_lclick(i, x, y);
        break;

    case WC_GRID:
        /* 6.9.4: a click in a data band moves the selection and does NOT arm
         * the bar; a click in the bar arms it. Both are native and neither
         * runs a bytecode op - the selection's own event goes in the ring. */
        k = w_gclick(i, x, y);
        if (!k) {
            w_setfocus(-1);
            break;
        }
        k = w_iblk(id);
        if (k < 0)
            break;
        if (w_focus != k)
            w_setfocus(k);
        if (w_caron) {
            wd_lcaroff(w_fld[k]);
            w_caron = 0;
        }
        wd_lclick(w_fld[k], x, y);
        wd_lcaron(w_fld[k]);
        w_caron = 1;
        break;

    default:
        w_setfocus(-1);
        w_gactive = 0;                  /* 6.9.4: the arrows belong to the
                                         * grid only while it is what was
                                         * last clicked */
        break;
    }
}

/* w_release - the release, and the gesture's whole verdict.
 *
 * os88ui_fire CLEARS, which is what makes a missed release harmless: the
 * kernel guarantees exactly one W_ONMOUSEUP per W_ONCLICK (SPEC.md 13.7), so
 * there is no path that leaves this armed and no fallback to forget. */
static void w_release(int x, int y)
{
    int armed, hit, i, id, other, oid;

    armed = wd_fire();
    if (w_bdown >= 0) {
        i = w_bdown;
        w_bdown = -1;
        other = w_find_lay(i);
        if (other >= 0)
            w_repaint_one(other);       /* up again, whatever happens next */
    }
    if (armed == 0)
        return;
    hit = w_hit(x, y);
    if (hit != armed)
        return;                         /* released elsewhere: a CANCEL, and
                                         * it draws nothing and fires nothing */
    i = armed - 1;
    id = w_lay[i].id;
    if (w_cflag[id] & (CF_HIDDEN | CF_DISABLED))
        return;

    if (w_lay[i].ctype == WC_BUTTON) {
        w_enq(id, WA_ONCLICK, 0, 0);    /* 3.4: onclick carries no words */
        return;
    }
    if (w_lay[i].ctype == WC_CHECK) {
        w_cval[id] = w_cval[id] ? 0 : 1;
        w_repaint_one(i);
        w_enq(id, WA_ONCHANGE, w_cval[id], 0);
        return;
    }
    if (w_lay[i].ctype == WC_RADIO) {
        if (w_cval[id])
            return;                     /* 6.6: checking the checked one is
                                         * not a change, and re-drawing it
                                         * would be a glyph for nothing */
        oid = w_radio_holder(i);
        if (oid >= 0) {
            w_cval[w_lay[oid].id] = 0;
            w_repaint_one(oid);         /* ...and the PREVIOUS holder, which
                                         * is the second of 6.6's two glyphs */
        }
        w_cval[id] = 1;
        w_repaint_one(i);
        w_enq(id, WA_ONCHANGE, 1, 0);
    }
}

/* w_radio_holder - which other radio in this one's group is checked, -1 none.
 * The group is an ATOM (3.3), so the comparison is one byte and never a
 * string: that is what interning the markup at pack time bought. */
static int w_radio_holder(int i)
{
    int k;
    unsigned g;

    g = w_patom(w_lay[i].props, WA_GROUP);
    if (g == 0)
        return -1;
    for (k = 0; k < w_nlay; k++) {
        if (k == i || w_lay[k].ctype != WC_RADIO)
            continue;
        if (w_cval[w_lay[k].id] == 0)
            continue;
        if (w_patom(w_lay[k].props, WA_GROUP) == g)
            return k;
    }
    return -1;
}

/* ============================================================================
 * KEYS (WEAVE-SPEC 6.7, 1.7)
 * ==========================================================================*/

/* w_key - one keystroke.  Answers 1 when it was used.
 *
 * ORDER MATTERS AND IT IS THE PLATFORM'S. The Reload shortcut is a CONTROL
 * character (1.7), which os88line_key hands straight back to its caller, so
 * a focused field and the shortcut cannot fight - but the test is made first
 * anyway, because a field that swallowed ^R would be a bug nobody could see
 * from the outside. Tab is next, then the field, then nothing. */
static int w_key(void *win, int ascii, int scan)
{
    int k, used;

    if (ascii == 0x12) {                /* ^R - 1.7's edit-run loop */
        w_reload(win);
        w_repaint2(win, 1);
        return 1;
    }
    if (ascii == 9) {                   /* Tab: the next field, 6.7 */
        w_tab();
        return 1;
    }
    /* 6.9.4's arrows move the grid's selection, but only while the grid is
     * what was last clicked AND the formula bar is not the armed field - a
     * left arrow inside a formula being typed is a caret move, not a cell
     * move, and the two would otherwise fight over the same keystroke. */
    if (w_focus < 0 || w_iof[w_focus] != (unsigned char)w_gid) {
        if (w_gkey(ascii, scan))
            return 1;
    }
    if (w_focus < 0)
        return 0;
    k = w_focus;
    if (ascii == 27 && w_gid && w_iof[k] == (unsigned char)w_gid) {
        w_gload();                      /* 6.9.4: Escape is the cancel */
        wd_ldraw(w_fld[k]);
        return 1;
    }
    if (ascii == 13 && w_gid && w_iof[k] == (unsigned char)w_gid) {
        /* 6.9.3, and it is NOT an onchange: a grid's bar commits a CELL.
         * Tenant 7 lives in the overlay (1.2.1), so "it ran" is asked
         * separately from what it decided - a refused module must say so
         * rather than swallow the keystroke. */
        w_gcommitran = 0;
        ovl_gcommit();
        if (!w_gcommitran)
            os88_toast("WEAVE.OVL is gone; no cell can be committed.", 0);
        return 1;
    }
    if (ascii == 13) {                  /* Enter COMMITS (3.3, 6.7) */
        /* AND NOTHING IS COPIED ANYWHERE. An earlier version of this line
         * made a VM string out of the field and parked it in w_ctext, so
         * that `who.text` would read what was typed - which meant allocating
         * OUTSIDE a slice, under the gfx lock, and collecting there when the
         * arena was full. WEAVE-SPEC 4.8 says the collector runs only BETWEEN
         * slices, never under the lock, and it says so for a reason.
         *
         * It was also unnecessary: w_getp reads a live <input>'s buffer
         * directly (6.7 - "the LIVE field, which is what `who.text` has to
         * mean"), so the field IS the state and there is nothing to mirror.
         * The commit is the event and the event is all of it. */
        w_enq(w_iof[k], WA_ONCHANGE, 0, 0);
        return 1;
    }
    if (w_caron) {
        wd_lcaroff(w_fld[k]);
        w_caron = 0;
    }
    used = wd_lkey(w_fld[k], ascii, scan) == 0;
    if (used) {
        /* 6.7: a keystroke letters ~2 cells, ~1.8 ms - the Note Pad contract
         * (SPEC.md 27.2). os88line_draw is what does that; it re-letters only
         * its own rect and the padding is the erase. */
        wd_ldraw(w_fld[k]);
        w_enq(w_iof[k], WA_ONKEY, ascii, scan);
    }
    wd_lcaron(w_fld[k]);
    w_caron = 1;
    return used;
}
