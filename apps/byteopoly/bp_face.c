/* Byteopoly - the face: layouts, the board, the sidebar, the widget kit
 * (SPEC.md 131.8). Lane: face. Drawing goes through Breadboard XT's band
 * core (apps/breadxt/draw.inc, shared as source, SPEC.md 130.6.2): every
 * repaint is strips, each one band composed and put down with one call.
 *
 * Every rect the face paints has an x and a width that are multiples of 8
 * (the band's contract) and every pixel of it is written by the section that
 * owns it, so a strip never carries stale band bytes to the glass. A section
 * is repainted only when its signature (a hash of every input it draws)
 * changed, or when it was dirtied with BP_D_FORCE. */
#include "art.h"

int bx_rows(int x, int y, int w, int h);
void bx_new(int x, int y, int w, int h, int colour);
void bx_fill(int x1, int y1, int x2, int y2, int colour);
void bx_dither(int x1, int y1, int x2, int y2, int c1, int c2, int pat);
void bx_dots(int x1, int y1, int x2, int y2, int step, int colour);
unsigned bx_size(int id);
int bx_spr(int x, int y, int id);
int bx_artstr(int id, int n, char *buf);
int bx_text(int x, int y, const char *s, int colour, int font);
int bx_twidth(const char *s, int font);
int bx_fonth(int font);
int bx_out(void);
void bx_setup(unsigned seg, void *band, unsigned cap, unsigned artseg, int bpp);
extern unsigned bx_bseg;

BPRect bp_r_board, bp_r_center, bp_r_side, bp_r_hint;
BPByte bp_layout, bp_mono, bp_sel;

/* ---- state ------------------------------------------------------------- */
#define FC_HOME_SCAN 0x47
#define FC_UP_SCAN 0x48
#define FC_LEFT_SCAN 0x4B
#define FC_RIGHT_SCAN 0x4D
#define FC_DOWN_SCAN 0x50

static BPRect fc_rp, fc_rd, fc_rl, fc_rg;       /* players details link log */
static BPRect fc_all;                           /* the aligned content */
static struct os88_pt fc_org;
static struct os88_size fc_size;
static int fc_cw, fc_ch, fc_fs, fc_fb, fc_ft, fc_sh, fc_fh, fc_gr;
static int fc_sx1, fc_sy1, fc_sx2, fc_sy2;      /* the strip being composed */
static unsigned fc_secs;                        /* sections this pass draws */
static unsigned fc_dirty;
static unsigned fc_dsp[2];                      /* cells dirtied by space */
static unsigned fc_csig[BP_SPACES];             /* last painted signatures */
static unsigned fc_psig, fc_dsig, fc_lsig, fc_hsig, fc_gsig, fc_logver;
static int fc_lx, fc_ly, fc_lw, fc_lh, fc_lv;   /* layout cache key */
static int fc_pw, fc_ph;                        /* the sprite fc_pick chose */
static BPByte fc_ready;
static BPByte fc_art;                           /* the bar's centre art is on the glass */
static int fc_aty;                              /* its art line: the dice row and the bar lie below */
static char fc_log[BP_LOG_LINES][BP_LOG_LEN + 1];
static char fc_buf[48], fc_num[16], fc_fit[48];
static BPU32 fc_u;
#ifdef BP_FACE_COUNT
/* magic, paints, strips (find the magic in guest RAM; tools-free measuring) */
unsigned fc_cnt[4] = {0xFACE, 0xB0B0, 0, 0};
#endif

static const BPByte fc_gcol[BP_GROUPS] = {1, 2, 3, 4, 5, 8};
static const BPByte fc_sdark[BP_SEATS] = {4, 1, 2, 5};
static const BPByte fc_slite[BP_SEATS] = {12, 9, 14, 10};
/* token silhouettes: half-width of each of 7 rows around the centre column:
 * floppy square, mouse round, joystick triangle, cassette diamond */
static const BPByte fc_shape[4][7] = {
    {3, 3, 3, 3, 3, 3, 3}, {1, 2, 3, 3, 3, 2, 1},
    {0, 1, 1, 2, 2, 3, 3}, {0, 1, 2, 3, 2, 1, 0}};
static const char *const fc_lnk[8] = {"Off", "Searching", "Peer found", "Bad peer",
    "Lobby", "Linked", "PAUSED", "Resync"};

/* ---- small helpers ------------------------------------------------------ */
static int fc_live(void) { return bp_game.status != BP_ST_EMPTY; }
static int fc_hit(const BPRect *r)
{
    return r->w > 0 && r->x <= fc_sx2 && r->x + r->w - 1 >= fc_sx1 &&
           r->y <= fc_sy2 && r->y + r->h - 1 >= fc_sy1;
}
static unsigned fc_mix(unsigned h, unsigned v) { return (h << 3) + (h >> 13) + v + 1; }
static char *fc_dec(unsigned v) { return os88_utoa(v, fc_num); }
static int fc_ap(char *d, int n, const char *s)
{
    while (*s && n < 46) d[n++] = *s++;
    d[n] = 0;
    return n;
}
/* fc_fit cut until it fits maxw pixels */
static const char *fc_cut(int font, int maxw)
{
    int n;
    n = os88_strlen(fc_fit);
    while (n > 1 && bx_twidth(fc_fit, font) > maxw) fc_fit[--n] = 0;
    return fc_fit;
}
/* s fitted to maxw pixels (truncated copy in fc_fit when it does not) */
static const char *fc_fits(const char *s, int font, int maxw)
{
    if (bx_twidth(s, font) <= maxw) return s;
    os88_strcpy(fc_fit, s, sizeof(fc_fit));
    return fc_cut(font, maxw);
}
/* a cell's short name: too wide, its tail goes lower case ("E-waste",
 * "Power", narrower in the proportional font), and only then is it cut */
static const char *ovl_fc_cfits(const char *s, int maxw)
{
    char *p;
    if (bx_twidth(s, fc_fs) <= maxw) return s;
    os88_strcpy(fc_fit, s, sizeof(fc_fit));
    for (p = fc_fit + 1; *p; p++)
        if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + 32);
    return fc_cut(fc_fs, maxw);
}
static void fc_ctext(int x1, int x2, int y, const char *s, int colour, int font)
{
    int w;
    w = bx_twidth(s, font);
    bx_text(x1 + (x2 - x1 + 1 - w) / 2, y, s, colour, font);
}
static void fc_rtext(int x2, int y, const char *s, int colour, int font)
{
    if (y + 13 < fc_sy1 || y > fc_sy2) return;
    bx_text(x2 - bx_twidth(s, font) + 1, y, s, colour, font);
}
/* text that is skipped outright when its line is off the strip */
static void fc_t(int x, int y, const char *s, int colour, int font)
{
    if (y + 13 >= fc_sy1 && y <= fc_sy2) bx_text(x, y, s, colour, font);
}
static void fc_rect(int x, int y, int w, int h, int colour)
{
    bx_fill(x, y, x + w - 1, y + h - 1, colour);
}
/* a 1 px black frame with a fill. A wide one is four edges and the inside,
 * not black under the fill: every row of a 300 px panel costs ~40 bytes a
 * plane, an edge column a row loop. */
static void ovl_fc_edges(int x, int y, int x2, int y2)
{
    bx_fill(x, y, x2, y, 0);
    bx_fill(x, y2, x2, y2, 0);
    bx_fill(x, y, x, y2, 0);
    bx_fill(x2, y, x2, y2, 0);
}
static void fc_frame(int x, int y, int w, int h, int fill)
{
    if (w < 96 || h < 24) fc_rect(x, y, w, h, 0);
    else ovl_fc_edges(x, y, x + w - 1, y + h - 1);
    fc_rect(x + 1, y + 1, w - 2, h - 2, fill);
}
/* the round shown: completed + 1, held at a Quick Match's limit */
static unsigned ovl_fc_rnd(void)
{
    unsigned n;
    n = bp_game.round + 1;
    if (bp_game.rounds_limit && n > bp_game.rounds_limit) n = bp_game.rounds_limit;
    return n;
}
/* strike a text line through (greyed, eliminated, disabled on 1bpp) */
static void fc_strike(int x, int y, int w) { bx_dots(x, y, x + w - 1, y, 2, bp_mono ? 15 : 0); }

/* ---- the ring ---------------------------------------------------------- */
static int fc_col(int s)
{
    if (s <= 7) return 0;
    if (s <= 14) return s - 7;
    if (s <= 21) return 7;
    return 28 - s;
}
static int fc_row(int s)
{
    if (s <= 7) return 7 - s;
    if (s <= 14) return 0;
    if (s <= 21) return s - 14;
    return 7;
}
static int fc_spaceat(int col, int row)
{
    if (col < 0 || col > 7 || row < 0 || row > 7) return -1;
    if (col == 0) return 7 - row;
    if (row == 0) return 7 + col;
    if (col == 7) return 14 + row;
    if (row == 7) return 28 - col;
    return -1;
}
static void fc_cellrect(int s, BPRect *r)
{
    r->x = bp_r_board.x + fc_col(s) * fc_cw;
    r->y = bp_r_board.y + fc_row(s) * fc_ch;
    r->w = fc_cw;
    r->h = fc_ch;
}

/* ---- init and layout --------------------------------------------------- */
#ifdef BP_FACE_DEMO
static void ovl_fc_demo(void)
{
    static const char *nm[4] = {"You", "Ada", "Alan", "Grace"};
    int i;
    bp_game.status = BP_ST_PLAY; bp_game.nseats = 4; bp_game.phase = BP_PH_MANAGE;
    bp_game.round = 3; bp_game.d1 = 3; bp_game.d2 = 4; bp_game.rolled = 1;
    for (i = 0; i < 4; i++) {
        os88_strcpy(bp_game.name[i], nm[i], 9);
        bp_game.token[i] = (BPByte)i; bp_game.alive[i] = 1; bp_game.pos[i] = (BPByte)(i == 3 ? 4 : 4 + i * 6);
        bp_u32_set(&bp_game.cash[i], (BPWord)(1280 + i * 170), 0);
    }
    bp_game.pos[1] = 4; bp_game.alive[3] = 1; bp_game.detained[2] = 1; bp_game.pos[2] = 7;
    bp_u32_set(&bp_game.cash[3], 70, 0); bp_game.alive[3] = 0;
    for (i = 0; i < BP_PROPS; i++) { bp_game.owner[i] = BP_NOBODY; }
    bp_game.owner[2] = 0; bp_game.level[2] = 2; bp_game.owner[0] = 0; bp_game.owner[1] = 0; bp_game.level[0] = 5;
    bp_game.owner[7] = 1; bp_game.level[7] = 4; bp_game.owner[8] = 2; bp_game.mortgaged[8] = 1;
    bp_game.owner[3] = 1; bp_game.owner[10] = 0; bp_game.owner[13] = 2; bp_game.level[13] = 1;
    bp_sel = 4;
}
#endif
int bp_face_init(void *win, unsigned artseg, unsigned bandseg, unsigned bandcap, int bpp)
{
    static BPByte fallback[2048];
    (void)win;
    bp_mono = bpp < 4;
    fc_gr = bp_mono ? 15 : 7;
    if (bandseg) bx_setup(bandseg, 0, bandcap, artseg, bpp >= 4 ? 4 : 1);
    else bx_setup(0, fallback, sizeof(fallback), artseg, bpp >= 4 ? 4 : 1);
    fc_dirty = BP_D_ALL;
    fc_ready = 0;
    bp_sel = 0;
#ifdef BP_FACE_DEMO
    ovl_fc_demo();
#endif
    return BP_OK;
}
static void fc_layout(int w, int h)
{
    int ax, aw, sw, hb, hp, hl, rem, side_h, rh, th;
    ax = (fc_org.x + 7) & ~7;
    aw = (fc_org.x + w - ax) & ~7;
    bp_layout = (w >= 600 && h >= 380) ? BP_LAY_FULL : (w >= 560 && h >= 240) ? BP_LAY_MID : BP_LAY_COMPACT;
    if (bp_layout == BP_LAY_FULL) { fc_fs = BP_F_TEXT08; fc_fb = BP_F_TEXT10; fc_ft = BP_F_TEXT13; sw = 192; }
    else if (bp_layout == BP_LAY_MID) { fc_fs = BP_F_TEXT08; fc_fb = BP_F_TEXT08; fc_ft = BP_F_TEXT10; sw = 208; }
    else { fc_fs = BP_F_TEXT08; fc_fb = BP_F_TEXT08; fc_ft = BP_F_TEXT08; sw = 192; }
    fc_sh = bx_fonth(fc_fs);
    fc_fh = bx_fonth(fc_fb);
    hb = fc_fh + 4;
    if (aw - sw < 320) sw = 0;                          /* too narrow for a sidebar: the ring takes it all */
    fc_cw = ((aw - sw) / 8) & ~7;
    if (fc_cw < 16) fc_cw = 16;
    fc_ch = (h - hb) / 8;
    if (fc_ch < 16) fc_ch = 16;
    fc_all.x = ax; fc_all.y = fc_org.y; fc_all.w = aw; fc_all.h = h;
    bp_r_board.x = ax; bp_r_board.y = fc_org.y; bp_r_board.w = fc_cw * 8; bp_r_board.h = fc_ch * 8;
    bp_r_center.x = ax + fc_cw; bp_r_center.y = fc_org.y + fc_ch; bp_r_center.w = fc_cw * 6; bp_r_center.h = fc_ch * 6;
    bp_r_side.x = ax + fc_cw * 8; bp_r_side.y = fc_org.y; bp_r_side.w = aw - fc_cw * 8; bp_r_side.h = fc_ch * 8;
    bp_r_hint.x = ax; bp_r_hint.y = fc_org.y + fc_ch * 8; bp_r_hint.w = aw; bp_r_hint.h = h - fc_ch * 8;
    /* the sidebar's four boxes tile the column exactly */
    side_h = bp_r_side.h;
    rh = fc_fh + 3;
    th = fc_sh + 4;
    hp = (bp_layout == BP_LAY_COMPACT ? 0 : th) + 4 * rh + 3;
    hl = (bp_layout == BP_LAY_FULL ? 2 * rh : rh) + 3;
    rem = side_h - hp - hl;
    fc_rp.x = fc_rd.x = fc_rl.x = fc_rg.x = bp_r_side.x;
    fc_rp.w = fc_rd.w = fc_rl.w = fc_rg.w = bp_r_side.w;
    fc_rp.y = bp_r_side.y; fc_rp.h = hp;
    fc_rd.y = fc_rp.y + hp;
    fc_rd.h = bp_layout == BP_LAY_FULL ? rem * 3 / 5 : bp_layout == BP_LAY_MID ? rem * 14 / 25 : rem / 2;
    fc_rl.y = fc_rd.y + fc_rd.h; fc_rl.h = hl;
    fc_rg.y = fc_rl.y + hl; fc_rg.h = side_h - hp - fc_rd.h - hl;
}

/* ---- tokens ------------------------------------------------------------- */
/* a 7x7 token: outline ol (black on a light ground, white on a dark one),
 * seat colour inside - on 1bpp a white outline keeps a black inside, so on a
 * dark ground every token is a white ring (the owner mark never reads as a
 * white-filled token standing on the cell) - shape by token id */
static void fc_tok(int x, int y, int seat, int tok, int ol)
{
    int i, hw, fill;
    const BPByte *sh;
    sh = fc_shape[tok & 3];
    fill = ol && bp_mono ? 0 : fc_slite[seat & 3];
    for (i = 0; i < 7; i++) {
        hw = sh[i];
        bx_fill(x + 3 - hw, y + i, x + 3 + hw, y + i, ol);
        if (i > 0 && i < 6 && hw > 0) bx_fill(x + 4 - hw, y + i, x + 2 + hw, y + i, fill);
    }
}
static void fc_chip(int x, int y, int seat)
{
    bx_fill(x, y, x + 7, y + 7, bp_mono ? 0 : fc_sdark[seat & 3]);
    fc_num[0] = (char)('1' + seat); fc_num[1] = 0;
    bx_text(x + 1, y, fc_num, 15, BP_F_TEXT08);
}

/* ---- cells -------------------------------------------------------------- */
static const char *fc_cellsub(const BPSpace *sp)
{
    if (sp->kind == BP_K_BOOT) return "+200";
    if (sp->kind == BP_K_CHARGE) { fc_buf[0] = '-'; os88_strcpy(fc_buf + 1, fc_dec(sp->price), 8); return fc_buf; }
    return "";
}
static int fc_specart(const BPSpace *sp, int s)
{
    switch (sp->kind) {
    case BP_K_BOOT: return BP_A_BOOT;
    case BP_K_REPAIR: return BP_A_REPAIR;
    case BP_K_SWAP: return BP_A_SWAP;
    case BP_K_CRASH: return BP_A_CRASH;
    case BP_K_EVENT: return sp->group ? BP_A_SYSERR : BP_A_FLOPPY;
    case BP_K_CHARGE: return s == 6 ? BP_A_DUTY : BP_A_EWASTE;
    }
    return -1;
}
/* the 1bpp twin of an art id (SPEC.md 131.10.5), else the id itself */
static int fc_mono(int id)
{
    if (!bp_mono) return id;
    if (id >= BP_A_T_PET && id <= BP_A_T_486) return BP_A_MTINY(id - BP_A_T_PET);
    switch (id) {
    case BP_A_TOK_FLOPPY: return BP_A_M_TOK_FLOPPY;
    case BP_A_TOK_JOYSTICK: return BP_A_M_TOK_JOYSTICK;
    case BP_A_BOOT: return BP_A_M_BOOT;
    case BP_A_SWAP: return BP_A_M_SWAP;
    case BP_A_FLOPPY: return BP_A_M_FLOPPY;
    case BP_A_CRASH: return BP_A_M_CRASH;
    case BP_A_CARD_FF: return BP_A_M_CARD_FF;
    case BP_A_CARD_SE: return BP_A_M_CARD_SE;
    case BP_A_RAM: return BP_A_M_RAM;
    case BP_A_MORTGAGE: return BP_A_M_MORTGAGE;
    case BP_A_EWASTE: return BP_A_M_EWASTE;
    }
    return id;
}
/* the biggest of SMALL / TINY that fits, else -1; sets fc_pw/fc_ph */
static int fc_pick(int prop, int aw, int ah)
{
    unsigned z;
    int id;
    id = BP_A_SMALL(prop);
    z = bx_size(id);
    if (z && (int)(z & 255) <= aw && (int)(z >> 8) <= ah) { fc_pw = z & 255; fc_ph = z >> 8; return id; }
    id = fc_mono(BP_A_TINY(prop));
    z = bx_size(id);
    if (z && (int)(z & 255) <= aw && (int)(z >> 8) <= ah) { fc_pw = z & 255; fc_ph = z >> 8; return id; }
    return -1;
}
/* a corner or special space's picture: SMALL, else TINY (131.10), else -1 */
static int ovl_fc_spick(const BPSpace *sp, int s, int aw, int ah)
{
    int id, k, t;
    unsigned z;
    id = fc_specart(sp, s);
    if (id < 0) return -1;
    for (k = 0; k < 2; k++) {
        t = fc_mono(id);
#ifdef BP_A_TSPECIAL
        if (k) t = bp_mono ? BP_A_MTSPECIAL(id) : BP_A_TSPECIAL(id);
#else
        if (k) break;
#endif
        z = bx_size(t);
        if (z && (int)(z & 255) <= aw && (int)(z >> 8) <= ah) { fc_pw = z & 255; fc_ph = z >> 8; return t; }
    }
    return -1;
}
static int fc_seatat(int s, int k)      /* k-th live seat standing on space s, or -1 */
{
    int i;
    if (!fc_live()) return -1;
    for (i = 0; i < bp_game.nseats && i < BP_SEATS; i++)
        if (bp_game.alive[i] && bp_game.pos[i] == s) { if (k == 0) return i; k--; }
    return -1;
}
static void fc_tokens(int x, int y, int cw, int s, int compact)
{
    int k, seat, gap;
    gap = compact ? 8 : (cw - 4) / 4;
    for (k = 0; k < BP_SEATS; k++) {
        seat = fc_seatat(s, k);
        if (seat < 0) break;
        fc_tok(x + 2 + k * gap, y, seat, bp_game.token[seat], 0);
    }
}
/* upgrade pips: 1..4 RAM bars, 5 a Workshop block (no art, and COMPACT) */
static void fc_pips(int x2, int y, int level)
{
    int i;
    if (level >= BP_MAX_LEVEL) { fc_rect(x2 - 8, y, 9, 6, 0); return; }
    for (i = 0; i < level; i++) fc_rect(x2 - (i + 1) * 4 + 2, y, 3, 6, 0);
}
/* the level in the margin right of the sprite: art chips, else drawn pips */
static void fc_lvl(int xr, int y, int lvl, int mw)
{
    unsigned z;
    int id, i, pw, ph, cols;
    id = lvl >= BP_MAX_LEVEL ? BP_A_WORKSHOP : BP_A_RAM;
    z = bx_size(id);
    if (!z) { fc_pips(xr, y, lvl); return; }
    pw = z & 255; ph = z >> 8;
    if (lvl >= BP_MAX_LEVEL) { bx_spr(xr - pw + 1, y, id); return; }
    cols = mw / (pw + 1);
    if (cols < 1) cols = 1;
    for (i = 0; i < lvl; i++)
        bx_spr(xr - (i % cols + 1) * (pw + 1) + 2, y + (i / cols) * (ph + 1), fc_mono(id));
}
static void fc_cell(int s)
{
    static BPRect r;
    const BPSpace *sp;
    int x, y, x2, y2, cw, ch, prop, own, lvl, mort, bar, nw, id, sy, ah, sel, bg, ink;
    cw = fc_cw; ch = fc_ch;
    fc_cellrect(s, &r);
    x = r.x; y = r.y; x2 = x + cw - 1; y2 = y + ch - 1;
    sp = bp_space(s);
    prop = sp->prop;
    sel = s == bp_sel;
    bg = sel ? 1 : 15; ink = sel ? 15 : 0;
    own = BP_NOBODY; lvl = 0; mort = 0;
    if (prop != BP_NOBODY && fc_live() && bp_game.owner[prop] < bp_game.nseats) {
        own = bp_game.owner[prop]; lvl = bp_game.level[prop]; mort = bp_game.mortgaged[prop];
    }
    fc_rect(x, y, cw, ch, 15);
    bx_fill(x, y, x2, y, 0);
    bx_fill(x, y, x, y2, 0);
    if (fc_col(s) == 7) bx_fill(x2, y, x2, y2, 0);
    if (fc_row(s) == 7) bx_fill(x, y2, x2, y2, 0);
    if (ch >= 28) {                                     /* FULL and MID cells */
        bar = fc_sh;
        nw = cw - 6;
        if (prop != BP_NOBODY) {
            fc_rect(x + 2, y + 2, cw - 4, bar, bp_mono ? 0 : fc_gcol[sp->kind == BP_K_UTILITY ? BP_GROUPS - 1 : sp->group]);
            fc_buf[0] = sp->kind == BP_K_UTILITY ? 'U' : bp_group_letter(sp->group); fc_buf[1] = 0;
            id = bx_text(x + 4, y + 2, fc_buf, 15, fc_fs);
            if (own != BP_NOBODY)                       /* the owner: a white ring on the dark bar */
                fc_tok(x + 7 + id, y + 2 + (bar - 7) / 2, own, bp_game.token[own], 15);
            fc_rtext(x2 - 3, y + 2, fc_dec(sp->price), 15, fc_fs);
        }
        sy = y + (prop != BP_NOBODY ? bar + 2 + (ch >= 44) : 2 + (ch >= 44));
        if (sel) bx_fill(x + 2, sy, x2 - 2, sy + fc_sh, 1);
        fc_ctext(x + 2, x2 - 2, sy, ovl_fc_cfits(sp->shortname, nw), ink, fc_fs);
        sy += fc_sh + (ch >= 44);
        ah = y2 - sy - (ch >= 44 ? 4 : 1);
        id = prop != BP_NOBODY ? fc_pick(prop, nw, ah) : ovl_fc_spick(sp, s, nw, ah);
        if (mort) bx_dither(x + 2, sy, x2 - 2, y2 - 2, 15, 7, 2);
        if (id >= 0) bx_spr(x + (cw - fc_pw) / 2, sy + (ah - fc_ph) / 2, id);
        else if (prop == BP_NOBODY) fc_ctext(x + 2, x2 - 2, sy + 2, fc_cellsub(sp), 0, fc_fs);
        if (lvl) fc_lvl(x2 - 3, sy + 1, lvl, id >= 0 ? (cw - fc_pw) / 2 - 3 : 20);
        if (mort) {                                     /* the left margin, top of the picture row */
            if (bx_size(BP_A_MORTGAGE)) bx_spr(x + 3, sy + 1, fc_mono(BP_A_MORTGAGE));
            else {
                fc_rect(x + 3, sy + 1, 9, 9, 0);
                bx_text(x + 4, sy + 1, "M", 15, BP_F_TEXT08);
            }
        }
        fc_tokens(x, y2 - 8, cw, s, 0);
        if (sel) {                                      /* a frame inside the edge, over the ground only */
            bx_fill(x + 1, y + 1, x2 - 1, y + 1, bp_mono ? 0 : 14);
            bx_fill(x + 1, y2 - 1, x2 - 1, y2 - 1, bp_mono ? 0 : 14);
            bx_fill(x + 1, y + 1, x + 1, y2 - 1, bp_mono ? 0 : 14);
            bx_fill(x2 - 1, y + 1, x2 - 1, y2 - 1, bp_mono ? 0 : 14);
        }
    } else {                                            /* COMPACT cells: 16 rows */
        nw = cw - 2;
        if (prop != BP_NOBODY) {
            bx_fill(x + 1, y + 1, x + 8, y + 7, bp_mono ? 0 : fc_gcol[sp->kind == BP_K_UTILITY ? BP_GROUPS - 1 : sp->group]);
            fc_buf[0] = sp->kind == BP_K_UTILITY ? 'U' : bp_group_letter(sp->group); fc_buf[1] = 0;
            bx_text(x + 1, y + 1, fc_buf, 15, BP_F_TEXT08);
            if (own != BP_NOBODY) fc_chip(x + 1, y + 8, own);
            nw = cw - 13;
        }
        id = prop != BP_NOBODY ? x + 11 : x + 2;
        if (sel) bx_fill(id - 1, y + 1, x2 - 1, y + 7, 1);
        bx_text(id, y + 1, ovl_fc_cfits(sp->shortname, nw), ink, fc_fs);
        if (mort) fc_strike(id, y + 5, nw);
        if (fc_seatat(s, 0) >= 0) fc_tokens(id - 2, y + 8, cw, s, 1);
        else if (prop != BP_NOBODY) bx_text(id, y + 8, fc_dec(sp->price), 0, fc_fs);
        else bx_text(id, y + 8, fc_cellsub(sp), 0, fc_fs);
        if (lvl) fc_pips(x2 - 2, y + 9, lvl);
        if (mort) bx_text(x2 - 2 - bx_twidth("M", BP_F_TEXT08), y + 8, "M", 0, BP_F_TEXT08);
    }
}
static unsigned fc_sigcell(int s)
{
    const BPSpace *sp;
    unsigned h;
    int k, p;
    sp = bp_space(s);
    p = sp->prop;
    h = s == bp_sel ? 0x55 : 1;
    if (!fc_live()) return h;
    if (p != BP_NOBODY) h = fc_mix(fc_mix(fc_mix(h, bp_game.owner[p]), bp_game.level[p]), bp_game.mortgaged[p]);
    for (k = 0; k < bp_game.nseats && k < BP_SEATS; k++)
        if (bp_game.alive[k] && bp_game.pos[k] == s) h = fc_mix(h, 0x100 + k * 4 + bp_game.token[k]);
    return h;
}

/* ---- the centre --------------------------------------------------------- */
static void ovl_fc_die(int x, int y, int sz, int n)
{
    int id;
    id = BP_A_DIE(n);
    if (bx_size(id)) { bx_spr(x, y, id); return; }
    fc_frame(x, y, sz, sz, 15);                     /* no art: the number in a box */
    fc_num[0] = (char)('0' + n); fc_num[1] = 0;
    fc_ctext(x, x + sz - 1, y + (sz - 8) / 2, fc_num, 0, BP_F_TEXT08);
}
static void ovl_fc_centre(void)
{
    static const char logo[] = "BYTEOPOLY";
    int x, y, w, h, kw, kh, i, lx, ly, dsz, my, cid;
    unsigned z;
    x = bp_r_center.x; y = bp_r_center.y; w = bp_r_center.w; h = bp_r_center.h;
    bx_fill(x, y, x + w - 1, y, 0); bx_fill(x, y + h - 1, x + w - 1, y + h - 1, 0);
    bx_fill(x, y, x, y + h - 1, 0); bx_fill(x + w - 1, y, x + w - 1, y + h - 1, 0);
    if (bp_pn_covers() == BP_PN_CENTRE) {              /* the panel owns all but the 1 px ring */
        bx_dither(x + 1, y + 1, x + w - 2, y + 1, 3, 11, 2);
        bx_dither(x + 1, y + h - 2, x + w - 2, y + h - 2, 3, 11, 2);
        bx_dither(x + 1, y + 2, x + 1, y + h - 3, 3, 11, 2);
        bx_dither(x + w - 2, y + 2, x + w - 2, y + h - 3, 3, 11, 2);
        return;
    }
    bx_dither(x + 1, y + 1, x + w - 2, y + h - 2, 3, 11, 2);
    kh = bp_layout == BP_LAY_FULL ? 28 : bp_layout == BP_LAY_MID ? 20 : 16;
    kw = kh;
    z = bp_layout == BP_LAY_COMPACT ? bx_size(BP_A_KEYCAP) : 0;
    if (z) { kw = (int)(z & 255) + 1; kh = (int)(z >> 8) + 1; }
    lx = x + (w - 9 * kw) / 2;
    ly = bp_layout == BP_LAY_COMPACT ? y + 8 : y + h / 2 - kh / 2;
    for (i = 0; i < 9; i++) {
        if (z) bx_spr(lx + i * kw, ly, BP_A_KEYCAP);
        else fc_frame(lx + i * kw, ly, kw - 2, kh - 2, 15);
        fc_buf[0] = logo[i]; fc_buf[1] = 0;
        fc_ctext(lx + i * kw, lx + i * kw + kw - 3, ly + (kh - 2 - bx_fonth(fc_ft)) / 2, fc_buf, 0, fc_ft);
    }
    fc_aty = bp_layout == BP_LAY_COMPACT ? y : ly + kh + 6;
    my = y + 6;
    if (bp_layout != BP_LAY_COMPACT) {                  /* the mascot between the two card decks */
        z = bx_size(BP_A_MASCOT);
        if (z && (int)(z >> 8) <= ly - my - 2) {
            bx_spr(x + (w - (int)(z & 255)) / 2, my, BP_A_MASCOT);
            cid = fc_mono(BP_A_CARD_FF);
            z = bx_size(cid);
            if (z && (int)(z >> 8) <= ly - my - 2 && w >= 300) {
                bx_spr(x + w / 2 - 56 - (int)(z & 255), my, cid);
                bx_spr(x + w / 2 + 56, my, fc_mono(BP_A_CARD_SE));
            }
        }
    }
    if (fc_live() && bp_game.d1 > 0 && bp_game.d1 < 7 && bp_game.d2 > 0 && bp_game.d2 < 7) {
        z = bx_size(BP_A_DIE1);
        dsz = z ? (int)(z & 255) : bp_layout == BP_LAY_FULL ? 24 : bp_layout == BP_LAY_MID ? 20 : 14;
        if (bp_layout == BP_LAY_COMPACT) {
            ovl_fc_die(x + 12, ly, dsz, bp_game.d1);
            ovl_fc_die(x + w - 12 - dsz, ly, dsz, bp_game.d2);
        } else {
            ovl_fc_die(x + w / 2 - dsz - 6, ly + kh + 10, dsz, bp_game.d1);
            ovl_fc_die(x + w / 2 + 6, ly + kh + 10, dsz, bp_game.d2);
        }
    }
}

/* ---- the sidebar -------------------------------------------------------- */
static void fc_wpanel(const BPRect *r, const char *title)
{
    int th;
    th = fc_fh + 3;
    fc_frame(r->x, r->y, r->w, r->h, fc_gr);
    bx_fill(r->x + 1, r->y + 1, r->x + r->w - 2, r->y + th, 1);
    bx_text(r->x + 4, r->y + 2, fc_fits(title, fc_fb, r->w - 8), 15, fc_fb);
}
static void fc_wbox(const BPRect *r, int fill, int edge)
{
    fc_frame(r->x, r->y, r->w, r->h, fill);
    if (edge != 0) {
        bx_fill(r->x, r->y, r->x + r->w - 1, r->y, edge);
        bx_fill(r->x, r->y, r->x, r->y + r->h - 1, edge);
        bx_fill(r->x + r->w - 1, r->y, r->x + r->w - 1, r->y + r->h - 1, edge);
        bx_fill(r->x, r->y + r->h - 1, r->x + r->w - 1, r->y + r->h - 1, edge);
    }
}
static void fc_head(const BPRect *r, const char *title, const char *right)
{
    int th;
    th = fc_sh + 4;
    fc_frame(r->x, r->y, r->w, r->h, fc_gr);
    bx_fill(r->x + 1, r->y + 1, r->x + r->w - 2, r->y + th - 1, 1);
    bx_text(r->x + 4, r->y + 2, title, 15, fc_fs);
    if (right) fc_rtext(r->x + r->w - 5, r->y + 2, right, 15, fc_fs);
}
static void fc_players(void)
{
    const BPRect *r;
    int i, y, rh, x2, th, cw;
    unsigned hi;
    r = &fc_rp;
    rh = fc_fh + 3;
    th = bp_layout == BP_LAY_COMPACT ? 0 : fc_sh + 4;
    x2 = r->x + r->w - 1;
    if (th) {
        if (fc_live()) {
            fc_buf[0] = 0; i = fc_ap(fc_buf, 0, "Round ");
            fc_ap(fc_buf, i, fc_dec(ovl_fc_rnd()));
            fc_head(r, fc_buf, 0);
            if (bp_game.d1 && bp_game.rolled) {
                fc_num[0] = (char)('0' + bp_game.d1); fc_num[1] = '+'; fc_num[2] = (char)('0' + bp_game.d2); fc_num[3] = 0;
                fc_rtext(x2 - 5, r->y + 2, fc_num, 15, fc_fs);
            }
        } else fc_head(r, "Players", 0);
    } else fc_frame(r->x, r->y, r->w, r->h, fc_gr);
    y = r->y + th + 1;
    if (!fc_live()) { fc_t(r->x + 6, y + 1, "No match - Game > New Game", 0, fc_fb); return; }
    for (i = 0; i < bp_game.nseats && i < BP_SEATS; i++, y += rh) {
        hi = (bp_game.status == BP_ST_PLAY && bp_game.active == i && bp_game.alive[i]);
        if (hi) bx_fill(r->x + 1, y, x2 - 1, y + rh - 1, 1);
        fc_tok(r->x + 4, y + (rh - 7) / 2, i, bp_game.token[i], hi ? 15 : 0);
        fc_t(r->x + 15, y + 1, bp_game.name[i], hi ? 15 : 0, fc_fb);
        if (!bp_game.alive[i]) {
            fc_strike(r->x + 15, y + rh / 2, bx_twidth(bp_game.name[i], fc_fb));
            fc_rtext(x2 - 4, y + 1, "OUT", 0, fc_fb);
        } else {
            fc_rtext(x2 - 4, y + 1, bp_u32_fmt(fc_fit, &bp_game.cash[i]), hi ? 15 : 0, fc_fb);
            if (bp_game.detained[i]) {
                cw = bx_twidth(bp_u32_fmt(fc_fit, &bp_game.cash[i]), fc_fb);
                fc_rect(x2 - 4 - cw - 12, y + 2, 9, rh - 4, hi ? 15 : 0);
                fc_t(x2 - 4 - cw - 11, y + 1, "R", hi ? 1 : 15, BP_F_TEXT08);
            }
        }
    }
}
/* one "label ....... value" line */
static void ovl_fc_kv(int x1, int x2, int y, const char *k, const char *v)
{
    fc_t(x1, y, k, 0, fc_fb);
    fc_rtext(x2, y, v, 0, fc_fb);
}
static const char *ovl_fc_plain(int s)
{
    switch (bp_space(s)->kind) {
    case BP_K_BOOT: return "Start. Passing BOOT pays 200 cr.";
    case BP_K_EVENT: return bp_space(s)->group ? "Draw a System Error card." : "Draw a Floppy Find card.";
    case BP_K_CHARGE: return "Pay the bank.";
    case BP_K_REPAIR: return "Visiting is safe; detained here.";
    case BP_K_SWAP: return "A free rest. Nothing happens.";
    case BP_K_CRASH: return "Go to the Repair Shop.";
    }
    return "";
}
/* a..d joined into fc_buf; numbers come from ovl_fc_nn's two slots */
static char fc_n[2][8];
static const char *ovl_fc_nn(int k, unsigned v) { return os88_utoa(v, fc_n[k]); }
static const char *ovl_fc_cat(const char *a, const char *b, const char *c, const char *d)
{
    int n;
    fc_buf[0] = 0;
    n = fc_ap(fc_buf, 0, a);
    n = fc_ap(fc_buf, n, b);
    n = fc_ap(fc_buf, n, c);
    fc_ap(fc_buf, n, d);
    return fc_buf;
}
static const char *fc_lvltxt(int lvl, int mort)
{
    static char b[8];
    if (mort) return "Mortgaged";
    if (lvl >= BP_MAX_LEVEL) return "Workshop";
    b[0] = (char)('0' + lvl); os88_strcpy(b + 1, " RAM", 6);
    return b;
}
static void ovl_fc_details(void)
{
    static const BPByte mul[5] = {5, 15, 30, 45, 60};
    static const char *const rl[5] = {"1 RAM", "2 RAM", "3 RAM", "4 RAM", "Workshop"};
    const BPRect *r;
    const BPSpace *sp;
    const char *own;
    int x1, x2, y, rh, i, id, prop, lvl, mort, cw, cp;
    unsigned base;
    r = &fc_rd;
    sp = bp_space(bp_sel);
    prop = sp->prop;
    rh = fc_fh + 3;
    cp = bp_layout == BP_LAY_COMPACT;
    x1 = r->x + 5; x2 = r->x + r->w - 6;
    own = "Bank"; lvl = 0; mort = 0;
    if (prop != BP_NOBODY && fc_live() && bp_game.owner[prop] < bp_game.nseats) {
        own = bp_game.name[bp_game.owner[prop]]; lvl = bp_game.level[prop]; mort = bp_game.mortgaged[prop];
    }
    fc_frame(r->x, r->y, r->w, r->h, fc_gr);
    y = r->y + 2;
    if (!cp) {                                          /* the title bar: the full name */
        bx_fill(r->x + 1, r->y + 1, r->x + r->w - 2, r->y + bx_fonth(fc_ft) + 3, 1);
        fc_t(x1, r->y + 2, fc_fits(sp->name, fc_ft, r->w - 10), 15, fc_ft);
        y += bx_fonth(fc_ft) + 3;
    }
    if (prop == BP_NOBODY) {
        id = fc_specart(sp, bp_sel);
        if (cp) { fc_t(x1, y, sp->name, 0, fc_fb); y += rh; }
        else if (id >= 0 && bx_size(id)) { bx_spr(r->x + (r->w - (int)(bx_size(id) & 255)) / 2, y, id); y += (int)(bx_size(id) >> 8) + 4; }
        fc_t(x1, y, fc_fits(ovl_fc_plain(bp_sel), fc_fb, r->w - 10), 0, fc_fb);
        if (sp->kind == BP_K_CHARGE) fc_t(x1, y + rh, ovl_fc_cat("Charge: ", ovl_fc_nn(0, sp->price), " cr", ""), 0, fc_fb);
        return;
    }
    base = sp->rent;
    if (cp) {                                           /* COMPACT: three lines */
        fc_t(x1, y, ovl_fc_cat(sp->name, "  ", ovl_fc_nn(0, sp->price), " cr"), 0, fc_fb);
        fc_t(x1, y + rh, sp->kind == BP_K_UTILITY ? "Rent 4x dice, 10x both" :
                 ovl_fc_cat("Rent ", ovl_fc_nn(0, base), " set ", ovl_fc_nn(1, base * 2)), 0, fc_fb);
        fc_t(x1, y + rh * 2, fc_fits(ovl_fc_cat("Owner ", own, " ", lvl || mort ? fc_lvltxt(lvl, mort) : ""), fc_fb, r->w - 10), 0, fc_fb);
        return;
    }
    id = bp_layout == BP_LAY_FULL ? BP_A_LARGE(prop) : -1;      /* top block; LARGE art to its right on FULL */
    cw = r->w - 10;
    if (id >= 0 && bx_size(id) && (int)(bx_size(id) & 255) <= r->w / 2) {
        bx_spr(x2 - (int)(bx_size(id) & 255) + 1, y, id);
        cw -= (int)(bx_size(id) & 255) + 4;
    }
    ovl_fc_kv(x1, x1 + cw - 1, y, "Price", ovl_fc_cat(ovl_fc_nn(0, sp->price), " cr", "", ""));
    if (mort) bp_redeem_cost(bp_sel, &fc_u);
    ovl_fc_kv(x1, x1 + cw - 1, y + rh, mort ? "Redeem" : "Mortgage",
              ovl_fc_cat(mort ? bp_u32_fmt(fc_fit, &fc_u) : ovl_fc_nn(0, (unsigned)bp_mortgage_value(bp_sel)), " cr", "", ""));
    ovl_fc_kv(x1, x1 + cw - 1, y + rh * 2, "Owner", own);
    ovl_fc_kv(x1, x1 + cw - 1, y + rh * 3, "Level", sp->kind == BP_K_UTILITY ? (mort ? "Mortgaged" : "-") : fc_lvltxt(lvl, mort));
    y += rh * 4 + 3;
    if (sp->kind == BP_K_UTILITY) {
        fc_t(x1, y, "Rent: 4 x dice total,", 0, fc_fb);
        fc_t(x1, y + rh, "10 x with both utilities.", 0, fc_fb);
        return;
    }
    bx_fill(r->x + 1, y - 2, x2 + 4, y - 2, 0);         /* the rent schedule, two columns (131.2.3) */
    cw = (r->w - 10) / 2;
    ovl_fc_kv(x1, x1 + cw - 6, y, "Base", ovl_fc_cat(ovl_fc_nn(0, base), "", "", ""));
    ovl_fc_kv(x1 + cw + 4, x2, y, "Set", ovl_fc_cat(ovl_fc_nn(0, base * 2), "", "", ""));
    for (i = 0; i < 5; i++) {
        y += (i & 1) == 0 ? rh : 0;
        ovl_fc_kv(i == 4 ? x1 : x1 + (i & 1) * (cw + 4), i == 4 || (i & 1) ? x2 : x1 + cw - 6, y, rl[i],
                  ovl_fc_cat(ovl_fc_nn(0, base * mul[i]), "", "", ""));
    }
}
static void fc_link(void)
{
    static char pre[16], dd[5];
    const BPRect *r;
    int st, loc;
    r = &fc_rl;
    fc_frame(r->x, r->y, r->w, r->h, fc_gr);
    st = bp_link_state();
    loc = bp_mode == BP_MODE_LOCAL;
    pre[0] = 0;
    if (bp_layout != BP_LAY_FULL && fc_live()) {    /* round and dice ride the link row off FULL */
        dd[0] = ' '; dd[1] = (char)('0' + bp_game.d1); dd[2] = '+'; dd[3] = (char)('0' + bp_game.d2); dd[4] = 0;
        os88_strcpy(pre, ovl_fc_cat("R", ovl_fc_nn(0, ovl_fc_rnd()), bp_game.rolled && bp_game.d1 ? dd : "", " "), sizeof(pre));
    }
    fc_t(r->x + 5, r->y + 2, ovl_fc_cat(pre, loc ? "Same PC" : st < 8 ? fc_lnk[st] : "?", bp_sess_busy() ? " ..." : "", ""), 0, fc_fb);
    if (st == BP_L_PAUSED || st == BP_L_BADPEER) fc_strike(r->x + 5, r->y + 1, r->w - 10);
    if (bp_layout == BP_LAY_FULL) fc_t(r->x + 5, r->y + 5 + fc_fh, fc_fits(loc ? "Cable: off" : bp_link_msg(), fc_fb, r->w - 10), 0, fc_fb);
}
static void fc_logbox(void)
{
    const BPRect *r;
    int n, i, y, rh, th, x1;
    r = &fc_rg;
    rh = fc_fh + 2;
    if (bp_layout == BP_LAY_COMPACT) { fc_frame(r->x, r->y, r->w, r->h, fc_gr); th = 0; }
    else { fc_head(r, "Log", 0); th = fc_sh + 4; }
    x1 = r->x + 4;
    n = (r->h - th - 3) / rh;
    if (n > BP_LOG_LINES) n = BP_LOG_LINES;
    y = r->y + r->h - 2 - rh;
    for (i = 0; i < n; i++, y -= rh)                    /* newest at the bottom */
        if (fc_log[i][0] && y + 13 >= fc_sy1 && y <= fc_sy2) fc_t(x1, y, fc_fits(fc_log[i], fc_fb, r->w - 8), 0, fc_fb);
}

/* ---- the hint bar ------------------------------------------------------- */
static void fc_hint(void)
{
    const char *h, *sp;
    int i, x, y, n, kw, lw, x2;
    x = bp_r_hint.x + 6;
    x2 = bp_r_hint.x + bp_r_hint.w - 1;
    y = bp_r_hint.y + (bp_r_hint.h - fc_fh) / 2;
    bx_fill(bp_r_hint.x, bp_r_hint.y, x2, bp_r_hint.y + bp_r_hint.h - 1, 1);
    for (i = 0; (h = bp_pn_hint(i)) != 0; i++) {
        for (sp = h; *sp && *sp != ' '; sp++) ;
        n = sp - h;
        if (!*sp) { n = 0; sp = h; } else sp++;
        if (n > 0 && n < 8) {
            os88_memcpy(fc_fit, h, n); fc_fit[n] = 0;
            kw = bx_twidth(fc_fit, fc_fb) + 6;
        } else { n = 0; kw = 0; }
        lw = bx_twidth(sp, fc_fb);
        if (x + kw + 4 + lw > x2) break;
        if (n) {
            bx_fill(x, y - 2, x + kw - 1, y + fc_fh + 1, 15);
            bx_fill(x, y + fc_fh + 2, x + kw - 1, y + fc_fh + 2, bp_mono ? 15 : 8);
            bx_text(x + 3, y, fc_fit, 0, fc_fb);
        }
        bx_text(x + kw + 4, y, sp, 15, fc_fb);
        x += kw + 4 + lw + 14;
    }
}

/* ---- the scene, per strip ---------------------------------------------- */
#ifdef BP_FACE_DEMO
static void ovl_fc_demopanel(void)
{
    static BPRect pr, b1, b2, b3, b4;
    pr.x = bp_r_center.x + 16; pr.w = bp_r_center.w - 32; pr.h = bp_r_center.h > 150 ? 84 : 52; pr.y = bp_r_center.y + bp_r_center.h - pr.h - 6;
    bp_w_panel(&pr, "You landed on Commodore 64");
    bp_w_text(pr.x + 6, pr.y + 16, "Price 220 cr   Rent 18 cr", 0, BP_F_TEXT10);
    b1.x = pr.x + 6; b1.y = pr.y + pr.h - 22; b1.w = 70; b1.h = 18;
    b2.y = b3.y = b4.y = b1.y; b2.h = b3.h = b4.h = 18; b2.w = b3.w = 70; b2.x = b1.x + 76; b3.x = b1.x + 152; b4.x = b1.x + 228; b4.w = 50;
    bp_w_button(&b1, "[B] Buy", BP_W_DEFAULT);
    bp_w_button(&b2, "[A] Auction", BP_W_FOCUS);
    bp_w_button(&b3, "[T] Trade", BP_W_DISABLED);
    bp_w_button(&b4, "Esc", BP_W_NORMAL);
}
#endif
static void fc_scene(void)
{
    static BPRect r;
    int s;
    if ((fc_secs & BP_D_CENTER) && bp_pn_covers() == BP_PN_FULL) {
        bp_pn_draw();
        return;
    }
    if (fc_secs & BP_D_BOARD)
        for (s = 0; s < BP_SPACES; s++) {
            fc_cellrect(s, &r);
            if (fc_hit(&r)) fc_cell(s);
        }
    if ((fc_secs & BP_D_CENTER) && fc_hit(&bp_r_center)) {
        ovl_fc_centre();
        bp_pn_draw();
#ifdef BP_FACE_DEMO
        ovl_fc_demopanel();
#endif
    }
    if (bp_r_side.w >= 96) {
        if ((fc_secs & BP_D_PLAYERS) && fc_hit(&fc_rp)) fc_players();
        if ((fc_secs & BP_D_DETAILS) && fc_hit(&fc_rd)) ovl_fc_details();
        if ((fc_secs & BP_D_LINK) && fc_hit(&fc_rl)) fc_link();
        if ((fc_secs & BP_D_LOG) && fc_hit(&fc_rg)) fc_logbox();
    }
    if ((fc_secs & BP_D_HINT) && fc_hit(&bp_r_hint)) fc_hint();
}
/* paint one aligned rect, in strips, with the sections in secs */
static void fc_paint(int x1, int y1, int w, int h, unsigned secs)
{
    int rows, n, y2;
    if (w <= 0 || h <= 0) return;
    rows = bx_rows(x1, y1, w, h);
    if (rows < 1) return;
    fc_secs = secs;
    y2 = y1 + h - 1;
#ifdef BP_FACE_COUNT
    fc_cnt[2]++;
#endif
    while (y1 <= y2) {
        n = y2 - y1 + 1; if (n > rows) n = rows;
        fc_sx1 = x1; fc_sx2 = x1 + w - 1; fc_sy1 = y1; fc_sy2 = y1 + n - 1;
        bx_new(x1, y1, w, n, -1);
        fc_scene();
        bx_out();
#ifdef BP_FACE_COUNT
        fc_cnt[3]++;
#endif
        y1 += n;
    }
}
static void fc_paintr(const BPRect *r, unsigned secs) { fc_paint(r->x, r->y, r->w, r->h, secs); }

/* ---- signatures of the sections that need no explicit dirty ------------- */
static unsigned fc_hb(unsigned h, const void *p, int n)
{
    const BPByte *q;
    for (q = (const BPByte *)p; n > 0; n--) h = fc_mix(h, *q++);
    return h;
}
static unsigned fc_sigscal(unsigned h)
{
    return fc_mix(fc_mix(fc_mix(h, bp_game.round), bp_game.d1 * 8 + bp_game.d2 + bp_game.rolled * 64), bp_game.active * 4 + bp_game.status);
}
static unsigned fc_sigplayers(void)
{
    unsigned h;
    h = fc_live() ? 7 : 3;
    if (!fc_live()) return h;
    h = fc_sigscal(h);
    h = fc_hb(h, bp_game.name, sizeof(bp_game.name));
    h = fc_hb(h, bp_game.cash, sizeof(bp_game.cash));
    h = fc_hb(h, bp_game.alive, BP_SEATS);
    h = fc_hb(h, bp_game.detained, BP_SEATS);
    return fc_hb(h, bp_game.token, BP_SEATS);
}
static unsigned fc_sigdetails(void)
{
    unsigned h;
    h = fc_mix(bp_sel, fc_live());
    if (!fc_live()) return h;
    h = fc_hb(h, bp_game.owner, BP_PROPS);
    h = fc_hb(h, bp_game.level, BP_PROPS);
    h = fc_hb(h, bp_game.mortgaged, BP_PROPS);
    return fc_hb(h, bp_game.name, sizeof(bp_game.name));
}
static unsigned fc_siglink(void)
{
    unsigned h;
    const char *m;
    h = fc_mix(fc_mix(bp_link_state(), bp_sess_busy()), bp_mode);
    if (bp_layout != BP_LAY_FULL) h = fc_sigscal(h);
    for (m = bp_link_msg(); *m; m++) h = fc_mix(h, *m);
    return h;
}
static unsigned fc_sighint(void)
{
    unsigned h;
    const char *m;
    int i;
    h = 5;
    for (i = 0; (m = bp_pn_hint(i)) != 0; i++) { h = fc_mix(h, i); for (; *m; m++) h = fc_mix(h, *m); }
    return h;
}

void bp_face_dirty(unsigned mask) { fc_dirty |= mask; }
int bp_face_isdirty(void) { return fc_dirty != 0 || fc_dsp[0] != 0 || fc_dsp[1] != 0; }
/* damage smaller than a section (131.8.5): up to FC_NR rects, a further one
 * merged into the last as their bounding box */
#define FC_D_RECT 0x4000
#define FC_NR 4
static BPRect fc_dr[FC_NR];
static BPByte fc_ndr;
static void ovl_fc_drect(const BPRect *r)
{
    BPRect *d;
    int x2, y2;
    if (!r || r->w <= 0 || r->h <= 0) return;
    fc_dirty |= FC_D_RECT;
    if (fc_ndr < FC_NR) {
        d = &fc_dr[fc_ndr++];
        d->x = r->x; d->y = r->y; d->w = r->w; d->h = r->h;
        return;
    }
    d = &fc_dr[FC_NR - 1];
    x2 = d->x + d->w; y2 = d->y + d->h;
    if (r->x + r->w > x2) x2 = r->x + r->w;
    if (r->y + r->h > y2) y2 = r->y + r->h;
    if (r->x < d->x) d->x = r->x;
    if (r->y < d->y) d->y = r->y;
    d->w = x2 - d->x; d->h = y2 - d->y;
}
void bp_face_dirty_rect(const BPRect *r) { ovl_fc_drect(r); }
static int ovl_fc_in(const BPRect *a, const BPRect *b)  /* a inside b */
{
    return a->x >= b->x && a->y >= b->y && a->x + a->w <= b->x + b->w && a->y + a->h <= b->y + b->h;
}
/* paint the queued rects; centre: the whole centre was painted this pass */
static void ovl_fc_rects(int cov, int centre)
{
    static BPRect r;
    unsigned secs;
    int i, x2, y2;
    for (i = 0; i < fc_ndr; i++) {
        r.x = fc_dr[i].x; r.y = fc_dr[i].y; r.w = fc_dr[i].w; r.h = fc_dr[i].h;
        if (centre && ovl_fc_in(&r, &bp_r_center)) continue;
        x2 = r.x + r.w; y2 = r.y + r.h;                 /* clipped to the content, x to 8 */
        if (r.x < fc_all.x) r.x = fc_all.x;
        if (r.y < fc_all.y) r.y = fc_all.y;
        if (x2 > fc_all.x + fc_all.w) x2 = fc_all.x + fc_all.w;
        if (y2 > fc_all.y + fc_all.h) y2 = fc_all.y + fc_all.h;
        r.x = fc_all.x + ((r.x - fc_all.x) & ~7);
        x2 = fc_all.x + ((x2 - fc_all.x + 7) & ~7);
        if (x2 > fc_all.x + fc_all.w) x2 = fc_all.x + fc_all.w;
        r.w = x2 - r.x; r.h = y2 - r.y;
        if (r.w <= 0 || r.h <= 0) continue;
        fc_sx1 = r.x; fc_sx2 = x2 - 1; fc_sy1 = r.y; fc_sy2 = y2 - 1;
        secs = BP_D_CENTER;                             /* the sections it touches */
        if (cov != BP_PN_FULL) {
            if (!fc_hit(&bp_r_center)) secs = 0;
            if (fc_hit(&bp_r_board) && !ovl_fc_in(&r, &bp_r_center)) secs |= BP_D_BOARD;
            if (fc_hit(&fc_rp)) secs |= BP_D_PLAYERS;
            if (fc_hit(&fc_rd)) secs |= BP_D_DETAILS;
            if (fc_hit(&fc_rl)) secs |= BP_D_LINK;
            if (fc_hit(&fc_rg)) secs |= BP_D_LOG;
            if (fc_hit(&bp_r_hint)) secs |= BP_D_HINT;
        }
        fc_paintr(&r, secs);
    }
    fc_ndr = 0;
}
void bp_face_dirty_space(int space)
{
    if (space < 0 || space >= BP_SPACES) return;
    fc_dsp[space >> 4] |= 1u << (space & 15);
    fc_dirty |= BP_D_BOARD;
}
static void fc_fullcentre(void)
{
    bp_r_center.x = fc_all.x; bp_r_center.y = fc_all.y; bp_r_center.w = fc_all.w; bp_r_center.h = fc_all.h;
}
static void fc_paintall(void *win, int full)
{
    static BPRect r;
    unsigned d, sg;
    int s, nch, cov;
    nch = 0;
    os88_wm_content(win, &fc_org);
    if (os88_wm_geom(win, &fc_size) != 0) return;
    cov = bp_pn_covers();
    if (!fc_ready || fc_org.x != fc_lx || fc_org.y != fc_ly || fc_size.w != fc_lw || fc_size.h != fc_lh ||
        (cov == BP_PN_FULL) != (fc_lv == BP_PN_FULL)) {
        fc_lx = fc_org.x; fc_ly = fc_org.y; fc_lw = fc_size.w; fc_lh = fc_size.h; fc_lv = cov;
        fc_layout(fc_size.w, fc_size.h);
        if (cov == BP_PN_FULL) fc_fullcentre();
        fc_ready = 1;
        full = 1;
    } else if (cov != fc_lv) {                          /* bar <-> centre panel: inside the centre */
        fc_lv = cov;
        fc_dirty |= BP_D_CENTER | BP_D_HINT;
    }
    if (full) {
        for (s = 0; s < BP_SPACES; s++) fc_csig[s] = fc_sigcell(s);
        fc_psig = fc_sigplayers(); fc_dsig = fc_sigdetails(); fc_lsig = fc_siglink(); fc_hsig = fc_sighint(); fc_gsig = fc_logver;
        fc_dsp[0] = fc_dsp[1] = 0; fc_dirty = 0; fc_ndr = 0; fc_art = cov == 0;
        fc_paintr(&fc_all, cov == BP_PN_FULL ? BP_D_CENTER : BP_D_ALL);
        return;
    }
    d = fc_dirty;
    if (!d) return;
    fc_dirty = 0;
    if (cov == BP_PN_FULL) {
        if (d & BP_D_CENTER) { fc_ndr = 0; fc_paintr(&fc_all, BP_D_CENTER); }
        else if (d & FC_D_RECT) ovl_fc_rects(cov, 0);
        return;
    }
    if (d & BP_D_BOARD) {                               /* cells whose signature moved */
        nch = 0;
        for (s = 0; s < BP_SPACES; s++) {
            sg = fc_sigcell(s);
            if (sg != fc_csig[s] || (d & BP_D_FORCE) || (fc_dsp[s >> 4] & (1u << (s & 15)))) { fc_csig[s] = sg; fc_dsp[s >> 4] |= 1u << (s & 15); nch++; }
        }
        if (nch > 10) { fc_paintr(&bp_r_board, BP_D_BOARD | BP_D_CENTER); fc_art = cov == 0; }   /* many: the board and its centre at once */
        else
            for (s = 0; s < BP_SPACES; s++)
                if (fc_dsp[s >> 4] & (1u << (s & 15))) { fc_cellrect(s, &r); fc_paintr(&r, BP_D_BOARD); }
        fc_dsp[0] = fc_dsp[1] = 0;
    }
    if ((d & BP_D_CENTER) && nch <= 10) {
        r.x = bp_r_center.x; r.w = bp_r_center.w; r.y = bp_r_center.y; r.h = bp_r_center.h;
        if (cov == 0 && fc_art && fc_aty > r.y && fc_aty < r.y + r.h) {     /* keycaps, mascot, cards stand */
            r.h -= fc_aty - r.y; r.y = fc_aty;
        }
        fc_paintr(&r, BP_D_CENTER);
        fc_art = cov == 0;
    }
    if (bp_r_side.w >= 96) {
        if (d & BP_D_PLAYERS) { sg = fc_sigplayers(); if (sg != fc_psig || (d & BP_D_FORCE)) { fc_psig = sg; fc_paintr(&fc_rp, BP_D_PLAYERS); } }
        if (d & BP_D_DETAILS) { sg = fc_sigdetails(); if (sg != fc_dsig || (d & BP_D_FORCE)) { fc_dsig = sg; fc_paintr(&fc_rd, BP_D_DETAILS); } }
        if (d & BP_D_LINK) { sg = fc_siglink(); if (sg != fc_lsig || (d & BP_D_FORCE)) { fc_lsig = sg; fc_paintr(&fc_rl, BP_D_LINK); } }
        if ((d & BP_D_LOG) && (fc_gsig != fc_logver || (d & BP_D_FORCE))) { fc_gsig = fc_logver; fc_paintr(&fc_rg, BP_D_LOG); }
    }
    if (d & BP_D_HINT) { sg = fc_sighint(); if (sg != fc_hsig || (d & BP_D_FORCE)) { fc_hsig = sg; fc_paintr(&bp_r_hint, BP_D_HINT); } }
    if (d & FC_D_RECT) ovl_fc_rects(cov, (d & BP_D_CENTER) || nch > 10);
}

/* ---- input -------------------------------------------------------------- */
static int fc_hitspace(int x, int y)
{
    int col, row;
    if (!fc_ready || bp_pn_covers() == BP_PN_FULL) return -1;
    if (x < bp_r_board.x || y < bp_r_board.y) return -1;
    col = (x - bp_r_board.x) / fc_cw;
    row = (y - bp_r_board.y) / fc_ch;
    return fc_spaceat(col, row);
}
void bp_face_select(int space)
{
    if (space < 0 || space >= BP_SPACES || space == bp_sel) return;
    bp_sel = (BPByte)space;
    fc_dirty |= BP_D_BOARD | BP_D_DETAILS;
}
static int ovl_fc_key(int ascii, int scan)
{
    int col, row, dx, dy, t;
    if (bp_pn_covers() == BP_PN_FULL) return 0;
#ifdef BP_FACE_DEMO
    if (ascii == 'm') { bp_game.pos[1] = (BPByte)((bp_game.pos[1] + 1) % 28); bp_face_dirty(BP_D_ALL); return 1; }
    if (ascii == 'k') { bp_game.cash[0].lo += 10; bp_face_dirty(BP_D_ALL); return 1; }
    if (ascii == 'o') { bp_game.owner[9] = 1; bp_game.level[9] = 3; bp_face_dirty(BP_D_ALL); return 1; }
    if (ascii == 'l') { bp_log_add("Ada rolled 7 and paid 24."); return 1; }
#endif
    if (ascii != 0 && ascii != 0xE0) return 0;
    dx = 0; dy = 0;
    switch (scan) {
    case FC_LEFT_SCAN: dx = -1; break;
    case FC_RIGHT_SCAN: dx = 1; break;
    case FC_UP_SCAN: dy = -1; break;
    case FC_DOWN_SCAN: dy = 1; break;
    case FC_HOME_SCAN:
        if (fc_live() && bp_game.active < bp_game.nseats) { bp_face_select(bp_game.pos[bp_game.active]); return 1; }
        return 0;
    default: return 0;
    }
    col = fc_col(bp_sel); row = fc_row(bp_sel);
    t = fc_spaceat(col + dx, row + dy);
    if (t < 0) {                                        /* an inward step crosses to the far side */
        if (dx && (row == 0 || row == 7)) t = -1;
        else if (dx) t = fc_spaceat(dx > 0 ? 7 : 0, row);
        else t = fc_spaceat(col, dy > 0 ? 7 : 0);
    }
    if (t >= 0 && t != bp_sel) bp_face_select(t);
    return 1;
}

/* ---- the widget kit (valid only while bp_pn_draw runs) ------------------ */
static void fc_wbutton(const BPRect *r, const char *label, int state)
{
    int x, y, x2, y2, tw, ty, dis, foc, i;
    x = r->x; y = r->y; x2 = x + r->w - 1; y2 = y + r->h - 1;
    dis = state & BP_W_DISABLED; foc = (state & BP_W_FOCUS) && !dis;
    fc_rect(x, y, r->w, r->h, 0);
    if (state & BP_W_DEFAULT) fc_rect(x + 1, y + 1, r->w - 2, r->h - 2, 0), fc_rect(x + 2, y + 2, r->w - 4, r->h - 4, foc ? 1 : fc_gr);
    else fc_rect(x + 1, y + 1, r->w - 2, r->h - 2, foc ? 1 : fc_gr);
    if (!foc && !(state & BP_W_DEFAULT)) {              /* the bevel: light top-left, dark bottom-right */
        if (!bp_mono) { bx_fill(x + 1, y + 1, x2 - 1, y + 1, 15); bx_fill(x + 1, y + 1, x + 1, y2 - 1, 15); }
        bx_fill(x + 1, y2 - 1, x2 - 1, y2 - 1, bp_mono ? 0 : 8);
        bx_fill(x2 - 1, y + 1, x2 - 1, y2 - 1, bp_mono ? 0 : 8);
    }
    label = fc_fits(label, fc_fb, r->w - 6);
    tw = bx_twidth(label, fc_fb);
    ty = y + (r->h - fc_fh) / 2;
    bx_text(x + (r->w - tw) / 2, ty, label, foc ? 15 : dis ? (bp_mono ? 0 : 8) : 0, fc_fb);
    if (dis && bp_mono)                                  /* a screen door over the label */
        for (i = 0; i < fc_fh; i++) bx_dots(x + (r->w - tw) / 2 + (i & 1), ty + i, x + (r->w + tw) / 2, ty + i, 2, 15);
}
int bp_w_text(int x, int y, const char *s, int colour, int font) { return bx_text(x, y, s, colour, font); }
int bp_w_textw(const char *s, int font) { return bx_twidth(s, font); }
int bp_w_fonth(int font) { return bx_fonth(font); }
int bp_w_sprite(int x, int y, int art_id) { return bx_spr(x, y, art_id); }
unsigned bp_w_spritesz(int art_id) { return bx_size(art_id); }
void bp_w_fill(int x1, int y1, int x2, int y2, int colour) { bx_fill(x1, y1, x2, y2, colour); }

/* ---- the log ------------------------------------------------------------ */
void bp_log_add(const char *line)
{
    int i;
    for (i = BP_LOG_LINES - 1; i > 0; i--) os88_strcpy(fc_log[i], fc_log[i - 1], BP_LOG_LEN + 1);
    os88_strcpy(fc_log[0], line, BP_LOG_LEN + 1);
    fc_logver++;
    fc_dirty |= BP_D_LOG;
}
const char *bp_log_line(int i) { if (i < 0 || i >= BP_LOG_LINES) return ""; return fc_log[i]; }

/* the public doors are one resident wrapper each: the bodies are overlay code */
void bp_face_layout(int w, int h) { fc_layout(w, h); }
void bp_face_paint(void *win, int full) { fc_paintall(win, full); }
int bp_face_key(int ascii, int scan) { return ovl_fc_key(ascii, scan); }
int bp_face_hit_space(int x, int y) { return fc_hitspace(x, y); }
void bp_w_panel(const BPRect *r, const char *title) { fc_wpanel(r, title); }
void bp_w_box(const BPRect *r, int fill, int edge) { fc_wbox(r, fill, edge); }
void bp_w_button(const BPRect *r, const char *label, int state) { fc_wbutton(r, label, state); }
