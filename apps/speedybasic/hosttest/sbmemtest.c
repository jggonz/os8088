#define SB_HOST_TEST 1
#include "sbmem_host.c"
#include <stdio.h>

int main(void)
{
    int b;
    int i;
    char out[8];
    static char large[30000];
    if (sbm_init() || (b = sbm_make(4001)) != 0 || sbmh_cap != 4096) return 1;
    sbm_set(0, -7); sbm_set(4000, 123);
    if (sbm_make(4001) != 4001 || sbmh_cap != 8192) return 2;
    if (sbm_get(0) != -7 || sbm_get(4000) != 123) return 3;
    if (sbm_make(4001) != 8002 || sbmh_cap != 12288) return 4;
    if (sbmh_hi_live || sbm_set_pair(12002, 0x5678, 0x1234)) return 5;
    if (!sbmh_hi_live || sbm_get_hi(0) != -1) return 6;
    if (sbm_make(4381) != 12003 || sbmh_cap != 16384) return 7;
    if (sbm_get(0) != -7 || sbm_get(4000) != 123) return 8;
    if (sbm_get_lo(12002) != 0x5678 || sbm_get_hi(12002) != 0x1234)
        return 9;
    if (sbm_get_lo(16383) || sbm_get_hi(16383)) return 10;
    if (sbm_make(1) != -1 || sbmh_used != 16384) return 11;
    if (sbm_kind_get(0) || sbm_kind_get(16383)) return 10;
    sbm_kind_set(0, 1); sbm_kind_set(16383, 1);
    if (!sbm_kind_get(0) || !sbm_kind_get(16383)) return 12;
    sbm_kind_set(0, 0);
    if (sbm_kind_get(0) || !sbm_kind_get(16383)) return 13;

    /* A failed high-plane replacement leaves capacity, used count, segments'
       modeled contents, and both words of every existing cell unchanged. */
    sbm_reset();
    if (sbm_make(100) || sbm_set_pair(0, 0x4321, 0x1234)) return 14;
    if (sbmh_cap != 2048 || !sbmh_hi_live) return 15;
    sbmh_fail_after = 2;
    if (sbm_make(2100) != -1) return 16;
    if (sbmh_used != 100 || sbmh_cap != 2048 || sbmh_lo_cap != 4096)
        return 17;
    if (sbm_get_lo(0) != 0x4321 || sbm_get_hi(0) != 0x1234) return 18;
    if (sbm_make(2100) != 100 || sbmh_cap != 4096) return 19;
    if (sbm_get_lo(0) != 0x4321 || sbm_get_hi(0) != 0x1234) return 20;

    /* Late high allocation is also failure-atomic and reports -1. */
    sbm_reset();
    if (sbm_make(2)) return 21;
    sbm_set(0, 7);
    sbmh_fail_after = 1;
    if (sbm_set_pair(0, 0x5678, 0x1234) != -1) return 22;
    if (sbm_get_lo(0) != 7 || sbm_get_hi(0) != 0 || sbmh_hi_live) return 23;
    if (sbm_set_pair(0, 0x5678, 0x1234)) return 24;
    if (sbm_get_lo(0) != 0x5678 || sbm_get_hi(0) != 0x1234) return 25;

    sbm_reset();
    b = sbm_make(2);
    if (sbm_kind_get(16383)) return 26;
    if (b || sbm_str_set(1, "ARRAY", 5)) return 27;
    if (!sbmh_hi_live || sbm_get_hi(0) != 0) return 28;
    if (sbm_str_len(1) != 5 || sbm_str_char(1, 4) != 'Y') return 29;
    if (sbm_str_copy(1, out, sizeof(out)) != 5 || strcmp(out, "ARRAY")) return 30;
    sbm_reset(); sbm_make(2);
    if (sbm_str_set(0, large, 30000)) return 31;
    for (i = 0; i < 100; i++)
        if (sbm_str_set(0, large, 1000)) return 32;
    if (sbm_str_set(1, large, 2767)) return 33;
    puts("sbmemtest: dynamic 16384-cell planes and string pool - PASS");
    return 0;
}
