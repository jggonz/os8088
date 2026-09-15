#include "speedybasic/compiler/numrt.c"

/* Runs through SmallerC and executes on the raw 8086 gate. */
int numrt_ccprobe(void)
{
    sbr_num_load(0x1170u, 1, SBN_LONG);
    sbr_num_store();
    sbr_num_int(2);
    if (sbr_num_binary(SBR_NUM_ADD, sbr_num_lo, sbr_num_hi,
                       sbr_num_kind) != SBN_OK) return 1;
    if (sbn_alo != 0x1172u || sbn_ahi != 1 || sbn_akind != SBN_LONG)
        return 2;

    sbr_num_load(0x8000u, 1, SBN_FIXED);
    sbr_num_store();
    sbr_num_int(2);
    if (sbr_num_binary(SBR_NUM_ADD, sbr_num_lo, sbr_num_hi,
                       sbr_num_kind) != SBN_OK) return 3;
    if (sbn_alo != 0x8000u || sbn_ahi != 3 || sbn_akind != SBN_FIXED)
        return 4;

    sbr_num_int(7);
    sbr_num_store();
    sbr_num_int(2);
    if (sbr_num_binary(SBR_NUM_FDIV, sbr_num_lo, sbr_num_hi,
                       sbr_num_kind) != SBN_OK) return 5;
    if (sbr_num_convert(SBN_LONG) != SBN_OK || sbn_alo != 4 ||
        sbn_ahi != 0 || sbn_akind != SBN_LONG) return 6;

    sbr_num_load(0x1170u, 1, SBN_LONG);
    sbr_num_store();
    sbr_num_load(0x116fu, 1, SBN_LONG);
    if (sbr_num_relation(SBR_NUM_GT, sbr_num_lo, sbr_num_hi,
                         sbr_num_kind) != SBN_OK) return 7;
    if (sbn_alo != 0xffffu || sbn_ahi != -1 || !sbr_num_truth()) return 8;

    sbr_num_load(0x5678u, 0x1234, SBN_LONG);
    if (sbr_num_unary(SBR_NUM_NOT) != SBN_OK || sbn_alo != 0xa987u ||
        (unsigned)sbn_ahi != 0xedcbu) return 9;

    sbr_num_load(0xee90u, 0xfffe, SBN_LONG);
    sbr_num_store();
    sbr_num_int(300);
    if (sbr_num_binary(SBR_NUM_MOD, sbr_num_lo, sbr_num_hi,
                       sbr_num_kind) != SBN_OK) return 10;
    if (sbn_alo != 0xff9cu || sbn_ahi != -1 || sbn_akind != SBN_LONG)
        return 11;

    sbr_num_int(2);
    sbr_num_store();
    sbr_num_int(-3);
    if (sbr_num_binary(SBR_NUM_POW, sbr_num_lo, sbr_num_hi,
                       sbr_num_kind) != SBN_OK) return 12;
    if (sbn_alo != 0x2000u || sbn_ahi != 0 || sbn_akind != SBN_FIXED)
        return 13;

    sbr_num_load(0x8000u, 0xfffe, SBN_FIXED);
    if (sbr_num_target_int() != -2 || sbn_alo != 0x8000u ||
        (unsigned)sbn_ahi != 0xfffeu || sbn_akind != SBN_FIXED) return 14;

    sbr_num_load(0xffffu, 0x7fff, SBN_LONG);
    sbr_num_store();
    sbr_num_int(1);
    if (sbr_num_binary(SBR_NUM_ADD, sbr_num_lo, sbr_num_hi,
                       sbr_num_kind) != SBN_OVERFLOW) return 15;
    if (sbn_alo || sbn_ahi || sbn_akind != SBN_LONG) return 16;
    return 0;
}
