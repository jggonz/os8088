/* Byteopoly host harness: the panels (SPEC.md 131.9.7-131.9.11), the
 * SHIPPING bp_panels.c over a fake face and the real engine and session
 * (panels lane). The fake widget kit records what is drawn; a scenario is
 * keys and clicks in, commands and drawn text out. */
#include "bphost.h"
#include "byteoply.c"
#include "art.h"
int bx_artstr(int id, int n, char *buf) { (void)id; (void)n; buf[0] = 0; return 0; }
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
#include "bp_panels.c"
/* ---- helpers ---- */
static void t_paint(void) { bp_t_ink[0] = 0; bp_t_buttons = 0; bp_gp_draw(); }
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
static void t_key(int a, int sc) { bp_gp_key(a, sc); }
static void t_tick(int n) { while (n--) { bp_host_ticks++; bp_gp_tick(); } }
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
    t_key(13, 0x1C);                            /* too soon: a held key's first repeat */
    t_paint();
    BP_CHECK(t_has("Pass the keyboard"), "an early Enter does not confirm");
    t_tick(12);
    t_key(13, 0x1C);                            /* quiet for 12 ticks, 22 in: fresh */
    t_paint();
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
                BP_CHECK(bp_gp_covers() && t_has("Price:"), "buy panel shows the price");
                if (!bought) {
                    t_key('b', 0x30);
                    BP_CHECK(bp_game.phase != BP_PH_BUY, "B buys");
                    bought = 1;
                } else {
                    t_key('a', 0x1E);
                    BP_CHECK(bp_game.phase == BP_PH_AUCTION, "A opens the auction");
                    t_paint();
                    BP_CHECK(t_has("Auction:") && t_has("[B] Bid 10"), "auction shows the opening bid");
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
