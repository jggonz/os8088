/* os8088 - apps/pagedesk/hosttest/pdu_lay.c - the layout's host test
 * (PAGEDESK-SPEC 17.1, 20.6.4; PD-02/03/04).
 *
 * Compiles apps/pagedesk/pdulay.c - the SAME file the package compiles -
 * and checks, for every content box from 320x100 to 720x460 and for the
 * five displays by name (VGA 640x480, EGA 640x350, Hercules 720x348 AND
 * 720x350, CGA 640x200):
 *   - every region and every control lies inside the content box
 *   - no two controls overlap, and each lies inside its own bar or panel
 *   - the canvas's left edge and right edge + 1 are multiples of 8 (the
 *     gfx_scroll contract) and the canvas is at least PDU_MINCANVAS wide
 *     whenever a panel is shown
 *   - regions do not overlap one another, and bars/canvas/status tile the
 *     content box's height exactly
 *   - the layout KIND follows 17.1's thresholds on the box alone
 * Exit 0 when every check holds; prints the first failures otherwise.
 *
 *   cc -DPD_HOST -I apps/pagedesk -o build/pdu_lay apps/pagedesk/hosttest/pdu_lay.c
 */
#include <stdio.h>
#include <stdlib.h>
#ifndef PD_HOST
#define PD_HOST
#endif
struct os88_rect { int x1, y1, x2, y2; };
#include "../pdu.h"
#include "../pdulay.c"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { if (fails++ < 25) { \
    printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static int inside(const struct os88_rect *r, int x1, int y1, int x2, int y2)
{
    return r->x1 >= x1 && r->x2 <= x2 && r->y1 >= y1 && r->y2 <= y2;
}
static int present(const struct os88_rect *r) { return r->x1 <= r->x2 && r->y1 <= r->y2; }
static int overlap(const struct os88_rect *a, const struct os88_rect *b)
{
    return present(a) && present(b) && a->x1 <= b->x2 && b->x1 <= a->x2
        && a->y1 <= b->y2 && b->y1 <= a->y2;
}

static struct pdu_lay L;

static void one(int w, int h, int side, int notes, int mode, int empty,
                const char *tag)
{
    int i, j, ox = 8, oy = 40, x2, y2;
    struct os88_rect *reg[7];
    struct os88_rect cr, cj;

    pdu_layout(&L, ox, oy, w, h, side, notes, mode, empty);
    x2 = ox + w - 1; y2 = oy + h - 1;
    reg[0] = &L.bar1; reg[1] = &L.bar2; reg[2] = &L.side; reg[3] = &L.notes;
    reg[4] = &L.canvas; reg[5] = &L.sbar; reg[6] = &L.status;
    for (i = 0; i < 7; i++)
        if (present(reg[i]))
            CHECK(inside(reg[i], ox, oy, x2, y2), "%s %dx%d region %d outside", tag, w, h, i);
    for (i = 0; i < 7; i++)
        for (j = i + 1; j < 7; j++)
            CHECK(!overlap(reg[i], reg[j]), "%s %dx%d regions %d and %d overlap", tag, w, h, i, j);
    CHECK(L.canvas.x1 % 8 == 0 && (L.canvas.x2 + 1) % 8 == 0,
          "%s %dx%d canvas %d..%d not on the 8 grid", tag, w, h, L.canvas.x1, L.canvas.x2);
    CHECK(L.canvas.y2 >= L.canvas.y1, "%s %dx%d no canvas rows", tag, w, h);
    if (present(&L.side) || present(&L.notes))
        CHECK(L.canvas.x2 - L.canvas.x1 + 1 >= PDU_MINCANVAS,
              "%s %dx%d canvas %d wide beside a panel", tag, w, h, L.canvas.x2 - L.canvas.x1 + 1);
    /* the column tiles the height: bars, canvas, status */
    CHECK(L.status.y2 == y2, "%s %dx%d status not at the bottom", tag, w, h);
    CHECK(L.canvas.y2 + 1 == L.status.y1, "%s %dx%d canvas/status gap", tag, w, h);
    CHECK(L.bar2.y2 + 1 == L.canvas.y1, "%s %dx%d bar/canvas gap", tag, w, h);
    CHECK(L.sbar.x2 <= x2 && L.sbar.y1 == L.canvas.y1 && L.sbar.y2 == L.canvas.y2,
          "%s %dx%d scroll bar not beside the canvas", tag, w, h);
    for (i = 0; i < L.nctl; i++) {
        struct pdu_ctl *c = &L.ctl[i];
        const struct os88_rect *home;
        cr.x1 = c->x1; cr.y1 = c->y1; cr.x2 = c->x2; cr.y2 = c->y2;
        CHECK(c->x1 <= c->x2 && c->y1 <= c->y2, "%s %dx%d ctl %d empty", tag, w, h, c->id);
        home = (c->flags & CF_BAR1) ? &L.bar1 : (c->flags & CF_BAR2) ? &L.bar2
             : (c->flags & CF_EMPTY) ? &L.canvas
             : (c->x1 >= L.side.x1 && c->x2 <= L.side.x2) ? &L.side : &L.notes;
        CHECK(inside(&cr, home->x1, home->y1, home->x2, home->y2),
              "%s %dx%d ctl %d (%d,%d)-(%d,%d) outside its home", tag, w, h, c->id,
              c->x1, c->y1, c->x2, c->y2);
        CHECK(c->y2 - c->y1 + 1 >= 10, "%s %dx%d ctl %d under 10 rows tall", tag, w, h, c->id);
        for (j = i + 1; j < L.nctl; j++) {
            cj.x1 = L.ctl[j].x1; cj.y1 = L.ctl[j].y1; cj.x2 = L.ctl[j].x2; cj.y2 = L.ctl[j].y2;
            CHECK(!overlap(&cr, &cj), "%s %dx%d ctl %d and %d overlap", tag, w, h,
                  c->id, L.ctl[j].id);
        }
    }
    CHECK(L.kind == pdu_kind(w, h), "%s %dx%d kind", tag, w, h);
    /* a shown panel can always be put away from its own controls (PD-04):
     * the first build never laid out the Pages panel's hide button */
    if (present(&L.side))
        CHECK(pdu_find(&L, C_SB_HIDE) >= 0, "%s %dx%d Pages panel has no hide button", tag, w, h);
    if (present(&L.notes))
        CHECK(pdu_find(&L, C_NP_CLOSE) >= 0, "%s %dx%d Notes panel has no close button", tag, w, h);
}

/* the content box os8088 gives a full-desktop PageDesk window on a display
 * (20.6.4); the grid sweep covers every other size. */
struct disp { const char *name; int sw, sh, cw, ch, kind; };

int main(void)
{
    /* content = the screen's width by its height less 64 (menu bar,
     * title, dock): MEASURED on QEMU for VGA (640x416), CGA (640x136) and
     * Hercules 348 (720x284) with Help > Document info, EGA and Hercules
     * 350 by that rule */
    static const struct disp D[] = {
        { "VGA 640x480",      640, 480, 640, 416, PDU_L_FULL },
        { "EGA 640x350",      640, 350, 640, 286, PDU_L_FULL },
        { "Hercules 720x348", 720, 348, 720, 284, PDU_L_FULL },
        { "Hercules 720x350", 720, 350, 720, 286, PDU_L_FULL },
        { "CGA 640x200",      640, 200, 640, 136, PDU_L_COMPACT },
    };
    int i, w, h, m;

    for (w = 320; w <= 720; w += 8)
        for (h = 100; h <= 460; h += 2)
            for (m = 0; m < 4; m++) {
                one(w, h, 1, 1, m, 0, "grid");
                one(w, h, 0, 0, m, 1, "grid-empty");
            }
    for (i = 0; i < 5; i++) {
        for (m = 0; m < 4; m++) {
            one(D[i].cw, D[i].ch, 1, 1, m, 0, D[i].name);
            one(D[i].cw, D[i].ch, 1, 1, m, 1, D[i].name);
            one(D[i].cw, D[i].ch, 0, 1, m, 0, D[i].name);
        }
        pdu_layout(&L, 8, 40, D[i].cw, D[i].ch, 1, 1, PDU_MODE_ANNOT, 0);
        CHECK(L.kind == D[i].kind, "%s: kind %d, expected %d", D[i].name, L.kind, D[i].kind);
        printf("%-17s content %dx%d: kind %s, canvas %dx%d, side %s, notes %s, %d controls\n",
               D[i].name, D[i].cw, D[i].ch,
               L.kind == PDU_L_FULL ? "FULL" : L.kind == PDU_L_MID ? "MID" : "COMPACT",
               L.canvas.x2 - L.canvas.x1 + 1, L.canvas.y2 - L.canvas.y1 + 1,
               present(&L.side) ? "shown" : "-", present(&L.notes) ? "shown" : "-", L.nctl);
        /* every mode's tools are on the glass in that mode at that display */
        for (m = 0; m < 4; m++) {
            const int *t;
            pdu_layout(&L, 8, 40, D[i].cw, D[i].ch, 1, 1, m, 0);
            for (t = pdu_tools(m, L.kind == PDU_L_COMPACT); *t; t++)
                CHECK(pdu_find(&L, *t) >= 0, "%s mode %d: tool %d not laid out", D[i].name, m, *t);
            CHECK(pdu_find(&L, C_M_READ + m) >= 0, "%s mode %d: its tab is missing", D[i].name, m);
        }
    }
    printf("pdu_lay: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
