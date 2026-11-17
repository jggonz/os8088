/* Byteopoly bp_math.c on the target (SPEC.md 131.4.1). Lane: rules.
 * Every expected value is a literal worked out by hand (hex products, the
 * comma-formatted strings), never computed by the code under test. N
 * toggles an intentionally wrong expectation (the negative control). */
#include "os88.h"
#include "../../apps/byteopoly/bp.h"
#include "../../apps/byteopoly/bp_math.c"
static BPU32 bm_a, bm_b;
static unsigned bm_pass, bm_fail, bm_done, bm_negative, bm_case;
static void *bm_win;
static struct os88_pt bm_org;
static char bm_line[48], bm_num[8], bm_buf[14];
static void bm_check(int ok)
{
    bm_case++;
    if (bm_negative && bm_case == 1) ok = 0;
    if (ok) bm_pass++; else bm_fail++;
}
static int bm_is(const BPU32 *a, BPWord hi, BPWord lo) { return a->hi == hi && a->lo == lo; }
static int bm_streq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static void bm_fmt(BPWord hi, BPWord lo, const char *want)
{
    bp_u32_set(&bm_a, lo, hi);
    bp_u32_fmt(bm_buf, &bm_a);
    bm_check(bm_streq(bm_buf, want));
    bm_check(bp_u32_parse(&bm_b, bm_buf) == BP_OK && bm_is(&bm_b, hi, lo));
}
static void bm_test(void)
{
    bm_pass = 0; bm_fail = 0; bm_case = 0;
    /* add: carry from lo into hi; overflow refused, target unchanged */
    bp_u32_set(&bm_a, 0xFFFF, 0); bp_u32_set(&bm_b, 1, 0);
    bm_check(bp_u32_add(&bm_a, &bm_b) == BP_OK && bm_is(&bm_a, 1, 0));
    bp_u32_set(&bm_a, 0xFFFF, 0xFFFF);
    bm_check(bp_u32_add(&bm_a, &bm_b) == BP_EOVERFLOW && bm_is(&bm_a, 0xFFFF, 0xFFFF));
    bp_u32_set(&bm_a, 0x8000, 0xFFFF); bp_u32_set(&bm_b, 0x8000, 0);
    bm_check(bp_u32_add(&bm_a, &bm_b) == BP_EOVERFLOW && bm_is(&bm_a, 0xFFFF, 0x8000));
    bp_u32_set(&bm_a, 0xFFF0, 0xFFFF);
    bm_check(bp_u32_addw(&bm_a, 0x10) == BP_EOVERFLOW && bm_is(&bm_a, 0xFFFF, 0xFFF0));
    bm_check(bp_u32_addw(&bm_a, 0x0F) == BP_OK && bm_is(&bm_a, 0xFFFF, 0xFFFF));
    /* sub: borrow; a balance never goes negative */
    bp_u32_set(&bm_a, 0, 1); bp_u32_set(&bm_b, 1, 0);
    bm_check(bp_u32_sub(&bm_a, &bm_b) == BP_OK && bm_is(&bm_a, 0, 0xFFFF));
    bp_u32_set(&bm_a, 0, 1); bp_u32_set(&bm_b, 1, 1);
    bm_check(bp_u32_sub(&bm_a, &bm_b) == BP_EFUNDS && bm_is(&bm_a, 1, 0));
    bp_u32_set(&bm_a, 5, 0);
    bm_check(bp_u32_subw(&bm_a, 6) == BP_EFUNDS && bm_is(&bm_a, 0, 5));
    bp_u32_set(&bm_a, 0, 2);
    bm_check(bp_u32_subw(&bm_a, 1) == BP_OK && bm_is(&bm_a, 1, 0xFFFF));
    /* compare */
    bp_u32_set(&bm_a, 0, 1); bp_u32_set(&bm_b, 0xFFFF, 0);
    bm_check(bp_u32_cmp(&bm_a, &bm_b) == 1 && bp_u32_cmp(&bm_b, &bm_a) == -1 &&
             bp_u32_cmpw(&bm_b, 0xFFFF) == 0 && bp_u32_cmpw(&bm_a, 0xFFFF) == 1);
    /* multiply: an 8088 unsigned * unsigned keeps 16 bits; these do not */
    bm_check(bp_u32_mulw(&bm_a, 0xFFFF, 0xFFFF) == BP_OK && bm_is(&bm_a, 0xFFFE, 0x0001));
    bm_check(bp_u32_mulw(&bm_a, 0x0100, 0x0100) == BP_OK && bm_is(&bm_a, 1, 0));
    bm_check(bp_u32_mulw(&bm_a, 0x1234, 0x5678) == BP_OK && bm_is(&bm_a, 0x0626, 0x0060));
    bm_check(bp_u32_mulw(&bm_a, 0xABCD, 0x00FF) == BP_OK && bm_is(&bm_a, 0x00AB, 0x2133));
    bm_check(bp_u32_mulw(&bm_a, 40, 60) == BP_OK && bm_is(&bm_a, 0, 2400));
    bp_u32_set(&bm_b, 1, 1);
    bm_check(bp_u32_mul(&bm_a, &bm_b, 0xFFFF) == BP_OK && bm_is(&bm_a, 0xFFFF, 0xFFFF));
    bp_u32_set(&bm_b, 2, 1); bp_u32_set(&bm_a, 7, 0);
    bm_check(bp_u32_mul(&bm_a, &bm_b, 0xFFFF) == BP_EOVERFLOW && bm_is(&bm_a, 0, 7));
    /* decimal, both ways */
    bm_fmt(0, 0, "0");
    bm_fmt(0, 999, "999");
    bm_fmt(0, 1000, "1,000");
    bm_fmt(0, 0xFFFF, "65,535");
    bm_fmt(1, 0, "65,536");
    bm_fmt(0x000F, 0x423F, "999,999");
    bm_fmt(0x000F, 0x4240, "1,000,000");
    bm_fmt(0xFFFF, 0xFFFF, "4,294,967,295");
    bp_u32_set(&bm_a, 42, 0);
    bm_check(bp_u32_parse(&bm_a, "4294967296") == BP_EOVERFLOW && bm_is(&bm_a, 0, 42));
    bm_check(bp_u32_parse(&bm_a, "1,") == BP_EARG && bm_is(&bm_a, 0, 42));
    /* xorshift32 from state 1: 0004 2021h, then a die */
    bp_u32_set(&bm_a, 1, 0);
    bm_check(bp_rand_next(&bm_a, 0) == 0x0004 && bm_is(&bm_a, 0x0004, 0x2021));
    bm_done = 1;
}
void os88_paint(void *win)
{
    os88_wm_content(win, &bm_org);
    os88_font_run(bm_org.x + 8, bm_org.y + 8, "Byteopoly two-word arithmetic", 0, 15);
    os88_strcpy(bm_line, bm_done ? (bm_fail ? "FAIL " : "PASS ") : "Pending ", 48);
    os88_utoa(bm_pass, bm_num); os88_strcpy(bm_line + os88_strlen(bm_line), bm_num, 32);
    os88_strcpy(bm_line + os88_strlen(bm_line), " passed, ", 24);
    os88_utoa(bm_fail, bm_num); os88_strcpy(bm_line + os88_strlen(bm_line), bm_num, 16);
    os88_strcpy(bm_line + os88_strlen(bm_line), " failed    ", 12);
    os88_font_run(bm_org.x + 8, bm_org.y + 24, bm_line, 0, 15);
    os88_font_run(bm_org.x + 8, bm_org.y + 40, "N: negative control", 0, 15);
}
void os88_onkey(int ascii, int scan, void *win)
{
    (void)scan;
    if (ascii == 'n' || ascii == 'N') { bm_negative = !bm_negative; bm_done = 0; os88_wm_wake(win); }
}
void os88_onwake(void *win)
{
    if (!bm_done) bm_test();
    os88_gfx_lock();
    if (os88_wm_clip_set(win) == 0) { os88_paint(win); os88_wm_grow(win); }
    os88_gfx_unlock();
}
static struct os88_class bm_cls = {OS88_CLASS_LEN, 0,
    os88_t_paint, os88_t_onkey, 0, 0, os88_t_onwake, 0, 0, 0, 0, 0, 0, 0, 0};
void *os88_main(void)
{
    bm_win = os88_wm_create(112, 64, 344, 88, "Byteopoly arithmetic", &bm_cls);
    if (bm_win) os88_wm_wake(bm_win);
    return bm_win;
}
