/* ============================================================================
 * os8088 - apps/weave/wovl.c
 *
 * WEAVE.OVL - the runtime's one overlay (WEAVE-SPEC 1.2, SPEC.md
 * 73.14). #included by apps/weave/weave.c.
 *
 * WHAT LIVES HERE AND WHAT MAY NOT. WEAVE-SPEC 1.2: "refusable, UI-task-only
 * command paths - verbose bundle diagnostics, About, state import/export
 * dialogs, formula-function help. Nothing an event handler needs mid-run lives
 * here." The split is by FREQUENCY, not by size: a keystroke's path stays
 * resident, a menu command's can go out, because a menu command may refuse and
 * a keystroke may not.
 *
 * THE MARKING IS THE WHOLE MECHANISM. A function whose name begins `ovl_` has
 * its CODE emitted into `.modc` and shipped as WEAVE.OVL; every global,
 * literal and bss byte it names stays resident and DS-relative, and
 * tools/cc8086.py turns the calls into far calls both ways. Nothing else about
 * these functions is special - they name resident statics, resident string
 * literals and resident functions exactly as the code beside them does.
 *
 * TWO RULES THAT BITE:
 *   - NEVER take the address of an ovl_* function. The address of a function
 *     that is not resident is not a thing; cc8086.py refuses it by name.
 *   - A REFUSED LOAD RETURNS 0, and 0 has to mean "it did not happen"
 *     (apps/cc/crt0.asm). The overlay is resolved in the app's CURRENT
 *     directory, and after a double-click that is the DOCUMENT's folder
 *     (SPEC.md 54.9, 19.2.1) - which is why WEAVE.OVL ships in the same folder
 *     as the bundles and the package, and not in one of its own.
 *
 * IT SHIPS FROM DAY ONE rather than when the runtime outgrows 55,000 bytes,
 * for two reasons. WEAVE-SPEC 1.2 names it as a piece of the family, and
 * CLAUDE.md's rule is that the document changes before the code. And the
 * two-file disk layout - and its stale-pair failure mode - is then exercised
 * by `weavesmoke` from the first wave instead of being discovered in wave 4
 * under size pressure.
 * ==========================================================================*/

/* ovl_about - the About box's body (SPEC.md 20.4).
 *
 * A toast rather than a window: it costs this window no pixels and takes
 * itself down (SPEC.md 59), which is the right shape for something that is not
 * part of the document. */
static void ovl_about(void)
{
    os88_toast("WEAVE - web-style apps, compiled to a bundle "
               "(docs/WEAVE-SPEC.md)", 0);
}

/* ovl_info - Bundle -> Bundle Info: the verbose diagnostics WEAVE-SPEC 1.2
 * names as this overlay's first tenant.
 *
 * TWO THINGS, and the second is a CONTRACT rather than a convenience.
 *
 * The toast says what the bundle DECLARED - the claim asks, the sections, the
 * flags - because that is the arithmetic every refusal in section 10 is
 * computed from, and a user looking at "This app needs 42KB" wants to see
 * where 42 came from.
 *
 * The CONTENT AREA gets THE LAYOUT LISTING: the derived CW x CH and one line
 * per laid-out component carrying its comp_id, its ctype and its cell
 * rectangle. That listing is the family's LAYOUT DIFFERENTIAL and it is the
 * only machine-readable form the 8086's answer has. WEAVE-SPEC 7.2 says
 * "`weavesim --render` prints the cell rectangles and the 8086 must match them
 * exactly", and 12.1 makes weavesim the oracle; without this the only evidence
 * of that is a human reading two lists side by side, and after the components
 * were painted the only thing left on the glass would be PIXELS - which are a
 * far worse thing to diff a layout against, because they move on a font tweak,
 * a one-pixel inset, or a component that legitimately draws nothing, none of
 * which are layout regressions.
 *
 * THE FORMAT IS THE POINT, so it is boring, fixed-shape and greppable:
 *
 *     GRID <CW>x<CH> CARD <n> ROWS <r> COMPS <k>
 *     C <comp_id> <ctype> <x> <y> <w> <h>          ...one per component
 *     MORE <k>                                     ...only if it did not fit
 *
 * Cell coordinates, not pixels, because cells are what section 7 and
 * `--render` both speak in. Space-separated, one line per component, in
 * UISTREAM order - the same order the walk emits and the same order the
 * oracle prints. A truncated listing says MORE rather than simply stopping,
 * so a differ can tell "incomplete" from "wrong".
 *
 * IT LIVES HERE AND NOT IN THE PAINTER. An overlay call may refuse (SPEC.md
 * 73.14), so nothing on the paint path may make one; this draws once, when
 * the command is chosen, and the next W_PAINT brings the card back. A refused
 * load draws nothing and leaves the card alone.
 *
 * It builds its lines in the resident w_line[] and reads resident statics
 * throughout; only this function's code is out of segment. */
static void ovl_layout(void)
{
    int i, fit;

    if (w_state != W_ST_RUN || !w_layout(w_win))
        return;
    w_wipe();

    /* HOW MANY COMPONENT LINES THERE IS ROOM FOR, decided BEFORE the loop and
     * not inside it. Row 0 is the header, so CH-1 rows are left - but a card
     * that overflows spends one of them on MORE, and the row MORE lands on
     * must be a row no component line was written to.
     *
     * Getting that wrong is not a cosmetic overflow, and this is what it
     * looked like: the loop drew a component on the last row and MORE was
     * then written over it, leaving `MORE 5abel 28 14 12 1` on the glass -
     * the count and the tail of the line underneath it, because
     * os88_font_run letters exactly the cells it is given and a shorter line
     * leaves the longer one's tail behind (w_wipe's own note). A listing
     * whose last row is two lines superimposed is worse than one that
     * truncates, because a differ reads it as a component with a corrupt
     * rectangle rather than as a listing that ran out of room. */
    fit = w_ch - 1;
    if (w_nlay > fit)
        fit = w_ch - 2;
    if (w_infoln[0])
        fit--;                          /* one for the ask line */
    if (w_serr[0])
        fit--;                          /* ...and one for the LAST SENTENCE */
    if (fit < 0)
        fit = 0;

    w_l0();
    w_ls("GRID ");
    w_ln(w_cw);
    w_ls("x");
    w_ln(w_ch);
    w_ls(" CARD ");
    w_ln(w_entry);
    w_ls(" ROWS ");
    w_ln(w_nrow);
    w_ls(" COMPS ");
    w_ln(w_nlay);
    w_row(0, w_line);
    if (w_infoln[0])
        w_row(1, w_infoln);             /* the ask arithmetic, in full: the
                                         * toast that raised it holds 23
                                         * characters (SPEC.md 59.8) and this
                                         * line is 45 */

    for (i = 0; i < w_nlay && i < fit; i++) {
        w_l0();
        w_ls("C ");
        w_ln(w_lay[i].id);
        w_ls(" ");
        w_ls(w_ctname(w_lay[i].ctype));
        w_ls(" ");
        w_ln(w_lay[i].cx);
        w_ls(" ");
        w_ln(w_lay[i].cy);
        w_ls(" ");
        w_ln(w_lay[i].cw);
        w_ls(" ");
        w_ln(w_lay[i].ch);
        w_row(1 + (w_infoln[0] ? 1 : 0) + i, w_line);
    }

    if (i < w_nlay) {
        /* 7.4's clip, and it must SAY so: a listing that simply stops reads
         * as a walk that emitted fewer components. `fit` reserved this row,
         * so nothing is underneath it. */
        w_l0();
        w_ls("MORE ");
        w_ln(w_nlay - i);
        w_row(1 + (w_infoln[0] ? 1 : 0) + fit, w_line);
    }

    /* THE LAST SENTENCE, IN FULL (WEAVE-SPEC 10.6.0). The toast that raised
     * it holds 23 characters and every one of 10.6.1's sentences is longer,
     * so the toast is the alarm and this is the detail - which is what 1.2
     * names this overlay's diagnostics for. It is the bottom row of the
     * listing because it is the thing a user came here to read, and a row
     * was reserved for it above so nothing is underneath it. */
    if (w_serr[0])
        w_row(w_ch - 1, w_serr);
}

static void ovl_info(void)
{
    w_l0();
    w_ls("ask ");
    w_ln(w_claimkb + w_vmkb + w_gridkb + w_canvaskb);
    w_ls("KB = ");
    w_ln(w_claimkb);
    w_ls(" bundle + ");
    w_ln(w_vmkb);
    w_ls(" vm");
    if (w_gridkb) {
        w_ls(" + ");
        w_ln(w_gridkb);
        w_ls(" grid");
    }
    if (w_canvaskb) {
        w_ls(" + ");
        w_ln(w_canvaskb);
        w_ls(" canvas");
    }
    w_ls("; sections ");
    w_ln(w_nsec);
    w_ls(", flags ");
    w_ln(w_flags);
    os88_strcpy(w_infoln, w_line, sizeof(w_infoln));
    w_l0();                             /* ...and a SHORT form for the toast,
                                         * which holds 23 characters: `ask
                                         * 26KB, 9 sections` is 20 at the
                                         * biggest bundle the format allows */
    w_ls("ask ");
    w_ln(w_claimkb + w_vmkb + w_gridkb + w_canvaskb);
    w_ls("KB, ");
    w_ln(w_nsec);
    w_ls(" sections");
    os88_toast(w_line, 0);
    ovl_layout();                       /* ...and the listing, on the glass.
                                         * AFTER the toast, because the toast
                                         * draws over the desktop and this
                                         * draws in our content: doing it the
                                         * other way round puts the toast's
                                         * arrival between the wipe and the
                                         * lines, which is a blank window for
                                         * as long as the toast takes */
}

/* ============================================================================
 * OVERLAY TENANT 6: THE GRID'S LOAD PATH (WEAVE-SPEC 1.2.1, 5.6)
 *
 * It runs exactly once per bundle, at open, on the UI task, from inside
 * tenant 4's own body; it draws nothing and no handler can reach it; and its
 * refusal already has a meaning, because a grid claim that cannot be had is
 * 10.1's sentence and that path exists whether the overlay loads or not.
 * That is the paragraph 1.2 asks a new tenant to be able to write.
 *
 * Everything ELSE the grid does - the band composer, the FX VM, the resident
 * formula compiler, the sliced recalculation - is on a keystroke's path and
 * stays resident.
 *
 * IT ANSWERS THROUGH w_glderr AND NOT THROUGH A RETURN VALUE, because a
 * refused overlay returns 0 and 0 is also a perfectly good "no grid in this
 * bundle" (1.2's rule: say "it ran" separately from what it decided).
 * ==========================================================================*/

static void ovl_gridload(void)
{
    unsigned s, n, i, rec, id, ct, props, need, off, k;
    unsigned kind, pay, r, c, slot;

    w_glderr = 0;
    w_gid = 0;
    w_gcols = 0;
    w_grows = 0;
    if ((w_flags & WABF_GRID) == 0)
        return;                         /* no grid: nothing to claim, and the
                                         * header's grid KB is 0 (2.2.1) */

    /* Which component IS the grid, and how big is its sheet?  wval.c has
     * already refused a bundle with two grids or with cols/rows out of 3.3's
     * range, so this walk believes what it reads. */
    s = w_soff[W_UISTREAM];
    n = w_sextra[W_UISTREAM];
    for (i = 0; i < n; i++) {
        rec = s + i * W_REC_SIZE;
        if (w_b(w_seg, rec) != W_REC_COMP)
            continue;
        ct = w_b(w_seg, rec + W_R_CTYPE);
        if (ct != WC_GRID)
            continue;
        id = w_b(w_seg, rec + W_R_ID);
        props = w_w(w_seg, rec + W_R_PROPS);
        w_gid = (int)id;
        w_gcols = (int)w_pint(props, WA_COLS, 1);
        w_grows = (int)w_pint(props, WA_ROWS, 1);
        break;
    }
    if (w_gid == 0)
        return;                         /* WABF_GRID with no <grid> is a
                                         * malformed bundle wval.c already
                                         * refused; belt and braces */

    w_gseg = os88_mem_claim((int)w_gridkb);
    if (w_gseg == 0) {
        w_glderr = 1;                   /* 10.1's sentence, from the caller */
        return;
    }
    w_gkb = w_gridkb;

    /* 5.6's header and its dense array, zeroed - an empty cell is kind 0 and
     * a zeroed claim is a sheet of them, which is what makes the CELLS
     * section a list of the non-empty ones (2.10). */
    need = 16 + (unsigned)(w_grows * w_gcols) * 4;
    for (i = 0; i < need; i += 2)
        w_pw(w_gseg, i, 0);
    w_pb(w_gseg, 0, (unsigned)w_gcols);
    w_pw(w_gseg, 2, (unsigned)w_grows);
    w_pw(w_gseg, 4, need);              /* the pool's bump pointer */
    w_pw(w_gseg, 6, w_gkb << 10);       /* ...and its end */

    /* 2.10's CELLS records, 8 bytes each, already sorted row-major. */
    off = w_soff[W_CELLS];
    n = w_slen[W_CELLS] / 8;
    for (i = 0; i < n; i++) {
        rec = off + i * 8;
        r = w_b(w_seg, rec);
        c = w_b(w_seg, rec + 1);
        kind = w_b(w_seg, rec + 2);
        if (r >= (unsigned)w_grows || c >= (unsigned)w_gcols)
            continue;                   /* a record outside the sheet: wval.c
                                         * refused it, and a claim is not the
                                         * place to find out otherwise */
        pay = w_w(w_seg, rec + 4);
        if (kind == 1) {
            /* 2.10 kind 1 is a 16.16 dword; 5.6's kind 1 is a whole number
             * with no pool cost, so a fraction takes a pool slot here. */
            k = w_w(w_seg, rec + 6);
            if (pay == 0)
                w_gput((int)r, (int)c, WGK_INT, 0, k);
            else {
                slot = w_galloc(4);
                if (slot == 0)
                    continue;
                w_pw(w_gseg, slot, pay);
                w_pw(w_gseg, slot + 2, k);
                w_gput((int)r, (int)c, WGK_NUM, 0, slot);
            }
        } else if (kind == 2)
            w_gput((int)r, (int)c, WGK_ATOM, 0, pay);
        else if (kind == 3) {
            /* 5.6's formula slot: the FXCODE index, the cached value and
             * the pre-walk one. */
            slot = w_galloc(10);
            if (slot == 0)
                continue;
            w_pw(w_gseg, slot, pay);
            w_pw(w_gseg, slot + 2, 0);
            w_pw(w_gseg, slot + 4, 0);
            w_pw(w_gseg, slot + 6, 0);
            w_pw(w_gseg, slot + 8, 0);
            w_gput((int)r, (int)c, WGK_BFORM, 0, slot);
        }
    }

    wfx_bind(w_gseg, (unsigned)w_gcols, (unsigned)w_grows);

    /* The formula bar is a library-wired <input> out of the same eight-block
     * pool (6.9.1), keyed by the GRID's own comp_id - a grid is not an input,
     * so the two cannot collide. */
    w_ialloc(w_gid, WG_BARCOLS);
    w_gbar = w_iblk(w_gid);
    w_gsel_r = 1;
    w_gsel_c = 1;
    w_gtop = 0;
    w_gleft = 0;
    w_gload();
    w_gtrigger();                       /* 5.5: the cached values are computed
                                         * at open, in slices like every other
                                         * recalculation */
}
