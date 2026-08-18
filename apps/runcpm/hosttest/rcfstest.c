/* ============================================================================
 * os8088 - apps/runcpm/hosttest/rcfstest.c    RUNCPM's disk layer, on the host
 *
 * A BUILD-HOST PROGRAM (docs/RUNCPM-PORT-PLAN.md wave 4). It never runs on
 * the 8086, it is not in the package, and apps/runcpm/build.sh compiles and
 * runs it BEFORE the target build; a failure stops the build. Part of RUNCPM
 * (SPEC.md 71), a reimplementation of RunCPM 6.9 by Marcelo Dantas / "Mockba
 * the Borg" (MIT licence, Copyright (c) 2017 Mockba the Borg): the values it
 * expects - the extent chain, the record codes, $$$.SUB's shape - are worked
 * by hand from RunCPM/disk.h and cpm.h.
 *
 * WHY. rcfs.c (SPEC.md 71.3) reimplements RunCPM's disk.h over whole files
 * in heap claims: the FCB and directory-entry synthesis, the record math in
 * 16 bits, the open-file table with its eviction and write-back, the search
 * state and the multi-extent chain, $$$.SUB's truncation on close, the
 * capture files. Every one of those is a number a CP/M program reads back,
 * and a wrong extent byte or a record off by one is invisible on the glass
 * until MBASIC cannot LOAD what it SAVEd. So this includes rccpm.c and
 * rcfs.c - the BDOS dispatcher and the disk layer, as shipped - over a FAKE
 * FOLDER TREE WITH CONTENTS (the launch folder, A\0 with files of the sizes
 * the plan names - 0, 127, 128, 16384, 16385, 32768, 65535, 67301 and
 * 134899 bytes - and an empty B\0), fake claims (with a regrow that always
 * MOVES, so a caller that keeps the old segment is caught), the Z80's RAM
 * as an array, and drives the BDOS through rc_bdos() exactly as the Z80
 * would: C = the function, DE = the FCB, HL/A the answer, the DMA at 0080.
 *
 * WHAT IT CANNOT SEE. `int` is four bytes here and two there (the 16-bit
 * arithmetic is masked where it matters, and the size sums that would
 * overflow a 16-bit word are written as shift-and-carry in rcfs.c); the
 * movers are C loops here and rcmem.inc there (hosttest/rcmemtest.sh runs
 * those on a real x86); os88_file_find's cost is a COUNT here (n_find) and
 * int 13h calls there.
 *
 *   cc -O1 -w -I apps/runcpm/hosttest -I apps/runcpm \
 *      -o build/rcfstest apps/runcpm/hosttest/rcfstest.c && build/rcfstest
 * ==========================================================================*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "os88.h"

/* --- what runcpm.c declares ahead of the parts (the same names) ----------- */
static void rc_putc(int c);
static void rc_puts(const char *s);
static int  rc_key_pop(void);
static int  rc_fs_cd(int d, int u);
static int  rc_fs_home(void);
static int  rc_fs_drive_exists(int d);
static void rc_fs_flush_all(void);
static unsigned ovl_fs_open(void);
static unsigned ovl_fs_close(void);
static unsigned ovl_fs_make(void);
static unsigned ovl_search_first(unsigned fcbaddr, int isdir);
static unsigned ovl_fs_snext(void);
static unsigned ovl_fs_delete(void);
static unsigned ovl_fs_rename(void);
static unsigned rc_fs_readseq(void);
static unsigned rc_fs_writeseq(void);
static unsigned rc_fs_readrand(void);
static unsigned rc_fs_writerand(void);
static unsigned ovl_fs_size(void);
static unsigned ovl_fs_randrec(void);
static void ovl_fs_setuser(int u);
static unsigned ovl_fs_makedisk(void);
static unsigned ovl_fs_checksub(void);
static void rc_fs_capture(int lst, int c);
int  rc_rd(unsigned a);
int  rc_rd16(unsigned a);
void rc_wr(unsigned a, int v);
void rc_wr16(unsigned a, int v);
void rc_zcopy_in(unsigned zaddr, const void *src, unsigned n);
void rc_zcopy_out(void *dst, unsigned zaddr, unsigned n);
void rc_zzcopy_in(unsigned zaddr, unsigned seg, unsigned off, unsigned n);
void rc_zzcopy_out(unsigned seg, unsigned off, unsigned zaddr, unsigned n);
void rc_zfill(unsigned zaddr, int v, unsigned n);
void rc_sfill(unsigned seg, unsigned off, int v, unsigned n);

#define RC_KRING 64
static unsigned char rc_kbuf[RC_KRING];
static int rc_khead, rc_ktail;
static int rc_mask = 0x7F;
static unsigned rc_tk_hi;
static unsigned rc_ticks32(void) { return 0; }
static int rc_key_pop(void)
{
    int c;
    if (rc_khead == rc_ktail) return -1;
    c = rc_kbuf[rc_khead];
    rc_khead = (rc_khead + 1) & (RC_KRING - 1);
    return c;
}
static void rc_key_push(int c) { rc_kbuf[rc_ktail] = (unsigned char)c; rc_ktail = (rc_ktail + 1) & (RC_KRING - 1); }

/* the console: what the BDOS printed, for the assertions */
static char con[4096];
static int conn;
static void rc_putc(int c) { if (conn < (int)sizeof(con) - 1) con[conn++] = (char)c; con[conn] = 0; }
static void rc_puts(const char *s) { while (*s) rc_putc(*s++); }
static void con_clear(void) { conn = 0; con[0] = 0; }
static int rc_full;                                /* runcpm.c's fullscreen latch */
static void *rc_win;                               /* ...and its window */
static void rc_flush(void *w);

#include "../rccpm.c"
#include "../rcfs.c"

struct rc_zregs rc_z;

/* --- the Z80's RAM and the claims ------------------------------------------ */
static unsigned char zram[65536];
#define NCLAIM 20
static unsigned char *cl_mem[NCLAIM];    /* by index; seg = 0x1000 * (i + 1) */
static unsigned cl_kb[NCLAIM];
static int cl_live[NCLAIM];
static int n_claims_made, n_regrows, n_frees;
static int cl_limit;                     /* the heap holds this many claims
                                          * (0 = plenty) */
static int ci(unsigned seg)
{
    int i = (int)(seg >> 12) - 1;
    if (seg == 0 || (seg & 0xFFF) || i < 0 || i >= NCLAIM || !cl_live[i]) {
        printf("HARNESS: segment %04x is not a live claim\n", seg); exit(1);
    }
    return i;
}
static int live_claims(void) { int i, n = 0; for (i = 0; i < NCLAIM; i++) n += cl_live[i]; return n; }
unsigned os88_mem_claim(int kb)
{
    int i;
    if (cl_limit && live_claims() >= cl_limit) return 0;
    for (i = 0; i < NCLAIM; i++) if (!cl_live[i]) break;
    if (i == NCLAIM) return 0;
    cl_mem[i] = malloc(65536); memset(cl_mem[i], 0xA5, 65536);   /* garbage */
    cl_kb[i] = kb; cl_live[i] = 1; n_claims_made++;
    return (unsigned)(0x1000 * (i + 1));
}
int os88_mem_free(unsigned seg) { int i = ci(seg); free(cl_mem[i]); cl_mem[i] = 0; cl_live[i] = 0; n_frees++; return 0; }
/* regrow ALWAYS MOVES here: a caller keeping the old segment reads garbage */
unsigned os88_mem_regrow(unsigned seg, int kb)
{
    int i = ci(seg), j;
    for (j = 0; j < NCLAIM; j++) if (!cl_live[j]) break;
    if (j == NCLAIM) return 0;
    cl_mem[j] = malloc(65536); memset(cl_mem[j], 0xA5, 65536);
    memcpy(cl_mem[j], cl_mem[i], cl_kb[i] << 10);
    cl_kb[j] = kb; cl_live[j] = 1;
    free(cl_mem[i]); cl_mem[i] = 0; cl_live[i] = 0;
    n_regrows++;
    return (unsigned)(0x1000 * (j + 1));
}
unsigned os88_mem_largest_kb(void) { return 200; }
int  os88_peek(unsigned seg, unsigned off) { return cl_mem[ci(seg)][off & 0xFFFF]; }
void os88_poke(unsigned seg, unsigned off, int v) { cl_mem[ci(seg)][off & 0xFFFF] = (unsigned char)v; }
static void claim_bounds(int i, unsigned off, unsigned n)
{
    if ((unsigned long)off + n > ((unsigned long)cl_kb[i] << 10) && cl_kb[i] < 64) {
        printf("HARNESS: access %u+%u past a %uKB claim\n", off, n, cl_kb[i]); exit(1);
    }
}
int  rc_rd(unsigned a) { return zram[a & 0xFFFF]; }
int  rc_rd16(unsigned a) { return zram[a & 0xFFFF] | (zram[(a + 1) & 0xFFFF] << 8); }
void rc_wr(unsigned a, int v) { zram[a & 0xFFFF] = (unsigned char)v; }
void rc_wr16(unsigned a, int v) { zram[a & 0xFFFF] = (unsigned char)v; zram[(a + 1) & 0xFFFF] = (unsigned char)(v >> 8); }
void rc_zcopy_in(unsigned zaddr, const void *src, unsigned n)
{ unsigned i; for (i = 0; i < n; i++) zram[(zaddr + i) & 0xFFFF] = ((const unsigned char *)src)[i]; }
void rc_zcopy_out(void *dst, unsigned zaddr, unsigned n)
{ unsigned i; for (i = 0; i < n; i++) ((unsigned char *)dst)[i] = zram[(zaddr + i) & 0xFFFF]; }
void rc_zzcopy_in(unsigned zaddr, unsigned seg, unsigned off, unsigned n)
{ int c = ci(seg); unsigned i; claim_bounds(c, off, n); for (i = 0; i < n; i++) zram[(zaddr + i) & 0xFFFF] = cl_mem[c][(off + i) & 0xFFFF]; }
void rc_zzcopy_out(unsigned seg, unsigned off, unsigned zaddr, unsigned n)
{ int c = ci(seg); unsigned i; claim_bounds(c, off, n); for (i = 0; i < n; i++) cl_mem[c][(off + i) & 0xFFFF] = zram[(zaddr + i) & 0xFFFF]; }
void rc_zfill(unsigned zaddr, int v, unsigned n)
{ unsigned i; for (i = 0; i < n; i++) zram[(zaddr + i) & 0xFFFF] = (unsigned char)v; }
void rc_sfill(unsigned seg, unsigned off, int v, unsigned n)
{ int c = ci(seg); unsigned i; claim_bounds(c, off, n); for (i = 0; i < n; i++) cl_mem[c][(off + i) & 0xFFFF] = (unsigned char)v; }

/* --- the fake folder tree ---------------------------------------------------- */
#define MAXF 64
struct ffile { char name[13]; unsigned char *data; unsigned long size; int live; };
struct ffold { unsigned clus; unsigned parent; char name[13]; struct ffile f[MAXF]; int live; };
static struct ffold folders[32];
static int nfold;
static unsigned std_clus, next_clus = 5;
static unsigned a0_clus;
static int ferr, n_find, n_find_a0, n_reads, n_writes, n_deletes, n_renames, n_mkdirs, mkdir_full;
static char last_toast[128];
static int lock_depth, n_toast_locked, n_flush;    /* rc_say_now: the toast
                                                    * under the lock, the flush */
static void rc_flush(void *w) { (void)w; n_flush++; }
void os88_gfx_lock(void)   { lock_depth++; }
void os88_gfx_unlock(void) { lock_depth--; }
int  os88_wm_clip_set(void *win) { (void)win; return 0; }
static struct ffold *fold(unsigned clus) { int i; for (i = 0; i < nfold; i++) if (folders[i].live && folders[i].clus == clus) return &folders[i]; return 0; }
static struct ffold *mkfold(unsigned parent, const char *name)
{
    struct ffold *fo = &folders[nfold++];
    memset(fo, 0, sizeof(*fo));
    fo->clus = next_clus++; fo->parent = parent; strcpy(fo->name, name); fo->live = 1;
    return fo;
}
static struct ffile *ffind(struct ffold *fo, const char *name)
{ int i; for (i = 0; i < MAXF; i++) if (fo->f[i].live && strcmp(fo->f[i].name, name) == 0) return &fo->f[i]; return 0; }
static struct ffile *fput(struct ffold *fo, const char *name, const void *data, unsigned long size)
{
    struct ffile *f = ffind(fo, name);
    int i;
    if (!f) { for (i = 0; i < MAXF && fo->f[i].live; i++) ; if (i == MAXF) return 0; f = &fo->f[i]; memset(f, 0, sizeof(*f)); strcpy(f->name, name); f->live = 1; }
    free(f->data); f->data = malloc(size ? size : 1); if (size) memcpy(f->data, data, size); f->size = size;
    return f;
}
static struct ffold *subfold(struct ffold *fo, const char *name)
{ int i; for (i = 0; i < nfold; i++) if (folders[i].live && folders[i].parent == fo->clus && strcmp(folders[i].name, name) == 0) return &folders[i]; return 0; }
static struct ffold *path(const char *p)             /* "A/0" from the root */
{
    struct ffold *fo = fold(0);
    char buf[64], *t;
    strcpy(buf, p);
    for (t = strtok(buf, "/"); t && fo; t = strtok(0, "/")) fo = subfold(fo, t);
    return fo;
}

int os88_ferr(void) { return ferr; }
void os88_file_here(struct os88_place *p) { p->clus = std_clus; p->vol = 0; }
int os88_file_goto_q_mark(unsigned clus, int vol)
{ if (vol != 0 || !fold(clus)) { ferr = OS88_FERR_IO; return -1; } std_clus = clus; ferr = 0; return 0; }
int os88_file_find(int ordinal, struct os88_find *f)
{
    struct ffold *fo = fold(std_clus);
    int i, k = 0;
    n_find++;
    if (std_clus == a0_clus) n_find_a0++;   /* the walks of A\0 itself, apart
                                             * from the place table's short ones */
    memset(f, 0, sizeof(*f));
    if (std_clus != 0) { if (ordinal == k) { strcpy(f->name, ".."); f->type = OS88_FT_UP; return k + 1; } k++; }
    for (i = 0; i < nfold; i++) if (folders[i].live && folders[i].parent == std_clus && folders[i].clus != 0) {
        if (ordinal == k) { strcpy(f->name, folders[i].name); f->type = OS88_FT_DIR; f->clus = folders[i].clus; return k + 1; }
        k++;
    }
    for (i = 0; i < MAXF; i++) if (fo->f[i].live) {
        if (ordinal == k) {
            strcpy(f->name, fo->f[i].name); f->type = OS88_FT_FILE;
            f->size_lo = (unsigned)(fo->f[i].size & 0xFFFF); f->size_hi = (unsigned)(fo->f[i].size >> 16);
            return k + 1;
        }
        k++;
    }
    ferr = OS88_FERR_NOENT;
    return -1;
}
unsigned os88_file_read_seg(const char *name, unsigned seg, unsigned cap)
{
    struct ffile *f = ffind(fold(std_clus), name);
    int c = ci(seg);
    n_reads++;
    if (!f) { ferr = OS88_FERR_NOENT; return 0; }
    if (f->size > cap) { ferr = OS88_FERR_BIG; return 0; }
    memcpy(cl_mem[c], f->data, f->size);
    ferr = 0;
    return (unsigned)f->size;
}
static int n_probes;                     /* os88_file_read: _CheckSUB's by-name look-up */
unsigned os88_file_read(const char *name, void *buf, unsigned cap)
{
    struct ffile *f = ffind(fold(std_clus), name);
    n_probes++;
    if (!f) { ferr = OS88_FERR_NOENT; return 0; }
    if (f->size > cap) { ferr = OS88_FERR_BIG; return 0; }   /* decided from the entry: no data read */
    memcpy(buf, f->data, f->size);
    ferr = 0;
    return (unsigned)f->size;
}
int os88_file_write_seg(const char *name, unsigned seg, unsigned count)
{
    int c = ci(seg);
    n_writes++;
    if (!fput(fold(std_clus), name, cl_mem[c], count)) { ferr = OS88_FERR_DIRFULL; return -1; }
    ferr = 0;
    return 0;
}
int os88_file_delete(const char *name)
{
    struct ffile *f = ffind(fold(std_clus), name);
    n_deletes++;
    if (!f) { ferr = OS88_FERR_NOENT; return -1; }
    free(f->data); f->data = 0; f->live = 0; ferr = 0;
    return 0;
}
int os88_file_rename(const char *o, const char *n)
{
    struct ffold *fo = fold(std_clus);
    struct ffile *f = ffind(fo, o);
    n_renames++;
    if (!f) { ferr = OS88_FERR_NOENT; return -1; }
    if (ffind(fo, n)) { ferr = OS88_FERR_EXIST; return -1; }
    strcpy(f->name, n); ferr = 0;
    return 0;
}
int os88_file_mkdir(const char *name)
{
    struct ffold *fo = fold(std_clus);
    n_mkdirs++;
    if (mkdir_full) { ferr = OS88_FERR_FULL; return -1; }
    if (subfold(fo, name)) { ferr = OS88_FERR_EXIST; return -1; }
    mkfold(std_clus, name); ferr = 0;
    return 0;
}
int os88_toast(const char *text, int ticks) { strncpy(last_toast, text, sizeof(last_toast) - 1); if (lock_depth) n_toast_locked++; return 0; }
void os88_memset(void *p, int c, unsigned n) { memset(p, c, n); }
void os88_memcpy(void *d, const void *s, unsigned n) { memcpy(d, s, n); }
unsigned os88_strlen(const char *s) { return (unsigned)strlen(s); }
void os88_strcpy(char *dst, const char *src, unsigned cap) { strncpy(dst, src, cap - 1); dst[cap - 1] = 0; }
char *os88_utoa(unsigned v, char *dst6) { sprintf(dst6, "%u", v); return dst6; }

/* --- driving the BDOS as the Z80 does ---------------------------------------- */
static int fails;
#define FCB 0x5C
#define DMA 0x80
static unsigned bdos(int fn, unsigned de)
{
    rc_z.bc = (rc_z.bc & 0xFF00) | (unsigned)fn;
    rc_z.de = de;
    rc_z.pc = 0xED01;
    rc_bdos();
    return rc_z.hl;
}
static void fcb_set(unsigned addr, int dr, const char *n11)
{
    zram[addr] = (unsigned char)dr;
    memcpy(zram + addr + 1, n11, 11);
    memset(zram + addr + 12, 0, 24);
}
static void dma_pattern(int k)
{ int i; for (i = 0; i < 128; i++) zram[DMA + i] = (unsigned char)(k * 7 + i); }
static int dma_is(int k)
{ int i; for (i = 0; i < 128; i++) if (zram[DMA + i] != (unsigned char)(k * 7 + i)) return 0; return 1; }
static void check(int ok, const char *what)
{ if (!ok) { printf("FAIL: %s\n", what); fails++; } }
static void checkeq(unsigned long got, unsigned long want, const char *what)
{ if (got != want) { printf("FAIL: %s: got %lu (0x%lx), wanted %lu\n", what, got, got, want); fails++; } }
static void reset_machine(void)
{
    rc_errwait = 0; rc_status = RC_ST_RUNNING; rc_khead = rc_ktail = 0;
    rc_cdrive = rc_odrive = 0; rc_user = 0; rc_rovec = 0; rc_login = 0; rc_dma = 0x80;
    zram[4] = 0;
    con_clear(); last_toast[0] = 0;
}
static void hostfile(const char *p, const char *name, unsigned long *size, unsigned char **data)
{
    struct ffile *f = ffind(path(p), name);
    *size = f ? f->size : (unsigned long)-1;
    *data = f ? f->data : 0;
}

/* a search that lists everything of the current user, ex '?': the entries
 * for `name` in order */
static unsigned char ents[16][32];
static int list_file(const char *n11, int allext)
{
    int n = 0;
    unsigned r;
    fcb_set(0x100, 0, "???????????");
    if (allext) zram[0x100 + 12] = '?';
    r = bdos(17, 0x100);
    while (r != 0xFF && n < 16) {
        if (rc_errwait) { printf("HARNESS: list_file: an error is pending: %s\n", con); exit(1); }
        if (memcmp(zram + DMA + 1, n11, 11) == 0) memcpy(ents[n++], zram + DMA, 32);
        r = bdos(18, 0x100);
    }
    return n;
}
/* the cache's order is the order of the HOST names, as glob() sorts them */
static void check_order(const char *what)
{
    static char p[13], q[13];
    int i;
    for (i = 1; i < rc_dcc->n; i++) {
        rc_n11_host(rc_dcc->e[i - 1].name, p);
        rc_n11_host(rc_dcc->e[i].name, q);
        if (strcmp(p, q) >= 0) { printf("  %s >= %s\n", p, q); check(0, what); return; }
    }
}
static void mkdata(unsigned char *b, unsigned long n)
{ unsigned long i; for (i = 0; i < n; i++) b[i] = (unsigned char)(i * 13 + (i >> 8)); }

int main(void)
{
    struct ffold *root, *a, *a0, *b, *b0;
    static unsigned char big[140000];
    unsigned long sz;
    unsigned char *dat;
    unsigned r;
    int i, k, n;

    setvbuf(stdout, 0, _IONBF, 0);
    /* the tree: root(0) -> A(5) -> 0(6), 3 absent; B absent (MAKEDISK makes it) */
    root = mkfold(0, ""); root->clus = 0; next_clus = 5;
    a = mkfold(0, "A"); a0 = mkfold(a->clus, "0"); a0_clus = a0->clus;
    fput(root, "RUNCPM.O88", "x", 1); fput(root, "CCP-DR.60K", "y", 1);
    mkdata(big, sizeof(big));
    fput(a0, "S0.BIN", big, 0);      fput(a0, "S127.BIN", big, 127);   fput(a0, "S128.BIN", big, 128);
    fput(a0, "S16384.BIN", big, 16384); fput(a0, "S16385.BIN", big, 16385); fput(a0, "S32768.BIN", big, 32768);
    fput(a0, "S65535.BIN", big, 65535); fput(a0, "S67301.BIN", big, 67301); fput(a0, "S134899.BIN", big, 134899);
    { static char h100[101]; memset(h100, '.', 100); memcpy(h100, "Hello from A0", 13); fput(a0, "HELLO.TXT", h100, 100); }
    for (i = 0; i < 9; i++) { char nm[13]; sprintf(nm, "F%d.DAT", i); fput(a0, nm, big, 300); }
    std_clus = 0;
    rc_fs_init();
    reset_machine();

    /* --- names ---------------------------------------------------------- */
    {
        static char h[13];
        fcb_set(FCB, 0, "HELLO   TXT");
        check(rc_fcb2host(zram + FCB, h) == 1 && strcmp(h, "HELLO.TXT") == 0, "FCB -> HELLO.TXT");
        fcb_set(FCB, 0, "PIP     COM"); rc_fcb2host(zram + FCB, h);
        check(strcmp(h, "PIP.COM") == 0, "PIP.COM");
        fcb_set(FCB, 0, "ab/c       "); rc_fcb2host(zram + FCB, h);
        check(strcmp(h, "AB_C") == 0, "lower case and NOSLASH: AB_C");
        fcb_set(FCB, 0, "A?      TXT");
        check(rc_fcb2host(zram + FCB, h) == 0 && strcmp(h, "A?.TXT") == 0, "a pattern is not unique");
        fcb_set(FCB, '?', "HELLO   TXT"); rc_fcb2host(zram + FCB, h);
        check(strcmp(h, "????????.???") == 0, "dr '?' is every name");
        fcb_set(FCB, 0, "           "); rc_fcb2host(zram + FCB, h);
        check(h[0] == 0, "an empty name is invalid");
        zram[FCB + 1] = (unsigned char)('X' | 0x80); zram[FCB + 2] = 'Y'; rc_fcb2host(zram + FCB, h);
        check(strcmp(h, "XY") == 0, "the high bit is masked");
        rc_host2n11("A.B", h); check(memcmp(h, "A       B  ", 11) == 0, "A.B -> 11 chars");
        rc_host2n11("LONGNAME.EXT", h); check(memcmp(h, "LONGNAMEEXT", 11) == 0, "8.3 -> 11 chars");
        rc_n11_host("A       B  ", h); check(strcmp(h, "A.B") == 0, "11 chars -> A.B");
        rc_n11_host("NOEXT      ", h); check(strcmp(h, "NOEXT") == 0, "no extension, no dot");
        check(rc_match("HELLO   TXT", "HELLO   TXT") && rc_match("HELLO   TXT", "H???????TXT") &&
              rc_match("HELLO   TXT", "???????????") && !rc_match("HELLO   TXT", "HELLO   COM"), "match()");
    }

    /* --- _CheckSUB before anything has been listed: a by-name look-up, no walk */
    n_find = 0; n_find_a0 = 0; n_probes = 0;
    checkeq(bdos(13, 0), 0, "DRV_ALLRESET at the first boot: no $$$.SUB");
    checkeq((unsigned long)n_find_a0, 0, "...answered with NO walk of A\\0 (nothing on the boot path)");
    check(n_find > 0 && n_find < 8, "...only the place table's short walks of the root and A");
    checkeq((unsigned long)n_probes, 1, "...by ONE by-name look-up");
    checkeq(bdos(13, 0), 0, "DRV_ALLRESET again (every warm boot asks)");
    checkeq((unsigned long)n_probes, 1, "...from the remembered fact: no look-up");
    { struct ffold *a4 = mkfold(a->clus, "4"); fput(a4, "$$$.SUB", "\004DIR", 5); }
    rc_user = 4;
    checkeq(bdos(13, 0), 0xFF, "user 4, never listed, holds a $$$.SUB: 0xFF by name");
    checkeq((unsigned long)n_probes, 2, "...one look-up"); checkeq((unsigned long)n_find_a0, 0, "...no walk");
    rc_user = 6;
    checkeq(bdos(13, 0), 0, "user 6 has no folder: 0");
    checkeq((unsigned long)n_probes, 2, "...and no look-up");
    rc_user = 0;
    n_find = 0;

    /* --- the directory: DIR lists A\0 once walked, then from the cache ------ */
    n_find = 0;
    fcb_set(FCB, 0, "???????????");
    r = bdos(17, FCB);
    checkeq(r, 0, "SFIRST *.* finds the first entry (A = 0)");
    n = 1;
    while (bdos(18, FCB) != 0xFF) n++;
    checkeq((unsigned long)n, 19, "DIR lists the 19 files of A\\0");
    check(n_find > 19, "the first DIR walked the folder (os88_file_find)");
    check(zram[0xED10] == 1 && memcmp(zram + 0xED11, rc_s_name, 11) == 0, "tmpFCB holds the drive and the last name found");
    n_find = 0;
    fcb_set(FCB, 0, "???????????");
    n = (bdos(17, FCB) == 0) ? 1 : 0;
    while (bdos(18, FCB) != 0xFF) n++;
    checkeq((unsigned long)n, 19, "the second DIR lists the same 19");
    checkeq((unsigned long)n_find, 0, "...with NO walk: the cache");
    check_order("the cache is in name order (glob's order: the host name)");
    fcb_set(FCB, 0, "???????????");
    bdos(17, FCB);
    check(memcmp(zram + DMA + 1, "F0      DAT", 11) == 0, "the first DIR entry is the first name: F0.DAT");
    /* dr = the current drive by 0; the pattern by name */
    fcb_set(FCB, 1, "S1????? BIN");
    n = (bdos(17, FCB) == 0) ? 1 : 0;
    while (bdos(18, FCB) != 0xFF) n++;
    checkeq((unsigned long)n, 5, "S1?????.BIN matches S127, S128, S16384, S16385, S134899");

    /* --- _mockupDirEntry: extents, rc, the chain -------------------------- */
    {
        /* size -> {entries, [ex, s2, rc, first al, nblocks] ...}, by hand from
         * disk.h 229-267 with exm 1, 4KB blocks, 8 16-bit al words, first block 8 */
        struct exp { const char *n11; int nent; int ex[6], s2[6], rc[6], al0[6], nb[6]; } ex[] = {
            { "S0      BIN", 1, {0}, {0}, {0}, {8}, {0} },
            { "S127    BIN", 1, {0}, {0}, {1}, {8}, {1} },
            { "S128    BIN", 1, {0}, {0}, {1}, {8}, {1} },
            { "S16384  BIN", 1, {0}, {0}, {128}, {8}, {4} },
            { "S16385  BIN", 1, {1}, {0}, {1}, {8}, {5} },
            { "S32768  BIN", 1, {1}, {0}, {128}, {8}, {8} },
            { "S65535  BIN", 2, {1, 3}, {0, 0}, {128, 128}, {8, 16}, {8, 8} },
            { "S67301  BIN", 3, {1, 3, 4}, {0, 0, 0}, {128, 128, 14}, {8, 16, 24}, {8, 8, 1} },
            { "S134899 BIN", 5, {1, 3, 5, 7, 8}, {0, 0, 0, 0, 0}, {128, 128, 128, 128, 30}, {8, 16, 24, 32, 40}, {8, 8, 8, 8, 1} },
        };
        for (i = 0; i < 9; i++) {
            char what[80];
            n = list_file(ex[i].n11, 1);
            sprintf(what, "%.11s: %d entries with EX = '?'", ex[i].n11, ex[i].nent);
            checkeq((unsigned long)n, (unsigned long)ex[i].nent, what);
            for (k = 0; k < n && k < ex[i].nent; k++) {
                int j, blocks = 0;
                for (j = 0; j < 8; j++) if (ents[k][16 + 2 * j] | ents[k][17 + 2 * j]) blocks++;
                sprintf(what, "%.11s entry %d: ex/s2/rc/al0/blocks = %d/%d/%d/%d/%d, wanted %d/%d/%d/%d/%d", ex[i].n11, k,
                        ents[k][12], ents[k][14], ents[k][15], ents[k][16] | (ents[k][17] << 8), blocks,
                        ex[i].ex[k], ex[i].s2[k], ex[i].rc[k], ex[i].al0[k], ex[i].nb[k]);
                check(ents[k][12] == ex[i].ex[k] && ents[k][14] == ex[i].s2[k] && ents[k][15] == ex[i].rc[k] &&
                      blocks == ex[i].nb[k] && (blocks == 0 || (ents[k][16] | (ents[k][17] << 8)) == ex[i].al0[k]) &&
                      ents[k][0] == 0 && ents[k][13] == 0, what);
            }
            n = list_file(ex[i].n11, 0);
            sprintf(what, "%.11s: ONE entry without EX = '?'", ex[i].n11);
            checkeq((unsigned long)n, 1, what);
            check(ents[0][12] == ex[i].ex[0] && ents[0][15] == ex[i].rc[0], "...the first of the chain");
        }
        /* the user code in the entry */
        rc_user = 3;
        n = list_file("S0      BIN", 0);
        checkeq((unsigned long)n, 0, "user 3 (absent folder) lists nothing");
        rc_user = 0;
    }

    /* --- files above 65,535 bytes: listed, refused ------------------------ */
    fcb_set(FCB, 0, "S67301  BIN");
    checkeq(bdos(15, FCB), 0xFF, "F_OPEN of a 67,301-byte file is refused (after a refill: the fill's scratch is its own)");
    check(strstr(last_toast, "S67301.BIN>64K") != 0, "...with the toast naming it");
    fcb_set(FCB, 0, "S134899 BIN");
    checkeq(bdos(15, FCB), 0xFF, "F_OPEN of a 134,899-byte file is refused");
    checkeq(bdos(35, FCB), 0, "F_SIZE of it answers");
    checkeq((unsigned long)(zram[FCB + 33] | (zram[FCB + 34] << 8) | (zram[FCB + 35] << 16)), 1054, "...1054 records");
    fcb_set(FCB, 0, "S65535  BIN");
    checkeq(bdos(15, FCB), 0, "F_OPEN of a 65,535-byte file works");
    checkeq(zram[FCB + 15], 128, "...rc = 128"); checkeq(zram[FCB + 14], 0x80, "...s2 = 0x80 unmodified");
    zram[FCB + 33] = 0xFF; zram[FCB + 34] = 1;   /* record 511: the tail */
    checkeq(bdos(33, FCB), 0, "READRAND record 511 (the last 127 bytes)");
    check(zram[DMA] == big[65408] && zram[DMA + 126] == big[65534] && zram[DMA + 127] == 0x1A, "...bytes 65408.., then ^Z");
    zram[FCB + 33] = 0; zram[FCB + 34] = 2;
    checkeq(bdos(33, FCB), 1, "READRAND record 512 is past the end");
    checkeq(bdos(16, FCB), 0, "F_CLOSE (unmodified)");
    checkeq((unsigned long)live_claims(), 1, "...and the entry is KEPT, clean (a reopen reads nothing)");
    n_reads = 0;
    checkeq(bdos(15, FCB), 0, "F_OPEN S65535.BIN again"); checkeq((unsigned long)n_reads, 0, "...no read: from the table");
    checkeq(bdos(16, FCB), 0, "close");
    rc_fs_flush_all();
    checkeq((unsigned long)live_claims(), 0, "the program's end releases it");

    /* --- sequential write, close, read; the partial record ---------------- */
    fcb_set(FCB, 0, "NEW     TXT");
    checkeq(bdos(22, FCB), 0, "F_MAKE NEW.TXT");
    check(zram[FCB + 12] == 0 && zram[FCB + 14] == 0 && zram[FCB + 15] == 0 && zram[FCB + 32] == 0, "...ex/s2/rc/cr zeroed");
    n = list_file("NEW     TXT", 0);
    checkeq((unsigned long)n, 1, "the made file is in the directory at once");
    for (k = 0; k < 3; k++) { dma_pattern(k); checkeq(bdos(21, FCB), 0, "F_WRITE seq"); }
    checkeq(zram[FCB + 32], 3, "cr = 3 after 3 writes"); checkeq(zram[FCB + 15], 3, "rc = 3");
    check(!(zram[FCB + 14] & 0x80), "s2 modified");
    hostfile("A/0", "NEW.TXT", &sz, &dat);
    check(sz == (unsigned long)-1 || sz == 0, "nothing on the disk before F_CLOSE (whole-file model)");
    checkeq(bdos(16, FCB), 0, "F_CLOSE writes it back");
    hostfile("A/0", "NEW.TXT", &sz, &dat);
    checkeq(sz, 384, "NEW.TXT is 384 bytes on the disk");
    check(dat && dat[0] == 0 && dat[128] == 7 && dat[256] == 14 && dat[383] == (unsigned char)(14 + 127), "...holding the three records");
    checkeq((unsigned long)live_claims(), 1, "the claim is KEPT on close, clean");
    fcb_set(FCB, 0, "NEW     TXT");
    n_reads = 0;
    checkeq(bdos(15, FCB), 0, "F_OPEN NEW.TXT"); checkeq(zram[FCB + 15], 3, "rc = 3 on open");
    checkeq((unsigned long)n_reads, 0, "...without a read (SAVE then LOAD costs no disk)");
    for (k = 0; k < 3; k++) { checkeq(bdos(20, FCB), 0, "F_READ seq"); check(dma_is(k), "...the record"); }
    checkeq(bdos(20, FCB), 1, "the 4th F_READ is 1: end of file");
    checkeq(bdos(16, FCB), 0, "F_CLOSE");
    checkeq(bdos(20, FCB), 1, "a read after the close still answers (by name, upstream's way): EOF");
    zram[FCB + 32] = 1;
    checkeq(bdos(20, FCB), 0, "...and record 1 of it"); check(dma_is(1), "...is record 1");
    rc_fs_flush_all();                           /* the program ends: released */
    checkeq((unsigned long)live_claims(), 0, "the warm boot releases the kept entries");
    fcb_set(FCB, 0, "NEW     TXT"); zram[FCB + 32] = 1;
    n_reads = 0;
    checkeq(bdos(20, FCB), 0, "the next program's read of it"); check(dma_is(1), "...is record 1");
    checkeq((unsigned long)n_reads, 1, "...read again (reopened by the read)");
    checkeq((unsigned long)live_claims(), 1, "(one claim)");
    /* the partial record: HELLO.TXT is 100 bytes */
    fcb_set(FCB, 0, "HELLO   TXT");
    checkeq(bdos(15, FCB), 0, "F_OPEN HELLO.TXT"); checkeq(zram[FCB + 15], 1, "rc = 1 for 100 bytes");
    checkeq(bdos(20, FCB), 0, "F_READ its one record");
    check(memcmp(zram + DMA, "Hello from A0", 13) == 0 && zram[DMA + 99] == '.' && zram[DMA + 100] == 0x1A && zram[DMA + 127] == 0x1A, "100 bytes then ^Z");
    checkeq(bdos(20, FCB), 1, "then EOF");
    checkeq(bdos(35, FCB), 0, "F_SIZE HELLO.TXT"); checkeq(zram[FCB + 33], 1, "...1 record");
    checkeq(bdos(16, FCB), 0, "close it");
    /* a file that is not there: 0x10 (_sys_readseq's fopen failure) */
    fcb_set(FCB, 0, "NOWHERE TXT");
    checkeq(bdos(15, FCB), 0xFF, "F_OPEN of a missing file");
    checkeq(bdos(20, FCB), 0x10, "F_READ of a missing file is 0x10");
    checkeq(bdos(21, FCB), 0x10, "F_WRITE of a file never made is 0x10");

    /* --- random records: the gap, the size, the FCB positions ------------- */
    fcb_set(FCB, 0, "NEW     TXT");
    checkeq(bdos(15, FCB), 0, "F_OPEN NEW.TXT again");
    zram[FCB + 33] = 5; dma_pattern(9);
    checkeq(bdos(34, FCB), 0, "F_WRITERAND record 5");
    check(zram[FCB + 32] == 5 && zram[FCB + 12] == 0 && zram[FCB + 14] == 0, "cr/ex/s2 = 5/0/0, unmodified flag cleared");
    zram[FCB + 33] = 4;
    checkeq(bdos(33, FCB), 0, "F_READRAND record 4 (the gap) reads");
    check(zram[DMA] == 0 && zram[DMA + 127] == 0, "...zeros");
    zram[FCB + 33] = 6;
    checkeq(bdos(33, FCB), 1, "F_READRAND record 6 is unwritten: 1");
    checkeq(zram[FCB + 32], 6, "...and the FCB moved there anyway (disk.h 730)");
    zram[FCB + 35] = 1; zram[FCB + 33] = 3;
    checkeq(bdos(33, FCB), 1, "F_READRAND with r2 != 0: 1 (upstream's fseek past the end succeeds, fread moves nothing)");
    checkeq(zram[FCB + 32], 3, "...and cr/ex/s2 stepped from the record's low bits (disk.h 730-733)");
    zram[FCB + 35] = 0;
    checkeq(bdos(35, FCB), 0, "F_SIZE"); checkeq(zram[FCB + 33], 6, "...6 records now (in the table, unsaved)");
    zram[FCB + 32] = 3; zram[FCB + 12] = 1; zram[FCB + 14] = 0;
    checkeq(bdos(36, FCB), 0, "F_RANDREC"); checkeq((unsigned long)(zram[FCB + 33] | (zram[FCB + 34] << 8)), 131, "...cr 3 ex 1 = 131");
    zram[FCB + 32] = 0; zram[FCB + 12] = 0; zram[FCB + 14] = 3;
    checkeq(bdos(36, FCB), 0, "F_RANDREC s2"); checkeq((unsigned long)(zram[FCB + 33] | (zram[FCB + 34] << 8)), 3 * 4096, "...s2 3 = 12288");
    checkeq(bdos(16, FCB), 0, "F_CLOSE after the random write");
    hostfile("A/0", "NEW.TXT", &sz, &dat);
    checkeq(sz, 768, "NEW.TXT is 768 bytes: 6 records");
    check(dat && dat[383] == (unsigned char)(14 + 127) && dat[384] == 0 && dat[639] == 0 && dat[640] == 63 && dat[767] == (unsigned char)(63 + 127), "...gap zero, record 5 in place");
    /* the write past what the model holds */
    fcb_set(FCB, 0, "NEW     TXT");
    zram[FCB + 33] = 0xFF; zram[FCB + 34] = 1;
    last_toast[0] = 0;
    checkeq(bdos(34, FCB), 6, "F_WRITERAND record 511 is refused (6)");
    check(strstr(last_toast, "NEW.TXT>64K") != 0, "...with the toast");
    zram[FCB + 33] = 0xFE; zram[FCB + 34] = 1; dma_pattern(2);
    checkeq(bdos(34, FCB), 0, "F_WRITERAND record 510 is the last one allowed");
    check(n_regrows > 0, "the claim was regrown for record 510 (and MOVED by the harness)");
    checkeq(bdos(16, FCB), 0, "close");
    hostfile("A/0", "NEW.TXT", &sz, &dat);
    checkeq(sz, 65408, "NEW.TXT is now 65,408 bytes");
    check(dat && dat[65280] == 14 && dat[65407] == (unsigned char)(14 + 127) && dat[768] == 0, "...record 510 at the end, zeros between");
    fcb_set(FCB, 0, "NEW     TXT");
    checkeq(bdos(15, FCB), 0, "reopen the 65,408-byte file"); checkeq(zram[FCB + 15], 128, "rc 128");
    checkeq(bdos(35, FCB), 0, "F_SIZE"); checkeq((unsigned long)(zram[FCB + 33] | (zram[FCB + 34] << 8)), 511, "...511 records");
    zram[FCB + 12] = 3; zram[FCB + 32] = 127;    /* record 511: ex 3, cr 127 */
    last_toast[0] = 0;
    checkeq(bdos(21, FCB), 0xFF, "F_WRITE seq of record 511 is refused with 0xFF (upstream's failed fwrite)");
    check(strstr(last_toast, "NEW.TXT>64K") != 0, "...with the toast");
    checkeq(bdos(16, FCB), 0, "close");
    /* the sequential write over records: extent roll */
    fcb_set(FCB, 0, "SEQ     DAT");
    checkeq(bdos(22, FCB), 0, "F_MAKE SEQ.DAT");
    for (k = 0; k < 130; k++) { dma_pattern(k & 7); if (bdos(21, FCB) != 0) { check(0, "F_WRITE seq of 130 records"); break; } }
    check(zram[FCB + 12] == 1 && zram[FCB + 32] == 2 && zram[FCB + 15] == 3, "after 130 records: ex 1, cr 2, rc 3 (disk.h 651-659)");
    checkeq(bdos(16, FCB), 0, "close");
    hostfile("A/0", "SEQ.DAT", &sz, &dat); checkeq(sz, 130 * 128, "SEQ.DAT is 130 records");
    fcb_set(FCB, 0, "SEQ     DAT");
    checkeq(bdos(15, FCB), 0, "reopen"); checkeq(zram[FCB + 15], 128, "rc = min(records, 128)");
    for (k = 0; k < 130; k++) if (bdos(20, FCB) != 0) { check(0, "F_READ 130 records back"); break; }
    check(dma_is(129 & 7), "the 130th record");
    checkeq(bdos(20, FCB), 1, "then EOF");
    checkeq(bdos(16, FCB), 0, "close");
    /* F_MAKE of an existing file keeps its bytes (fopen "a") */
    fcb_set(FCB, 0, "SEQ     DAT");
    checkeq(bdos(22, FCB), 0, "F_MAKE of an existing file");
    checkeq(bdos(35, FCB), 0, "F_SIZE"); checkeq(zram[FCB + 33], 130, "...still 130 records: upstream's fopen \"a\"");
    checkeq(bdos(16, FCB), 0, "close");
    hostfile("A/0", "SEQ.DAT", &sz, &dat); checkeq(sz, 130 * 128, "...and on the disk");

    /* --- the open-file table: eight, eviction, the ninth ------------------ */
    rc_fs_flush_all();                           /* nothing kept from above */
    n_frees = 0;
    for (i = 0; i < 8; i++) { char nm[12]; sprintf(nm, "F%d      DAT", i); fcb_set(0x200 + i * 36, 0, nm); checkeq(bdos(15, 0x200 + i * 36), 0, "open F0..F7"); }
    checkeq((unsigned long)live_claims(), 8, "8 files, 8 claims");
    fcb_set(0x200 + 8 * 36, 0, "F8      DAT");
    checkeq(bdos(15, 0x200 + 8 * 36), 0, "the 9th open evicts a clean entry");
    checkeq((unsigned long)live_claims(), 8, "...still 8 claims");
    checkeq(bdos(20, 0x200), 0, "F0 reads again after eviction (reloaded by name)");
    for (i = 0; i < 8; i++) { dma_pattern(i); bdos(21, 0x200 + i * 36); }   /* F0..F7 dirty (F8 evicted, reloaded) */
    checkeq((unsigned long)live_claims(), 8, "8 dirty files held");
    fcb_set(0x100, 0, "F8      DAT");
    reset_machine();
    r = bdos(15, 0x100);
    check(rc_errwait == 1 && strstr(con, "Bdos Err on A: \r\nCP/M ERR") != 0, "the 9th DIRTY open errs 'Bdos Err on A: CP/M ERR'");
    check(strcmp(last_toast, "RunCPM: 8 files open") == 0, "...with the toast");
    rc_key_push('x');
    bdos(15, 0x100);                             /* the retried trap: the key ends the wait */
    check(rc_errwait == 0 && rc_status == RC_ST_RESTART, "the key ends the wait with a warm boot");
    rc_fs_flush_all();                           /* what the warm boot does */
    checkeq((unsigned long)live_claims(), 0, "the warm boot released every claim");
    for (i = 0; i < 8; i++) { char nm[13]; sprintf(nm, "F%d.DAT", i); hostfile("A/0", nm, &sz, &dat); check(sz == 300 && dat[0] == (unsigned char)(i * 7), "...and wrote the dirty ones back"); }
    reset_machine();

    /* --- $$$.SUB, the way SUBMIT.COM and the DRI CCP use it -------------- */
    fcb_set(FCB, 0, "$$$     SUB");
    checkeq(bdos(22, FCB), 0, "SUBMIT: F_MAKE $$$.SUB");
    memset(zram + DMA, 0, 128); zram[DMA] = 4; memcpy(zram + DMA + 1, "DIR2", 4);   /* the LAST line first */
    checkeq(bdos(21, FCB), 0, "...record 0 = the last line");
    memset(zram + DMA, 0, 128); zram[DMA] = 4; memcpy(zram + DMA + 1, "DIR1", 4);
    checkeq(bdos(21, FCB), 0, "...record 1 = the first line");
    checkeq(bdos(16, FCB), 0, "...close");
    checkeq(bdos(13, 0), 0xFF, "DRV_ALLRESET answers 0xFF: a $-file is on A: (_CheckSUB)");
    for (k = 2; k >= 1; k--) {
        char want[8];
        fcb_set(RC_BATCHFCB, 0, "$$$     SUB");
        checkeq(bdos(15, RC_BATCHFCB), 0, "the CCP opens $$$.SUB");
        checkeq(zram[RC_BATCHFCB + 15], (unsigned long)k, "...rc = lines left");
        zram[RC_BATCHFCB + 32] = (unsigned char)(k - 1);
        checkeq(bdos(20, RC_BATCHFCB), 0, "...reads the last record");
        sprintf(want, "DIR%d", 3 - k);
        check(zram[DMA] == 4 && memcmp(zram + DMA + 1, want, 4) == 0, "...which is the next line in order");
        zram[RC_BATCHFCB + 14] = 0;              /* s2 = 0, rc-- (CCP-DR.60K 0xE56D) */
        zram[RC_BATCHFCB + 15]--;
        checkeq(bdos(16, RC_BATCHFCB), 0, "...and closes it");
        hostfile("A/0", "$$$.SUB", &sz, &dat);
        checkeq(sz, (unsigned long)(k - 1) * 128, "$$$.SUB is truncated to rc records on the disk");
    }
    checkeq(bdos(19, RC_BATCHFCB), 0, "the CCP deletes the empty $$$.SUB");
    checkeq(bdos(13, 0), 0, "DRV_ALLRESET answers 0 now");
    hostfile("A/0", "$$$.SUB", &sz, &dat); check(sz == (unsigned long)-1, "...gone from the disk");

    /* --- USER n makes the folder; files live there ------------------------ */
    n_mkdirs = 0;
    checkeq(bdos(32, 0x0003), 0, "F_USERNUM set 3");
    checkeq((unsigned long)n_mkdirs, 1, "...made A\\3 (_SetUser -> _MakeUserDir)");
    check(path("A/3") != 0, "A\\3 exists");
    checkeq(bdos(32, 0x00FF), 3, "F_USERNUM get = 3");
    fcb_set(FCB, 0, "U3      TXT");
    checkeq(bdos(22, FCB), 0, "F_MAKE in user 3"); dma_pattern(1); bdos(21, FCB); checkeq(bdos(16, FCB), 0, "close");
    hostfile("A/3", "U3.TXT", &sz, &dat); checkeq(sz, 128, "U3.TXT is in A\\3");
    hostfile("A/0", "U3.TXT", &sz, &dat); check(sz == (unsigned long)-1, "...and not in A\\0");
    n = list_file("U3      TXT", 0); checkeq((unsigned long)n, 1, "user 3's DIR lists it");
    check(ents[0][0] == 3, "...with user code 3 in the entry");
    bdos(32, 0x0000);
    checkeq((unsigned long)n_mkdirs, 1, "USER 0 makes nothing (A\\0 exists)");
    n = list_file("U3      TXT", 0); checkeq((unsigned long)n, 0, "user 0's DIR does not");
    /* the all-users search (dr = '?') walks every area */
    {
        int want = 0, u3 = 0;
        for (i = 0; i < MAXF; i++) { want += a0->f[i].live; want += path("A/3")->f[i].live; want += path("A/4")->f[i].live; }
        fcb_set(0x100, '?', "???????????");
        n = 0; r = bdos(17, 0x100);
        while (r != 0xFF && n < 100) { if (zram[DMA] == 3) u3++; n++; r = bdos(18, 0x100); }
        checkeq((unsigned long)n, (unsigned long)want, "dr '?' lists A\\0's files, A\\3's and A\\4's");
        checkeq((unsigned long)u3, 1, "...one of them with user code 3");
    }
    /* a full disk: USER 5 cannot make its folder, and the area reads empty */
    mkdir_full = 1;
    n_mkdirs = 0;
    bdos(32, 0x0005);
    check(path("A/5") == 0, "USER 5 on a full disk: no folder");
    checkeq((unsigned long)n_mkdirs, 1, "...one os88_file_mkdir");
    fcb_set(FCB, 0, "X       TXT");
    checkeq(bdos(22, FCB), 0xFF, "F_MAKE in an absent area is refused");
    fcb_set(FCB, 0, "???????????");
    checkeq(bdos(17, FCB), 0xFF, "...and it lists nothing");
    bdos(32, 0x0005); bdos(32, 0x0005);          /* two warm boots' _SetUser */
    checkeq((unsigned long)n_mkdirs, 1, "two warm boots in user 5: the refusal is REMEMBERED, no mkdir");
    mkdir_full = 0;
    bdos(32, 0x0005);
    checkeq((unsigned long)n_mkdirs, 1, "...even with room now (nothing said the disk changed)");
    bdos(32, 0x0000);
    fcb_set(FCB, 0, "T1      TMP"); bdos(22, FCB); bdos(16, FCB);
    fcb_set(FCB, 0, "T1      TMP"); checkeq(bdos(19, FCB), 0, "ERA frees something...");
    bdos(32, 0x0005);
    checkeq((unsigned long)n_mkdirs, 2, "...so USER 5 tries once more, and makes A\\5");
    check(path("A/5") != 0, "A\\5 exists now");
    bdos(32, 0x0000);

    /* --- delete with a pattern; rename --------------------------------- */
    fcb_set(FCB, 0, "T1      TMP"); bdos(22, FCB); bdos(16, FCB);
    fcb_set(FCB, 0, "T2      TMP"); bdos(22, FCB); dma_pattern(3); bdos(21, FCB);   /* open and dirty */
    fcb_set(0x100, 0, "T?      TMP");
    checkeq(bdos(19, 0x100), 0, "F_DELETE T?.TMP");
    hostfile("A/0", "T1.TMP", &sz, &dat); check(sz == (unsigned long)-1, "T1.TMP gone");
    hostfile("A/0", "T2.TMP", &sz, &dat); check(sz == (unsigned long)-1, "T2.TMP gone (it was open: dropped, not written)");
    checkeq(bdos(17, 0x100), 0xFF, "...none left in the directory");
    checkeq(bdos(19, 0x100), 0xFF, "deleting nothing is 0xFF");
    fcb_set(FCB, 0, "NEW     TXT"); memcpy(zram + FCB + 17, "OLD     TXT", 11); zram[FCB + 16] = 9;
    checkeq(bdos(23, FCB), 0, "F_RENAME NEW.TXT -> OLD.TXT");
    checkeq(zram[FCB + 16], 0, "...the new name's drive byte is the old one's");
    hostfile("A/0", "OLD.TXT", &sz, &dat); checkeq(sz, 65408, "OLD.TXT on the disk");
    hostfile("A/0", "NEW.TXT", &sz, &dat); check(sz == (unsigned long)-1, "NEW.TXT gone");
    n = list_file("OLD     TXT", 0); checkeq((unsigned long)n, 1, "the cache says OLD.TXT");
    n = list_file("NEW     TXT", 0); checkeq((unsigned long)n, 0, "...and no NEW.TXT");
    check_order("the cache is STILL in name order after REN");
    check(rc_dcc->n > 2 && memcmp(rc_dcc->e[0].name, "OLD     TXT", 11) != 0, "...OLD.TXT did not take NEW.TXT's row");
    fcb_set(FCB, 0, "OLD     TXT"); memcpy(zram + FCB + 17, "SEQ     DAT", 11);
    checkeq(bdos(23, FCB), 0xFF, "renaming onto an existing name is refused");
    fcb_set(FCB, 0, "SEQ     DAT"); bdos(15, FCB); dma_pattern(5); bdos(21, FCB);   /* open, dirty */
    memcpy(zram + FCB + 17, "SEQ2    DAT", 11);
    checkeq(bdos(23, FCB), 0, "renaming an open modified file");
    hostfile("A/0", "SEQ2.DAT", &sz, &dat); check(sz == 130 * 128 && dat[0] == 35, "...flushed it under the old name first, then renamed");
    check(rc_oft_find(0, 0, "SEQ2    DAT") >= 0 && rc_oft_find(0, 0, "SEQ     DAT") < 0, "...and the held bytes follow the new name");
    rc_fs_flush_all();

    /* --- the capture files: PUN.TXT ------------------------------------- */
    checkeq(bdos(4, 'H'), 0, "A_WRITE H"); bdos(4, 'i');
    rc_fs_flush_all();
    hostfile("A/0", "PUN.TXT", &sz, &dat); check(sz == 2 && memcmp(dat, "Hi", 2) == 0, "A\\0\\PUN.TXT = 'Hi' after the warm boot's flush");
    bdos(4, '!'); rc_fs_flush_all();
    hostfile("A/0", "PUN.TXT", &sz, &dat); check(sz == 3 && memcmp(dat, "Hi!", 3) == 0, "the next program APPENDS (pun_open outlives it)");
    fcb_set(FCB, 0, "PUN     TXT"); checkeq(bdos(19, FCB), 0, "ERA PUN.TXT");
    bdos(4, 'x'); rc_fs_flush_all();
    hostfile("A/0", "PUN.TXT", &sz, &dat); check(sz == 1 && dat[0] == 'x', "...starts it over");
    bdos(5, 'L'); rc_fs_flush_all();
    hostfile("A/0", "LST.TXT", &sz, &dat); check(sz == 1 && dat[0] == 'L', "L_WRITE -> LST.TXT");
    rc_user = 2; rc_cdrive = 0;
    bdos(4, 'y'); rc_fs_flush_all();
    hostfile("A/0", "PUN.TXT", &sz, &dat); check(sz == 2 && dat[1] == 'y', "PUN.TXT is A\\0's whatever the user");
    bdos(32, 0x0003);                            /* user 3 has a folder */
    fcb_set(FCB, 0, "PUN     TXT"); bdos(22, FCB); bdos(16, FCB);   /* a PUN.TXT of user 3's own */
    checkeq(bdos(19, FCB), 0, "ERA PUN.TXT in user 3");
    hostfile("A/0", "PUN.TXT", &sz, &dat); check(sz == 2, "...A\\0\\PUN.TXT untouched by the ERA itself");
    bdos(4, 'z'); rc_fs_flush_all();
    hostfile("A/0", "PUN.TXT", &sz, &dat); check(sz == 1 && dat[0] == 'z', "...but pun_open was closed BY NAME (disk.h 489): the next punch starts A\\0\\PUN.TXT over");
    bdos(32, 0x0000);
    rc_user = 0;

    /* --- Select and R/O errors from a disk function ---------------------- */
    reset_machine();
    fcb_set(FCB, 3, "X       TXT");            /* C: has no folder */
    r = bdos(15, FCB);
    check(rc_errwait == 1 && strstr(con, "\r\nBdos Err on C: Select") != 0, "F_OPEN on C: is 'Bdos Err on C: Select'");
    rc_key_push('x'); bdos(15, FCB);
    check(rc_status == RC_ST_RESTART && rc_z.hl == 0x02FF, "...the key: a warm boot, and HL = 02FF (H = errSELECT)");
    reset_machine();
    rc_rovec = 1;
    fcb_set(FCB, 0, "SEQ2    DAT"); bdos(15, FCB); rc_errwait = 0; con_clear();
    dma_pattern(0); r = bdos(21, FCB);
    check(rc_errwait == 1 && strstr(con, "Bdos Err on A: R/O") != 0, "F_WRITE on an R/O drive is 'Bdos Err on A: R/O'");
    reset_machine();
    checkeq(bdos(37, 0xFFFF), 0, "DRV_RESET clears R/O");
    checkeq(bdos(16, FCB), 0, "close (unmodified)");

    /* --- BDOS 249 makes a drive -------------------------------------------- */
    fcb_set(FCB, 2, "           ");
    bdos(14, 1);
    check(rc_errwait && strstr(con, "Bdos Err on B: Select") != 0, "DRV_SET B: before it exists: Select");
    reset_machine();
    checkeq(bdos(249, FCB), 0, "F_MAKEDISK B:");
    check(path("B") != 0 && path("B/0") != 0, "B and B\\0 made below the launch folder");
    checkeq(bdos(249, FCB), 0xFE, "...again: 0xFE, it exists");
    zram[FCB] = 0; checkeq(bdos(249, FCB), 0xFF, "dr 0: 0xFF");
    zram[FCB] = 17; checkeq(bdos(249, FCB), 0xFF, "dr 17: 0xFF");
    checkeq(bdos(14, 1), 0, "DRV_SET B: now selects");
    checkeq(rc_cdrive, 1, "...cDrive = 1");
    fcb_set(FCB, 0, "ONB     TXT"); checkeq(bdos(22, FCB), 0, "F_MAKE on B:"); dma_pattern(4); bdos(21, FCB); bdos(16, FCB);
    hostfile("B/0", "ONB.TXT", &sz, &dat); checkeq(sz, 128, "ONB.TXT is in B\\0");
    fcb_set(FCB, 1, "ONB     TXT"); checkeq(bdos(15, FCB), 0xFF, "...and not on A: (dr 1 = A)");
    checkeq(bdos(14, 0), 0, "back to A:");
    check(rc_fs_drive_exists(1) && !rc_fs_drive_exists(2) && !rc_fs_drive_exists(15), "drive_exists: B yes, C and P no");
    check(rc_fs_drive_exists(0), "A yes");

    /* --- two directory slots: A\0 and B\0 coexist ------------------------- */
    fcb_set(0x100, 1, "???????????"); bdos(17, 0x100);       /* A\0's slot wanted */
    fcb_set(0x100, 2, "???????????"); bdos(17, 0x100);       /* B\0's */
    n_find = 0;
    fcb_set(0x100, 1, "???????????"); checkeq(bdos(17, 0x100), 0, "DIR A: with B\\0 wanted since");
    fcb_set(0x100, 2, "???????????"); checkeq(bdos(17, 0x100), 0, "DIR B: with A\\0 wanted since");
    fcb_set(0x100, 1, "S0      BIN"); checkeq(bdos(15, 0x100), 0, "open on A:"); bdos(16, 0x100);
    fcb_set(0x100, 2, "ONB     TXT"); checkeq(bdos(15, 0x100), 0, "open on B:"); bdos(16, 0x100);
    checkeq((unsigned long)n_find, 0, "...NO walk: the two areas hold a slot each (PIP B:=A:*.* refills neither)");
    rc_user = 3;                                 /* a third area takes the least recently wanted slot */
    n = list_file("U3      TXT", 0); checkeq((unsigned long)n, 1, "user 3's DIR (a third area)");
    check(n_find > 0, "...walked A\\3 into the least recently wanted slot");
    rc_user = 0; rc_fs_flush_all();
    /* the flush of a B\0 file leaves the instance standing in B\0 while A\0's
     * slot is valid: the next open of an A\0 file must STAND in A\0 first */
    fcb_set(0x100, 1, "???????????"); bdos(17, 0x100);       /* A\0 the most recently wanted... */
    fcb_set(0x100, 2, "ONB     TXT"); bdos(15, 0x100); dma_pattern(2); bdos(21, 0x100);
    rc_fs_flush_all();                           /* ...ONB.TXT flushed: standing in B\0 */
    hostfile("A/0", "PUN.TXT", &sz, &dat); k = (int)sz;
    bdos(4, 'q'); rc_fs_flush_all();
    hostfile("A/0", "PUN.TXT", &sz, &dat);
    check((int)sz == k + 1 && dat[k] == 'q', "a punch with the entry gone and A\\0's slot valid appends to A\\0\\PUN.TXT (read where it lives)");

    /* --- the warm-boot flush of an unclosed file -------------------------- */
    fcb_set(FCB, 0, "UNCLOSEDTXT"); checkeq(bdos(22, FCB), 0, "F_MAKE UNCLOSED.TXT"); dma_pattern(6); checkeq(bdos(21, FCB), 0, "write 1"); checkeq(bdos(21, FCB), 0, "write 2");
    rc_fs_flush_all();
    hostfile("A/0", "UNCLOSED.TXT", &sz, &dat); check(sz == 256 && dat[128] == 42, "an unclosed file reaches the disk at the warm boot");
    checkeq((unsigned long)live_claims(), 0, "...and nothing is held");

    /* --- no memory: the toast, the eviction that helps -------------------- */
    fcb_set(0x200, 0, "S32768  BIN"); checkeq(bdos(15, 0x200), 0, "open S32768");
    cl_limit = 1;                                /* the heap holds ONE claim */
    fcb_set(FCB, 0, "S16384  BIN");
    checkeq(bdos(15, FCB), 0, "a refused claim evicts the clean S32768 and retries");
    checkeq((unsigned long)live_claims(), 1, "...one claim");
    dma_pattern(1); checkeq(bdos(21, FCB), 0, "...written to: dirty");
    checkeq(bdos(15, 0x200), 0xFF, "with no room and nothing clean: 0xFF");
    check(strcmp(last_toast, "RunCPM RAM? S32768.BIN") == 0, "...and the toast names it");
    cl_limit = 0;
    rc_fs_flush_all();

    /* --- rc_say: fullscreen puts the fact on the terminal too; the fill's
     * toast is under the lock (immediate) ------------------------------- */
    reset_machine();
    rc_full = 1;
    fcb_set(FCB, 0, "S67301  BIN");
    checkeq(bdos(15, FCB), 0xFF, "open of a >64K file refused");
    check(strcmp(last_toast, "RunCPM: S67301.BIN>64K") == 0, "...toasted");
    check(strstr(con, "\r\nRunCPM: S67301.BIN>64K\r\n") != 0, "...AND printed on the terminal while fullscreen (a toast is under a fullscreen window)");
    con_clear(); rc_full = 0;
    fcb_set(FCB, 0, "S67301  BIN"); bdos(15, FCB);
    check(con[0] == 0, "...and only while fullscreen");
    { struct ffold *a6 = mkfold(a->clus, "6"); fput(a6, "A-B.TXT", "x", 1); fput(a6, "A.TXT", "y", 1); fput(a6, "A", "z", 1); fput(a6, "A.B", "w", 1); }
    n_toast_locked = 0; n_flush = 0; rc_full = 1; con_clear();
    bdos(32, 0x0006);
    fcb_set(FCB, 0, "???????????");
    checkeq(bdos(17, FCB), 0, "DIR of user 6 (a fresh area: the walk)");
    checkeq((unsigned long)n_toast_locked, 1, "...announced by ONE toast under the gfx lock (immediate) before the walk");
    check(strcmp(last_toast, "RunCPM: reading A:6") == 0, "...'RunCPM: reading A:6'");
    check(strstr(con, "\r\nRunCPM: reading A:6\r\n") != 0 && n_flush == 1, "...on the terminal and flushed on the spot while fullscreen");
    rc_full = 0;
    checkeq((unsigned long)rc_dcc->n, 4, "user 6 has four names");
    check(memcmp(rc_dcc->e[0].name, "A          ", 11) == 0 && memcmp(rc_dcc->e[1].name, "A-B     TXT", 11) == 0 &&
          memcmp(rc_dcc->e[2].name, "A       B  ", 11) == 0 && memcmp(rc_dcc->e[3].name, "A       TXT", 11) == 0,
          "...in HOST order: A, A-B.TXT, A.B, A.TXT (glob's; the FCB form would put A-B.TXT last)");
    check_order("...check_order agrees");
    fcb_set(FCB, 0, "A-A     TXT"); bdos(22, FCB); bdos(16, FCB);
    check(memcmp(rc_dcc->e[1].name, "A-A     TXT", 11) == 0, "a MAKE inserts in host order too (A-A.TXT after A, before A-B.TXT)");
    bdos(32, 0x0000);
    rc_fs_flush_all();

    printf("rcfstest: %d failure(s); %d claims made, %d regrows, %d writes, %d walks\n",
           fails, n_claims_made, n_regrows, n_writes, n_find);
    return fails ? 1 : 0;
}
