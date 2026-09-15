#include <stdio.h>
#include "speedybasic/sbasic.h"
#include "speedybasic/compiler/numrt.h"

static int checks;

static int value(unsigned lo, int hi, int kind)
{
    return sbn_alo == lo && sbn_ahi == hi && sbn_akind == kind;
}

#define CHECK(e) do { if (!(e)) { \
    fprintf(stderr, "numrttest: check %d failed at line %d\n", \
            checks + 1, __LINE__); return 1; } ++checks; } while (0)

int main(void)
{
    int cell;

    CHECK(sbm_init() == 0);
    cell = sbm_make(2);
    CHECK(cell == 0);

    /* An exact 32-bit LONG survives the pointer-free assignment latch. */
    sbr_num_load(0x1170u, 1, SBN_LONG);       /* 70000 */
    sbr_num_store();
    sbr_num_int(2);
    CHECK(sbr_num_binary(SBR_NUM_ADD, sbr_num_lo, sbr_num_hi,
                         sbr_num_kind) == SBN_OK);
    CHECK(value(0x1172u, 1, SBN_LONG));
    sbr_num_store();
    sbr_num_int(0);
    sbr_num_reload();
    CHECK(value(0x1172u, 1, SBN_LONG));

    /* Mixed arithmetic promotes the LONG exactly to Q16.16. */
    sbr_num_load(0x8000u, 1, SBN_FIXED);      /* 1.5 */
    sbr_num_store();
    sbr_num_int(2);
    CHECK(sbr_num_binary(SBR_NUM_ADD, sbr_num_lo, sbr_num_hi,
                         sbr_num_kind) == SBN_OK);
    CHECK(value(0x8000u, 3, SBN_FIXED));      /* 3.5 */

    /* BASIC slash and banker's CINT stay in the common sbnum engine. */
    sbr_num_int(7);
    sbr_num_store();
    sbr_num_int(2);
    CHECK(sbr_num_binary(SBR_NUM_FDIV, sbr_num_lo, sbr_num_hi,
                         sbr_num_kind) == SBN_OK);
    CHECK(value(0x8000u, 3, SBN_FIXED));
    CHECK(sbr_num_convert(SBN_LONG) == SBN_OK);
    CHECK(value(4, 0, SBN_LONG));
    sbr_num_load(0x8000u, 2, SBN_FIXED);      /* tie: 2.5 -> even 2 */
    CHECK(sbr_num_unary(SBR_NUM_CINT) == SBN_OK);
    CHECK(value(2, 0, SBN_LONG));

    /* Relations publish BASIC's exact -1/0 representation in A. */
    sbr_num_load(0x1170u, 1, SBN_LONG);
    sbr_num_store();
    sbr_num_load(0x116fu, 1, SBN_LONG);
    CHECK(sbr_num_relation(SBR_NUM_GT, sbr_num_lo, sbr_num_hi,
                           sbr_num_kind) == SBN_OK);
    CHECK(value(0xffffu, -1, SBN_LONG));
    CHECK(sbr_num_truth());

    /* Bitwise operations and NOT cover both words of an exact LONG. */
    sbr_num_load(0x5678u, 0x1234, SBN_LONG);
    CHECK(sbr_num_unary(SBR_NUM_NOT) == SBN_OK);
    CHECK(value(0xa987u, (short)0xedcb, SBN_LONG));
    sbr_num_load(0x5678u, 0x1234, SBN_LONG);
    sbr_num_store();
    sbr_num_load(0x0ff0u, 0x00ff, SBN_LONG);
    CHECK(sbr_num_binary(SBR_NUM_AND, sbr_num_lo, sbr_num_hi,
                         sbr_num_kind) == SBN_OK);
    CHECK(value(0x0670u, 0x0034, SBN_LONG));
    sbr_num_load(0x5678u, 0x1234, SBN_LONG);
    sbr_num_store();
    sbr_num_load(0x0ff0u, 0x00ff, SBN_LONG);
    CHECK(sbr_num_binary(SBR_NUM_OR, sbr_num_lo, sbr_num_hi,
                         sbr_num_kind) == SBN_OK);
    CHECK(value(0x5ff8u, 0x12ff, SBN_LONG));
    sbr_num_load(0x5678u, 0x1234, SBN_LONG);
    sbr_num_store();
    sbr_num_load(0x0ff0u, 0x00ff, SBN_LONG);
    CHECK(sbr_num_binary(SBR_NUM_XOR, sbr_num_lo, sbr_num_hi,
                         sbr_num_kind) == SBN_OK);
    CHECK(value(0x5988u, 0x12cb, SBN_LONG));

    /* IDIV/MOD coerce to LONG but retain the full signed 32-bit value. */
    sbr_num_load(0x1170u, 1, SBN_LONG);
    sbr_num_store();
    sbr_num_int(3);
    CHECK(sbr_num_binary(SBR_NUM_IDIV, sbr_num_lo, sbr_num_hi,
                         sbr_num_kind) == SBN_OK);
    CHECK(value(0x5b25u, 0, SBN_LONG));       /* 70000 \\ 3 */
    sbr_num_load(0xee90u, (short)0xfffe, SBN_LONG); /* -70000 */
    sbr_num_store();
    sbr_num_int(300);
    CHECK(sbr_num_binary(SBR_NUM_MOD, sbr_num_lo, sbr_num_hi,
                         sbr_num_kind) == SBN_OK);
    CHECK(value(0xff9cu, -1, SBN_LONG));      /* remainder is -100 */
    sbr_num_load(0x8000u, 7, SBN_FIXED);     /* 7.5 CINTs to 8 */
    sbr_num_store();
    sbr_num_int(3);
    CHECK(sbr_num_binary(SBR_NUM_MOD, sbr_num_lo, sbr_num_hi,
                         sbr_num_kind) == SBN_OK);
    CHECK(value(2, 0, SBN_LONG));
    sbr_num_load(0, (short)0x8000, SBN_LONG);
    sbr_num_store();
    sbr_num_int(-1);
    CHECK(sbr_num_binary(SBR_NUM_IDIV, sbr_num_lo, sbr_num_hi,
                         sbr_num_kind) == SBN_OVERFLOW);
    CHECK(value(0, 0, SBN_LONG));

    /* Power handles signed integral exponents by squaring. */
    sbr_num_int(-3);
    sbr_num_store();
    sbr_num_int(5);
    CHECK(sbr_num_binary(SBR_NUM_POW, sbr_num_lo, sbr_num_hi,
                         sbr_num_kind) == SBN_OK);
    CHECK(value(0xff0du, -1, SBN_LONG));
    sbr_num_int(2);
    sbr_num_store();
    sbr_num_int(-3);
    CHECK(sbr_num_binary(SBR_NUM_POW, sbr_num_lo, sbr_num_hi,
                         sbr_num_kind) == SBN_OK);
    CHECK(value(0x2000u, 0, SBN_FIXED));      /* 2 ^ -3 = .125 */
    sbr_num_int(-2);
    sbr_num_store();
    sbr_num_int(31);
    CHECK(sbr_num_binary(SBR_NUM_POW, sbr_num_lo, sbr_num_hi,
                         sbr_num_kind) == SBN_OK);
    CHECK(value(0, (short)0x8000, SBN_LONG));
    sbr_num_int(2);
    sbr_num_store();
    sbr_num_load(0, 2, SBN_FIXED);            /* integral FIXED exponent */
    CHECK(sbr_num_binary(SBR_NUM_POW, sbr_num_lo, sbr_num_hi,
                         sbr_num_kind) == SBN_OK);
    CHECK(value(4, 0, SBN_LONG));
    sbr_num_int(0);
    sbr_num_store();
    sbr_num_int(-1);
    CHECK(sbr_num_binary(SBR_NUM_POW, sbr_num_lo, sbr_num_hi,
                         sbr_num_kind) == SBN_DIVZERO);
    CHECK(value(0, 0, SBN_FIXED));
    sbr_num_int(2);
    sbr_num_store();
    sbr_num_load(0x8000u, 1, SBN_FIXED);
    CHECK(sbr_num_binary(SBR_NUM_POW, sbr_num_lo, sbr_num_hi,
                         sbr_num_kind) == SBN_DOMAIN);
    CHECK(value(0, 0, SBN_FIXED));

    /* Narrowing is explicit, banker's-rounded, and does not destroy A. */
    sbr_num_load(0x1170u, 1, SBN_LONG);
    CHECK(sbr_num_target_int() == 4464);
    CHECK(value(0x1170u, 1, SBN_LONG));
    sbr_num_load(0x8000u, (short)0xfffe, SBN_FIXED); /* -1.5 */
    CHECK(sbr_num_target_int() == -2);
    CHECK(value(0x8000u, (short)0xfffe, SBN_FIXED));

    /* sbmem array helpers preserve both words and the per-cell kind. */
    sbr_num_load(0x5678u, 0x1234, SBN_FIXED);
    CHECK(sbr_num_array_store((unsigned)cell) == 0);
    sbr_num_int(0);
    CHECK(sbr_num_array_load((unsigned)cell) == 0);
    CHECK(value(0x5678u, 0x1234, SBN_FIXED));

    /* Failures are returned and leave sbnum's defined zero result. */
    sbr_num_load(0xffffu, 0x7fff, SBN_LONG);
    sbr_num_store();
    sbr_num_int(1);
    CHECK(sbr_num_binary(SBR_NUM_ADD, sbr_num_lo, sbr_num_hi,
                         sbr_num_kind) == SBN_OVERFLOW);
    CHECK(value(0, 0, SBN_LONG));
    CHECK(sbr_num_unary(99) == SBN_BADKIND);
    CHECK(value(0, 0, SBN_LONG));

    sbm_reset();
    printf("numrttest: %d parser-free numeric checks - PASS\n", checks);
    return 0;
}
