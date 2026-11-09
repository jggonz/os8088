/* ============================================================================
 * os8088 - tests/chuge/chuge.c
 *
 * CHUGE, a TEST instrument (nothing under tests/ ships): the C SDK's way to
 * the memory a big program needs (docs/plans/MEGA-HUGE-PLAN.md F3, F6, F7).
 * Every step is a line, on port E9 (Bochs prints it) and into CHUGE.TXT in
 * the folder it was started from (any emulator's disk carries it home):
 *
 *   CHUGE: avail LLLL TTTT     os88_mem_largest_kb(), os88_mem_total_kb()
 *   CHUGE: f6 accept|refuse    the Weave-shaped pre-read check: 11 claims of
 *                              64KB, total and largest - and on accept
 *   CHUGE: f6 claims NNNN      ...how many of the 11 os88_mem_claim(64) were
 *                              really given (the answer must be SERVED)
 *   CHUGE: w32 ask KKKK got SSSS step CCCC   os88_mem_huge_ask(W32), and
 *                              os88_mem_huge_w() of exactly that (F3)
 *   CHUGE: ask0 KKKK           os88_mem_huge_ask(0), the bridge's figure
 *   CHUGE: huge SSSS step CCCC os88_mem_huge(136) (F7: BX = 0, not SP)
 *   CHUGE: fill ok|bad BBBB    272 blocks of 512 put through os88_seg_put
 *                              by window and read back by os88_seg_get
 *   CHUGE: write ok|ferr EEEE  os88_file_write_huge, 139,264 bytes
 *   CHUGE: read LLLL HHHH ok|bad BBBB   os88_file_read_huge into a second
 *                              claim, os88_file_hi(), every block compared
 *   CHUGE: readat ok|bad EEEE  os88_file_read_at_seg of the cluster at 128KB
 *                              into window 1 at 200h of the FIRST claim
 *   CHUGE: done
 *
 * The four C rules (docs/C-TOOLCHAIN.md): every addressed object is static,
 * no struct by value, no long, small frames. The 32-bit sizes are word
 * pairs, as the SDK has them.
 * ==========================================================================*/
#include "os88.h"

void chuge_e9(const char *s);           /* the shim's: one line on E9 */

#define CH_KB      136                  /* the huge claim: three windows */
#define CH_BLOCKS  272                  /* ...of 512-byte blocks */
#define CH_PERWIN  128                  /* blocks in a 64KB window */

static char ch_line[40];
static char ch_out[640];
static unsigned ch_outn;
static unsigned char ch_buf[512];
static unsigned char ch_got[512];
static unsigned ch_seg, ch_step, ch_seg2, ch_step2;

static void ch_put(const char *s)
{
    while (*s && ch_outn < sizeof(ch_out) - 1)
        ch_out[ch_outn++] = *s++;
}

static void ch_hex(char *d, unsigned v)
{
    int i;
    for (i = 3; i >= 0; i--) {
        d[i] = "0123456789ABCDEF"[v & 15];
        v >>= 4;
    }
    d[4] = 0;
}

/* "CHUGE: <what>[ <hex>]" - a line, both ways */
static void ch_say(const char *what, int has, unsigned v)
{
    os88_strcpy(ch_line, "CHUGE: ", sizeof(ch_line));
    os88_strcpy(ch_line + 7, what, sizeof(ch_line) - 13);
    if (has) {
        unsigned n = os88_strlen(ch_line);
        ch_line[n] = ' ';
        ch_hex(ch_line + n + 1, v);
    }
    chuge_e9(ch_line);
    ch_put(ch_line);
    ch_put("\r\n");
}

/* block b's pattern into ch_buf */
static void ch_pat(unsigned b)
{
    unsigned i;
    for (i = 0; i < 512; i++)
        ch_buf[i] = (unsigned char)(b + i * 7 + (b >> 8));
    ch_buf[0] = (unsigned char)b;
    ch_buf[1] = (unsigned char)(b >> 8);
}

/* compare ch_got with block b's pattern: 0 = equal */
static int ch_cmp(unsigned b)
{
    unsigned i;
    ch_pat(b);
    for (i = 0; i < 512; i++)
        if (ch_got[i] != ch_buf[i])
            return 1;
    return 0;
}

/* the window and offset block b lives at, in a claim of that step */
static unsigned ch_win(unsigned seg, unsigned step, unsigned b)
{
    return seg + (b / CH_PERWIN) * step;
}

static void ch_f6(void)
{
    static unsigned segs[11];
    unsigned l, t, n, i;
    l = os88_mem_largest_kb();
    t = os88_mem_total_kb();
    ch_say("avail", 1, l);
    ch_say("total", 1, t);
    /* WEAVE'S SHAPE (apps/weave/wload.c step 3): the sum against the total,
     * the largest single claim against the largest - eleven claims of 64KB,
     * 704KB, which no conventional heap holds */
    if (t < 704 || l < 64) {
        ch_say("f6 refuse", 0, 0);
        return;
    }
    ch_say("f6 accept", 0, 0);
    n = 0;
    for (i = 0; i < 11; i++) {
        segs[i] = os88_mem_claim(64);
        if (segs[i] != 0)
            n++;
    }
    ch_say("f6 claims", 1, n);
    for (i = 0; i < 11; i++)
        if (segs[i] != 0)
            os88_mem_free(segs[i]);
}

static void ch_f3(void)
{
    unsigned k, s;
    k = os88_mem_huge_ask(OS88_HUGE_W32);
    ch_say("w32 ask", 1, k);
    s = os88_mem_huge_w((int)k, OS88_HUGE_W32);
    ch_say("w32 got", 1, s);
    ch_say("w32 step", 1, os88_mem_huge_step());
    if (s != 0)
        os88_mem_free(s);
    ch_say("ask0", 1, os88_mem_huge_ask(0));
}

static int ch_fill(void)
{
    unsigned b;
    for (b = 0; b < CH_BLOCKS; b++) {
        ch_pat(b);
        os88_seg_put(ch_win(ch_seg, ch_step, b), (b % CH_PERWIN) * 512,
                     ch_buf, 512);
    }
    for (b = 0; b < CH_BLOCKS; b++) {
        os88_seg_get(ch_got, ch_win(ch_seg, ch_step, b),
                     (b % CH_PERWIN) * 512, 512);
        if (ch_cmp(b)) {
            ch_say("fill bad", 1, b);
            return 1;
        }
    }
    ch_say("fill ok", 0, 0);
    return 0;
}

static void ch_f7(void)
{
    unsigned b, n, h, cs;
    ch_seg = os88_mem_huge(CH_KB);
    ch_step = os88_mem_huge_step();
    ch_say("huge", 1, ch_seg);
    ch_say("step", 1, ch_step);
    if (ch_seg == 0)
        return;
    if (ch_fill())
        return;
    /* 136KB = 0x22000 bytes: DX:CX = 0002:2000 */
    if (os88_file_write_huge("CHUGE.DAT", ch_seg, 0x2000, 2) != 0) {
        ch_say("write ferr", 1, os88_ferr());
        return;
    }
    ch_say("write ok", 0, 0);
    ch_seg2 = os88_mem_huge(CH_KB);
    ch_step2 = os88_mem_huge_step();
    if (ch_seg2 == 0) {
        ch_say("huge2 refused", 0, 0);
        return;
    }
    n = os88_file_read_huge("CHUGE.DAT", ch_seg2, 0x2000, 2);
    h = os88_file_hi();
    ch_say("read lo", 1, n);
    ch_say("read hi", 1, h);
    if (os88_ferr() != 0) {
        ch_say("read ferr", 1, os88_ferr());
        return;
    }
    for (b = 0; b < CH_BLOCKS; b++) {
        os88_seg_get(ch_got, ch_win(ch_seg2, ch_step2, b),
                     (b % CH_PERWIN) * 512, 512);
        if (ch_cmp(b)) {
            ch_say("read bad", 1, b);
            return;
        }
    }
    ch_say("read ok", 0, 0);
    /* THE CLUSTER AT 128KB (block 256, window 2 of the file) into the FIRST
     * claim's window 1 at 200h, over block 129 - then it must read as 256 */
    cs = (unsigned)os88_disk_cluster_sectors();
    cs <<= 9;                           /* sectors -> bytes */
    n = os88_file_read_at_seg("CHUGE.DAT", ch_seg + ch_step, 0x200, cs, 0, 2);
    if (n != cs) {
        ch_say("readat ferr", 1, os88_ferr());
        return;
    }
    os88_seg_get(ch_got, ch_seg + ch_step, 0x200, 512);
    if (ch_cmp(256)) {
        ch_say("readat bad", 1, ch_got[0] | (ch_got[1] << 8));
        return;
    }
    ch_say("readat ok", 0, 0);
}

void os88_paint(void *win)              /* never: os88_main opens nothing */
{
}

void *os88_main(void)
{
    ch_outn = 0;
    ch_f6();
    ch_f3();
    ch_f7();
    ch_say("done", 0, 0);
    os88_file_write("CHUGE.TXT", ch_out, ch_outn);
    return 0;                           /* nothing to keep open */
}
