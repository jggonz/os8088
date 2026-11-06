/* os8088 - apps/pagedesk/hosttest/pdu_nt.c - the Notes & edits list model's
 * host test (PAGEDESK-SPEC 17.3, 13.4, 20.9; PD-12, PD-16).
 *
 * Compiles apps/pagedesk/pdunotes.c - the SAME file the package compiles; its
 * resident hooks are behind PD_HOST - against a fake edit store and checks:
 *   - the three filters' membership and ORDER (This page / All pages /
 *     Changes), deleted records and review replies hidden except in Changes
 *   - the count equals the number of steps, seek(k)+step is item k, find(id)
 *     is k, for every k and every filter
 *   - a source annotation a record stands in for (copy-on-edit) is listed once
 *   - review state (13.4): newest reply wins, a PageDesk reply to a SOURCE
 *     note (PDEF_IRTSRC) beats the source's own, a reopen undoes a resolve
 *   - a 256-record store walks without any per-item state (the iterator is
 *     one int)
 *   - the source /Subtype -> kind table, the change words
 * Exit 0 when every check holds.
 *
 *   cc -DPD_HOST -I apps/pagedesk -o build/pdu_nt apps/pagedesk/hosttest/pdu_nt.c
 */
#include <stdio.h>
#include <string.h>
#ifndef PD_HOST
#define PD_HOST
#endif
struct os88_rect { int x1, y1, x2, y2; };
#include "../pdu.h"
#include "../pdunt.h"

static unsigned pdu_page;
static int pdu_npall;

/* ---- the fake store ------------------------------------------------------- */
#define MAXE 300
static struct pd_edit store[MAXE];
static int nstore;
int pdu_ed_count(void) { return nstore; }
int pdu_ed_get(int i, struct pd_edit *e)
{
    if (i < 0 || i >= nstore)
        return -1;
    memcpy(e, &store[i], sizeof(*e));
    return 0;
}
int pdu_ed_new(struct pd_edit *e, const char *t, unsigned n, const pd_pt *p, unsigned np) { return -1; }
int pdu_ed_set(int i, const struct pd_edit *e) { return -1; }
int pdu_ed_text(int i, const char *t, unsigned n) { return -1; }
unsigned pdu_ed_text_get(int i, char *d, unsigned c) { return 0; }
int pdu_ed_del(int i) { return -1; }
void pdu_undo_begin(int l) { }
void pdu_undo_end(void) { }
void pdu_ed_select(int i) { }
int pdu_ed_selected(void) { return -1; }
unsigned pdu_ed_gen(void) { return 0; }
static char pdu_tbuf[1025];

#include "../pdunotes.c"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { if (fails++ < 25) { \
    printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static int add(int kind, int page, int flags, unsigned id, unsigned irt,
               unsigned state, unsigned src)
{
    struct pd_edit *e = &store[nstore];
    memset(e, 0, sizeof(*e));
    e->kind = kind; e->page = page; e->flags = flags; e->id = id;
    e->irt = irt; e->state = state; e->src_num = src;
    return nstore++;
}

/* the model's own counting, step by step */
static int walk(int *ids, int cap)
{
    int n = 0, id;
    pdu_nt_pos = 0;
    while ((id = ovl_nt_step()) >= 0) {
        if (n < cap) ids[n] = id;
        n++;
    }
    return n;
}

static void consistent(const char *tag)
{
    int ids[400], n, k, id;
    ovl_nt_sync();
    n = walk(ids, 400);
    CHECK(n == pdu_nt_cnt, "%s: count %d but %d steps", tag, pdu_nt_cnt, n);
    for (k = 0; k < n && k < 400; k++) {
        ovl_nt_seek(k);
        id = ovl_nt_step();
        CHECK(id == ids[k], "%s: seek(%d) gave %d, walk %d", tag, k, id, ids[k]);
        CHECK(ovl_nt_find(ids[k]) == k, "%s: find(%d) != %d", tag, ids[k], k);
    }
    ovl_nt_seek(n);
    CHECK(ovl_nt_step() == -1, "%s: step past the end", tag);
}

static void reset(void)
{
    nstore = 0;
    pdu_nt_ns = 0; pdu_nt_nsr = 0; pdu_nt_cnt = -1; pdu_nt_nr = 0;
    pdu_nt_key0 = pdu_nt_key1 = pdu_nt_key2 = pdu_nt_key3 = -1;
    pdu_nt_gen++;
}

static void src(int k, unsigned obj, int kind)
{
    pdu_nt_so[k] = obj; pdu_nt_sk[k] = kind; pdu_nt_sh[k] = 0;
    if (k >= pdu_nt_ns) pdu_nt_ns = k + 1;
}

int main(void)
{
    int ids[400], n, i;

    /* ---- membership and order ---- */
    reset();
    add(PDA_NOTE, 2, PDEF_DIRTY, 1, 0, 0, 0);                  /* 0 page 2 */
    add(PDA_HIGHLIGHT, 3, PDEF_DIRTY, 2, 0, 0, 0);             /* 1 page 3 */
    add(PDA_TEXT, 3, PDEF_DIRTY, 3, 0, 0, 0);                  /* 2 page 3 */
    add(PDA_NOTE, 3, PDEF_DIRTY | PDEF_DELETED, 4, 0, 0, 0);   /* 3 deleted */
    add(PDA_NOTE, 3, PDEF_DIRTY, 5, 3, PDS_COMPLETED, 0);      /* 4 reply to #3 */
    add(PDA_INK, 4, PDEF_DIRTY, 6, 0, 0, 0);                   /* 5 page 4 */
    src(0, 900, PDA_NOTE);                                     /* page 3's own */
    src(1, 901, PDA_HIGHLIGHT);
    pdu_page = 3; pdu_npall = 0;
    ovl_nt_sync();
    n = walk(ids, 400);
    CHECK(n == 4 && ids[0] == 1 && ids[1] == 2 && ids[2] == PDU_NT_SBASE
          && ids[3] == PDU_NT_SBASE + 1,
          "This page: %d items %d %d %d %d", n, ids[0], ids[1], ids[2], ids[3]);
    consistent("page 3");
    pdu_npall = 1;
    ovl_nt_sync();
    n = walk(ids, 400);
    CHECK(n == 6 && ids[0] == 0 && ids[3] == 5, "All pages: %d items", n);
    consistent("all");
    pdu_npall = 2;
    ovl_nt_sync();
    n = walk(ids, 400);
    CHECK(n == 6 && ids[3] == 3 && ids[4] == 4,
          "Changes: all six records, deleted and reply included (%d)", n);
    consistent("changes");

    /* ---- copy-on-edit hides the source entry ---- */
    pdu_npall = 0;
    add(PDA_NOTE, 3, PDEF_DIRTY, 7, 0, 0, 900);
    pdu_nt_gen++;
    ovl_nt_sync();
    n = walk(ids, 400);
    CHECK(n == 4 && ids[2] == 6 && ids[3] == PDU_NT_SBASE + 1,
          "record 900 stands in for source 900 (%d items)", n);
    consistent("shadow");

    /* ---- review state, 13.4 ---- */
    reset();
    add(PDA_NOTE, 1, PDEF_DIRTY, 10, 0, 0, 0);                     /* note 10 */
    add(PDA_NOTE, 1, PDEF_DIRTY, 11, 10, PDS_COMPLETED, 0);        /* resolved */
    src(0, 700, PDA_NOTE);
    src(1, 701, PDA_NOTE);
    pdu_nt_srk[0] = 701; pdu_nt_srs[0] = PDS_COMPLETED; pdu_nt_nsr = 1;
    pdu_page = 1; pdu_npall = 0;
    ovl_nt_sync();
    CHECK(ovl_nt_done(0, 10) == 1, "a PageDesk reply resolves note 10");
    CHECK(ovl_nt_done(0, 11) == 0, "the reply itself is not a note");
    CHECK(ovl_nt_done(1, 701) == 1, "a source reply resolves source 701");
    CHECK(ovl_nt_done(1, 700) == 0, "source 700 is open");
    add(PDA_NOTE, 1, PDEF_DIRTY, 12, 10, PDS_NONE, 0);             /* reopened */
    add(PDA_NOTE, 1, PDEF_DIRTY | PDEF_IRTSRC, 13, 701, PDS_NONE, 0);
    add(PDA_NOTE, 1, PDEF_DIRTY | PDEF_IRTSRC, 14, 700, PDS_COMPLETED, 0);
    pdu_nt_gen++;
    ovl_nt_sync();
    CHECK(ovl_nt_done(0, 10) == 0, "the newest reply wins: note 10 reopened");
    CHECK(ovl_nt_done(1, 701) == 0, "a PageDesk reopen beats the source's resolve");
    CHECK(ovl_nt_done(1, 700) == 1, "a PageDesk reply resolves a source note");
    CHECK(ovl_nt_done(0, 700) == 0, "an object number is not a record id");
    consistent("review");
    /* a deleted reply no longer counts */
    store[2].flags |= PDEF_DELETED;
    pdu_nt_gen++;
    ovl_nt_sync();
    CHECK(ovl_nt_done(0, 10) == 1, "deleting the reopen reply resolves again");

    /* ---- 256 records, no per-item state ---- */
    reset();
    for (i = 0; i < 256; i++)
        add(i % 3 ? PDA_NOTE : PDA_TEXT, i % 8, PDEF_DIRTY, 100 + i, 0, 0, 0);
    pdu_page = 5; pdu_npall = 1;
    ovl_nt_sync();
    CHECK(pdu_nt_cnt == 256, "256 notes counted: %d", pdu_nt_cnt);
    consistent("256 all");
    pdu_npall = 0;
    pdu_nt_cnt = -1;
    ovl_nt_sync();
    CHECK(pdu_nt_cnt == 32, "page 5 holds 32 of them: %d", pdu_nt_cnt);
    consistent("256 page");

    /* ---- the tables ---- */
    CHECK(ovl_nt_kind("Text") == PDA_NOTE, "Text");
    CHECK(ovl_nt_kind("Highlight") == PDA_HIGHLIGHT, "Highlight");
    CHECK(ovl_nt_kind("Underline") == PDA_HIGHLIGHT, "Underline");
    CHECK(ovl_nt_kind("Squiggly") == PDA_HIGHLIGHT, "Squiggly");
    CHECK(ovl_nt_kind("StrikeOut") == PDA_HIGHLIGHT, "StrikeOut");
    CHECK(ovl_nt_kind("FreeText") == PDA_TEXT, "FreeText");
    CHECK(ovl_nt_kind("Ink") == PDA_INK, "Ink");
    CHECK(ovl_nt_kind("Line") == PDA_LINE, "Line");
    CHECK(ovl_nt_kind("Square") == PDA_RECT, "Square");
    CHECK(ovl_nt_kind("Circle") == PDA_ELLIPSE, "Circle");
    CHECK(ovl_nt_kind("Link") == 0, "Link is not listed");
    CHECK(ovl_nt_kind("Popup") == 0, "Popup is not listed");
    CHECK(ovl_nt_kind("Stamp") == 0, "Stamp is not listed");
    CHECK(ovl_nt_kind("Widget") == 0, "Widget is not listed");
    CHECK(ovl_nt_kind("Caret") == 0, "Caret is not an ellipse");
    CHECK(ovl_nt_kind("FileAttachment") == 0, "FileAttachment is not a text box");
    CHECK(ovl_nt_kind("TrapNet") == 0, "TrapNet is not a note");
    CHECK(ovl_nt_kind("StrikeOut") == PDA_HIGHLIGHT && ovl_nt_kind("Sound") == 0
          && ovl_nt_kind("Screen") == 0 && ovl_nt_kind("Polygon") == 0,
          "Sound, Screen, Polygon are not listed");
    {
        struct pd_edit e;
        memset(&e, 0, sizeof(e));
        e.flags = PDEF_DELETED;
        CHECK(!strcmp(ovl_nt_change(&e), "Deleted"), "Deleted");
        e.flags = 0; e.kind = PDA_NOTE; e.irt = 3; e.state = PDS_COMPLETED;
        CHECK(!strcmp(ovl_nt_change(&e), "Resolved"), "Resolved");
        e.state = PDS_NONE;
        CHECK(!strcmp(ovl_nt_change(&e), "Reopened"), "Reopened");
        e.irt = 0; e.src_num = 9;
        CHECK(!strcmp(ovl_nt_change(&e), "Edited"), "Edited");
        e.src_num = 0; e.flags = PDEF_MOVED;
        CHECK(!strcmp(ovl_nt_change(&e), "Moved"), "Moved");
        e.flags = PDEF_DIRTY;
        CHECK(!strcmp(ovl_nt_change(&e), "Added"), "Added");
    }

    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
