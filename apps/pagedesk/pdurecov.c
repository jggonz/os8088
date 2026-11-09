/* ============================================================================
 * os8088 - apps/pagedesk/pdurecov.c   THE RECOVERY JOURNAL, RESIDENT PART
 *                                     (PAGEDESK-SPEC 14.7, 14.3, 20.17; PD-20)
 *
 * Everything that touches a file or the edit store is an ovl_* in
 * pdovlrecov.c. This part is what has to be there on the keystroke and the
 * wake: a flag word, the debounce, the prompt's key. A command only marks the
 * journal dirty; the write is the wake's, half a second after the last one,
 * so a callback never reaches a file (the stack gate, D21) and a typing run
 * or a drag is one write.
 * ==========================================================================*/

#define RCF_OFF    0x01                 /* off for the session (20.17.3) */
#define RCF_LIVE   0x02                 /* this document's header is on the disk */
#define RCF_DIRTY  0x04                 /* a command completed since the write */
#define RCF_CHECK  0x08                 /* look for a journal to offer */
#define RCF_RETIRE 0x10                 /* delete the journal */
#define RCF_ANS    0x20                 /* the prompt was answered... */
#define RCF_YES    0x40                 /* ...Enter */
#define RCF_FIN    0x200                /* this save's note is written */
#define RCF_JRN    0x80                 /* a journal is on the disk, not yet offered */
#define RCF_BYE    0x400                /* close the window (the wake, SPEC.md 75.2) */
#define RC_QUIET   9                    /* ticks with no command (~0.5 s) */

static void ovl_rc_wake(void);
static void ovl_rc_fin(void);
static void pdu_saveas_dlg(void);       /* pducmd.c */

static unsigned rc_f = RCF_JRN;        /* the first OPEN looks (not the launch:
                                         * the overlay fetch, D26's reason) */
static int rc_pk;                       /* the prompt: 1 recover, 2 finish,
                                         * 3 the close's question */
static unsigned rc_t;                   /* the tick of the last command */
static unsigned rc_fp[4];               /* the document's fingerprint (6.2) */

/* ---- SYSTEM/APPDATA on the boot volume (14.7, 19.9): bodies in the overlay ---- */
#define RC_JRN    "PAGEDESK.JRN"
#define RC_TGT    "PDSAVE.TGT"            /* the interrupted-save note (20.17.6) */
static unsigned rc_fclus, rc_k;
static struct os88_place rc_here;
#define rc_ff ((struct os88_find *)&ed_w)
static void ovl_rc_leave(void);
static int ovl_rc_enter(void);
static int ovl_rc_has(const char *nm);

void pdu_rc_cmd(void)
{
    rc_f |= RCF_DIRTY;
    rc_t = os88_ticks();
    pdu_kick();
}

/* a document is being opened. After a Save As the output holds the marks:
 * retire. Anything else - another document, or New, which also reopens with
 * pdu_pgsaved - leaves the journal as the last document's unsaved work, and
 * every later open looks at it again (20.17.4) */
void pdu_rc_begin(void)
{
    if (rc_f & (RCF_LIVE | RCF_FIN)) {
        if (pdu_pgsaved && !pdu_savekind)
            rc_f |= RCF_RETIRE;
        else
            rc_f |= RCF_JRN;
    }
    if (rc_f & RCF_JRN)                 /* look once the open is through: the
                                         * wake waits for the job */
        rc_f |= RCF_CHECK;
    rc_f &= RCF_OFF | RCF_RETIRE | RCF_CHECK | RCF_JRN;
    rc_pk = 0;
}

/* a prompt is up: its keys are Enter and Esc and no other - except while a
 * job runs, whose Esc cancels the job and must not discard a journal */
int pdu_rc_key(int ascii)
{
    if (!rc_pk || pdu_job != J_NONE)
        return 0;
    if (rc_pk == 3) {                   /* the close's question (20.17.8) */
        ascii |= 0x20;                  /* Esc is 27 | 32 = 59 */
        if (ascii == 'd') {             /* the wake closes; the journal goes */
            rc_f = (rc_f & ~RCF_DIRTY) | RCF_BYE;
            if (rc_f & RCF_LIVE)
                rc_f |= RCF_RETIRE;
        }
        else if (ascii == 's')          /* Save As: once saved, a close asks nothing */
            pdu_saveas_dlg();
        else if (ascii != 59)
            return 1;
        rc_pk = 0;
        pdu_tell("");
    } else if (ascii == 13)
        rc_f |= RCF_ANS | RCF_YES;
    else if (ascii == 27)
        rc_f |= RCF_ANS;
    pdu_kick();
    return 1;
}

/* the wake: nothing while a job, a popover or a typing session has the
 * screen or pdu_tbuf (20.17.3). The overlay is reached only when there is
 * work: a write, a look after an open (never a bare launch), an answer */
void pdu_rc_wake(void)
{
    if (!(rc_f & (RCF_DIRTY | RCF_CHECK | RCF_RETIRE | RCF_ANS | RCF_BYE))
        || pdu_job != J_NONE || pdu_pop != P_NONE || pdu_tx >= 0 || pdu_gs
        || pdu_about)
        return;
    ovl_rc_wake();
    if (rc_f & RCF_BYE) {               /* Discard (20.17.8): the journal went
                                         * first (RETIRE outranks the rest).
                                         * No job runs - the key waited - so
                                         * there is no temporary file, and the
                                         * instance's death frees every claim */
        os88_wm_close(pdu_win);         /* the negotiator is not asked again */
        rc_f = RCF_OFF;                 /* ...and this runs once */
    }
}

/* a save is about to delete its target (the writer was told REPLACE) */
void pdu_rc_fin(void)
{
    if (pdu_xsave.state == PDW_ST_REPLACE)
        ovl_rc_fin();
    else
        rc_f &= ~RCF_FIN;               /* each save writes its own note */
}

/* the close negotiator's question (SPEC.md 75.1, PD-20): unsaved changes
 * refuse the close and ask on the status line; the key answers (pdu_rc_key) */
int pdu_rc_close(void)
{
    if (pdu_doc != D_OPEN || !pdu_dirty())
        return 1;
    rc_pk = 3;
    pdu_tell("Unsaved: S saves, D discards, Esc keeps");
    return 0;
}
