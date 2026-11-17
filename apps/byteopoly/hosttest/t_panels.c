/* Byteopoly host harness: the panels (SPEC.md 131.9.7-131.9.11), the
 * SHIPPING bp_panels.c over a fake face and the real engine and session
 * (panels lane). The fake widget kit records what is drawn; a scenario is
 * keys and clicks in, commands and drawn text out. */
#include "bphost.h"
#include "byteoply.c"
#include "art.h"
#define OS88_KEYMAP_HELD(map, s) ((map)[(s) >> 3] & (1 << ((s) & 7)))
static unsigned char bp_t_keys[16];
static int os88_key_map(unsigned char *m) { memcpy(m, bp_t_keys, 16); return 0; }
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
    /* Enter is held while a two-seat hot-seat match starts: it starts under a handoff */
    bp_t_keys[0x1C >> 3] |= 1 << (0x1C & 7);
    t_new(2);
    t_paint();
    BP_CHECK(bp_gp_covers() && t_has("Pass the keyboard"), "handoff covers the centre");
    BP_CHECK(bp_gp_key(' ', 0x39) == 1 && bp_game.phase == BP_PH_MANAGE, "a key under the handoff changes nothing");
    t_tick(10);
    t_key(13, 0x1C);
    t_paint();
    BP_CHECK(t_has("Pass the keyboard"), "a held Enter does not confirm");
    memset(bp_t_keys, 0, 16);
    t_tick(1);                                  /* the timer poll sees the release */
    t_key(13, 0x1C);
    t_paint();
    BP_CHECK(!t_has("Pass the keyboard"), "a fresh Enter confirms");
    BP_CHECK(!bp_gp_covers() && t_has("Roll"), "the idle bar offers Roll");
    /* rolling goes through the session; the revision moves once */
    rev = (int)bp_game.rev.lo;
    t_key(' ', 0x39);
    BP_CHECK((int)bp_game.rev.lo == rev + 1, "Space rolls once");
    printf("t_panels: %s\n", bp_host_fail ? "FAILED" : "ok");
    return bp_host_fail ? 1 : 0;
}
