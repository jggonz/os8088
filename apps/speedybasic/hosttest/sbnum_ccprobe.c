#include "sbnum.h"

/* Compile-only SmallerC consumer.  It makes every public symbol cross the C
 * ABI and is assembled with sbnum.inc by sbnumtest.sh. */
int sbnum_ccprobe(int op)
{
    if (op == 1) sbn_add();
    else if (op == 2) sbn_sub();
    else if (op == 3) sbn_mul();
    else if (op == 4) sbn_div();
    else if (op == 5) sbn_neg();
    else if (op == 6) sbn_floor();
    else if (op == 7) sbn_fix();
    else if (op == 8) sbn_cint();
    else if (op == 9) sbn_sqrt();
    else if (op == 10) sbn_sin();
    else if (op == 11) sbn_cos();
    else if (op == 13) sbn_float();
    else if (op == 14) sbn_fdiv();
    else return sbn_cmp();
    return sbn_err ? -sbn_err : (int)sbn_alo;
}
