/* ============================================================================
 * os8088 - apps/cword/cwcmd.c     the commands, the keyboard map and the mouse
 *
 * #included by cword.c - one translation unit (SPEC.md 67.1).
 *
 * The keyboard map is `Opus/resource/keys.cmd`'s, less the bindings that name
 * a feature this platform does not have. Three of Word's Ctrl keys cannot
 * arrive here at all: int 16h folds Ctrl-H, Ctrl-I and Ctrl-M into BkSp, Tab
 * and Enter, so italic, hidden text and one indent command reach the document
 * through the ribbon and the Format Character dialog instead. That is the same
 * collision apps/notepad documents, and it is a fact about the BIOS rather
 * than a decision.
 * ==========================================================================*/

/* ==========================================================================
 * THE CLIPBOARD (SPEC.md 55)
 *
 * Text only, both ways, and that is a narrowing worth stating: the clipboard
 * is one buffer of TEXT for the whole machine, so a Copy loses the formatting
 * and a Paste arrives in whatever the caret is set to. cw_rtf[] is borrowed as
 * the staging buffer - it is 12,000 bytes of bss that is otherwise idle
 * between an Open and a Save, and a second buffer would be that many bytes of
 * the 60KB ceiling spent on a second name for one thing.
 * ========================================================================*/

static void cw_copy(void *win)
{
    int a;
    int b;
    int i;
    int n;

    (void)win;
    if (!cw_sel_on)
        return;
    cw_sel_bounds();
    a = cw_w_end;
    b = cw_w_next;
    n = 0;
    for (i = a; i < b && n < CW_RTF_MAX - 1; i++)
        cw_rtf[n++] = cw_buf[i];
    if (os88_clip_put(cw_rtf, (unsigned)n) != 0)
        cw_toast("The clipboard would not take it.");
}

static void cw_cut(void *win)
{
    if (!cw_sel_on)
        return;
    cw_copy(win);
    cw_sel_kill(win);
}

static void cw_paste(void *win)
{
    int n;
    int i;
    int c;
    int lo;
    int put;

    n = os88_clip_size();
    if (n <= 0)
        return;
    if (n > CW_RTF_MAX)
        n = CW_RTF_MAX;
    n = (int)os88_clip_get(cw_rtf, (unsigned)n);
    if (n <= 0)
        return;

    if (cw_sel_on)
        cw_sel_kill(win);
    lo = cw_cur;
    put = 0;
    for (i = 0; i < n; i++) {
        c = (unsigned char)cw_rtf[i];
        if (c == '\r')
            continue;                   /* the LF beside it is the paragraph */
        if (c == '\t')
            c = ' ';
        if (c != '\n' && (c < 32 || c > 126))
            continue;
        if (c == '\n') {
            if (!cw_insert('\n', cw_pap_of(cw_cur)))
                break;
        } else if (!cw_insert(c, cw_attr)) {
            break;
        }
        cw_cur++;
        put++;
    }
    if (put == 0) {
        cw_toast("Nothing on the clipboard this document can hold.");
        return;
    }
    cw_touch();
    cw_show(win, lo, cw_cur, put);
}

/* ==========================================================================
 * FILES
 *
 * The format is RTF, read and written by tables transcribed out of Opus's
 * RTFOUT.C and RTFIN.C - which is what makes this program a descendant of
 * Word by inheritance rather than by resemblance. Both directions live in the
 * OVERLAY (SPEC.md 67.14): they run once per command and they are the largest
 * body of code in the package, which is exactly the trade that section exists
 * to make.
 * ========================================================================*/

static void cw_retitle(void *win)
{
    os88_strcpy(cw_title, "Microsoft Word - ", sizeof(cw_title));
    os88_strcpy(cw_title + 17, cw_fname[0] ? cw_fname : "Document",
                sizeof(cw_title) - 17);
    os88_wm_title(win, cw_title);

    /* the Window menu's item 1 carries the same live name */
    os88_strcpy(cw_wname, "1 ", sizeof(cw_wname));
    os88_strcpy(cw_wname + 2, cw_fname[0] ? cw_fname : "Document",
                sizeof(cw_wname) - 2);
}

static void cw_newdoc(void *win)
{
    cw_doc_clear();
    cw_cur = 0;
    cw_top = 0;
    cw_attr = 0;
    cw_pap_now = 0;
    cw_sel_on = 0;
    cw_ext = 0;
    cw_mod = 0;
    cw_fname[0] = 0;
    cw_retitle(win);
    cw_redraw_all(win);
}

/* ==========================================================================
 * THE COMMAND DISPATCHER
 *
 * One switch for the menu, the keyboard and the ribbon, so a command cannot
 * behave differently depending on which door it came through - which is the
 * defect that arrives the moment a shortcut is wired straight to its worker
 * routine and skips a guard the menu applies.
 * ========================================================================*/

static void cw_do(void *win, int act)
{
    switch (act) {
    case CWA_NEW:
        if (cw_mod) {
            ovl_dlg_open(win, CW_DLG_SAVE_NEW);
            return;
        }
        cw_newdoc(win);
        break;
    case CWA_OPEN:
        if (cw_mod) {
            ovl_dlg_open(win, CW_DLG_SAVE_OPEN);
            return;
        }
        os88_file_dlg(OS88_FDLG_OPEN, win, 0);
        break;
    case CWA_SAVE:
        if (cw_fname[0] == 0) {
            os88_file_dlg(OS88_FDLG_SAVE, win, "DOC.RTF");
            break;
        }
        if (ovl_file_store(win, cw_fname))
            cw_show(win, -2, 0, 0);
        break;
    case CWA_SAVEAS:
        os88_file_dlg(OS88_FDLG_SAVE, win, cw_fname[0] ? cw_fname : "DOC.RTF");
        break;
    case CWA_CLOSE:
    case CWA_EXIT:
        if (cw_mod) {
            ovl_dlg_open(win, CW_DLG_SAVE_EXIT);
            return;
        }
        os88_wm_hide(win);
        cw_quit = 1;
        break;

    case CWA_CUT:      cw_cut(win);   break;
    case CWA_COPY:     cw_copy(win);  break;
    case CWA_PASTE:    cw_paste(win); break;

    case CWA_SEARCH:   ovl_dlg_open(win, CW_DLG_FIND);    break;
    case CWA_REPL:     ovl_dlg_open(win, CW_DLG_REPLACE); break;
    case CWA_GOTO:     ovl_dlg_open(win, CW_DLG_GOTO);    break;
    case CWA_CHAR:     ovl_dlg_open(win, CW_DLG_CHAR);    break;
    case CWA_PARA:     ovl_dlg_open(win, CW_DLG_PARA);    break;
    case CWA_ABOUT:    ovl_dlg_open(win, CW_DLG_ABOUT);   break;

    case CWA_SORT:     ovl_util(win, 0); break;
    case CWA_RENUM:    ovl_util(win, 1); break;
    case CWA_TOC:      ovl_util(win, 2); break;

    case CWA_DRAFT:
        break;                          /* already the mode, and the only one */
    case CWA_VRIB:  cw_vrib = !cw_vrib; cw_redraw_all(win); break;
    case CWA_VRUL:  cw_vrul = !cw_vrul; cw_redraw_all(win); break;
    case CWA_VSTA:  cw_vsta = !cw_vsta; cw_redraw_all(win); break;
    case CWA_SHOWP: cw_showp = !cw_showp; cw_redraw_all(win); break;

    case CWA_WIN1:
    case CWA_UNDO:
    default:
        break;
    }
}

/* ==========================================================================
 * THE MOUSE
 * ========================================================================*/

/* The document offset a point in the text band names, or -1. A click past the
 * end of a row lands at the row's end, and a click below the last row lands at
 * the end of the document - both of which are what a text editor is expected
 * to do and neither of which falls out of the arithmetic on its own. */
static int cw_text_hit(int mx, int my)
{
    int r;
    int c;
    int n;
    int p;

    if (my < cw_ty)
        return -1;
    r = (my - cw_ty) / CW_PITCH;
    if (r >= cw_rows)
        r = cw_rows - 1;
    while (r > 0 && cw_ls[r] < 0)
        r--;
    if (cw_ls[r] < 0)
        return cw_len;

    n = cw_build_row(r);
    c = (mx - cw_tx) / 8 - cw_row_ind;
    if (c < 0)
        c = 0;
    if (c > n)
        c = n;
    p = cw_ls[r] + c;
    if (p > cw_le[r])
        p = cw_le[r];
    if (p > cw_len)
        p = cw_len;
    return p;
}

/* cw_track_menu - the press-drag-release gesture, which is modal by nature:
 * the button is down, the panel is on the glass, and until it comes up the
 * only thing that can happen is the highlight moving.
 *
 * It polls rather than waiting for events because there is no motion callback
 * in this OS (SPEC.md 11 delivers a click and a release), and it yields on
 * every pass so that the machine is not spun. The gfx lock IS held here - the
 * callback it is reached from holds it - which blocks other painters for as
 * long as the user holds the button down; that is what a modal menu means, it
 * is what the assembly port does, and it is bounded by the user rather than by
 * a computation (SPEC.md 67.11 rule 1).
 *
 * out: the item chosen, or -1; the panel is still up either way. */
static int cw_track_menu(void)
{
    struct os88_mouse *m;
    int k;
    int guard;

    m = &cw_ms;
    guard = 0;
    for (;;) {
        os88_mouse(m);
        if ((m->btn & 1) == 0)
            break;
        k = cw_menu_hit(m->x, m->y);
        cw_menu_move(k);
        os88_task_yield();
        if (++guard > 30000)            /* a mouse that never releases is a
                                         * broken driver, not a patient user */
            break;
    }
    return cw_m_item;
}

/* ==========================================================================
 * THE CALLBACKS
 * ========================================================================*/

void os88_onclick(int x, int y, void *win)
{
    int m;
    int k;
    int bit;
    int p;
    int was;

    cw_dragging = 0;                    /* every path below that is not a
                                         * press in the text leaves it clear */

    /* A dialog owns every click while it is up (SPEC.md 38's modality, done
     * here rather than by the kernel because these panels are ours). */
    if (cw_dlg) {
        if (!ovl_dlg_click(win, x, y))
            cw_dlg = 0;
        return;
    }

    if (cw_m_open >= 0) {
        /* a second click chooses from a sticky menu */
        k = cw_menu_hit(x, y);
        m = cw_m_open;
        cw_menu_close(win);
        if (k >= 0)
            cw_do(win, cw_it[cw_mn_first[m] + k].act);
        return;
    }

    m = cw_bar_hit(x, y);
    if (m >= 0) {
        cw_menu_open(m);
        k = cw_track_menu();            /* press-drag-release, if that is the
                                         * gesture; a plain click comes back
                                         * immediately with nothing chosen */
        if (k >= 0) {
            m = cw_m_open;
            cw_menu_close(win);
            cw_do(win, cw_it[cw_mn_first[m] + k].act);
        } else {
            cw_m_sticky = 1;            /* it stays up: this was a click */
        }
        return;
    }

    bit = cw_rib_hit(x, y);
    if (bit != 0) {
        if (bit < 0)
            cw_do(win, CWA_SHOWP);
        else
            cw_chp_apply(win, bit, 0);
        return;
    }

    k = cw_rul_hit(x, y);
    if (k != 0) {
        if (k <= 4)
            cw_align(win, k - 1);
        else if (k <= 7)
            cw_spacing(win, k - 5);
        else
            cw_openspc(win, k == 8);
        return;
    }

    p = cw_text_hit(x, y);
    if (p < 0)
        return;
    was = cw_cur;
    cw_cur = p;
    cw_anchor = p;
    cw_ext = 0;
    if (cw_sel_on) {
        cw_sel_on = 0;
        cw_show(win, -1, 0, 0);         /* the selection came off */
    } else {
        cw_moved(win, was);
    }
    cw_dragging = 1;
}

/* The release half of a content click (SPEC.md 13.7): it arrives even outside
 * the window, so everything here is range-tested. A drag through the text
 * selects it. */
void os88_onmouseup(int x, int y, void *win)
{
    int p;
    int was;

    was = cw_dragging;
    cw_dragging = 0;
    if (cw_dlg || cw_m_open >= 0)
        return;

    /* ONLY IF THE PRESS STARTED IN THE TEXT, which is what cw_dragging
     * records. A release is delivered wherever it happens, including outside
     * this window entirely (SPEC.md 13.7) - so without this test, choosing a
     * menu item whose row happens to lie over the text band SELECTS from the
     * caret to that point on the way out. Measured: File > Save As left the
     * first four cells of the document inverted, which reads as a corrupted
     * document rather than as a selection nobody asked for. */
    if (!was)
        return;
    p = cw_text_hit(x, y);
    if (p < 0 || p == cw_cur)
        return;
    cw_sel_on = 1;
    cw_cur = p;
    cw_show(win, -1, 0, 0);
}

/* The kernel's own bar carries this program's NAME and an About item; the nine
 * menus are the window's own (SPEC.md 12.2, 65.2). */
void os88_about(void *win)
{
    ovl_dlg_open(win, CW_DLG_ABOUT);
}

void os88_onfile(int mode, const char *name, unsigned size_lo,
                 unsigned size_hi, void *win)
{
    int changed;

    changed = 0;
    if (mode == OS88_FDLG_OPEN)
        changed = ovl_file_load(win, name, size_lo, size_hi);
    else if (ovl_file_store(win, name))
        cw_retitle(win);
    (void)changed;

    /* EVERYTHING, EVEN AFTER A SAVE THAT CHANGED NOT ONE CHARACTER. The
     * Standard File dialog is a window of its own and it was over ours: the
     * shadow still describes what WE last drew, and the glass now holds
     * whatever the dialog left plus whatever the kernel repainted underneath
     * it. Drawing the difference between those two is drawing the difference
     * between two things that are both wrong.
     *
     * Measured, and it is worth writing down because it looked like a
     * corrupted document rather than a stale screen: a save left the text row
     * reading `iH#there bold` for `Hi there bold`, and a click anywhere put it
     * right. The first version repainted only the status line here, on the
     * reasoning that a save changes no character - which is true, and is about
     * the DOCUMENT rather than about the glass. */
    cw_redraw_all(win);

    /* A pending New / Open / Close was waiting for this save to finish, which
     * is what makes "Save" in the save-changes prompt continue the command the
     * user actually asked for rather than just saving. */
    if (mode == OS88_FDLG_SAVE && cw_pending) {
        int p;

        p = cw_pending;
        cw_pending = 0;
        cw_mod = 0;
        cw_do(win, p);
    }
}

/* ==========================================================================
 * THE WORKER, AND WHY A PROGRAM WITH NOTHING TO DO IN THE BACKGROUND HAS ONE
 *
 * There is no self-close API slot: os88_wm_destroy() is documented for a
 * SECOND window a package owns, and a package's own window closes with the
 * instance. So File > Close and File > Exit do what SPEC.md 65.2 describes -
 * the UI half hides the window and raises a flag, and the worker destroys the
 * record and dies inside its next os88_task_alive(), which is the kernel's own
 * teardown path and frees the task, the region and every claim including
 * CWORD.OVL's.
 *
 * It sleeps four ticks a pass, so it costs a fifth of a second of latency on
 * a command the user has already committed to and nothing at all otherwise. It
 * draws nothing and takes no lock, which is what keeps it clear of SPEC.md
 * 20.6's rules - and the overlay is closed to it by cc_iswk() regardless
 * (SPEC.md 67.14: overlay code is UI-task code).
 * ========================================================================*/
void os88_worker(void *win)
{
    for (;;) {
        os88_task_sleep(4);
        if (cw_quit) {
            /* UNDER THE LOCK, which os88_wm_destroy() requires and
             * os88_task_alive() forbids - so they cannot share a bracket and
             * the lock has to close between them. Without the lock the destroy
             * does not take, the window record stays owned, and the next
             * task_alive() finds the instance perfectly healthy: the window is
             * hidden, the dock icon stays, and the program is unreachable and
             * still resident. Measured exactly that way before this bracket
             * existed.
             *
             * Then the destroy is what MAKES task_alive die: it answers the
             * liveness question of a window that no longer has an owner
             * (kernel/instance.inc, inst_pkg_alive), and the kernel's own
             * teardown frees the task, the instance, the region and every
             * claim - including the one CWORD.OVL is in. */
            os88_gfx_lock();
            os88_wm_destroy(win);
            os88_gfx_unlock();
        }
        os88_task_alive(win);           /* never returns once we are closing */
    }
}

void os88_onkey(int ascii, int scan, void *win)
{
    int was;
    int i;

    if (cw_dlg) {
        if (!ovl_dlg_key(win, ascii, scan))
            cw_dlg = 0;
        return;
    }

    /* --- an open menu is modal: it consumes every key ------------------- */
    if (cw_m_open >= 0) {
        int first;
        int n;

        first = cw_mn_first[cw_m_open];
        n = cw_mn_count[cw_m_open];
        if (scan == 0x01) {                     /* Esc */
            cw_menu_close(win);
            return;
        }
        if (scan == CW_K_DOWN || scan == CW_K_UP) {
            i = cw_m_item;
            for (;;) {
                i = i + ((scan == CW_K_DOWN) ? 1 : -1);
                if (i < 0)
                    i = n - 1;
                if (i >= n)
                    i = 0;
                if (!(cw_it[first + i].flag & CWF_SEP))
                    break;
            }
            cw_menu_move(i);
            return;
        }
        if (scan == CW_K_LEFT || scan == CW_K_RIGHT) {
            i = cw_m_open + ((scan == CW_K_RIGHT) ? 1 : -1);
            if (i < 0)
                i = CW_NMENU - 1;
            if (i >= CW_NMENU)
                i = 0;
            cw_menu_close(win);
            cw_menu_open(i);
            return;
        }
        if (ascii == 13) {                      /* Enter */
            i = cw_m_item;
            cw_menu_close(win);
            if (i >= 0)
                cw_do(win, cw_it[first + i].act);
            return;
        }
        if (ascii >= 'a' && ascii <= 'z')
            ascii = ascii - 32;
        if (ascii >= 'A' && ascii <= 'Z') {
            for (i = 0; i < n; i++) {
                if (cw_it[first + i].key == (unsigned char)ascii &&
                    !(cw_it[first + i].flag & CWF_SEP)) {
                    cw_menu_close(win);
                    cw_do(win, cw_it[first + i].act);
                    return;
                }
            }
        }
        return;
    }

    /* --- Alt+mnemonic opens a menu from the keyboard -------------------- */
    if (ascii == 0) {
        for (i = 0; i < CW_NMENU; i++) {
            if (scan == cw_mn_alt[i]) {
                cw_menu_open(i);
                cw_m_sticky = 1;
                return;
            }
        }
    }

    was = cw_cur;

    /* --- the control characters, keys.cmd's map ------------------------- */
    if (ascii != 0) {
        switch (ascii) {
        case CW_C_BOLD:   cw_chp_apply(win, CW_A_BOLD, 0);  return;
        case CW_C_ULINE:  cw_chp_apply(win, CW_A_ULINE, 0); return;
        case CW_C_WUL:    cw_chp_apply(win, CW_A_WUL, 0);   return;
        case CW_C_DUL:    cw_chp_apply(win, CW_A_DUL, 0);   return;
        case CW_C_SCAP:   cw_chp_apply(win, CW_A_SCAP, 0);  return;
                                        /* and NOT Ctrl+Space, keys.cmd's
                                         * "reset character formatting": int
                                         * 16h hands it over as ASCII 32 with
                                         * the space bar's own scan code, so it
                                         * is not distinguishable from SPACE
                                         * and a binding for it eats every
                                         * space the user types. Measured that
                                         * way round - "The quick" arrived as
                                         * "Thequick". It is the same collision
                                         * as Ctrl-H/I/M, and the command is
                                         * reached from Format > Character with
                                         * every box cleared. */
        case CW_C_LEFT:   cw_align(win, CW_AL_LEFT);        return;
        case CW_C_CENTER: cw_align(win, CW_AL_CENTER);      return;
        case CW_C_RIGHT:  cw_align(win, CW_AL_RIGHT);       return;
        case CW_C_JUST:   cw_align(win, CW_AL_JUST);        return;
        case CW_C_OPEN:   cw_openspc(win, 1);               return;
        case CW_C_CLOSE:  cw_openspc(win, 0);               return;
        case CW_C_INDENT: cw_indent(win, 5);                return;
        case CW_C_HANG:   cw_hang(win, 1);                  return;
        case CW_C_UNHANG: cw_hang(win, 0);                  return;
        case CW_C_FIND:   cw_do(win, CWA_CHAR);             return;
        case CW_C_GOTO:   cw_do(win, CWA_GOTO);             return;
        case CW_C_SAVE:   cw_do(win, CWA_SAVE);             return;
        default:
            break;
        }
    }

    if (ascii == 13) {
        cw_type_para(win);
        return;
    }
    if (ascii == 8) {
        cw_backspace(win);
        return;
    }
    if (ascii == 9) {
        cw_type(win, ' ');              /* a tab is a space in this model */
        return;
    }
    if (ascii >= 32 && ascii <= 126) {
        cw_type(win, ascii);
        return;
    }

    /* --- the extended keys --------------------------------------------- */
    if (ascii != 0)
        return;
    switch (scan) {
    case CW_K_LEFT:
        if (cw_cur > 0)
            cw_cur--;
        cw_moved(win, was);
        break;
    case CW_K_RIGHT:
        if (cw_cur < cw_len)
            cw_cur++;
        cw_moved(win, was);
        break;
    case CW_K_UP:
        cw_up();
        cw_moved(win, was);
        break;
    case CW_K_DOWN:
        cw_down();
        cw_moved(win, was);
        break;
    case CW_K_HOME:
        i = cw_row_of(cw_cur);
        cw_cur = (i >= 0) ? cw_ls[i] : cw_para_start(cw_cur);
        cw_moved(win, was);
        break;
    case CW_K_END:
        i = cw_row_of(cw_cur);
        if (i >= 0)
            cw_cur = cw_le[i];
        cw_moved(win, was);
        break;
    case CW_K_PGUP:
        for (i = 0; i < cw_rows - 1; i++)
            cw_up();
        cw_moved(win, was);
        break;
    case CW_K_PGDN:
        for (i = 0; i < cw_rows - 1; i++)
            cw_down();
        cw_moved(win, was);
        break;
    case CW_K_DEL:
        cw_del(win);
        break;
    case CW_K_INS:
        cw_ovr = !cw_ovr;
        cw_st_lamp[0] = 0;
        cw_status();
        break;
    case CW_K_F8:
        /* extend-selection mode: F8 arms it, plain caret motion then EXTENDS
         * the selection, and any edit or Esc disarms it (SPEC.md 65.2) */
        cw_ext = !cw_ext;
        if (cw_ext && !cw_sel_on)
            cw_anchor = cw_cur;
        cw_st_lamp[0] = 0;
        cw_status();
        break;
    case CW_K_F1:
        cw_do(win, CWA_ABOUT);
        break;
    case CW_K_F5:
        cw_do(win, CWA_GOTO);
        break;
    case CW_K_F4:
    case CW_K_SF4:
        ovl_find_again(win, scan == CW_K_SF4);
        break;
    case CW_K_F12:
        cw_do(win, CWA_SAVEAS);
        break;
    default:
        break;
    }
}
