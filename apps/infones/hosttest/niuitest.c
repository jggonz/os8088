/* ============================================================================
 * os8088 - apps/infones/hosttest/niuitest.c      INFONES's host harness
 *
 * Part of INFONES (SPEC.md 91.14.4). Run by apps/infones/build.sh BEFORE
 * anything is built for the 8086, and it STOPS THE BUILD.
 *
 * WHAT IT IS. The whole program - apps/infones/infones.c and every part it
 * #includes - compiled with the HOST's cc against a stub os88.h placed ahead
 * of apps/cc on the include path, with a MODEL OF THE GLASS underneath it. It
 * drives the program the way a user does: launch, look, open a ROM, look
 * again, refuse two bad ones, open the About panel, close it. After every
 * step it asserts, FIELD FOR FIELD, that the glass shows what the shadow says
 * it shows - and it prints the cost table in calls, cells and milliseconds.
 *
 * WHY IT EXISTS, and it is not "unit tests". Three defects on this OS are
 * INVISIBLE IN AN EMULATOR (PERFORMANCE.md): a visible redraw, a double-draw
 * flash, and input overrun. None of them shows in a screendump. What shows
 * them is a cost table and a shadow audit, and cword's harness found three
 * real defects that no screendump would have (LESSONS.md 7).
 *
 * TWO TRAPS TAKEN FROM LESSONS.md 7, both live here:
 *   - A STUB THAT ALWAYS REFUSES MEASURES THE FALLBACK PATH. Every stub below
 *     models what the machine does, and os88_font_run in particular WRITES
 *     INTO THE GLASS rather than counting a call and returning.
 *   - A NEW ASSEMBLY SHIM IS A NEW HOST STUB, IN THE SAME EDIT. Every cdecl
 *     entry point of nicpu.inc, nimem.inc, niband.inc and nifsx.inc has a
 *     definition below; one added to the package without one here fails to
 *     LINK, three steps later than it should.
 * ==========================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "os88.h"

/* ==========================================================================
 * THE MACHINE UNDER THE PROGRAM
 * ========================================================================*/

/* One megabyte of "real mode", so that a claim's segment is an index into it
 * exactly as it is on the machine. 0x2000 paragraphs in is where the heap
 * starts here, which is above the kernel's own footprint the way the real one
 * is (docs/KERNEL-MEMORY.md) - and it matters, because nicpu.inc's SRAM bias
 * subtracts 0x600 paragraphs and a claim below that would underflow. */
#define POOL_BYTES (1024u * 1024u)
#define HEAP_PARA  0x2000
static unsigned char pool[POOL_BYTES];
static unsigned heap_para = HEAP_PARA;

#define MAXCLAIM 16
static struct { unsigned seg, para; int live; } claims[MAXCLAIM];
static unsigned largest_kb = 400;   /* what the heap will admit to */

static int fail_count;
static int step_no;
static const char *step_name = "(start)";

static void FAIL(const char *why)
{
    fail_count++;
    fprintf(stderr, "niuitest: FAIL at step %d (%s): %s\n",
            step_no, step_name, why);
}

#define CHECK(cond, msg) do { if (!(cond)) FAIL(msg); } while (0)

unsigned os88_mem_claim(int kb)
{
    int i;

    if ((unsigned)kb > largest_kb)
        return 0;
    for (i = 0; i < MAXCLAIM; i++) {
        if (!claims[i].live) {
            claims[i].seg = heap_para;
            claims[i].para = (unsigned)kb * 64;
            claims[i].live = 1;
            heap_para += claims[i].para;
            if ((unsigned)heap_para * 16u > POOL_BYTES) {
                fprintf(stderr, "niuitest: the model heap is exhausted\n");
                exit(2);
            }
            memset(pool + (size_t)claims[i].seg * 16, 0,
                   (size_t)claims[i].para * 16);
            return claims[i].seg;
        }
    }
    return 0;
}

int os88_mem_free(unsigned seg)
{
    int i;

    for (i = 0; i < MAXCLAIM; i++)
        if (claims[i].live && claims[i].seg == seg) {
            claims[i].live = 0;
            return 0;
        }
    FAIL("os88_mem_free of a segment that was never claimed");
    return -1;
}

unsigned os88_mem_largest_kb(void) { return largest_kb; }

int os88_peek(unsigned seg, unsigned off)
{
    return pool[((size_t)seg * 16 + off) & (POOL_BYTES - 1)];
}

void os88_poke(unsigned seg, unsigned off, int value)
{
    pool[((size_t)seg * 16 + off) & (POOL_BYTES - 1)] = (unsigned char)value;
}

/* ==========================================================================
 * THE GLASS
 *
 * A cell grid keyed by the pixel row a cell's TOP is on, which is what the
 * panel's ten-pixel pitch produces - so the model can answer "what does row
 * y read?" the way a person reading the screen can, and a field drawn at the
 * wrong y is a row that reads empty rather than a test that quietly passes.
 * ========================================================================*/
#define GW 100
#define GH 500
static char glass[GH][GW + 1];

static int n_font_run, n_cells, n_fill, n_frame, n_calls;

static void glass_clear(void)
{
    int y, x;

    for (y = 0; y < GH; y++) {
        for (x = 0; x < GW; x++)
            glass[y][x] = ' ';
        glass[y][GW] = 0;
    }
}

void os88_font_run(int x, int y, const char *s, int ink, int paper)
{
    int col, i;

    (void)ink;
    (void)paper;
    n_font_run++;
    n_calls++;
    if (y < 0 || y >= GH)
        return;
    col = x / 8;
    for (i = 0; s[i]; i++) {
        n_cells++;
        if (col + i >= 0 && col + i < GW)
            glass[y][col + i] = s[i];
    }
}

static int fill_x1 = -1, fill_y1 = -1, fill_x2 = -1, fill_y2 = -1;

void os88_gfx_fill(int x1, int y1, int x2, int y2)
{
    int y, c;

    n_fill++;
    n_calls++;
    fill_x1 = x1; fill_y1 = y1; fill_x2 = x2; fill_y2 = y2;
    for (y = y1; y <= y2 && y < GH; y++)
        if (y >= 0)
            for (c = x1 / 8; c <= x2 / 8 && c < GW; c++)
                if (c >= 0)
                    glass[y][c] = ' ';
}

void os88_gfx_frame(int x1, int y1, int x2, int y2)
{
    (void)x1; (void)y1; (void)x2; (void)y2;
    n_frame++;
    n_calls++;
}

void os88_set_color(int colour) { (void)colour; n_calls++; }

static int lock_held;
void os88_gfx_lock(void)   { lock_held++; }
void os88_gfx_unlock(void)
{
    if (lock_held <= 0)
        FAIL("os88_gfx_unlock without a matching lock");
    lock_held--;
}

/* ==========================================================================
 * THE WINDOW, THE MENUS AND THE REST OF THE KERNEL
 * ========================================================================*/
static int win_w = 334, win_h = 158;
static int win_ox = 8, win_oy = 40;
static int wake_pending;
static int toast_count;
static char last_toast[128];
static struct os88_menuset *reg_menus;
static int fdlg_open;

void *os88_wm_create(int x, int y, int w, int h, const char *title)
{
    (void)x; (void)y; (void)title;
    win_w = w;
    win_h = h;
    return (void *)0x1234;
}

void os88_wm_content(void *win, struct os88_pt *o)
{
    (void)win;
    o->x = win_ox;
    o->y = win_oy;
}

int os88_wm_geom(void *win, struct os88_size *s)
{
    (void)win;
    s->w = win_w - 2;
    s->h = win_h - OS88_TITLE_H - 1;    /* the content is W_H - TITLE_H - 1
                                         * and NOT W_H - TITLE_H: RUNCPM's
                                         * window showed 24 rows and a sliver
                                         * for exactly this reason
                                         * (LESSONS.md 13) */
    return 0;
}

void os88_wm_snap(void *win, int on) { (void)win; (void)on; }
void os88_wm_minsize(void *win, int w, int h) { (void)win; (void)w; (void)h; }

/* WF_OWNBG AND THE DAMAGE RECT (SPEC.md 11.90.1/11.90.2), modelled rather
 * than stubbed: `ownbg` records whether the kernel would still be whitening
 * the content, and os88_wm_damage refuses to answer anything but "whole"
 * without it - which is the kernel's own interlock, and a harness that let a
 * partial answer through with the flag clear would be testing an ABI the
 * machine does not have. */
static int win_ownbg;
static int dmg_partial;             /* the test asks for a partial expose */
static struct os88_rect dmg_rect;

void os88_wm_ownbg(void *win, int on) { (void)win; win_ownbg = on; }

int os88_wm_damage(void *win, struct os88_rect *r)
{
    (void)win;
    if (!win_ownbg || !dmg_partial) {
        r->x1 = win_ox;
        r->y1 = win_oy;
        r->x2 = win_ox + (win_w - 2) - 1;
        r->y2 = win_oy + (win_h - OS88_TITLE_H - 1) - 1;
        return 1;
    }
    *r = dmg_rect;
    return 0;
}
void os88_wm_close(void *win) { (void)win; }
void os88_wm_onwake(void *win) { (void)win; }
int  os88_wm_wake(void *win) { (void)win; wake_pending = 1; return 0; }
void os88_menu_set(void *win, struct os88_menuset *set)
{
    (void)win;
    reg_menus = set;
}
void os88_about_set(void *win) { (void)win; }

/* THE STRIP IS 24 CHARACTERS AND IT TRUNCATES IN SILENCE (kernel/toast.inc's
 * TOAST_MAX; `toast_stage` copies CX = TOAST_MAX bytes with no ellipsis and no
 * error). This package shipped a 37-character launch refusal that read
 * `INFONES wanted 13 KB, la` on the glass - the whole fact cut off, on the one
 * refusal with no state line behind it - so the model REFUSES a long one here
 * rather than modelling the truncation: build.sh greps the literals, and this
 * catches the composed ones the grep cannot see (the heap refusal, the two
 * `Header claims` sentences and the mapper's). */
#define NI_TOAST_MAX 24

int os88_toast(const char *text, int ticks)
{
    (void)ticks;
    toast_count++;
    snprintf(last_toast, sizeof(last_toast), "%s", text);
    if ((int)strlen(text) > NI_TOAST_MAX) {
        fail_count++;
        fprintf(stderr, "niuitest: FAIL at step %d (%s): a toast of %d "
                "characters, over TOAST_MAX's %d - the strip cuts it dead\n"
                "            [%s]\n",
                step_no, step_name, (int)strlen(text), NI_TOAST_MAX, text);
    }
    return 0;
}

int os88_cpu(void) { return OS88_CPU_8086; }

void os88_video(struct os88_video *v)
{
    v->w = 640;
    v->h = 480;
    v->dock_top = 448;
    v->kind = OS88_VID_VGA;
    v->bpp = 4;
}

static unsigned fake_ticks = 100;
unsigned os88_ticks(void) { return fake_ticks; }
int os88_key_down(int scan) { (void)scan; return 0; }

int os88_file_dlg(int mode, void *win, const char *defname)
{
    (void)mode; (void)win; (void)defname;
    fdlg_open = 1;
    return 0;
}

int os88_assoc_set(const char *ext, const char *stem)
{
    (void)ext; (void)stem;
    return 0;
}

/* --- the launch document (SPEC.md 54.10) -------------------------------- */
static const char *arg_name;        /* what a double-click named, or 0 */
static int arg_goto_fails;

int os88_arg_file(char *name13, struct os88_place *p)
{
    if (!arg_name)
        return -1;                  /* the ordinary case: launched empty */
    os88_strcpy(name13, arg_name, 13);
    p->clus = 7;
    p->vol = 1;
    return 0;
}

int os88_file_goto(struct os88_place *p)
{
    (void)p;
    return arg_goto_fails ? -1 : 0;
}

/* --- the file the dialog is pretending to have chosen -------------------- */
static unsigned char *filebuf;
static unsigned filelen;

unsigned os88_file_read_seg(const char *name, unsigned seg, unsigned cap)
{
    unsigned n;

    (void)name;
    n = (cap < filelen) ? cap : filelen;
    memcpy(pool + (size_t)seg * 16, filebuf, n);
    return n;
}

/* --- the runtime's own helpers ------------------------------------------- */
void os88_memset(void *p, int c, unsigned n) { memset(p, c, n); }
void os88_memcpy(void *d, const void *s, unsigned n) { memcpy(d, s, n); }
unsigned os88_strlen(const char *s) { return (unsigned)strlen(s); }

void os88_strcpy(char *dst, const char *src, unsigned cap)
{
    unsigned i = 0;

    if (cap == 0)
        return;
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

char *os88_utoa(unsigned v, char *dst6)
{
    sprintf(dst6, "%u", v);
    return dst6;
}

/* ==========================================================================
 * THE PROGRAM ITSELF
 * ========================================================================*/
#include "infones.c"

/* ==========================================================================
 * THE HAND-WRITTEN HALF, MODELLED
 *
 * Every cdecl entry point of the four .inc files. THE MOVERS ARE MODELLED
 * FAITHFULLY, because the loader's correctness depends on them; the CORE is
 * a stub, and that is stated rather than hidden: `make nicputest` is what
 * gates the 2A03, against nestest's 8,991 lines, and a C model of a 6502 here
 * would be a second emulator whose agreement with the first proves nothing
 * (SPEC.md 91.14.4's note about C64-SPEC 9.8's all-black screendump).
 * ========================================================================*/
struct ni_mach ni_m;

void ni_move(unsigned dseg, unsigned doff, unsigned sseg, unsigned soff,
             unsigned n)
{
    memmove(pool + (size_t)dseg * 16 + doff,
            pool + (size_t)sseg * 16 + soff, n);
}

void ni_fill(unsigned dseg, unsigned doff, int val, unsigned n)
{
    memset(pool + (size_t)dseg * 16 + doff, val, n);
}

unsigned ni_peekw(unsigned seg, unsigned off)
{
    return (unsigned)pool[(size_t)seg * 16 + off]
         | ((unsigned)pool[(size_t)seg * 16 + off + 1] << 8);
}

void ni_pokew(unsigned seg, unsigned off, unsigned v)
{
    pool[(size_t)seg * 16 + off] = (unsigned char)v;
    pool[(size_t)seg * 16 + off + 1] = (unsigned char)(v >> 8);
}

void ni_oam_move(unsigned ramoff)
{
    memmove(pool + (size_t)ni_m.machseg * 16 + 0x3000,
            pool + (size_t)ni_m.machseg * 16 + ramoff, 256);
}

/* ni_chr_decode - MODELLED, and wave 1's note said what would make it so:
 * "wave 2 gives this a body only if niref.py needs one to check the COMPOSER's
 * C model, and it will say so." It does. The C model of the composer below
 * reads the tile cache exactly as the shipping assembly does - a byte a
 * pixel, 64 bytes a tile - so a cache with nothing in it would make the model
 * render 240 lines of backdrop and agree with nothing.
 *
 * IT IS STILL NOT A GATE ON THE REAL DECODER. That is nimemtest's, on a real
 * x86 under SS != DS, against tiles computed by hand at both ends of a bank
 * (SPEC.md 91.14.4) - and this body is written from the CHR format (two bit
 * planes, most significant bit leftmost) rather than from niband.inc, for the
 * reason that section gives about C64-SPEC 9.8. */
void ni_chr_decode(unsigned dseg, unsigned doff, unsigned sseg, unsigned soff)
{
    unsigned char *d = pool + (size_t)dseg * 16 + doff;
    const unsigned char *s = pool + (size_t)sseg * 16 + soff;
    int t, r, b;

    for (t = 0; t < 64; t++)
        for (r = 0; r < 8; r++) {
            int lo = s[t * 16 + r], hi = s[t * 16 + 8 + r];
            for (b = 0; b < 8; b++)
                d[t * 64 + r * 8 + b] =
                    (unsigned char)(((lo >> (7 - b)) & 1)
                                    | (((hi >> (7 - b)) & 1) << 1));
        }
}

/* ==========================================================================
 * THE COMPOSER, MODELLED - and this half IS the thing tools/niref.py checks
 *
 * SPEC.md 91.14.4 names two dumps and they are not the same claim: niref.py
 * is compared against **niuitest's C MODEL frame** - what is below - and
 * against **nimemtest's SHIPPING-ASSEMBLY frame**, written out over the
 * serial port by the routine that actually runs on the machine. Running the
 * independent compositor against the model alone would be C64-SPEC 9.8's
 * cautionary case one level up: the C harness transcribed the routine
 * correctly, and that is exactly what made the real routine's two defects
 * invisible.
 *
 * So this is written from the NESdev description of the PPU and from the port
 * plan's R7 - nowhere from apps/infones/niband.inc - and the two are expected
 * to agree because they implement one specification, not because one was
 * copied.
 * ========================================================================*/
struct ni_comp ni_cs;
unsigned char ni_bgpal[16];
unsigned char ni_sppal[16];

#define HFW 256
#define HFH 240
static unsigned char hoam[256];         /* OAM, as the package copies it */
static unsigned char hline[272];        /* the composed line, window at +8 */
static unsigned char hspr[272];         /* the sprite scratch */
static unsigned char hframe[HFW * HFH]; /* ...and the frame niref.py checks */
static int h_present_rows;              /* how many present calls landed */
static int h_present_bad;               /* ...and how many were out of bounds */

void ni_oam_grab(void)
{
    memcpy(hoam, pool + (size_t)ni_cs.machseg * 16 + 0x3000, 256);
}

/* the nametable fold: four logical tables onto two physical ones */
static unsigned h_nt(unsigned addr)
{
    unsigned idx = (addr >> 10) & 3;
    unsigned phys = ni_cs.mirror ? (idx & 1) : ((idx >> 1) & 1);
    return pool[(size_t)ni_cs.machseg * 16 + 0x2800 + phys * 1024
                + (addr & 0x3FF)];
}

void ni_bg_line(void)
{
    unsigned v = ni_cs.v;
    unsigned cx0 = v & 0x1F, cy = (v >> 5) & 0x1F;
    unsigned nt0 = (v >> 10) & 3, fy = (v >> 12) & 7;
    const unsigned char *cache = pool + (size_t)ni_cs.cacheseg * 16;
    int col, b, x;

    if (!(ni_cs.mask & 0x08)) {
        for (x = 0; x < 256; x++)
            hline[8 + x] = ni_bgpal[0];
    } else {
        for (col = 0; col < 33; col++) {
            unsigned cx = cx0 + col;
            unsigned nt = nt0 ^ ((cx >= 32) ? 1u : 0u);
            unsigned base, tile, attr, shift, pal4, off;
            cx &= 0x1F;
            base = 0x2000 | (nt << 10);
            tile = h_nt(base | (cy << 5) | cx);
            attr = h_nt(0x23C0 | (nt << 10) | ((cy >> 2) << 3) | (cx >> 2));
            shift = ((cy & 2) << 1) | (cx & 2);
            pal4 = ((attr >> shift) & 3) * 4;
            off = (ni_cs.bgtile + tile) * 64 + fy * 8;
            for (b = 0; b < 8; b++) {
                int px = 8 - (int)ni_cs.fx + col * 8 + b;
                hline[px] = ni_bgpal[pal4 + cache[off + b]];
            }
        }
        if (!(ni_cs.mask & 0x02))
            for (x = 0; x < 8; x++)
                hline[8 + x] = ni_bgpal[0];
    }
    /* the frame niref.py reads is the VISIBLE window, by SOURCE line */
    if (ni_cs.line < HFH)
        memcpy(hframe + ni_cs.line * HFW, hline + 8, 256);
}

int ni_spr_line(void)
{
    const unsigned char *cache = pool + (size_t)ni_cs.cacheseg * 16;
    int i, n = 0, hit = -1, lo = 512, hi = -1, x;
    struct { unsigned off, x, tag, flip; } sl[8];

    ni_cs.hit = (unsigned)-1;
    ni_cs.nspr = 0;
    if (!(ni_cs.mask & 0x10))
        return -1;

    for (i = 0; i < 64; i++) {
        int row = (int)ni_cs.line - hoam[i * 4] - 1;
        int attr, tile, tag;
        if (row < 0 || row >= (int)ni_cs.sph)
            continue;
        if (n == 8) { ni_cs.ovf = 1; break; }
        attr = hoam[i * 4 + 2];
        if (attr & 0x80)
            row = (int)ni_cs.sph - 1 - row;
        if (ni_cs.sph == 8) {
            tile = (int)ni_cs.sptile + hoam[i * 4 + 1];
        } else {
            int t = hoam[i * 4 + 1];
            tile = ((t & 1) ? 256 : 0) + (t & 0xFE) + (row >> 3);
            row &= 7;
        }
        tag = ((attr & 3) * 4) | ((attr & 0x20) ? 0x20 : 0)
            | ((i == 0) ? 0x10 : 0);
        sl[n].off = (unsigned)(tile * 64 + row * 8);
        sl[n].x = hoam[i * 4 + 3];
        sl[n].tag = (unsigned)tag;
        sl[n].flip = (unsigned)(attr & 0x40);
        if ((int)sl[n].x < lo) lo = (int)sl[n].x;
        if ((int)sl[n].x + 7 > hi) hi = (int)sl[n].x + 7;
        n++;
    }
    ni_cs.nspr = (unsigned)n;
    if (n == 0)
        return -1;

    /* ASCENDING OAM ORDER, FIRST WRITER WINS (the plan's R7) */
    for (i = 0; i < n; i++) {
        int b;
        for (b = 0; b < 8; b++) {
            int src = sl[i].flip ? (7 - b) : b;
            int val = cache[sl[i].off + src];
            int d = 8 + (int)sl[i].x + b;
            if (!val || hspr[d])
                continue;
            hspr[d] = (unsigned char)(val | sl[i].tag);
        }
    }

    /* ...and ONE merge with the untouched background resolves priority */
    if (hi > 255) hi = 255;
    for (x = lo; x <= hi; x++) {
        int s = hspr[8 + x], b;
        if (!s)
            continue;
        if (x < 8 && !(ni_cs.mask & 0x04))
            continue;
        b = hline[8 + x];
        if ((s & 0x10) && !(b & 0x80) && hit < 0 && x != 255)
            hit = x;
        if ((s & 0x20) && !(b & 0x80))
            continue;
        hline[8 + x] = ni_sppal[s & 0x0F];
    }
    for (x = lo; x <= hi + 8 && x < 264; x++)
        hspr[8 + x] = 0;

    if (hit >= 0)
        ni_cs.hit = (unsigned)hit;
    if (ni_cs.line < HFH)
        memcpy(hframe + ni_cs.line * HFW, hline + 8, 256);
    return hit;
}

/* ni_present13 - the model checks the ONE thing this routine can get wrong on
 * a machine with no screen: that every store lands inside the 64,000-byte
 * framebuffer (SPEC.md 91.6.2, and nimemtest asserts the same bound on the
 * real one). */
void ni_present13(int row)
{
    h_present_rows++;
    if (row < 0 || row > 199 || row * 320 + 32 + 256 > 64000)
        h_present_bad++;
}

static int fsx_go_answer = 1;
static int fsx_go_calls;
unsigned ni_stk_lo;
int  ni_fsx_go(void *win)  { (void)win; fsx_go_calls++; return fsx_go_answer; }
void ni_fsx_wait(int c)    { (void)c; }
int  ni_getkey(void)       { return -1; }

static int fsx_caps_answer = 0x0F;
int ni_fsx_caps(void *win)
{
    (void)win;
    return fsx_caps_answer;
}

int  ni_run(int cycles) { (void)cycles; return 0; }
void ni_bwrite(unsigned a, int v) { (void)a; (void)v; }

int ni_bread(unsigned a)
{
    /* enough of the banked reader for ni_boot's reset vector: the window
     * table the loader just filled, at the stride the core reads it at */
    unsigned seg, off;

    if (a < 0x8000)
        return 0;
    off = 0x3120 + 0x80 + ((a >> 13) & 3) * 0x20;
    seg = ni_peekw(ni_m.machseg, off);
    if (seg == 0)
        return 0;
    return pool[(size_t)seg * 16 + (a & 0x1FFF)];
}

void ni_boot(void)
{
    ni_m.s = 0xFD;
    ni_m.p = 0x24;
    ni_m.pc = (unsigned)ni_bread(0xFFFC) | ((unsigned)ni_bread(0xFFFD) << 8);
}


/* ==========================================================================
 * THE FIXTURE, AND THE STATE BLOB tools/niref.py READS
 *
 * The formula is niref.py's synth() and nimemtest.asm's nifix_build, written
 * a third time. Every constant below is one of those two files' as well, and
 * build.sh diffs the blob this writes against niref.py's own.
 * ========================================================================*/
#define FX_CTRL   0x20              /* bg pattern table $0000, 8x16 sprites */
#define FX_MASK   0x1A              /* bg on + bg left on, sprites on,
                                     * sprite LEFT COLUMN OFF */
#define FX_FX     3
#define FX_MIRROR 1                 /* vertical */

static unsigned fx_v(int y)
{
    /* coarse Y walks with the line and coarse X steps at line 100: a
     * mid-frame $2005 write, which is the case a single snapshot cannot
     * express and a scanline model exists for */
    unsigned cy = (unsigned)((y >> 3) & 0x1F);
    unsigned fy = (unsigned)(y & 7);
    unsigned cx = (y < 100) ? 0u : 5u;
    unsigned nt = (y < 100) ? 0u : 1u;
    return (cx | (cy << 5)) | (nt << 10) | (fy << 12);
}

static void compose_fixture(void)
{
    unsigned mach, chr, cache;
    unsigned char *m, *c;
    int i, y, b;
    FILE *f;

    mach = os88_mem_claim(13);
    chr = os88_mem_claim(8);
    cache = os88_mem_claim(32);
    CHECK(mach && chr && cache, "the fixture's claims were refused");
    m = pool + (size_t)mach * 16;
    c = pool + (size_t)chr * 16;

    for (i = 0; i < 2048; i++)
        m[0x2800 + i] = (unsigned char)((i * 7) & 0xFF);
    for (i = 0; i < 32; i++)
        m[0x3100 + i] = (unsigned char)((i * 3 + 1) & 0x3F);
    for (i = 0; i < 64; i++) {
        m[0x3000 + i * 4 + 0] = (unsigned char)((i * 17) & 0xFF);
        m[0x3000 + i * 4 + 1] = (unsigned char)((i * 5) & 0xFF);
        m[0x3000 + i * 4 + 2] = (unsigned char)(i & 0xE3);
        m[0x3000 + i * 4 + 3] = (unsigned char)((i * 13) & 0xFF);
    }
    for (i = 0; i < 8192; i++)
        c[i] = (unsigned char)((i * 13 + (i >> 4)) & 0xFF);

    ni_machseg = mach;
    ni_chrseg = chr;
    ni_cacheseg = cache;
    for (b = 0; b < 8; b++)
        ni_chr_decode(cache, (unsigned)b * 4096, chr, (unsigned)b * 1024);
    ni_pal_build();

    memset(hspr, 0, sizeof hspr);
    ni_cs.cacheseg = cache;
    ni_cs.machseg = mach;
    ni_cs.bgtile = (FX_CTRL & 0x10) ? 256 : 0;
    ni_cs.sptile = (FX_CTRL & 0x08) ? 256 : 0;
    ni_cs.sph = (FX_CTRL & 0x20) ? 16 : 8;
    ni_cs.fx = FX_FX;
    ni_cs.mask = FX_MASK;
    ni_cs.mirror = FX_MIRROR;
    ni_oam_grab();
    for (y = 0; y < 240; y++) {
        ni_cs.v = fx_v(y);
        ni_cs.line = (unsigned)y;
        ni_cs.ovf = 0;
        ni_bg_line();
        ni_spr_line();
    }

    /* the state blob, in tools/niref.py's own layout */
    f = fopen("build/nirefC.state", "wb");
    CHECK(f != 0, "cannot write build/nirefC.state");
    fwrite("NIREF1", 1, 6, f);
    fputc(FX_CTRL, f);
    fputc(FX_MASK, f);
    fputc(FX_FX, f);
    fputc(FX_MIRROR, f);
    for (y = 0; y < 240; y++) {
        unsigned v = fx_v(y);
        fputc(v & 0xFF, f);
        fputc((v >> 8) & 0xFF, f);
    }
    fwrite(m + 0x3100, 1, 32, f);
    fwrite(m + 0x2800, 1, 2048, f);
    fwrite(c, 1, 8192, f);
    fwrite(m + 0x3000, 1, 256, f);
    fclose(f);

    f = fopen("build/nirefC.frm", "wb");
    CHECK(f != 0, "cannot write build/nirefC.frm");
    fwrite(hframe, 1, sizeof hframe, f);
    fclose(f);

    os88_mem_free(mach);
    os88_mem_free(chr);
    os88_mem_free(cache);
    ni_machseg = 0;
    ni_chrseg = 0;
    ni_cacheseg = 0;
}

/* ==========================================================================
 * THE agnes REPLAY (SPEC.md 91.14.4, the port plan's R4)
 *
 * `build/niagnes` runs a real ROM on agnes - the MIT reference machine - and
 * records, per sampled frame, the PPU's per-line scroll history in
 * tools/niref.py's `NIREF1` layout beside agnes's OWN 256x240 screen of NES
 * palette indices. This drives the C MODEL with that state and requires the
 * picture to come out the same.
 *
 * IT IS A DIFFERENT CLAIM FROM THE FIXTURE and both are wanted: the fixture
 * exercises every mechanism the composer HAS, and a real ROM exercises the
 * ones it USES, in the combinations it uses them in, on states nobody chose.
 *
 * NOT A build.sh ROW: it needs agnes fetched off a network, and a fresh
 * clone's `make infones` must neither stall nor fail on one (apps/c64's
 * build.sh says the same thing in capitals). `make niagnes` is where it runs.
 * ========================================================================*/
#define NB_HDR 10
#define NB_V   NB_HDR
#define NB_PAL (NB_V + 480)
#define NB_NT  (NB_PAL + 32)
#define NB_CHR (NB_NT + 2048)
#define NB_OAM (NB_CHR + 8192)
#define NB_LEN (NB_OAM + 256)

static int replay_one(const char *state, const char *frame)
{
    static unsigned char blob[NB_LEN];
    static unsigned char want[HFW * HFH];
    unsigned mach, chr, cache;
    unsigned char *m, *c;
    FILE *f;
    int i, y, b, bad = 0, first = -1;

    f = fopen(state, "rb");
    if (!f || fread(blob, 1, NB_LEN, f) != NB_LEN) {
        fprintf(stderr, "niuitest: %s is not a NIREF1 blob\n", state);
        return 1;
    }
    fclose(f);
    if (memcmp(blob, "NIREF1", 6) != 0) {
        fprintf(stderr, "niuitest: %s has no NIREF1 magic\n", state);
        return 1;
    }
    f = fopen(frame, "rb");
    if (!f || fread(want, 1, sizeof want, f) != sizeof want) {
        fprintf(stderr, "niuitest: %s is not a 256x240 frame\n", frame);
        return 1;
    }
    fclose(f);

    mach = os88_mem_claim(13);
    chr = os88_mem_claim(8);
    cache = os88_mem_claim(32);
    if (!mach || !chr || !cache) {
        fprintf(stderr, "niuitest: the replay's claims were refused\n");
        return 1;
    }
    m = pool + (size_t)mach * 16;
    c = pool + (size_t)chr * 16;
    memcpy(m + 0x3100, blob + NB_PAL, 32);
    memcpy(m + 0x2800, blob + NB_NT, 2048);
    memcpy(m + 0x3000, blob + NB_OAM, 256);
    memcpy(c, blob + NB_CHR, 8192);

    ni_machseg = mach;
    ni_chrseg = chr;
    ni_cacheseg = cache;
    for (b = 0; b < 8; b++)
        ni_chr_decode(cache, (unsigned)b * 4096, chr, (unsigned)b * 1024);
    ni_pal_build();

    memset(hspr, 0, sizeof hspr);
    ni_cs.cacheseg = cache;
    ni_cs.machseg = mach;
    ni_cs.bgtile = (blob[6] & 0x10) ? 256 : 0;
    ni_cs.sptile = (blob[6] & 0x08) ? 256 : 0;
    ni_cs.sph = (blob[6] & 0x20) ? 16 : 8;
    ni_cs.fx = blob[8] & 7;
    ni_cs.mask = blob[7];
    ni_cs.mirror = blob[9] & 1;
    ni_oam_grab();
    for (y = 0; y < HFH; y++) {
        ni_cs.v = (unsigned)blob[NB_V + y * 2]
                | ((unsigned)blob[NB_V + y * 2 + 1] << 8);
        ni_cs.line = (unsigned)y;
        ni_cs.ovf = 0;
        ni_bg_line();
        ni_spr_line();
    }
    os88_mem_free(mach);
    os88_mem_free(chr);
    os88_mem_free(cache);
    ni_machseg = ni_chrseg = ni_cacheseg = 0;

    /* THE TAGS ARE MASKED, because they are this port's own rendering
     * convenience and not part of the picture (SPEC.md 91.5.2) - which is
     * exactly what tools/niref.py's own compare does. */
    for (i = 0; i < HFW * HFH; i++)
        if ((hframe[i] & 0x3F) != (want[i] & 0x3F)) {
            bad++;
            if (first < 0)
                first = i;
        }
    if (bad) {
        printf("niuitest: %s: %d differing pixel(s), the first at x=%d y=%d "
               "(model %d, agnes %d)\n", frame, bad, first % HFW, first / HFW,
               hframe[first] & 0x3F, want[first] & 0x3F);
        return 1;
    }

    /* THE NEGATIVE CONTROL, per frame, because a check that cannot fail is
     * not a check (tools/niref.py's own rule). One pixel of the expected
     * picture is changed by one PALETTE INDEX - not by a tag bit, which is
     * masked on purpose - and the compare must see it. A first attempt at
     * this injected the defect into the STATE instead, flipping one bit of
     * one line's scroll, and the picture came out identical: that line's
     * background was uniform there, so the control proved nothing and read as
     * a pass. */
    {
        int probe = (HFH / 2) * HFW + HFW / 2;
        unsigned char save = want[probe];
        want[probe] = (unsigned char)((save & 0x3F) ^ 0x01);
        if ((hframe[probe] & 0x3F) == (want[probe] & 0x3F)) {
            printf("niuitest: %s: the NEGATIVE CONTROL did not fire - a "
                   "changed palette index compared equal\n", frame);
            want[probe] = save;
            return 1;
        }
        want[probe] = save;
    }
    printf("niuitest: %s: 61,440 pixels identical to agnes's own screen "
           "(and a one-index defect is seen)\n", frame);
    return 0;
}

/* ==========================================================================
 * THE DRIVER
 * ========================================================================*/
static void *win;

static int replay_main(int argc, char **argv)
{
    int i, bad = 0;

    for (i = 2; i + 1 < argc; i += 2)
        bad += replay_one(argv[i], argv[i + 1]);
    if (bad) {
        fprintf(stderr, "niuitest: %d recording(s) FAILED\n", bad);
        return 1;
    }
    printf("niuitest: %d agnes recording(s) replayed, every frame identical\n",
           (argc - 2) / 2);
    return 0;
}

static void step(const char *name)
{
    step_no++;
    step_name = name;
}

/* row(f) - what the GLASS reads on field f's row, trimmed. This is the model
 * talking, not the shadow: the whole point is to compare the two. */
static const char *row(int f)
{
    static char buf[GW + 1];
    int y, i, n;

    y = win_oy + NI_MARGIN_Y + f * NI_ROW_H;
    if (y < 0 || y >= GH)
        return "";
    memcpy(buf, glass[y], GW);
    buf[GW] = 0;
    n = (int)strlen(buf);
    while (n > 0 && buf[n - 1] == ' ')
        buf[--n] = 0;
    /* ...and the leading margin, which is the content origin's own column */
    i = (win_ox + NI_MARGIN_X) / 8;
    return buf + i;
}

/* THE SHADOW AUDIT: every field on the glass must read what ni_txt says. It
 * is the check that catches a shadow which has stopped describing the glass -
 * the defect a dialog leaves behind, and the one LESSONS.md 6 records as
 * "after a Save the row still read `iH#there bold`". */
static void audit(void)
{
    int f;

    for (f = 0; f < NI_F_N; f++) {
        const char *g = row(f);
        if (strcmp(g, ni_txt[f]) != 0) {
            fail_count++;
            fprintf(stderr, "niuitest: FAIL at step %d (%s): field %d\n"
                    "            glass  [%s]\n"
                    "            wanted [%s]\n"
                    "            shadow [%s]\n",
                    step_no, step_name, f, g, ni_txt[f], ni_sh[f]);
        }
    }
}

static void expect(int f, const char *want)
{
    if (strcmp(ni_txt[f], want) != 0) {
        fail_count++;
        fprintf(stderr, "niuitest: FAIL at step %d (%s): field %d\n"
                "            is   [%s]\n"
                "            want [%s]\n",
                step_no, step_name, f, ni_txt[f], want);
    }
}

/* --- the ROM fixtures, all synthesised: no ROM file is read here ---------- */
static unsigned char rombuf[64 * 1024];

static unsigned mk_rom(int prg16, int chr8, int flags6, int flags7,
                       int diskdude)
{
    unsigned n;

    memset(rombuf, 0, sizeof(rombuf));
    rombuf[0] = 'N'; rombuf[1] = 'E'; rombuf[2] = 'S'; rombuf[3] = 0x1A;
    rombuf[4] = (unsigned char)prg16;
    rombuf[5] = (unsigned char)chr8;
    rombuf[6] = (unsigned char)flags6;
    rombuf[7] = (unsigned char)flags7;
    if (diskdude)
        memcpy(rombuf + 8, "DiskDude!", 8);
    n = 16 + (unsigned)prg16 * 16384 + (unsigned)chr8 * 8192;
    /* the reset vector, so ni_boot has something to read: $C000. BOUNDED,
     * because step 7 asks for a header that CLAIMS 256KB - the fixture is a
     * lie about its own length and the buffer is 64KB. Writing the vector
     * where the header says the ROM ends walked 192KB past this array on the
     * first run of this harness, which is the same class of defect the length
     * check in nirom.c exists to stop one layer down. */
    if (16 + (unsigned)prg16 * 16384 <= sizeof(rombuf)) {
        rombuf[16 + (unsigned)prg16 * 16384 - 4] = 0x00;
        rombuf[16 + (unsigned)prg16 * 16384 - 3] = 0xC0;
    }
    return n;
}

static void open_rom(const char *name, unsigned len)
{
    filebuf = rombuf;
    filelen = len;
    os88_gfx_lock();
    os88_onfile(OS88_FDLG_OPEN, name, len, 0, win);
    os88_gfx_unlock();
    if (wake_pending) {
        wake_pending = 0;
        os88_onwake(win);
    }
}

static int menu_dis(const char *s) { return s[0] == (char)OS88_MENU_DIS; }

int main(int argc, char **argv)
{
    /* `--replay STATE FRAME [STATE FRAME...]` is `make niagnes`'s half and
     * runs NOTHING ELSE: the panel steps below drive the program like a user
     * and the replay drives the COMPOSER like a frame, and mixing the two
     * would make one of them the other's setup. */
    if (argc > 1 && strcmp(argv[1], "--replay") == 0)
        return replay_main(argc, argv);

    unsigned len;
    int before;
    int c_whole_call = 0, c_whole_cell = 0, c_one_call = 0, c_one_cell = 0;

    glass_clear();

    /* ---- step 1: the launch --------------------------------------------- */
    step("launch");
    win = os88_main();
    CHECK(win != 0, "os88_main refused a launch the heap could serve");
    CHECK(ni_m.machseg != 0, "the machine claim was never taken");
    CHECK(reg_menus != 0, "no menu set was registered");
    CHECK(reg_menus->nmenus == 3, "there must be exactly three menus");
    CHECK(strcmp(reg_menus->name, "InfoNES") == 0,
          "AM_NAME must be the product, `InfoNES`");
    CHECK(win_h + 0 <= 448 - OS88_MBAR_H, "the window must fit the desktop");
    /* THE FIRST WAKE IS WHAT RESOLVES THE OVERLAY (SPEC.md 91.13.1), and on a
     * plain launch nothing else posts one. Without it every menu pick refuses
     * with `Unable to load INFONES.OVL.` on a disk that has the file - which
     * is what the glass showed on the first run of this fix. */
    CHECK(wake_pending == 1, "os88_main did not post a wake");
    wake_pending = 0;
    os88_onwake(win);               /* the probe runs here, unlocked */
    CHECK(ni_ovl_res == 1, "ovl_probe did not resolve the module");

    /* every item must fit MENU_MAXCH's 24 glyphs AFTER the MENU_DIS byte -
     * kernel/menu.inc:1977-1983 skips it before menu_trunc counts, and a
     * shortened label is what SPEC.md 91.8's rule forbids */
    {
        int m, i;
        for (m = 0; m < reg_menus->nmenus; m++) {
            CHECK(reg_menus->menu[m].nitems <= 11,
                  "a pull-down is over MENU_POPMAX");
            for (i = 0; i < reg_menus->menu[m].nitems; i++) {
                const char *s = reg_menus->menu[m].items[i];
                if (menu_dis(s))
                    s++;
                if ((int)strlen(s) > 24) {
                    fail_count++;
                    fprintf(stderr, "niuitest: menu %d item %d is %d glyphs, "
                            "over MENU_MAXCH's 24: [%s]\n",
                            m, i, (int)strlen(s), s);
                }
            }
        }
    }

    /* ---- step 2: the first paint ---------------------------------------- */
    step("first paint");
    os88_gfx_lock();
    os88_paint(win);
    os88_gfx_unlock();
    expect(NI_F_ROM,    "ROM : (none)");
    expect(NI_F_MAPPER, "Mapper : -");
    expect(NI_F_STATE,  "No ROM");
    CHECK(ni_txt[NI_F_FACT][0] != 0,
          "the FACT LINE is empty and this build greys six items");
    audit();

    /* ---- step 3: the DELTA DRAW ----------------------------------------- */
    step("repaint with nothing changed");
    before = n_font_run;
    c_whole_call = n_calls;
    c_whole_cell = n_cells;
    os88_gfx_lock();
    os88_paint(win);
    os88_gfx_unlock();
    c_whole_call = n_calls - c_whole_call;
    c_whole_cell = n_cells - c_whole_cell;
    /* os88_paint is a W_PAINT with WF_OWNBG set: it fills the damage itself
     * and the shadow is dropped over it, so EVERY field is drawn again. That
     * is correct and it is what `whole` means. */
    CHECK(n_font_run - before == NI_F_N,
          "a W_PAINT must re-letter every field, because the fill under it "
          "whitened the content");
    /* ...AND IT MUST NOT PAD. Every one of those thirteen runs is over paper
     * the fill has just laid down, so a padded run writes paper over paper:
     * 468 cells to show 225 characters, 219 ms of a 4.77 MHz 8088 thrown
     * away on every expose (PERFORMANCE.md rule 2). */
    {
        int f, want = 0;
        for (f = 0; f < NI_F_N; f++)
            want += (int)strlen(ni_txt[f]);
        CHECK(c_whole_cell == want,
              "a whole repaint padded its runs: the glass under them is the "
              "fill's own paper and there is nothing to erase");
    }
    before = n_font_run;
    os88_gfx_lock();
    ni_panel_paint(win, 0);         /* ...and a NON-whole repaint with nothing
                                     * changed must draw NOTHING */
    os88_gfx_unlock();
    CHECK(n_font_run - before == 0,
          "the delta draw re-lettered a field whose text had not changed");
    audit();

    step("ONE field changes and only the differing SPAN is lettered");
    c_one_call = n_calls;
    c_one_cell = n_cells;
    ni_setfield(NI_F_STATE, "Running");     /* `Ready` -> `Running` */
    os88_gfx_lock();
    ni_panel_paint(win, 0);
    os88_gfx_unlock();
    c_one_call = n_calls - c_one_call;
    c_one_cell = n_cells - c_one_cell;
    CHECK(c_one_call == 1, "a one-field update must be ONE drawing call");
    CHECK(c_one_cell <= 8,
          "a one-field update lettered the whole 40-cell field to change six "
          "characters - 33 ms on the target against 6");
    audit();
    ni_panel_state();               /* put the state line back */
    os88_gfx_lock();
    ni_panel_paint(win, 0);
    os88_gfx_unlock();
    audit();

    /* ---- step 3b: WF_OWNBG's partial expose ------------------------------
     * The whole point of the flag: a window dragged across a corner of this
     * one exposes two rows, and this must repaint two rows and not thirteen.
     * Without os88_wm_ownbg the kernel has already whitened the content and
     * "whole" is the only correct answer (SPEC.md 11.90.2's interlock), which
     * is what the stub above models. */
    step("a partial expose repaints only the rows under it");
    dmg_partial = 1;
    dmg_rect.x1 = win_ox;
    dmg_rect.x2 = win_ox + (win_w - 2) - 1;
    dmg_rect.y1 = win_oy + NI_MARGIN_Y + 1 * NI_ROW_H;
    dmg_rect.y2 = win_oy + NI_MARGIN_Y + 2 * NI_ROW_H + 7;
    before = n_font_run;
    os88_gfx_lock();
    os88_paint(win);
    os88_gfx_unlock();
    CHECK(n_font_run - before == 2,
          "a two-row expose must re-letter TWO fields, not thirteen");
    audit();

    /* ...AND THE CELLS, NOT ONLY THE ROWS (SPEC.md 11.90.2: "a partial repaint
     * is per ELEMENT, not per pixel" - the element here is a row, and a row
     * that was RIGHT before the fill owes only the span the fill covered).
     * A window dragged off the left 48 pixels damages 6 cells of 40; before
     * the cut this re-lettered both rows at full width, 13 calls / ~245 cells
     * / ~230 ms of a 4.77 MHz 8088 on a whole-panel expose. */
    step("...and a NARROW expose letters only the damaged cells");
    dmg_rect.x1 = win_ox;
    dmg_rect.x2 = win_ox + 48 - 1;
    before = n_cells;
    os88_gfx_lock();
    os88_paint(win);
    os88_gfx_unlock();
    CHECK(n_cells - before <= 2 * 7,
          "a 48-pixel expose lettered whole 40-cell rows: the cells outside "
          "the fill were never erased and did not need drawing again");
    CHECK(n_cells - before > 0, "a narrow expose drew nothing at all");
    audit();                        /* ...and the glass still reads right */

    step("...and an EMPTY damage rect draws nothing at all");
    dmg_rect.x1 = 10;
    dmg_rect.x2 = 0;                /* x1 > x2: SPEC.md 11.90.2's empty rect */
    before = n_calls;
    os88_gfx_lock();
    os88_paint(win);
    os88_gfx_unlock();
    CHECK(n_calls - before == 0,
          "an empty damage rect must cost nothing, not one fill");
    dmg_partial = 0;
    os88_gfx_lock();
    os88_paint(win);                /* back to a known glass */
    os88_gfx_unlock();
    audit();

    /* ---- step 4: Concentration Room's own numbers ------------------------ */
    step("open a 16K/8K mapper-0 ROM");
    len = mk_rom(1, 1, 0x01, 0x00, 0);      /* vertical mirroring, no SRAM */
    open_rom("CROOM.NES", len);
    expect(NI_F_ROM,    "ROM : CROOM.NES");
    expect(NI_F_MAPPER, "Mapper : 0");
    expect(NI_F_PRG,    "PRG ROM : 16KB");
    expect(NI_F_CHR,    "CHR ROM : 8KB");
    expect(NI_F_MIRROR, "Mirroring : V");
    expect(NI_F_SRAM,   "SRAM : No");
    expect(NI_F_FOUR,   "4 Screen : No");
    expect(NI_F_TRAIN,  "Trainer : No");
    expect(NI_F_STATE,  "Ready");
    audit();
    CHECK(ni_m.pc == 0xC000, "the reset vector was not read through the "
                             "window table");
    CHECK(!menu_dis(reg_menus->menu[NI_M_FILE].items[NI_I_RESET]),
          "Reset must be LIVE once a ROM is loaded");

    /* ---- step 5: CHR-RAM reads as CHR RAM, not `CHR ROM : 0KB` ---------- */
    step("open a 32K CHR-RAM ROM");
    len = mk_rom(2, 0, 0x00, 0x00, 0);      /* horizontal, CHR-RAM */
    open_rom("RFK.NES", len);
    expect(NI_F_PRG,    "PRG ROM : 32KB");
    expect(NI_F_CHR,    "CHR RAM : 8KB");
    expect(NI_F_MIRROR, "Mirroring : H");
    audit();

    /* ---- step 6: the mapper refusal, with InfoNES's own sentence -------- */
    step("open a mapper-66 ROM");
    len = mk_rom(1, 1, 0x21, 0x40, 0);      /* mapper 66 */
    open_rom("BADMAP.NES", len);
    expect(NI_F_STATE, "Mapper #66 is unsupported.");
    expect(NI_F_ROM,   "ROM : (none)");     /* a refusal must not leave the
                                             * last ROM's numbers on the glass */
    expect(NI_F_MAPPER, "Mapper : -");
    audit();

    /* ---- step 7: THE LENGTH CHECK, which none of the three references has
     *
     * TWO CASES, AND THEY ARE THE TWO THAT WRAP A 16-BIT PRODUCT. A header
     * claiming 256KB computes 270,352 bytes and a 128KB one 131,088, which in
     * SIXTEEN bits are 8,208 and 16 - both SMALLER than the file they are
     * lying about, so a byte-wise check passes them and the loader then moves
     * a quarter of a megabyte out of a 24KB staging claim.
     *
     * THIS HARNESS CANNOT SEE THAT OVERFLOW AND NEVER COULD: the host's
     * `unsigned` is thirty-two bits and the product does not wrap here. It
     * was found on the glass, and what fixed it is arithmetic in KB that
     * cannot wrap at all (nirom.c). These two rows are the BEHAVIOUR, kept so
     * that a later wave which re-derives the check has to keep it. */
    step("open a header that claims 256KB in a 24KB file");
    len = mk_rom(16, 1, 0x01, 0x00, 0);
    open_rom("SHORT.NES", 24 * 1024);
    CHECK(strncmp(ni_txt[NI_F_STATE], "Header claims", 13) == 0
          || strncmp(ni_txt[NI_F_STATE], "ROM is larger", 13) == 0,
          "a lying header must be refused with both numbers");
    expect(NI_F_ROM, "ROM : (none)");
    audit();

    step("...and one that claims 128KB in a 40KB file");
    len = mk_rom(8, 0, 0x01, 0x00, 0);
    open_rom("SHORT2.NES", 40 * 1024);
    CHECK(strncmp(ni_txt[NI_F_STATE], "Header claims", 13) == 0
          || strncmp(ni_txt[NI_F_STATE], "ROM is larger", 13) == 0,
          "128KB claimed in a 40KB file must be refused too - it is the same "
          "defect with a different wrap");
    expect(NI_F_ROM, "ROM : (none)");
    audit();

    step("...and a 48KB ROM, which is the largest the whole-file path takes");
    len = mk_rom(3, 1, 0x01, 0x00, 0);      /* 48KB PRG + 8KB CHR = 56KB */
    open_rom("BIG.NES", len);
    expect(NI_F_PRG,   "PRG ROM : 48KB");
    expect(NI_F_STATE, "Ready");
    audit();

    /* ---- step 8: the DiskDude guard ------------------------------------- */
    step("open a DiskDude! header");
    /* byInfo2's high nibble is 4 and bytes 12-15 are dirty, so the mapper is
     * the LOW nibble alone - 0 - and the ROM loads (InfoNES.cpp:365-373) */
    len = mk_rom(1, 1, 0x01, 0x40, 1);
    open_rom("DIRTY.NES", len);
    expect(NI_F_MAPPER, "Mapper : 0");
    expect(NI_F_STATE,  "Ready");
    audit();

    /* ---- step 9: the About panel owns the glass and gives it back ------- */
    step("About InfoNES");
    os88_gfx_lock();
    os88_about(win);
    os88_gfx_unlock();
    CHECK(ni_abt == 1, "the About latch was not set");
    CHECK(strcmp(glass[ni_abt_y + 6], "") != 0, "the About panel drew nothing");
    /* A REPAINT WHILE THE PANEL IS UP MUST PUT IT BACK. Without it, another
     * window dragged across erased the panel AND the readout under it and
     * drew neither: a blank white box with ni_abt still latched, and the only
     * way out was to guess that a key would close it. */
    step("...and a repaint while it is up redraws it");
    before = n_frame;
    os88_gfx_lock();
    os88_paint(win);
    os88_gfx_unlock();
    CHECK(ni_abt == 1, "the About latch was dropped by a repaint");
    CHECK(n_frame - before >= 2,
          "a repaint with the About panel up must redraw the panel and its "
          "OK button, not leave a blank white box");
    CHECK(strcmp(glass[ni_abt_y + 6], "") != 0,
          "the About panel's rows were not redrawn");
    /* ...AND THE READOUT AROUND IT. os88_paint whitens the whole damage rect
     * and WF_OWNBG says it owes every pixel in it (SPEC.md 11.90.1); the About
     * panel is 280 x 112 inside a ~324 x 140 content box, so the strip beside
     * it and the bands above and below are the package's to repay. A draft
     * skipped every field while ni_abt was set - ni_panel_paint returns on its
     * first line - and left those bare white until Esc. Field 0's row is above
     * the panel entirely, which makes it the one that can be read. */
    CHECK(strcmp(row(NI_F_ROM), ni_txt[NI_F_ROM]) == 0,
          "a repaint with the About panel up left the readout AROUND it blank "
          "- WF_OWNBG's promise is every pixel in the damage rect");

    /* THE CLOSE MUST ERASE THE LIVE CONTENT BOX AND NOTHING OUTSIDE IT. The
     * banked ni_abt_x/y are absolute SCREEN coordinates and os88_gfx_fill is
     * unclipped, so a window that moved while the panel was up had this fill
     * land on its own title bar and on the desktop beside it - photographed
     * before the fix. */
    step("...and its close repaints the panel");
    win_ox += 40;                   /* the window MOVED while the panel was up */
    win_oy += 30;
    fill_x1 = fill_y1 = fill_x2 = fill_y2 = -1;
    os88_gfx_lock();
    ni_about_close(win);
    os88_gfx_unlock();
    CHECK(fill_x1 >= win_ox && fill_y1 >= win_oy
          && fill_x2 <= win_ox + (win_w - 2) - 1
          && fill_y2 <= win_oy + (win_h - OS88_TITLE_H - 1) - 1,
          "the About close filled OUTSIDE the live content box - which on the "
          "glass is this window's own title bar and the desktop beside it");
    CHECK(ni_abt == 0, "the About latch survived the close");
    audit();                        /* THE SHADOW MUST HAVE BEEN DROPPED: a
                                     * panel that owned the glass and did not
                                     * drop it leaves every field describing
                                     * pixels the panel overwrote */

    /* ---- step 10: Stop unloads ------------------------------------------ */
    step("File > Stop");
    os88_gfx_lock();
    os88_oncmd(NI_I_STOP, NI_M_FILE, win);
    os88_gfx_unlock();
    expect(NI_F_ROM,   "ROM : (none)");
    expect(NI_F_STATE, "No ROM");
    CHECK(menu_dis(reg_menus->menu[NI_M_FILE].items[NI_I_RESET]),
          "Reset must be GREYED with no ROM");
    audit();

    /* ---- step 11: the heap refusal quotes both numbers ------------------- */
    step("a heap that cannot hold the ROM");
    largest_kb = 20;                /* enough for the staging claim, not for
                                     * the 32KB tile cache */
    len = mk_rom(1, 1, 0x01, 0x00, 0);
    open_rom("CROOM.NES", len);
    CHECK(strstr(ni_txt[NI_F_STATE], "largest") != 0,
          "a heap refusal must quote what it asked for AND what there is");
    largest_kb = 400;
    audit();

    /* ---- step 12: THE LAUNCH DOCUMENT (SPEC.md 54.10) -------------------
     * A double-click on a .NES launches this program - that is what
     * niassoc.inc and os88_assoc_set buy - and it must LOAD THE ROM. The
     * association without this banking is a program that opens empty, which
     * is the hole cword had and 54.10 closed. The size is NOT known on this
     * path, so the loader's `0 means unknown` arm is what runs. */
    step("a .NES double-clicked on the desktop");
    largest_kb = 400;
    len = mk_rom(1, 1, 0x01, 0x00, 0);
    filebuf = rombuf;
    filelen = len;
    arg_name = "THWAITE.NES";
    win = os88_main();              /* a second instance, the way a launch is */
    CHECK(win != 0, "the launch was refused");
    /* os88_main POSTS A WAKE, and it must: the kernel posts a first one
     * itself only on the launch-document path, so a plain launch would
     * otherwise never run the wake - and SPEC.md 91.13.1's ovl_probe, which
     * is what resolves INFONES.OVL off the gfx lock, lives there. On THIS
     * path the kernel would have posted one too, and the kernel keeps at
     * most one queued wake per window, so the two are the same wake. */
    CHECK(wake_pending == 1, "os88_main must post a wake, or nothing ever "
                             "resolves the overlay on a plain launch");
    wake_pending = 0;
    os88_onwake(win);               /* ...the kernel's first wake */
    expect(NI_F_ROM,   "ROM : THWAITE.NES");
    expect(NI_F_STATE, "Ready");
    os88_gfx_lock();
    os88_paint(win);
    os88_gfx_unlock();
    audit();

    step("...and a folder that cannot be listed");
    arg_goto_fails = 1;
    win = os88_main();
    os88_onwake(win);
    CHECK(strcmp(ni_txt[NI_F_STATE], "Could not open that folder.") == 0,
          "a goto that refuses must SAY so - without an arm the launch fails "
          "in silence and the double-click looks as if it did nothing");
    arg_goto_fails = 0;
    arg_name = 0;

    /* ---- THE COMPOSER, AND THE FRAME tools/niref.py CHECKS ---------------
     * SPEC.md 91.14.4 names TWO dumps and they are not the same claim: this
     * one is the C MODEL's, which touches no assembly at all, and
     * nimemtest's is the SHIPPING ASSEMBLY's, written out over the serial
     * port. Running the independent compositor against the model alone would
     * be C64-SPEC 9.8's cautionary case one level up.
     *
     * THE FIXTURE IS WRITTEN THREE TIMES ON PURPOSE - here, in
     * tools/niref.py's synth() and in nimemtest.asm's nifix_build - and
     * build.sh compares this state blob against niref.py's byte for byte. A
     * fixture written once and shared would make two frames agree about a
     * state neither of them checked. */
    step("the composer's C model, and its frame for tools/niref.py");
    compose_fixture();

    /* ---- THE ROW-DROP TABLE, AND THE OVERFLOW A HOST CANNOT SEE ---------
     * The 240 -> 200 reduction (SPEC.md 91.6.2) has to keep EXACTLY 200
     * source rows, map them to 0..199 strictly increasing, and put every
     * store inside the 64,000-byte screen. Those three are checked below and
     * they are the easy half.
     *
     * THE HARD HALF IS THAT `int` IS SIXTEEN BITS ON THE TARGET AND
     * THIRTY-TWO HERE. The first version of ni_rowtab computed
     * `(i * 200) / 224`, whose product passes 32,767 at i = 164: on the 8086
     * the quotient went NEGATIVE, the last sixty entries were garbage, and
     * thirty-six destination rows were never presented at all - a picture
     * with its bottom fifth missing and the mode's border showing through.
     * On the host it is exactly right and this harness saw nothing, which is
     * `nirom.c`'s own lesson a second time.
     *
     * So the table is re-computed here with the multiply TRUNCATED TO SIXTEEN
     * BITS, the way the machine does it, and the two must agree. That is a
     * check for the DEFECT CLASS and not for one expression: any later wave
     * that changes the fraction is checked the same way. */
    step("the 240 -> 200 row-drop table, and its 16-bit arithmetic");
    {
        int i, n = 0, last = -1, bad16 = 0;
        ni_rowtab();
        for (i = 0; i < 240; i++) {
            if (ni_rowdst[i] == 0xFF)
                continue;
            n++;
            CHECK(i >= 8 && i < 232,
                  "a kept row outside the 8-top-8-bottom crop");
            CHECK((int)ni_rowdst[i] == last + 1,
                  "the destination rows are not 0..199 strictly increasing");
            last = ni_rowdst[i];
            CHECK((int)ni_rowdst[i] * 320 + 32 + 256 <= 64000,
                  "a present would store past the 64,000-byte screen");
        }
        CHECK(n == 200, "the table does not keep exactly 200 source rows");
        /* THE 16-BIT HALF IS NOT HERE AND CANNOT BE. `int` is thirty-two
         * bits in this harness and sixteen on the target, and the only way
         * to see an overflow from C is to model the exact expression the
         * source uses - which is a check that restates what it checks and
         * stops working the moment the expression is rewritten (LESSONS.md
         * 2). build.sh's `nirow` row READS ni_rowtab out of nirun.c instead,
         * and refuses a multiply whose largest product passes 32,767. What
         * IS here is the shape of the table, which is worth checking on
         * either word size. */
        (void)bad16;
    }

    /* ---- the cost table -------------------------------------------------- */
    printf("\nniuitest: the panel's cost, on the target 4.77 MHz 8088\n");
    printf("  (PERFORMANCE.md: any gfx_* call ~756 us, one 8x8 glyph cell "
           "~900 us)\n\n");
    printf("    drawing calls          %5d\n", n_calls);
    printf("      of them font runs    %5d\n", n_font_run);
    printf("      fills / frames       %5d / %d\n", n_fill, n_frame);
    printf("    glyph cells            %5d\n", n_cells);
    /* MEASURED, NOT ASSERTED. The first version of this table printed
     * NI_F_N * 36 - the padded width - as if it were the price rather than
     * the budget, and that was the defect: 468 cells for 225 characters of
     * text. These two rows are counted off the model above. */
    printf("    a WHOLE panel repaint  %5d calls, %d cells = %d ms\n",
           c_whole_call, c_whole_cell,
           (c_whole_call * 756 + c_whole_cell * 900) / 1000);
    printf("    ONE field update       %5d call,  %d cells = %d ms\n",
           c_one_call, c_one_cell,
           (c_one_call * 756 + c_one_cell * 900) / 1000);
    printf("      (padded, as it was:  %5d call,  %d cells = %d ms)\n",
           1, NI_FCELLS, (756 + NI_FCELLS * 900) / 1000);
    printf("\n");

    if (fail_count) {
        fprintf(stderr, "niuitest: %d FAILURE(S)\n", fail_count);
        return 1;
    }
    printf("niuitest: %d steps, every field on the glass agrees with the "
           "shadow\n", step_no);
    return 0;
}
