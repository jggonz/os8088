/* Byteopoly host harness: the panels (SPEC.md 131.9.7-131.9.11), the
 * SHIPPING bp_panels.c over a fake face and the real engine and session
 * (panels lane). The fake widget kit records what is drawn; a scenario is
 * keys and clicks in, commands and drawn text out. */
#include "bphost.h"
#include "byteoply.c"
#include "art.h"
/* a help record: 50 rules lines, #KEYS, 5 keys lines (0 = no art yet) */
static int bp_t_help;
int bx_artstr(int id, int n, char *buf)
{
    (void)id;
    buf[0] = 0;
    if (!bp_t_help || n < 0 || n > 55) return 0;
    if (n == 50) strcpy(buf, "#KEYS");
    else if (n < 50) snprintf(buf, 52, "rule %02d", n);
    else snprintf(buf, 52, "key %02d", n - 51);
    return 1;
}
/* the face's damage doors, recorded (131.9.7) */
static unsigned bp_t_dirty;
static int bp_t_rects, bp_t_ry, bp_t_rh;
void bp_face_dirty(unsigned mask) { bp_t_dirty |= mask; }
void bp_face_dirty_rect(const BPRect *r) { bp_t_rects++; bp_t_ry = r->y; bp_t_rh = r->h; }
static void t_clean(void) { bp_t_dirty = 0; bp_t_rects = 0; }
/* the fake face: geometry plus a transcript of every text drawn */
BPRect bp_r_board, bp_r_center, bp_r_side, bp_r_hint;
BPByte bp_layout, bp_mono, bp_sel;
static char bp_t_ink[8192];
static int bp_t_buttons;
void bp_w_panel(const BPRect *r, const char *title) { (void)r; strcat(bp_t_ink, title); strcat(bp_t_ink, "\n"); }
void bp_w_box(const BPRect *r, int fill, int edge) { (void)r; (void)fill; (void)edge; }
void bp_w_button(const BPRect *r, const char *label, int state)
{ (void)r; (void)state; bp_t_buttons++; strcat(bp_t_ink, "["); strcat(bp_t_ink, label); strcat(bp_t_ink, "]\n"); }
int bp_w_text(int x, int y, const char *s, int colour, int font)
{ (void)x; (void)y; (void)colour; (void)font; strcat(bp_t_ink, s); strcat(bp_t_ink, "\n"); return (int)strlen(s) * 8; }
int bp_w_textw(const char *s, int font) { (void)font; return (int)strlen(s) * 8; }
int bp_w_fonth(int font) { (void)font; return 8; }
int bp_w_sprite(int x, int y, int id) { (void)x; (void)y; (void)id; return 0; }
unsigned bp_w_spritesz(int id) { (void)id; return 0; }
void bp_w_fill(int x1, int y1, int x2, int y2, int c) { (void)x1; (void)y1; (void)x2; (void)y2; (void)c; }
void bp_log_add(const char *l) { (void)l; }
const char *bp_log_line(int i) { (void)i; return ""; }
/* the session's UI hooks and port query: weak, so link's own win when it lands */
__attribute__((weak)) int bp_ck_port(void) { return 0; }
__attribute__((weak)) void bp_ui_committed(const BPResult *r) { (void)r; }
__attribute__((weak)) void bp_ui_link(void) {}
__attribute__((weak)) void bp_ui_chat(int machine, const char *text) { (void)machine; (void)text; }
#include "../../cc/gk.h"
/* the game kit's widgets, recorded: the text a panel hands them (and the
 * dynamic strings of a scene) lands in the transcript like bp_w_text's */
static void t_ink(const char *s) { if (s) { strcat(bp_t_ink, s); strcat(bp_t_ink, "\n"); } }
void gk_box(int x, int y, int w, int h, int style, int fill) { (void)x; (void)y; (void)w; (void)h; (void)style; (void)fill; }
void gk_hline(int x, int y, int w, int raised) { (void)x; (void)y; (void)w; (void)raised; }
int gk_text(int x, int y, int w, const char *s, int colour, int font, int flags)
{ (void)x; (void)y; (void)w; (void)colour; (void)font; (void)flags; t_ink(s); return s ? (int)strlen(s) * 8 : 0; }
int gk_textbox(int x, int y, int w, int h, const char *s, int colour, int font, int flags)
{ (void)h; return gk_text(x, y, w, s, colour, font, flags); }
void gk_titlebar(int x, int y, int w, int h, const char *s, int font, int flags)
{ (void)x; (void)y; (void)w; (void)h; (void)font; (void)flags; t_ink(s); }
int gk_panel(int x, int y, int w, int h, int bar, const char *title, int font, int style)
{ (void)x; (void)w; (void)h; (void)font; (void)style; strcat(bp_t_ink, title); strcat(bp_t_ink, "\n"); return y + bar; }
void gk_banner(int x, int y, int w, int h, const char *s, int font, int fill)
{ (void)x; (void)y; (void)w; (void)h; (void)font; (void)fill; t_ink(s); }
void gk_radio(int x, int y, int size, int on) { (void)x; (void)y; (void)size; (void)on; }
int gk_sprshadow(int x, int y, int id, int d) { (void)x; (void)y; (void)id; (void)d; return 0; }
int gk_role(int role) { (void)role; return 0; }
/* the kit's scenes (131.9.12), faked: a scene's string slots go to the
 * transcript (a handle is its literal here), its tags are plausible rects:
 * #200 the panel, #202 the button row at its foot, #201 the status line
 * above it, any other (the auction's editor, the trade's lists) mid-panel */
static int bp_t_scene;
int gk_scene(int id, int x, int y, const char *const *sv, const int *iv)
{
    int i;
    (void)x; (void)y; (void)iv;
    bp_t_scene = id;
    if (id >= 430 && id < 440) { for (i = 0; i < 6; i++) if (sv[i]) { strcat(bp_t_ink, sv[i]); strcat(bp_t_ink, "\n"); } return 1; }
    for (i = 1; i < 21; i++)
        if (sv[i] && sv[i][0]) { strcat(bp_t_ink, sv[i]); strcat(bp_t_ink, "\n"); }
    return 1;
}
int gk_scene_rect(int id, int tag, int *r)
{
    (void)id;
    r[0] = 10; r[2] = bp_r_center.w - 20; r[3] = 20;
    r[1] = tag == 200 ? 4 : tag == 202 ? bp_r_center.h - 30 : tag == 201 ? bp_r_center.h - 50 : 60;
    if (tag == 200) r[3] = bp_r_center.h - 8;
    return 1;
}
#include "bp_panels.c"
/* ---- helpers ---- */
static void t_paint(void) { bp_t_ink[0] = 0; bp_t_buttons = 0; bp_t_scene = 0; bp_gp_draw(); }
static int t_has(const char *s) { return strstr(bp_t_ink, s) != 0; }
static void t_geom(int layout)
{
    bp_layout = (BPByte)layout;
    bp_r_center.x = 100; bp_r_center.y = 80; bp_r_center.w = 320; bp_r_center.h = layout == BP_LAY_COMPACT ? 90 : 240;
}
static void t_new(int nseats)
{
    BPSetup su;
    int i;
    bp_setup_default(&su);
    su.nseats = (BPByte)nseats;
    for (i = 0; i < nseats; i++) { snprintf(su.name[i], sizeof(su.name[i]), "P%d", i + 1); su.token[i] = (BPByte)i; su.machine[i] = 0; }
    bp_sess_local();
    BP_CHECK(bp_sess_new(&su) == BP_OK, "new match");
}
/* a key as the app delivers it: the panels, the repaint, then keyend */
static void t_rkey(int a, int sc) { t_clean(); bp_gp_key(a, sc); t_paint(); bp_gp_keyend(); }
/* a key at a human pace: pressed 25 ticks after the last thing drawn */
static void t_key(int a, int sc) { bp_host_ticks += 25; t_rkey(a, sc); }
static int t_fockey(void) { return gp_bkey[gp_foc]; }
static void t_tick(int n) { while (n--) { bp_host_ticks++; bp_gp_tick(); } }
/* the uplifted panels (131.9.12), every layout: a trade of SEVERAL assets
 * each way (the wrapped list) and of one computer plus cash (the card), and
 * the link-lost panel of a cable match */
static void t_uplift(void)
{
    int lay, one;
    for (lay = BP_LAY_FULL; lay <= BP_LAY_COMPACT; lay++)
        for (one = 0; one < 2; one++) {
            t_geom(lay);
            t_new(2);
            bp_game.owner[1] = 0; bp_game.owner[14] = 0; bp_game.owner[2] = 1; bp_game.owner[11] = 1;
            bp_game.mortgaged[11] = 1; bp_game.disks[0] = 1;
            bp_game.phase = BP_PH_TRADE; bp_game.offer_from = 0; bp_game.offer_to = 1;
            bp_game.decider = 1; gp_ack = 1; gp_lastd = 1;
            bp_game.offer_give.lo = one ? 1u << 14 : (1u << 14) | (1u << 1);
            bp_game.offer_disks = one ? 0 : 1;
            bp_game.offer_get.lo = (1u << 2) | (1u << 11);
            bp_u32_set(&bp_game.offer_cash, 100, 0);
            t_paint();
            if (t_has("Pass the keyboard")) { t_tick(30); t_key(13, 0x1C); }
            BP_CHECK(bp_t_scene == GP_SC(2), "trade review: the trade scene");
            BP_CHECK(t_has("[A] Accept") && t_has("[R] Reject"), "trade review: Accept and Reject");
            BP_CHECK(t_has(TP_MORTGAGED_P), "trade review: a mortgaged computer says so");
            BP_CHECK(one ? t_has(bp_space(bp_prop_space(14))->name)
                         : t_has(ovl_gp_pname(14)) && t_has(lay == BP_LAY_COMPACT ? ovl_gp_pname(1) : bp_space(bp_prop_space(1))->name),
                     "trade review: the first side named (its card's full name, the rest short)");
            BP_CHECK(one || lay == BP_LAY_COMPACT || t_has(TP_PLUS_), "trade review: the rest under the card");
            BP_CHECK(!one || t_has("100 cr"), "trade review: the cash the card's side adds");
        }
    t_geom(BP_LAY_FULL);
    t_new(2);
    bp_mode = BP_MODE_HOST; bp_here = 0;
    bpl.on = 1; bpl.ph = 2; bpl.heard = bpl.clk; bpl.left = 1;
    bp_game.machine[1] = 1; bp_game.decider = 0; bp_game.active = 0;
    t_paint();
    BP_CHECK(bp_t_scene == GP_SC(3), "link lost: the pause scene");
    BP_CHECK(t_has("Retry Link") && t_has(TP_PAIR_NONE), "link lost: Retry and the paired save's state");
    BP_CHECK(t_has(TP_S_PC), "link lost: whose PC is awaited");
    bpl.left = 0; bpl.on = 0; bp_mode = BP_MODE_LOCAL;
}
/* the fix round's checks (QA Qqa1-2, -3, -4, -6, Qqa2-4, -5, -14, -22, -23) */
static void t_extra(void)
{
    static BPState keep;
    int rev, i, n;
    /* -- focus: a new panel opens on its first ENABLED button -- */
    t_new(2);
    bp_host_ticks += 40; t_paint();
    t_key(13, 0x1C);                            /* confirm the opening handoff */
    BP_CHECK(bp_game.phase == BP_PH_MANAGE && !t_has("Pass the keyboard"), "match open, idle bar");
    BP_CHECK(t_fockey() == ' ', "the idle bar opens on Roll");
    /* the hint lists enabled buttons only (Qqa2-23) */
    BP_CHECK(bp_gp_hint(0) && !strcmp(bp_gp_hint(0), "Space Roll"), "hint: Roll first while it is legal");
    /* damage: Tab moves the focus and repaints only the button row (Qqa1-4) */
    t_key(9, 0x0F);
    BP_CHECK(t_fockey() != ' ' && t_fockey() != 'e' && gp_bwhy[gp_foc] == 0, "Tab skips the grey End Turn");
    BP_CHECK(!(bp_t_dirty & BP_D_CENTER) && bp_t_rects == 1 && !(bp_t_dirty & BP_D_HINT), "Tab: one rect, no centre, no hint");
    t_clean();
    bp_host_ticks += 25; bp_gp_key(0, 0x47);    /* Home: not a panel key */
    BP_CHECK(!bp_t_dirty && !bp_t_rects, "a key the panel ignores dirties nothing");
    bp_gp_keyend();
    /* F1 Rules, page to the end, Esc, then Enter: never Trade (Qqa1-2) */
    bp_t_help = 1;
    t_clean(); bp_gp_open(BP_GP_RULES); t_paint();
    BP_CHECK(bp_t_dirty & BP_D_CENTER, "opening Rules dirties the centre");
    BP_CHECK(t_has("rule 00") && t_fockey() == 'n', "Rules opens on page 1 focused on Next");
    BP_CHECK(gp_bwhy[0] != 0, "Prev is grey on the first page");
    for (n = 0; n < 12; n++) t_key(0, 0x51);    /* PgDn past the end */
    BP_CHECK(gp_help + 1 == gp_hpages && gp_bwhy[1] != 0, "paging stops on the last page, Next grey");
    BP_CHECK(t_fockey() == 'b', "the focus leaves the greyed Next for Prev");
    BP_CHECK(gp_hpages >= 2 && t_has("rule 49") && !t_has("#KEYS") && !t_has("key 0"), "the last Rules page ends at #KEYS (Qqa2-5)");
    t_key(27, 0x01);
    BP_CHECK(!bp_gp_covers(), "Esc closes Rules");
    BP_CHECK(t_fockey() == ' ', "after Rules the bar's focus is Roll again, not index 2");
    rev = (int)bp_game.rev.lo;
    t_key(' ', 0x39);
    BP_CHECK((int)bp_game.rev.lo == rev + 1, "rolled");
    if (bp_game.phase == BP_PH_MANAGE || bp_game.phase == BP_PH_AFTER) {
        BP_CHECK(gp_bwhy[t_fockey() == ' ' ? 0 : gp_foc] == 0 || bp_game.phase == BP_PH_MANAGE, "focus never rests on a greyed Roll");
        if (bp_game.phase == BP_PH_AFTER && !bp_game.doubles) {
            BP_CHECK(t_fockey() == 'e', "after the roll the focus moves to End Turn");
            BP_CHECK(strcmp(bp_gp_hint(0), "Space Roll") != 0, "the spent Roll leaves the hint");
        }
    }
    /* -- a held Enter on End Turn never acts for the next player (Qqa1-3) -- */
    for (n = 0; n < 40 && !(bp_game.phase == BP_PH_AFTER && !gp_hand); n++) {
        t_paint();
        if (t_has("Pass the keyboard")) t_key(13, 0x1C);
        else if (bp_game.phase == BP_PH_MANAGE) t_key(' ', 0x39);
        else if (bp_game.phase == BP_PH_BUY) t_key('b', 0x30);
        else if (bp_game.phase == BP_PH_CARD) t_key(13, 0x1C);
        else if (bp_game.phase == BP_PH_AUCTION) t_key('p', 0x19);
        else if (bp_game.phase == BP_PH_REPAIR) t_key('p', 0x19);
        else if (bp_game.phase == BP_PH_DEBT) t_key('s', 0x1F);
        else break;
    }
    BP_CHECK(bp_game.phase == BP_PH_AFTER, "reached a turn's end");
    if (bp_game.phase == BP_PH_AFTER) {
        i = bp_game.decider;
        while (t_fockey() != 'e') t_key(9, 0x0F);
        t_key(13, 0x1C);                        /* End Turn; the repaint took 8 s... */
        BP_CHECK(bp_game.decider != i && gp_hand, "End Turn raised the next handoff");
        rev = (int)bp_game.rev.lo;
        bp_host_ticks += 150;                   /* ...an XT repaint, the repeats queued */
        bp_gp_keyend();
        for (n = 0; n < 15; n++) t_rkey(13, 0x1C);     /* the queue drains back to back */
        for (n = 0; n < 30; n++) { bp_host_ticks += 2; t_rkey(13, 0x1C); }   /* still held */
        BP_CHECK(gp_hand && (int)bp_game.rev.lo == rev, "queued and held Enters leave the handoff up");
        bp_host_ticks += 19;
        t_rkey(13, 0x1C);                       /* 19 ticks: the slowest typematic delay is 18 */
        BP_CHECK(gp_hand, "19 quiet ticks are not enough");
        bp_host_ticks += 21;
        t_rkey(13, 0x1C);                       /* let go, then a fresh press */
        BP_CHECK(!gp_hand && (int)bp_game.rev.lo == rev, "a fresh Enter after letting go confirms, and only that");
    }
    /* -- damage of an editor key: the auction's + -- */
    for (n = 0; n < 200 && bp_game.phase != BP_PH_AUCTION; n++) {
        t_paint();
        if (t_has("Pass the keyboard")) t_key(13, 0x1C);
        else if (bp_game.phase == BP_PH_MANAGE) t_key(' ', 0x39);
        else if (bp_game.phase == BP_PH_AFTER) t_key('e', 0x12);
        else if (bp_game.phase == BP_PH_BUY) t_key('a', 0x1E);
        else if (bp_game.phase == BP_PH_CARD) t_key(13, 0x1C);
        else if (bp_game.phase == BP_PH_REPAIR) t_key('p', 0x19);
        else if (bp_game.phase == BP_PH_DEBT) t_key('s', 0x1F);
        else break;
    }
    t_paint();
    if (t_has("Pass the keyboard")) t_key(13, 0x1C);
    if (bp_game.phase == BP_PH_AUCTION) {
        BP_CHECK(t_fockey() == 'b', "an auction opens on Bid");
        t_key('+', 0x0D);
        BP_CHECK(!(bp_t_dirty & BP_D_CENTER) && bp_t_rects == 1, "a bid step is one rect, not the centre");
        BP_CHECK(bp_t_ry >= bp_r_center.y + 40 && bp_t_ry + bp_t_rh <= bp_r_center.y + bp_r_center.h,
                 "the bid step's rect is the editor and below, inside the centre");
        BP_CHECK(bp_t_dirty & BP_D_HINT, "the Bid label in the hint changed");
        t_key(9, 0x0F);
        BP_CHECK(bp_t_rects == 1 && !(bp_t_dirty & BP_D_HINT) && bp_t_ry > bp_r_center.y + bp_r_center.h - 40,
                 "Tab: the button row only, the hint untouched");
    } else BP_CHECK(0, "reached an auction");
    /* -- DEBT opens on Manage; its confirmation opens on No (Qqa2-4) -- */
    t_new(2);
    bp_host_ticks += 40; t_paint(); t_key(13, 0x1C);
    bp_game.phase = BP_PH_DEBT; bp_game.debt_to = 1; bp_u32_set(&bp_game.debt_amount, 4000, 0);
    bp_game.rev.lo++;
    t_paint();
    BP_CHECK(t_has("DEBT") && t_fockey() == 'm', "DEBT opens on Manage, not Bankrupt");
    t_key('k', 0x25);
    BP_CHECK(gp_conf && t_fockey() == 27 && t_has("Go bankrupt"), "the confirmation opens on No, its label whole");
    t_key(13, 0x1C);
    BP_CHECK(!gp_conf && bp_game.alive[0] && bp_game.phase == BP_PH_DEBT, "two Enters do not bankrupt");
    t_key('k', 0x25);
    for (n = 0; n < 12; n++) { bp_host_ticks += 2; t_rkey('k', 0x25); }   /* held */
    BP_CHECK(bp_game.alive[0] && gp_conf, "a held K stops at the confirmation");
    BP_CHECK(t_has("Let go, then press again"), "the confirmation says why");
    bp_host_ticks += 6; t_rkey('k', 0x25);
    BP_CHECK(bp_game.alive[0], "a K typed again at once does not answer");
    bp_host_ticks += 21; t_rkey('k', 0x25);
    BP_CHECK(!bp_game.alive[0] || bp_game.status != BP_ST_PLAY || bp_game.phase != BP_PH_DEBT,
             "a deliberate K after letting go declares bankruptcy");
    /* -- a Load of an earlier position: the handoff names the right player (Qqa1-6) -- */
    t_new(2);
    bp_host_ticks += 40; t_paint(); t_key(13, 0x1C);
    memcpy(&keep, &bp_game, sizeof(keep));
    for (n = 0; n < 60 && bp_game.decider == keep.decider; n++) {
        t_paint();
        if (t_has("Pass the keyboard")) t_key(13, 0x1C);
        else if (bp_game.phase == BP_PH_MANAGE) t_key(' ', 0x39);
        else if (bp_game.phase == BP_PH_AFTER) t_key('e', 0x12);
        else if (bp_game.phase == BP_PH_BUY) t_key('b', 0x30);
        else if (bp_game.phase == BP_PH_CARD) t_key(13, 0x1C);
        else break;
    }
    t_paint();
    BP_CHECK(gp_hand == bp_game.decider + 1, "the next player's handoff is up");
    memcpy(&bp_game, &keep, sizeof(keep));      /* "Load" the earlier position */
    t_paint();
    BP_CHECK(!gp_hand || gp_hand == bp_game.decider + 1, "after the Load no handoff names another player");
    /* -- an auction after doubles: the bar comes back focused on Roll (Qqa3-2) -- */
    {
        int seen = 0;
        t_new(2);
        bp_host_ticks += 40; t_paint(); t_key(13, 0x1C);
        for (n = 0; n < 600 && !seen; n++) {
            t_paint();
            if (t_has("Pass the keyboard")) t_key(13, 0x1C);
            else if (bp_game.phase == BP_PH_MANAGE) t_key(' ', 0x39);
            else if (bp_game.phase == BP_PH_AFTER) {
                if (bp_game.doubles && bp_game.d1 == bp_game.d2 && i == 1) {
                    seen = 1;
                    BP_CHECK(gp_bwhy[t_fockey() == ' ' ? 0 : gp_foc] == 0, "after the auction the focus is on an enabled button");
                    BP_CHECK(gp_bwhy[gp_foc] == 0, "no focus on a greyed button");
                } else t_key(bp_game.doubles ? ' ' : 'e', bp_game.doubles ? 0x39 : 0x12);
            } else if (bp_game.phase == BP_PH_BUY) { i = bp_game.doubles && bp_game.d1 == bp_game.d2; t_key('a', 0x1E); }
            else if (bp_game.phase == BP_PH_AUCTION) t_key('p', 0x19);
            else if (bp_game.phase == BP_PH_CARD) t_key(13, 0x1C);
            else if (bp_game.phase == BP_PH_REPAIR) t_key('p', 0x19);
            else if (bp_game.phase == BP_PH_DEBT) t_key('s', 0x1F);
            else break;
            if (bp_game.phase != BP_PH_AFTER) i = bp_game.phase == BP_PH_AUCTION ? i : (bp_game.phase == BP_PH_BUY ? i : 0);
        }
        BP_CHECK(seen, "reached the bar after a doubles auction");
    }
    /* -- the bar never rests on a grey button, however it got there (Qqa3-2) -- */
    t_new(2);
    bp_host_ticks += 40; t_paint(); t_key(13, 0x1C);
    BP_CHECK(bp_game.phase == BP_PH_MANAGE && gp_kind == GK_IDLE, "idle bar before the roll");
    gp_foc = 1;                                 /* End Turn, grey before the roll */
    t_paint();
    BP_CHECK(gp_bwhy[gp_foc] == 0 && t_fockey() == ' ', "a focus on a grey button moves to the first enabled");
    /* -- a composer refusal repaints from the lists down (Qqa3-1) -- */
    bp_game.owner[0] = bp_game.decider; bp_game.level[0] = 1; bp_game.owner[1] = bp_game.decider;
    t_key('t', 0x14);
    BP_CHECK(gp_kind == GK_COMPOSE, "the composer is open");
    t_key(' ', 0x39);                           /* the cursor's row is in an upgraded set */
    BP_CHECK(t_has("Upgraded sets cannot be traded"), "the refusal shows");
    BP_CHECK(!(bp_t_dirty & BP_D_CENTER) && bp_t_rects == 1 && bp_t_ry <= gp_vy + 1,
             "the status line appearing repaints from the column heads down, not mid-line");
    t_key(9, 0x0F);                             /* clears the refusal: the same band */
    BP_CHECK(bp_t_rects == 1 && bp_t_ry <= gp_vy + 1, "the status line going repaints from the column heads too");
    t_key(9, 0x0F);
    BP_CHECK(bp_t_rects == 1 && bp_t_ry == gp_r.y + gp_r.h - gp_bh - 4,
             "a focus move alone is the button row from its own top");
    bp_t_help = 0;
}
int main(void)
{
    int rev;
    t_geom(BP_LAY_FULL);
    /* nothing running: no panel, the foundation hints */
    memset(&bp_game, 0, sizeof(bp_game));
    BP_CHECK(!bp_gp_covers(), "no match, no panel");
    BP_CHECK(bp_gp_key('b', 0) == 0, "no match: keys fall through");
    t_new(2);
    t_paint();
    BP_CHECK(bp_gp_covers() && t_has("Pass the keyboard"), "handoff covers the centre");
    BP_CHECK(bp_gp_key(' ', 0x39) == 1 && bp_game.phase == BP_PH_MANAGE, "a key under the handoff changes nothing");
    t_tick(10);
    t_rkey(13, 0x1C);                           /* too soon: a held key's first repeat */
    BP_CHECK(t_has("Pass the keyboard"), "an early Enter does not confirm");
    t_tick(12);
    t_rkey(13, 0x1C);                           /* 12 quiet ticks after the last key: still held */
    BP_CHECK(t_has("Pass the keyboard"), "12 ticks after a key is not quiet enough");
    t_tick(21);
    t_rkey(13, 0x1C);                           /* 21 quiet ticks: fresh */
    BP_CHECK(!t_has("Pass the keyboard"), "a fresh Enter confirms");
    BP_CHECK(!bp_gp_covers() && t_has("Roll"), "the idle bar offers Roll");
    /* rolling goes through the session; the revision moves once */
    rev = (int)bp_game.rev.lo;
    t_key(' ', 0x39);
    BP_CHECK((int)bp_game.rev.lo == rev + 1, "Space rolls once");
    /* play on: roll, end turn, until a computer is landed on */
    {
        int n, d, bought = 0, auc = 0;
        for (n = 0; n < 400 && !(bought && auc); n++) {
            t_paint();
            d = bp_game.decider;
            if (bp_game.phase == BP_PH_MANAGE) t_key(' ', 0x39);
            else if (bp_game.phase == BP_PH_AFTER) t_key('e', 0x12);
            else if (bp_game.phase == BP_PH_BUY) {
                BP_CHECK(bp_t_scene == GP_SC(0) && !bp_gp_covers() == (bp_layout != BP_LAY_COMPACT) && t_has(" cr"),
                         "buy panel: the landing scene, a bottom panel, the price");
                if (!bought) {
                    t_key('b', 0x30);
                    BP_CHECK(bp_game.phase != BP_PH_BUY, "B buys");
                    bought = 1;
                } else {
                    t_key('a', 0x1E);
                    BP_CHECK(bp_game.phase == BP_PH_AUCTION, "A opens the auction");
                    t_paint();
                    BP_CHECK(bp_t_scene == GP_SC(1) && t_has("[B] Bid 10"), "auction shows the opening bid");
                    t_key('3', 0x04); t_key('0', 0x0B);
                    t_paint();
                    BP_CHECK(t_has("[B] Bid 30"), "typed digits set the bid");
                    t_key('b', 0x30);
                    BP_CHECK(bp_game.auc_high != BP_NOBODY, "B bids");
                    auc = 1;
                }
            } else if (bp_game.phase == BP_PH_CARD) t_key(13, 0x1C);
            else if (bp_game.phase == BP_PH_AUCTION) t_key('p', 0x19);
            else if (bp_game.phase == BP_PH_REPAIR) t_key('r', 0x13);
            else if (bp_game.phase == BP_PH_DEBT) t_key('k', 0x25), t_key('k', 0x25);
            else break;
            /* a handoff between the two local seats: wait it out */
            t_paint();
            if (t_has("Pass the keyboard")) { t_tick(30); t_key(13, 0x1C); }
            (void)d;
        }
        BP_CHECK(bought && auc, "a purchase and an auction were played through the panels");
    }
    t_extra();
    t_uplift();
    t_geom(BP_LAY_FULL);
    /* a seat on the other PC: facts shown, no buttons, no commands */
    t_new(2);
    bp_mode = BP_MODE_HOST; bp_here = 0;
    /* a cable match in play as the session keeps it (link lane, 131.6):
     * the link open, the match phase, heard this tick - bp_sess_mine reads
     * the game's machines only in a match, the lobby roster before one */
    bpl.on = 1; bpl.ph = 2; bpl.heard = bpl.clk;
    bp_game.machine[0] = 1; bp_game.machine[1] = 1;
    bp_game.decider = 0; bp_game.active = 0;
    t_paint();
    BP_CHECK(t_has("Waiting for P1 on the other PC"), "remote decider: waiting line");
    BP_CHECK(bp_t_buttons <= 1, "remote decider: no economic buttons");
    {
        int rv = (int)bp_game.rev.lo;
        t_key(' ', 0x39);
        BP_CHECK((int)bp_game.rev.lo == rv, "remote decider: Space does nothing");
    }
    bp_mode = BP_MODE_LOCAL;
    printf("t_panels: %s\n", bp_host_fail ? "FAILED" : "ok");
    return bp_host_fail ? 1 : 0;
}
