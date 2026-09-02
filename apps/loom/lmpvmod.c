/* ============================================================================
 * os8088 - apps/loom/lmpvmod.c
 *
 * LOOM.WPV, THE PREVIEW MODULE (WEAVE-SPEC 1.2.4). A second, RESIDENT segment
 * beside LOOM.O88, read once when Preview is first opened, holding WEAVE's
 * flow walk and WEAVE's component painter and nothing else.
 *
 * IT IS NOT A PACKAGE. There is no window, no menu, no callback and no
 * os88_main(): apps/loom/lmpvmod.asm gives it an eight-byte header and one
 * far-called entry, exactly as apps/weave/wcanvas.asm does for WEAVE.WSM.
 * apps/weave/wpvabi.inc is the contract and this file is written from it.
 *
 * ---------------------------------------------------------------------------
 * THE ONE RULE THIS FILE EXISTS TO KEEP (WEAVE-SPEC 1.2)
 * ---------------------------------------------------------------------------
 * "never a second copy: two layouts that must agree cell-for-cell is the
 * failure WEAVE-SPEC 11's byte-identity rule exists to prevent, said about
 * code instead of about bundles."
 *
 * So the two bodies below are #included, not rewritten:
 *
 *     apps/weave/wflow.c     WEAVE-SPEC 7's walk, verbatim
 *     apps/weave/wpaint.c    WEAVE-SPEC 6's painter, verbatim
 *
 * and the drawing they call is apps/weave/wdraw.inc, the same assembly
 * WEAVE.O88 %includes (apps/loom/lmpvmod.asm). One source, compiled into two
 * images, which is what SPEC.md 20.5.1 says code sharing IS on this platform.
 * The pictures cannot drift because there is only one description of them.
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS FILE ADDS, AND IT IS ALL SEAM
 * ---------------------------------------------------------------------------
 * The two included files name state and functions that live elsewhere in
 * WEAVE - in weave.c, wval.c, wact.c, wgrid.c, wcanv.c, wevent.c and
 * wvm.inc. Everything below is either
 *
 *   * that STATE, declared here exactly as weave.c declares it, or
 *   * a SEAM: the smallest honest thing the painter can call in a program
 *     where no bytecode runs, no field pool exists, no cell store has been
 *     built and no canvas module is bound.
 *
 * Each seam says which of those four facts makes it what it is. None of them
 * is a second painter: every one either reaches a shared core in wdraw.inc
 * with the arguments WEAVE would have given it, or does nothing because the
 * thing it would have done cannot exist here.
 *
 * ---------------------------------------------------------------------------
 * THE SEGMENT RULES, WHICH ARE THE WHOLE TAX ON LIVING OUT HERE
 * ---------------------------------------------------------------------------
 *  * DS = CS = this module's claim while a verb runs; lmpvmod.asm banks the
 *    caller's DS and puts it back. So every static below is a plain
 *    DS-relative name and the C is ordinary C.
 *  * SS is LOOM's task stack and never ours - CLAUDE.md's hard rule - which
 *    is why tools/cc8086.py's refusal of `&local` binds this compilation
 *    exactly as it binds a package's.
 *  * OSAPI_* are KERNEL_SEG:offset far immediates (apps/os88api.inc), so the
 *    module calls the kernel directly and needs no vector back into LOOM.
 *  * The bundle image is in LOOM's OUTPUT CLAIM, a third segment, reached
 *    only through wblob.inc's explicit (segment, offset) accessors - never
 *    through a C pointer (C64-SPEC 3.6's rule).
 * ==========================================================================*/

#include "os88.h"
#include "../weave/weave.h"
#include "../weave/wpvabi.h"   /* the module contract's C side (1.2.4) */

/* THE PREVIEW SWITCH. It reaches exactly one place in shared source -
 * w_grid() in apps/weave/wflow.c, three lines - and it is there because the
 * box a Preview lays out in is a PANE inside a window and not a window's
 * content area (WEAVE-SPEC 1.7). Everything else in both included files
 * compiles here the way it compiles in WEAVE. */
#define W_PREVIEW 1

/* ============================================================================
 * THE STATE THE TWO INCLUDED FILES NAME
 *
 * Declared here exactly as apps/weave/weave.c declares it, in the same order
 * and with the same comments' meaning, because a difference between the two
 * declarations is a difference in the picture and nothing would report it.
 * ==========================================================================*/

static void *w_win;                     /* the window the pane is inside */
static int   w_state;                   /* W_ST_* - W_ST_RUN while a card is
                                         * being drawn, because w_layout()
                                         * tests it before it re-walks */

/* The bundle, as it stands in LOOM's staged output claim (WEAVE-SPEC 2). */
static unsigned w_seg;                  /* the claim, 0 = none */
static unsigned w_size;                 /* the header's own size word */
static unsigned w_flags, w_nsec, w_entry;
static unsigned w_soff[W_NSECTYPE], w_slen[W_NSECTYPE], w_sextra[W_NSECTYPE];
static unsigned char w_shave[W_NSECTYPE];
static unsigned w_natoms, w_ncomp, w_ncard;

/* The cell grid (WEAVE-SPEC 7.1) and the walk's output (7.2). */
static struct os88_pt   w_org;
static struct os88_size w_sz;
static int w_ox, w_oy;
static int w_cw, w_ch;

#define W_MAXROW W_MAXLAY
struct w_lay {
    unsigned props;
    unsigned char cx, cy;
    unsigned char cw, ch;
    unsigned char id;
    unsigned char ctype;
    unsigned char style;
    unsigned char cflags;
};
static struct w_lay w_lay[W_MAXLAY];
static int w_nlay, w_nrow;
static int w_lay_cw, w_lay_ch, w_lay_card;

/* ============================================================================
 * THE FORWARD DECLARATIONS
 *
 * The same four apps/weave/weave.c writes, for the same reason: a C package
 * (and a C module) is ONE compilation (SPEC.md 73.1), and wpaint.c calls
 * these before this file defines them.
 * ==========================================================================*/
unsigned lpv_cds(void);                 /* lmpvmod.asm: the CALLER's DS, banked
                                         * by the far entry - WPVV_PAINT's
                                         * parameter block lives in it */

static void w_infield(int i, int x1, int y1, int x2, int y2, int dis);
static void w_press(int i, int x, int y);
static void w_enq(int comp, int atom, int d1, int d2);
static void w_gpaint(int i);
static void w_cpaint(int i);

/* ============================================================================
 * THE SHARED BODIES
 * ==========================================================================*/

#include "../weave/watom.c"             /* 2.7's atom accessors, extracted in
                                         * wave 7 for exactly this include */
#include "../weave/wflow.c"             /* WEAVE-SPEC 7's flow walk */
#include "../weave/wpaint.c"            /* ...and WEAVE-SPEC 6's painter */

/* ============================================================================
 * THE SEAMS
 *
 * Four functions and a fifth in wvm's place. Every one of them is what the
 * painter's call MEANS in a program with no VM, no field pool, no cell store
 * and no canvas module - and every one says so, because the alternative is
 * that somebody later reads a three-line body as a shortcut and "fixes" it
 * into a second painter.
 * ==========================================================================*/

/* w_infield - one <input>, and this IS WEAVE's own path.
 *
 * apps/weave/wact.c's w_infield() takes the field pool's block for this
 * component and hands wd_ldraw() a live os88line field; when there is no
 * block - `k < 0`, which is every component in a program that never built a
 * pool, and which WEAVE itself takes for a DISABLED field - it resolves the
 * text and calls wd_input() with it. That second branch is the whole of
 * Preview's input: the same core, the same arguments, the same picture as a
 * field nobody has clicked in. There is no pool here to have a block in, so
 * the branch is not chosen at run time, it is the only one there is. */
static void w_infield(int i, int x1, int y1, int x2, int y2, int dis)
{
    (void)dis;
    w_cstr(w_lay[i].id, w_lay[i].props, WA_TEXT);
    wd_input(x1, y1, x2, y2, w_str);
}

/* w_gpaint / w_cpaint - <grid> and <canvas>, drawn as their RECT.
 *
 * WEAVE-SPEC 1.7.1 says what these are and why, and the short of it is that
 * neither component's body is a layout fact. A grid's picture is the band
 * composer over a CELL STORE - a claim of its own, built by the grid load
 * path and filled by a recalculation (WEAVE-SPEC 5.6, 5.5) - and a canvas's
 * is the sprite compositor inside WEAVE.WSM, on a worker task, over a canvas
 * claim (6.10). Standing either of those up in here is a second claim, a
 * second module and a recalculation Preview has no reason to run; what
 * Preview owes the author is WHERE the component is and HOW BIG, which is the
 * frame.
 *
 * It is wd_box(), the SAME core WC_BOX draws with, at the rect the shared
 * walk computed - so this is not a picture of its own either. The oracle
 * draws exactly this: `weavesim --render --preview` frames a grid and a
 * canvas and draws nothing inside them, and tests/weaveprev.py diffs the two.
 *
 * The y2 clamp is w_paint_comp's own, for WC_BOX, and it is here for the same
 * reason it is there: a rect that runs past the content box would draw
 * through the window border in ABSOLUTE screen coordinates (7.4). */
static void w_gframe(int i)
{
    int y2;

    w_rectof(i);
    y2 = w_rect[3];
    if (y2 > w_ybot)
        y2 = w_ybot;
    wd_box(w_rect[0], w_rect[1], w_rect[2], y2);
}

static void w_gpaint(int i)
{
    w_gframe(i);
}

static void w_cpaint(int i)
{
    w_gframe(i);
}

/* w_press / w_enq - the gesture and the ring, and Preview has neither.
 *
 * WEAVE-SPEC 1.7.1 records the scope: wave 7 ships Preview's PICTURE. Arming
 * a control in the pane needs apps/weave/wact.c's press/release pair and the
 * field pool underneath it, and firing it needs the event ring - which is the
 * VM's, and the VM is what a Preview is defined as not running. Both are
 * reachable only from a click the module is never given, because LOOM's
 * WPVV_PAINT is the only verb: they exist because wpaint.c names them and a
 * compilation has to resolve every name it uses.
 *
 * They are NOT stubs that quietly do half a job. w_onhit() is unreachable
 * here, and if a later wave gives this module a click verb it must bring
 * wact.c with it rather than growing these. */
static void w_press(int i, int x, int y)
{
    (void)i;
    (void)x;
    (void)y;
}

static void w_enq(int comp, int atom, int d1, int d2)
{
    (void)comp;
    (void)atom;
    (void)d1;
    (void)d2;
}

/* wvm_str_read - the VM's string arena, which does not exist here.
 *
 * w_cstr() reads it when w_ctext[id] is non-zero, and w_ctext[] is only ever
 * written by a SETP - a bytecode op. No bytecode runs in a Preview
 * (WEAVE-SPEC 1.7), w_pstate() clears the array before every walk, and so
 * every component still shows the atom its UISTREAM record named, which is
 * what an author has just typed. This answers "an empty string" for the case
 * that cannot arise, rather than 0, because w_cstr()'s caller letters what it
 * is handed. It is a NAME the compilation has to resolve and not a policy. */
int wvm_str_read(int handle, char *dst, unsigned cap)
{
    (void)handle;
    if (cap)
        dst[0] = 0;
    return 0;
}

/* ============================================================================
 * THE SECTION TABLE (WEAVE-SPEC 2.2, 2.3)
 *
 * NOT THE VALIDATOR. WEAVE's ovl_val_* pass exists because a `.WAB` on a disk
 * need never have been through a packer (10.4) and every byte off a disk is
 * hostile (SPEC.md 19). This image never went near a disk: LOOM's own packer
 * wrote it into LOOM's output claim a few milliseconds ago and LOOM's packer
 * re-read it through its own independent reader before Preview was offered
 * the claim at all (11.3's self-check).
 *
 * What is still checked is everything an offset could make this module index
 * out of its own claim with - the magic, the version, the count, and every
 * `offset + length <= total size` 2.3 requires of a reader. That is not
 * belt-and-braces: a Preview drawn out of a wrong offset would be a picture
 * of some other bytes, and the person looking at it is trying to find out
 * what their program looks like.
 * ==========================================================================*/
static int lpv_sections(void)
{
    unsigned i, off, t, o, l;

    for (i = 0; i < W_NSECTYPE; i++) {
        w_soff[i] = 0;
        w_slen[i] = 0;
        w_sextra[i] = 0;
        w_shave[i] = 0;
    }
    if (w_b(w_seg, 0) != 'W' || w_b(w_seg, 1) != 'A'
            || w_b(w_seg, 2) != 'B' || w_b(w_seg, 3) != 0x1A)
        return WPVE_MAGIC;
    if (w_w(w_seg, 4) != 1)
        return WPVE_MAGIC;
    w_size = w_w(w_seg, 6);
    w_flags = w_w(w_seg, 8);
    w_nsec = w_b(w_seg, 12);
    w_entry = w_b(w_seg, 13);
    if (w_size < 32 || w_nsec < 5 || w_nsec > 9)
        return WPVE_SECT;
    if (32 + 8 * w_nsec > w_size)
        return WPVE_SECT;
    for (i = 0; i < w_nsec; i++) {
        off = 32 + 8 * i;
        t = w_b(w_seg, off);
        o = w_w(w_seg, off + 2);
        l = w_w(w_seg, off + 4);
        if (t < 1 || t >= W_NSECTYPE)
            return WPVE_SECT;
        if (o > w_size || l > w_size || o + l > w_size)
            return WPVE_SECT;
        w_soff[t] = o;
        w_slen[t] = l;
        w_sextra[t] = w_w(w_seg, off + 6);
        w_shave[t] = 1;
    }
    if (!w_shave[W_UISTREAM] || !w_shave[W_PROPS] || !w_shave[W_ATOMS])
        return WPVE_SECT;
    if (w_slen[W_UISTREAM] != W_REC_SIZE * w_sextra[W_UISTREAM])
        return WPVE_SECT;
    if (w_slen[W_ATOMS] < 2)
        return WPVE_SECT;
    w_natoms = w_w(w_seg, w_soff[W_ATOMS]);       /* 2.7: the count is first */
    if (w_natoms > (WA_APP_LAST - WA_APP_FIRST + 1))
        return WPVE_SECT;
    if (2 + 2 * w_natoms > w_slen[W_ATOMS])
        return WPVE_SECT;
    return 0;
}

/* lpv_count - the cards and the components, off the UISTREAM the walk is
 * about to read. The walk needs the CARD to exist before it is asked for one;
 * everything else here is what LOOM shows beside the picture. */
static void lpv_count(void)
{
    unsigned s, n, i, rec;

    w_ncomp = 0;
    w_ncard = 0;
    s = w_soff[W_UISTREAM];
    n = w_sextra[W_UISTREAM];
    for (i = 0; i < n; i++) {
        rec = s + i * W_REC_SIZE;
        if (w_b(w_seg, rec) == W_REC_CARD)
            w_ncard++;
        else if (w_b(w_seg, rec) == W_REC_COMP)
            w_ncomp++;
    }
}

/* ============================================================================
 * THE ENTRY (apps/weave/wpvabi.inc)
 *
 * One verb that draws and one that says which module this is. The answer is a
 * word: 1 for a card that was walked and painted, or 0 with the WPVE_* code
 * in the high byte - the "did it run" / "what did it say" split apps/cc/
 * crt0.asm makes every refusable call make (WEAVE-SPEC 1.2.1).
 * ==========================================================================*/
int lpv_verb(int verb, unsigned bseg, unsigned parm, void *win)
{
    int e, card;

    if (verb == WPVV_ABOUT)
        return WPV_ABI;
    if (verb != WPVV_PAINT)
        return 0;

    /* The pane, out of the caller's block. It arrived as an OFFSET into
     * LOOM's DS, which lmpvmod.asm has banked - reading it is a w_b/w_w
     * against that segment, exactly as the bundle is read against its own. */
    w_org.x = (int)w_w(lpv_cds(), parm + WPVP_X);
    w_org.y = (int)w_w(lpv_cds(), parm + WPVP_Y);
    w_sz.w = (int)w_w(lpv_cds(), parm + WPVP_W);
    w_sz.h = (int)w_w(lpv_cds(), parm + WPVP_H);
    card = (int)w_w(lpv_cds(), parm + WPVP_CARD);

    w_seg = bseg;
    w_win = win;
    w_state = 0;
    if ((e = lpv_sections()) != 0)
        return e << 8;
    lpv_count();
    if (card > 0)
        w_entry = (unsigned)card;
    if (w_entry < 1 || w_entry > w_ncard)
        return WPVE_CARD << 8;

    /* A fresh image every time Preview repacks, so nothing is cached across a
     * call: the component state is reset and the walk is forced. w_layout()
     * skips w_flow() when the cell grid and the card are what the table was
     * built for, which is right in a running app and wrong here - the table
     * would be yesterday's bundle at today's size. */
    w_pstate();
    w_lay_cw = -1;
    w_lay_ch = -1;
    w_lay_card = -1;
    w_state = W_ST_RUN;
    if (!w_layout(w_win)) {
        w_state = 0;
        return WPVE_PANE << 8;
    }
    w_padnow = 0;                       /* clean ground: LOOM whitened the
                                         * pane before it called us, which is
                                         * the same promise the kernel makes
                                         * WEAVE before W_PAINT (wpaint.c's
                                         * note on wd_button's `fill`) */
    w_paint_card(w_win);
    w_state = 0;
    return 1;
}
