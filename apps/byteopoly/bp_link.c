/* Byteopoly - the session: local play, the banker host, the client, the
 * cable protocol and recovery (SPEC.md 131.5, 131.6, 131.7.5). Lane: link.
 *
 * OS-FREE: the UART is reached only through bp_ck_* (bp_cable.inc on the
 * guest, a fake pipe in hosttest/t_link.c) and time only through
 * os88_ticks(), so the host harness runs the whole protocol. Every byte of
 * session state is in ONE static record (bpl) plus bp_game/bp_mode/bp_here/
 * bp_diag and three buffers, which is what lets t_link.c run two endpoints
 * in one process by swapping them (BPL_STATE below names them all).
 *
 * Frequency split (SPEC.md 131.11): the per-tick service, the transaction
 * (INTENT/RESULT/ACK/PING) and submit stay resident; the handshake, lobby,
 * chat, snapshot, checkpoint and resume are ovl_* (UI task only, eager). */

/* ---- protocol constants (SPEC.md 131.5.2, 131.5.3) ----------------------- */
#define BPL_VER         1
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

/* link phases (bpl.ph) */
#define BPL_UNBOUND     0
#define BPL_LOBBY       1
#define BPL_MATCH       2
/* pending kinds (bpl.pend) */
#define BPL_P_RESULT    1
#define BPL_P_START     2
/* checkpoint stages (bpl.ck): host 1-2, client 10-12 */
#define BPL_CK_WRITTEN  1       /* host: candidate written, CKBEGIN out */
#define BPL_CK_COMMIT   2       /* host: committed, CKCOMMIT out */
#define BPL_CK_ASKED    10      /* client: CKREQ out */
#define BPL_CK_CAND     11      /* client: candidate written, waits COMMIT */
#define BPL_CK_DONE     12      /* client: committed (or failed), replies kept */

typedef struct {
    BPByte ph, intent, bad, left, diverged, on;     /* on: the port is ours */
    BPByte lst;                 /* bp_link_state at the last tick */
    const char *ckmsg;          /* bp_ck_msg() at the last tick */
    BPU32  me, peer;            /* endpoint ids (131.6.1); peer 0 unbound */
    BPWord clk, last, heard, sent, beat;
    const char *msg;            /* the last event's line, 0 none */
    BPByte msgcode;             /* "Banker: <code>" when msg is bpl_m_banker */
    /* lobby (131.6.2) */
    BPSetup lob;                /* host: the setup; client: the agreed copy */
    BPSetup lin;                /* client: halves being assembled */
    BPSetup want;               /* client: its proposed names/tokens */
    BPWord lseq, linseq;
    BPByte rmask, lhave, linmask, myready, nameout, nametries;
    BPU32  seed;
    /* the transaction (131.6.3) */
    BPByte pend, tries;
    BPWord due;
    BPCmd  rcmd;                /* host: the retained result's command */
    BPByte rorigin, rd1, rd2, rcard;
    BPWord rseq, hseq, hw;
    BPU32  rrev, rdig;
    BPByte hwcode;              /* host: the answer to seq hw: 0 RESULT, else REFUSE code */
    BPByte cpend;               /* client: an INTENT is out */
    BPWord cseq;
    BPCmd  ccmd;
    BPByte tok, torigin, tdisks, tpayer;
    BPWord tseq;
    BPU32  tgive, tget;
    BPU32  dig;                 /* bp_digest(&bp_game), kept current */
    BPByte pflags;              /* the peer's last PING flags */
    /* chat (131.6.6) */
    char   cout[BP_CHAT_LEN + 1];
    BPByte cbusy, cnum, ctries, crx;
    BPWord cdue, clast;
    /* snapshot (131.6.5) */
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
static BPResult bpl_res;
static BPCmd bpl_wc;                    /* a command being canonicalised */
static BPU32 bpl_t1, bpl_t2, bpl_t3, bpl_t4;
static char bpl_line[44];
static char bpl_chat[BP_CHAT_LEN + 1];

static const char bpl_magic[4] = { 'B', 'Y', 'T', 'O' };
static const char bpl_m_banker[] = "Banker: ";

/* ---- small helpers -------------------------------------------------------- */
static void bpl_put16(BPByte *p, BPWord v)
{
    p[0] = (BPByte)v;
    p[1] = (BPByte)(v >> 8);
}
static BPWord bpl_get16(const BPByte *p)
{
    return (BPWord)(p[0] | (p[1] << 8));
}
static void bpl_put32(BPByte *p, const BPU32 *v)
{
    bpl_put16(p, v->lo);
    bpl_put16(p + 2, v->hi);
}
static void bpl_get32(BPU32 *v, const BPByte *p)
{
    v->lo = bpl_get16(p);
    v->hi = bpl_get16(p + 2);
}
static int bpl_eq(const BPU32 *a, const BPU32 *b)
{
    return a->lo == b->lo && a->hi == b->hi;
}
static int bpl_zero(const BPU32 *a)
{
    return a->lo == 0 && a->hi == 0;
}
/* r = a + 1, wrapping (a revision; never reaches 2^32 in a game) */
static void bpl_inc(BPU32 *r, const BPU32 *a)
{
    r->lo = (BPWord)(a->lo + 1);
    r->hi = a->hi;
    if (r->lo == 0) r->hi = (BPWord)(r->hi + 1);
}
/* a < b as 32-bit unsigned */
static int bpl_lt(const BPU32 *a, const BPU32 *b)
{
    if (a->hi != b->hi) return a->hi < b->hi;
    return a->lo < b->lo;
}
static int bpl_due(BPWord t)
{
    return (BPWord)(bpl.clk - t) < 0x8000u;
}
static BPWord bpl_after(BPWord n)
{
    if (bpl.ph == BPL_MATCH && bp_link_state() == BP_L_PAUSED) n = (BPWord)(n + n);
    return (BPWord)(bpl.clk + n);
}
static int bpl_memdiff(const void *a, const void *b, unsigned n)
{
    const BPByte *x, *y;
    x = (const BPByte *)a;
    y = (const BPByte *)b;
    while (n--) if (*x++ != *y++) return 1;
    return 0;
}
static int bpl_namecmp(const char *a, const char *b)
{
    int i;
    for (i = 0; i < BP_NAMELEN; i++) {
        if (a[i] != b[i]) return 1;
        if (!a[i]) return 0;
    }
    return 0;
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

/* ---- sending -------------------------------------------------------------- */
/* a frame of this type: zeroed, the type and (but for HELLO) our tag */
static BPByte *bpl_begin(int type)
{
    os88_memset(bpl_tx, 0, BP_CK_MAX);
    bpl_tx[0] = (BPByte)type;
    if (type != BPF_HELLO) bpl_put16(bpl_tx + 1, bpl.me.lo);
    return bpl_tx;
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
static void bpl_send_ack(void)
{
    BPByte *p;
    p = bpl_begin(BPF_ACK);
    bpl_put32(p + 3, &bp_game.rev);
    bpl_put32(p + 7, &bpl.dig);
    bpl_send();
}
static void bpl_send_ping(void)
{
    BPByte *p, f;
    p = bpl_begin(BPF_PING);
    bpl_put32(p + 3, &bp_game.rev);
    bpl_put32(p + 7, &bpl.dig);
    f = 0;
    if (bp_sess_busy()) f |= 1;
    if (bp_link_state() == BP_L_PAUSED) f |= 2;
    if (bpl.diverged) f |= 4;
    p[11] = f;
    bpl_send();
}
static void bpl_send_refuse(BPWord seq, int code)
{
    BPByte *p;
    p = bpl_begin(BPF_REFUSE);
    bpl_put16(p + 3, seq);
    p[5] = (BPByte)code;
    bpl_put32(p + 6, &bp_game.rev);
    bpl_send();
}
/* host: the retained RESULT (TERMS first for a PROPOSE), or START */
static void bpl_send_result(void)
{
    BPByte *p;
    if (bpl.pend == BPL_P_START) {
        p = bpl_begin(BPF_START);
        bpl_put16(p + 3, bpl.lseq);
        bpl_put32(p + 5, &bpl.seed);
        bpl_put32(p + 9, &bpl.rrev);
        bpl_put32(p + 13, &bpl.rdig);
        bpl_send();
        return;
    }
    if (bpl.rcmd.type == BP_C_PROPOSE) {
        p = bpl_begin(BPF_TERMS);
        p[3] = bpl.rorigin;
        bpl_put16(p + 4, bpl.rseq);
        bpl_put32(p + 6, &bpl.rcmd.give);
        bpl_put32(p + 10, &bpl.rcmd.get);
        p[14] = bpl.rcmd.disks;
        p[15] = bpl.rcmd.payer;
        bpl_send();
    }
    p = bpl_begin(BPF_RESULT);
    p[3] = bpl.rorigin;
    bpl_put16(p + 4, bpl.rseq);
    bpl_put32(p + 6, &bpl.rrev);
    bpl_put32(p + 10, &bpl.rdig);
    p[14] = bpl.rd1;
    p[15] = bpl.rd2;
    p[16] = bpl.rcard;
    p[17] = bpl.rcmd.type;
    p[18] = bpl.rcmd.seat;
    p[19] = bpl.rcmd.arg;
    p[20] = bpl.rcmd.to;
    bpl_put32(p + 21, &bpl.rcmd.amount);
    bpl_send();
}
static void bpl_send_intent(void)
{
    BPByte *p;
    p = bpl_begin(BPF_INTENT);
    bpl_put16(p + 3, bpl.cseq);
    p[5] = bpl.ccmd.type;
    p[6] = bpl.ccmd.seat;
    p[7] = bpl.ccmd.arg;
    p[8] = bpl.ccmd.to;
    bpl_put32(p + 9, &bpl.ccmd.rev);
    bpl_put32(p + 13, &bpl.ccmd.amount);
    bpl_put32(p + 17, &bpl.ccmd.give);
    bpl_put32(p + 21, &bpl.ccmd.get);
    p[25] = bpl.ccmd.disks;
    p[26] = bpl.ccmd.payer;
    bpl_send();
}

/* a command, canonicalised (131.5.2): the trade fields only for PROPOSE */
static void bpl_canon(BPCmd *d, const BPCmd *c)
{
    if (d != c) os88_memcpy(d, c, sizeof(BPCmd));
    if (d->type != BP_C_PROPOSE) {
        bp_u32_set(&d->give, 0, 0);
        bp_u32_set(&d->get, 0, 0);
        d->disks = 0;
        d->payer = 0;
    }
}

/* ---- the state the UI sees ------------------------------------------------ */
int bp_link_state(void)
{
    if (bp_mode == BP_MODE_LOCAL || !bpl.on) return BP_L_OFF;
    if (bpl.ph == BPL_UNBOUND) {
        if (bpl.bad) return BP_L_BADPEER;
        return bpl_zero(&bpl.peer) ? BP_L_SEARCH : BP_L_PEER;
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
        return bpl.pend || bpl.ckwant || bpl.ck == BPL_CK_WRITTEN || bpl.ck == BPL_CK_COMMIT;
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

const char *bp_link_msg(void)
{
    int s;
    if (bp_mode == BP_MODE_LOCAL) return "Same PC";
    s = bp_link_state();
    if (bpl.msg && s != BP_L_SEARCH && s != BP_L_RESYNC) {
        if (bpl.msg != bpl_m_banker) return bpl.msg;
        os88_strcpy(bpl_line, bpl_m_banker, sizeof bpl_line);
        os88_strcpy(bpl_line + 8, bp_err_text(bpl.msgcode), sizeof bpl_line - 8);
        return bpl_line;
    }
    switch (s) {
    case BP_L_OFF:      return "Not linked";
    case BP_L_SEARCH:   return bp_ck_msg();
    case BP_L_PEER:     return "Other PC found: agreeing";
    case BP_L_BADPEER:  return "The other PC cannot play this match";
    case BP_L_LOBBY:    return bpl.intent ? "Linked: matching saves" : "Linked: lobby";
    case BP_L_PAUSED:   return "Link paused: waiting for the other PC";
    case BP_L_DIVERGED:
        return bp_mode == BP_MODE_CLIENT ? "Boards differ: use the banker's board?"
                                         : "Boards differ: the other PC must resync";
    case BP_L_RESYNC:
        os88_strcpy(bpl_line, bp_mode == BP_MODE_HOST ? "Sending the board " : "Fetching the board ",
                    sizeof bpl_line);
        os88_utoa(bp_mode == BP_MODE_HOST ? bpl.sack : bpl.ssent,
                  bpl_line + os88_strlen(bpl_line));
        return bpl_line;
    }
    /* PLAY: role, port, baud, the synchronised revision, waiting */
    os88_strcpy(bpl_line, bp_mode == BP_MODE_HOST ? "Banker COM" : "Linked COM", sizeof bpl_line);
    s = (int)os88_strlen(bpl_line);
    bpl_line[s] = (char)('0' + bp_ck_port());
    os88_strcpy(bpl_line + s + 1, " 2400 rev ", sizeof bpl_line - (unsigned)s - 1);
    os88_utoa(bp_game.rev.lo, bpl_line + os88_strlen(bpl_line));
    if (bp_sess_busy())
        os88_strcpy(bpl_line + os88_strlen(bpl_line), " waiting", 9);
    return bpl_line;
}

/* ---- local mode, and the start of every mode ----------------------------- */
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

static void bpl_seed(void)
{
    bpl_rand32(&bpl.seed);
}

int bp_sess_new(const BPSetup *su)
{
    int e;
    if (bp_mode == BP_MODE_HOST) return bp_lobby_start();
    if (bp_mode != BP_MODE_LOCAL) return BP_EPHASE;
    bpl_seed();
    e = bp_new_match(&bp_game, su, &bpl.seed, &bpl_res);
    if (e == BP_OK) bp_ui_committed(&bpl_res);
    return e;
}

int bp_sess_load_local(const BPState *loaded)
{
    int e;
    if (bp_mode != BP_MODE_LOCAL) return BP_EPHASE;
    e = bp_validate(loaded);
    if (e != BP_OK) return e;
    os88_memcpy(&bp_game, loaded, sizeof(BPState));
    os88_memset(&bpl_res, 0, sizeof(BPResult));
    bp_ui_committed(&bpl_res);
    return BP_OK;
}

/* ---- the transaction (131.6.3) -------------------------------------------- */
/* host: wcmd (canonical) applied once and retained; BP_OK or the refusal */
static int bpl_commit(BPByte origin, BPWord seq)
{
    int e;
    e = bp_apply(&bp_game, &bpl_wc, &bpl_res);
    if (e != BP_OK) return e;
    bpl_redigest();
    os88_memcpy(&bpl.rcmd, &bpl_wc, sizeof(BPCmd));
    bpl.rorigin = origin;
    bpl.rseq = seq;
    bp_u32_copy(&bpl.rrev, &bp_game.rev);
    bp_u32_copy(&bpl.rdig, &bpl.dig);
    bpl.rd1 = bpl_res.d1;
    bpl.rd2 = bpl_res.d2;
    bpl.rcard = bpl_res.card;
    bpl.pend = BPL_P_RESULT;
    bpl.tries = 0;
    bpl.due = bpl_after(BPL_RETRY);
    bpl.msg = 0;
    bpl_send_result();
    bp_ui_committed(&bpl_res);
    return BP_OK;
}

static int bpl_gate(const BPCmd *c)
{
    int s;
    s = bp_link_state();
    if (s == BP_L_DIVERGED || s == BP_L_RESYNC) return BP_EPENDING;
    if (s != BP_L_PLAY) return BP_EPAUSED;
    if (c->seat >= BP_SEATS || bp_game.machine[c->seat] != bp_here) return BP_ENOTMINE;
    if (bp_sess_busy()) return BP_EPENDING;
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
    e = bpl_gate(c);
    if (e != BP_OK) return e;
    bpl_canon(&bpl_wc, c);
    if (bp_mode == BP_MODE_HOST) {
        bpl.hseq = (BPWord)(bpl.hseq + 1);
        return bpl_commit(0, bpl.hseq);
    }
    if (bpl.cseq == 0xFFFFu) {
        bpl.msg = "Save and resume the match";
        return BP_EOVERFLOW;
    }
    e = bp_check(&bp_game, &bpl_wc);
    if (e != BP_OK) return e;
    os88_memcpy(&bpl.ccmd, &bpl_wc, sizeof(BPCmd));
    bpl.cseq = (BPWord)(bpl.cseq + 1);
    bpl.cpend = 1;
    bpl.tries = 0;
    bpl.due = bpl_after(BPL_RETRY);
    bpl.msg = 0;
    bpl_send_intent();
    return BP_EPENDING;
}

/* host: an INTENT */
static void bpl_h_intent(void)
{
    const BPByte *p;
    BPWord seq;
    int e;
    p = bpl_rx;
    seq = bpl_get16(p + 3);
    if (bpl.ph != BPL_MATCH || seq == 0) { bp_diag.frame++; return; }
    if (seq <= bpl.hw) {
        /* a duplicate: the retained answer for exactly that seq, if any */
        if (seq == bpl.hw) {
            if (bpl.hwcode) bpl_send_refuse(seq, bpl.hwcode);
            else if (bpl.rorigin == 1 && bpl.rseq == seq) bpl_send_result();
            else bp_diag.frame++;
        } else bp_diag.frame++;
        return;
    }
    if (bp_sess_busy()) { bpl_send_refuse(seq, BP_EPENDING); return; }
    bpl_wc.type = p[5];
    bpl_wc.seat = p[6];
    bpl_wc.arg = p[7];
    bpl_wc.to = p[8];
    bpl_get32(&bpl_wc.rev, p + 9);
    bpl_get32(&bpl_wc.amount, p + 13);
    bpl_get32(&bpl_wc.give, p + 17);
    bpl_get32(&bpl_wc.get, p + 21);
    bpl_wc.disks = p[25];
    bpl_wc.payer = p[26];
    bpl_canon(&bpl_wc, &bpl_wc);
    bpl.hw = seq;
    if (bpl_wc.seat >= BP_SEATS || bp_game.machine[bpl_wc.seat] != 1) e = BP_ENOTMINE;
    else e = bpl_commit(1, seq);
    bpl.hwcode = (BPByte)e;
    if (e != BP_OK) bpl_send_refuse(seq, e);
}

/* host: an ACK */
static void bpl_h_ack(void)
{
    bpl_get32(&bpl_t1, bpl_rx + 3);
    bpl_get32(&bpl_t2, bpl_rx + 7);
    if (bpl.pend && bpl_eq(&bpl_t1, &bpl.rrev)) {
        if (bpl_eq(&bpl_t2, &bpl.rdig)) {
            bpl.pend = 0;
            bpl.tries = 0;
            bpl.diverged = 0;
            bpl.snap = 0;
            bpl.msg = 0;
            bp_ui_link();
        } else if (!bpl.diverged) {
            bpl.diverged = 1;
            bp_ui_link();
        }
        return;
    }
    if (bpl_eq(&bpl_t1, &bp_game.rev) && bpl_eq(&bpl_t2, &bpl.dig)) {
        if (bpl.diverged || bpl.snap) {     /* an adopted snapshot */
            bpl.diverged = 0;
            bpl.snap = 0;
            bp_ui_link();
        }
    }
}

static void bpl_diverge(void)
{
    if (bpl.diverged) return;
    bpl.diverged = 1;
    bpl.msg = 0;
    bpl_send_ping();
    bp_ui_link();
}

/* client: a RESULT (TERMS for a PROPOSE arrived just before it) */
static void bpl_c_result(void)
{
    const BPByte *p;
    BPByte origin;
    BPWord seq;
    int e;
    p = bpl_rx;
    if (bpl.ph != BPL_MATCH) { bp_diag.frame++; return; }
    origin = p[3];
    seq = bpl_get16(p + 4);
    bpl_get32(&bpl_t1, p + 6);              /* the new revision */
    bpl_get32(&bpl_t2, p + 10);             /* the host's digest */
    if (bpl_eq(&bpl_t1, &bp_game.rev)) {    /* already applied: the ACK again */
        if (origin == 1 && seq == bpl.cseq) bpl.cpend = 0;
        bpl_send_ack();
        return;
    }
    bpl_inc(&bpl_t3, &bp_game.rev);
    if (!bpl_eq(&bpl_t1, &bpl_t3)) {
        if (bpl_lt(&bpl_t1, &bp_game.rev)) bp_diag.frame++;    /* stale */
        else bpl_diverge();                                     /* skipped */
        return;
    }
    if (bpl.diverged) { bp_diag.frame++; return; }
    os88_memset(&bpl_wc, 0, sizeof(BPCmd));
    bpl_wc.type = p[17];
    bpl_wc.seat = p[18];
    bpl_wc.arg = p[19];
    bpl_wc.to = p[20];
    bpl_get32(&bpl_wc.amount, p + 21);
    bp_u32_copy(&bpl_wc.rev, &bp_game.rev);
    if (bpl_wc.type == BP_C_PROPOSE) {
        if (!bpl.tok || bpl.torigin != origin || bpl.tseq != seq) { bp_diag.frame++; return; }
        bp_u32_copy(&bpl_wc.give, &bpl.tgive);
        bp_u32_copy(&bpl_wc.get, &bpl.tget);
        bpl_wc.disks = bpl.tdisks;
        bpl_wc.payer = bpl.tpayer;
    }
    bpl.tok = 0;
    e = bp_apply(&bp_game, &bpl_wc, &bpl_res);
    if (e == BP_OK) bpl_redigest();
    if (origin == 1 && seq == bpl.cseq) bpl.cpend = 0;
    if (e != BP_OK || bpl_res.d1 != p[14] || bpl_res.d2 != p[15] || bpl_res.card != p[16]
        || !bpl_eq(&bpl_t2, &bpl.dig)) {
        bpl_send_ack();
        bpl_diverge();
        if (e == BP_OK) bp_ui_committed(&bpl_res);
        return;
    }
    bpl.msg = 0;
    bpl_send_ack();
    bp_ui_committed(&bpl_res);
}

/* client: TERMS, kept for the RESULT that follows */
static void bpl_c_terms(void)
{
    const BPByte *p;
    p = bpl_rx;
    bpl.tok = 1;
    bpl.torigin = p[3];
    bpl.tseq = bpl_get16(p + 4);
    bpl_get32(&bpl.tgive, p + 6);
    bpl_get32(&bpl.tget, p + 10);
    bpl.tdisks = p[14];
    bpl.tpayer = p[15];
}

/* client: a REFUSE */
static void bpl_c_refuse(void)
{
    BPWord seq;
    int code;
    seq = bpl_get16(bpl_rx + 3);
    code = bpl_rx[5];
    if (!bpl.cpend || seq != bpl.cseq) { bp_diag.frame++; return; }
    if (code == BP_EPENDING) {          /* the banker was busy: try again */
        bpl.due = bpl_after(BPL_RETRY);
        return;
    }
    bpl.cpend = 0;
    bpl.tries = 0;
    bpl.msg = bpl_m_banker;
    bpl.msgcode = (BPByte)code;
    bp_ui_link();
}

/* either: a PING - liveness, and the state hash at equal revisions */
static void bpl_ping(void)
{
    bpl.pflags = bpl_rx[11];
    if (bpl.ph != BPL_MATCH) return;
    bpl_get32(&bpl_t1, bpl_rx + 3);
    bpl_get32(&bpl_t2, bpl_rx + 7);
    if (bp_mode == BP_MODE_HOST) {
        if (bpl.pflags & 4) {
            if (!bpl.diverged) { bpl.diverged = 1; bp_ui_link(); }
        } else if (bpl.diverged && !bpl.pend && bpl_eq(&bpl_t1, &bp_game.rev)
                   && bpl_eq(&bpl_t2, &bpl.dig)) {
            bpl.diverged = 0;
            bp_ui_link();
        }
    }
    if (bpl_eq(&bpl_t1, &bp_game.rev) && !bpl_eq(&bpl_t2, &bpl.dig)) {
        if (bp_mode == BP_MODE_CLIENT) bpl_diverge();
        else if (!bpl.diverged) { bpl.diverged = 1; bp_ui_link(); }
    }
}

/* ---- the handshake (131.6.1) ----------------------------------------------- */
static void bpl_send_hello(void)
{
    BPByte *p;
    p = bpl_begin(BPF_HELLO);
    os88_memcpy(p + 1, bpl_magic, 4);
    p[5] = BPL_VER;
    p[6] = bp_mode;
    bpl_put32(p + 7, &bpl.me);
    bp_fingerprint(&bpl_t4);
    bpl_put32(p + 11, &bpl_t4);
    bpl_put32(p + 15, &bpl.peer);
    p[19] = bpl.intent;
    if (bpl.intent) bpl_put32(p + 20, &bpl.rsm.match_id);
    bpl_send();
}

static void bpl_unbind(void)
{
    bpl.ph = BPL_UNBOUND;
    bp_u32_set(&bpl.peer, 0, 0);
    bpl.rmask = 0;
    bpl.myready = 0;
    bpl.lhave = 0;
    bpl.rsok = 0;
}

static void bpl_bad(const char *why)
{
    bp_diag.frame++;
    if (bpl.ph != BPL_UNBOUND) return;
    if (!bpl.bad || bpl.msg != why) {
        bpl.bad = 1;
        bpl_say(why);
    }
}

static void bpl_lobby_send(void);

static void ovl_hello(void)
{
    const BPByte *p;
    BPByte role, intent;
    p = bpl_rx;
    if (p[1] != 'B' || p[2] != 'Y' || p[3] != 'T' || p[4] != 'O') { bp_diag.frame++; return; }
    bp_ck_heard();
    role = p[6];
    intent = p[19];
    bpl_get32(&bpl_t1, p + 7);              /* their id */
    bpl_get32(&bpl_t2, p + 11);             /* their fingerprint */
    bpl_get32(&bpl_t3, p + 15);             /* the id they have bound */
    if (bpl.ph == BPL_MATCH) {
        if (bpl_eq(&bpl_t1, &bpl.peer)) {   /* our partner, still agreeing */
            bpl.heard = bpl.clk;
            bpl_send_hello();
        } else {
            bp_diag.frame++;
            bpl_say("Another Byteopoly answered: not this match's partner");
        }
        return;
    }
    bp_fingerprint(&bpl_t4);
    if (p[5] != BPL_VER || !bpl_eq(&bpl_t2, &bpl_t4)) { bpl_bad("Different Byteopoly version"); return; }
    if (role == bp_mode) {
        bpl_bad(bp_mode == BP_MODE_HOST ? "The other PC chose Host too" : "The other PC chose Join too");
        return;
    }
    if (role != BP_MODE_HOST && role != BP_MODE_CLIENT) { bp_diag.frame++; return; }
    if (intent != bpl.intent) {
        bpl_bad(intent ? "The other PC is resuming a saved match"
                       : "The other PC is starting a new match");
        return;
    }
    if (intent) {
        bpl_get32(&bpl_t4, p + 20);
        if (!bpl_eq(&bpl_t4, &bpl.rsm.match_id)) { bpl_bad("The other PC's save is another match"); return; }
    }
    if (bpl_zero(&bpl_t1) || bpl_t1.lo == 0) { bp_diag.frame++; return; }
    if (bpl.bad) { bpl.bad = 0; bpl.msg = 0; }
    if (!bpl_eq(&bpl_t1, &bpl.peer)) {      /* a new partner (or a rebind) */
        bpl_unbind();
        bp_u32_copy(&bpl.peer, &bpl_t1);
        if (bp_mode == BP_MODE_HOST) bpl.lseq = (BPWord)(bpl.lseq + 1);
        bp_ui_link();
    }
    bpl.heard = bpl.clk;
    if (bpl_eq(&bpl_t3, &bpl.me)) {
        if (bpl.ph == BPL_UNBOUND) {
            bpl.ph = BPL_LOBBY;
            bpl.msg = 0;
            bpl.beat = bpl.clk;
            bp_ui_link();
        }
    }
    bpl_send_hello();
    if (bpl.ph == BPL_LOBBY) bpl_lobby_send();
}

/* ---- the lobby (131.6.2) ----------------------------------------------------- */
static void bpl_name_put(BPByte *d, const char *s)
{
    int i;
    for (i = 0; i < BP_NAMELEN && s[i]; i++) d[i] = (BPByte)s[i];
}
static void bpl_name_get(char *d, const BPByte *s)
{
    int i;
    for (i = 0; i < BP_NAMELEN; i++) {
        d[i] = (char)s[i];
        if (s[i] && s[i] < 32) d[i] = '?';
    }
    d[BP_NAMELEN] = 0;
}

/* what the lobby sends every two beats: the host its setup, the client its
 * ready flag and any name it is proposing; both RESUME when resuming */
static void ovl_lobby_send(void)
{
    BPByte *p;
    int s;
    if (bpl.intent) {
        if (bpl.ph != BPL_UNBOUND) {
            p = bpl_begin(BPF_RESUME);
            p[3] = bp_mode;
            p[4] = bpl.rsok;
            bpl_put32(p + 5, &bpl.rsm.match_id);
            bpl_put32(p + 9, &bpl.rsm.ckpt_id);
            bpl_put32(p + 13, &bpl.rsm.rev);
            bpl_put32(p + 17, &bpl.rsm.digest);
            bpl_put32(p + 21, &bpl.rsm.host_id);
            bpl_put32(p + 25, &bpl.rsm.client_id);
            bpl_send();
        }
        return;
    }
    if (bp_mode == BP_MODE_HOST) {
        p = bpl_begin(BPF_LOBBY1);
        bpl_put16(p + 3, bpl.lseq);
        p[5] = bpl.lob.nseats;
        p[6] = bpl.lob.rules;
        p[7] = bpl.lob.rounds;
        for (s = 0; s < BP_SEATS; s++) {
            p[8 + s] = bpl.lob.token[s];
            p[12 + s] = bpl.lob.machine[s];
        }
        p[16] = bpl.rmask;
        bpl_name_put(p + 17, bpl.lob.name[0]);
        bpl_send();
        p = bpl_begin(BPF_LOBBY2);
        bpl_put16(p + 3, bpl.lseq);
        for (s = 1; s < BP_SEATS; s++) bpl_name_put(p + 5 + (s - 1) * BP_NAMELEN, bpl.lob.name[s]);
        bpl_send();
        return;
    }
    p = bpl_begin(BPF_READY);
    bpl_put16(p + 3, bpl.lseq);
    p[5] = bpl.myready;
    bpl_send();
    for (s = 0; s < BP_SEATS; s++) {
        if (!(bpl.nameout & (1 << s))) continue;
        p = bpl_begin(BPF_NAME);
        bpl_put16(p + 3, bpl.lseq);
        p[5] = (BPByte)s;
        p[6] = bpl.want.token[s];
        bpl_name_put(p + 7, bpl.want.name[s]);
        bpl_send();
    }
    if (bpl.nameout && ++bpl.nametries > BPL_TRIES) {
        bpl.nameout = 0;
        bpl_say("The banker kept the old name or token");
    }
}
static void bpl_lobby_send(void)
{
    ovl_lobby_send();
}

/* client: LOBBY1 / LOBBY2 - a setup is taken when both halves of one
 * lobby revision have arrived */
static void ovl_lobby_in(void)
{
    const BPByte *p;
    BPWord seq;
    int s, changed;
    p = bpl_rx;
    if (bp_mode != BP_MODE_CLIENT || bpl.ph != BPL_LOBBY || bpl.intent) { bp_diag.frame++; return; }
    seq = bpl_get16(p + 3);
    if (seq != bpl.linseq || !bpl.lhave) {
        bpl.linseq = seq;
        bpl.lhave = 0;
    }
    if (p[0] == BPF_LOBBY1) {
        bpl.lin.nseats = p[5];
        bpl.lin.rules = p[6];
        bpl.lin.rounds = p[7];
        for (s = 0; s < BP_SEATS; s++) {
            bpl.lin.token[s] = p[8 + s];
            bpl.lin.machine[s] = p[12 + s];
        }
        bpl.linmask = p[16];
        bpl_name_get(bpl.lin.name[0], p + 17);
        bpl.lhave |= 1;
    } else {
        for (s = 1; s < BP_SEATS; s++) bpl_name_get(bpl.lin.name[s], p + 5 + (s - 1) * BP_NAMELEN);
        bpl.lhave |= 2;
    }
    if (bpl.lhave != 3) return;
    if (bpl.lin.nseats < 2 || bpl.lin.nseats > BP_SEATS) { bp_diag.frame++; return; }
    for (s = 0; s < BP_SEATS; s++)
        if (bpl.lin.machine[s] > 1) { bp_diag.frame++; return; }
    changed = seq != bpl.lseq || bpl_memdiff(&bpl.lob, &bpl.lin, sizeof(BPSetup));
    os88_memcpy(&bpl.lob, &bpl.lin, sizeof(BPSetup));
    if (seq != bpl.lseq) bpl.myready = 0;
    bpl.lseq = seq;
    bpl.rmask = bpl.linmask;
    for (s = 0; s < BP_SEATS; s++) {
        if ((bpl.nameout & (1 << s)) && bpl.lob.token[s] == bpl.want.token[s]
            && !bpl_namecmp(bpl.lob.name[s], bpl.want.name[s]))
            bpl.nameout &= (BPByte)~(1 << s);
    }
    if (changed) bp_ui_link();
}

/* host: a NAME (the client proposes its own seat) or READY */
static void ovl_lobby_host(void)
{
    const BPByte *p;
    int s, t;
    p = bpl_rx;
    if (bp_mode != BP_MODE_HOST || bpl.ph != BPL_LOBBY || bpl.intent) { bp_diag.frame++; return; }
    if (bpl_get16(p + 3) != bpl.lseq) return;           /* an old revision: ignored */
    if (p[0] == BPF_READY) {
        t = bpl.rmask;
        if (p[5]) bpl.rmask |= 2;
        else bpl.rmask &= (BPByte)~2;
        if (t != bpl.rmask) { bpl_lobby_send(); bp_ui_link(); }
        return;
    }
    s = p[5];
    if (s >= bpl.lob.nseats || bpl.lob.machine[s] != 1 || p[6] >= 4) { bp_diag.frame++; return; }
    for (t = 0; t < bpl.lob.nseats; t++)
        if (t != s && bpl.lob.token[t] == p[6]) { bp_diag.frame++; return; }
    os88_memcpy(&bpl.lin, &bpl.lob, sizeof(BPSetup));
    bpl.lin.token[s] = p[6];
    bpl_name_get(bpl.lin.name[s], p + 7);
    if (!bpl.lin.name[s][0] || bp_setup_check(&bpl.lin) != BP_OK) { bp_diag.frame++; return; }
    if (!bpl_memdiff(&bpl.lin, &bpl.lob, sizeof(BPSetup))) return;
    os88_memcpy(&bpl.lob, &bpl.lin, sizeof(BPSetup));
    bpl.lseq = (BPWord)(bpl.lseq + 1);
    bpl.rmask = 0;
    bpl_lobby_send();
    bp_ui_link();
}

static int ovl_lobby_set(const BPSetup *su)
{
    int s, here;
    if (bp_mode == BP_MODE_LOCAL || !bpl.on || bpl.ph == BPL_MATCH) return BP_EPHASE;
    if (bp_mode == BP_MODE_CLIENT) {
        /* only this machine's seats' names and tokens, proposed to the host */
        if (bpl.ph != BPL_LOBBY) return BP_EPHASE;
        for (s = 0; s < bpl.lob.nseats; s++) {
            if (bpl.lob.machine[s] != 1) continue;
            if (su->token[s] != bpl.lob.token[s] || bpl_namecmp(su->name[s], bpl.lob.name[s])) {
                bpl.want.token[s] = su->token[s];
                os88_strcpy(bpl.want.name[s], su->name[s], BP_NAMELEN + 1);
                bpl.nameout |= (BPByte)(1 << s);
            }
        }
        bpl.nametries = 0;
        bpl_lobby_send();
        return BP_OK;
    }
    if (su->nseats < 2 || su->nseats > BP_SEATS || bp_setup_check(su) != BP_OK) return BP_EARG;
    here = 0;
    for (s = 0; s < su->nseats; s++) {
        if (su->machine[s] > 1) return BP_EARG;
        here |= 1 << su->machine[s];
    }
    if (here != 3) return BP_EARG;          /* a seat on each machine */
    os88_memcpy(&bpl.lob, su, sizeof(BPSetup));
    for (s = su->nseats; s < BP_SEATS; s++) bpl.lob.machine[s] = 0;
    bpl.lseq = (BPWord)(bpl.lseq + 1);
    bpl.rmask = 0;
    if (bpl.ph == BPL_LOBBY) bpl_lobby_send();
    bp_ui_link();
    return BP_OK;
}
int bp_lobby_set(const BPSetup *su) { return ovl_lobby_set(su); }

int bp_lobby_ready(int ready)
{
    if (bp_mode == BP_MODE_LOCAL || bpl.ph != BPL_LOBBY || bpl.intent) return BP_EPHASE;
    if (bp_mode == BP_MODE_HOST) {
        if (ready) bpl.rmask |= 1;
        else bpl.rmask &= (BPByte)~1;
    } else bpl.myready = (BPByte)(ready != 0);
    bpl_lobby_send();
    bp_ui_link();
    return BP_OK;
}

const BPSetup *bp_lobby_setup(void)
{
    if (bp_mode == BP_MODE_LOCAL) return 0;
    return &bpl.lob;
}

/* host: both ready on this revision: the match begins, as a transaction */
static int ovl_lobby_start(void)
{
    int e;
    if (bp_mode != BP_MODE_HOST || bpl.ph != BPL_LOBBY || bpl.intent) return BP_EPHASE;
    if (bpl.rmask != 3) return BP_EPENDING;
    e = bp_setup_check(&bpl.lob);
    if (e != BP_OK) return e;
    bpl_seed();
    e = bp_new_match(&bp_game, &bpl.lob, &bpl.seed, &bpl_res);
    if (e != BP_OK) return e;
    bpl_redigest();
    bpl.ph = BPL_MATCH;
    bpl.pend = BPL_P_START;
    bp_u32_copy(&bpl.rrev, &bp_game.rev);
    bp_u32_copy(&bpl.rdig, &bpl.dig);
    bpl.rorigin = 0xFF;
    bpl.tries = 0;
    bpl.hw = 0;
    bpl.hwcode = 0;
    bpl.hseq = 0;
    bpl.heard = bpl.clk;
    bpl.due = bpl_after(BPL_RETRY);
    bpl.msg = 0;
    bpl_send_result();
    bp_ui_committed(&bpl_res);
    bp_ui_link();
    return BP_OK;
}
int bp_lobby_start(void) { return ovl_lobby_start(); }

/* client: START - the same setup and seed through the same engine */
static void ovl_start_in(void)
{
    const BPByte *p;
    int e;
    p = bpl_rx;
    if (bp_mode != BP_MODE_CLIENT) { bp_diag.frame++; return; }
    bpl_get32(&bpl_t1, p + 9);
    if (bpl.ph == BPL_MATCH) {          /* a repeat: the ACK again */
        if (bpl_eq(&bpl_t1, &bp_game.rev)) bpl_send_ack();
        else bp_diag.frame++;
        return;
    }
    if (bpl.ph != BPL_LOBBY || bpl.intent || bpl_get16(p + 3) != bpl.lseq) { bp_diag.frame++; return; }
    bpl_get32(&bpl.seed, p + 5);
    bpl_get32(&bpl_t2, p + 13);
    e = bp_new_match(&bp_game, &bpl.lob, &bpl.seed, &bpl_res);
    bpl_redigest();
    bpl.ph = BPL_MATCH;
    bpl.cseq = 0;
    bpl.cpend = 0;
    bpl.heard = bpl.clk;
    bpl.msg = 0;
    bpl_send_ack();
    if (e != BP_OK || !bpl_eq(&bpl_t1, &bp_game.rev) || !bpl_eq(&bpl_t2, &bpl.dig)) bpl_diverge();
    if (e == BP_OK) bp_ui_committed(&bpl_res);
    bp_ui_link();
}

/* ---- chat (131.6.6) -------------------------------------------------------- */
static int ovl_chat_send(const char *text)
{
    BPByte *p;
    int i;
    if (bp_mode == BP_MODE_LOCAL || bpl.ph == BPL_UNBOUND) return BP_EPHASE;
    if (bpl.cbusy || (bpl.clast && (BPWord)(bpl.clk - bpl.clast) < BPL_PING)) {
        bp_diag.chatdrop++;
        return BP_EPENDING;
    }
    for (i = 0; i < BP_CHAT_LEN && text[i]; i++)
        bpl.cout[i] = (char)((BPByte)text[i] < 32 ? '?' : text[i]);
    bpl.cout[i] = 0;
    if (!i) return BP_EARG;
    bpl.cnum = (BPByte)(bpl.cnum + 1);
    if (!bpl.cnum) bpl.cnum = 1;
    bpl.cbusy = 1;
    bpl.ctries = 0;
    bpl.clast = bpl.clk;
    if (!bpl.clast) bpl.clast = 1;
    bpl.cdue = bpl_after(BPL_RETRY);
    p = bpl_begin(BPF_CHAT);
    p[3] = bpl.cnum;
    os88_memcpy(p + 4, bpl.cout, (unsigned)i);
    bpl_send();
    bp_ui_chat(bp_here, bpl.cout);
    return BP_OK;
}
int bp_chat_send(const char *text) { return ovl_chat_send(text); }

static void ovl_chat_in(void)
{
    BPByte *p;
    BPByte n;
    int i;
    n = bpl_rx[3];
    if (bpl_rx[0] == BPF_CHATACK) {
        if (bpl.cbusy && n == bpl.cnum) bpl.cbusy = 0;
        return;
    }
    p = bpl_begin(BPF_CHATACK);
    p[3] = n;
    bpl_send();
    if (n == 0 || n == bpl.crx) return;     /* a copy: acknowledged, not shown */
    bpl.crx = n;
    for (i = 0; i < BP_CHAT_LEN; i++) {
        bpl_chat[i] = (char)bpl_rx[4 + i];
        if (bpl_chat[i] && bpl_rx[4 + i] < 32) bpl_chat[i] = '?';
    }
    bpl_chat[BP_CHAT_LEN] = 0;
    bp_ui_chat(1 - bp_here, bpl_chat);
}

/* ---- snapshot adoption (131.6.5) -------------------------------------------- */
static void bpl_snapreq(void)
{
    BPByte *p;
    p = bpl_begin(BPF_SNAPREQ);
    bpl_put32(p + 3, &bp_game.match_id);
    bpl_put16(p + 7, bpl.ssent);
    bpl_send();
    bpl.sdue = bpl_after(BPL_RETRY);
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
    bpl_snapreq();
    bp_ui_link();
    return BP_OK;
}
int bp_sess_adopt(void) { return ovl_adopt(); }

static void bpl_snapdata(void)
{
    BPByte *p;
    BPWord n;
    p = bpl_begin(BPF_SNAPDATA);
    bpl_put16(p + 3, bpl.ssent);
    n = (BPWord)(bpl.sn - bpl.ssent);
    if (n > BPL_CHUNK) n = BPL_CHUNK;
    os88_memcpy(p + 5, bpl_snap + bpl.ssent, n);
    if (bpl_send() == BP_OK) {
        bpl.ssent = (BPWord)(bpl.ssent + n);
        bpl.sdue = bpl_after(BPL_RETRY);
    }
}
static void bpl_snaphead(void)
{
    BPByte *p;
    p = bpl_begin(BPF_SNAPHEAD);
    bpl_put32(p + 3, &bp_game.match_id);
    bpl_put32(p + 7, &bpl.srev);
    bpl_put32(p + 11, &bpl.sdig);
    bpl_put16(p + 15, bpl.sn);
    bpl_send();
}

/* host: SNAPREQ - start (offset 0) or the client's progress */
static void ovl_snapreq_in(void)
{
    BPWord off;
    bpl_get32(&bpl_t1, bpl_rx + 3);
    off = bpl_get16(bpl_rx + 7);
    if (bp_mode != BP_MODE_HOST || bpl.ph != BPL_MATCH || !bpl_eq(&bpl_t1, &bp_game.match_id)) {
        bp_diag.frame++;
        return;
    }
    if (bpl.snap != 1 || off == 0) {
        bpl.sn = (BPWord)bp_encode(&bp_game, bpl_snap, BP_STATE_MAX);
        if (!bpl.sn) { bp_diag.frame++; return; }
        bp_u32_copy(&bpl.srev, &bp_game.rev);
        bp_u32_copy(&bpl.sdig, &bpl.dig);
        bpl.snap = 1;
        bpl.sack = 0;
        bpl.ssent = 0;
        bpl.stries = 0;
        bp_diag.resync++;
        bpl_snaphead();
        bpl_snapdata();
        bp_ui_link();
        return;
    }
    if (off > bpl.sn) { bp_diag.frame++; return; }
    if (off > bpl.sack) { bpl.sack = off; bpl.stries = 0; }
    if (bpl.ssent < bpl.sack) bpl.ssent = bpl.sack;
    if (bpl.sack >= bpl.sn) { bpl.snap = 0; bp_ui_link(); }
}

/* client: SNAPHEAD / SNAPDATA, in order, for this match only */
static void ovl_snap_in(void)
{
    const BPByte *p;
    BPWord off, n;
    int e;
    p = bpl_rx;
    if (bp_mode != BP_MODE_CLIENT || bpl.snap != 2) { bp_diag.frame++; return; }
    if (p[0] == BPF_SNAPHEAD) {
        bpl_get32(&bpl_t1, p + 3);
        n = bpl_get16(p + 15);
        if (!bpl_eq(&bpl_t1, &bp_game.match_id) || n == 0 || n > BP_STATE_MAX) {
            bp_diag.frame++;
            bpl_say("That board is another match");
            return;
        }
        bpl_get32(&bpl.srev, p + 7);
        bpl_get32(&bpl.sdig, p + 11);
        bpl.sn = n;
        bpl.shead = 1;
        bpl.ssent = 0;
        return;
    }
    off = bpl_get16(p + 3);
    if (!bpl.shead || off != bpl.ssent) { bp_diag.frame++; bpl_snapreq(); return; }
    n = (BPWord)(bpl.sn - off);
    if (n > BPL_CHUNK) n = BPL_CHUNK;
    os88_memcpy(bpl_snap + off, p + 5, n);
    bpl.ssent = (BPWord)(off + n);
    bpl.stries = 0;
    bpl_snapreq();
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
        bpl.msgcode = (BPByte)e;
        bpl_say("The banker's board did not check");
        return;
    }
    os88_memcpy(&bp_game, &bpl_scr, sizeof(BPState));
    bp_u32_copy(&bpl.dig, &bpl_t2);
    bpl.diverged = 0;
    bpl.cpend = 0;
    bpl.tok = 0;
    bp_diag.resync++;
    bpl_send_ack();
    bpl_say("Using the banker's board");
    os88_memset(&bpl_res, 0, sizeof(BPResult));
    bp_ui_committed(&bpl_res);
}

/* ---- the checkpoint exchange (131.7.5) --------------------------------------- */
static void bpl_ck_frame(int type)
{
    BPByte *p;
    p = bpl_begin(type);
    if (type == BPF_CKBEGIN) {
        bpl_put32(p + 3, &bpl.ckm.match_id);
        bpl_put32(p + 7, &bpl.ckm.ckpt_id);
        bpl_put32(p + 11, &bpl.ckm.rev);
        bpl_put32(p + 15, &bpl.ckm.digest);
        bpl_put32(p + 19, &bpl.ckm.host_id);
        bpl_put32(p + 23, &bpl.ckm.client_id);
    } else if (type == BPF_CKREQ) {
        bpl_put32(p + 3, &bp_game.match_id);
    } else {
        bpl_put32(p + 3, &bpl.ckm.ckpt_id);
        if (type == BPF_CKREPLY) {
            p[7] = (BPByte)(bpl.ck == BPL_CK_DONE ? 2 : 1);
            p[8] = bpl.ckcode;
        }
    }
    bpl_send();
    bpl.ckdue = bpl_after(BPL_RETRY);
}

static void bpl_ck_end(int code, const char *why)
{
    bpl.ck = 0;
    bpl.ckwant = 0;
    bpl.cktries = 0;
    bpl.ckcode = (BPByte)code;
    bpl.pair = (BPByte)(code ? BP_PAIR_FAIL : BP_PAIR_DONE);
    bpl.msgcode = (BPByte)code;
    bpl_say(why);
}

/* host: the exchange begins (nothing outstanding) */
static void bpl_ck_begin(void)
{
    int e;
    bpl.ckwant = 0;
    os88_memset(&bpl.ckm, 0, sizeof(BPCkpt));
    bp_u32_copy(&bpl.ckm.match_id, &bp_game.match_id);
    bpl_rand32(&bpl.ckm.ckpt_id);
    bp_u32_copy(&bpl.ckm.rev, &bp_game.rev);
    bp_u32_copy(&bpl.ckm.digest, &bpl.dig);
    bp_u32_copy(&bpl.ckm.host_id, &bpl.me);
    bp_u32_copy(&bpl.ckm.client_id, &bpl.peer);
    bpl.ckm.role = BP_MODE_HOST;
    bpl.ckm.state = 1;
    e = bp_ckpt_write(&bpl.ckm, &bp_game);
    if (e != BP_OK) { bpl_ck_end(e, "Save failed on this PC"); return; }
    bpl.ck = BPL_CK_WRITTEN;
    bpl.cktries = 0;
    bpl.pair = BP_PAIR_BUSY;
    bpl_ck_frame(BPF_CKBEGIN);
    bp_ui_link();
}

static int ovl_save_pair(void)
{
    if (bp_mode == BP_MODE_LOCAL) return BP_EPHASE;
    if (bpl.ph != BPL_MATCH || bp_link_state() != BP_L_PLAY) return BP_EPAUSED;
    if (bpl.ck || bpl.ckwant) return BP_EPENDING;
    bpl.pair = BP_PAIR_BUSY;
    bpl.msg = 0;
    if (bp_mode == BP_MODE_CLIENT) {
        bpl.ck = BPL_CK_ASKED;
        bpl.cktries = 0;
        bpl_ck_frame(BPF_CKREQ);
    } else if (bp_sess_busy()) {
        bpl.ckwant = 1;                     /* behind the transaction */
    } else bpl_ck_begin();
    bp_ui_link();
    return BP_OK;
}
int bp_sess_save_pair(void) { return ovl_save_pair(); }

static void ovl_ck_in(void)
{
    const BPByte *p;
    int e, stage;
    p = bpl_rx;
    if (bpl.ph != BPL_MATCH) { bp_diag.frame++; return; }
    if (bp_mode == BP_MODE_HOST) {
        if (p[0] == BPF_CKREQ) {
            bpl_get32(&bpl_t1, p + 3);
            if (!bpl_eq(&bpl_t1, &bp_game.match_id)) { bp_diag.frame++; return; }
            if (bpl.ck == BPL_CK_WRITTEN) bpl_ck_frame(BPF_CKBEGIN);
            else if (bpl.ck == BPL_CK_COMMIT) bpl_ck_frame(BPF_CKCOMMIT);
            else if (!bpl.ckwant) {
                bpl.pair = BP_PAIR_BUSY;
                if (bp_sess_busy()) bpl.ckwant = 1;
                else bpl_ck_begin();
            }
            return;
        }
        if (p[0] != BPF_CKREPLY) { bp_diag.frame++; return; }
        bpl_get32(&bpl_t1, p + 3);
        if (!bpl.ck || !bpl_eq(&bpl_t1, &bpl.ckm.ckpt_id)) { bp_diag.frame++; return; }
        stage = p[7];
        e = p[8];
        if (bpl.ck == BPL_CK_WRITTEN && stage == 1) {
            if (e) { bpl_ck_end(e, "The other PC could not save"); return; }
            e = bp_ckpt_commit(&bpl.ckm);
            if (e) { bpl_ck_end(e, "Save failed on this PC"); return; }
            bpl.ck = BPL_CK_COMMIT;
            bpl.cktries = 0;
            bpl_ck_frame(BPF_CKCOMMIT);
        } else if (bpl.ck == BPL_CK_COMMIT && stage == 2) {
            if (e) bpl_ck_end(e, "Save incomplete: resume will finish it");
            else bpl_ck_end(0, "Paired save complete");
        }
        return;
    }
    /* the client */
    if (p[0] == BPF_CKBEGIN) {
        bpl_get32(&bpl_t1, p + 7);
        if (bpl.ck >= BPL_CK_CAND && bpl_eq(&bpl_t1, &bpl.ckm.ckpt_id)) {
            bpl_ck_frame(BPF_CKREPLY);      /* a repeat: the same answer */
            return;
        }
        if (bpl.cpend || bpl.snap) { bp_diag.frame++; return; }    /* not yet */
        os88_memset(&bpl.ckm, 0, sizeof(BPCkpt));
        bpl_get32(&bpl.ckm.match_id, p + 3);
        bp_u32_copy(&bpl.ckm.ckpt_id, &bpl_t1);
        bpl_get32(&bpl.ckm.rev, p + 11);
        bpl_get32(&bpl.ckm.digest, p + 15);
        bpl_get32(&bpl.ckm.host_id, p + 19);
        bpl_get32(&bpl.ckm.client_id, p + 23);
        bpl.ckm.role = BP_MODE_CLIENT;
        bpl.ckm.state = 1;
        e = BP_OK;
        if (!bpl_eq(&bpl.ckm.match_id, &bp_game.match_id) || !bpl_eq(&bpl.ckm.rev, &bp_game.rev)
            || !bpl_eq(&bpl.ckm.digest, &bpl.dig) || !bpl_eq(&bpl.ckm.host_id, &bpl.peer)
            || !bpl_eq(&bpl.ckm.client_id, &bpl.me)) e = BP_EMATCH;
        else e = bp_ckpt_write(&bpl.ckm, &bp_game);
        bpl.ckcode = (BPByte)e;
        bpl.ck = BPL_CK_CAND;
        bpl.pair = BP_PAIR_BUSY;
        bpl_ck_frame(BPF_CKREPLY);
        if (e) {
            bpl.ck = BPL_CK_DONE;           /* keeps answering the same code */
            bpl.pair = BP_PAIR_FAIL;
            bpl.msgcode = (BPByte)e;
            bpl_say("Save failed on this PC");
        } else bp_ui_link();
        return;
    }
    if (p[0] != BPF_CKCOMMIT) { bp_diag.frame++; return; }
    bpl_get32(&bpl_t1, p + 3);
    if (bpl.ck < BPL_CK_CAND || !bpl_eq(&bpl_t1, &bpl.ckm.ckpt_id)) { bp_diag.frame++; return; }
    if (bpl.ck == BPL_CK_CAND) {
        e = bp_ckpt_commit(&bpl.ckm);
        bpl.ckcode = (BPByte)e;
        bpl.ck = BPL_CK_DONE;
        bpl.pair = (BPByte)(e ? BP_PAIR_FAIL : BP_PAIR_DONE);
        bpl.msgcode = (BPByte)e;
        bpl_ck_frame(BPF_CKREPLY);
        bpl_say(e ? "Save incomplete: resume will finish it" : "Paired save complete");
        return;
    }
    bpl_ck_frame(BPF_CKREPLY);
}

/* ---- resuming a paired save (131.7.5) ------------------------------------------ */
static int ovl_resume_pair(void)
{
    int e;
    if (bp_mode == BP_MODE_LOCAL || !bpl.on || bpl.ph == BPL_MATCH) return BP_EPHASE;
    os88_memset(&bpl.rsm, 0, sizeof(BPCkpt));
    e = bp_ckpt_load(bp_mode, &bpl.rsm, &bpl_scr);
    if (e == BP_OK && (bpl.rsm.role != bp_mode || bpl.rsm.state != 2)) e = BP_EMATCH;
    if (e != BP_OK) {
        bpl.intent = 0;
        bpl.pair = BP_PAIR_FAIL;
        bpl.msgcode = (BPByte)e;
        bpl_say("No paired save for this side on this disk");
        return e;
    }
    bpl.intent = 1;
    bpl.rsok = 0;
    bpl.rscommit = 0;
    bpl.pair = BP_PAIR_BUSY;
    bpl.bad = 0;
    bpl.msg = 0;
    bpl_unbind();
    bp_ui_link();
    return BP_OK;
}
int bp_sess_resume_pair(void) { return ovl_resume_pair(); }

static void bpl_rs_fail(const char *why)
{
    bp_diag.frame++;
    if (bpl.msg == why) return;
    bpl.pair = BP_PAIR_FAIL;
    bpl_say(why);
}

/* try to offer checkpoint id t1 instead (the client: its candidate or a
 * committed one; the host: a committed one) */
static int bpl_rs_switch(void)
{
    static BPCkpt m;
    int e;
    os88_memset(&m, 0, sizeof(BPCkpt));
    bp_u32_copy(&m.ckpt_id, &bpl_t1);
    e = bp_ckpt_load(bp_mode, &m, &bpl_scr);
    if (e != BP_OK || !bpl_eq(&m.ckpt_id, &bpl_t1) || m.role != bp_mode) return 0;
    if (m.state != 2 && (bp_mode != BP_MODE_CLIENT || m.state != 1)) return 0;
    os88_memcpy(&bpl.rsm, &m, sizeof(BPCkpt));
    bpl.rscommit = (BPByte)(m.state == 1);
    return 1;
}

static void ovl_resume_in(void)
{
    const BPByte *p;
    BPByte role, ok;
    int e;
    p = bpl_rx;
    role = p[3];
    ok = p[4];
    bpl_get32(&bpl_t2, p + 5);              /* match */
    bpl_get32(&bpl_t1, p + 9);              /* checkpoint */
    if (bpl.ph == BPL_MATCH) {              /* adopted already: confirm it */
        if (bpl.intent && bpl_eq(&bpl_t1, &bpl.rsm.ckpt_id)) {
            bpl.rsok = 1;
            bpl_lobby_send();
        } else bp_diag.frame++;
        return;
    }
    if (bpl.ph != BPL_LOBBY || !bpl.intent) { bp_diag.frame++; return; }
    if (role == bp_mode) { bpl_rs_fail("Both PCs loaded the same side's save"); return; }
    bpl_get32(&bpl_t3, p + 21);
    bpl_get32(&bpl_t4, p + 25);
    if (!bpl_eq(&bpl_t2, &bpl.rsm.match_id) || !bpl_eq(&bpl_t3, &bpl.rsm.host_id)
        || !bpl_eq(&bpl_t4, &bpl.rsm.client_id)) { bpl_rs_fail("These saves are not a pair"); return; }
    if (!bpl_eq(&bpl_t1, &bpl.rsm.ckpt_id)) {
        /* the host's offer leads: the client rolls forward to it; the host
         * falls back only when the client says it cannot (ok = 2) */
        if (bp_mode == BP_MODE_CLIENT) {
            if (bpl_rs_switch()) bpl.rsok = 0;
            else bpl.rsok = 2;
            bpl_lobby_send();
            return;
        }
        if (ok == 2 && bpl_rs_switch()) { bpl.rsok = 0; bpl_lobby_send(); return; }
        if (ok == 2) bpl_rs_fail("These saves are not a pair");
        return;
    }
    bpl_get32(&bpl_t3, p + 13);
    bpl_get32(&bpl_t4, p + 17);
    if (!bpl_eq(&bpl_t3, &bpl.rsm.rev) || !bpl_eq(&bpl_t4, &bpl.rsm.digest)) {
        bpl_rs_fail("Same save, different board");
        return;
    }
    /* agreed: roll a candidate forward, then adopt */
    if (bpl.rscommit) {
        bpl.rsm.state = 2;
        e = bp_ckpt_commit(&bpl.rsm);
        if (e != BP_OK) { bpl.msgcode = (BPByte)e; bpl_rs_fail("Could not finish the last save"); return; }
        bpl.rscommit = 0;
    }
    os88_memcpy(&bp_game, &bpl_scr, sizeof(BPState));
    bpl_redigest();
    bpl.ph = BPL_MATCH;
    bpl.pend = 0;
    bpl.cpend = 0;
    bpl.hw = 0;
    bpl.hwcode = 0;
    bpl.cseq = 0;
    bpl.hseq = 0;
    bpl.tries = 0;
    bpl.rsok = 1;
    bpl.pair = BP_PAIR_DONE;
    bpl.heard = bpl.clk;
    bpl_lobby_send();
    bpl_say("Resumed the paired save");
    os88_memset(&bpl_res, 0, sizeof(BPResult));
    bp_ui_committed(&bpl_res);
}

/* ---- one received frame ------------------------------------------------------ */
static void bpl_frame(int n)
{
    int t;
    t = bpl_rx[0];
    if (t <= 0 || t >= BPF_N || n != bpl_lens[t] - 2) { bp_diag.frame++; return; }
    bp_diag.rx++;
    if (t == BPF_HELLO) { ovl_hello(); return; }
    if (bpl_zero(&bpl.peer) || bpl_get16(bpl_rx + 1) != bpl.peer.lo) { bp_diag.frame++; return; }
    bpl.heard = bpl.clk;
    if (bpl.ph == BPL_UNBOUND) {            /* their frame says they bound us */
        bpl.ph = BPL_LOBBY;
        bpl.msg = 0;
        bp_ui_link();
    }
    switch (t) {
    case BPF_INTENT:   if (bp_mode == BP_MODE_HOST) bpl_h_intent(); else bp_diag.frame++; break;
    case BPF_ACK:      if (bp_mode == BP_MODE_HOST) bpl_h_ack(); else bp_diag.frame++; break;
    case BPF_TERMS:    if (bp_mode == BP_MODE_CLIENT) bpl_c_terms(); else bp_diag.frame++; break;
    case BPF_RESULT:   if (bp_mode == BP_MODE_CLIENT) bpl_c_result(); else bp_diag.frame++; break;
    case BPF_REFUSE:   if (bp_mode == BP_MODE_CLIENT) bpl_c_refuse(); else bp_diag.frame++; break;
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
        if (!bpl.left && bpl.ph == BPL_MATCH) {
            bpl.left = 1;
            bpl_say("The other PC left the match");
        } else if (bpl.ph == BPL_LOBBY) {
            bpl_unbind();
            bpl_say("The other PC left the lobby");
        }
        break;
    default:           ovl_ck_in(); break;
    }
}

/* ---- the per-tick service ------------------------------------------------------ */
static void bpl_timers(void)
{
    if (bpl.ph == BPL_UNBOUND) {
        if ((BPWord)(bpl.clk - bpl.beat) >= BPL_BEAT) {
            bpl.beat = bpl.clk;
            bpl_send_hello();
        }
        return;
    }
    if ((BPWord)(bpl.clk - bpl.heard) >= BPL_QUIET && bpl.ph == BPL_LOBBY) {
        bpl_unbind();                       /* a lobby partner went quiet */
        bpl_say("The other PC went quiet");
        return;
    }
    if (bpl.ph == BPL_LOBBY) {
        if ((BPWord)(bpl.clk - bpl.beat) >= 2 * BPL_BEAT) {
            bpl.beat = bpl.clk;
            bpl_lobby_send();
        }
        return;
    }
    /* the match: the outstanding transaction first */
    if ((bpl.pend && !bpl.diverged) || bpl.cpend) {
        if (bpl_due(bpl.due)) {
            if (bpl.tries < 255) bpl.tries++;
            bp_diag.retry++;
            bpl.due = bpl_after(BPL_RETRY);
            if (bpl.pend) bpl_send_result();
            else bpl_send_intent();
        }
    } else if (bpl.ckwant && !bpl.pend && !bpl.snap && !bpl.diverged) bpl_ck_begin();
    if (bpl.ck == BPL_CK_WRITTEN || bpl.ck == BPL_CK_COMMIT || bpl.ck == BPL_CK_ASKED) {
        if (bpl_due(bpl.ckdue)) {
            if (bpl.cktries < 255) bpl.cktries++;
            bp_diag.retry++;
            bpl_ck_frame(bpl.ck == BPL_CK_WRITTEN ? BPF_CKBEGIN
                         : bpl.ck == BPL_CK_COMMIT ? BPF_CKCOMMIT : BPF_CKREQ);
        }
    }
    if (bpl.snap == 1) {                    /* host: the window, or go back */
        if (bpl_due(bpl.sdue) && bpl.ssent > bpl.sack) {
            if (bpl.stries < 255) bpl.stries++;
            bp_diag.retry++;
            bpl.ssent = bpl.sack;
            if (!bpl.sack) bpl_snaphead();
        }
        if (bpl.ssent < bpl.sn && bpl.ssent < bpl.sack + BPL_WINDOW) bpl_snapdata();
    } else if (bpl.snap == 2 && bpl_due(bpl.sdue)) {
        if (bpl.stries < 255) bpl.stries++;
        bp_diag.retry++;
        if (!bpl.shead) bpl.ssent = 0;
        bpl_snapreq();
    }
    if (bpl.cbusy && bpl_due(bpl.cdue)) {
        if (++bpl.ctries > BPL_TRIES) {
            bpl.cbusy = 0;
            bp_diag.chatdrop++;
        } else {
            BPByte *p;
            p = bpl_begin(BPF_CHAT);
            p[3] = bpl.cnum;
            os88_memcpy(p + 4, bpl.cout, os88_strlen(bpl.cout));
            bpl_send();
            bpl.cdue = bpl_after(BPL_RETRY);
        }
    }
    if ((BPWord)(bpl.clk - bpl.sent) >= BPL_PING) bpl_send_ping();
}

void bp_sess_tick(void)
{
    BPWord now, el;
    int i, n, s;
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
    s = bp_link_state();
    if (s != bpl.lst || (s == BP_L_SEARCH && bp_ck_msg() != bpl.ckmsg)) {
        bpl.lst = (BPByte)s;
        bpl.ckmsg = bp_ck_msg();
        bp_ui_link();
    }
}

/* ---- opening, joining, leaving ---------------------------------------------------- */
static int ovl_open(int mode, int port)
{
    int e;
    bpl_reset();
    bp_mode = (BPByte)mode;
    bp_here = (BPByte)(mode == BP_MODE_CLIENT);
    os88_memset(&bp_game, 0, sizeof(BPState));
    e = bp_ck_open(port);
    if (e != BP_OK) {
        bpl.msg = bp_ck_msg();
        bp_ui_link();
        return e;
    }
    bpl.on = 1;
    bpl_rand32(&bpl.me);
    bpl.clk = 1;
    bpl.beat = 0;
    bpl.sent = 1;
    bpl.heard = 1;
    if (mode == BP_MODE_HOST) {
        bp_setup_default(&bpl.lob);
        bpl.lob.machine[1] = 1;             /* a seat on each machine */
        bpl.lseq = 1;
    }
    bpl.lst = (BPByte)bp_link_state();
    bp_ui_link();
    return BP_OK;
}
int bp_link_host(int port) { return ovl_open(BP_MODE_HOST, port); }
int bp_link_join(int port) { return ovl_open(BP_MODE_CLIENT, port); }

void bp_link_leave(void)
{
    BPByte *p;
    if (bp_mode == BP_MODE_LOCAL || !bpl.on) return;
    if (!bpl_zero(&bpl.peer)) {
        p = bpl_begin(BPF_BYE);
        bpl_send();
        p = bpl_begin(BPF_BYE);
        bpl_send();
        (void)p;
    }
    bp_ck_close();
    bpl.on = 0;
    bpl.ph = BPL_UNBOUND;
    bp_u32_set(&bpl.peer, 0, 0);
    bpl.pend = 0;
    bpl.cpend = 0;
    bpl.ck = 0;
    bpl.ckwant = 0;
    bpl.snap = 0;
    bpl.msg = "Left the cable match";
    bp_ui_link();
}

/* ---- host harness defaults (131.12): a hosttest program that is not
 * t_link.c gets an unplugged cable and a silent UI ------------------------------- */
#if defined(BP_HOST) && !defined(BP_LINK_FAKE)
int bp_ck_open(int port) { (void)port; return BP_EIO; }
void bp_ck_close(void) {}
void bp_ck_tick(void) {}
int bp_ck_heard(void) { return 0; }
int bp_ck_send(const BPByte *body, unsigned n) { (void)body; (void)n; return BP_EIO; }
int bp_ck_recv(BPByte *body) { (void)body; return 0; }
int bp_ck_port(void) { return 0; }
const char *bp_ck_msg(void) { return "Off"; }
BPWord bp_ck_rand(void) { return 0x1234; }
void bp_ui_committed(const BPResult *r) { (void)r; }
void bp_ui_link(void) {}
void bp_ui_chat(int machine, const char *text) { (void)machine; (void)text; }
#ifndef BP_CKPT_HOST    /* (a harness with the save lane's files defines it) */
int bp_ckpt_write(const BPCkpt *meta, const BPState *s) { (void)meta; (void)s; return BP_EIO; }
int bp_ckpt_commit(const BPCkpt *meta) { (void)meta; return BP_EIO; }
int bp_ckpt_load(int role, BPCkpt *meta, BPState *out) { (void)role; (void)meta; (void)out; return BP_EIO; }
#endif
#endif
