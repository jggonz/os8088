/* ============================================================================
 * os8088 - tests/chello/chello.c
 *
 * THE C TOOLCHAIN'S CAPABILITY GATE (SPEC.md 67): the first program written in
 * C that this operating system ever loaded and ran. Everything else about the
 * toolchain - the compiler patch set, tools/cc8086.py, apps/cc/crt0.asm, the
 * cdecl bridge in apps/cc/os88thunk.asm - is inference from emitted assembly
 * until something built out of it appears on the screen. This is that
 * something, and it is under tests/ because it proves a capability rather than
 * being one (CLAUDE.md: everything in apps/ ships, nothing in tests/ does).
 *
 * It is deliberately NOT apps/cc/ccsmoke.c. ccsmoke is the SDK's worked
 * example and is meant to stay small and boring; this one exists to be LOOKED
 * AT and to make every part of the round trip visible in a screendump:
 *
 *   the entry trampoline   the window exists at all, with the title C gave it
 *   os88_paint()           text AND graphics, drawn from the LIVE geometry
 *   os88_onclick()         a counter that counts, and - the part that catches
 *                          a wrong push order - a CROSSHAIR AT THE POINT THAT
 *                          WAS CLICKED. If x and y were swapped by the
 *                          trampoline the number still counts up and the mark
 *                          lands somewhere else; only the mark can tell you.
 *   os88_onkey()           'r' resets, anything else counts, so the two
 *                          arguments of the second three-argument trampoline
 *                          are distinguishable too.
 *
 * Build it (`tests/chello/build.sh` is the chain written out) and boot it:
 *
 *     make test TESTAPPS=build/chello.img
 *     ...then double-click Disk B, then CHELLO.O88.
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS FILE IS CAREFUL ABOUT, AND WHY (apps/cc/os88.h's four rules)
 * ---------------------------------------------------------------------------
 *  1. EVERY ADDRESSED OBJECT IS static. SS is LOW_SEG and DS is the package's
 *     segment, so the address of an automatic is a stack offset that every
 *     later dereference resolves through DS - the program writes to the stack
 *     and reads out of its own image. tools/cc8086.py refuses the bp-relative
 *     `lea` by name (67.5), so this is a build failure and not a bug. The two
 *     out-parameter structs below are `static` inside their function, which is
 *     the idiom; so is every char buffer, because os88_utoa() takes an address.
 *  2. NOTHING TAKES ES. No string instruction is reachable from here: the four
 *     helpers used (strlen/strcpy/utoa and the API's own) are DS-only loops in
 *     apps/cc/os88thunk.asm, and the window record is read through
 *     os88_wm_content()/os88_wm_geom() rather than dereferenced.
 *  3. THE FRAMES ARE TINY. No function here has more than four `int` locals;
 *     the build prints every frame size and the largest in this file is 8
 *     bytes. The UI task has about 700 bytes of headroom (67.8).
 *  4. NO 32-BIT INTEGER AND NO float. Nothing here wants one.
 *
 * ---------------------------------------------------------------------------
 * WHAT IT COSTS ON THE TARGET MACHINE (PERFORMANCE.md)
 * ---------------------------------------------------------------------------
 * A repaint is priced by the number of primitive calls, not by the pixels.
 * This one makes 14 of them - 2 to ask where it is, 3 os88_font_run()s, 5
 * shapes, 1 pen, 3 for the mark (a set_color and two arms) - which is
 * 14 x 756 us = 10.6 ms of call floor, plus the glyph cells: 12 + 24 + 27 = 63
 * cells at ~900 us = 57 ms. Call it 68 ms for the whole window, a bit over one
 * 18.2 Hz tick. A click that MOVES the mark adds three more calls to take the
 * old one off the glass, and only when it moves: an unconditional erase is the
 * double-draw flash that PERFORMANCE.md names as one of the three defects an
 * emulator cannot show you. That is the whole cost model of this OS in one
 * window, and it is exactly the sum that stops being affordable in a real
 * application - which is what SPEC.md 67.11 is about.
 * ==========================================================================*/

#include "os88.h"

/* The frame, authored against the 640x480 reference screen (SPEC.md 39 - it is
 * a reference, not a promise: wm_create clamps it onto the live screen, and
 * every layout below comes from os88_wm_content()/os88_wm_geom() rather than
 * from these numbers, so the window is correct on all three adapters). */
#define CH_X   152
#define CH_Y   128
#define CH_W   264
#define CH_H   128

/* --- the state, all of it static ----------------------------------------- */

static int  ch_clicks;                  /* content clicks since the last reset */
static int  ch_keys;                    /* ...and keys */
static int  ch_mx = -1;                 /* the last click, RELATIVE to the      */
static int  ch_my = -1;                 /* content origin: the window moves,    */
                                        /* so a banked absolute coordinate      */
                                        /* would paint the mark in the wrong    */
                                        /* place after a drag. -1 = never       */
                                        /* clicked, so paint nothing.           */

/* Where the crosshair actually IS on the glass, which is not the same fact as
 * where the last click was: the mark sits in the 4 blank rows BETWEEN two
 * lines of text, and nothing else this window draws covers them. Without this
 * pair the mark left behind by the previous click survives every repaint -
 * observed, and it is the ordinary shape of a stale-pixel bug: the repaint
 * that "works" is the one whose content happens to overlap the old ink. */
static int  ch_px = -1;
static int  ch_py = -1;

static char ch_line[48];                /* STATIC: os88_strcpy() and            */
static char ch_num[8];                  /* os88_utoa() take their addresses     */

/* The live geometry, re-asked every repaint and shared with ch_cross() rather
 * than passed, because a struct passed by value is copied through the address
 * of an automatic and that is refused at build time (apps/cc/os88.h rule 1). */
static struct os88_pt   ch_org;
static struct os88_size ch_sz;

/* --- string building, which is all a C program gets (SPEC.md 67.9: no printf,
 * because the family is thousands of bytes for the one conversion anybody
 * wanted) ------------------------------------------------------------------ */

/* ch_append - concatenate onto ch_line, never overrunning it. os88_strcpy()
 * always terminates and its cap counts the NUL. */
static void ch_append(const char *s)
{
    unsigned n;

    n = os88_strlen(ch_line);
    os88_strcpy(ch_line + n, s, sizeof(ch_line) - n);
}

static void ch_append_num(int v)
{
    os88_utoa((unsigned)v, ch_num);
    ch_append(ch_num);
}

/* --- the painting -------------------------------------------------------- */

/* ch_cross - the click mark, at (rx, ry) RELATIVE to the content origin, in
 * `colour`. Two calls: 1.5 ms on the target machine.
 *
 * Drawing it and erasing it are the same routine deliberately. The arms are 9
 * pixels long and the API's rectangles are inclusive, so the mark is the one
 * thing here that can run out of the window - and if the range test differed
 * between the two, an erase would be skipped exactly where the draw was not,
 * which is a stale mark that appears only near an edge.
 *
 * in:  rx, ry - content coordinates, or rx < 0 for "there is no mark"
 *      colour - OS88_BLACK to draw, OS88_WHITE to erase (the content
 *               background is the kernel's white fill: this window does not
 *               ask for wm_ownbg)
 * out: nothing; ch_org and ch_sz must be current */
static void ch_cross(int rx, int ry, int colour)
{
    if (rx < 0)
        return;
    if (rx - 4 < 0 || rx + 4 > ch_sz.w - 1 ||
        ry - 4 < 0 || ry + 4 > ch_sz.h - 1)
        return;
    os88_set_color(colour);
    os88_gfx_hline(ch_org.x + rx - 4, ch_org.x + rx + 4, ch_org.y + ry);
    os88_gfx_vline(ch_org.x + rx, ch_org.y + ry - 4, ch_org.y + ry + 4);
}

/* ch_draw - the whole content, from the live geometry. Called by os88_paint()
 * and by the two handlers that change what it says, because THE KERNEL DOES
 * NOT REPAINT AFTER A CLICK OR A KEY (SPEC.md 11) - the redraw is the app's.
 *
 * in:  win - the window record; the gfx lock is held in every caller
 * out: nothing */
static void ch_draw(void *win)
{
    int x, y, right;

    /* ch_org and ch_sz are STATIC because `&automatic` is refused at build
     * time (67.5). Verified rather than assumed: making just this one an
     * ordinary local and rebuilding gives
     *
     *   error: `lea ax, [bp-10]` takes the address of an automatic. [...]
     *   cc8086: 1 problem(s); build/chello.gen.asm not written
     *
     * which is the whole design in one message - it names the instruction, the
     * reason and the fix, and it writes no output for nasm to succeed on. */
    if (os88_wm_geom(win, &ch_sz) != 0)      /* not visible: nothing to draw */
        return;
    os88_wm_content(win, &ch_org);

    /* Take the old mark off the glass BEFORE anything else, and only if it is
     * about to move: the rest of this routine repaints its own rows, so the
     * mark is the only ink that would otherwise survive. Erasing one that is
     * not moving would be the double-draw flash PERFORMANCE.md names as one of
     * the three defects an emulator cannot show. */
    if (ch_px >= 0 && (ch_px != ch_mx || ch_py != ch_my))
        ch_cross(ch_px, ch_py, OS88_WHITE);

    x     = ch_org.x + 8;
    right = ch_org.x + ch_sz.w - 9;          /* inclusive, like the API */

    /* Line 1 - the headline. os88_font_run() lays the cells' background and
     * their glyphs in one pass (SPEC.md 6.1), so the line is never momentarily
     * blank; the erase-then-letter pair is the canonical double-draw and it is
     * invisible in an emulator (PERFORMANCE.md). */
    y = ch_org.y + 6;
    os88_font_run(x, y, "HELLO FROM C", OS88_BLACK, OS88_WHITE);

    /* The graphics band: a 50% dither, a frame around it, and a diagonal
     * through it. Three shapes rather than one because the point is to prove
     * three different thunks pushed four arguments each in the right order -
     * a swapped x2/y1 gives an empty or inverted rect, which is visible. */
    y += 12;
    os88_gfx_fill_gray(x, y, right, y + 21);
    os88_set_color(OS88_BLACK);
    os88_gfx_frame(x, y, right, y + 21);
    os88_gfx_line(x + 2, y + 19, right - 2, y + 2, 0);

    /* A solid black tab at the left end of the band, so a 1bpp adapter (where
     * the dither is the only grey there is) still shows something filled. */
    os88_gfx_fill(x + 4, y + 5, x + 15, y + 16);

    /* Line 2 - the counters. */
    y += 26;
    os88_strcpy(ch_line, "clicks ", sizeof(ch_line));
    ch_append_num(ch_clicks);
    ch_append("  keys ");
    ch_append_num(ch_keys);
    ch_append("      ");                     /* trailing blanks: the run paints
                                              * its own background, so this is
                                              * how a shortening number erases
                                              * the digit it used to have */
    os88_font_run(x, y, ch_line, OS88_BLACK, OS88_WHITE);

    /* Line 3 - where the last click landed, in CONTENT coordinates. This is
     * the line that catches a trampoline that pushed y before x: the count
     * still counts and the crosshair still appears, but the pair reads back
     * transposed, and a click near the left edge of a wide window reports an
     * x that could not fit in the height. */
    y += 12;
    if (ch_mx < 0) {
        os88_strcpy(ch_line, "click me, or press a key   ", sizeof(ch_line));
    } else {
        os88_strcpy(ch_line, "last click at ", sizeof(ch_line));
        ch_append_num(ch_mx);
        ch_append(",");
        ch_append_num(ch_my);
        ch_append("        ");
    }
    os88_font_run(x, y, ch_line, OS88_BLACK, OS88_WHITE);

    /* The mark, last so that it sits over anything it crosses, and banked as
     * "what is on the glass" so the next repaint can take it off again. */
    ch_cross(ch_mx, ch_my, OS88_BLACK);
    ch_px = ch_mx;
    ch_py = ch_my;
}

/* --- the callbacks the shim declared ------------------------------------- */

/* W_PAINT. The gfx lock is ALREADY HELD (SPEC.md 11): draw, never take it. */
void os88_paint(void *win)
{
    ch_draw(win);
}

/* W_ONCLICK. x and y are ABSOLUTE screen pixels, on the UI task, under the
 * lock. The kernel does not repaint after a click, so this one does. */
void os88_onclick(int x, int y, void *win)
{
    os88_wm_content(win, &ch_org);           /* &static: an out-parameter has
                                              * to point at one (67.5) */
    ch_mx = x - ch_org.x;
    ch_my = y - ch_org.y;
    ch_clicks++;
    ch_draw(win);
}

/* W_ONKEY. AL was the ASCII code and AH the scan code; the trampoline widened
 * each into its own int. 'r' resets, which is how the two are told apart. */
void os88_onkey(int ascii, int scan, void *win)
{
    (void)scan;
    if (ascii == 'r' || ascii == 'R') {
        ch_clicks = 0;
        ch_keys   = 0;
        ch_mx     = -1;
        ch_my     = -1;
    } else {
        ch_keys++;
    }
    ch_draw(win);
}

/* --- the entry point (SPEC.md 20.2, 21 step 8) ---------------------------
 * The UI task, the gfx lock NOT held, no window yet and the instance not
 * published. Create the window and return it; 0 aborts the launch and the
 * runtime turns that into the CF the loader is owed. Do not draw here - the
 * loader shows the window, which paints it. */
void *os88_main(void)
{
    void *win;

    win = os88_wm_create(CH_X, CH_Y, CH_W, CH_H, "C Hello");
    if (win == 0)
        return 0;

    /* Content origin on a multiple of 8, so every os88_font_run() above takes
     * the single-store path on the two 1bpp adapters (SPEC.md 6.1, 11.94). A
     * no-op on VGA, so it is asked for unconditionally rather than after a
     * question about the adapter. */
    os88_wm_snap(win, 1);
    return win;
}
