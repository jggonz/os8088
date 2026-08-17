/* ============================================================================
 * os8088 - tests/covl/covl.c
 *
 * THE CAPABILITY GATE FOR C OVERLAYS (SPEC.md 67.14). tests/chello proved a
 * compiled package can hold a window; this one proves a compiled package can
 * hold a window while half of its code is in a SECOND SEGMENT, read off the
 * floppy on demand.
 *
 * It is under tests/ because it proves a capability rather than being one
 * (CLAUDE.md: everything in apps/ ships, nothing in tests/ does).
 *
 * ---------------------------------------------------------------------------
 * THE FIVE THINGS IT HAS TO MAKE VISIBLE
 * ---------------------------------------------------------------------------
 * Each is a number or a string on the glass, because every one of them is the
 * sort of thing that assembles cleanly and runs wrong, and none of them shows
 * up as a crash:
 *
 *  1. THE MODULE LOADS AND THE FAR CALL LANDS. `Loaded: yes` and a count that
 *     only the module can increment.
 *
 *  2. ARGUMENTS CROSS IN THE RIGHT ORDER AND AT THE RIGHT OFFSETS. An overlay
 *     function is entered by a far call, which pushes FOUR bytes of return
 *     address where a near call pushes two, so every argument it names is two
 *     bytes further from its frame pointer than the compiler thought
 *     (SPEC.md 67.14). ovl_mix(1,2,3,4) answers 1234 and nothing else: get
 *     the fixup wrong and it reads the saved CS as an argument, which is a
 *     large and obviously wrong number rather than a plausible one.
 *
 *  3. THE MODULE CAN CALL BACK OUT, INCLUDING INTO CODE THAT TOUCHES THE UI.
 *     This is the case SPEC.md 65.10 could not prove for the assembly module
 *     and would not ship a feature on top of: the module calls cv_note_set()
 *     here, which is resident, which calls os88_toast(), which is the kernel.
 *     The toast appears in the menu bar and the app is alive afterwards.
 *
 *  4. THE MODULE CAN CALL ITSELF. ovl_mix() calls ovl_inner() twice, which is
 *     a call that stays inside the module - and is still far, because a
 *     function body cannot have two argument layouts (67.14).
 *
 *  5. IT SURVIVES REPETITION. The counters go up by exactly one per press:
 *     a mechanism that leaks a word of stack per call works for a while and
 *     then does not, and a single trip through it proves nothing.
 *
 * ---------------------------------------------------------------------------
 * DRIVING IT
 * ---------------------------------------------------------------------------
 *     make covl
 *     make test TESTAPPS=build/covl.img
 *     ...double-click Disk B, then COVL.O88, then press SPACE.
 *
 * The disk carries COVL.O88 and COVL.OVL. Delete the second one from the
 * image and press SPACE: the refusal path is the other half of the design -
 * a toast naming the missing file, `Loaded: no`, and a call that answers 0
 * without happening (SPEC.md 47).
 * ==========================================================================*/

#include "os88.h"

#define CV_X   120
#define CV_Y   120
#define CV_W   304
#define CV_H   136

/* --- state, all static (os88.h rule 1: &automatic is refused at build time) */

static int  cv_calls;                   /* entries into the module            */
static int  cv_backs;                   /* calls back out of it               */
static int  cv_mix;                     /* what ovl_mix() last answered       */
static int  cv_inner;                   /* ...and ovl_inner(), on its own     */
static char cv_note[40];                /* what the module said through the   */
                                        /* resident function below            */
static char cv_line[48];
static char cv_num[8];

static struct os88_pt   cv_org;
static struct os88_size cv_sz;

/* --- resident helpers ---------------------------------------------------- */

static void cv_append(const char *s)
{
    unsigned n;

    n = os88_strlen(cv_line);
    os88_strcpy(cv_line + n, s, sizeof(cv_line) - n);
}

static void cv_append_num(int v)
{
    os88_utoa((unsigned)v, cv_num);
    cv_append(cv_num);
}

/* cv_note_set - RESIDENT, AND CALLED FROM THE MODULE (point 3 above).
 *
 * It is deliberately not a leaf: it writes this segment's memory, and then it
 * far-calls the kernel. So the path being proved is module -> shim -> resident
 * C -> kernel and all the way back, which is three segment changes deep with
 * the module's own return address still on the stack.
 *
 * in:  tag - a number the module chose, so the string proves WHICH call
 * out: nothing */
static void cv_note_set(int tag)
{
    cv_backs++;
    os88_strcpy(cv_note, "module said ", sizeof(cv_note));
    os88_utoa((unsigned)tag, cv_num);
    os88_strcpy(cv_note + os88_strlen(cv_note), cv_num,
                sizeof(cv_note) - os88_strlen(cv_note));
    os88_toast(cv_note, 0);             /* the UI-touching half of the trip */
}

/* ===========================================================================
 * THE MODULE. Everything from here to the end of this block is compiled into
 * `.modc` and shipped as COVL.OVL, because the names begin with `ovl_` and
 * that is the whole of the marking (SPEC.md 67.14). Nothing else about these
 * functions is special: they name resident statics, resident string literals
 * and resident functions exactly as the code above does, and tools/cc8086.py
 * turns the calls into far calls.
 * ========================================================================= */

/* ovl_inner - two digits, so that ovl_mix()'s answer is readable by eye. */
static int ovl_inner(int a, int b)
{
    return a * 10 + b;
}

/* ovl_mix - four arguments, a call back out, and two calls to itself.
 *
 * The four arguments are the point. SmallerC names them [bp+4], [bp+6],
 * [bp+8] and [bp+10]; entered by a far call they are at +6, +8, +10 and +12,
 * and cc8086.py rewrites every one of them. If it rewrote too few, `a` reads
 * the saved CS - a segment number in the 0x1000s - and the answer is not
 * 1234 but something in the millions with the low digits still right, which
 * is exactly the failure that would survive a "does it run" test. */
static int ovl_mix(int a, int b, int c, int d)
{
    cv_note_set(a + b + c + d);         /* out through a shim */
    return ovl_inner(a, b) * 100 + ovl_inner(c, d);
}

/* ===========================================================================
 * ...and back to the resident program.
 * ========================================================================= */

static void cv_draw(void *win)
{
    int x, y;

    if (os88_wm_geom(win, &cv_sz) != 0)
        return;
    os88_wm_content(win, &cv_org);
    x = cv_org.x + 8;
    y = cv_org.y + 8;

    os88_font_run(x, y, "C overlay gate (SPEC.md 67.14)", OS88_BLACK,
                  OS88_WHITE);
    y += 16;

    cv_line[0] = 0;
    /* From the ANSWER, not from the attempt: cv_calls counts presses, and a
     * refused call increments it just the same. Reporting the attempt made
     * the refusal screen say `Loaded: yes` beside two zeroes. */
    cv_append("Loaded: ");
    cv_append(cv_mix ? "yes" : "no ");
    cv_append("   entries ");
    cv_append_num(cv_calls);
    cv_append("  backs ");
    cv_append_num(cv_backs);
    os88_font_run(x, y, cv_line, OS88_BLACK, OS88_WHITE);
    y += 12;

    cv_line[0] = 0;
    cv_append("ovl_mix(1,2,3,4) = ");
    cv_append_num(cv_mix);
    cv_append("  (1234)");
    os88_font_run(x, y, cv_line, OS88_BLACK, OS88_WHITE);
    y += 12;

    cv_line[0] = 0;
    cv_append("ovl_inner(5,6)   = ");
    cv_append_num(cv_inner);
    cv_append("  (56)");
    os88_font_run(x, y, cv_line, OS88_BLACK, OS88_WHITE);
    y += 12;

    /* Padded to a fixed width, because os88_font_run() letters exactly the
     * cells it is given and a SHORTER string leaves the tail of the longer one
     * it replaced on the glass. Observed here: "module said 10" over "module
     * has not spoken" read "module said 10 spoken", which is a stale-pixel
     * defect and not an overlay one - and the reason to fix it in a gate is
     * that a gate is read as an example. */
    cv_line[0] = 0;
    cv_append(cv_note[0] ? cv_note : "module has not spoken");
    for (x = (int)os88_strlen(cv_line); x < 30; x++)
        cv_line[x] = ' ';
    cv_line[30] = 0;
    os88_font_run(cv_org.x + 8, y, cv_line, OS88_BLACK, OS88_WHITE);
    y += 16;
    x = cv_org.x + 8;
    os88_font_run(x, y, "SPACE calls out, R resets", OS88_BLACK, OS88_WHITE);
}

void os88_paint(void *win)
{
    cv_draw(win);
}

/* W_ONKEY. SPACE is the whole gate: two overlay calls, one of which calls
 * back out and calls itself twice. */
void os88_onkey(int ascii, int scan, void *win)
{
    (void)scan;
    if (ascii == 'r' || ascii == 'R') {
        cv_calls = 0;
        cv_backs = 0;
        cv_mix   = 0;
        cv_inner = 0;
        cv_note[0] = 0;
    } else if (ascii == ' ') {
        cv_calls++;
        cv_mix   = ovl_mix(1, 2, 3, 4);
        cv_inner = ovl_inner(5, 6);
    }
    cv_draw(win);
}

void os88_onclick(int x, int y, void *win)
{
    (void)x;
    (void)y;
    cv_calls++;
    cv_mix   = ovl_mix(1, 2, 3, 4);
    cv_inner = ovl_inner(5, 6);
    cv_draw(win);
}

void *os88_main(void)
{
    void *win;

    win = os88_wm_create(CV_X, CV_Y, CV_W, CV_H, "C Overlay");
    if (win == 0)
        return 0;
    os88_wm_snap(win, 1);
    return win;
}
