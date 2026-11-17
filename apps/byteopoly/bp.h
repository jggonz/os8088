/* Byteopoly - the shared C contract (SPEC.md 131). Original.
 *
 * ONE header every lane compiles against. It owns the types, the capacities,
 * the canonical match record, the command and result records, and the
 * interface of every subsystem. Ownership (SPEC.md 131.3.5): the LEAD owns
 * this file; a lane that needs a new declaration in another lane's block
 * asks in its log. A lane may ADD declarations at the end of its OWN block.
 *
 *   lane     files                                  block below
 *   rules    bp_math.c bp_board.c bp_cards.c        BP_RULES
 *            bp_rules.c hosttest/t_rules.c
 *   save     bp_codec.c bp_save.c hosttest/t_save.c BP_SAVE
 *   link     bp_link.c bp_cable.inc                 BP_LINK
 *            hosttest/t_link.c
 *   face     bp_face.c                              BP_FACE
 *   panels   bp_panels.c                            BP_PANELS
 *   app      bp_app.c bp_setup.c                    BP_APP
 *   art      art.h art/ icon.inc tools/os88bpart.py (art.h)
 *
 * The C rules of SPEC.md 73 bind everything here: every addressable object
 * is static, no struct is copied/passed/returned by value (pass pointers to
 * statics), no long/float/bit-field, frames under 96 bytes. A logical 32-bit
 * value is a BPU32 of two named 16-bit words, and every operation on one is
 * a checked function in bp_math.c (SPEC.md 131.4.1) - never a host integer.
 *
 * BP_HOST builds the same source on the host for hosttest/ (SPEC.md 131.12):
 * BPWord is then `unsigned short`, so a value is STORED as on the target;
 * the arithmetic functions mask every intermediate to 16 bits so a wider
 * host `int` cannot hide a guest overflow. */
#ifndef BYTEOPOLY_BP_H
#define BYTEOPOLY_BP_H

#ifdef BP_HOST
typedef unsigned short BPWord;
typedef short BPInt;
#else
typedef unsigned BPWord;
typedef int BPInt;
#endif
typedef unsigned char BPByte;

/* A logical 32-bit unsigned value: money, revisions, identities, digests. */
typedef struct { BPWord lo, hi; } BPU32;

/* ---- capacities and fixed geometry (SPEC.md 131.2) ---------------------- */
#define BP_SEATS        4
#define BP_SPACES       28
#define BP_PROPS        18      /* 16 computers + 2 utilities, space order */
#define BP_GROUPS       6       /* C H A I R P */
#define BP_DECKS        2       /* 0 Floppy Find, 1 System Error */
#define BP_DECK_CARDS   8
#define BP_NAMELEN      8       /* display name chars, NUL not counted */
#define BP_NOBODY       0xFF    /* owner = bank, no seat, no card */
#define BP_START_CASH   1500
#define BP_SALARY       200
#define BP_BAIL         50
#define BP_REPAIR_SPACE 7
#define BP_CRASH_SPACE  21
#define BP_MAX_LEVEL    5       /* 1..4 RAM chips, 5 Workshop */
#define BP_EV_MAX       24      /* events one transaction may report */

/* space kinds */
#define BP_K_BOOT       0
#define BP_K_COMPUTER   1
#define BP_K_UTILITY    2
#define BP_K_EVENT      3       /* deck in BPSpace.group: 0 FF, 1 SE */
#define BP_K_CHARGE     4       /* amount in BPSpace.price */
#define BP_K_REPAIR     5
#define BP_K_SWAP       6
#define BP_K_CRASH      7

/* collections (BPSpace.group for a computer) */
#define BP_G_COMMODORE  0       /* C */
#define BP_G_HOME       1       /* H */
#define BP_G_APPLE      2       /* A */
#define BP_G_IBM        3       /* I */
#define BP_G_RIVALS     4       /* R */
#define BP_G_POWER      5       /* P */

/* tokens */
#define BP_T_FLOPPY     0
#define BP_T_MOUSE      1
#define BP_T_JOYSTICK   2
#define BP_T_CASSETTE   3

/* rules */
#define BP_RULES_CLASSIC 0
#define BP_RULES_QUICK   1

/* match status */
#define BP_ST_EMPTY     0       /* no match */
#define BP_ST_PLAY      1
#define BP_ST_OVER      2

/* phases: every economic decision has one (SPEC.md 131.3.2). MOVING,
 * RESOLVE_LANDING and TURN_COMPLETE are inside one transaction and never
 * observable; HANDOFF and LINK_PAUSED are presentation/session overlays. */
#define BP_PH_NONE      0
#define BP_PH_MANAGE    1       /* before the roll: ROLL, trade, manage */
#define BP_PH_REPAIR    2       /* detained, before choosing: PAY/DISK/ROLL, manage */
#define BP_PH_BUY       3       /* PURCHASE_DECISION: BUY or DECLINE */
#define BP_PH_AUCTION   4
#define BP_PH_CARD      5       /* SHOW_CARD: ACK_CARD resolves the effect */
#define BP_PH_DEBT      6
#define BP_PH_TRADE     7       /* TRADE_RESPONSE */
#define BP_PH_AFTER     8       /* landing resolved: END_TURN or bonus ROLL, manage */
#define BP_PH_OVER      9

/* commands: the ONLY way canonical state changes (SPEC.md 131.3.3) */
#define BP_C_NONE       0
#define BP_C_ROLL       1       /* MANAGE / AFTER(bonus) / REPAIR(attempt) */
#define BP_C_BUY        2
#define BP_C_DECLINE    3       /* opens the auction */
#define BP_C_BID        4       /* amount */
#define BP_C_PASS       5
#define BP_C_ACK_CARD   6
#define BP_C_PAY_BAIL   7
#define BP_C_USE_DISK   8       /* arg = deck whose Recovery Disk */
#define BP_C_UPGRADE    9       /* arg = space */
#define BP_C_SELL       10      /* arg = space: one upgrade step */
#define BP_C_MORTGAGE   11      /* arg = space */
#define BP_C_REDEEM     12      /* arg = space */
#define BP_C_PROPOSE    13      /* trade terms below */
#define BP_C_ACCEPT     14      /* arg = low byte of offer id; rev checks the rest */
#define BP_C_REJECT     15
#define BP_C_CANCEL     16      /* proposer withdraws */
#define BP_C_SETTLE     17      /* debt: pay now that cash suffices */
#define BP_C_BANKRUPT   18
#define BP_C_END_TURN   19
#define BP_C_COUNT      20

/* results (0 = BP_OK; every other value names why nothing changed) */
#define BP_OK           0
#define BP_EPHASE       1       /* wrong phase for this command */
#define BP_ESEAT        2       /* wrong actor */
#define BP_ESTALE       3       /* expected revision is not the current one */
#define BP_EFUNDS       4       /* insufficient cash */
#define BP_EOWNER       5       /* asset not owned / not ownable by this seat */
#define BP_ERULE        6       /* a rule refuses it (even build, set, mortgage...) */
#define BP_EOVERFLOW    7       /* checked arithmetic refused */
#define BP_EARG         8       /* malformed argument / offer */
#define BP_EOUT         9       /* actor eliminated / match over */
#define BP_EINVALID     10      /* state failed validation */
#define BP_EDEPTH       11      /* continuation limit: invalid state, stopped */
#define BP_EPENDING     12      /* session: a transaction is outstanding */
#define BP_EPAUSED      13      /* session: link paused */
#define BP_ENOTMINE     14      /* session: seat belongs to the other machine */
#define BP_EIO          15      /* file */
#define BP_EFORMAT      16      /* file/frame format */
#define BP_EVERSION     17
#define BP_ECRC         18
#define BP_EMATCH       19      /* wrong match / peer / role / pair */
#define BP_EFULL        20      /* disk full / write protected */
#define BP_ECOUNT       21

/* the immutable board (bp_board.c) */
typedef struct {
    BPByte kind, group, prop;   /* prop = index 0..17 or BP_NOBODY */
    BPWord price, rent;         /* computers/utilities: price, base rent;
                                   charge: amount in price */
    const char *name, *shortname;
} BPSpace;

/* the immutable card table (bp_cards.c) */
#define BP_E_GAIN       0       /* + amount from bank */
#define BP_E_PAY        1       /* - amount to bank */
#define BP_E_TO         2       /* advance forward to space arg */
#define BP_E_FORWARD    3       /* forward arg spaces */
#define BP_E_BACK       4       /* backward arg spaces, no salary */
#define BP_E_REPAIRS    5       /* Bench Service: 25/RAM chip, 100/Workshop */
#define BP_E_DETAIN     6       /* go to Repair Shop detained */
#define BP_E_KEEP       7       /* Recovery Disk */
typedef struct {
    BPByte id, deck, effect, arg;
    BPWord amount;
    const char *title, *text;
} BPCard;

/* ---- the canonical match (SPEC.md 131.3.1) ------------------------------
 * Everything that decides the game, and nothing that does not: selection,
 * focus, animation, sound and the log are presentation and live elsewhere.
 * Encoded field by field (bp_codec.c), never copied out raw. */
typedef struct {
    BPByte status, rules, rounds_limit, nseats;
    BPU32  match_id;            /* random at creation; new for a copy */
    BPU32  rev;                 /* +1 per committed command */
    BPWord turn;                /* +1 per turn begun */
    BPWord round;               /* completed rounds */
    BPByte round_left;          /* seats still owing a turn this round (mask) */
    BPByte phase, active, decider;
    /* seats */
    char   name[BP_SEATS][BP_NAMELEN + 1];
    BPByte token[BP_SEATS], machine[BP_SEATS], alive[BP_SEATS];
    BPByte pos[BP_SEATS];
    BPU32  cash[BP_SEATS];
    BPByte detained[BP_SEATS], tries[BP_SEATS];
    BPByte disks[BP_SEATS];     /* bit d: holds deck d's Recovery Disk */
    /* properties, by prop index */
    BPByte owner[BP_PROPS], level[BP_PROPS], mortgaged[BP_PROPS];
    /* dice and movement */
    BPByte d1, d2, doubles, rolled;     /* rolled: this turn has a dice total */
    BPByte pending_move;        /* 3rd failed release: move after payment */
    /* decks: order[] holds card ids; n cards in the cycle (a held disk is out) */
    BPByte deck[BP_DECKS][BP_DECK_CARDS];
    BPByte deck_n[BP_DECKS], deck_top[BP_DECKS];
    BPByte card;                /* card shown in BP_PH_CARD, or BP_NOBODY */
    BPByte ret_phase;           /* where DEBT / TRADE return to */
    BPByte depth;               /* card continuation depth (bounded) */
    BPU32  rng;                 /* PRNG state (algorithm BP_RNG_ALG) */
    /* auction */
    BPByte auc_space, auc_open, auc_passed, auc_turn, auc_high;
    BPU32  auc_bid;
    /* debt: the debtor is always the active seat */
    BPByte debt_to, debt_reason;        /* creditor seat or BP_NOBODY = bank */
    BPU32  debt_amount;
    /* trade: one frozen offer at a time */
    BPWord offer_id;
    BPByte offer_from, offer_to, offer_disks, offer_payer;
    BPU32  offer_give, offer_get;       /* prop-index bit masks */
    BPU32  offer_cash;
    /* the end */
    BPU32  score[BP_SEATS];
    BPByte winners;             /* seat mask */
} BPState;

/* the match a lobby or setup screen builds (bp_new_match's input) */
typedef struct {
    BPByte nseats, rules, rounds;       /* rounds: 0 Classic, 10/20/30 Quick */
    char   name[BP_SEATS][BP_NAMELEN + 1];
    BPByte token[BP_SEATS], machine[BP_SEATS];  /* machine: 0 here/host, 1 client */
} BPSetup;

/* one command (an INTENT on the wire). Fields a type does not use are 0. */
typedef struct {
    BPByte type, seat, arg, to;         /* to: trade recipient */
    BPU32  rev;                         /* the revision the actor saw */
    BPU32  amount;                      /* bid, or trade cash */
    BPU32  give, get;                   /* trade prop masks */
    BPByte disks, payer;                /* trade: bit d give, bit 4+d get;
                                           payer 0 proposer, 1 recipient */
} BPCmd;

/* what a committed transaction did, for the log, sounds and the client's
 * cross-check. Bounded; an overflow is BP_EDEPTH, never a silent drop. */
#define BP_V_ORDER      1       /* seat, amount = d1*16+d2 roll-off roll */
#define BP_V_FIRST      2       /* seat starts */
#define BP_V_ROLL       3       /* seat, other = d1, space = d2 */
#define BP_V_MOVE       4       /* seat, space = destination */
#define BP_V_SALARY     5       /* seat, amount */
#define BP_V_BUY        6       /* seat, space, amount */
#define BP_V_RENT       7       /* seat pays other, space, amount */
#define BP_V_CHARGE     8       /* seat pays bank, space, amount */
#define BP_V_CARD       9       /* seat, other = card id */
#define BP_V_GAIN       10      /* seat, amount from bank */
#define BP_V_DETAIN     11      /* seat */
#define BP_V_RELEASE    12      /* seat, other = how (0 pay 1 disk 2 doubles 3 forced) */
#define BP_V_AUCTION    13      /* space: opened */
#define BP_V_BID        14      /* seat, amount */
#define BP_V_PASS       15      /* seat (other = 1: could not raise) */
#define BP_V_WON        16      /* seat, space, amount */
#define BP_V_UNSOLD     17      /* space */
#define BP_V_UPGRADE    18      /* seat, space, other = new level, amount */
#define BP_V_SELL       19      /* seat, space, other = new level, amount */
#define BP_V_MORTGAGE   20      /* seat, space, amount */
#define BP_V_REDEEM     21      /* seat, space, amount */
#define BP_V_OFFER      22      /* seat -> other */
#define BP_V_TRADED     23      /* seat <-> other */
#define BP_V_REJECTED   24
#define BP_V_CANCELLED  25
#define BP_V_DEBT       26      /* seat owes other (BP_NOBODY bank), amount */
#define BP_V_SETTLED    27      /* seat, amount */
#define BP_V_BANKRUPT   28      /* seat, other = creditor */
#define BP_V_ESTATE     29      /* seat -> other, amount = cash moved */
#define BP_V_TURN       30      /* seat begins a turn */
#define BP_V_DOUBLES    31      /* seat: another roll */
#define BP_V_ROUND      32      /* amount = rounds completed */
#define BP_V_OVER       33      /* other = winners mask */
#define BP_V_DISK       34      /* seat kept deck other's Recovery Disk */
typedef struct { BPByte kind, seat, other, space; BPU32 amount; } BPEvent;
typedef struct {
    BPByte n, d1, d2, card;
    BPEvent ev[BP_EV_MAX];
} BPResult;

/* ===== BP_RULES: bp_math.c bp_board.c bp_cards.c bp_rules.c ============== */
/* checked two-word arithmetic (SPEC.md 131.4.1). Every mutator answers
 * BP_OK or an error AND leaves *r unchanged on error. */
void   bp_u32_set(BPU32 *r, BPWord lo, BPWord hi);
void   bp_u32_copy(BPU32 *r, const BPU32 *a);
int    bp_u32_add(BPU32 *r, const BPU32 *a);      /* r += a, BP_EOVERFLOW */
int    bp_u32_addw(BPU32 *r, BPWord v);
int    bp_u32_sub(BPU32 *r, const BPU32 *a);      /* r -= a, BP_EFUNDS if a > r */
int    bp_u32_subw(BPU32 *r, BPWord v);
int    bp_u32_cmp(const BPU32 *a, const BPU32 *b);/* -1 0 1 */
int    bp_u32_cmpw(const BPU32 *a, BPWord v);
int    bp_u32_mulw(BPU32 *r, BPWord a, BPWord b); /* r = a*b (never overflows) */
int    bp_u32_mul(BPU32 *r, const BPU32 *a, BPWord b); /* r = a*b, BP_EOVERFLOW */
int    bp_u32_iszero(const BPU32 *a);
char  *bp_u32_fmt(char *buf14, const BPU32 *a);   /* "1,280", NUL; 14 bytes */
int    bp_u32_parse(BPU32 *r, const char *s);     /* digits, optional commas */
BPWord bp_rand_next(BPU32 *state, BPWord bound);  /* 0..bound-1, BP_RNG_ALG */
#define BP_RNG_ALG 1
/* the board and cards */
const BPSpace *bp_space(int space);
int    bp_prop_space(int prop);                   /* prop index -> space */
const BPCard *bp_card(int id);                    /* 0..15: deck*8 + n */
BPWord bp_group_upgrade_cost(int group);
int    bp_group_size(int group);
char   bp_group_letter(int group);                /* 'C' 'H' 'A' 'I' 'R' 'P' */
const char *bp_group_name(int group);
/* the engine (SPEC.md 131.4). bp_apply is the single mutation door: it
 * validates c against s and either commits the whole transaction (BP_OK,
 * s->rev + 1, r filled) or changes nothing and answers why. */
void   bp_setup_default(BPSetup *su);
int    bp_setup_check(const BPSetup *su);
int    bp_new_match(BPState *s, const BPSetup *su, const BPU32 *seed,
                    BPResult *r);                 /* roll-off included */
int    bp_check(const BPState *s, const BPCmd *c);
int    bp_apply(BPState *s, const BPCmd *c, BPResult *r);
const char *bp_err_text(int code);
/* queries for the UI - pure, never mutate */
int    bp_rent_quote(const BPState *s, int space, BPU32 *out);  /* as if landed now */
int    bp_can(const BPState *s, int seat, int type, int arg);   /* BP_OK or why not */
int    bp_mortgage_value(int space);
void   bp_redeem_cost(int space, BPU32 *out);
int    bp_group_owner(const BPState *s, int group);  /* seat owning all, or BP_NOBODY */
void   bp_score_of(const BPState *s, int seat, BPU32 *out);
int    bp_seat_assets(const BPState *s, int seat, BPU32 *mask);
/* rules lane additions (SPEC.md 131.4.4, 131.4.5, 131.4.9) */
#define BP_DR_NONE      0       /* BPState.debt_reason */
#define BP_DR_RENT      1
#define BP_DR_CHARGE    2       /* a charge space */
#define BP_DR_CARD      3       /* a card payment or Bench Service */
#define BP_DR_BAIL      4       /* the third failed release: pending_move set */
#define BP_DEPTH_MAX    3       /* cards drawn since the last dice move */
#define BP_BID_STEP     10
void   bp_bid_min(const BPState *s, BPU32 *out);  /* the next legal bid */
int    bp_state_check(const BPState *s);          /* BP_OK or BP_EINVALID */

/* ===== BP_SAVE: bp_codec.c bp_save.c ===================================== */
#define BP_STATE_MAX    320     /* encoded canonical state, bytes (cap) */
#define BP_SAVE_MAX     512     /* a save file, bytes (cap) */
typedef struct {
    BPU32  match_id, ckpt_id, rev, digest;
    BPU32  host_id, client_id;  /* endpoint identities */
    BPByte role;                /* this file's side: 0 local, 1 host, 2 client */
    BPByte state;               /* 0 none, 1 candidate, 2 committed */
} BPCkpt;
BPWord bp_crc16(const BPByte *p, unsigned n, BPWord crc);   /* CCITT-FALSE */
unsigned bp_encode(const BPState *s, BPByte *buf, unsigned cap);  /* 0 = no room */
int    bp_decode(const BPByte *buf, unsigned n, BPState *out);  /* + validate */
int    bp_validate(const BPState *s);             /* BP_OK or BP_EINVALID */
void   bp_digest(const BPState *s, BPU32 *out);   /* over bp_encode's bytes */
int    bp_save_local(const char *name, const BPState *s);
int    bp_load_local(const char *name, BPState *out);   /* out = scratch */
int    bp_ckpt_write(const BPCkpt *meta, const BPState *s); /* candidate */
int    bp_ckpt_commit(const BPCkpt *meta);
int    bp_ckpt_load(int role, BPCkpt *meta, BPState *out);  /* last committed */
/* the rules/content fingerprint the handshake and every file carry: a CRC
 * over the immutable board and card tables and BP_RULES_VER */
#define BP_RULES_VER    1
void   bp_fingerprint(BPU32 *out);
/* the save lane's additions (SPEC.md 131.7.1-131.7.4) */
#define BP_ENC_VER      1
#define BP_ENC_LEN      233     /* bp_encode's exact output length */
#define BP_FILE_HDR     40
int    bp_invalid_rule(void);                     /* rule of the last bp_validate refusal */
unsigned bp_file_build(const BPCkpt *meta, const BPState *s, BPByte *buf, unsigned cap);
int    bp_file_peek(const BPByte *buf, unsigned n, BPCkpt *meta);   /* header + CRC only */
int    bp_file_parse(const BPByte *buf, unsigned n, BPCkpt *meta, BPState *out);
/* which: 0 current committed, 1 previous committed, 2 candidate; out 0 = header only */
int    bp_ckpt_open(int role, int which, BPCkpt *meta, BPState *out);
#define BP_HAVE_CKPT_LOAD_ID 1
int    bp_ckpt_load_id(int role, const BPU32 *ckpt_id, BPCkpt *meta, BPState *out); /* committed or candidate; meta->state says which */
int    bp_ckpt_damaged(int role);                 /* slot files that exist but fail: 0..2 */

/* ===== BP_LINK: bp_link.c bp_cable.inc =================================== */
#define BP_MODE_LOCAL   0
#define BP_MODE_HOST    1
#define BP_MODE_CLIENT  2
/* link states (bp_link_state) */
#define BP_L_OFF        0
#define BP_L_SEARCH     1       /* port open, no peer */
#define BP_L_PEER       2       /* peer heard, handshake running */
#define BP_L_BADPEER    3       /* incompatible: bp_link_msg says why */
#define BP_L_LOBBY      4       /* compatible, roster being agreed */
#define BP_L_PLAY       5       /* match running, in sync */
#define BP_L_PAUSED     6       /* silence or resync: economic input frozen */
#define BP_L_RESYNC     7       /* snapshot transfer in progress */
extern BPState bp_game;         /* THE canonical match; only the session mutates it */
extern BPByte  bp_mode;         /* BP_MODE_* */
extern BPByte  bp_here;         /* this machine's index: 0 host/local, 1 client */
void   bp_sess_local(void);                       /* local mode, no link */
int    bp_sess_new(const BPSetup *su);            /* local/host: start the match */
int    bp_sess_submit(const BPCmd *c);            /* BP_OK applied, BP_EPENDING sent, BP_E* refused */
void   bp_sess_tick(void);                        /* every timer tick: serial, retries, timeouts */
int    bp_sess_mine(int seat);                    /* seat is played on this machine */
int    bp_sess_busy(void);                        /* a transaction is outstanding */
int    bp_link_state(void);
const char *bp_link_msg(void);                    /* one line for the status box */
int    bp_link_host(int port);                    /* port 0 automatic, 1 COM1, 2 COM2 */
int    bp_link_join(int port);
void   bp_link_leave(void);                       /* explicit abandon */
int    bp_lobby_set(const BPSetup *su);           /* host proposes roster/rules */
int    bp_lobby_ready(int ready);
int    bp_lobby_start(void);                      /* host, both ready */
const BPSetup *bp_lobby_setup(void);              /* the agreed (or proposed) setup */
int    bp_chat_send(const char *text);
int    bp_sess_save_pair(void);                   /* paired checkpoint, either side */
int    bp_sess_resume_pair(void);                 /* after both reopen */
int    bp_sess_load_local(const BPState *loaded); /* local mode: adopt a validated load */
/* the cable bridge (bp_cable.inc on the guest; hosttest/ supplies a fake).
 * bp_link.c reaches the UART ONLY through these, which is what lets the
 * whole protocol run in the host harness. Frames are apps/os88cable.inc's:
 * byte 0 the type, at most BP_CK_MAX body bytes before its CRC. */
#define BP_CK_MAX       30
int    bp_ck_open(int port);                      /* 0 auto, 1 COM1, 2 COM2; BP_OK or BP_EIO */
void   bp_ck_close(void);
void   bp_ck_tick(void);                          /* the port search, UI task */
int    bp_ck_heard(void);                         /* tell the search a peer answered */
int    bp_ck_send(const BPByte *body, unsigned n);/* BP_OK, or BP_EPENDING when the TX ring is full */
int    bp_ck_recv(BPByte *body);                  /* -> length of the next valid frame, 0 none */
int    bp_ck_port(void);                          /* the port held, 0 none */
const char *bp_ck_msg(void);                      /* the framer's own status line */
/* diagnostics (US-20) */
typedef struct { BPWord crc, retry, frame, overrun, resync, rx, tx, chatdrop; } BPDiag;
extern BPDiag bp_diag;
/* the session calls these; the UI (bp_app.c) defines them */
void   bp_ui_committed(const BPResult *r);        /* a revision was applied here */
void   bp_ui_link(void);                          /* link state/msg changed */
void   bp_ui_chat(int machine, const char *text);

/* ===== BP_FACE: bp_face.c ================================================= */
/* layouts picked from the content box (SPEC.md 131.8.1) */
#define BP_LAY_FULL     0       /* VGA 640x480-class */
#define BP_LAY_MID      1       /* Hercules 720x348-class */
#define BP_LAY_COMPACT  2       /* CGA 640x200-class */
typedef struct { int x, y, w, h; } BPRect;     /* SCREEN coordinates */
extern BPRect bp_r_board, bp_r_center, bp_r_side, bp_r_hint;
extern BPByte bp_layout, bp_mono;
extern BPByte bp_sel;           /* the selected space (presentation) */
/* face sections a change can dirty */
#define BP_D_BOARD      0x0001  /* every space cell */
#define BP_D_CENTER     0x0002  /* the centre (background + panel) */
#define BP_D_PLAYERS    0x0004
#define BP_D_DETAILS    0x0008
#define BP_D_LINK       0x0010
#define BP_D_LOG        0x0020
#define BP_D_HINT       0x0040
#define BP_D_ALL        0x007F
#define BP_D_FORCE      0x8000  /* with other bits: repaint them even if their
                                   signature says nothing changed */
int    bp_face_init(void *win, unsigned artseg, unsigned bandseg, unsigned bandcap, int bpp);
void   bp_face_layout(int w, int h);              /* content size changed */
void   bp_face_dirty(unsigned mask);
void   bp_face_dirty_space(int space);
void   bp_face_paint(void *win, int full);        /* full: the whole content */
int    bp_face_hit_space(int x, int y);           /* screen coords -> space, -1 */
void   bp_face_select(int space);
int    bp_face_key(int ascii, int scan);          /* board navigation; 1 = used */
/* the widget kit panels draw with - valid ONLY while bp_pn_draw runs */
#define BP_W_NORMAL     0
#define BP_W_FOCUS      1
#define BP_W_DISABLED   2
#define BP_W_DEFAULT    4
void   bp_w_panel(const BPRect *r, const char *title);
void   bp_w_box(const BPRect *r, int fill, int edge);
void   bp_w_button(const BPRect *r, const char *label, int state);
int    bp_w_text(int x, int y, const char *s, int colour, int font);  /* -> width */
int    bp_w_textw(const char *s, int font);
int    bp_w_fonth(int font);
int    bp_w_sprite(int x, int y, int art_id);     /* -> width, 0 absent */
unsigned bp_w_spritesz(int art_id);               /* w | h << 8 */
int    bp_face_isdirty(void);                     /* any section dirty (the timer repaints on it) */
void   bp_w_fill(int x1, int y1, int x2, int y2, int colour);
/* the log ring the sidebar shows (bounded) */
#define BP_LOG_LINES    8
#define BP_LOG_LEN      40
void   bp_log_add(const char *line);
const char *bp_log_line(int i);                   /* 0 newest */

/* ===== BP_PANELS: bp_panels.c ============================================ */
/* the in-game panels in the centre (SPEC.md 131.9.7-131.9.11): purchase,
 * auction, card, Repair Shop, debt, trade composer and review, collection /
 * manage, handoff, link pause, chat, diagnostics, rules/help, game over.
 * Driven by bp_game and the session; every decision becomes a BPCmd through
 * bp_sess_submit, never a write to bp_game. */
#define BP_GP_NONE      0       /* bp_gp_open's which: menu-opened panels */
#define BP_GP_MANAGE    1       /* collection: upgrade/sell/mortgage/redeem */
#define BP_GP_TRADE     2       /* compose a trade */
#define BP_GP_CHAT      3
#define BP_GP_DIAG      4
#define BP_GP_RULES     5
#define BP_GP_KEYS      6
#define BP_GP_STANDINGS 7
void   bp_gp_draw(void);                          /* into bp_r_center, per strip */
int    bp_gp_covers(void);                        /* 1 = panel hides the centre art */
int    bp_gp_key(int ascii, int scan);            /* 1 = consumed */
int    bp_gp_click(int x, int y);                 /* 1 = consumed */
const char *bp_gp_hint(int i);                    /* hint bar entries, 0 ends */
void   bp_gp_sync(void);                          /* game/link state changed */
void   bp_gp_open(int which);                     /* a menu asked for a panel */

/* ===== BP_APP: bp_app.c bp_setup.c ======================================= */
/* the application (SPEC.md 131.9.1-131.9.6): every os88_* callback, the
 * mode (title/setup, lobby, game), menus, keys, sound cues, files and the
 * close decision; bp_setup.c is the New Game screen (Same PC / Host Cable /
 * Join Cable, the roster, the rules) and the cable lobby, both drawn over
 * the WHOLE content. The face calls only the bp_pn_* door below; bp_app.c
 * routes it to the setup screens or to the game panels (bp_gp_*). */
#define BP_PN_CENTRE    1       /* bp_pn_covers: panel hides the centre art */
#define BP_PN_FULL      2       /* ...or the whole content (setup, lobby):
                                   the face then draws only bp_pn_draw, with
                                   bp_r_center set to the whole content */
void   bp_pn_draw(void);
int    bp_pn_covers(void);                        /* 0, BP_PN_CENTRE, BP_PN_FULL */
int    bp_pn_key(int ascii, int scan);
int    bp_pn_click(int x, int y);
const char *bp_pn_hint(int i);
void   bp_pn_sync(void);
/* sound cues (bp_app.c), honouring Options > Sound */
#define BP_SND_DICE     0
#define BP_SND_BUY      1
#define BP_SND_RENT     2
#define BP_SND_CARD     3
#define BP_SND_CRASH    4
#define BP_SND_BANKRUPT 5
#define BP_SND_WIN      6
#define BP_SND_ERROR    7
void   bp_sound(int cue);

#endif
