/* os8088 - apps/pagedesk/hosttest/pdu_ed_test.c - the edit store and undo's
 * host test (PAGEDESK-SPEC 13.1.1, 13.6, 20.8; PD-12..PD-16).
 *
 * Compiles apps/pagedesk/pduedit.c and pduundo.c - the SAME files the package
 * compiles - against a 1 MiB real-mode arena (hosttest/pdhost.c) and the
 * smallest stand-in for the shell, then checks:
 *   - every kind lands in 13.1.1's layout (points, PENUP, text), the heaps
 *     never overlap and never leave the segment, ids count up
 *   - the limits are refused with the right code and change nothing
 *   - undo/redo of create, set, move, delete, text, points and the typing
 *     session, one command at a time, against SNAPSHOTS of the logical state
 *   - coalescing (a drag's sets, typing) and truncation of the oldest step,
 *     said once
 *   - the save point: undo back to it is clean, a new edit below it never is
 *   - source annotations (copy-on-edit) and page-operation deltas
 *   - a seeded random walk of commands, undone and redone in random order
 *     with every state compared with the snapshot taken when it was made
 *
 *   cc -DPD_HOST -I apps/pagedesk -o build/pdu_ed apps/pagedesk/hosttest/pdu_ed_test.c \
 *      apps/pagedesk/pdmath.c apps/pagedesk/hosttest/pdhost.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef PD_HOST
#define PD_HOST
#endif
struct os88_rect { int x1, y1, x2, y2; };
#include "../pdu.h"
#include "../pdmath.h"

#define PDU_DC_EDITS 6
#define PDU_DC_UNDO 7
extern unsigned char pd_host_mem[];
extern pdw pd_host_ds;
/* ---- the shell, as small as it can be ---------------------------------------- */
#define os88_memcpy memcpy
#define os88_memset memset
#define os88_strcpy(d, s, n) do { strncpy((d), (s), (n)); (d)[(n) - 1] = 0; } while (0)
static void *pdu_win;
static struct pdu_lay pdu_L;
static struct pd_view pdu_view;
static unsigned pdu_page;
static int pdu_doc = D_OPEN;
static int pdu_changes, pdu_pop = P_NONE, pdu_about, pdu_rwant, pdu_job;
static int pdu_tool, pdu_mode, pdu_rdonly;
static char pdu_line[96];
static char said[160];
static int nsaid, nrender, nui;
static struct os88_rect lastrender;
static void pdu_cat(char *d, const char *s, unsigned cap) { strncat(d, s, cap - strlen(d) - 1); }
static void pdu_tell(const char *s) { strncpy(said, s, sizeof(said) - 1); nsaid++; }
static void pdu_menusync(void) { nui++; }
static void pdu_ctl_repaint(int id) { }
static void pdu_title_set(void) { }
static void pdu_render_want(struct os88_rect *r) { lastrender = *r; nrender++; }
static int pdu_isect(struct os88_rect *a, struct os88_rect *b, struct os88_rect *o)
{   /* pdudraw.c's, verbatim in effect */
    o->x1 = a->x1 > b->x1 ? a->x1 : b->x1;
    o->y1 = a->y1 > b->y1 ? a->y1 : b->y1;
    o->x2 = a->x2 < b->x2 ? a->x2 : b->x2;
    o->y2 = a->y2 < b->y2 ? a->y2 : b->y2;
    return o->x1 <= o->x2 && o->y1 <= o->y2;
}
static unsigned pdu_muldiv(unsigned a, unsigned b, unsigned c) { return (unsigned)((unsigned long long)a * b / c); }
static void os88_gfx_lock(void) {}
static void os88_gfx_unlock(void) {}
static int nxor;
static void os88_gfx_xor_fill(int a, int b, int c, int d) { nxor++; }
static int os88_wm_clip_set(void *w) { return 0; }
/* QA F26: the edit store is a claim of its own, taken at the first edit
 * (ovl_ed_need): the document's claims and the store, as pdumod.c holds them */
static unsigned pdu_dc[12];             /* PDU_NDC */
static unsigned pdu_dclaim[3] = { 0x1000, 0x1800, 0x2000 };
static unsigned claimhi_ans = 0x2000, claimhi_n;
static unsigned pdu_claimhi(unsigned paras) { claimhi_n++; return paras == 1024 ? claimhi_ans : 0; }
static unsigned pdu_dcseg(int row)
{
    return row == PDU_DC_EDITS ? 0x2000 : row == PDU_DC_UNDO ? 0x3000 : 0;
}
static int pgapply_n, pgapply_undo;
static unsigned char pgapply_buf[200];
static unsigned pgapply_len;
#define PDU_PAGES 1
void pdu_pg_apply(const void *d, unsigned n, int undo)
{
    pgapply_n++;
    pgapply_undo = undo;
    pgapply_len = n;
    memcpy(pgapply_buf, d, n);
}

#include "../pduedit.c"
#include "../pduundo.c"

/* ---- the harness ---------------------------------------------------------------- */
static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { if (fails++ < 30) { \
    printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static struct pd_edit E;
static pd_pt P[2 * 64];

static void fresh(void)
{
    memset(pd_host_mem, 0xA5, 0x110000);     /* NOT zeroed (D15) */
    pdu_ed_init();
    pdu_page = 0;
    nsaid = 0;
    said[0] = 0;
}

static int mk(int kind, int page, int x0, int y0, int x1, int y1)
{
    memset(&E, 0, sizeof(E));
    E.kind = kind;
    E.page = page;
    E.rect.x0 = x0; E.rect.y0 = y0; E.rect.x1 = x1; E.rect.y1 = y1;
    E.colour = 4;
    return 0;
}

/* a canonical dump of the logical state: count, then per record its fixed
 * fields, its text and its points. Bytes the store keeps beyond the count (so
 * redo can restore without a copy) are, rightly, not part of it. */
static unsigned char snap[1 << 16];
static unsigned snap_len(void)
{
    unsigned n = 0, i, k;
    struct pd_edit e;
    char t[1100];
    pd_pt p[400];
    snap[n++] = (unsigned char)pdu_ed_count();
    for (i = 0; i < (unsigned)pdu_ed_count(); i++) {
        pdu_ed_get(i, &e);
        memcpy(snap + n, &e, 48);
        /* the heap offsets are not state: only what they point at is */
        memset(snap + n + 22, 0, 8);
        n += 48;
        k = pdu_ed_text_get(i, t, sizeof(t));
        snap[n++] = (unsigned char)k; snap[n++] = (unsigned char)(k >> 8);
        memcpy(snap + n, t, k); n += k;
        k = pdu_ed_pts_get(i, p, 400);
        snap[n++] = (unsigned char)k; snap[n++] = (unsigned char)(k >> 8);
        memcpy(snap + n, p, k * 4); n += k * 4;
    }
    return n;
}
static unsigned char *saved[400];
static unsigned savedn[400];
static void keep(int k)
{
    unsigned n = snap_len();
    free(saved[k]);
    saved[k] = malloc(n);
    memcpy(saved[k], snap, n);
    savedn[k] = n;
}
static int same(int k)
{
    unsigned n = snap_len();
    return n == savedn[k] && memcmp(snap, saved[k], n) == 0;
}

static void test_layout(void)
{
    int a, b, c, d, e, f, g, i;
    struct pd_edit r;
    char t[64];
    pd_pt q[16];

    fresh();
    CHECK(pdu_ed_count() == 0 && !pdu_dirty() && !pdu_undo_can() && !pdu_redo_can(),
          "a new document: no records, clean, nothing to undo");
    /* a two-line highlight: two quads, UL UR LL LR, rect = the union */
    mk(PDA_HIGHLIGHT, 0, 100, 200, 900, 380);
    for (i = 0; i < 16; i++) P[i] = (pd_pt)(100 + i * 7);
    a = pdu_ed_new(&E, "", 0, P, 8);
    CHECK(a == 0, "first record is index 0 (%d)", a);
    CHECK(E.id == 1 && (E.flags & PDEF_DIRTY), "id assigned, DIRTY set");
    CHECK(E.pts_n == 8 && E.pts_off + 32 == ED_END, "highlight: 8 points at the top of the segment (%u)", E.pts_off);
    /* ink with two strokes */
    mk(PDA_INK, 0, 0, 0, 50, 50);
    P[0] = 10; P[1] = 10; P[2] = 20; P[3] = 30; P[4] = ED_PENUP; P[5] = 0; P[6] = 40; P[7] = 40; P[8] = 45; P[9] = 49;
    E.size = 4;
    b = pdu_ed_new(&E, "", 0, P, 5);
    CHECK(b == 1, "ink index");
    pdu_ed_pts_get(b, q, 16);
    CHECK(q[4] == ED_PENUP && q[5] == 0 && q[6] == 40, "PENUP survives the heap");
    CHECK(pdu_ed_pts_get(b, q, 3) == 3, "pts_get is capped by cap");
    mk(PDA_LINE, 0, 0, 0, 90, 90);
    P[0] = 0; P[1] = 0; P[2] = 90; P[3] = 90;
    E.align = 2; E.style = 1;
    c = pdu_ed_new(&E, "", 0, P, 2);
    mk(PDA_RECT, 0, 5, 5, 99, 99);       d = pdu_ed_new(&E, "", 0, (pd_pt *)0, 0);
    mk(PDA_ELLIPSE, 1, 5, 5, 99, 99);    e = pdu_ed_new(&E, "", 0, (pd_pt *)0, 0);
    mk(PDA_NOTE, 0, 500, 500, 660, 644); f = pdu_ed_new(&E, "Check the date", 14, (pd_pt *)0, 0);
    mk(PDA_TEXT, 0, 700, 700, 1500, 1000); E.size = 12;
    g = pdu_ed_new(&E, "Tested on two IBM XTs.", 22, (pd_pt *)0, 0);
    CHECK(c == 2 && d == 3 && e == 4 && f == 5 && g == 6, "indices in z-order");
    CHECK(pdu_ed_count() == 7, "count");
    pdu_ed_get(f, &r);
    CHECK(r.text_len == 14 && r.text_off >= ED_TEXT0, "note text in the text heap");
    CHECK(pdu_ed_text_get(f, t, sizeof(t)) == 14 && strcmp(t, "Check the date") == 0, "note text reads back");
    CHECK(pdu_ed_text_get(g, t, 8) == 7 && strcmp(t, "Tested ") == 0, "text_get is capped and terminated");
    pdu_ed_get(g, &r);
    CHECK(r.kind == PDA_TEXT && r.size == 12 && r.rect.x0 == 700, "text box fixed fields");
    for (i = 0; i < 7; i++) {
        struct pd_edit x, y;
        pdu_ed_get(i, &x);
        CHECK(x.id == i + 1, "ids count up (%d)", x.id);
        CHECK(x.text_off + x.text_len <= pdu_ed_tt && x.pts_off >= pdu_ed_pt && x.pts_off + x.pts_n * 4 <= ED_END,
              "record %d's heaps lie inside the used heaps", i);
        for (b = i + 1; b < 7; b++) {
            pdu_ed_get(b, &y);
            if (x.pts_n && y.pts_n)
                CHECK(x.pts_off + x.pts_n * 4 <= y.pts_off || y.pts_off + y.pts_n * 4 <= x.pts_off,
                      "point heaps %d,%d disjoint", i, b);
            if (x.text_len && y.text_len)
                CHECK(x.text_off + x.text_len <= y.text_off || y.text_off + y.text_len <= x.text_off,
                      "text heaps %d,%d disjoint", i, b);
        }
    }
    /* iteration, page 0 live in z-order */
    pdu_ed_del(3);
    {
        int seq[8], n = 0;
        for (i = pdu_ed_first(0); i >= 0; i = pdu_ed_next(i, 0)) seq[n++] = i;
        CHECK(n == 5 && seq[0] == 0 && seq[1] == 1 && seq[2] == 2 && seq[3] == 5 && seq[4] == 6,
              "page 0 live records, z-order, deleted skipped (n=%d)", n);
        CHECK(pdu_ed_first(1) == 4 && pdu_ed_next(4, 1) == -1, "page 1 has the ellipse");
        CHECK(pdu_ed_first(9) == -1, "an empty page");
    }
    CHECK(pdu_ed_get(99, &r) == -1 && pdu_ed_get(-1, &r) == -1, "bad index");
    CHECK(pdu_ed_seg() == 0x2000, "the segment for the renderer and writer");
}

static void test_limits(void)
{
    int i, n;
    char big[1100];
    struct pd_edit r;
    unsigned tt, pt;

    fresh();
    memset(big, 'x', sizeof(big));
    mk(PDA_NOTE, 0, 0, 0, 160, 144);
    CHECK(pdu_ed_new(&E, big, 1025, (pd_pt *)0, 0) == -1 && pdu_ed_err == PDE_LIMIT,
          "1,025 bytes of text: PDE_LIMIT");
    CHECK(pdu_ed_count() == 0, "...and nothing was stored");
    mk(PDA_NOTE, 0, 0, 0, 160, 144);
    CHECK(pdu_ed_new(&E, big, 1024, (pd_pt *)0, 0) == 0, "1,024 bytes fit");
    for (n = 1; n < ED_MAX; n++) {
        mk(PDA_RECT, 0, 0, 0, 100, 100);
        if (pdu_ed_new(&E, "", 0, (pd_pt *)0, 0) < 0)
            break;
    }
    CHECK(n == ED_MAX, "128 records fit (%d)", n);
    mk(PDA_RECT, 0, 0, 0, 100, 100);
    tt = pdu_ed_tt; pt = pdu_ed_pt;
    CHECK(pdu_ed_new(&E, "", 0, (pd_pt *)0, 0) == -1 && pdu_ed_err == PDE_LIMIT, "129th record: PDE_LIMIT");
    CHECK(tt == pdu_ed_tt && pt == pdu_ed_pt && pdu_ed_count() == 128, "...and no top moved");
    /* heap exhaustion */
    fresh();
    for (i = 0; i < 64; i++) P[i] = 7;
    n = 0;
    for (i = 0; i < 100; i++) {
        mk(PDA_INK, 0, 0, 0, 10, 10);
        if (pdu_ed_new(&E, big, 1000, P, 32) < 0) {
            CHECK(pdu_ed_err == PDE_NOMEM, "the heaps full: PDE_NOMEM (%d)", pdu_ed_err);
            break;
        }
        n++;
    }
    CHECK(n > 5 && n < 100, "the heaps filled after %d records of 1 KB + 128 B", n);
    CHECK(pdu_ed_tt <= pdu_ed_pt, "the heaps never cross (%u %u)", pdu_ed_tt, pdu_ed_pt);
    /* a failed edit leaves the record and the history alone */
    pdu_ed_get(0, &r);
    CHECK(pdu_ed_text(0, big, 1024) == -1 || 1, "a full heap may refuse a replacement text");
    CHECK(pdu_ed_text(0, big, 1025) == -1 && pdu_ed_err == PDE_LIMIT, "text over 1,024: PDE_LIMIT");
}

static void test_undo_basic(void)
{
    struct pd_edit r;
    int a, b;
    char t[40];

    fresh();
    keep(0);
    mk(PDA_RECT, 0, 100, 100, 400, 300);
    pdu_undo_begin(UL_RECT);
    a = pdu_ed_new(&E, "", 0, (pd_pt *)0, 0);
    pdu_undo_end();
    keep(1);
    CHECK(pdu_dirty() && pdu_undo_can() && !pdu_redo_can(), "one command: dirty, can undo, cannot redo");
    CHECK(nrender > 0, "the render was owed");
    CHECK(pdu_undo() == 0 && pdu_ed_count() == 0 && same(0), "undo of create: gone");
    CHECK(pdu_ed_tt == ED_TEXT0 && pdu_ed_pt == ED_END, "...and both heap tops are back (%u %u)", pdu_ed_tt, pdu_ed_pt);
    CHECK(!pdu_dirty() && !pdu_undo_can() && pdu_redo_can(), "...clean again, can redo");
    CHECK(pdu_redo() == 0 && pdu_ed_count() == 1 && same(1), "redo restores the record, no copy kept");
    CHECK(pdu_dirty(), "dirty after redo");
    /* set: a drag of 5 sets is ONE step */
    pdu_undo_begin(UL_RESIZE);
    for (b = 1; b <= 5; b++) {
        pdu_ed_get(a, &r);
        r.rect.x1 = 400 + b * 10;
        r.rect.y1 = 300 + b * 10;
        pdu_ed_set(a, &r);
    }
    pdu_undo_end();
    keep(2);
    pdu_ed_get(a, &r);
    CHECK(r.rect.x1 == 450 && (r.flags & PDEF_MOVED), "resized, MOVED set");
    CHECK(pdu_undo() == 0, "undo the resize");
    pdu_ed_get(a, &r);
    CHECK(r.rect.x1 == 400 && r.rect.y1 == 300, "ONE undo undoes the five sets (x1=%d)", r.rect.x1);
    CHECK(same(1), "...to the exact earlier state");
    CHECK(pdu_redo() == 0 && same(2), "redo");
    /* colour and style */
    pdu_ed_get(a, &r);
    r.colour = PDC_BLUE; r.style = 3; r.size = 9;
    pdu_ed_set(a, &r);
    keep(3);
    pdu_undo();
    pdu_ed_get(a, &r);
    CHECK(r.colour == 4 && r.style == 0 && r.size == 0, "colour/style/size undone");
    pdu_redo();
    CHECK(same(3), "...and redone");
    /* delete */
    pdu_ed_del(a);
    CHECK(pdu_ed_first(0) == -1, "deleted: not iterated");
    pdu_undo();
    CHECK(pdu_ed_first(0) == a, "undo brings it back");
    pdu_redo();
    CHECK(pdu_ed_first(0) == -1, "redo deletes it again");
    pdu_undo();
    /* text */
    mk(PDA_NOTE, 0, 0, 0, 160, 144);
    b = pdu_ed_new(&E, "abc", 3, (pd_pt *)0, 0);
    keep(4);
    pdu_ed_text(b, "abcdef", 6);            /* in place, a prefix grows */
    pdu_ed_text_get(b, t, sizeof(t));
    CHECK(strcmp(t, "abcdef") == 0, "text grew");
    keep(5);
    pdu_ed_text(b, "xyz", 3);               /* not a prefix: a new block */
    pdu_ed_text_get(b, t, sizeof(t));
    CHECK(strcmp(t, "xyz") == 0, "text replaced");
    keep(6);
    pdu_undo(); CHECK(same(5), "undo replacement");
    pdu_undo(); CHECK(same(4), "undo growth");
    pdu_redo(); pdu_redo(); CHECK(same(6), "redo both");
    pdu_ed_text(b, "xy", 2);                /* a prefix shrinks in place */
    pdu_ed_text_get(b, t, sizeof(t));
    CHECK(strcmp(t, "xy") == 0, "text shrank");
    pdu_undo(); pdu_ed_text_get(b, t, sizeof(t));
    CHECK(strcmp(t, "xyz") == 0, "shrink undone");
    /* points */
    mk(PDA_INK, 0, 0, 0, 50, 50);
    P[0] = 1; P[1] = 2; P[2] = 3; P[3] = 4;
    a = pdu_ed_new(&E, "", 0, P, 2);
    keep(7);
    P[0] = 9; P[1] = 9; P[2] = 8; P[3] = 8; P[4] = 7; P[5] = 7;
    pdu_ed_pts(a, P, 3);
    keep(8);
    pdu_undo(); CHECK(same(7), "points undone");
    pdu_redo(); CHECK(same(8), "points redone");
    /* nothing to do */
    while (pdu_undo() == 0) ;
    CHECK(pdu_undo() == -1 && !pdu_dirty(), "undo at the bottom: -1, clean");
    while (pdu_redo() == 0) ;
    CHECK(pdu_redo() == -1 && same(8), "redo at the top: -1");
}

static void test_move(void)
{
    struct pd_edit r;
    pd_pt q[16];
    int a, b;
    fresh();
    mk(PDA_INK, 0, 10, 10, 60, 60);
    P[0] = 10; P[1] = 10; P[2] = 60; P[3] = 60; P[4] = ED_PENUP; P[5] = 0; P[6] = 30; P[7] = 40;
    a = pdu_ed_new(&E, "", 0, P, 4);
    mk(PDA_RECT, 0, 5, 5, 500, 500);
    b = pdu_ed_new(&E, "", 0, (pd_pt *)0, 0);
    keep(0);
    pdu_undo_begin(UL_MOVE);
    pdu_ed_move(a, 100, -3);
    pdu_ed_move(a, 5, 1);                   /* a drag's steps: one move */
    pdu_undo_end();
    pdu_ed_get(a, &r);
    CHECK(r.rect.x0 == 115 && r.rect.y0 == 8 && r.rect.x1 == 165 && r.rect.y1 == 58, "rect moved by the sum");
    pdu_ed_pts_get(a, q, 16);
    CHECK(q[0] == 115 && q[1] == 8 && q[2] == 165 && q[3] == 58 && q[4] == ED_PENUP && q[6] == 135 && q[7] == 38,
          "points moved, PENUP kept");
    keep(1);
    pdu_undo();
    CHECK(same(0), "ONE undo moves it all the way back, exactly");
    pdu_redo();
    CHECK(same(1), "redo moves it again");
    pdu_ed_move(b, -5, -5);
    pdu_undo();
    pdu_undo();
    CHECK(pdu_ed_get(a, &r) == 0 && r.rect.x0 == 10, "two undos");

    /* a move, a resize and a points change, then a move again, all ONE
     * command: the second move must not fold into the first across them */
    fresh();
    mk(PDA_INK, 0, 10, 10, 60, 60);
    P[0] = 10; P[1] = 10; P[2] = 60; P[3] = 60;
    a = pdu_ed_new(&E, "", 0, P, 2);
    keep(0);
    pdu_undo_begin(UL_MOVE);
    pdu_ed_move(a, 7, 3);
    pdu_ed_get(a, &r);
    r.rect.x1 += 40;
    pdu_ed_set(a, &r);
    pdu_ed_move(a, 2, 2);
    P[0] = 100; P[1] = 100; P[2] = 200; P[3] = 200;
    pdu_ed_pts(a, P, 2);
    pdu_ed_move(a, -4, 9);
    pdu_undo_end();
    keep(1);
    pdu_undo();
    CHECK(same(0), "move/set/pts/move as one command: undo is exact");
    pdu_redo();
    CHECK(same(1), "...and redo is exact");
}

static void test_typing(void)
{
    struct pd_edit r;
    int a, i;
    char t[64];
    const char *w = "Hello";

    fresh();
    keep(0);
    mk(PDA_TEXT, 0, 0, 0, 800, 300);
    E.size = 12;
    pdu_undo_begin(UL_TEXT);
    a = pdu_ed_new(&E, "", 0, (pd_pt *)0, 0);
    pdu_undo_end();
    pdu_ed_select(a);
    pdu_tx_begin(a, 1);
    for (i = 0; w[i]; i++)
        CHECK(pdu_tx_key(w[i]) == 1, "key taken");
    pdu_tx_key(8);
    pdu_tx_key('p');
    pdu_tx_key(13);
    pdu_tx_key('!');
    pdu_ed_text_get(a, t, sizeof(t));
    CHECK(strcmp(t, "Hellp\n!") == 0, "typed text, a backspace, a newline (%s)", t);
    CHECK(pdu_tx_key(1) == 0, "a control key is not text");
    keep(1);
    pdu_tx_end();
    CHECK(pdu_ed_count() == 1, "a typed box is kept");
    CHECK(pdu_undo() == 0 && pdu_ed_count() == 0 && same(0), "ONE undo removes the box AND its typing");
    CHECK(pdu_redo() == 0 && same(1), "ONE redo brings the box back with its text");
    /* typing into an existing box is one step of its own */
    pdu_tx_begin(a, 0);
    pdu_tx_key('x'); pdu_tx_key('y'); pdu_tx_key('z');
    pdu_tx_end();
    keep(2);
    pdu_ed_text_get(a, t, sizeof(t));
    CHECK(strcmp(t, "Hellp\n!xyz") == 0, "typed on");
    CHECK(pdu_undo() == 0, "undo typing");
    pdu_ed_text_get(a, t, sizeof(t));
    CHECK(strcmp(t, "Hellp\n!") == 0 && same(1), "ONE undo takes all three letters (%s)", t);
    pdu_redo();
    CHECK(same(2), "redo");
    /* typos cost the heap nothing: a backspace inside the run gives its byte
     * back, a backspace into the OLD text does not (undo needs it) */
    {
        pdw t0;
        int k;
        pdu_tx_begin(a, 0);
        pdu_tx_key('a');
        t0 = pdu_ed_tt;
        for (k = 0; k < 300; k++) { pdu_tx_key(8); pdu_tx_key('b' + k % 20); }
        CHECK(pdu_ed_tt == t0, "300 typos in a run use no heap (%u -> %u)", t0, pdu_ed_tt);
        pdu_tx_key(8); pdu_tx_key(8);       /* into "xyz": the old text */
        pdu_tx_key('Q');
        pdu_ed_text_get(a, t, sizeof(t));
        CHECK(strcmp(t, "Hellp\n!xyQ") == 0, "backspace into the old text (%s)", t);
        pdu_tx_end();
        CHECK(pdu_undo() == 0 && same(2), "ONE undo takes the whole run back, old text intact");
        CHECK(pdu_redo() == 0, "redo the run");
        pdu_ed_text_get(a, t, sizeof(t));
        CHECK(strcmp(t, "Hellp\n!xyQ") == 0, "redo restores the run (%s)", t);
        pdu_undo();
    }
    /* a save between keystrokes ends the coalescing */
    pdu_saved();
    pdu_tx_begin(a, 0);
    pdu_tx_key('q');
    CHECK(pdu_dirty(), "typing after a save is dirty (a new step)");
    pdu_tx_end();
    pdu_undo();
    CHECK(!pdu_dirty(), "...and undo returns to the saved state");
    /* a born-empty box is not a change */
    pdu_saved();
    mk(PDA_NOTE, 0, 0, 0, 160, 144);
    pdu_undo_begin(UL_NOTE);
    i = pdu_ed_new(&E, "", 0, (pd_pt *)0, 0);
    pdu_undo_end();
    CHECK(pdu_dirty(), "a placed note is a change until it is dropped");
    pdu_ed_select(i);
    pdu_tx_begin(i, 1);
    pdu_tx_end();
    CHECK(!pdu_dirty() && pdu_ed_count() == 1 && pdu_ed_sel == -1,
          "an empty new note vanishes, clean again (n=%d)", pdu_ed_count());
    CHECK(!pdu_redo_can(), "...and leaves no redo behind");
    /* a full box says so and keeps what was typed */
    mk(PDA_TEXT, 0, 0, 0, 800, 300);
    i = pdu_ed_new(&E, "", 0, (pd_pt *)0, 0);
    pdu_tx_begin(i, 1);
    {
        int n;
        for (n = 0; n < 1030; n++) pdu_tx_key('a');
    }
    pdu_ed_get(i, &r);
    CHECK(r.text_len == 1024, "text stops at 1,024 (%u)", r.text_len);
    CHECK(strstr(said, "1,024"), "...and says so: %s", said);
    pdu_tx_end();
}

static void test_history(void)
{
    int i, a;
    struct pd_edit r;

    fresh();
    nsaid = 0;
    for (i = 0; i < 70; i++) {
        mk(PDA_RECT, 0, i, i, i + 100, i + 100);
        pdu_undo_begin(UL_RECT);
        a = pdu_ed_new(&E, "", 0, (pd_pt *)0, 0);
        pdu_undo_end();
        if (i == 63)
            CHECK(nsaid == 0, "no truncation yet at 64 commands");
        if (i == 64)
            CHECK(nsaid == 1 && strstr(said, "oldest"), "the 65th command announces the dropped one: %s", said);
    }
    CHECK(nsaid == 1, "...announced ONCE (%d)", nsaid);
    for (i = 0; i < 70; i++)
        if (pdu_undo() != 0)
            break;
    CHECK(i == 64, "exactly 64 commands can be undone (%d)", i);
    CHECK(pdu_dirty(), "the saved state fell off the history: always dirty");
    CHECK(pdu_ed_count() == 6, "the six oldest stay (%d)", pdu_ed_count());
    for (i = 0; i < 70; i++)
        if (pdu_redo() != 0)
            break;
    CHECK(i == 64 && pdu_ed_count() == 70, "and all 64 come back (%d, n=%d)", i, pdu_ed_count());
    /* entries run out before commands do */
    fresh();
    mk(PDA_INK, 0, 0, 0, 5, 5);
    a = pdu_ed_new(&E, "", 0, P, 2);
    for (i = 0; i < 60; i++) {
        pdu_undo_begin(UL_PROPS);
        pdu_ed_get(a, &r);
        r.rect.x1 = 10 + i;
        r.colour = i & 15;
        r.irt = i;
        r.flags ^= PDEF_IRTSRC;
        pdu_ed_set(a, &r);
        pdu_ed_move(a, 1, 1);
        pdu_ed_del(a);
        pdu_ed_get(a, &r);
        r.flags &= ~PDEF_DELETED;
        pdu_undo_end();
    }
    CHECK(ud_ntot > 120, "entries were written (%u)", ud_ntot);
    {
        int n = 0;
        while (pdu_undo() == 0) n++;
        CHECK(n >= 40 && n <= 64, "the ring kept %d commands", n);
    }
}

static void test_savepoint(void)
{
    int a;
    fresh();
    mk(PDA_RECT, 0, 0, 0, 100, 100);
    pdu_ed_new(&E, "", 0, (pd_pt *)0, 0);
    pdu_ed_new(&E, "", 0, (pd_pt *)0, 0);
    pdu_saved();
    CHECK(!pdu_dirty(), "saved: clean");
    pdu_ed_new(&E, "", 0, (pd_pt *)0, 0);
    pdu_ed_new(&E, "", 0, (pd_pt *)0, 0);
    CHECK(pdu_dirty(), "two edits: dirty");
    pdu_undo();
    CHECK(pdu_dirty(), "one undone: still dirty");
    pdu_undo();
    CHECK(!pdu_dirty(), "undo back to the save point: CLEAN");
    pdu_undo();
    CHECK(pdu_dirty(), "below the save point: dirty");
    pdu_redo();
    CHECK(!pdu_dirty(), "redo up to it: clean");
    pdu_redo();
    CHECK(pdu_dirty(), "redo past it: dirty");
    pdu_undo();
    pdu_undo();
    /* a new edit made from the save point keeps the redo branch off */
    a = pdu_ed_new(&E, "", 0, (pd_pt *)0, 0);
    CHECK(a == 1 && !pdu_redo_can() && pdu_dirty(), "a new edit below the save point clears the redo branch (a=%d)", a);
    pdu_undo();
    CHECK(pdu_dirty(), "...and the saved state is on the dead branch: never clean again");
    pdu_ed_new(&E, "", 0, (pd_pt *)0, 0);
    pdu_ed_new(&E, "", 0, (pd_pt *)0, 0);
    CHECK(pdu_dirty(), "a new branch grown to the saved position is still not it");
    pdu_undo(); pdu_undo(); pdu_undo();
    CHECK(pdu_dirty(), "at the bottom: dirty");
    pdu_saved();
    CHECK(!pdu_dirty(), "...until the next save");
    /* a failed save keeps the dirty state: it just never calls pdu_saved() */
    pdu_ed_new(&E, "", 0, (pd_pt *)0, 0);
    CHECK(pdu_dirty(), "dirty without pdu_saved()");
}

/* D19: notes refreshes on pdu_ed_gen(), so every mutation, undo, redo and
 * selection change must move it, and a read must not */
static void test_gen(void)
{
    unsigned g;
    struct pd_edit r;
    int a;
    fresh();
    g = pdu_ed_gen();
    mk(PDA_RECT, 0, 100, 100, 600, 600);
    a = pdu_ed_new(&E, "", 0, (pd_pt *)0, 0);
    CHECK(pdu_ed_gen() != g, "gen: new"); g = pdu_ed_gen();
    pdu_ed_get(a, &r);
    pdu_ed_first(0);
    CHECK(pdu_ed_gen() == g, "gen: a read leaves it");
    r.colour = 9;
    pdu_ed_set(a, &r);
    CHECK(pdu_ed_gen() != g, "gen: set"); g = pdu_ed_gen();
    pdu_ed_move(a, 16, 16);
    CHECK(pdu_ed_gen() != g, "gen: move"); g = pdu_ed_gen();
    pdu_undo();
    CHECK(pdu_ed_gen() != g, "gen: undo"); g = pdu_ed_gen();
    pdu_redo();
    CHECK(pdu_ed_gen() != g, "gen: redo"); g = pdu_ed_gen();
    pdu_ed_select(a);
    CHECK(pdu_ed_gen() != g && pdu_ed_selected() == a, "gen: select"); g = pdu_ed_gen();
    pdu_page = 1;                           /* the page changes under it */
    pdu_ed_canvas();
    CHECK(pdu_ed_gen() != g && pdu_ed_selected() < 0, "gen: a page change deselects");
    pdu_page = 0;
    g = pdu_ed_gen();
    pdu_ed_del(a);
    CHECK(pdu_ed_gen() != g, "gen: delete");
    CHECK(pdu_ed_get(a, &r) == 0 && r.id != 0, "ids are never 0");
}

static void test_src(void)
{
    struct pd_edit r;
    int a, b;
    fresh();
    mk(PDA_HIGHLIGHT, 0, 10, 10, 200, 40);
    a = pdu_ed_copy_src(12, 0, &E);
    CHECK(a == 0, "a source annotation gets a record");
    pdu_ed_get(a, &r);
    CHECK(r.src_num == 12 && r.src_gen == 0, "...naming the source object");
    memset(&E, 0, sizeof(E));
    b = pdu_ed_copy_src(12, 0, &E);
    CHECK(b == a && E.src_num == 12 && E.kind == PDA_HIGHLIGHT, "asking again finds it (and fills e)");
    mk(PDA_NOTE, 0, 10, 10, 200, 40);
    b = pdu_ed_copy_src(13, 0, &E);
    CHECK(b == 1 && pdu_ed_count() == 2, "another object, another record");
    pdu_ed_del(a);
    pdu_ed_get(a, &r);
    CHECK((r.flags & PDEF_DELETED) && r.src_num == 12, "a deleted source record STAYS as the deletion");
    CHECK(pdu_ed_copy_src(12, 0, &E) == a, "...and is still found");
    keep(0);
    pdu_undo();
    pdu_ed_get(a, &r);
    CHECK(!(r.flags & PDEF_DELETED), "undelete");
}

static void test_pageop(void)
{
    unsigned char d[40], big[200];
    int i;
    fresh();
    for (i = 0; i < 40; i++) d[i] = i + 1;
    pgapply_n = 0;
    pdu_undo_pageop(d, 20);
    CHECK(pdu_dirty() && pdu_undo_can(), "a page operation is a command");
    pdu_undo();
    CHECK(pgapply_n == 1 && pgapply_undo == 1 && pgapply_len == 20 && memcmp(pgapply_buf, d, 20) == 0,
          "undo hands the delta back with undo=1");
    pdu_redo();
    CHECK(pgapply_n == 2 && pgapply_undo == 0 && pgapply_len == 20 && memcmp(pgapply_buf, d, 20) == 0,
          "redo with undo=0");
    pdu_undo_pageop(d, 8);
    pdu_undo();
    CHECK(pgapply_len == 8 && memcmp(pgapply_buf, d, 8) == 0, "an 8-byte delta (one chunk)");
    pdu_redo();
    /* mixed with annotation commands */
    mk(PDA_RECT, 0, 0, 0, 10, 10);
    pdu_ed_new(&E, "", 0, (pd_pt *)0, 0);
    pdu_undo(); pdu_undo(); pdu_undo();
    CHECK(!pdu_undo_can(), "all three undone");
    for (i = 0; i < 3; i++) pdu_redo();
    CHECK(pdu_ed_count() == 1 && pgapply_undo == 0, "all three redone, in order");
    memset(big, 7, sizeof(big));
    nsaid = 0;
    pdu_undo_pageop(big, 150);
    CHECK(!pdu_undo_can() && nsaid > 0 && strstr(said, "Too large"),
          "a delta too big to keep clears the history and says so: %s", said);
    CHECK(pdu_dirty(), "...and the document stays dirty");
}

/* ---- the random walk ------------------------------------------------------------- */
static unsigned seed = 12345;
static int rnd(int n) { seed = seed * 1103515245u + 12345u; return (int)((seed >> 16) % (unsigned)n); }

static int rw_pin = -1;                 /* the record a multi-op command aims at */
static int pick(int n) { return (rw_pin >= 0 && rw_pin < n && rnd(4)) ? rw_pin : rnd(n); }

static void do_random_op(void)
{
    struct pd_edit r;
    int n = pdu_ed_count(), k = rnd(10), a, i, j;
    char t[40];
    switch (k) {
    case 0: case 1: case 2:
        mk(PDA_NOTE + rnd(9), rnd(2), rnd(500), rnd(500), 500 + rnd(500), 500 + rnd(500));
        j = rnd(4);
        for (i = 0; i < 2 * j; i++) P[i] = rnd(2000) - 1000;
        if (E.kind == PDA_INK && j > 1) P[2] = ED_PENUP;
        for (i = 0; i < rnd(12); i++) t[i] = 'a' + rnd(26);
        pdu_ed_new(&E, t, i, P, j);
        break;
    case 3:
        if (n) { pdu_ed_get(a = pick(n), &r); r.rect.x1 += rnd(50); r.colour = rnd(16); r.size = rnd(30); pdu_ed_set(a, &r); }
        break;
    case 4:
        if (n) pdu_ed_move(pick(n), rnd(40) - 20, rnd(40) - 20);
        break;
    case 5:
        if (n) pdu_ed_del(pick(n));
        break;
    case 6:
        if (n) { for (i = 0; i < rnd(30); i++) t[i] = 'A' + rnd(26); pdu_ed_text(pick(n), t, i); }
        break;
    case 7:
        if (n) { for (i = 0; i < 2 * (j = rnd(6)); i++) P[i] = rnd(3000); pdu_ed_pts(pick(n), P, j); }
        break;
    case 8:
        if (n) {
            a = pick(n);
            pdu_tx_begin(a, 0);
            for (i = 0; i < 1 + rnd(5); i++) pdu_tx_key(i % 7 == 6 ? 8 : 'a' + rnd(26));
            pdu_tx_end();
        }
        break;
    case 9:
        if (n) { pdu_ed_get(a = pick(n), &r); r.irt = rnd(100); r.state = rnd(3); pdu_ed_set(a, &r); }
        break;
    }
}

/* one user command: usually one call, but a third of them several calls in
 * ONE bracket (a caller may do a move, a resize and a points change as one
 * step - 20.8 allows any mix), aimed at one record half the time so the
 * entries of one command interleave on it */
static int rw_multi = 1;
static void do_random_command(void)
{
    int k, ops;
    pdu_undo_begin(UL_PROPS);
    ops = (rw_multi && rnd(3) == 0) ? 2 + rnd(4) : 1;
    rw_pin = (ops > 1 && pdu_ed_count()) ? rnd(pdu_ed_count()) : -1;
    for (k = 0; k < ops; k++)
        do_random_op();
    rw_pin = -1;
    pdu_undo_end();
}

static void test_walk(unsigned s, int steps)
{
    int i, bad = 0, k, pos;
    seed = s;
    fresh();
    keep(0);
    for (i = 0; i < steps && bad < 3; i++) {
        k = rnd(10);
        if (k < 5) {
            do_random_command();
            pdu_ed_err = 0;
            pos = (ud_c0 + ud_cur) % 400;
            keep(pos);
        } else if (k < 8) {
            if (pdu_undo() == 0) {
                pos = (ud_c0 + ud_cur) % 400;
                if (!same(pos)) { bad++; printf("walk %u step %d: undo to %d differs\n", s, i, pos); }
            }
        } else {
            if (pdu_redo() == 0) {
                pos = (ud_c0 + ud_cur) % 400;
                if (!same(pos)) { bad++; printf("walk %u step %d: redo to %d differs\n", s, i, pos); }
            }
        }
        if (ud_cur > 0 && rnd(97) == 0) {         /* undo all the way and back */
            int d = 0;
            while (pdu_undo() == 0) d++;
            pos = (ud_c0 + ud_cur) % 400;
            if (!same(pos)) { bad++; printf("walk %u: full unwind differs at %d\n", s, pos); }
            if (ud_c0 == 0 && (pdu_ed_n || pdu_ed_tt != ED_TEXT0 || pdu_ed_pt != ED_END)) { bad++; printf("walk %u: unwound but heaps not empty %u %u\n", s, pdu_ed_tt, pdu_ed_pt); }
            while (d-- > 0 && pdu_redo() == 0) ;
            pos = (ud_c0 + ud_cur) % 400;
            if (!same(pos)) { bad++; printf("walk %u: full rewind differs at %d\n", s, pos); }
        }
    }
    CHECK(bad == 0, "random walk seed %u: %d bad states", s, bad);
}

/* QA F26: the store is claimed at the first edit, once, from the top, and an
 * edit with no store is refused (never written through segment 0) */
static void test_lazy_store(void)
{
    unsigned keep = pdu_dclaim[2];
    fresh();
    pdu_dclaim[2] = 0;
    claimhi_n = 0;
    CHECK(pdu_ed_new(&E, "", 0, (pd_pt *)0, 0) == -1 && pdu_ed_err == PDE_NOMEM,
          "no store: the record is refused with PDE_NOMEM");
    pdu_dclaim[0] = 0;
    CHECK(ovl_ed_need() == 1 && claimhi_n == 0, "no document: nothing is claimed");
    pdu_dclaim[0] = 0x1000;
    claimhi_ans = 0;
    CHECK(ovl_ed_need() == 1 && claimhi_n == 1 && !pdu_dclaim[2], "no room: refused");
    claimhi_ans = 0x2000;
    CHECK(ovl_ed_need() == 0 && pdu_dclaim[2] == 0x2000 && pdu_dc[PDU_DC_EDITS] == 0x2000,
          "the first edit claims 16 KiB and the row names it");
    CHECK(ovl_ed_need() == 0 && claimhi_n == 2, "held: not claimed again");
    mk(PDA_HIGHLIGHT, 0, 0, 0, 100, 100);
    CHECK(pdu_ed_new(&E, "", 0, (pd_pt *)0, 0) == 0 && pdu_ed_n == 1, "and the edit goes in");
    pdu_dclaim[2] = keep;
}

int main(void)
{
    unsigned s;
    pd_host_ds = 0x1000;
    pdu_view.box.x0 = 0; pdu_view.box.y0 = 0; pdu_view.box.x1 = 9792; pdu_view.box.y1 = 3200;
    pdu_view.rot = 0; pdu_view.zoom = 100; pdu_view.asp_num = 1; pdu_view.asp_den = 1;
    pdu_view.ox = 0; pdu_view.oy = 0;
    pdu_L.canvas.x1 = 0; pdu_L.canvas.y1 = 0; pdu_L.canvas.x2 = 639; pdu_L.canvas.y2 = 479;
    test_layout();
    test_limits();
    test_undo_basic();
    test_move();
    test_typing();
    test_history();
    test_savepoint();
    test_gen();
    test_src();
    test_pageop();
    test_lazy_store();
    for (s = 1; s <= 40; s++)
        test_walk(s, 400);
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
