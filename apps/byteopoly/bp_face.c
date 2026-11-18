/* Byteopoly - the face: layouts, the board, the widget kit (SPEC.md 131.8;
 * the sidebar is bp_side.c, 131.8.1). Lane: face. Drawing goes through Breadboard XT's band
 * core (apps/breadxt/draw.inc, shared as source, SPEC.md 130.6.2): every
 * repaint is strips, each one band composed and put down with one call.
 *
 * Every rect the face paints has an x and a width that are multiples of 8
 * (the band's contract) and every pixel of it is written by the section that
 * owns it, so a strip never carries stale band bytes to the glass. A section
 * is repainted only when its signature (a hash of every input it draws)
 * changed, or when it was dirtied with BP_D_FORCE. */
#include "art.h"
#include "art2.h"
#include "gk.h"                          /* gk_*: the game kit (SPEC.md 133) */

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

/* the sidebar (bp_side.c, included after this file: SPEC.md 131.8.1) */
static void fc_sidelayout(void);
static void fc_players(void);
static void ovl_fc_details(void);
static void fc_link(void);
static void fc_logbox(void);
static unsigned fc_sigplayers(void);
static unsigned fc_sigdetails(void);
static unsigned fc_siglink(void);

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
    s = bx_str(s);
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
    os88_strcpy(fc_fit, bx_str(s), sizeof(fc_fit));
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
    gk_init(bpp);                       /* the kit's role table: colour, or mono under 4 bpp */
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
    int ax, aw, sw, hb;
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
    fc_sidelayout();
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
/* a corner or special space below its name: the picture with the amount
 * beside it (BOOT, the charges) where both fit, SMALL before TINY (131.10),
 * else the SMALL picture alone, else the TINY pair or picture, else the
 * amount alone */
static void ovl_fc_special(const BPSpace *sp, int s, int x, int sy, int nw, int ah)
{
    const char *a;
    int aw, k, t, id, pw, ph, w;
    unsigned z;
    a = fc_cellsub(sp);
    aw = *a ? bx_twidth(a, fc_fs) + 3 : 0;
    id = fc_specart(sp, s);
    for (k = 0; k < 4 && id >= 0; k++) {
        t = fc_mono(id);
#ifdef BP_A_TSPECIAL
        if (k & 2) t = bp_mono ? BP_A_MTSPECIAL(id) : BP_A_TSPECIAL(id);
#else
        if (k & 2) break;
#endif
        z = bx_size(t);
        if (!z) continue;
        pw = z & 255; ph = z >> 8;
        w = pw + ((k & 1) ? 0 : aw);
        if (w > nw || ph > ah) continue;
        x += (fc_cw - w) / 2;
        bx_spr(x, sy + (ah - ph) / 2, t);
        if (!(k & 1) && aw) bx_text(x + pw + 3, sy + (ah - fc_sh) / 2, a, 0, fc_fs);
        return;
    }
    fc_ctext(x + 2, x + fc_cw - 3, sy + 2, a, 0, fc_fs);
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
    z = bx_size(id);                                    /* the art is required (131.10.7) */
    pw = z & 255; ph = z >> 8;
    if (lvl >= BP_MAX_LEVEL) { bx_spr(xr - pw + 1, y, id); return; }
    cols = mw / (pw + 1);
    if (cols < 1) cols = 1;
    for (i = 0; i < lvl; i++)
        bx_spr(xr - (i % cols + 1) * (pw + 1) + 2, y + (i / cols) * (ph + 1), fc_mono(id));
}
/* ---- a FULL cell (56 x 50): the concept's ivory card (SPEC.md 131.8.2) ---- */
static char fc_nm[28];                                  /* a cell name's first line */
/* the cell's name from its text handle: fc_nm the first line, the answer the
 * second (0 when it is one line); "/" is the break */
static const char *fc_names(int s)
{
    char *p;
    os88_strcpy(fc_nm, bx_str((const char *)((unsigned)TF_CELL00 + s)), sizeof(fc_nm));
    for (p = fc_nm; *p; p++)
        if (*p == '/') { *p++ = 0; return p; }
    return 0;
}
static void fc_ln(int x, int y, int w, const char *s, int font)
{
    gk_text(x + 1, y, w, s, 0, font, GK_CENTRE | GK_FIT);
}
/* the colour or 1bpp twin of a corner picture */
static int fc_cpic(const BPSpace *sp, int s)
{
    int id;
    switch (sp->kind) {
    case BP_K_BOOT: id = BP_A2_C_BOOT; break;
    case BP_K_REPAIR: id = BP_A2_C_REPAIR; break;
    case BP_K_SWAP: id = BP_A2_C_SWAP; break;
    case BP_K_CRASH: id = BP_A2_C_CRASH; break;
    case BP_K_EVENT: id = sp->group ? BP_A2_C_SYSERR : BP_A2_C_FLOPPY; break;
    default: return fc_mono(fc_specart(sp, s));
    }
    if (bp_mono && id == BP_A2_C_BOOT) return BP_A2_M_C_BOOT;
    return id;
}
/* the selection: yellow 2 px inside the cell's black edge (black, 3 px, on 1bpp) */
static void fc_selframe(int x, int y, int x2, int y2)
{
    int c, k;
    k = bp_mono ? 0 : 14;
    for (c = 1; c <= (bp_mono ? 3 : 2); c++) {
        bx_fill(x + c, y + c, x2 - c, y + c, k);
        bx_fill(x + c, y2 - c, x2 - c, y2 - c, k);
        bx_fill(x + c, y + c, x + c, y2 - c, k);
        bx_fill(x2 - c, y + c, x2 - c, y2 - c, k);
    }
}
static void fc_cfull(int s, int x, int y, int x2, int y2, const BPSpace *sp, int own, int lvl, int mort, int sel)
{
    const char *l2;
    int cw, nw, prop, ny, id, i, pw, ph, c;
    unsigned z;
    cw = fc_cw; nw = cw - 2;
    prop = sp->prop;
    l2 = fc_names(s);
    if (prop != BP_NOBODY) {
        c = sp->kind == BP_K_UTILITY ? BP_GROUPS - 1 : sp->group;
        bx_fill(x + 1, y + 1, x2 - 1, y + 7, bp_mono ? 0 : fc_gcol[c]);
        if (sel) fc_selframe(x, y, x2, y2);
        fc_buf[0] = sp->kind == BP_K_UTILITY ? 'U' : bp_group_letter(sp->group); fc_buf[1] = 0;
        bx_text(x + 4, y + 1, fc_buf, 15, fc_fs);
        if (own != BP_NOBODY) fc_tok(x + 14, y + 1, own, bp_game.token[own], 15);
        ny = y + 9;
        fc_ln(x, ny, nw, fc_nm, fc_fs);
        if (l2) { ny += 8; fc_ln(x, ny, nw, l2, fc_fs); }
        ny += 7;
        i = y + 40 - ny;                                /* the picture's rows: down to the price line */
        id = fc_pick(prop, nw, i);
        if (mort) bx_dither(x + 3, ny, x2 - 3, y + 39, 15, 7, 2);
        if (id >= 0) bx_spr(x + (cw - fc_pw) / 2, ny + (i - fc_ph) / 2, id);
        if (mort) bx_spr(x + 3, ny, fc_mono(BP_A_MORTGAGE));
        if (fc_seatat(s, 0) >= 0) fc_tokens(x, y + 41, cw, s, 0);
        else {
            i = fc_ap(fc_buf, 0, fc_dec(sp->price));
            fc_ap(fc_buf, i, TA_CR);
            fc_ln(x, y + 41, nw, fc_buf, fc_fs);
        }
        if (lvl >= BP_MAX_LEVEL) bx_spr(x2 - 15, y + 1, fc_mono(BP_A_WORKSHOP));
        else
            for (i = 0; i < lvl; i++) bx_spr(x2 - 17 - (lvl - 1 - i) * 5, y + 1, BP_A2_CHIP);
    } else {                                            /* a corner, a deck, a charge */
        if (sel) fc_selframe(x, y, x2, y2);
        c = sp->kind == BP_K_CHARGE;
        i = c ? fc_fs : sp->kind == BP_K_CRASH ? BP_F_BIG12 : fc_fb;   /* charges are small print, CRASH! wants a real '!' */
        ny = y + (sp->kind == BP_K_BOOT ? 1 : 3);
        fc_ln(x, ny, nw, fc_nm, i);
        ny += bx_fonth(i);
        if (l2) { fc_ln(x, ny, nw, l2, i); ny += bx_fonth(i); }
        id = fc_cpic(sp, s);
        z = bx_size(id);
        pw = z & 255; ph = z >> 8;
        if (sp->kind == BP_K_BOOT) {                    /* BOOT / arrow / Collect 200 */
            ny = y + 10;
            l2 = fc_names(28);
            fc_ln(x, y2 - 15, nw, fc_nm, fc_fs);
            fc_ln(x, y2 - 8, nw, l2, fc_fs);
        } else if (c) {
            fc_buf[0] = '-'; os88_strcpy(fc_buf + 1, fc_dec(sp->price), 8);
            fc_ln(x, y2 - 8, nw, fc_buf, fc_fs);
            ny = y + 17;
        } else ny += (y2 - 1 - ny - ph) / 2;
        if (z) bx_spr(x + (cw - pw) / 2, ny, id);
        /* the seats standing here (QA-1): over the picture's foot, or under
         * the name where BOOT and a charge print their bottom rows */
        if (fc_seatat(s, 0) >= 0)
            fc_tokens(x, sp->kind == BP_K_BOOT || c ? y + 11 : y2 - 9, cw, s, 0);
    }
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
    if (ch >= 44) fc_cfull(s, x, y, x2, y2, sp, own, lvl, mort, sel);
    else if (ch >= 28) {                                /* MID cells */
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
        sy = y + (prop != BP_NOBODY ? bar + 2 : 2);
        if (sel) bx_fill(x + 2, sy, x2 - 2, sy + fc_sh, 1);
        fc_ctext(x + 2, x2 - 2, sy, ovl_fc_cfits(sp->shortname, nw), ink, fc_fs);
        sy += fc_sh;
        ah = y2 - sy - 1;
        id = prop != BP_NOBODY ? fc_pick(prop, nw, ah) : -1;
        if (mort) bx_dither(x + 2, sy, x2 - 2, y2 - 2, 15, 7, 2);
        if (id >= 0) bx_spr(x + (cw - fc_pw) / 2, sy + (ah - fc_ph) / 2, id);
        else if (prop == BP_NOBODY) ovl_fc_special(sp, s, x, sy, nw, ah);
        if (lvl) fc_lvl(x2 - 3, sy + 1, lvl, id >= 0 ? (cw - fc_pw) / 2 - 3 : 20);
        if (mort) {                                     /* the left margin, top of the picture row */
            bx_spr(x + 3, sy + 1, fc_mono(BP_A_MORTGAGE));
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
/* the bottom bar the panel owns while the centre shows its art (DECISIONS D9):
 * the landing / Buy panel and the turn bar draw into the bottom BARH rows of
 * bp_r_center, the art stands above them. 0 on COMPACT: the panel sizes its own bar */
#define FC_BAR_FULL 96
#define FC_BAR_MID 64
int bp_face_barh(void)
{
    return bp_layout == BP_LAY_FULL ? FC_BAR_FULL : bp_layout == BP_LAY_MID ? FC_BAR_MID : 0;
}
static void ovl_fc_die(int x, int y, int n) { bx_spr(x, y, BP_A_DIE(n)); }
static void ovl_fc_centre(void)
{
    static const char logo[] = "BYTEOPOLY";
    static int iv[3];
    int x, y, w, h, kw, kh, i, ly, dsz, live;
    unsigned z;
    x = bp_r_center.x; y = bp_r_center.y; w = bp_r_center.w; h = bp_r_center.h;
    if (bp_pn_covers() == BP_PN_CENTRE) {              /* the panel owns all but the 2 px edge */
        gk_box(x, y, w, h, GK_SUNKEN2, -1);
        return;
    }
    live = fc_live() && bp_game.d1 > 0 && bp_game.d1 < 7 && bp_game.d2 > 0 && bp_game.d2 < 7;
    if (bp_layout != BP_LAY_COMPACT) {                  /* the scene: ground, cards, mascot, key caps, dice */
        iv[0] = BP_A2_DIE1 - 1 + bp_game.d1; iv[1] = BP_A2_DIE1 - 1 + bp_game.d2; iv[2] = live;
        if (bp_layout == BP_LAY_MID) iv[0] = BP_A_DIE1 - 1 + bp_game.d1, iv[1] = BP_A_DIE1 - 1 + bp_game.d2;
        gk_scene(BP_A2_SC_BOARD + (bp_layout == BP_LAY_MID), x, y, 0, iv);
        fc_aty = y + (bp_layout == BP_LAY_MID ? 40 : 152);   /* above this line the art stands */
        return;
    }
    bx_fill(x, y, x + w - 1, y, 0); bx_fill(x, y + h - 1, x + w - 1, y + h - 1, 0);
    bx_fill(x, y, x, y + h - 1, 0); bx_fill(x + w - 1, y, x + w - 1, y + h - 1, 0);
    bx_dither(x + 1, y + 1, x + w - 2, y + h - 2, 3, 11, 2);
    z = bx_size(BP_A_KEYCAP);
    kw = (int)(z & 255) + 1; kh = (int)(z >> 8) + 1;
    ly = y + 8;
    for (i = 0; i < 9; i++) {
        bx_spr(x + (w - 9 * kw) / 2 + i * kw, ly, BP_A_KEYCAP);
        fc_buf[0] = logo[i]; fc_buf[1] = 0;
        fc_ctext(x + (w - 9 * kw) / 2 + i * kw, x + (w - 9 * kw) / 2 + i * kw + kw - 3, ly + (kh - 2 - bx_fonth(fc_ft)) / 2, fc_buf, 0, fc_ft);
    }
    fc_aty = y;
    if (live) {
        dsz = (int)(bx_size(BP_A_DIE1) & 255);
        ovl_fc_die(x + 12, ly, bp_game.d1);
        ovl_fc_die(x + w - 12 - dsz, ly, bp_game.d2);
    }
}

/* ---- panel boxes (the widget kit's, used by bp_w_panel/bp_w_box) ------- */
static void fc_wpanel(const BPRect *r, const char *title)
{
    gk_panel(r->x, r->y, r->w, r->h, fc_fh + 3, title, fc_fb, GK_RAISED);
}
/* a white fill is a sunken well, any other fill a raised slab (the kit) */
static void fc_wbox(const BPRect *r, int fill, int edge)
{
    (void)edge;
    gk_box(r->x, r->y, r->w, r->h, fill == 15 ? GK_SUNKEN2 : GK_RAISED, fill);
}

/* ---- the hint bar ------------------------------------------------------- */
static void fc_hint(void)
{
    const char *h, *sp;
    int i, n, x, y, kw, lw, x2, ch, step, kh;
    x2 = bp_r_hint.x + bp_r_hint.w - 1;
    bx_fill(bp_r_hint.x, bp_r_hint.y, x2, bp_r_hint.y + bp_r_hint.h - 1, 1);
    for (n = 0; bp_pn_hint(n) != 0; n++) ;
    if (n == 0) return;
    kh = fc_fh + 5;                                     /* a key cap: the label's rows and its sides */
    if (kh > bp_r_hint.h - 2) kh = bp_r_hint.h - 2;
    y = bp_r_hint.y + (bp_r_hint.h - kh) / 2;
    ch = bp_r_hint.w - 12;
    step = ch / n;
    for (i = 0; i < n; i++) {                           /* the entries spread across the bar */
        h = bx_str(bp_pn_hint(i));
        for (sp = h; *sp && *sp != ' '; sp++) ;
        kw = sp - h;
        if (!*sp) { kw = 0; sp = h; } else sp++;
        x = bp_r_hint.x + 6 + i * step;
        lw = bx_twidth(sp, fc_fb);
        if (kw > 0 && kw < 8) {
            os88_memcpy(fc_fit, h, kw); fc_fit[kw] = 0;
            kw = bx_twidth(fc_fit, fc_fb) + 8;
            if (x + kw + 4 + lw > x2) break;
            if (bp_layout == BP_LAY_FULL) gk_keycap(x, y, kw, kh, fc_fit, fc_fb);
            else {                                      /* 8-row labels do not read inside the kit's cap (1bpp) */
                bx_fill(x, y, x + kw - 1, y + kh - 2, 15);
                bx_fill(x, y + kh - 1, x + kw - 1, y + kh - 1, bp_mono ? 15 : 8);
                bx_text(x + 4, y + (kh - 1 - fc_fh) / 2, fc_fit, 0, fc_fb);
            }
            kw += 4;
        } else kw = 0;
        if (x + kw + lw > x2) break;
        gk_text(x + kw, bp_r_hint.y + (bp_r_hint.h - fc_fh) / 2, 0, sp, 15, fc_fb, 0);
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
        if ((fc_secs & BP_D_HINT) && fc_hit(&bp_r_hint)) fc_hint();
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
static unsigned fc_sighint(void)
{
    unsigned h;
    const char *m;
    int i;
    h = 5;
    for (i = 0; (m = bp_pn_hint(i)) != 0; i++) { h = fc_mix(h, i); for (m = bx_str(m); *m; m++) h = fc_mix(h, *m); }
    return h;
}

void bp_face_dirty(unsigned mask) { fc_dirty |= mask; }
int bp_face_isdirty(void) { return fc_dirty != 0 || fc_dsp[0] != 0 || fc_dsp[1] != 0; }
/* damage smaller than a section (131.8.5): the rects named before a paint,
 * as their bounding box (x2, y2 exclusive); FC_D_RECT says it is set */
#define FC_D_RECT 0x4000
static int fc_dx1, fc_dy1, fc_dx2, fc_dy2;
static void ovl_fc_drect(const BPRect *r)
{
    if (!r || r->w <= 0 || r->h <= 0) return;
    if (!(fc_dirty & FC_D_RECT)) {
        fc_dirty |= FC_D_RECT;
        fc_dx1 = fc_dx2 = r->x; fc_dy1 = fc_dy2 = r->y;
    }
    if (r->x < fc_dx1) fc_dx1 = r->x;
    if (r->y < fc_dy1) fc_dy1 = r->y;
    if (r->x + r->w > fc_dx2) fc_dx2 = r->x + r->w;
    if (r->y + r->h > fc_dy2) fc_dy2 = r->y + r->h;
}
void bp_face_dirty_rect(const BPRect *r) { ovl_fc_drect(r); }
/* paint the named damage; centre: the whole centre was painted this pass */
static void ovl_fc_rects(int cov, int centre)
{
    static BPRect r;
    unsigned secs;
    int x2, y2, in;
    in = fc_dx1 >= bp_r_center.x && fc_dy1 >= bp_r_center.y &&
         fc_dx2 <= bp_r_center.x + bp_r_center.w && fc_dy2 <= bp_r_center.y + bp_r_center.h;
    if (centre && in) return;
    r.x = fc_dx1; r.y = fc_dy1; x2 = fc_dx2; y2 = fc_dy2;   /* clipped to the content, x to 8 */
    if (r.x < fc_all.x) r.x = fc_all.x;
    if (r.y < fc_all.y) r.y = fc_all.y;
    if (y2 > fc_all.y + fc_all.h) y2 = fc_all.y + fc_all.h;
    r.x = fc_all.x + ((r.x - fc_all.x) & ~7);
    x2 = fc_all.x + ((x2 - fc_all.x + 7) & ~7);
    if (x2 > fc_all.x + fc_all.w) x2 = fc_all.x + fc_all.w;
    r.w = x2 - r.x; r.h = y2 - r.y;
    if (r.w <= 0 || r.h <= 0) return;
    fc_sx1 = r.x; fc_sx2 = x2 - 1; fc_sy1 = r.y; fc_sy2 = y2 - 1;
    secs = BP_D_CENTER;                                 /* the sections it touches */
    if (cov != BP_PN_FULL) {
        if (!fc_hit(&bp_r_center)) secs = 0;
        if (!in && fc_hit(&bp_r_board)) secs |= BP_D_BOARD;
        if (fc_hit(&fc_rp)) secs |= BP_D_PLAYERS;
        if (fc_hit(&fc_rd)) secs |= BP_D_DETAILS;
        if (fc_hit(&fc_rl)) secs |= BP_D_LINK;
        if (fc_hit(&fc_rg)) secs |= BP_D_LOG;
        if (fc_hit(&bp_r_hint)) secs |= BP_D_HINT;
    }
    fc_paintr(&r, secs);
}
void bp_face_dirty_space(int space)
{
    if (space < 0 || space >= BP_SPACES) return;
    fc_dsp[space >> 4] |= 1u << (space & 15);
    fc_dirty |= BP_D_BOARD;
}
static void fc_fullcentre(void)
{
    bp_r_center.x = fc_all.x; bp_r_center.y = fc_all.y; bp_r_center.w = fc_all.w; bp_r_center.h = bp_r_hint.y - fc_all.y;   /* the hint bar stays below it (D17) */
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
        fc_dsp[0] = fc_dsp[1] = 0; fc_dirty = 0; fc_art = cov == 0;
        fc_paintr(&fc_all, cov == BP_PN_FULL ? BP_D_CENTER | BP_D_HINT : BP_D_ALL);
        return;
    }
    d = fc_dirty;
    if (!d) return;
    fc_dirty = 0;
    if (cov == BP_PN_FULL) {
        if (d & BP_D_CENTER) { fc_hsig = fc_sighint(); fc_paintr(&fc_all, BP_D_CENTER | BP_D_HINT); }
        else {
            if (d & BP_D_HINT) { sg = fc_sighint(); if (sg != fc_hsig || (d & BP_D_FORCE)) { fc_hsig = sg; fc_paintr(&bp_r_hint, BP_D_HINT); } }
            if (d & FC_D_RECT) ovl_fc_rects(cov, 0);
        }
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
    int k;
    k = (state & BP_W_DISABLED) ? GK_B_DISABLED : (state & BP_W_FOCUS) ? GK_B_FOCUS :
        (state & BP_W_DEFAULT) ? GK_B_DEFAULT : GK_B_NORMAL;
    gk_button(r->x, r->y, r->w, r->h, label, fc_fb, k | GK_B_FLAT);
}
int bp_w_text(int x, int y, const char *s, int colour, int font) { return bx_text(x, y, s, colour, font); }
int bp_w_textw(const char *s, int font) { return bx_twidth(s, font); }
int bp_w_fonth(int font) { return bx_fonth(font); }
int bp_w_sprite(int x, int y, int art_id) { return bx_spr(x, y, art_id); }
unsigned bp_w_spritesz(int art_id) { return bx_size(art_id); }
void bp_w_fill(int x1, int y1, int x2, int y2, int colour) { bx_fill(x1, y1, x2, y2, colour); }


/* the public doors are one resident wrapper each: the bodies are overlay code */
void bp_face_layout(int w, int h) { fc_layout(w, h); }
void bp_face_paint(void *win, int full) { fc_paintall(win, full); }
int bp_face_key(int ascii, int scan) { return ovl_fc_key(ascii, scan); }
int bp_face_hit_space(int x, int y) { return fc_hitspace(x, y); }
void bp_w_panel(const BPRect *r, const char *title) { fc_wpanel(r, title); }
void bp_w_box(const BPRect *r, int fill, int edge) { fc_wbox(r, fill, edge); }
void bp_w_button(const BPRect *r, const char *label, int state) { fc_wbutton(r, label, state); }
