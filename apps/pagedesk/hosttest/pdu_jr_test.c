/* os8088 - apps/pagedesk/hosttest/pdu_jr_test.c - the recovery journal's host
 * test (PAGEDESK-SPEC 14.7, 14.3, 20.17; PD-20).
 *
 * Compiles pduedit.c, pduundo.c, pdurecov.c and pdovlrecov.c - the SAME files
 * the package compiles - against a 1 MiB real-mode arena, an in-memory FAT
 * that refuses an append to a file whose size is not a whole number of
 * clusters (as OSAPI_FILE_APPEND/WRITE_SEQ do, 18.4.4) and the smallest
 * stand-in for the shell, then checks:
 *   - what is written is what a new session recovers: every kind, text,
 *     points, ids, flags - against SNAPSHOTS taken at each write
 *   - a seeded random walk of create / move / set / text / delete / undo /
 *     redo with a write after each burst, every written state recovered into
 *     a fresh store, at two cluster sizes
 *   - the debounce (a burst is one write; nothing before half a second)
 *   - a stale fingerprint is not offered, and nothing is applied
 *   - a corrupt or torn record stops replay at the last valid boundary
 *   - a read-only disk turns the journal off, said once, and the document
 *     goes on
 *   - the file stays bounded (a fresh file past 8 KiB) and still recovers
 *   - the interrupted save: offered with the target absent and the temp file
 *     there, silently dropped with the target present, renamed on Enter
 *   - /NM: the seed is a multiple of 16 below 0x4000 (128 records stay under
 *     0x8000) and a recovered record keeps its id while new ids continue
 *     above it
 *   - (verify) another document or New leaves the journal and a later open
 *     of the source offers it; the fingerprint is read once per document; a
 *     hostile pts_n stops replay; each REPLACE writes its own note; Esc
 *     during a job is the job's; the close question's S / D / Esc
 *
 *   cc -DPD_HOST -I apps/pagedesk -o build/pdu_jr apps/pagedesk/hosttest/pdu_jr_test.c \
 *      apps/pagedesk/pdmath.c apps/pagedesk/hosttest/pdhost.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef PD_HOST
#define PD_HOST
#endif
struct os88_rect { int x1, y1, x2, y2; };
struct os88_place { unsigned clus; int vol; };
struct os88_find { char name[13]; unsigned char attr; unsigned type; unsigned clus;
                   unsigned size_lo, size_hi, rsvd; };
#define OS88_FT_DIR 2
#define OS88_FERR_NOENT 4
#define OS88_FERR_BIG 10
#define PDU_RC_CMD() pdu_rc_cmd()
#define PDU_RC_SEED() ((os88_ticks() << 4) & 0x3FF0u)
#include "../pdu.h"
#include "../pdmath.h"

#define PDU_DC_EDITS 6
#define PDU_DC_UNDO 7
#define PDU_M_ENG 0
#define PDV_ENG_FINGERPRINT 0x10
#define PDW_ST_REPLACE 0x0200u
extern unsigned char pd_host_mem[];
extern pdw pd_host_ds;
#define os88_memcpy memcpy
#define os88_memset memset
#define os88_strcpy(d, s, n) do { strncpy((d), (s), (n)); (d)[(n) - 1] = 0; } while (0)
static void *pdu_win;
static struct pdu_lay pdu_L;
static struct pd_view pdu_view;
static unsigned pdu_page;
static int pdu_doc = D_OPEN;
static int pdu_changes, pdu_pop = P_NONE, pdu_about, pdu_rwant, pdu_job;
static int pdu_tool, pdu_mode, pdu_rdonly, pdu_gs, pdu_pgmap, pdu_pgsaved;
static char pdu_line[96], pdu_msg[72], pdu_num[8], pdu_name[14], pdu_target[14];
static struct os88_place pdu_tgtplace;
static struct { unsigned state; } pdu_xsave;
static unsigned pdu_xb = 0x7000, pdu_mcf, nfp;
static int pdu_savekind, nsaveas, nclose;
static void pdu_saveas_dlg(void) { nsaveas++; }
static void os88_wm_close(void *w) { nclose++; }
static unsigned pdu_clkw[4];
static unsigned now_ticks, nkick;
static int nsaid;
static char said[160];
static void pdu_cat(char *d, const char *s, unsigned cap) { strncat(d, s, cap - strlen(d) - 1); }
static void pdu_say(const char *s) { if (s != pdu_msg) strncpy(pdu_msg, s, sizeof(pdu_msg) - 1); pdu_msg[sizeof(pdu_msg) - 1] = 0; }
static void pdu_say2(const char *a, const char *b) { pdu_say(a); pdu_cat(pdu_msg, b, sizeof(pdu_msg)); }
static void pdu_wsay(const char *s) { pdu_say(s); strcpy(said, s); nsaid++; }
static void pdu_tell(const char *s) { strncpy(said, s, sizeof(said) - 1); }
static void pdu_kick(void) { nkick++; }
static void pdu_menusync(void) { }
static void pdu_ctl_repaint(int id) { }
static void pdu_title_set(void) { }
static void pdu_render_want(struct os88_rect *r) { }
static int pdu_isect(struct os88_rect *a, struct os88_rect *b, struct os88_rect *o)
{
    o->x1 = a->x1 > b->x1 ? a->x1 : b->x1;
    o->y1 = a->y1 > b->y1 ? a->y1 : b->y1;
    o->x2 = a->x2 < b->x2 ? a->x2 : b->x2;
    o->y2 = a->y2 < b->y2 ? a->y2 : b->y2;
    return o->x1 <= o->x2 && o->y1 <= o->y2;
}
static unsigned pdu_muldiv(unsigned a, unsigned b, unsigned c) { return (unsigned)((unsigned long long)a * b / c); }
static void os88_gfx_lock(void) {}
static void os88_gfx_unlock(void) {}
static void os88_gfx_xor_fill(int a, int b, int c, int d) { }
static int os88_wm_clip_set(void *w) { return 0; }
static unsigned pdu_dcseg(int row)
{
    return row == PDU_DC_EDITS ? 0x2000 : row == PDU_DC_UNDO ? 0x3000 : 0;
}
#define PDU_PAGES 1
void pdu_pg_apply(const void *d, unsigned n, int undo) { }
static unsigned os88_ticks(void) { return now_ticks; }
static int pdu_clk(unsigned *w) { w[0] = 2026; w[1] = 0x0B06; w[2] = 0x0C22; w[3] = 7; return 1; }

/* ---- the fingerprint the "engine" answers ------------------------------------ */
static unsigned char fpr[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
static unsigned pdu_mcall(int m, unsigned verb, unsigned a, unsigned b, unsigned c)
{
    memcpy(&pd_host_mem[pdu_xb * 16], fpr, 8);
    pdu_mcf = 0;
    nfp++;
    return 0;
}

/* ---- a FAT that is only as forgiving as the real one -------------------------- */
#define NF 16
static struct hf { int used; unsigned dir; char name[14]; unsigned char *d; unsigned n; } F[NF];
static struct hd { unsigned clus; const char *name; unsigned parent; } D[] = {
    { 10, "SYSTEM", 0 }, { 11, "APPDATA", 10 }, { 20, "DOC", 0 } };
static unsigned cur_clus = 20, cur_vol, ferr, cl_sect = 1, nwrites, fail_write;
static unsigned claim_next = 0x8000, nclaims;
static unsigned os88_ferr(void) { return ferr; }
static int os88_vol_sys(void) { return 0; }
static int os88_disk_cluster_sectors(void) { return (int)cl_sect; }
static void os88_file_here(struct os88_place *p) { p->clus = cur_clus; p->vol = 0; }
static int os88_file_goto_q_mark(unsigned clus, int vol)
{
    unsigned k;
    if (clus == 0) { cur_clus = 0; return 0; }
    for (k = 0; k < 3; k++)
        if (D[k].clus == clus) { cur_clus = clus; return 0; }
    return -1;
}
static int os88_file_find(int ord, struct os88_find *f)
{
    unsigned k, i = 0;
    for (k = 0; k < 3; k++)
        if (D[k].parent == cur_clus) {
            if ((int)i++ == ord) {
                memset(f, 0, sizeof(*f));
                strcpy(f->name, D[k].name); f->type = OS88_FT_DIR; f->clus = D[k].clus;
                return ord + 1;
            }
        }
    return -1;
}
static struct hf *fget(const char *name, int make)
{
    int k, e = -1;
    for (k = 0; k < NF; k++) {
        if (F[k].used && F[k].dir == cur_clus && strcmp(F[k].name, name) == 0) return &F[k];
        if (!F[k].used && e < 0) e = k;
    }
    if (!make || e < 0) return 0;
    F[e].used = 1; F[e].dir = cur_clus; strcpy(F[e].name, name);
    if (!F[e].d) F[e].d = malloc(65536);
    F[e].n = 0;
    return &F[e];
}
static unsigned os88_file_read(const char *name, void *buf, unsigned cap)
{
    struct hf *f = fget(name, 0);
    ferr = 0;
    if (!f) { ferr = OS88_FERR_NOENT; return 0; }
    if (f->n > cap) { ferr = OS88_FERR_BIG; return 0; }
    memcpy(buf, f->d, f->n);
    return f->n;
}
static unsigned os88_file_read_seg(const char *name, unsigned seg, unsigned cap)
{
    struct hf *f = fget(name, 0);
    unsigned n;
    ferr = 0;
    if (!f) { ferr = OS88_FERR_NOENT; return 0; }
    n = f->n < cap ? f->n : cap;
    memcpy(&pd_host_mem[seg * 16], f->d, n);
    return n;
}
static int os88_file_write(const char *name, const void *buf, unsigned count)
{
    struct hf *f;
    ferr = 0; nwrites++;
    if (fail_write) { ferr = 9; return -1; }
    f = fget(name, 1);
    if (!f) { ferr = 7; return -1; }
    memcpy(f->d, buf, count); f->n = count;
    return 0;
}
static int os88_file_write_seg(const char *name, unsigned seg, unsigned count)
{
    return os88_file_write(name, &pd_host_mem[seg * 16], count);
}
static int os88_file_write_seq(const char *name, unsigned seg, unsigned off, unsigned count,
                               unsigned flags, unsigned *token)
{
    struct hf *f = fget(name, 0);
    ferr = 0; nwrites++;
    if (fail_write) { ferr = 9; return -1; }
    if (!f || (f->n % (cl_sect * 512)) != 0) { ferr = 3; return -1; }   /* 18.4.4 */
    memcpy(f->d + f->n, &pd_host_mem[seg * 16 + off], count);
    f->n += count;
    return 0;
}
static int os88_file_delete(const char *name)
{
    struct hf *f = fget(name, 0);
    if (!f) { ferr = OS88_FERR_NOENT; return -1; }
    f->used = 0; ferr = 0;
    return 0;
}
static int os88_file_rename(const char *a, const char *b)
{
    struct hf *f = fget(a, 0), *g;
    if (!f) return -1;
    if (fget(b, 0)) return -1;
    strcpy(f->name, b);
    (void)g;
    return 0;
}
static unsigned csz[8], cseg[8];
static unsigned os88_mem_claim_p(unsigned paras)
{
    unsigned s = claim_next;
    cseg[nclaims] = s; csz[nclaims] = (paras + 0x1F) & ~0x1Fu;
    claim_next += csz[nclaims];
    nclaims++;
    return s;
}
static int os88_mem_free(unsigned seg)
{
    unsigned k;
    for (k = 0; k < nclaims; k++)
        if (cseg[k] == seg) {
            if (k == nclaims - 1) claim_next = seg;          /* a stack, which is all this test does */
            cseg[k] = cseg[nclaims - 1]; csz[k] = csz[nclaims - 1];
            nclaims--;
            return 0;
        }
    return -1;
}
static char *os88_utoa(unsigned v, char *dst) { sprintf(dst, "%u", v); return dst; }

static void ovl_rc_wake(void);
#include "../pduedit.c"
#include "../pduundo.c"
#include "../pdurecov.c"
#include "../pdovlrecov.c"

/* ---- the harness ------------------------------------------------------------- */
static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { if (fails++ < 30) { \
    printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static struct pd_edit E;
static pd_pt P[256];
static unsigned seed;
static unsigned rnd(unsigned n) { seed = seed * 1103515245u + 12345u; return (seed >> 8) % n; }

static void fresh(void)
{
    int k;
    memset(pd_host_mem, 0xA5, 0x110000);
    for (k = 0; k < NF; k++) F[k].used = 0;
    claim_next = 0x8000; nclaims = 0;
    cur_clus = 20; cl_sect = 1; fail_write = 0; nwrites = 0; nsaid = 0; said[0] = 0;
    now_ticks = 100;
    strcpy(pdu_name, "TEST.PDF");
    rc_f = RCF_JRN; rc_pk = 0; pdu_job = 0; pdu_savekind = 0;
    pdu_pgmap = 0; pdu_pgsaved = 0; pdu_doc = D_OPEN;
    pdu_ed_init();
    pdu_page = 0;
}
/* a new session on the same disk: empty store, the flags of a launch */
static void session(void)
{
    memset(&pd_host_mem[0x2000 * 16], 0xA5, 16384);
    memset(&pd_host_mem[0x3000 * 16], 0xA5, 4096);
    claim_next = 0x8000; nclaims = 0; cur_clus = 20;
    rc_f = RCF_JRN; rc_pk = 0; nsaid = 0; said[0] = 0; pdu_msg[0] = 0;
    pdu_ed_init();
}
#define TR(x) do { if (getenv("JRV")) printf("  %s:%d\n", x, __LINE__); } while (0)
static void wake(void) { TR("wake"); pdu_rc_wake(); TR("woke"); }
static void idle(void) { now_ticks += 12; wake(); wake(); }   /* half a second passes */

static int mk(int kind, int page, int x0, int y0, int x1, int y1)
{
    memset(&E, 0, sizeof(E));
    E.kind = kind; E.page = page;
    E.rect.x0 = x0; E.rect.y0 = y0; E.rect.x1 = x1; E.rect.y1 = y1;
    E.colour = 4;
    return 0;
}

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
struct sv { unsigned n; unsigned char *snap; unsigned fn; unsigned char *file; };
static struct sv V[600];
static int nv;
static void vfree(void)
{
    int i;
    for (i = 0; i < nv; i++) { free(V[i].snap); free(V[i].file); }
    nv = 0;
}
/* remember the store and the journal as they are now */
static void keep(void)
{
    struct hf *f;
    unsigned save = cur_clus;
    V[nv].n = snap_len();
    V[nv].snap = malloc(V[nv].n); memcpy(V[nv].snap, snap, V[nv].n);
    cur_clus = 11; f = fget("PAGEDESK.JRN", 0); cur_clus = save;
    V[nv].fn = f ? f->n : 0;
    V[nv].file = f ? malloc(f->n + 1) : 0;
    if (f) memcpy(V[nv].file, f->d, f->n);
    nv++;
}
static struct hf *jrn(void) { unsigned s = cur_clus; struct hf *f; cur_clus = 11; f = fget("PAGEDESK.JRN", 0); cur_clus = s; return f; }
static void put_jrn(const unsigned char *d, unsigned n)
{
    unsigned s = cur_clus; struct hf *f;
    cur_clus = 11; f = fget("PAGEDESK.JRN", 1); memcpy(f->d, d, n); f->n = n; cur_clus = s;
}

/* a launch + open: look, answer the prompt Enter, and say what happened */
static int recover(int yes)
{
    pdu_rc_begin();                     /* an open: the check waits for it */
    wake();
    if (!rc_pk) return 0;
    CHECK(pdu_rc_key(yes ? 13 : 27) == 1, "the prompt owns the keys");
    wake();
    return 1;
}
static int same_snap(int k)
{
    unsigned n = snap_len();
    return n == V[k].n && memcmp(snap, V[k].snap, n) == 0;
}

static void random_command(void)
{
    int k = rnd(9), n, i, t;
    struct pd_edit r;
    char tx[64];
    pdu_undo_begin(UL_NONE);
    n = pdu_ed_count();
    if (k < 3 || n == 0) {
        int kind = rnd(5);
        mk(kind == 0 ? PDA_NOTE : kind == 1 ? PDA_INK : kind == 2 ? PDA_RECT : kind == 3 ? PDA_TEXT : PDA_HIGHLIGHT,
           rnd(3), rnd(500), rnd(500), 600 + rnd(500), 600 + rnd(500));
        t = (kind == 0 || kind == 3) ? 1 + rnd(40) : 0;
        for (i = 0; i < t; i++) tx[i] = 'a' + rnd(26);
        for (i = 0; i < 24; i++) P[i] = (pd_pt)(rnd(900) - 100);
        pdu_ed_new(&E, tx, t, P, kind == 1 ? 3 + rnd(10) : kind == 4 ? 8 : 0);
    } else {
        i = rnd(n);
        switch (k) {
        case 3: case 4: pdu_ed_move(i, (int)rnd(200) - 100, (int)rnd(200) - 100); break;
        case 5: pdu_ed_get(i, &r); r.colour = rnd(16); r.rect.x1 += rnd(50); pdu_ed_set(i, &r); break;
        case 6:
            t = 1 + rnd(30);
            for (n = 0; n < t; n++) tx[n] = 'A' + rnd(26);
            pdu_ed_text(i, tx, t);
            break;
        case 7: pdu_ed_del(i); break;
        default: for (n = 0; n < 24; n++) P[n] = (pd_pt)rnd(700); pdu_ed_pts(i, P, 2 + rnd(6)); break;
        }
    }
    pdu_undo_end();
    pdu_ed_err = 0;
}

static void test_basic(void)
{
    int a, b, id1;
    struct pd_edit r;
    unsigned w0;

    fresh();
    wake();                                     /* the launch: nothing to find */
    CHECK(!rc_pk && nwrites == 0, "no journal, no prompt, no write at launch");
    mk(PDA_NOTE, 0, 100, 100, 260, 244);
    a = pdu_ed_new(&E, "Check the date", 14, (pd_pt *)0, 0);
    for (b = 0; b < 12; b++) P[b] = (pd_pt)(50 + b * 9);
    mk(PDA_HIGHLIGHT, 0, 50, 50, 160, 90);
    pdu_ed_new(&E, "", 0, P, 8);
    wake();
    CHECK(nwrites == 0, "nothing is written before half a second has passed");
    now_ticks += 4; wake();
    CHECK(nwrites == 0, "...nor at 4 ticks");
    idle();
    CHECK(nwrites == 1 && jrn(), "one write (%u writes, %u bytes)", nwrites, jrn() ? jrn()->n : 0);
    w0 = nwrites;
    pdu_ed_get(a, &r);
    pdu_ed_move(a, 40, 40);
    pdu_ed_get(a, &r);
    for (b = 0; b < 5; b++) { pdu_ed_text(a, "x", 1); now_ticks += 2; wake(); }
    CHECK(nwrites == w0, "a burst of commands is still one write, not five");
    idle();
    CHECK(nwrites == w0 + 1, "...and it is written once, after the quiet (%u)", nwrites - w0);
    wake(); idle();
    CHECK(nwrites == w0 + 1, "nothing changed: nothing written");
    keep();
    id1 = r.id;
    session();
    CHECK(recover(1) == 1, "the journal is offered");
    CHECK(strstr(said, "Recovered") != 0 || strstr(pdu_msg, "Recovered") != 0, "...and recovery says so (%s)", pdu_msg);
    CHECK(same_snap(0), "the recovered store is the written one");
    CHECK(pdu_ed_count() == 2, "two records");
    pdu_ed_get(0, &r);
    CHECK(r.id == id1 && pdu_dirty(), "the id is kept and the document is dirty");
    CHECK(pdu_undo() == 0 && pdu_ed_count() == 0, "recovery is one undo step");
    pdu_redo();
    mk(PDA_RECT, 0, 1, 1, 9, 9);
    b = pdu_ed_new(&E, "", 0, (pd_pt *)0, 0);
    pdu_ed_get(b, &r);
    CHECK(r.id > id1 && r.id > 2, "a new record's id continues above the recovered ones (%u)", r.id);
    /* the session continues the journal: its next write is a fresh file with all three */
    idle();
    keep();
    session();
    CHECK(recover(1) && same_snap(1), "recovery of a recovered session's journal");
    /* Esc discards */
    session();
    CHECK(recover(0) == 1, "offered again");
    CHECK(pdu_ed_count() == 0 && !jrn(), "Esc: nothing applied and the journal is gone");
    vfree();
}

static void test_stale(void)
{
    fresh();
    mk(PDA_RECT, 0, 1, 1, 90, 90);
    pdu_ed_new(&E, "", 0, (pd_pt *)0, 0);
    idle();
    CHECK(jrn() != 0, "written");
    session();
    fpr[3] ^= 1;                                /* the source changed */
    CHECK(recover(1) == 0, "a different fingerprint is not offered");
    CHECK(pdu_ed_count() == 0 && jrn() != 0, "...nothing is applied and the file is left alone");
    fpr[3] ^= 1;
    session();
    strcpy(pdu_name, "OTHER.PDF");
    CHECK(recover(1) == 1, "a copy under another name with the same bytes is the same source");
    strcpy(pdu_name, "TEST.PDF");
}

static void test_corrupt(void)
{
    struct hf *f;
    unsigned at, k;
    int i;
    unsigned char *img, *orig;
    unsigned fn;
    struct pd_edit r;

    fresh();
    for (i = 0; i < 4; i++) {
        mk(PDA_RECT, 0, i * 10, 1, 90 + i, 90);
        pdu_ed_new(&E, "", 0, (pd_pt *)0, 0);
    }
    idle();
    f = jrn();
    CHECK(f && f->n == 36 + 4 * (2 + 48 + 2), "a header and four records (%u)", f ? f->n : 0);
    img = malloc(f->n);
    memcpy(img, f->d, f->n);
    orig = malloc(f->n);
    memcpy(orig, f->d, f->n);
    fn = f->n;
    img[36 + 2 * 52 + 7] ^= 0x40;               /* a bad byte in the third record */
    put_jrn(img, f->n);
    session();
    CHECK(recover(1) == 1 && pdu_ed_count() == 2, "a corrupt record stops replay at the last good one (%d)", pdu_ed_count());
    pdu_ed_get(1, &r);
    CHECK(r.rect.x0 == 10, "...and the records before it are whole");
    put_jrn(orig, fn - 30);                    /* a torn tail */
    session();
    CHECK(recover(1) == 1, "a torn file is still offered");
    at = pdu_ed_count();
    CHECK(at == 3, "...and recovers what is whole (%u)", at);
    session();
    memcpy(img, orig, fn);
    memset(img, 0, 36);                         /* a bad header */
    put_jrn(img, fn);
    CHECK(recover(1) == 0, "a bad header is not offered");
    (void)k;
    free(img);
}

static void test_readonly(void)
{
    unsigned w;
    fresh();
    fail_write = 1;
    mk(PDA_RECT, 0, 1, 1, 90, 90);
    pdu_ed_new(&E, "", 0, (pd_pt *)0, 0);
    idle();
    CHECK((rc_f & RCF_OFF) && strstr(said, "Recovery off"), "a refused write turns the journal off, said (%s)", said);
    w = nwrites;
    mk(PDA_RECT, 0, 2, 2, 90, 90);
    pdu_ed_new(&E, "", 0, (pd_pt *)0, 0);
    idle();
    CHECK(nwrites == w && nsaid == 1, "...and it is not tried or said again");
    CHECK(pdu_ed_count() == 2, "the document is untouched");
}

static void test_nodir(void)
{
    fresh();
    D[1].parent = 99;                           /* no SYSTEM/APPDATA */
    mk(PDA_RECT, 0, 1, 1, 90, 90);
    pdu_ed_new(&E, "", 0, (pd_pt *)0, 0);
    idle();
    CHECK((rc_f & RCF_OFF) && nwrites == 0, "no APPDATA: off, no write attempted");
    D[1].parent = 10;
}

static void test_walk(unsigned s, unsigned cs)
{
    int i, bad = 0, k;
    seed = s;
    fresh();
    cl_sect = cs;
    vfree();
    for (i = 0; i < 90; i++) {
        for (k = 1 + rnd(3); k > 0; k--) random_command();
        if (rnd(5) == 0) { if (pdu_undo() < 0) pdu_redo(); }
        if (rnd(6) == 0) pdu_redo();
        idle();
        if (nv < 590 && !(rc_f & RCF_OFF)) keep();
        if (rc_f & RCF_OFF) break;
    }
    /* the last write recovers to the state it was written in */
    for (k = nv - 1; k >= 0 && k >= nv - 6; k--) {
        session();
        cl_sect = cs;
        put_jrn(V[k].file, V[k].fn);
        if (!recover(1)) { bad++; printf("seed %u write %d: not offered\n", s, k); continue; }
        if (!same_snap(k)) { bad++; printf("seed %u write %d: recovered state differs (%u records)\n", s, k, pdu_ed_count()); }
    }
    CHECK(bad == 0, "seed %u cluster %u: %d bad recoveries", s, cs * 512, bad);
    vfree();
}

static void test_grow(void)
{
    int i, n;
    char tx[200];
    struct pd_edit r;
    fresh();
    memset(tx, 'q', sizeof(tx));
    mk(PDA_TEXT, 0, 1, 1, 600, 90);
    pdu_ed_new(&E, "", 0, (pd_pt *)0, 0);
    for (i = 0; i < 80; i++) {
        pdu_ed_text(0, tx, 100 + (i % 7) * 15);
        idle();
        CHECK(jrn() && jrn()->n <= 8192, "write %d: bounded (%u)", i, jrn() ? jrn()->n : 0);
    }
    pdu_ed_get(0, &r);
    n = r.text_len;
    session();
    CHECK(recover(1), "after many rewrites: offered");
    pdu_ed_get(0, &r);
    CHECK(pdu_ed_count() == 1 && (int)r.text_len == n, "...the last text that fit (%u)", r.text_len);
    /* more than one write stages: off, said, the document unharmed */
    fresh();
    for (i = 0; i < 38; i++) {
        mk(PDA_NOTE, 0, 1, 1, 90, 90);
        pdu_ed_new(&E, tx, 200, (pd_pt *)0, 0);
    }
    idle();
    CHECK((rc_f & RCF_OFF) && pdu_ed_count() == 38, "a store too big for one write: off, said");
}

static void test_pages(void)
{
    struct pd_edit r;
    fresh();
    mk(PDA_RECT, 0, 1, 1, 90, 90);          pdu_ed_new(&E, "", 0, (pd_pt *)0, 0);
    mk(PDA_RECT, 0x8001, 1, 1, 90, 90);     pdu_ed_new(&E, "", 0, (pd_pt *)0, 0);
    mk(PDA_RECT, 0, 3, 3, 90, 90);          pdu_ed_new(&E, "", 0, (pd_pt *)0, 0);
    pdu_pgmap = 1;
    idle();
    session();
    CHECK(recover(1), "offered");
    CHECK(pdu_ed_count() == 3, "the indices are kept (a hole for the new page's record: %d)", pdu_ed_count());
    pdu_ed_get(1, &r);
    CHECK((r.flags & PDEF_DELETED) && r.page != 0x8001, "...the hole is deleted and on no page");
}

static void test_fin(void)
{
    struct hf *f;
    unsigned s;
    fresh();
    strcpy(pdu_target, "OUT.PDF");
    pdu_tgtplace.clus = 20; pdu_tgtplace.vol = 0;
    pdu_xsave.state = 0;
    pdu_rc_fin();
    CHECK(nwrites == 0, "not before the writer is told to replace");
    pdu_xsave.state = PDW_ST_REPLACE;
    pdu_rc_fin();
    CHECK(nwrites == 1, "the note is written");
    pdu_rc_fin();
    CHECK(nwrites == 1, "...once");
    /* the save died between the delete and the rename */
    cur_clus = 20; f = fget("PDSAVE.$$$", 1); memcpy(f->d, "%PDF-1.5 ...", 12); f->n = 12;
    session();
    CHECK(recover(1) == 1, "the interrupted save is offered at launch");
    cur_clus = 20;
    CHECK(fget("OUT.PDF", 0) != 0 && fget("PDSAVE.$$$", 0) == 0, "Enter renames the temp file");
    cur_clus = 11;
    CHECK(fget("PDSAVE.TGT", 0) == 0, "...and the note is gone");
    /* the target never went: the note is dropped without a word */
    fresh();
    strcpy(pdu_target, "OUT.PDF");
    pdu_tgtplace.clus = 20;
    pdu_xsave.state = PDW_ST_REPLACE;
    pdu_rc_fin();
    cur_clus = 20; f = fget("PDSAVE.$$$", 1); f->n = 12; f = fget("OUT.PDF", 1); f->n = 5;
    session();
    CHECK(recover(1) == 0, "target present: nothing offered");
    cur_clus = 11;
    CHECK(fget("PDSAVE.TGT", 0) == 0, "...the stale note is deleted");
    /* Esc keeps the temp file and drops the note */
    fresh();
    strcpy(pdu_target, "OUT.PDF");
    pdu_tgtplace.clus = 20;
    pdu_xsave.state = PDW_ST_REPLACE;
    pdu_rc_fin();
    cur_clus = 20; f = fget("PDSAVE.$$$", 1); f->n = 12;
    session();
    CHECK(recover(0) == 1, "offered");
    cur_clus = 20; s = fget("PDSAVE.$$$", 0) != 0 && fget("OUT.PDF", 0) == 0;
    CHECK(s, "Esc leaves the temp file where it is");
    cur_clus = 11;
    CHECK(fget("PDSAVE.TGT", 0) == 0, "...and does not ask again");
}

static void test_retire(void)
{
    fresh();
    mk(PDA_RECT, 0, 1, 1, 90, 90);
    pdu_ed_new(&E, "", 0, (pd_pt *)0, 0);
    idle();
    CHECK(jrn() != 0, "written");
    pdu_pgsaved = 1;                            /* a save finished; the output is being opened */
    pdu_rc_begin();
    wake();
    CHECK(jrn() == 0, "a completed Save retires the journal");
    fresh();
    mk(PDA_RECT, 0, 1, 1, 90, 90);
    pdu_ed_new(&E, "", 0, (pd_pt *)0, 0);
    idle();
    pdu_pgsaved = 0;                            /* another document is opened without a save */
    pdu_rc_begin();
    wake();
    CHECK(jrn() != 0, "opening another document leaves the journal for the offer");
}

static void test_seed(void)
{
    unsigned a, b;
    unsigned t;
    now_ticks = 1000; a = PDU_RC_SEED();
    now_ticks = 1337; b = PDU_RC_SEED();
    CHECK((a & 15) == 0 && a < 0x4000u && (b & 15) == 0 && b < 0x4000u && a != b,
          "the /NM seed: a multiple of 16, below 0x4000, not the same twice (%u %u)", a, b);
    for (t = 0; t < 2048; t++) {
        now_ticks = t;
        if (PDU_RC_SEED() + 128 >= 0x8000u) break;
    }
    CHECK(t == 2048, "128 records from any seed stay below PDWRT's 0x8000 + page ids");
}

/* ---- verify-recov's additions ------------------------------------------------ */
static void test_other(void)
{
    struct hf *f;
    unsigned w;
    int i;
    /* another document: the journal is kept, and opening the source again in
     * the same session offers it */
    fresh();
    mk(PDA_RECT, 0, 1, 1, 90, 90);
    pdu_ed_new(&E, "", 0, (pd_pt *)0, 0);
    idle();
    keep();
    pdu_pgsaved = 0;
    fpr[0] ^= 1;                                /* another document */
    pdu_rc_begin(); pdu_ed_init(); wake();
    CHECK(jrn() != 0 && !rc_pk, "another document: kept, not offered for it");
    fpr[0] ^= 1;                                /* the source again */
    pdu_rc_begin(); pdu_ed_init();
    CHECK(recover(1) == 1 && pdu_ed_count() == 1 && same_snap(0),
          "...and reopening the source offers it (%d)", pdu_ed_count());
    vfree();
    /* New (a save of kind 1 that reopens with pdu_pgsaved) is not a Save As */
    fresh();
    mk(PDA_RECT, 0, 1, 1, 90, 90);
    pdu_ed_new(&E, "", 0, (pd_pt *)0, 0);
    idle();
    pdu_pgsaved = 1; pdu_savekind = 1;
    pdu_rc_begin(); wake();
    CHECK(jrn() != 0, "File > New does not retire the last document's journal");
    pdu_savekind = 0; pdu_pgsaved = 0;
    /* the fingerprint is read once per document, not per write */
    fresh();
    nfp = 0;
    for (i = 0; i < 4; i++) {
        mk(PDA_RECT, 0, i, 1, 90, 90);
        pdu_ed_new(&E, "", 0, (pd_pt *)0, 0);
        idle();
    }
    CHECK(nfp == 1, "four writes, one fingerprint (%u)", nfp);
    /* a CRC-valid record whose pts_n wraps np << 2 on the 8086 (here, with
     * 32-bit ints, the length test alone refuses it; np > ED_PMAX is the
     * guard the target relies on): replay stops, nothing written */
    fresh();
    mk(PDA_INK, 0, 1, 1, 90, 90);
    for (i = 0; i < 4; i++) P[i] = (pd_pt)i;
    pdu_ed_new(&E, "", 0, P, 1);
    idle();
    f = jrn();
    {
        unsigned char *d = f->d + 36;
        unsigned np = 0xFFFF, l, c;
        d[2 + 46] = (unsigned char)np; d[2 + 47] = (unsigned char)(np >> 8);   /* pts_n at +46 */
        d[2 + 34] = 4; d[2 + 35] = 0;           /* text_len 4 */
        l = (pdw)(48 + 4 + (np << 2));          /* = 48 on the 8086: 16-bit wrap */
        d[0] = (unsigned char)l; d[1] = (unsigned char)(l >> 8);
        c = pdm_crc16(0xFFFF, d + 2, l);
        d[2 + l] = (unsigned char)c; d[3 + l] = (unsigned char)(c >> 8);
        f->n = 36 + 4 + l;
    }
    session();
    w = recover(1);
    CHECK(pdu_ed_count() == 0, "a wrapping pts_n is refused (offered %u, %d records)", w, pdu_ed_count());
    /* each REPLACE writes its own note: a failed save leaves FIN set */
    fresh();
    strcpy(pdu_target, "ONE.PDF"); pdu_tgtplace.clus = 20;
    pdu_xsave.state = PDW_ST_REPLACE; pdu_rc_fin();
    pdu_xsave.state = 0; pdu_rc_fin();          /* the next save's first slice */
    strcpy(pdu_target, "TWO.PDF");
    pdu_xsave.state = PDW_ST_REPLACE; pdu_rc_fin();
    cur_clus = 11; f = fget("PDSAVE.TGT", 0); cur_clus = 20;
    CHECK(f && strcmp((char *)f->d + 4, "TWO.PDF") == 0, "the second save's note names its own target");
    /* Esc during a job is the job's, not the prompt's */
    fresh();
    rc_pk = 1; pdu_job = 1;
    CHECK(pdu_rc_key(27) == 0 && !(rc_f & RCF_ANS), "Esc while a job runs: not the prompt's");
    pdu_job = 0; rc_pk = 0;
}

static void test_close(void)
{
    fresh();
    CHECK(pdu_rc_close() == 1, "a clean document closes");
    mk(PDA_RECT, 0, 1, 1, 90, 90);
    pdu_ed_new(&E, "", 0, (pd_pt *)0, 0);
    idle();
    CHECK(jrn() != 0, "written");
    CHECK(pdu_rc_close() == 0 && rc_pk == 3, "unsaved: the close is refused and asked");
    CHECK(pdu_rc_key('x') == 1 && rc_pk == 3, "...other keys are the question's");
    CHECK(pdu_rc_key(27) == 1 && rc_pk == 0 && nclose == 0, "Esc: stays open");
    pdu_rc_close();
    nsaveas = 0;
    CHECK(pdu_rc_key('S') == 1 && nsaveas == 1 && rc_pk == 0, "S: Save As");
    pdu_rc_close();
    nclose = 0;
    pdu_rc_key('d');
    wake();
    CHECK(nclose == 1 && jrn() == 0, "D: the window closes and the journal goes");
    wake();
    CHECK(nclose == 1, "...once");
    pdu_doc = D_EMPTY;
    rc_f = RCF_JRN;
    CHECK(pdu_rc_close() == 1, "no document: closes");
}

int main(void)
{
    unsigned s;
    setvbuf(stdout, 0, _IONBF, 0);
    pd_host_ds = 0x1000;
    pdu_view.box.x0 = 0; pdu_view.box.y0 = 0; pdu_view.box.x1 = 9792; pdu_view.box.y1 = 3200;
    pdu_view.rot = 0; pdu_view.zoom = 100; pdu_view.asp_num = 1; pdu_view.asp_den = 1;
    pdu_L.canvas.x1 = 0; pdu_L.canvas.y1 = 0; pdu_L.canvas.x2 = 639; pdu_L.canvas.y2 = 479;
    if (!getenv("JRONLY") || !strcmp(getenv("JRONLY"), "basic")) test_basic();
    if (!getenv("JRONLY") || !strcmp(getenv("JRONLY"), "stale")) test_stale();
    if (!getenv("JRONLY") || !strcmp(getenv("JRONLY"), "corrupt")) test_corrupt();
    if (!getenv("JRONLY") || !strcmp(getenv("JRONLY"), "readonly")) test_readonly();
    if (!getenv("JRONLY") || !strcmp(getenv("JRONLY"), "nodir")) test_nodir();
    if (!getenv("JRONLY") || !strcmp(getenv("JRONLY"), "grow")) test_grow();
    if (!getenv("JRONLY") || !strcmp(getenv("JRONLY"), "pages")) test_pages();
    if (!getenv("JRONLY") || !strcmp(getenv("JRONLY"), "fin")) test_fin();
    if (!getenv("JRONLY") || !strcmp(getenv("JRONLY"), "retire")) test_retire();
    if (!getenv("JRONLY") || !strcmp(getenv("JRONLY"), "seed")) test_seed();
    if (!getenv("JRONLY") || !strcmp(getenv("JRONLY"), "other")) test_other();
    if (!getenv("JRONLY") || !strcmp(getenv("JRONLY"), "close")) test_close();
    for (s = 1; s <= 24 && !getenv("JRONLY"); s++)
        test_walk(s, s & 1 ? 1 : 2);
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
