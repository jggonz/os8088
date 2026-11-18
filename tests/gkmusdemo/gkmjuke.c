/* gkmjuke - the game kit's music player as a jukebox (SPEC.md 132.8). A
 * gate package, not shipped: it plays every song and every effect of
 * Byteopoly's banks (apps/byteopoly/art/music.txt and sfx.txt, packed into
 * this package's one art part by tools/os88gkmus.py part), so the player can
 * be heard before it is wired into a game. Up/Down choose, Enter plays (a
 * song loops; O plays it once), S stops, M and X turn music and effects off
 * and on, 0..9 play effects 0..9 over whatever plays, A runs every song for
 * 12 seconds and then every effect. */
#include "os88.h"
#include "gk.h"

void bx_setup(unsigned seg, void *band, unsigned cap, unsigned artseg, int bpp);

#define JK_SONGS 4
#define JK_FX 20
static const char *jk_names[JK_SONGS + JK_FX] = {
    "Song: title", "Song: game", "Song: auction", "Song: fanfare",
    "dice", "buy", "rent", "floppy", "crash", "bankrupt", "victory", "error",
    "step", "salary", "syserr", "upgrade", "mortgage", "bid", "sold",
    "tradeok", "tradeno", "linklost", "linkok", "yourturn"};
static void *jk_win;
static struct os88_pt jk_org;
static unsigned char jk_band[16];
static int jk_sel, jk_route, jk_music = 1, jk_fx = 1, jk_auto = -1;
static unsigned jk_due;
static char jk_line[48];

static void jk_paint_rows(void)
{
    int i, y;
    static const char *rt[3] = {"route: none", "route: FM, three voices", "route: speaker, melody"};
    os88_strcpy(jk_line, rt[jk_route < 0 || jk_route > 2 ? 0 : jk_route], 40);
    os88_font_run(jk_org.x + 8, jk_org.y + 4, jk_line, 0, 15);
    os88_strcpy(jk_line, jk_music ? "M music on   " : "M music off  ", 20);
    os88_strcpy(jk_line + 13, jk_fx ? "X effects on " : "X effects off", 20);
    os88_font_run(jk_org.x + 8, jk_org.y + 14, jk_line, 0, 15);
    for (i = 0; i < JK_SONGS + JK_FX; i++) {
        y = jk_org.y + 28 + (i % 12) * 10;
        jk_line[0] = (char)(i == jk_sel ? '>' : ' ');
        jk_line[1] = (char)(i < JK_SONGS && gkm_playing() == i ? '*' : ' ');
        os88_strcpy(jk_line + 2, jk_names[i], 20);
        while (os88_strlen(jk_line) < 16) os88_strcpy(jk_line + os88_strlen(jk_line), " ", 2);
        os88_font_run(jk_org.x + 8 + (i / 12) * 140, y, jk_line, 0, 15);
    }
}
void os88_paint(void *win)
{
    os88_wm_content(win, &jk_org);
    jk_paint_rows();
}
static void jk_redraw(void *win)
{
    if (os88_wm_clip_set(win) == 0) os88_paint(win);
}
static void jk_go(int i, int loop)
{
    if (i < JK_SONGS) gkm_play(i, loop);
    else gkm_sfx(i - JK_SONGS);
}
void os88_onkey(int ascii, int scan, void *win)
{
    if (scan == 0x48 && jk_sel > 0) jk_sel--;
    else if (scan == 0x50 && jk_sel < JK_SONGS + JK_FX - 1) jk_sel++;
    else if (ascii == 13 || ascii == ' ') jk_go(jk_sel, 1);
    else if (ascii == 'o' || ascii == 'O') jk_go(jk_sel, 0);
    else if (ascii == 's' || ascii == 'S') { gkm_stop(); jk_auto = -1; }
    else if (ascii == 'm' || ascii == 'M') { jk_music ^= 1; gkm_volume(jk_music, jk_fx); }
    else if (ascii == 'x' || ascii == 'X') { jk_fx ^= 1; gkm_volume(jk_music, jk_fx); }
    else if (ascii == 'a' || ascii == 'A') { jk_auto = 0; jk_due = os88_ticks(); }
    else if (ascii >= '0' && ascii <= '9') gkm_sfx(ascii - '0');
    else return;
    jk_redraw(win);
}
/* the automatic tour: each song 12 s (the fanfare once), each effect 1.5 s */
static void jk_tour(void *win)
{
    if (jk_auto < 0 || (int)(os88_ticks() - jk_due) < 0) return;
    if (jk_auto >= JK_SONGS + JK_FX) { gkm_stop(); jk_auto = -1; jk_redraw(win); return; }
    jk_sel = jk_auto;
    if (jk_auto == JK_SONGS) gkm_stop();
    jk_go(jk_auto, jk_auto != 3);
    jk_due = os88_ticks() + (jk_auto < JK_SONGS ? 218 : 27);
    jk_auto++;
    jk_redraw(win);
}
void os88_ontimer(void *win)
{
    gkm_tick();
    jk_tour(win);
    os88_wm_timer(win, 1);
}
int os88_onclose(void *win)
{
    (void)win;
    gkm_done();
    return 1;
}
static struct os88_class jk_cls = {OS88_CLASS_LEN, 0,
    os88_t_paint, os88_t_onkey, 0, 0, 0, os88_t_onclose, 0, 0, 0, 0, os88_t_ontimer, 0, 0};
void *os88_main(void)
{
    bx_setup(0, jk_band, sizeof(jk_band), os88_part_seg(0), 1);
    jk_route = gkm_init(0, 1);
    jk_win = os88_wm_create(80, 40, 320, 172, "Game kit jukebox", &jk_cls);
    if (jk_win) os88_wm_timer(jk_win, 1);
    return jk_win;
}
