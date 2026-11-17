/* Byteopoly host harness: the save lane (SPEC.md 131.7). Builds the shipping
 * bp_codec.c and bp_save.c over a fake in-memory file system with fault
 * injection, and writes every file it makes to build/bpsave/ with a manifest
 * of the verdict the C gave, for tools/bpsave.py (the independent second
 * reader) to agree with. */
#include "bphost.h"
#include "byteoply.c"

/* ---- a fake os88 file layer: 8 files, whole-file, with faults -------------- */
#define OS88_FERR_OK      0
#define OS88_FERR_NOENT   4
#define OS88_FERR_FULL    6
#define OS88_FERR_DIRFULL 7
#define OS88_FERR_PROT    8
#define OS88_FERR_WPROT   9
#define OS88_FERR_BIG    10
#define FS_N 8
static struct { char name[16]; unsigned char data[1024]; unsigned len; int used; } fs[FS_N];
static int fs_ferr, fs_failw, fs_torn, fs_nwrites;
static int fs_find(const char *name)
{
    int i;
    for (i = 0; i < FS_N; i++) if (fs[i].used && !strcmp(fs[i].name, name)) return i;
    return -1;
}
int os88_ferr(void) { return fs_ferr; }
unsigned os88_file_read(const char *name, void *buf, unsigned cap)
{
    int i = fs_find(name);
    fs_ferr = 0;
    if (i < 0) { fs_ferr = OS88_FERR_NOENT; return 0; }
    if (fs[i].len > cap) { fs_ferr = OS88_FERR_BIG; return 0; }
    memcpy(buf, fs[i].data, fs[i].len);
    return fs[i].len;
}
int os88_file_write(const char *name, const void *buf, unsigned count)
{
    int i = fs_find(name);
    fs_ferr = 0; fs_nwrites++;
    if (fs_failw) { fs_ferr = fs_failw; return -1; }
    if (i < 0) {
        for (i = 0; i < FS_N && fs[i].used; i++) ;
        if (i == FS_N) { fs_ferr = OS88_FERR_DIRFULL; return -1; }
        fs[i].used = 1; strcpy(fs[i].name, name);
    }
    fs[i].len = fs_torn && count > 10 ? count - 10 : count;   /* a lying short write */
    memcpy(fs[i].data, buf, fs[i].len);
    return 0;
}
int os88_file_delete(const char *name)
{
    int i = fs_find(name);
    fs_ferr = 0;
    if (i < 0) { fs_ferr = OS88_FERR_NOENT; return -1; }
    fs[i].used = 0;
    return 0;
}
#include "bp_save.c"

/* ---- scenario states -------------------------------------------------------- */
static void u32(BPU32 *r, unsigned lo, unsigned hi) { r->lo = (BPWord)lo; r->hi = (BPWord)hi; }
static void base(BPState *s)
{
    int i, j;
    memset(s, 0, sizeof *s);
    s->status = BP_ST_PLAY; s->rules = BP_RULES_CLASSIC; s->nseats = 3;
    u32(&s->match_id, 0x1234, 0xABCD); u32(&s->rev, 41, 0);
    s->turn = 7; s->round = 2; s->round_left = 6;
    s->phase = BP_PH_MANAGE; s->active = 1; s->decider = 1;
    strcpy(s->name[0], "ALICE"); strcpy(s->name[1], "BOB"); strcpy(s->name[2], "CAROLINE");
    for (i = 0; i < 3; i++) { s->token[i] = (BPByte)i; s->alive[i] = 1; u32(&s->cash[i], 1500 - i * 100, 0); }
    s->pos[0] = 4; s->pos[1] = 11; s->pos[2] = 20;
    memset(s->owner, BP_NOBODY, BP_PROPS);
    for (i = 0; i < BP_DECKS; i++) {
        for (j = 0; j < BP_DECK_CARDS; j++) s->deck[i][j] = (BPByte)(i * 8 + j);
        s->deck_n[i] = 8;
    }
    s->card = BP_NOBODY; s->auc_space = BP_NOBODY; s->auc_high = BP_NOBODY; s->debt_to = BP_NOBODY;
    u32(&s->rng, 0x5A5A, 0x1357);
}
static void sc_manage(BPState *s)
{
    base(s);
    s->owner[0] = s->owner[1] = s->owner[2] = 0;           /* a full Commodore set, 2/1/1 */
    s->level[0] = 2; s->level[1] = 1; s->level[2] = 1;
    s->owner[4] = 1; s->mortgaged[4] = 1;                  /* a mortgaged TRS-80 */
    s->owner[3] = 2;                                       /* POWER */
    s->disks[2] = 1; s->deck_n[0] = 7; s->deck_top[0] = 3; /* deck 0's disk is held */
    { static const BPByte d[7] = {0, 1, 2, 3, 4, 5, 7}; memcpy(s->deck[0], d, 7); }
    s->doubles = 1; s->d1 = 3; s->d2 = 3; s->rolled = 1;
}
static void sc_repair(BPState *s)
{
    base(s); s->phase = BP_PH_REPAIR; s->detained[1] = 1; s->tries[1] = 2; s->pos[1] = 7;
}
static void sc_buy(BPState *s) { base(s); s->phase = BP_PH_BUY; s->pos[1] = 1; }
static void sc_auction(BPState *s)
{
    base(s); s->phase = BP_PH_AUCTION; s->auc_space = 0; s->auc_open = 5; s->auc_passed = 2;
    s->auc_turn = 2; s->auc_high = 0; u32(&s->auc_bid, 130, 0);
}
static void sc_card(BPState *s)
{
    base(s); s->phase = BP_PH_CARD; s->card = 3; s->deck_top[0] = 3; s->d1 = 2; s->d2 = 5; s->rolled = 1;
}
static void sc_debt(BPState *s)
{
    base(s); s->phase = BP_PH_DEBT; s->pos[1] = 7; s->detained[1] = 1; s->tries[1] = 3;
    s->d1 = 2; s->d2 = 3; s->rolled = 1; s->pending_move = 1; s->ret_phase = BP_PH_REPAIR;
    u32(&s->debt_amount, 50, 0); u32(&s->cash[1], 20, 0);
}
static void sc_trade(BPState *s)
{
    base(s); s->phase = BP_PH_TRADE; s->ret_phase = BP_PH_AFTER; s->active = 0; s->decider = 1;
    s->owner[0] = 0; s->owner[5] = 1; s->disks[0] = 2; s->deck_n[1] = 7; s->deck_top[1] = 2;
    { static const BPByte d[7] = {8, 9, 10, 11, 12, 13, 14}; memcpy(s->deck[1], d, 7); }
    s->offer_id = 9; s->offer_from = 0; s->offer_to = 1; s->offer_disks = 0x02; s->offer_payer = 1;
    u32(&s->offer_give, 1, 0); u32(&s->offer_get, 2, 0); u32(&s->offer_cash, 45, 0);
    s->owner[1] = 1;                                       /* prop 1 offered by 1 */
}
static void sc_after(BPState *s) { base(s); s->phase = BP_PH_AFTER; s->d1 = 4; s->d2 = 4; s->doubles = 1; s->rolled = 1; }
static void sc_over(BPState *s)
{
    base(s); s->status = BP_ST_OVER; s->phase = BP_PH_OVER; s->rules = BP_RULES_QUICK;
    s->rounds_limit = 10; s->round = 10; s->winners = 5; s->alive[1] = 0;
    u32(&s->score[0], 3210, 0); u32(&s->score[2], 3210, 0); u32(&s->cash[1], 0, 0);
}
typedef void (*scfn)(BPState *);
static const scfn scs[] = {sc_manage, sc_repair, sc_buy, sc_auction, sc_card, sc_debt, sc_trade, sc_after, sc_over};
static const char *scn[] = {"manage", "repair", "buy", "auction", "card", "debt", "trade", "after", "over"};
#define NSC 9

/* ---- negative mutations: (rule, variant) -> an invalid state ---------------- */
static int mut(int rule, int v, BPState *s)
{
    switch (rule) {
    case 1: sc_manage(s);
        switch (v) { case 0: s->status = 0; break; case 1: s->rules = 2; break;
        case 2: s->nseats = 1; break; case 3: s->nseats = 5; break;
        case 4: s->rounds_limit = 5; break;
        case 5: s->rules = 1; s->rounds_limit = 15; break;
        case 6: s->rules = 1; s->rounds_limit = 10; s->round = 11; break; default: return 0; } break;
    case 2: sc_manage(s);
        switch (v) { case 0: s->phase = 0; break; case 1: s->phase = 10; break;
        case 2: s->status = BP_ST_OVER; s->winners = 1; break;
        case 3: s->phase = BP_PH_OVER; break; default: return 0; } break;
    case 3: sc_manage(s);
        switch (v) { case 0: s->alive[0] = 2; break; case 1: s->alive[3] = 1; break;
        case 2: s->alive[0] = s->alive[2] = 0; break; case 3: s->active = 3; break;
        case 4: s->alive[1] = 0; s->active = 0; s->decider = 1; break;
        case 5: s->round_left = 8; break; case 6: s->alive[2] = 0; s->round_left = 4; s->cash[2].lo = 0; s->owner[3] = BP_NOBODY; s->disks[2] = 0; s->deck_n[0] = 8;
            { static const BPByte d[8] = {0, 1, 2, 3, 4, 5, 6, 7}; memcpy(s->deck[0], d, 8); } break;
        default: return 0; } break;
    case 4: sc_manage(s);
        switch (v) { case 0: s->name[0][0] = 0; break; case 1: s->name[0][1] = 7; break;
        case 2: s->name[1][5] = 'x'; break; case 3: s->token[1] = 0; break;
        case 4: s->token[2] = 4; break; case 5: s->machine[0] = 2; break;
        case 6: s->token[3] = 1; break; case 7: s->name[0][8] = 'z'; break; default: return 0; } break;
    case 5: sc_manage(s);
        switch (v) { case 0: s->pos[0] = 28; break; case 1: s->detained[0] = 1; break;
        case 2: s->tries[0] = 4; s->detained[0] = 1; s->pos[0] = 7; break;
        case 3: s->tries[0] = 1; break; case 4: s->disks[0] = 4; break;
        case 5: s->cash[0].hi = 0x4000; break;
        case 6: s->detained[0] = 2; s->pos[0] = 7; break; default: return 0; } break;
    case 6: sc_manage(s);
        switch (v) { case 0: s->owner[9] = 3; break; case 1: s->level[9] = 6; s->owner[9] = 0; break;
        case 2: s->mortgaged[9] = 2; s->owner[9] = 0; break; case 3: s->level[9] = 1; break;
        case 4: s->mortgaged[9] = 1; break; default: return 0; } break;
    case 7: sc_manage(s);
        switch (v) { case 0: s->level[3] = 1; break;
        case 1: s->level[4] = 1; s->mortgaged[4] = 0; break;           /* an incomplete set */
        case 2: s->mortgaged[2] = 1; break;                            /* mortgage in a levelled set */
        case 3: s->level[0] = 3; break;                                /* spread 2 */
        case 4: s->owner[2] = 1; break;                                /* split owners */
        default: return 0; } break;
    case 8: sc_manage(s);
        switch (v) { case 0: s->d1 = 7; break; case 1: s->d1 = 0; break;
        case 2: s->d1 = s->d2 = 0; break; case 3: s->doubles = 3; break;
        case 4: s->pending_move = 2; break; case 5: s->pending_move = 1; break;
        case 6: s->rolled = 2; break; default: return 0; } break;
    case 9: sc_manage(s);
        switch (v) { case 0: s->disks[0] = 1; break;                   /* two holders */
        case 1: s->deck_n[1] = 7; break; case 2: s->deck_top[0] = 7; break;
        case 3: s->deck[1][1] = 8; break; case 4: s->deck[1][0] = 3; break;
        case 5: s->deck[0][6] = 6; break;                              /* held disk in cycle */
        case 6: s->card = 16; break; case 7: s->deck_n[0] = 9; break; default: return 0; } break;
    case 10:
        switch (v) { case 0: sc_repair(s); s->detained[1] = 0; s->tries[1] = 0; break;
        case 1: sc_buy(s); s->pos[1] = 2; break; case 2: sc_buy(s); s->owner[0] = 0; break;
        case 3: sc_buy(s); s->pos[1] = 0; break; default: return 0; } break;
    case 11: sc_auction(s);
        switch (v) { case 0: s->auc_space = 18; break; case 1: s->owner[0] = 1; break;
        case 2: s->auc_open = 7; break; case 3: s->auc_open = 4; break;
        case 4: s->auc_open = 0; s->auc_passed = 7; break; case 5: s->auc_turn = 1; break;
        case 6: s->auc_high = BP_NOBODY; break; case 7: s->auc_bid.lo = 135; break;
        case 8: s->auc_bid.lo = 0; break; case 9: s->auc_bid.lo = 1600; break;
        case 10: s->auc_high = 1; break; case 11: s->auc_bid.hi = 1; break;
        default: return 0; } break;
    case 12: sc_card(s);
        switch (v) { case 0: s->card = 6; s->disks[0] = 1; s->deck_n[0] = 7;      /* the shown card is held */
            { static const BPByte d[7] = {0, 1, 2, 3, 4, 5, 7}; memcpy(s->deck[0], d, 7); } break;
        case 1: s->card = 5; s->pos[1] = 2; s->rolled = 0; s->d1 = s->d2 = 0; break;   /* forward 3 onto POWER */
        case 2: s->card = 4; s->rolled = 0; s->d1 = s->d2 = 0; break;                  /* to the BBS */
        case 3: s->card = 13; s->pos[1] = 19; s->rolled = 0; s->d1 = s->d2 = 0; break; /* back 3 onto BBS */
        default: return 0; } break;
    case 13: sc_debt(s);
        switch (v) { case 0: s->debt_amount.lo = 0; s->pending_move = 0; break;
        case 1: s->debt_to = 1; break; case 2: s->ret_phase = BP_PH_AUCTION; break;
        case 3: s->debt_to = 7; break; case 4: s->debt_to = 2; s->alive[2] = 0; s->cash[2].lo = 0; s->round_left = 2; break;
        default: return 0; } break;
    case 14: sc_trade(s);
        switch (v) { case 0: s->offer_from = 1; break; case 1: s->offer_give.lo = 1 << 7; break;
        case 2: s->offer_give.lo = s->offer_get.lo = s->offer_cash.lo = 0; s->offer_disks = 0; break;
        case 3: s->offer_to = 0; break; case 4: s->offer_disks = 0x20; break;
        case 5: s->ret_phase = BP_PH_BUY; break; case 6: s->offer_payer = 2; break;
        case 7: s->offer_get.hi = 4; break; case 8: s->offer_to = 3; break; default: return 0; } break;
    case 15:
        switch (v) { case 0: sc_manage(s); s->winners = 1; break;
        case 1: sc_over(s); s->winners = 0; break; case 2: sc_over(s); s->winners = 8; break;
        default: return 0; } break;
    default: return 0;
    }
    return 1;
}

/* ---- file helpers ------------------------------------------------------------ */
static BPByte img[BP_SAVE_MAX + 16];
static BPByte img2[BP_SAVE_MAX + 16];
static BPCkpt cm, cm2;
static BPState st, st2, scratch;
static FILE *manifest;
static void fixcrc(BPByte *b, unsigned n)
{
    unsigned c = bp_crc16(b, n - 2, 0xFFFF);
    b[n - 2] = (BPByte)c; b[n - 1] = (BPByte)(c >> 8);
}
static void emit(const char *name, const BPByte *b, unsigned n, int expect, int rule)
{
    char path[96];
    FILE *f;
    snprintf(path, sizeof path, "build/bpsave/%s.bps", name);
    f = fopen(path, "wb");
    if (f) { fwrite(b, 1, n, f); fclose(f); }
    if (manifest) fprintf(manifest, "%s %d %d\n", name, expect, rule);
}
static unsigned build_local(const BPState *s, BPByte *b)
{
    memset(&cm, 0, sizeof cm); cm.role = 0; cm.state = 2;
    return bp_file_build(&cm, s, b, sizeof img);
}
static int parse_file(const BPByte *b, unsigned n)
{
    return bp_file_parse(b, n, &cm2, &scratch);
}

int main(void)
{
    int i, v, r, n_neg = 0;
    unsigned n, k;
    BPU32 d1, d2, fp;
    static BPByte enc[BP_STATE_MAX], enc2[BP_STATE_MAX];
    static char nm[48];

    if (system("mkdir -p build/bpsave") != 0) printf("note: no build/bpsave\n");
    manifest = fopen("build/bpsave/manifest.txt", "w");

    /* CRC-16/CCITT-FALSE check value and the digest's separate halves */
    BP_CHECK(bp_crc16((const BPByte *)"123456789", 9, 0xFFFF) == 0x29B1, "crc16 check value");
    bp_fingerprint(&fp);
    BP_CHECK(fp.lo || fp.hi, "fingerprint non-zero");
    if (manifest) fprintf(manifest, "#fingerprint %04x%04x\n", fp.hi, fp.lo);

    /* every phase: valid, round trip, digest, file image */
    for (i = 0; i < NSC; i++) {
        scs[i](&st);
        r = bp_validate(&st);
        if (r) printf("scenario %s rule %d\n", scn[i], bp_invalid_rule());
        BP_CHECK(r == BP_OK, scn[i]);
        n = bp_encode(&st, enc, sizeof enc);
        BP_CHECK(n == BP_ENC_LEN, "encode length 233");
        BP_CHECK(bp_encode(&st, enc, 232) == 0, "encode with no room");
        r = bp_decode(enc, n, &scratch);
        BP_CHECK(r == BP_OK, "decode");
        bp_encode(&scratch, enc2, sizeof enc2);
        BP_CHECK(!memcmp(enc, enc2, n), "round trip bytes");
        BP_CHECK(scratch.phase == st.phase && scratch.rev.lo == st.rev.lo
                 && !strcmp(scratch.name[2], st.name[2]), "round trip fields");
        bp_digest(&st, &d1); bp_digest(&scratch, &d2);
        BP_CHECK(d1.lo == d2.lo && d1.hi == d2.hi, "equal states, equal digest");
        n = build_local(&st, img);
        BP_CHECK(n == BP_FILE_HDR + BP_ENC_LEN + 2, "file length 275");
        r = parse_file(img, n);
        BP_CHECK(r == BP_OK, "file parse");
        BP_CHECK(cm2.digest.lo == d1.lo && cm2.digest.hi == d1.hi && cm2.role == 0 && cm2.state == 2,
                 "header digest");
        snprintf(nm, sizeof nm, "good_%s", scn[i]);
        emit(nm, img, n, 0, 0);
    }
    /* a cable-shaped file: roles 1 and 2 need seats on both machines */
    sc_manage(&st); st.machine[1] = 1; st.machine[2] = 1;
    memset(&cm, 0, sizeof cm); cm.role = 1; cm.state = 1; u32(&cm.ckpt_id, 7, 0);
    u32(&cm.host_id, 0xAAAA, 1); u32(&cm.client_id, 0xBBBB, 2);
    n = bp_file_build(&cm, &st, img, sizeof img);
    BP_CHECK(parse_file(img, n) == BP_OK && cm2.role == 1 && cm2.state == 1 && cm2.ckpt_id.lo == 7
             && cm2.client_id.hi == 2, "cable file");
    emit("good_cable_host_candidate", img, n, 0, 0);
    cm.role = 2; cm.state = 2; n = bp_file_build(&cm, &st, img, sizeof img);
    BP_CHECK(parse_file(img, n) == BP_OK, "cable client file");
    emit("good_cable_client", img, n, 0, 0);
    sc_manage(&st);                                  /* all machine 0 in a cable file */
    cm.role = 1; n = bp_file_build(&cm, &st, img, sizeof img);
    BP_CHECK(parse_file(img, n) == BP_EMATCH, "cable file needs both machines");
    emit("bad_cable_one_machine", img, n, BP_EMATCH, 0);
    sc_manage(&st); st.machine[1] = 1;
    n = build_local(&st, img);
    BP_CHECK(parse_file(img, n) == BP_EMATCH, "local file with a remote seat");
    emit("bad_local_remote_seat", img, n, BP_EMATCH, 0);
    memset(&cm, 0, sizeof cm); cm.role = 3; cm.state = 2; sc_manage(&st);
    BP_CHECK(bp_file_build(&cm, &st, img, sizeof img) == 0, "build refuses a bad role");
    BP_CHECK(bp_file_build(&cm, &st, img, 100) == 0, "build refuses no room");

    /* damaged files */
    sc_auction(&st); n = build_local(&st, img);
    memcpy(img2, img, n); img2[100] ^= 0x10;
    BP_CHECK(parse_file(img2, n) == BP_ECRC, "flipped payload bit"); emit("bad_crc", img2, n, BP_ECRC, 0);
    memcpy(img2, img, n); img2[n - 1] ^= 1;
    BP_CHECK(parse_file(img2, n) == BP_ECRC, "flipped crc"); emit("bad_crc_trailer", img2, n, BP_ECRC, 0);
    BP_CHECK(parse_file(img, n - 1) == BP_EFORMAT, "one byte short");
    emit("bad_truncated1", img, n - 1, BP_EFORMAT, 0);
    BP_CHECK(parse_file(img, n - 60) == BP_EFORMAT, "truncated payload");
    emit("bad_truncated60", img, n - 60, BP_EFORMAT, 0);
    BP_CHECK(parse_file(img, 20) == BP_EFORMAT && parse_file(img, 0) == BP_EFORMAT, "tiny");
    emit("bad_tiny", img, 20, BP_EFORMAT, 0);
    memcpy(img2, img, n); img2[n] = 0;
    BP_CHECK(parse_file(img2, n + 1) == BP_EFORMAT, "trailing byte"); emit("bad_trailing", img2, n + 1, BP_EFORMAT, 0);
    memcpy(img2, img, n); img2[0] = 'X';
    BP_CHECK(parse_file(img2, n) == BP_EFORMAT, "bad magic"); emit("bad_magic", img2, n, BP_EFORMAT, 0);
    memcpy(img2, img, n); img2[4] = 2;
    BP_CHECK(parse_file(img2, n) == BP_EVERSION, "unsupported file version"); emit("bad_fileversion", img2, n, BP_EVERSION, 0);
    memcpy(img2, img, n); img2[8] = 9; fixcrc(img2, n);
    BP_CHECK(parse_file(img2, n) == BP_EVERSION, "other rules version"); emit("bad_rulesversion", img2, n, BP_EVERSION, 0);
    memcpy(img2, img, n); img2[10] ^= 1; fixcrc(img2, n);
    BP_CHECK(parse_file(img2, n) == BP_EMATCH, "wrong fingerprint"); emit("bad_fingerprint", img2, n, BP_EMATCH, 0);
    memcpy(img2, img, n); img2[5] = 5; fixcrc(img2, n);
    BP_CHECK(parse_file(img2, n) == BP_EFORMAT, "bad role"); emit("bad_role", img2, n, BP_EFORMAT, 0);
    memcpy(img2, img, n); img2[6] = 0; fixcrc(img2, n);
    BP_CHECK(parse_file(img2, n) == BP_EFORMAT, "bad state"); emit("bad_state", img2, n, BP_EFORMAT, 0);
    memcpy(img2, img, n); img2[26] ^= 0xFF; fixcrc(img2, n);
    BP_CHECK(parse_file(img2, n) == BP_EFORMAT, "header digest lies"); emit("bad_digest_header", img2, n, BP_EFORMAT, 0);
    memcpy(img2, img, n); img2[22] ^= 1; fixcrc(img2, n);
    BP_CHECK(parse_file(img2, n) == BP_EFORMAT, "header rev lies"); emit("bad_rev_header", img2, n, BP_EFORMAT, 0);
    memcpy(img2, img, n); img2[BP_FILE_HDR] = 2; fixcrc(img2, n);
    BP_CHECK(parse_file(img2, n) == BP_EVERSION, "unsupported encoding version"); emit("bad_encversion", img2, n, BP_EVERSION, 0);
    memcpy(img2, img, n); img2[38] = 0xE8; img2[39] = 0x00; fixcrc(img2, n);
    BP_CHECK(parse_file(img2, n) == BP_EFORMAT, "length field disagrees"); emit("bad_length", img2, n, BP_EFORMAT, 0);

    /* every validation rule has a negative, through the whole file path */
    for (r = 1; r <= 15; r++) {
        for (v = 0; v < 16; v++) {
            if (!mut(r, v, &st)) break;
            BP_CHECK(bp_validate(&st) == BP_EINVALID, "mutation is refused");
            if (bp_invalid_rule() != r) printf("mutation %d.%d reported rule %d\n", r, v, bp_invalid_rule());
            BP_CHECK(bp_invalid_rule() == r, "the right rule");
            if (r == 4 && (v == 2 || v == 7)) continue;     /* the encoder cannot carry these */
            bp_encode(&st, enc, sizeof enc);
            BP_CHECK(bp_decode(enc, BP_ENC_LEN, &scratch) == BP_EINVALID, "decode refuses");
            memset(&cm, 0, sizeof cm); cm.role = 0; cm.state = 2;
            n = bp_file_build(&cm, &st, img, sizeof img);      /* the build does not validate */
            BP_CHECK(parse_file(img, n) == BP_EINVALID, "CRC-correct, invalid: refused");
            snprintf(nm, sizeof nm, "bad_rule%d_%d", r, v);
            emit(nm, img, n, BP_EINVALID, r);
            n_neg++;
        }
        BP_CHECK(v > 0, "rule has a negative case");
    }
    /* ...which a hostile file can: bytes past a name's NUL, in the payload itself */
    sc_manage(&st); n = build_local(&st, img);
    memcpy(img2, img, n); img2[BP_FILE_HDR + 21 + 8 + 5] = 'x'; fixcrc(img2, n);
    BP_CHECK(parse_file(img2, n) == BP_EINVALID && bp_invalid_rule() == 4, "name bytes past the NUL");
    emit("bad_rule4_nulpad", img2, n, BP_EINVALID, 4);
    printf("t_save: %d invalid-content cases\n", n_neg);
    sc_manage(&st);
    BP_CHECK(bp_validate(&st) == BP_OK && bp_invalid_rule() == 0, "valid clears the rule");
    memset(&st, 0, sizeof st);
    BP_CHECK(bp_validate(&st) == BP_EINVALID, "empty match is not savable");
    BP_CHECK(bp_decode(enc, 0, &scratch) == BP_EFORMAT, "empty buffer");
    enc[0] = 9;
    BP_CHECK(bp_decode(enc, BP_ENC_LEN, &scratch) == BP_EVERSION, "encoding version");
    BP_CHECK(bp_decode(enc, 100, &scratch) == BP_EVERSION, "version beats length");
    enc[0] = 1;
    BP_CHECK(bp_decode(enc, 100, &scratch) == BP_EFORMAT, "short encoding");

    /* the digest: stable, and sensitive to a one-field change */
    sc_manage(&st); bp_digest(&st, &d1);
    st.name[0][6] = 'q'; st.name[0][5] = 0;           /* garbage after the NUL is not state */
    bp_digest(&st, &d2);
    BP_CHECK(d1.lo == d2.lo && d1.hi == d2.hi, "bytes past the NUL do not move the digest");
    sc_manage(&st);
    for (i = 0; i < 12; i++) {
        sc_manage(&st2);
        switch (i) {
        case 0: st2.cash[0].lo++; break; case 1: st2.rev.lo++; break; case 2: st2.rng.hi ^= 1; break;
        case 3: st2.level[1] = 0; st2.level[2] = 0; st2.level[0] = 1; break;
        case 4: st2.phase = BP_PH_AFTER; break; case 5: st2.pos[2]++; break;
        case 6: st2.name[1][0] = 'Z'; break; case 7: st2.deck[1][7] ^= 0; st2.deck[1][0] = 9; st2.deck[1][1] = 8; break;
        case 8: st2.d1 = 1; break; case 9: st2.score[3].hi = 1; break;
        case 10: st2.turn++; break; default: st2.winners = 0; st2.depth = 1; break;
        }
        bp_digest(&st2, &d2);
        BP_CHECK(d1.lo != d2.lo || d1.hi != d2.hi, "one-field change moves the digest");
    }
    bp_encode(&st, enc, sizeof enc);
    for (k = 0; k < BP_ENC_LEN; k++) {                  /* every single-bit flip of the encoding */
        enc[k] ^= 1u << (k & 7);
        bpc_digest_bytes(enc, BP_ENC_LEN, &d2);
        enc[k] ^= 1u << (k & 7);
        if (d1.lo == d2.lo && d1.hi == d2.hi) { BP_CHECK(0, "a bit flip kept the digest"); break; }
    }

    /* ---- the guest file half over the fake file system ---------------------- */
    memset(fs, 0, sizeof fs); fs_failw = 0; fs_torn = 0; fs_ferr = 0;
    sc_debt(&st);
    BP_CHECK(bp_save_local("GAME.BPS", &st) == BP_OK, "save local");
    memset(&scratch, 0x55, sizeof scratch);
    BP_CHECK(bp_load_local("GAME.BPS", &scratch) == BP_OK, "load local");
    bp_encode(&st, enc, sizeof enc); bp_encode(&scratch, enc2, sizeof enc2);
    BP_CHECK(!memcmp(enc, enc2, BP_ENC_LEN), "local save exact: pending_move, debt and all");
    BP_CHECK(scratch.pending_move == 1 && scratch.debt_amount.lo == 50, "debt preserved");
    BP_CHECK(bp_load_local("NOPE.BPS", &scratch) == BP_EIO && os88_ferr() == OS88_FERR_NOENT, "missing file");
    /* disk full / write protected: the previous save survives */
    sc_manage(&st2);
    fs_failw = OS88_FERR_FULL;
    BP_CHECK(bp_save_local("GAME.BPS", &st2) == BP_EFULL, "disk full");
    fs_failw = OS88_FERR_WPROT;
    BP_CHECK(bp_save_local("GAME.BPS", &st2) == BP_EFULL, "write protected");
    fs_failw = OS88_FERR_DIRFULL;
    BP_CHECK(bp_save_local("OTHER.BPS", &st2) == BP_EFULL, "directory full");
    fs_failw = 2;
    BP_CHECK(bp_save_local("GAME.BPS", &st2) == BP_EIO, "i/o error");
    fs_failw = 0;
    memset(&scratch, 0, sizeof scratch);
    BP_CHECK(bp_load_local("GAME.BPS", &scratch) == BP_OK && scratch.phase == BP_PH_DEBT, "prior save kept");
    fs_torn = 1;
    BP_CHECK(bp_save_local("TORN.BPS", &st2) == BP_EIO, "short write caught by the read-back");
    fs_torn = 0;
    { BPState bad; sc_manage(&bad); bad.owner[3] = 7;
      BP_CHECK(bp_save_local("BAD.BPS", &bad) == BP_EINVALID && fs_find("BAD.BPS") < 0, "invalid state is not written"); }
    /* a damaged file on disk: the scratch result is an error, a sentinel state is untouched */
    i = fs_find("GAME.BPS"); fs[i].data[60] ^= 4;
    BP_CHECK(bp_load_local("GAME.BPS", &scratch) == BP_ECRC, "load: bad crc");
    fs[i].data[60] ^= 4; fs[i].len -= 5;
    BP_CHECK(bp_load_local("GAME.BPS", &scratch) == BP_EFORMAT, "load: truncated");
    fs[i].len += 5;
    BP_CHECK(bp_load_local("GAME.BPS", &scratch) == BP_OK, "load: restored");
    fs[i].len = 0;
    BP_CHECK(bp_load_local("GAME.BPS", &scratch) == BP_EFORMAT, "load: empty file");
    fs[i].len = 900;
    BP_CHECK(bp_load_local("GAME.BPS", &scratch) == BP_EFORMAT, "load: oversized file");
    /* a cable file is not a local save */
    memset(&cm, 0, sizeof cm); cm.role = 1; cm.state = 2; u32(&cm.ckpt_id, 1, 0);
    sc_manage(&st); st.machine[1] = 1;
    n = bp_file_build(&cm, &st, img, sizeof img);
    os88_file_write("CABLE.BPS", img, n);
    BP_CHECK(bp_load_local("CABLE.BPS", &scratch) == BP_EMATCH, "cable checkpoint is not a local save");

    /* ---- checkpoints --------------------------------------------------------- */
    memset(fs, 0, sizeof fs);
    {
        static BPCkpt m, got;
        static BPState g;
        int role, cyc;
        for (role = 1; role <= 2; role++) {
            memset(fs, 0, sizeof fs);
            BP_CHECK(bp_ckpt_open(role, 0, &got, 0) == BP_EIO, "nothing yet");
            for (cyc = 1; cyc <= 4; cyc++) {
                sc_manage(&st); st.machine[1] = 1; st.rev.lo = (BPWord)(100 + cyc);
                st.cash[0].lo = (BPWord)(1000 + cyc);
                memset(&m, 0, sizeof m); m.role = (BPByte)role; m.match_id = st.match_id; m.rev = st.rev;
                u32(&m.ckpt_id, cyc, 0); u32(&m.host_id, 1, 1); u32(&m.client_id, 2, 2);
                BP_CHECK(bp_ckpt_write(&m, &st) == BP_OK, "ckpt write");
                BP_CHECK(bp_ckpt_open(role, 2, &got, 0) == BP_OK && got.state == 1 && got.ckpt_id.lo == cyc,
                         "candidate visible");
                if (cyc == 1) BP_CHECK(bp_ckpt_load(role, &got, &g) == BP_EMATCH, "candidate alone is not a pair");
                else { BP_CHECK(bp_ckpt_load(role, &got, &g) == BP_OK && got.ckpt_id.lo == cyc - 1,
                               "current stays the last committed");
                       BP_CHECK(g.cash[0].lo == 1000 + cyc - 1, "its state"); }
                BP_CHECK(bp_ckpt_commit(&m) == BP_OK, "ckpt commit");
                BP_CHECK(bp_ckpt_commit(&m) == BP_OK, "commit is idempotent");
                BP_CHECK(bp_ckpt_load(role, &got, &g) == BP_OK && got.ckpt_id.lo == cyc && got.state == 2
                         && got.role == role && g.cash[0].lo == 1000 + cyc, "current is the new one");
                if (cyc > 1) BP_CHECK(bp_ckpt_open(role, 1, &got, &g) == BP_OK && got.ckpt_id.lo == cyc - 1
                                      && g.cash[0].lo == 1000 + cyc - 1, "previous pair kept");
                else BP_CHECK(bp_ckpt_open(role, 1, &got, 0) == BP_EIO, "no previous yet");
                BP_CHECK(bp_ckpt_open(role, 2, &got, 0) == BP_EIO, "no candidate after commit");
            }
            { BPU32 want; u32(&want, 4, 0);
              BP_CHECK(bp_ckpt_load_id(role, &want, &got, &g) == BP_OK && got.state == 2 && got.ckpt_id.lo == 4, "load by id: committed");
              u32(&want, 3, 0);
              BP_CHECK(bp_ckpt_load_id(role, &want, &got, &g) == BP_OK && got.ckpt_id.lo == 3, "load by id: previous");
              u32(&want, 8, 0);
              BP_CHECK(bp_ckpt_load_id(role, &want, &got, &g) == BP_EIO, "load by id: unknown"); }
            /* a failed paired save keeps the previous completed pair */
            sc_manage(&st); st.machine[1] = 1; st.rev.lo = 105;
            m.rev = st.rev; u32(&m.ckpt_id, 5, 0);
            fs_failw = OS88_FERR_FULL;
            BP_CHECK(bp_ckpt_write(&m, &st) == BP_EFULL, "ckpt write: disk full");
            fs_failw = 0;
            BP_CHECK(bp_ckpt_load(role, &got, &g) == BP_OK && got.ckpt_id.lo == 4, "current intact after the failure");
            BP_CHECK(bp_ckpt_open(role, 1, &got, &g) == BP_OK && got.ckpt_id.lo == 3, "previous intact after the failure");
            /* a failed commit leaves the candidate and the committed pair */
            BP_CHECK(bp_ckpt_write(&m, &st) == BP_OK, "ckpt write 5");
            fs_failw = OS88_FERR_WPROT;
            BP_CHECK(bp_ckpt_commit(&m) == BP_EFULL, "ckpt commit: write protected");
            fs_failw = 0;
            BP_CHECK(bp_ckpt_load(role, &got, &g) == BP_OK && got.ckpt_id.lo == 4, "still current 4");
            BP_CHECK(bp_ckpt_open(role, 2, &got, 0) == BP_OK && got.ckpt_id.lo == 5, "candidate 5 survives");
            { BPU32 want; u32(&want, 5, 0);
              BP_CHECK(bp_ckpt_load_id(role, &want, &got, &g) == BP_OK && got.state == 1, "load by id: candidate"); }
            m.digest.lo = 1;
            BP_CHECK(bp_ckpt_write(&m, &st) == BP_EMATCH, "meta digest must be the state's");
            m.digest.lo = 0;
            /* a commit for an id nobody wrote, another match, or a torn candidate */
            u32(&m.ckpt_id, 9, 0);
            BP_CHECK(bp_ckpt_commit(&m) == BP_EMATCH, "commit for an unknown candidate");
            u32(&m.ckpt_id, 5, 0); m.match_id.lo ^= 1;
            BP_CHECK(bp_ckpt_commit(&m) == BP_EMATCH, "commit for another match");
            m.match_id.lo ^= 1;
            /* write-time argument errors */
            m.rev.lo ^= 1; BP_CHECK(bp_ckpt_write(&m, &st) == BP_EMATCH, "meta must describe the state");
            m.rev.lo ^= 1; m.role = 0; BP_CHECK(bp_ckpt_write(&m, &st) == BP_EMATCH, "no local role"); m.role = (BPByte)role;
            /* the other role never sees this role's files */
            BP_CHECK(bp_ckpt_load(3 - role, &got, &g) == BP_EIO, "the other role has none");
            /* a new match purges the old match's slots */
            sc_manage(&st2); st2.machine[1] = 1; st2.match_id.lo = 0x7777; st2.rev.lo = 1;
            memset(&m, 0, sizeof m); m.role = (BPByte)role; m.match_id = st2.match_id; m.rev = st2.rev;
            u32(&m.ckpt_id, 1, 0);
            BP_CHECK(bp_ckpt_write(&m, &st2) == BP_OK, "new match candidate");
            BP_CHECK(bp_ckpt_load(role, &got, &g) == BP_EMATCH, "old match purged: candidate alone");
            BP_CHECK(bp_ckpt_commit(&m) == BP_OK && bp_ckpt_load(role, &got, &g) == BP_OK
                     && got.match_id.lo == 0x7777, "new match committed");
            /* a corrupt current is reported, and load never falls back */
            sc_manage(&st); st.machine[1] = 1; st.match_id.lo = 0x7777; st.rev.lo = 2;
            m.rev = st.rev; u32(&m.ckpt_id, 2, 0);
            BP_CHECK(bp_ckpt_write(&m, &st) == BP_OK && bp_ckpt_commit(&m) == BP_OK, "ckpt 2");
            for (i = 0; i < FS_N; i++)                 /* damage the slot holding ckpt 2 */
                if (fs[i].used && fs[i].data[18] == 2 && fs[i].data[6] == 2) fs[i].data[70] ^= 1;
            BP_CHECK(bp_ckpt_damaged(role) == 1, "a damaged slot is reported");
            BP_CHECK(bp_ckpt_load(role, &got, &g) == BP_OK && got.ckpt_id.lo == 1,
                     "the damaged one ranks as missing; the exchange must still agree on the id");
            BP_CHECK(bp_ckpt_open(role, 1, &got, &g) == BP_ECRC, "no previous behind it: the damaged slot says why");
        }
    }
    if (manifest) fclose(manifest);

    /* the second reader (tools/bpsave.py) must agree with every verdict above */
    {
        FILE *f = fopen("tools/bpsave.py", "r");
        if (f) {
            fclose(f);
            r = system("python3 -I tools/bpsave.py --manifest build/bpsave/manifest.txt");
            BP_CHECK(r == 0, "tools/bpsave.py agrees with the C codec");
        } else printf("note: tools/bpsave.py not found from here; run from the repo root\n");
    }
    printf("t_save: %s\n", bp_host_fail ? "FAILED" : "ok");
    return bp_host_fail ? 1 : 0;
}
