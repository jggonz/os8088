/* Byteopoly - the session: local play, the banker host, the client, the
 * cable protocol and recovery (SPEC.md 131.5, 131.6, 131.7.5). Lane: link.
 *
 * OS-FREE: the UART is reached only through bp_ck_* (bp_cable.inc on the
 * guest, a fake pipe in hosttest/t_link.c) and time only through
 * os88_ticks(), so the host harness runs the whole protocol. Every byte of
 * session state is in ONE static record (bpl) plus bp_game, bp_mode,
 * bp_here, bp_diag and the buffers below; t_link.c runs two endpoints in
 * one process by swapping exactly those (its swap list).
 *
 * Frames are read and written through a cursor (bpl_r* / bpl_w*), in the
 * field order of SPEC.md 131.5.2's table: the table IS the code's order.
 * Every multi-byte field is little-endian, written a byte at a time; no
 * struct is ever copied onto the wire.
 *
 * Frequency split (SPEC.md 131.11): the tick, the dispatcher and the
 * transaction (INTENT, RESULT, ACK, PING, submit) are resident; the
 * handshake, lobby, chat, snapshot, checkpoint and resume are ovl_* code
 * (UI task only - every caller is - and never address-taken). */

/* ---- host harness defaults (131.12) ---------------------------------------
 * A hosttest program that is not t_link.c (t_smoke, t_rules, t_save) builds
 * this file too. It gets an unplugged cable, a silent UI and no checkpoint
 * files: the calls below are renamed to file-local no-ops until the end of
 * this file, so it neither needs nor clashes with the real definitions that
 * test (or bp_save.c, included later) may provide. t_link.c defines
 * BP_LINK_FAKE and supplies its own. */
#if defined(BP_HOST) && !defined(BP_LINK_FAKE)
static int bpl_h_open(int port) { (void)port; return BP_EIO; }
static void bpl_h_void(void) {}
static int bpl_h_zero(void) { return 0; }
static int bpl_h_send(const BPByte *b, unsigned n) { (void)b; (void)n; return BP_EIO; }
static int bpl_h_recv(BPByte *b) { (void)b; return 0; }
static const char *bpl_h_msg(void) { return "Off"; }
static BPWord bpl_h_rand(void) { return 0x1234; }
static void bpl_h_committed(const BPResult *r) { (void)r; }
static void bpl_h_chat(int m, const char *t) { (void)m; (void)t; }
static int bpl_h_write(const BPCkpt *m, const BPState *s) { (void)m; (void)s; return BP_EIO; }
static int bpl_h_commit(const BPCkpt *m) { (void)m; return BP_EIO; }
static int bpl_h_load(int r, BPCkpt *m, BPState *o) { (void)r; (void)m; (void)o; return BP_EIO; }
static int bpl_h_loadid(int r, const BPU32 *i, BPCkpt *m, BPState *o) { (void)r; (void)i; (void)m; (void)o; return BP_EIO; }
static int bpl_h_open2(int r, int w, BPCkpt *m, BPState *o) { (void)r; (void)w; (void)m; (void)o; return BP_EIO; }
#define bp_ck_open bpl_h_open
#define bp_ck_close bpl_h_void
#define bp_ck_tick bpl_h_void
#define bp_ck_heard bpl_h_zero
#define bp_ck_send bpl_h_send
#define bp_ck_recv bpl_h_recv
#define bp_ck_port bpl_h_zero
#define bp_ck_msg bpl_h_msg
#define bp_ck_rand bpl_h_rand
#define bp_ui_committed bpl_h_committed
#define bp_ui_link bpl_h_void
#define bp_ui_chat bpl_h_chat
#define bp_ckpt_write bpl_h_write
#define bp_ckpt_commit bpl_h_commit
#define bp_ckpt_load bpl_h_load
#define bp_ckpt_load_id bpl_h_loadid
#define bp_ckpt_open bpl_h_open2
#endif

/* ---- protocol constants (SPEC.md 131.5.2, 131.5.3) ----------------------- */
#define BPL_VER         2
#define BPL_BEAT        9
#define BPL_PING        18
#define BPL_RETRY       27
#define BPL_QUIET       128
#define BPL_TRIES       5
#define BPL_CHUNK       24
#define BPL_WINDOW      (3 * BPL_CHUNK)
#define BPL_FRAMES      8       /* frames handled a tick */

#define BPF_HELLO       1
#define BPF_LOBBY1      2
#define BPF_LOBBY2      3
#define BPF_NAME        4
#define BPF_READY       5
#define BPF_START       6
#define BPF_INTENT      7
#define BPF_TERMS       8
#define BPF_RESULT      9
#define BPF_REFUSE      10
#define BPF_ACK         11
#define BPF_PING        12
#define BPF_CHAT        13
#define BPF_CHATACK     14
#define BPF_SNAPREQ     15
#define BPF_SNAPHEAD    16
#define BPF_SNAPDATA    17
#define BPF_RESUME      18
#define BPF_CKREQ       19
#define BPF_CKBEGIN     20
#define BPF_CKREPLY     21
#define BPF_CKCOMMIT    22
#define BPF_BYE         23
#define BPF_N           24

/* each type's body length WITH its CRC: os88cable.inc's lk_lens IS this
 * table (bp_cable.inc: lk_lens equ _bpl_lens), so there is one copy */
const BPByte bpl_lens[BPF_N] = {
    0, 26, 27, 31, 17, 8, 19, 29, 18, 27, 12, 13, 14, 30, 6, 11, 19, 31,
    31, 9, 29, 11, 9, 6
};
/* who may receive each type: bit 0 the host, bit 1 the client */
static const BPByte bpl_dir[BPF_N] = {
    0, 3, 2, 2, 1, 1, 2, 1, 2, 2, 2, 1, 3, 3, 3, 1, 2, 2,
    3, 1, 2, 1, 2, 3
};

/* link phases (bpl.ph) */
#define BPL_UNBOUND     0
#define BPL_LOBBY       1
#define BPL_MATCH       2
/* checkpoint stages (bpl.ck): host 1-2, client 10-12 */
#define BPL_CK_WRITTEN  1       /* host: candidate written, CKBEGIN out */
#define BPL_CK_COMMIT   2       /* host: committed, CKCOMMIT out */
#define BPL_CK_ASKED    10      /* client: CKREQ out */
#define BPL_CK_CAND     11      /* client: candidate written, waits COMMIT */
#define BPL_CK_DONE     12      /* client: committed or failed, replies kept */

typedef struct {
    BPByte on, ph, intent, bad, left, diverged, lst;
    BPU32  me, peer;            /* endpoint ids (131.6.1); peer 0 unbound */
    BPWord clk, last, heard, sent, beat;
    const char *msg, *ckmsg;    /* the last event's line; bp_ck_msg seen */
    /* lobby (131.6.2) */
    BPSetup lob;                /* host: the setup; client: the agreed copy */
    BPSetup lin;                /* host: a NAME being checked; client: halves */
    BPSetup want;               /* client: its proposed names and tokens */
    BPWord lseq, linseq;
    BPByte rmask, lhave, linmask, myready, nameout, nametries;
    BPU32  seed;
    /* the transaction (131.6.3) */
    BPByte pend;                /* host: 1 RESULT, 2 START outstanding */
    BPByte tries;
    BPWord due;
    BPCmd  rcmd;                /* host: the retained result's command */
    BPByte rorigin, rd1, rd2, rcard, hwcode;
    BPWord rseq, hseq, hw;
    BPU32  rrev, rdig;
    BPByte cpend;               /* client: an INTENT is out */
    BPWord cseq;
    BPCmd  ccmd, tcmd;          /* client: the intent; TERMS received */
    BPByte tok, torigin;
    BPWord tseq;
    BPU32  dig;                 /* bp_digest(&bp_game), kept current */
    /* chat (131.6.6) */
    char   cout[BP_CHAT_LEN + 1];
    BPByte cbusy, cnum, ctries, crx;
    BPWord cdue, clast;
    /* snapshot (131.6.5): snap 1 host sending, 2 client receiving */
    BPByte snap, shead, stries;
    BPWord sn, sack, ssent, sdue;
    BPU32  srev, sdig;
    /* checkpoint and resume (131.7.5) */
    BPByte ck, ckwant, cktries, ckcode, pair, rsok, rscommit;
    BPWord ckdue;
    BPCkpt ckm, rsm;
} BPLink;

BPState bp_game;
BPByte bp_mode, bp_here;
BPDiag bp_diag;
static BPLink bpl;
static BPState bpl_scr;                 /* scratch: snapshot, resume */
static BPByte bpl_snap[BP_STATE_MAX];   /* snapshot bytes, either side */
static BPByte bpl_tx[BP_CK_MAX];
static BPByte bpl_rx[BP_CK_MAX];
static BPByte *bpl_cur;                 /* the frame cursor */
static BPResult bpl_res;
static BPCmd bpl_wc;                    /* a command being applied */
static BPU32 bpl_t1, bpl_t2, bpl_t3, bpl_t4;
static BPByte bpl_d[3];
static BPCkpt bpl_km;
static char bpl_line[40];
static char bpl_chat[BP_CHAT_LEN + 1];

/* ---- the cursor ------------------------------------------------------------ */
static BPByte bpl_r8(void)
{
    return *bpl_cur++;
}
static BPWord bpl_r16(void)
{
    BPWord v;
    v = (BPWord)(bpl_cur[0] | (bpl_cur[1] << 8));
    bpl_cur += 2;
    return v;
}
static void bpl_r32(BPU32 *v)
{
    v->lo = bpl_r16();
    v->hi = bpl_r16();
}
static void bpl_w8(int v)
{
    *bpl_cur++ = (BPByte)v;
}
static void bpl_w16(BPWord v)
{
    bpl_w8(v);
    bpl_w8(v >> 8);
}
static void bpl_w32(const BPU32 *v)
{
    bpl_w16(v->lo);
    bpl_w16(v->hi);
}
/* bytes as bytes (a command's type/seat/arg/to, a name, chat text) */
static void bpl_wn(const void *s, unsigned n)
{
    os88_memcpy(bpl_cur, s, n);
    bpl_cur += n;
}
static void bpl_rn(void *d, unsigned n)
{
    os88_memcpy(d, bpl_cur, n);
    bpl_cur += n;
}
/* the trade fields of a command, in INTENT/TERMS order */
static void bpl_wterms(const BPCmd *c)
{
    bpl_w32(&c->give);
    bpl_w32(&c->get);
    bpl_w8(c->disks);
    bpl_w8(c->payer);
}
static void bpl_rterms(BPCmd *c)
{
    bpl_r32(&c->give);
    bpl_r32(&c->get);
    c->disks = bpl_r8();
    c->payer = bpl_r8();
}

/* ---- small helpers ------------------------------------------------------- */
static int bpl_eq(const BPU32 *a, const BPU32 *b)
{
    return a->lo == b->lo && a->hi == b->hi;
}
static int bpl_memdiff(const void *a, const void *b, unsigned n)
{
    const BPByte *x, *y;
    x = (const BPByte *)a;
    y = (const BPByte *)b;
    while (n--) if (*x++ != *y++) return 1;
    return 0;
}
static int bpl_due(BPWord t)
{
    return (BPWord)(bpl.clk - t) < 0x8000u;
}
static BPWord bpl_after(void)
{
    BPWord n;
    n = BPL_RETRY;
    if (bp_link_state() == BP_L_PAUSED) n = 2 * BPL_RETRY;
    return (BPWord)(bpl.clk + n);
}
static void bpl_rand32(BPU32 *r)
{
    r->lo = (BPWord)(bp_ck_rand() ^ os88_ticks());
    r->hi = bp_ck_rand();
    if (r->lo == 0) r->lo = 0x5A5A;
}
static void bpl_say(const char *m)
{
    bpl.msg = m;
    bp_ui_link();
}
static void bpl_redigest(void)
{
    bp_digest(&bp_game, &bpl.dig);
}
static void bpl_bump(BPByte *n)
{
    if (*n < 255) ++*n;
    bp_diag.retry++;
}

/* ---- sending ------------------------------------------------------------- */
/* a frame of this type: zeroed, the type and (but for HELLO) our tag */
static void bpl_begin(int type)
{
    os88_memset(bpl_tx, 0, BP_CK_MAX);
    bpl_cur = bpl_tx;
    bpl_w8(type);
    if (type != BPF_HELLO) bpl_w16(bpl.me.lo);
}
static int bpl_send(void)
{
    int e;
    e = bp_ck_send(bpl_tx, (unsigned)(bpl_lens[bpl_tx[0]] - 2));
    if (e == BP_OK) {
        bp_diag.tx++;
        bpl.sent = bpl.clk;
    }
    return e;
}
/* a frame carrying our revision and digest: ACK, or PING with its flags */
static void bpl_send_state(int type)
{
    int f;
    if (type == BPF_PING && bpl.ph == BPL_MATCH) bpl_redigest();  /* the state hash, afresh */
    bpl_begin(type);
    bpl_w32(&bp_game.rev);
    bpl_w32(&bpl.dig);
    if (type == BPF_PING) {
        f = bpl.diverged ? 4 : 0;
        if (bp_sess_busy()) f |= 1;
        if (bp_link_state() == BP_L_PAUSED) f |= 2;
        bpl_w8(f);
    }
    bpl_send();
}
static void bpl_send_refuse(BPWord seq, int code)
{
    bpl_begin(BPF_REFUSE);
    bpl_w16(seq);
    bpl_w8(code);
    bpl_w32(&bp_game.rev);
    bpl_send();
}
/* host: the retained RESULT (TERMS first for a PROPOSE), or START */
static void bpl_send_result(void)
{
    if (bpl.pend == 2) {
        bpl_begin(BPF_START);
        bpl_w16(bpl.lseq);
        bpl_w32(&bpl.seed);
        bpl_w32(&bpl.rrev);
        bpl_w32(&bpl.rdig);
        bpl_send();
        return;
    }
    if (bpl.rcmd.type == BP_C_PROPOSE) {
        bpl_begin(BPF_TERMS);
        bpl_w8(bpl.rorigin);
        bpl_w16(bpl.rseq);
        bpl_wterms(&bpl.rcmd);
        bpl_send();
    }
    bpl_begin(BPF_RESULT);
    bpl_w8(bpl.rorigin);
    bpl_w16(bpl.rseq);
    bpl_w32(&bpl.rrev);
    bpl_w32(&bpl.rdig);
    bpl_w8(bpl.rd1);
    bpl_w8(bpl.rd2);
    bpl_w8(bpl.rcard);
    bpl_wn(&bpl.rcmd.type, 4);          /* type, seat, arg, to: bytes */
    bpl_w32(&bpl.rcmd.amount);
    bpl_send();
}
static void bpl_send_intent(void)
{
    bpl_begin(BPF_INTENT);
    bpl_w16(bpl.cseq);
    bpl_wn(&bpl.ccmd.type, 4);
    bpl_w32(&bpl.ccmd.rev);
    bpl_w32(&bpl.ccmd.amount);
    bpl_wterms(&bpl.ccmd);
    bpl_send();
}

/* ---- the state the UI sees ----------------------------------------------- */
int bp_link_state(void)
{
    if (bp_mode == BP_MODE_LOCAL || !bpl.on) return BP_L_OFF;
    if (bpl.ph == BPL_UNBOUND) {
        if (bpl.bad) return BP_L_BADPEER;
        return bpl.peer.lo ? BP_L_PEER : BP_L_SEARCH;
    }
    if (bpl.ph == BPL_LOBBY) return BP_L_LOBBY;
    if (bpl.snap) return BP_L_RESYNC;
    if (bpl.diverged) return BP_L_DIVERGED;
    if (bpl.left || (BPWord)(bpl.clk - bpl.heard) >= BPL_QUIET
        || bpl.tries >= BPL_TRIES || bpl.cktries >= BPL_TRIES || bpl.stries >= BPL_TRIES)
        return BP_L_PAUSED;
    return BP_L_PLAY;
}

int bp_sess_busy(void)
{
    if (bp_mode == BP_MODE_LOCAL) return 0;
    if (bpl.snap || bpl.diverged) return 1;
    if (bp_mode == BP_MODE_HOST)
        return bpl.pend || bpl.ckwant || (bpl.ck && bpl.ck < BPL_CK_ASKED);
    return bpl.cpend || bpl.ck == BPL_CK_ASKED || bpl.ck == BPL_CK_CAND;
}

int bp_sess_mine(int seat)
{
    if (bp_mode == BP_MODE_LOCAL) return 1;
    if (seat < 0 || seat >= BP_SEATS) return 0;
    if (bpl.ph == BPL_MATCH) return bp_game.machine[seat] == bp_here;
    return bpl.lob.machine[seat] == bp_here;
}

int bp_sess_pair(void)
{
    return bpl.pair;
}

static const char *const bpl_states[9] = {
    "Not linked", "", "Other PC found: agreeing", "The other PC cannot play this match",
    "Linked: lobby", "", "Link paused: waiting for the other PC", "Sending the board",
    "Boards differ: use the banker's board?"
};

static const char *ovl_msg(void)
{
    int s;
    if (bp_mode == BP_MODE_LOCAL) return "Same PC";
    s = bp_link_state();
    if (s == BP_L_SEARCH) return bp_ck_msg();
    if (bpl.msg && s != BP_L_RESYNC) return bpl.msg;
    if (s == BP_L_DIVERGED && bp_mode == BP_MODE_HOST) return "Boards differ: the other PC must resync";
    if (s != BP_L_PLAY) return bpl_states[s];
    /* playing: role, port, baud, the synchronised revision, waiting */
    os88_strcpy(bpl_line, bp_mode == BP_MODE_HOST ? "Banker COM0 2400 rev " : "Linked COM0 2400 rev ",
                sizeof bpl_line);
    bpl_line[10] = (char)('0' + bp_ck_port());
    os88_utoa(bp_game.rev.lo, bpl_line + 21);
    if (bp_sess_busy()) os88_strcpy(bpl_line + os88_strlen(bpl_line), " waiting", 9);
    return bpl_line;
}
const char *bp_link_msg(void) { return ovl_msg(); }

/* ---- local mode, and the start of every mode -------------------------------- */
static void bpl_reset(void)
{
    if (bpl.on) bp_ck_close();
    os88_memset(&bpl, 0, sizeof(BPLink));
    bpl.last = (BPWord)os88_ticks();
}

void bp_sess_local(void)
{
    bpl_reset();
    bp_mode = BP_MODE_LOCAL;
    bp_here = 0;
}

/* a new match on a fresh seed. A roll-off tie storm can overflow the
 * event list (BP_EDEPTH, about 1e-6, *s untouched): the host - or the one
 * local machine - draws another seed, so a client never reseeds by itself
 * and START always carries a seed the engine accepted */
static int ovl_new_seeded(const BPSetup *su)
{
    int e, k;
    for (k = 0; k < 4; k++) {
        bpl_rand32(&bpl.seed);
        e = bp_new_match(&bp_game, su, &bpl.seed, &bpl_res);
        if (e != BP_EDEPTH) break;
    }
    return e;
}

static int ovl_sess_new(const BPSetup *su)
{
    int e;
    if (bp_mode == BP_MODE_HOST) return bp_lobby_start();
    if (bp_mode != BP_MODE_LOCAL) return BP_EPHASE;
    e = ovl_new_seeded(su);
    if (e == BP_OK) bp_ui_committed(&bpl_res);
    return e;
}
int bp_sess_new(const BPSetup *su) { return ovl_sess_new(su); }

static void ovl_adopted(const char *m)
{
    bpl_say(m);
    os88_memset(&bpl_res, 0, sizeof(BPResult));
    bp_ui_committed(&bpl_res);
}

static int ovl_load_local(const BPState *loaded)
{
    int e;
    if (bp_mode != BP_MODE_LOCAL) return BP_EPHASE;
    e = bp_validate(loaded);
    if (e != BP_OK) return e;
    os88_memcpy(&bp_game, loaded, sizeof(BPState));
    ovl_adopted(0);
    return BP_OK;
}
int bp_sess_load_local(const BPState *loaded) { return ovl_load_local(loaded); }

/* ---- the transaction (131.6.3) ---------------------------------------------- */
/* host: bpl_wc applied once and retained; BP_OK or the refusal. The trade
 * fields of anything but a PROPOSE are zeroed first (canonical: what the
 * client replays is exactly what was applied) */
static int bpl_commit(int origin, BPWord seq)
{
    int e;
    if (bpl_wc.type != BP_C_PROPOSE) os88_memset(&bpl_wc.give, 0, 10);
    e = bp_apply(&bp_game, &bpl_wc, &bpl_res);
    if (e != BP_OK) return e;
    bpl_redigest();
    os88_memcpy(&bpl.rcmd, &bpl_wc, sizeof(BPCmd));
    bpl.rorigin = (BPByte)origin;
    bpl.rseq = seq;
    bp_u32_copy(&bpl.rrev, &bp_game.rev);
    bp_u32_copy(&bpl.rdig, &bpl.dig);
    bpl.rd1 = bpl_res.d1;
    bpl.rd2 = bpl_res.d2;
    bpl.rcard = bpl_res.card;
    bpl.pend = 1;
    bpl.tries = 0;
    bpl.due = bpl_after();
    bpl.msg = 0;
    bpl_send_result();
    bp_ui_committed(&bpl_res);
    return BP_OK;
}

int bp_sess_submit(const BPCmd *c)
{
    int e;
    if (bp_mode == BP_MODE_LOCAL) {
        e = bp_apply(&bp_game, c, &bpl_res);
        if (e == BP_OK) bp_ui_committed(&bpl_res);
        return e;
    }
    e = bp_link_state();
    if (e == BP_L_DIVERGED || e == BP_L_RESYNC) return BP_EPENDING;
    if (e != BP_L_PLAY) return BP_EPAUSED;
    if (c->seat >= BP_SEATS || bp_game.machine[c->seat] != bp_here) return BP_ENOTMINE;
    if (bp_sess_busy()) return BP_EPENDING;
    os88_memcpy(&bpl_wc, c, sizeof(BPCmd));
    if (bp_mode == BP_MODE_HOST) {
        bpl.hseq++;
        return bpl_commit(0, bpl.hseq);
    }
    if (bpl.cseq == 0xFFFFu) {
        bpl.msg = "Save and resume the match";
        return BP_EOVERFLOW;
    }
    e = bp_check(&bp_game, &bpl_wc);
    if (e != BP_OK) return e;
    os88_memcpy(&bpl.ccmd, &bpl_wc, sizeof(BPCmd));
    bpl.cseq++;
    bpl.cpend = 1;
    bpl.tries = 0;
    bpl.due = bpl_after();
    bpl.msg = 0;
    bpl_send_intent();
    return BP_EPENDING;
}

/* host: an INTENT. A seq at or below the high-water mark is a duplicate:
 * the retained answer for exactly that seq, never a second application */
static void bpl_h_intent(void)
{
    BPWord seq;
    int e;
    seq = bpl_r16();
    if (bpl.ph != BPL_MATCH || seq == 0) goto bad;
    if (seq <= bpl.hw) {
        if (seq != bpl.hw) goto bad;
        if (bpl.hwcode) bpl_send_refuse(seq, bpl.hwcode);
        else if (bpl.rorigin == 1 && bpl.rseq == seq) bpl_send_result();
        else goto bad;
        return;
    }
    if (bp_sess_busy()) {               /* not taken: the client retries */
        bpl_send_refuse(seq, BP_EPENDING);
        return;
    }
    bpl_rn(&bpl_wc.type, 4);
    bpl_r32(&bpl_wc.rev);
    bpl_r32(&bpl_wc.amount);
    bpl_rterms(&bpl_wc);
    bpl.hw = seq;
    e = BP_ENOTMINE;
    if (bpl_wc.seat < BP_SEATS && bp_game.machine[bpl_wc.seat] == 1) e = bpl_commit(1, seq);
    bpl.hwcode = (BPByte)e;
    if (e != BP_OK) bpl_send_refuse(seq, e);
    return;
bad:
    bp_diag.frame++;
}

/* host: an ACK - the outstanding revision with our digest retires it */
static void bpl_h_ack(void)
{
    bpl_r32(&bpl_t1);
    bpl_r32(&bpl_t2);
    if (bpl.pend && bpl_eq(&bpl_t1, &bpl.rrev)) {
        if (!bpl_eq(&bpl_t2, &bpl.rdig)) {
            bpl.diverged = 1;
            return;
        }
        bpl.pend = 0;
        bpl.tries = 0;
    } else if (!bpl_eq(&bpl_t1, &bp_game.rev) || !bpl_eq(&bpl_t2, &bpl.dig)) return;
    bpl.diverged = 0;                       /* (an adopted snapshot, too) */
    bpl.snap = 0;
}

static void bpl_diverge(void)
{
    if (bpl.diverged) return;
    bpl.diverged = 1;
    bpl.msg = 0;
    bpl_send_state(BPF_PING);
}

/* client: a RESULT (TERMS for a PROPOSE arrived just before it). Applied
 * only at our revision + 1; our revision: the ACK again; older: stale;
 * later: a revision was skipped, which is a divergence */
static void bpl_c_result(void)
{
    BPByte origin;
    BPWord seq;
    int e;
    if (bpl.ph != BPL_MATCH) goto bad;
    origin = bpl_r8();
    seq = bpl_r16();
    bpl_r32(&bpl_t1);                       /* the new revision */
    bpl_r32(&bpl_t2);                       /* the host's digest */
    if (origin == 1 && seq == bpl.cseq) {
        bpl.cpend = 0;
        bpl.tries = 0;
    }
    if (bpl_eq(&bpl_t1, &bp_game.rev)) {
        bpl_send_state(BPF_ACK);
        return;
    }
    bp_u32_copy(&bpl_t3, &bp_game.rev);
    bpl_t3.lo++;
    if (!bpl_t3.lo) bpl_t3.hi++;
    if (!bpl_eq(&bpl_t1, &bpl_t3)) {
        if (bpl_t1.hi > bp_game.rev.hi || (bpl_t1.hi == bp_game.rev.hi && bpl_t1.lo > bp_game.rev.lo))
            bpl_diverge();
        goto bad;
    }
    if (bpl.diverged) goto bad;
    bpl_rn(bpl_d, 3);                       /* d1, d2, card */
    os88_memset(&bpl_wc, 0, sizeof(BPCmd));
    bpl_rn(&bpl_wc.type, 4);
    bpl_r32(&bpl_wc.amount);
    bp_u32_copy(&bpl_wc.rev, &bp_game.rev);
    if (bpl_wc.type == BP_C_PROPOSE) {
        if (!bpl.tok || bpl.torigin != origin || bpl.tseq != seq) goto bad;
        os88_memcpy(&bpl_wc.give, &bpl.tcmd.give, 10);
    }
    bpl.tok = 0;
    e = bp_apply(&bp_game, &bpl_wc, &bpl_res);
    if (e == BP_OK) bpl_redigest();
    bpl_send_state(BPF_ACK);
    if (e != BP_OK || bpl_res.d1 != bpl_d[0] || bpl_res.d2 != bpl_d[1] || bpl_res.card != bpl_d[2]
        || !bpl_eq(&bpl_t2, &bpl.dig)) bpl_diverge();
    else bpl.msg = 0;
    if (e == BP_OK) bp_ui_committed(&bpl_res);
    return;
bad:
    bp_diag.frame++;
}

/* client: TERMS, kept for the RESULT that follows */
static void bpl_c_terms(void)
{
    bpl.tok = 1;
    bpl.torigin = bpl_r8();
    bpl.tseq = bpl_r16();
    bpl_rterms(&bpl.tcmd);
}

/* client: a REFUSE for its intent; BP_EPENDING means "busy: again later" */
static void bpl_c_refuse(void)
{
    int code;
    if (!bpl.cpend || bpl_r16() != bpl.cseq) { bp_diag.frame++; return; }
    code = bpl_r8();
    bpl.due = bpl_after();
    if (code == BP_EPENDING) return;
    bpl.cpend = 0;
    bpl.tries = 0;
    os88_strcpy(bpl_line, "Banker: ", sizeof bpl_line);
    os88_strcpy(bpl_line + 8, bp_err_text(code), sizeof bpl_line - 8);
    bpl_say(bpl_line);
}

/* either: a PING - liveness, and the state hash at equal revisions */
static void bpl_ping(void)
{
    BPByte f;
    if (bpl.ph != BPL_MATCH) return;
    bpl_r32(&bpl_t1);
    bpl_r32(&bpl_t2);
    f = bpl_r8();
    if (bpl_eq(&bpl_t1, &bp_game.rev) && !bpl_eq(&bpl_t2, &bpl.dig)) f |= 4;
    if (f & 4) {
        if (bp_mode == BP_MODE_CLIENT) bpl_diverge();
        else bpl.diverged = 1;
    } else if (bp_mode == BP_MODE_HOST && !bpl.pend && bpl_eq(&bpl_t1, &bp_game.rev))
        bpl.diverged = 0;
}

/* ---- the handshake (131.6.1) ------------------------------------------------- */
static void ovl_send_hello(void)
{
    bpl_begin(BPF_HELLO);
    bpl_wn("BYTO", 4);
    bpl_w8(BPL_VER);
    bpl_w8(bp_mode);
    bpl_w32(&bpl.me);
    bp_fingerprint(&bpl_t4);
    bpl_w32(&bpl_t4);
    bpl_w32(&bpl.peer);
    bpl_w8(bpl.intent);
    bpl_w32(&bpl.rsm.match_id);
    bpl_send();
}

static void ovl_unbind(void)
{
    bpl.ph = BPL_UNBOUND;
    bp_u32_set(&bpl.peer, 0, 0);
    bpl.rmask = 0;
    bpl.myready = 0;
    bpl.lhave = 0;
    bpl.rsok = 0;
}

static void ovl_lobby_send(void);

static void ovl_hello(void)
{
    const char *why;
    BPByte role, intent;
    if (bpl_memdiff(bpl_cur, "BYTO", 4)) goto bad;
    bp_ck_heard();
    bpl_cur += 5;                           /* (the version is checked below) */
    role = bpl_r8();
    bpl_r32(&bpl_t1);                       /* their id */
    bpl_r32(&bpl_t2);                       /* their fingerprint */
    bpl_r32(&bpl_t3);                       /* the id they have bound */
    intent = bpl_r8();
    bpl_r32(&bpl_t4);                       /* the match they resume */
    if (bpl.ph == BPL_MATCH) {
        if (!bpl_eq(&bpl_t1, &bpl.peer)) {
            bpl_say("Another Byteopoly answered: not this match's partner");
            goto bad;
        }
        bpl.heard = bpl.clk;                /* our partner, still agreeing: */
        if (!bpl_eq(&bpl_t3, &bpl.me)) ovl_send_hello();   /* (it lacks our id) */
        return;
    }
    if (!bpl_t1.lo) goto bad;
    why = 0;
    if (intent && !bpl_eq(&bpl_t4, &bpl.rsm.match_id)) why = "The other PC's save is another match";
    if (intent != bpl.intent)
        why = intent ? "The other PC is resuming a saved match" : "The other PC is starting a new match";
    if (role == bp_mode) why = bp_mode == BP_MODE_HOST ? "The other PC chose Host too" : "The other PC chose Join too";
    if (role != BP_MODE_HOST && role != BP_MODE_CLIENT) goto bad;
    bp_fingerprint(&bpl_t4);
    if (bpl_rx[5] != BPL_VER || !bpl_eq(&bpl_t2, &bpl_t4)) why = "Different Byteopoly version";
    if (why) {
        if (bpl.ph == BPL_UNBOUND && (!bpl.bad || bpl.msg != why)) {
            bpl.bad = 1;
            bpl_say(why);
        }
        goto bad;
    }
    if (bpl.bad) {
        bpl.bad = 0;
        bpl.msg = 0;
    }
    if (!bpl_eq(&bpl_t1, &bpl.peer)) {      /* a new partner, or a rebind */
        ovl_unbind();
        bp_u32_copy(&bpl.peer, &bpl_t1);
        bpl.lseq++;
        bp_ui_link();
    }
    bpl.heard = bpl.clk;
    /* answered only while the other side lacks our id: two bound sides
     * never echo HELLOs at each other */
    if (!bpl_eq(&bpl_t3, &bpl.me)) ovl_send_hello();
    else if (bpl.ph == BPL_UNBOUND) {
        bpl.ph = BPL_LOBBY;
        bpl.msg = 0;
        bpl.beat = bpl.clk;
        ovl_send_hello();                   /* (so it learns we bound it) */
        ovl_lobby_send();
        bp_ui_link();
    }
    return;
bad:
    bp_diag.frame++;
}

/* ---- the lobby (131.6.2) ------------------------------------------------------- */
/* a received name: at most 8 bytes, control characters shown as '?' */
static void ovl_rname(char *d)
{
    int i;
    for (i = 0; i < BP_NAMELEN; i++) {
        d[i] = (char)bpl_r8();
        if (d[i] && (BPByte)d[i] < 32) d[i] = '?';
    }
    d[BP_NAMELEN] = 0;
}

/* what a lobby sends every two beats: the host its setup, the client its
 * ready flag and any name it proposes; both RESUME when resuming */
static void ovl_lobby_send(void)
{
    int s;
    if (bpl.intent) {
        bpl_begin(BPF_RESUME);
        bpl_w8(bp_mode);
        bpl_w8(bpl.rsok);
        bpl_w32(&bpl.rsm.match_id);
        bpl_w32(&bpl.rsm.ckpt_id);
        bpl_w32(&bpl.rsm.rev);
        bpl_w32(&bpl.rsm.digest);
        bpl_w32(&bpl.rsm.host_id);
        bpl_w32(&bpl.rsm.client_id);
        bpl_send();
        return;
    }
    if (bp_mode == BP_MODE_HOST) {
        bpl_begin(BPF_LOBBY1);
        bpl_w16(bpl.lseq);
        bpl_wn(&bpl.lob.nseats, 3);         /* nseats, rules, rounds */
        bpl_wn(bpl.lob.token, BP_SEATS);
        bpl_wn(bpl.lob.machine, BP_SEATS);
        bpl_w8(bpl.rmask);
        bpl_wn(bpl.lob.name[0], BP_NAMELEN);
        bpl_send();
        bpl_begin(BPF_LOBBY2);
        bpl_w16(bpl.lseq);
        for (s = 1; s < BP_SEATS; s++) bpl_wn(bpl.lob.name[s], BP_NAMELEN);
        bpl_send();
        return;
    }
    bpl_begin(BPF_READY);
    bpl_w16(bpl.lseq);
    bpl_w8(bpl.myready);
    bpl_send();
    for (s = 0; s < BP_SEATS; s++) {
        if (!(bpl.nameout & (1 << s))) continue;
        bpl_begin(BPF_NAME);
        bpl_w16(bpl.lseq);
        bpl_w8(s);
        bpl_w8(bpl.want.token[s]);
        bpl_wn(bpl.want.name[s], BP_NAMELEN);
        bpl_send();
    }
    if (bpl.nameout && ++bpl.nametries > BPL_TRIES) {
        bpl.nameout = 0;
        bpl_say("The banker kept the old name or token");
    }
}

/* client: LOBBY1 / LOBBY2. A setup is taken when both halves of ONE lobby
 * revision have arrived; a new revision clears this side's Ready */
static void ovl_lobby_in(void)
{
    BPWord seq;
    int s;
    if (bpl.ph != BPL_LOBBY || bpl.intent) goto bad;
    seq = bpl_r16();
    if (seq != bpl.linseq) {
        bpl.linseq = seq;
        bpl.lhave = 0;
    }
    if (bpl_rx[0] == BPF_LOBBY1) {
        bpl_rn(&bpl.lin.nseats, 3);
        bpl_rn(bpl.lin.token, BP_SEATS);
        bpl_rn(bpl.lin.machine, BP_SEATS);
        bpl.linmask = bpl_r8();
        ovl_rname(bpl.lin.name[0]);
        bpl.lhave |= 1;
    } else {
        for (s = 1; s < BP_SEATS; s++) ovl_rname(bpl.lin.name[s]);
        bpl.lhave |= 2;
    }
    if (bpl.lhave != 3) return;
    if (bpl.lin.nseats < 2 || bpl.lin.nseats > BP_SEATS) goto bad;
    for (s = 0; s < BP_SEATS; s++)
        if (bpl.lin.machine[s] > 1 || bpl.lin.token[s] > 3) goto bad;
    if (seq == bpl.lseq && bpl.rmask == bpl.linmask
        && !bpl_memdiff(&bpl.lob, &bpl.lin, sizeof(BPSetup))) return;
    os88_memcpy(&bpl.lob, &bpl.lin, sizeof(BPSetup));
    if (seq != bpl.lseq) bpl.myready = 0;
    bpl.lseq = seq;
    bpl.rmask = bpl.linmask;
    for (s = 0; s < BP_SEATS; s++)
        if (bpl.lob.token[s] == bpl.want.token[s]
            && !bpl_memdiff(bpl.lob.name[s], bpl.want.name[s], BP_NAMELEN))
            bpl.nameout &= (BPByte)~(1 << s);
    bp_ui_link();
    return;
bad:
    bp_diag.frame++;
}

/* host: a NAME (the client proposes its own seat) or READY, for the
 * current lobby revision only */
static void ovl_lobby_host(void)
{
    int s, t;
    if (bpl.ph != BPL_LOBBY || bpl.intent) goto bad;
    if (bpl_r16() != bpl.lseq) return;
    if (bpl_rx[0] == BPF_READY) {
        t = bpl.rmask;
        bpl.rmask &= 1;
        if (bpl_r8()) bpl.rmask |= 2;
        if (t == bpl.rmask) return;
    } else {
        s = bpl_r8();
        t = bpl_r8();
        if (s >= bpl.lob.nseats || bpl.lob.machine[s] != 1) goto bad;
        os88_memcpy(&bpl.lin, &bpl.lob, sizeof(BPSetup));
        bpl.lin.token[s] = (BPByte)t;
        ovl_rname(bpl.lin.name[s]);
        if (!bpl.lin.name[s][0] || bp_setup_check(&bpl.lin) != BP_OK) goto bad;
        for (t = 0; t < bpl.lob.nseats; t++)
            if (t != s && bpl.lob.token[t] == bpl.lin.token[s]) goto bad;
        if (!bpl_memdiff(&bpl.lin, &bpl.lob, sizeof(BPSetup))) return;
        os88_memcpy(&bpl.lob, &bpl.lin, sizeof(BPSetup));
        bpl.lseq++;
        bpl.rmask = 0;
    }
    ovl_lobby_send();
    bp_ui_link();
    return;
bad:
    bp_diag.frame++;
}

static int ovl_lobby_set(const BPSetup *su)
{
    int s, here;
    if (bp_mode == BP_MODE_LOCAL || !bpl.on || bpl.ph == BPL_MATCH) return BP_EPHASE;
    if (bp_mode == BP_MODE_CLIENT) {
        /* only this machine's seats' names and tokens, proposed */
        if (bpl.ph != BPL_LOBBY) return BP_EPHASE;
        for (s = 0; s < bpl.lob.nseats; s++) {
            if (bpl.lob.machine[s] != 1) continue;
            bpl.want.token[s] = su->token[s];
            os88_memset(bpl.want.name[s], 0, BP_NAMELEN + 1);
            os88_strcpy(bpl.want.name[s], su->name[s], BP_NAMELEN + 1);
            if (bpl.want.token[s] != bpl.lob.token[s]
                || bpl_memdiff(bpl.want.name[s], bpl.lob.name[s], BP_NAMELEN))
                bpl.nameout |= (BPByte)(1 << s);
        }
        bpl.nametries = 0;
    } else {
        if (su->nseats < 2 || su->nseats > BP_SEATS || bp_setup_check(su) != BP_OK) return BP_EARG;
        here = 0;
        for (s = 0; s < su->nseats; s++) {
            if (su->machine[s] > 1) return BP_EARG;
            here |= 1 << su->machine[s];
        }
        if (here != 3) return BP_EARG;      /* a seat on each machine */
        os88_memcpy(&bpl.lob, su, sizeof(BPSetup));
        bpl.lseq++;
        bpl.rmask = 0;
    }
    if (bpl.ph == BPL_LOBBY) ovl_lobby_send();
    bp_ui_link();
    return BP_OK;
}
int bp_lobby_set(const BPSetup *su) { return ovl_lobby_set(su); }

static int ovl_lobby_ready(int ready)
{
    if (bp_mode == BP_MODE_LOCAL || bpl.ph != BPL_LOBBY || bpl.intent) return BP_EPHASE;
    if (bp_mode == BP_MODE_HOST) bpl.rmask = (BPByte)((bpl.rmask & 2) | (ready != 0));
    else bpl.myready = (BPByte)(ready != 0);
    ovl_lobby_send();
    bp_ui_link();
    return BP_OK;
}
int bp_lobby_ready(int ready) { return ovl_lobby_ready(ready); }

static int ovl_peer_ready(void)
{
    if (bp_mode == BP_MODE_LOCAL || bpl.ph != BPL_LOBBY) return 0;
    return (bpl.rmask >> (1 - bp_here)) & 1;
}
int bp_lobby_peer_ready(void) { return ovl_peer_ready(); }

const BPSetup *bp_lobby_setup(void)
{
    if (bp_mode == BP_MODE_LOCAL) return 0;
    return &bpl.lob;
}

static void ovl_match_on(void)
{
    bpl.ph = BPL_MATCH;
    bpl.pend = 0;
    bpl.cpend = 0;
    bpl.hw = 0;
    bpl.hwcode = 0;
    bpl.cseq = 0;
    bpl.hseq = 0;
    bpl.tries = 0;
    bpl.heard = bpl.clk;
    bpl.msg = 0;
    bpl_redigest();
}

/* host: both ready on this revision - the match begins, as a transaction */
static int ovl_lobby_start(void)
{
    int e;
    if (bp_mode != BP_MODE_HOST || bpl.ph != BPL_LOBBY || bpl.intent) return BP_EPHASE;
    if (bpl.rmask != 3) return BP_EPENDING;
    e = bp_setup_check(&bpl.lob);
    if (e != BP_OK) return e;
    e = ovl_new_seeded(&bpl.lob);
    if (e != BP_OK) return e;
    ovl_match_on();
    bpl.pend = 2;
    bp_u32_copy(&bpl.rrev, &bp_game.rev);
    bp_u32_copy(&bpl.rdig, &bpl.dig);
    bpl.rorigin = 0xFF;
    bpl.due = bpl_after();
    bpl_send_result();
    bp_ui_committed(&bpl_res);
    bp_ui_link();
    return BP_OK;
}
int bp_lobby_start(void) { return ovl_lobby_start(); }

/* client: START - the same setup and seed through the same engine */
static void ovl_start_in(void)
{
    int e;
    if (bpl.ph == BPL_MATCH) {          /* a repeat: the ACK again */
        bpl_cur += 6;
        bpl_r32(&bpl_t1);
        if (bpl_eq(&bpl_t1, &bp_game.rev)) bpl_send_state(BPF_ACK);
        return;
    }
    if (bpl.ph != BPL_LOBBY || bpl.intent || bpl_r16() != bpl.lseq) { bp_diag.frame++; return; }
    bpl_r32(&bpl.seed);
    bpl_r32(&bpl_t1);
    bpl_r32(&bpl_t2);
    e = bp_new_match(&bp_game, &bpl.lob, &bpl.seed, &bpl_res);
    ovl_match_on();
    bpl_send_state(BPF_ACK);
    if (e != BP_OK || !bpl_eq(&bpl_t1, &bp_game.rev) || !bpl_eq(&bpl_t2, &bpl.dig)) bpl_diverge();
    if (e == BP_OK) bp_ui_committed(&bpl_res);
    bp_ui_link();
}

/* ---- chat (131.6.6) ------------------------------------------------------------ */
static void ovl_chat_tx(void)
{
    bpl_begin(BPF_CHAT);
    bpl_w8(bpl.cnum);
    bpl_wn(bpl.cout, os88_strlen(bpl.cout));
    bpl_send();
    bpl.cdue = bpl_after();
}

static int ovl_chat_send(const char *text)
{
    int i;
    if (bp_mode == BP_MODE_LOCAL || bpl.ph == BPL_UNBOUND) return BP_EPHASE;
    if (bpl.cbusy || (BPWord)(bpl.clk - bpl.clast) < BPL_PING) {
        bp_diag.chatdrop++;
        return BP_EPENDING;
    }
    for (i = 0; i < BP_CHAT_LEN && text[i]; i++)
        bpl.cout[i] = (char)((BPByte)text[i] < 32 ? '?' : text[i]);
    bpl.cout[i] = 0;
    if (!i) return BP_EARG;
    if (!++bpl.cnum) bpl.cnum = 1;
    bpl.cbusy = 1;
    bpl.ctries = 0;
    bpl.clast = bpl.clk;
    ovl_chat_tx();
    bp_ui_chat(bp_here, bpl.cout);
    return BP_OK;
}
int bp_chat_send(const char *text) { return ovl_chat_send(text); }

/* either: CHAT (acknowledge every copy, show a seq once) or CHATACK */
static void ovl_chat_in(void)
{
    BPByte n;
    n = bpl_r8();
    if (bpl_rx[0] == BPF_CHATACK) {
        if (n == bpl.cnum) bpl.cbusy = 0;
        return;
    }
    ovl_rname(bpl_chat);                    /* (the first 8, checked) */
    bpl_rn(bpl_chat + 8, BP_CHAT_LEN - 8);
    bpl_chat[BP_CHAT_LEN] = 0;
    bpl_begin(BPF_CHATACK);
    bpl_w8(n);
    bpl_send();
    if (!n || n == bpl.crx) return;
    bpl.crx = n;
    for (n = 8; n < BP_CHAT_LEN; n++)
        if (bpl_chat[n] && (BPByte)bpl_chat[n] < 32) bpl_chat[n] = '?';
    bp_ui_chat(1 - bp_here, bpl_chat);
}

/* ---- snapshot adoption (131.6.5) ----------------------------------------------- */
static void ovl_snapreq(void)
{
    bpl_begin(BPF_SNAPREQ);
    bpl_w32(&bp_game.match_id);
    bpl_w16(bpl.ssent);
    bpl_send();
    bpl.sdue = bpl_after();
}

static int ovl_adopt(void)
{
    if (bp_mode != BP_MODE_CLIENT || bpl.ph != BPL_MATCH || !bpl.diverged) return BP_EPHASE;
    if (bpl.snap) return BP_EPENDING;
    bpl.snap = 2;
    bpl.shead = 0;
    bpl.ssent = 0;
    bpl.stries = 0;
    bpl.msg = 0;
    ovl_snapreq();
    bp_ui_link();
    return BP_OK;
}
int bp_sess_adopt(void) { return ovl_adopt(); }

/* host: the next chunk, or (offset 0) SNAPHEAD first */
static void ovl_snapdata(void)
{
    BPWord n;
    if (!bpl.ssent) {
        bpl_begin(BPF_SNAPHEAD);
        bpl_w32(&bp_game.match_id);
        bpl_w32(&bpl.srev);
        bpl_w32(&bpl.sdig);
        bpl_w16(bpl.sn);
        bpl_send();
    }
    bpl_begin(BPF_SNAPDATA);
    bpl_w16(bpl.ssent);
    n = (BPWord)(bpl.sn - bpl.ssent);
    if (n > BPL_CHUNK) n = BPL_CHUNK;
    bpl_wn(bpl_snap + bpl.ssent, n);
    if (bpl_send() == BP_OK) {
        bpl.ssent = (BPWord)(bpl.ssent + n);
        bpl.sdue = bpl_after();
    }
}

/* host: SNAPREQ - start (offset 0), or the client's progress */
static void ovl_snapreq_in(void)
{
    BPWord off;
    bpl_r32(&bpl_t1);
    off = bpl_r16();
    if (bpl.ph != BPL_MATCH || !bpl_eq(&bpl_t1, &bp_game.match_id) || off > bpl.sn) goto bad;
    if (bpl.snap != 1 || !off) {
        bpl.sn = (BPWord)bp_encode(&bp_game, bpl_snap, BP_STATE_MAX);
        if (!bpl.sn) goto bad;
        bp_u32_copy(&bpl.srev, &bp_game.rev);
        bp_u32_copy(&bpl.sdig, &bpl.dig);
        bpl.snap = 1;
        bpl.sack = 0;
        bpl.ssent = 0;
        bpl.stries = 0;
        bp_diag.resync++;
        ovl_snapdata();
    } else if (off > bpl.sack) {
        bpl.sack = off;
        bpl.stries = 0;
        if (bpl.ssent < off) bpl.ssent = off;
        if (off >= bpl.sn) bpl.snap = 0;
    }
    bp_ui_link();
    return;
bad:
    bp_diag.frame++;
}

/* client: SNAPHEAD / SNAPDATA - in order, for this match only; adopted
 * only after it decodes, validates and has the head's digest */
static void ovl_snap_in(void)
{
    BPWord off, n;
    int e;
    if (bpl.snap != 2) goto bad;
    if (bpl_rx[0] == BPF_SNAPHEAD) {
        bpl_r32(&bpl_t1);
        bpl_r32(&bpl.srev);
        bpl_r32(&bpl.sdig);
        n = bpl_r16();
        if (!bpl_eq(&bpl_t1, &bp_game.match_id) || !n || n > BP_STATE_MAX) {
            bpl_say("That board is another match");
            goto bad;
        }
        bpl.sn = n;
        bpl.shead = 1;
        bpl.ssent = 0;
        return;
    }
    off = bpl_r16();
    if (!bpl.shead || off != bpl.ssent) {
        ovl_snapreq();                      /* where we are */
        goto bad;
    }
    n = (BPWord)(bpl.sn - off);
    if (n > BPL_CHUNK) n = BPL_CHUNK;
    bpl_rn(bpl_snap + off, n);
    bpl.ssent = (BPWord)(off + n);
    bpl.stries = 0;
    ovl_snapreq();
    bp_ui_link();
    if (bpl.ssent < bpl.sn) return;
    bpl.snap = 0;
    e = bp_decode(bpl_snap, bpl.sn, &bpl_scr);
    if (e == BP_OK) {
        bp_digest(&bpl_scr, &bpl_t2);
        if (!bpl_eq(&bpl_t2, &bpl.sdig) || !bpl_eq(&bpl_scr.rev, &bpl.srev)
            || !bpl_eq(&bpl_scr.match_id, &bp_game.match_id)) e = BP_EMATCH;
    }
    if (e != BP_OK) {
        bpl_say("The banker's board did not check");
        return;
    }
    os88_memcpy(&bp_game, &bpl_scr, sizeof(BPState));
    bp_u32_copy(&bpl.dig, &bpl_t2);
    bpl.diverged = 0;
    bpl.cpend = 0;
    bpl.tok = 0;
    bp_diag.resync++;
    bpl_send_state(BPF_ACK);
    ovl_adopted("Using the banker's board");
    return;
bad:
    bp_diag.frame++;
}

/* ---- the checkpoint exchange (131.7.5) -------------------------------------------- */
static void ovl_wckpt(const BPCkpt *m)
{
    bpl_w32(&m->match_id);
    bpl_w32(&m->ckpt_id);
    bpl_w32(&m->rev);
    bpl_w32(&m->digest);
    bpl_w32(&m->host_id);
    bpl_w32(&m->client_id);
}

static void ovl_ck_frame(int type)
{
    bpl_begin(type);
    if (type == BPF_CKBEGIN) ovl_wckpt(&bpl.ckm);
    else if (type == BPF_CKREQ) bpl_w32(&bp_game.match_id);
    else {
        bpl_w32(&bpl.ckm.ckpt_id);
        bpl_w8(bpl.ck == BPL_CK_DONE ? 2 : 1);
        bpl_w8(bpl.ckcode);
    }
    bpl_send();
    bpl.ckdue = bpl_after();
}

static void ovl_ck_end(int code, const char *why)
{
    bpl.ck = code && bp_mode == BP_MODE_CLIENT ? BPL_CK_DONE : 0;
    if (bp_mode == BP_MODE_HOST) bpl.ck = 0;
    bpl.ckwant = 0;
    bpl.cktries = 0;
    bpl.ckcode = (BPByte)code;
    bpl.pair = (BPByte)(code ? BP_PAIR_FAIL : BP_PAIR_DONE);
    bpl_say(why);
}

/* host: a checkpoint id above every one this match has on this disk -
 * the files rank slots by id (131.7.4), so ids only grow */
static void ovl_ck_newid(BPU32 *id)
{
    int w;
    bp_u32_set(id, 0, 0);
    for (w = 0; w < 3; w++)
        if (bp_ckpt_open(BP_MODE_HOST, w, &bpl_km, 0) == BP_OK && bpl_eq(&bpl_km.match_id, &bp_game.match_id)
            && (bpl_km.ckpt_id.hi > id->hi || (bpl_km.ckpt_id.hi == id->hi && bpl_km.ckpt_id.lo > id->lo)))
            bp_u32_copy(id, &bpl_km.ckpt_id);
    if (!++id->lo) id->hi++;
}

/* host: the exchange begins (nothing outstanding) */
static void ovl_ck_begin(void)
{
    int e;
    bpl.ckwant = 0;
    bpl.pair = BP_PAIR_BUSY;
    os88_memset(&bpl.ckm, 0, sizeof(BPCkpt));
    bp_u32_copy(&bpl.ckm.match_id, &bp_game.match_id);
    ovl_ck_newid(&bpl.ckm.ckpt_id);
    bp_u32_copy(&bpl.ckm.rev, &bp_game.rev);
    bp_u32_copy(&bpl.ckm.digest, &bpl.dig);
    bp_u32_copy(&bpl.ckm.host_id, &bpl.me);
    bp_u32_copy(&bpl.ckm.client_id, &bpl.peer);
    bpl.ckm.role = BP_MODE_HOST;
    bpl.ckm.state = 1;
    e = bp_ckpt_write(&bpl.ckm, &bp_game);
    if (e != BP_OK) { ovl_ck_end(e, "Save failed on this PC"); return; }
    bpl.ck = BPL_CK_WRITTEN;
    bpl.cktries = 0;
    ovl_ck_frame(BPF_CKBEGIN);
    bp_ui_link();
}

static int ovl_save_pair(void)
{
    if (bp_mode == BP_MODE_LOCAL) return BP_EPHASE;
    if (bp_link_state() != BP_L_PLAY) return BP_EPAUSED;
    if ((bpl.ck && bpl.ck != BPL_CK_DONE) || bpl.ckwant) return BP_EPENDING;
    bpl.pair = BP_PAIR_BUSY;
    bpl.msg = 0;
    bpl.cktries = 0;
    if (bp_mode == BP_MODE_CLIENT) {
        bpl.ck = BPL_CK_ASKED;
        ovl_ck_frame(BPF_CKREQ);
    } else if (bp_sess_busy()) bpl.ckwant = 1;      /* behind the transaction */
    else ovl_ck_begin();
    bp_ui_link();
    return BP_OK;
}
int bp_sess_save_pair(void) { return ovl_save_pair(); }

/* host: CKREQ / CKREPLY */
static void ovl_ck_host(void)
{
    int e, stage;
    bpl_r32(&bpl_t1);
    if (bpl_rx[0] == BPF_CKREQ) {
        if (!bpl_eq(&bpl_t1, &bp_game.match_id)) goto bad;
        if (bpl.ck) ovl_ck_frame(bpl.ck == BPL_CK_WRITTEN ? BPF_CKBEGIN : BPF_CKCOMMIT);
        else if (!bpl.ckwant) ovl_save_pair();
        return;
    }
    if (!bpl.ck || !bpl_eq(&bpl_t1, &bpl.ckm.ckpt_id)) goto bad;
    stage = bpl_r8();
    e = bpl_r8();
    if (bpl.ck == BPL_CK_WRITTEN && stage == 1) {
        if (e) { ovl_ck_end(e, "The other PC could not save"); return; }
        e = bp_ckpt_commit(&bpl.ckm);
        if (e) { ovl_ck_end(e, "Save failed on this PC"); return; }
        bpl.ck = BPL_CK_COMMIT;
        bpl.cktries = 0;
        ovl_ck_frame(BPF_CKCOMMIT);
    } else if (bpl.ck == BPL_CK_COMMIT && stage == 2)
        ovl_ck_end(e, e ? "Save incomplete: resume will finish it" : "Paired save complete");
    return;
bad:
    bp_diag.frame++;
}

/* client: CKBEGIN / CKCOMMIT; a repeat gets the same answer */
static void ovl_ck_client(void)
{
    int e;
    if (bpl_rx[0] == BPF_CKBEGIN) {
        os88_memset(&bpl_km, 0, sizeof(BPCkpt));
        bpl_r32(&bpl_km.match_id);
        bpl_r32(&bpl_km.ckpt_id);
        bpl_r32(&bpl_km.rev);
        bpl_r32(&bpl_km.digest);
        bpl_r32(&bpl_km.host_id);
        bpl_r32(&bpl_km.client_id);
        if (bpl.ck >= BPL_CK_CAND && bpl_eq(&bpl_km.ckpt_id, &bpl.ckm.ckpt_id)) {
            ovl_ck_frame(BPF_CKREPLY);
            return;
        }
        /* not during a snapshot. An INTENT of ours still out does NOT
         * hold it: the host starts a save only with nothing pending, so it
         * has not applied that intent (it refuses it BP_EPENDING until the
         * save ends) and CKBEGIN's revision is ours - waiting here for an
         * answer the host is waiting on us to give was a deadlock */
        if (bpl.snap) goto bad;
        os88_memcpy(&bpl.ckm, &bpl_km, sizeof(BPCkpt));
        bpl.ckm.role = BP_MODE_CLIENT;
        bpl.ckm.state = 1;
        e = BP_EMATCH;
        if (bpl_eq(&bpl_km.match_id, &bp_game.match_id) && bpl_eq(&bpl_km.rev, &bp_game.rev)
            && bpl_eq(&bpl_km.digest, &bpl.dig) && bpl_eq(&bpl_km.host_id, &bpl.peer)
            && bpl_eq(&bpl_km.client_id, &bpl.me)) e = bp_ckpt_write(&bpl.ckm, &bp_game);
        bpl.ckcode = (BPByte)e;
        bpl.ck = BPL_CK_CAND;
        bpl.pair = BP_PAIR_BUSY;
        ovl_ck_frame(BPF_CKREPLY);
        if (e) ovl_ck_end(e, "Save failed on this PC");
        else bp_ui_link();
        return;
    }
    bpl_r32(&bpl_t1);
    if (bpl.ck < BPL_CK_CAND || !bpl_eq(&bpl_t1, &bpl.ckm.ckpt_id)) goto bad;
    if (bpl.ck == BPL_CK_CAND) {
        e = bp_ckpt_commit(&bpl.ckm);
        bpl.ck = BPL_CK_DONE;
        bpl.ckcode = (BPByte)e;
        ovl_ck_frame(BPF_CKREPLY);
        ovl_ck_end(e, e ? "Save incomplete: resume will finish it" : "Paired save complete");
        bpl.ck = BPL_CK_DONE;
        return;
    }
    ovl_ck_frame(BPF_CKREPLY);
    return;
bad:
    bp_diag.frame++;
}

/* ---- resuming a paired save (131.7.5) ---------------------------------------------- */
static int ovl_resume_pair(void)
{
    int e;
    if (bp_mode == BP_MODE_LOCAL || !bpl.on || bpl.ph == BPL_MATCH) return BP_EPHASE;
    os88_memset(&bpl.rsm, 0, sizeof(BPCkpt));
    e = bp_ckpt_load(bp_mode, &bpl.rsm, &bpl_scr);
    if (e == BP_OK && (bpl.rsm.role != bp_mode || bpl.rsm.state != 2)) e = BP_EMATCH;
    bpl.intent = (BPByte)(e == BP_OK);
    bpl.pair = (BPByte)(e ? BP_PAIR_FAIL : BP_PAIR_BUSY);
    bpl.rsok = 0;
    bpl.rscommit = 0;
    bpl.bad = 0;
    ovl_unbind();
    bpl_say(e ? "No paired save for this side on this disk" : 0);
    return e;
}
int bp_sess_resume_pair(void) { return ovl_resume_pair(); }

static void ovl_rs_fail(const char *why)
{
    if (bpl.msg != why) {
        bpl.pair = BP_PAIR_FAIL;
        bpl_say(why);
    }
}

/* offer checkpoint id t1 instead, if this side has it: the client as a
 * committed checkpoint or its candidate, the host only as a committed one */
static int ovl_load_id(const BPU32 *id)
{
#ifdef BP_HAVE_CKPT_LOAD_ID
    return bp_ckpt_load_id(bp_mode, id, &bpl_km, &bpl_scr);
#else
    (void)id;                               /* (until the save lane lands) */
    return BP_EIO;
#endif
}
static int ovl_rs_switch(void)
{
    if (ovl_load_id(&bpl_t1) != BP_OK || !bpl_eq(&bpl_km.ckpt_id, &bpl_t1) || bpl_km.role != bp_mode
        || !bpl_eq(&bpl_km.match_id, &bpl.rsm.match_id)
        || (bpl_km.state != 2 && (bp_mode == BP_MODE_HOST || bpl_km.state != 1))) {
        /* not here: the offer we had stands, its state reloaded */
        ovl_load_id(&bpl.rsm.ckpt_id);
        return 0;
    }
    os88_memcpy(&bpl.rsm, &bpl_km, sizeof(BPCkpt));
    bpl.rscommit = (BPByte)(bpl_km.state == 1);
    return 1;
}

static void ovl_resume_in(void)
{
    BPByte role, ok;
    int e;
    role = bpl_r8();
    ok = bpl_r8();
    bpl_r32(&bpl_t2);                       /* match */
    bpl_r32(&bpl_t1);                       /* checkpoint */
    bpl_r32(&bpl_t3);                       /* revision */
    bpl_r32(&bpl_t4);                       /* digest */
    if (!bpl.intent) goto bad;
    if (bpl.ph == BPL_MATCH) {              /* adopted already: confirm it */
        if (bpl_eq(&bpl_t1, &bpl.rsm.ckpt_id)) ovl_lobby_send();
        return;
    }
    if (role == bp_mode) { ovl_rs_fail("Both PCs loaded the same side's save"); goto bad; }
    if (!bpl_eq(&bpl_t2, &bpl.rsm.match_id)) goto notpair;
    if (!bpl_eq(&bpl_t1, &bpl.rsm.ckpt_id)) {
        /* the host's offer leads: the client rolls forward to it; the host
         * falls back to the client's only when the client cannot (ok 2) */
        if (bp_mode == BP_MODE_CLIENT) {
            bpl.rsok = (BPByte)(ovl_rs_switch() ? 0 : 2);
            ovl_lobby_send();
        } else if (ok == 2) {
            if (!ovl_rs_switch()) goto notpair;
            ovl_lobby_send();
        }
        return;
    }
    /* the same checkpoint: the same pair of machines wrote it, at the same
     * revision with the same board */
    bpl_r32(&bpl_t2);
    if (!bpl_eq(&bpl_t2, &bpl.rsm.host_id)) goto notpair;
    bpl_r32(&bpl_t2);
    if (!bpl_eq(&bpl_t2, &bpl.rsm.client_id)) goto notpair;
    if (!bpl_eq(&bpl_t3, &bpl.rsm.rev) || !bpl_eq(&bpl_t4, &bpl.rsm.digest)) {
        ovl_rs_fail("Same save, different board");
        goto bad;
    }
    if (bpl.rscommit) {                     /* the roll forward */
        bpl.rsm.state = 2;
        e = bp_ckpt_commit(&bpl.rsm);
        if (e != BP_OK) { ovl_rs_fail("Could not finish the last save"); goto bad; }
        bpl.rscommit = 0;
    }
    os88_memcpy(&bp_game, &bpl_scr, sizeof(BPState));
    ovl_match_on();
    bpl.rsok = 1;
    bpl.pair = BP_PAIR_DONE;
    ovl_lobby_send();
    ovl_adopted("Resumed the paired save");
    return;
notpair:
    ovl_rs_fail("These saves are not a pair");
bad:
    bp_diag.frame++;
}

/* ---- one received frame ----------------------------------------------------------- */
static void bpl_frame(int n)
{
    int t;
    t = bpl_rx[0];
    if (t <= 0 || t >= BPF_N || n != bpl_lens[t] - 2 || !(bpl_dir[t] & (1 << bp_here))) goto bad;
    bp_diag.rx++;
    bpl_cur = bpl_rx + 1;
    if (t == BPF_HELLO) { ovl_hello(); return; }
    if (!bpl.peer.lo || bpl_r16() != bpl.peer.lo) goto bad;
    bpl.heard = bpl.clk;
    if (bpl.ph == BPL_UNBOUND) {            /* their frame says they bound us */
        bpl.ph = BPL_LOBBY;
        bpl.msg = 0;
        bp_ui_link();
    }
    switch (t) {
    case BPF_INTENT:   bpl_h_intent(); break;
    case BPF_ACK:      bpl_h_ack(); break;
    case BPF_TERMS:    bpl_c_terms(); break;
    case BPF_RESULT:   bpl_c_result(); break;
    case BPF_REFUSE:   bpl_c_refuse(); break;
    case BPF_PING:     bpl_ping(); break;
    case BPF_LOBBY1:
    case BPF_LOBBY2:   ovl_lobby_in(); break;
    case BPF_NAME:
    case BPF_READY:    ovl_lobby_host(); break;
    case BPF_START:    ovl_start_in(); break;
    case BPF_CHAT:
    case BPF_CHATACK:  ovl_chat_in(); break;
    case BPF_SNAPREQ:  ovl_snapreq_in(); break;
    case BPF_SNAPHEAD:
    case BPF_SNAPDATA: ovl_snap_in(); break;
    case BPF_RESUME:   ovl_resume_in(); break;
    case BPF_BYE:
        if (bpl.ph == BPL_MATCH) {
            bpl.left = 1;
            bpl_say("The other PC left the match");
        } else {
            ovl_unbind();
            bpl_say("The other PC left the lobby");
        }
        break;
    default:
        if (bpl.ph != BPL_MATCH) goto bad;
        if (bp_mode == BP_MODE_HOST) ovl_ck_host();
        else ovl_ck_client();
    }
    return;
bad:
    bp_diag.frame++;
}

/* ---- the per-tick service ---------------------------------------------------------- */
static void ovl_slow_timers(void)
{
    if (bpl.ck == BPL_CK_WRITTEN || bpl.ck == BPL_CK_COMMIT || bpl.ck == BPL_CK_ASKED) {
        if (bpl_due(bpl.ckdue)) {
            bpl_bump(&bpl.cktries);
            ovl_ck_frame(bpl.ck == BPL_CK_WRITTEN ? BPF_CKBEGIN
                         : bpl.ck == BPL_CK_COMMIT ? BPF_CKCOMMIT : BPF_CKREQ);
        }
    }
    if (bpl.ckwant && !bpl.pend && !bpl.snap && !bpl.diverged) ovl_ck_begin();
    if (bpl.snap == 1) {                    /* host: the window, or go back */
        if (bpl_due(bpl.sdue) && bpl.ssent > bpl.sack) {
            bpl_bump(&bpl.stries);
            bpl.ssent = bpl.sack;
        }
        if (bpl.ssent < bpl.sn && bpl.ssent < bpl.sack + BPL_WINDOW) ovl_snapdata();
    } else if (bpl.snap == 2 && bpl_due(bpl.sdue)) {
        bpl_bump(&bpl.stries);
        if (!bpl.shead) bpl.ssent = 0;
        ovl_snapreq();
    }
    if (bpl.cbusy && bpl_due(bpl.cdue)) {
        if (++bpl.ctries > BPL_TRIES) {
            bpl.cbusy = 0;
            bp_diag.chatdrop++;
        } else ovl_chat_tx();
    }
}

static void ovl_lobby_timers(void)
{
    if (bpl.ph == BPL_UNBOUND) {
        if ((BPWord)(bpl.clk - bpl.beat) >= BPL_BEAT) {
            bpl.beat = bpl.clk;
            ovl_send_hello();
        }
        return;
    }
    if (bpl.ph == BPL_LOBBY) {
        if ((BPWord)(bpl.clk - bpl.heard) >= BPL_QUIET) {
            ovl_unbind();                   /* a lobby partner went quiet */
            bpl_say("The other PC went quiet");
        } else if ((BPWord)(bpl.clk - bpl.beat) >= 2 * BPL_BEAT) {
            bpl.beat = bpl.clk;
            ovl_lobby_send();
        }
    }
}

static void bpl_timers(void)
{
    if (bpl.ph != BPL_MATCH) {
        ovl_lobby_timers();
        return;
    }
    /* the match: the outstanding transaction first */
    if (((bpl.pend && !bpl.diverged) || bpl.cpend) && bpl_due(bpl.due)) {
        bpl_bump(&bpl.tries);
        bpl.due = bpl_after();
        if (bpl.pend) bpl_send_result();
        else bpl_send_intent();
    }
    if (bpl.ck || bpl.ckwant || bpl.snap || bpl.cbusy) ovl_slow_timers();
    if ((BPWord)(bpl.clk - bpl.sent) >= BPL_PING) bpl_send_state(BPF_PING);
}

void bp_sess_tick(void)
{
    BPWord now, el;
    int i, n;
    if (bp_mode == BP_MODE_LOCAL || !bpl.on) return;
    now = (BPWord)os88_ticks();
    el = (BPWord)(now - bpl.last);
    bpl.last = now;
    if (el > 2) el = 2;                     /* our stall is not the peer's silence */
    bpl.clk = (BPWord)(bpl.clk + el);
    bp_ck_tick();
    for (i = 0; i < BPL_FRAMES; i++) {
        n = bp_ck_recv(bpl_rx);
        if (n <= 0) break;
        bpl_frame(n);
    }
    bpl_timers();
    n = bp_link_state();
    if (n != bpl.lst || (n == BP_L_SEARCH && bp_ck_msg() != bpl.ckmsg)) {
        bpl.lst = (BPByte)n;
        bpl.ckmsg = bp_ck_msg();
        bp_ui_link();
    }
}

/* ---- opening, joining, leaving ------------------------------------------------------- */
static int ovl_open(int mode, int port)
{
    int e;
    bpl_reset();
    bp_mode = (BPByte)mode;
    bp_here = (BPByte)(mode == BP_MODE_CLIENT);
    os88_memset(&bp_game, 0, sizeof(BPState));
    e = bp_ck_open(port);
    if (e != BP_OK) {
        bpl_say(bp_ck_msg());
        return e;
    }
    bpl.on = 1;
    bpl_rand32(&bpl.me);
    bpl.clk = 100;                          /* (the beat is due at once) */
    if (mode == BP_MODE_HOST) {
        bp_setup_default(&bpl.lob);
        bpl.lob.machine[1] = 1;             /* a seat on each machine */
        bpl.lseq = 1;
    }
    bp_ui_link();
    return BP_OK;
}
int bp_link_host(int port) { return ovl_open(BP_MODE_HOST, port); }
int bp_link_join(int port) { return ovl_open(BP_MODE_CLIENT, port); }

static void ovl_leave(void)
{
    if (bp_mode == BP_MODE_LOCAL || !bpl.on) return;
    if (bpl.peer.lo) {
        bpl_begin(BPF_BYE);
        bpl_send();
        bpl_send();
    }
    bpl_reset();                            /* (the port let go) */
    bp_mode = BP_MODE_LOCAL;                /* D3: the match ENDS - never a */
    bp_here = 0;                            /* local game made of it */
    bp_game.status = BP_ST_EMPTY;
    bpl_say("Left the cable match");
}
void bp_link_leave(void) { ovl_leave(); }

/* ---- the host harness's defaults end here (see the top of this file) ---- */
#if defined(BP_HOST) && !defined(BP_LINK_FAKE)
#undef bp_ck_open
#undef bp_ck_close
#undef bp_ck_tick
#undef bp_ck_heard
#undef bp_ck_send
#undef bp_ck_recv
#undef bp_ck_port
#undef bp_ck_msg
#undef bp_ck_rand
#undef bp_ui_committed
#undef bp_ui_link
#undef bp_ui_chat
#undef bp_ckpt_write
#undef bp_ckpt_commit
#undef bp_ckpt_load
#undef bp_ckpt_load_id
#undef bp_ckpt_open
#endif
