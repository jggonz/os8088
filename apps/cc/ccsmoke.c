/* ============================================================================
 * os8088 - apps/cc/ccsmoke.c
 *
 * The C toolchain's smoke test, and the worked example of every part of the
 * SDK a package actually uses (SPEC.md 73). It is deliberately small and
 * deliberately boring: one window, a counter, a menu, an About, a key handler
 * and a click handler - the shortest program that proves the entry
 * trampoline, all four callback trampolines, the cdecl bridge, the window
 * accessors that read the record through ES, and the runtime's own helpers
 * all work at once.
 *
 * It is NOT on any shipped floppy. `make cc-smoke` builds it to
 * build/ccsmoke.img and `make test TESTAPPS=build/ccsmoke.img` puts it in B:
 * (apps/cc/Makefile.inc has both rules). When something in apps/cc/ changes,
 * this is the thing to rebuild and click on: nothing in the C half of the
 * toolchain has a unit test, and a trampoline that pushes its arguments in
 * the wrong order still assembles.
 *
 * Read apps/cc/os88.h before this file. The four rules it opens with are the
 * whole difference between C here and C anywhere else, and this program is
 * written to obey them visibly:
 *
 *   - every buffer is `static`, because the address of an automatic is
 *     silently wrong here and tools/cc8086.py refuses it (73.5);
 *   - nothing is copied with a string instruction, because they address ES
 *     and ES is the kernel's (73.5.1);
 *   - the window record is read through os88_win_*(), never dereferenced;
 *   - there is no `long`, no `float` and no printf.
 * ==========================================================================*/

#include "os88.h"

/* The frame, authored against the 640x480 reference screen (SPEC.md 39: it is
 * a reference, not a promise - wm_create clamps it onto the live one). */
#define SM_X   180
#define SM_Y   140
#define SM_W   248
#define SM_H   104

static int sm_clicks;                    /* how many times the content was hit */
static int sm_keys;                      /* ...and how many keys arrived */
static char sm_line[40];                 /* STATIC: os88_utoa() takes its
                                          * address, and the address of an
                                          * automatic is the one thing this
                                          * toolchain refuses to build (73.5) */
static char sm_num[8];

/* The menu set. Not `const`: os88_menu_set() writes its oncmd field, because
 * a C program cannot name the assembly trampoline the kernel has to call
 * (SPEC.md 12.2, and see struct os88_menuset in os88.h). */
static const char *sm_items[] = { "Reset", "Say hello" };
static struct os88_menuset sm_menus = {
    "Smoke", 0, 1,
    { { "File", sm_items, 2 } }
};

#define SM_CMD_RESET 0
#define SM_CMD_TOAST 1

/* --- one line of text, laid out from the LIVE geometry ------------------- */

/* sm_draw - repaint the content. Called from os88_paint() and from the two
 * handlers that change what it says.
 *
 * Three calls per line and two lines: six primitives at ~756 us each on the
 * target machine, which is the honest price of this window and the number to
 * think in (PERFORMANCE.md). os88_font_run() draws the cells' background AND
 * their glyphs in one pass (SPEC.md 6.1), so the line is never momentarily
 * blank - the erase-then-letter pair is the canonical double-draw and it is
 * invisible in an emulator. */
static void sm_draw(void *win)
{
    /* STATIC, and this is the rule that catches everyone: an out-parameter is
     * an address, so a local `struct os88_pt org;` passed as `&org` is
     * `lea ax, [bp-N]` and the build stops. It was written that way first and
     * tools/cc8086.py refused both of them by name (73.5). Statics are also
     * what keeps the frame small enough for a 256-byte worker stack (73.8) -
     * the two rules are one rule seen twice. */
    static struct os88_pt org;
    static struct os88_size sz;
    int y;

    if (os88_wm_geom(win, &sz) != 0)     /* not visible: nothing to draw */
        return;
    os88_wm_content(win, &org);

    y = org.y + 8;

    os88_font_run(org.x + 8, y, "C on os8088", OS88_BLACK, OS88_WHITE);

    y += 16;
    os88_strcpy(sm_line, "clicks ", sizeof(sm_line));
    os88_utoa(sm_clicks, sm_num);
    os88_strcpy(sm_line + os88_strlen(sm_line), sm_num,
                sizeof(sm_line) - os88_strlen(sm_line));
    os88_strcpy(sm_line + os88_strlen(sm_line), "   keys ",
                sizeof(sm_line) - os88_strlen(sm_line));
    os88_utoa(sm_keys, sm_num);
    os88_strcpy(sm_line + os88_strlen(sm_line), sm_num,
                sizeof(sm_line) - os88_strlen(sm_line));
    os88_strcpy(sm_line + os88_strlen(sm_line), "      ",
                sizeof(sm_line) - os88_strlen(sm_line));
    os88_font_run(org.x + 8, y, sm_line, OS88_BLACK, OS88_WHITE);

    /* A rule across the content, sized from the live width rather than from
     * the template: a window is clamped onto the screen it got. */
    y += 16;
    os88_set_color(OS88_BLACK);
    os88_gfx_hline(org.x + 8, org.x + sz.w - 9, y);

    y += 8;
    os88_font_run(org.x + 8, y, "click me, or press a key",
                  OS88_BLACK, OS88_WHITE);
}

/* --- the callbacks the shim declared ------------------------------------- */

void os88_paint(void *win)               /* W_PAINT: the lock is already held */
{
    sm_draw(win);
}

void os88_onclick(int x, int y, void *win)
{
    (void)x;
    (void)y;
    sm_clicks++;
    sm_draw(win);                        /* the kernel does not repaint for a
                                          * click any more than for a command */
}

void os88_onkey(int ascii, int scan, void *win)
{
    (void)scan;
    if (ascii == 'r' || ascii == 'R') {
        sm_clicks = 0;
        sm_keys = 0;
    } else {
        sm_keys++;
    }
    sm_draw(win);
}

void os88_oncmd(int item, int menu, void *win)
{
    (void)menu;
    if (item == SM_CMD_RESET) {
        sm_clicks = 0;
        sm_keys = 0;
        sm_draw(win);
    } else if (item == SM_CMD_TOAST) {
        /* A toast costs this window no pixels and takes itself down
         * (SPEC.md 59), which is why it is the right way to say something
         * that is not part of the document. */
        os88_toast("hello from C", 0);
    }
}

void os88_about(void *win)
{
    os88_toast("CCSMOKE - the C toolchain's smoke test", 0);
    sm_draw(win);
}

/* --- the entry point (SPEC.md 20.2) --------------------------------------
 * The gfx lock is NOT held here and the window does not exist yet: create it,
 * attach the menu set and the About handler, and return it. The loader shows
 * it. Returning 0 aborts the launch, which is what a full window table looks
 * like from here. */
void *os88_main(void)
{
    void *win;

    win = os88_wm_create(SM_X, SM_Y, SM_W, SM_H, "C Smoke");
    if (win == 0)
        return 0;

    os88_menu_set(win, &sm_menus);       /* &static: fine. &automatic: refused */
    os88_about_set(win);

    /* Content origin on a multiple of 8, so every os88_font_run() above takes
     * the single-store path on the two 1bpp adapters (SPEC.md 6.1, 11.94). A
     * no-op on VGA, so it is set unconditionally rather than after asking. */
    os88_wm_snap(win, 1);
    return win;
}
