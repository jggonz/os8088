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
#define RC_QUIET   9                    /* ticks with no command (~0.5 s) */

static void ovl_rc_wake(void);
static void ovl_rc_fin(void);

static unsigned rc_f = RCF_CHECK;      /* the launch looks, too */
static int rc_pk;                       /* the prompt: 1 recover, 2 finish */
static unsigned rc_t;                   /* the tick of the last command */
static unsigned rc_n;                   /* records the journal holds */
static unsigned rc_sz;                  /* the file's size (20.17.2) */
static unsigned rc_seed;                /* this session's /NM seed */
static unsigned rc_dclus;               /* APPDATA's cluster, banked */

/* ---- SYSTEM/APPDATA on the boot volume (14.7, 19.9): bodies in the overlay ---- */
#define RC_JRN    "PAGEDESK.JRN"
#define RC_TGT    "PDSAVE.TGT"            /* the interrupted-save note (20.17.6) */
static unsigned rc_fclus, rc_cl, rc_k;
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

/* the /NM counter's first value: clock and tick, a multiple of 16 below
 * 0x7FF0, so the 128 records the store holds cannot wrap it (20.17.7) */
unsigned pdu_rc_seed(void)
{
    pdu_clk(pdu_clkw);
    rc_seed = ((pdu_clkw[2] ^ pdu_clkw[3]) + os88_ticks()) & 0x7FF0u;
    return rc_seed;
}

/* an open completed: look for a journal to offer */
void pdu_rc_open(void)
{
    if (rc_f & RCF_JRN)
        rc_f |= RCF_CHECK;
    pdu_kick();
}

/* a document is being opened: the journal of the last one is no longer ours
 * (a saved one is retired: its marks are in the file, 20.17.4) */
void pdu_rc_begin(void)
{
    if ((rc_f & (RCF_LIVE | RCF_FIN)) && pdu_pgsaved)
        rc_f |= RCF_RETIRE;
    rc_f &= RCF_OFF | RCF_RETIRE | RCF_CHECK | RCF_JRN;
    rc_pk = 0;
    rc_n = 0;
}

/* a prompt is up: its keys are Enter and Esc and no other */
int pdu_rc_key(int ascii)
{
    if (!rc_pk)
        return 0;
    if (ascii == 13 || ascii == 27) {
        rc_f |= RCF_ANS | (ascii == 13 ? RCF_YES : 0);
        pdu_kick();
    }
    return 1;
}

/* the wake: nothing while a job, a popover or a typing session has the
 * screen or pdu_tbuf (20.17.3); the launch and every open look for something
 * to offer WITHOUT the overlay, which is fetched only if there is */
void pdu_rc_wake(void)
{
    if (!(rc_f & (RCF_DIRTY | RCF_CHECK | RCF_RETIRE | RCF_ANS))
        || pdu_job != J_NONE || pdu_pop != P_NONE || pdu_tx >= 0 || pdu_gs
        || pdu_about)
        return;
    if (rc_f & (RCF_DIRTY | RCF_CHECK | RCF_RETIRE | RCF_ANS))
        ovl_rc_wake();
}

/* a save is about to delete its target (the writer was told REPLACE) */
void pdu_rc_fin(void)
{
    if (pdu_xsave.state == PDW_ST_REPLACE)
        ovl_rc_fin();
}
