/* Compact numeric support for parser-free, SmallerC-generated BASIC code. */
#include "numrt.h"

/* sbmem.inc supplies these ordinary SmallerC near-call entries. */
unsigned sbm_get_lo(unsigned cell);
int sbm_get_hi(unsigned cell);
int sbm_set_pair(unsigned cell, unsigned lo, int hi);
int sbm_kind_get(unsigned cell);
void sbm_kind_set(unsigned cell, int kind);

unsigned short sbr_num_lo;
short sbr_num_hi;
short sbr_num_kind;

/* Private global work words keep live 32-bit values out of C automatic
 * addresses.  The whole sbnum ABI is serialized, so these add no reentrancy
 * restriction beyond the one the underlying A/B register bank already has. */
static unsigned short sbr_work_alo;
static short sbr_work_ahi;
static short sbr_work_akind;
static unsigned short sbr_work_blo;
static short sbr_work_bhi;
static short sbr_work_bkind;
static unsigned short sbr_work_rlo;
static short sbr_work_rhi;
static short sbr_work_rkind;
static unsigned short sbr_work_elo;
static unsigned short sbr_work_ehi;

static int sbr_num_fail(int error, int kind)
{
    sbn_alo = 0;
    sbn_ahi = 0;
    sbn_akind = (short)kind;
    sbn_err = (short)error;
    return error;
}

static int sbr_num_badop(void)
{
    return sbr_num_fail(SBN_BADKIND, SBN_LONG);
}

void sbr_num_load(unsigned lo, int hi, int kind)
{
    sbn_alo = (unsigned short)lo;
    sbn_ahi = (short)hi;
    sbn_akind = (short)kind;
    sbn_err = SBN_OK;
}

void sbr_num_int(int value)
{
    sbn_alo = (unsigned short)value;
    sbn_ahi = value < 0 ? -1 : 0;
    sbn_akind = SBN_LONG;
    sbn_err = SBN_OK;
}

void sbr_num_store(void)
{
    sbr_num_lo = sbn_alo;
    sbr_num_hi = sbn_ahi;
    sbr_num_kind = sbn_akind;
}

void sbr_num_reload(void)
{
    sbr_num_load(sbr_num_lo, sbr_num_hi, sbr_num_kind);
}

static void sbr_num_left(unsigned lo, int hi, int kind)
{
    sbn_blo = sbn_alo;
    sbn_bhi = sbn_ahi;
    sbn_bkind = sbn_akind;
    sbn_alo = (unsigned short)lo;
    sbn_ahi = (short)hi;
    sbn_akind = (short)kind;
}

/* Apply BASIC integer coercion to both operands without ever taking the
 * address of a C automatic.  A/B are both LONG on success. */
static int sbr_num_pair_long(void)
{
    sbr_work_blo = sbn_blo;
    sbr_work_bhi = sbn_bhi;
    sbr_work_bkind = sbn_bkind;
    sbn_cint();
    if (sbn_err) return sbn_err;
    sbr_work_alo = sbn_alo;
    sbr_work_ahi = sbn_ahi;
    sbr_num_load(sbr_work_blo, sbr_work_bhi, sbr_work_bkind);
    sbn_cint();
    if (sbn_err) return sbn_err;
    sbn_blo = sbn_alo;
    sbn_bhi = sbn_ahi;
    sbn_bkind = SBN_LONG;
    sbn_alo = sbr_work_alo;
    sbn_ahi = sbr_work_ahi;
    sbn_akind = SBN_LONG;
    return SBN_OK;
}

static int sbr_num_mod(void)
{
    if (sbr_num_pair_long()) return sbn_err;
    sbr_work_alo = sbn_alo;
    sbr_work_ahi = sbn_ahi;
    sbr_work_blo = sbn_blo;
    sbr_work_bhi = sbn_bhi;
    sbn_div();
    if (sbn_err) return sbn_err;
    sbn_blo = sbr_work_blo;
    sbn_bhi = sbr_work_bhi;
    sbn_bkind = SBN_LONG;
    sbn_mul();
    if (sbn_err) return sbn_err;
    sbn_blo = sbn_alo;
    sbn_bhi = sbn_ahi;
    sbn_bkind = SBN_LONG;
    sbn_alo = sbr_work_alo;
    sbn_ahi = sbr_work_ahi;
    sbn_akind = SBN_LONG;
    sbn_sub();
    return sbn_err;
}

static int sbr_num_power(void)
{
    int negative;

    /* Save the base before validating and converting the exponent in A. */
    sbr_work_alo = sbn_alo;
    sbr_work_ahi = sbn_ahi;
    sbr_work_akind = sbn_akind;
    if (sbr_work_akind != SBN_LONG && sbr_work_akind != SBN_FIXED)
        return sbr_num_badop();
    if (sbn_bkind == SBN_FIXED && sbn_blo != 0)
        return sbr_num_fail(SBN_DOMAIN, SBN_FIXED);
    sbr_num_load(sbn_blo, sbn_bhi, sbn_bkind);
    sbn_cint();
    if (sbn_err) return sbn_err;
    sbr_work_elo = sbn_alo;
    sbr_work_ehi = (unsigned short)sbn_ahi;
    negative = (short)sbr_work_ehi < 0;
    if (negative) {
        sbr_work_elo = (unsigned short)(~sbr_work_elo + 1u);
        sbr_work_ehi = (unsigned short)(~sbr_work_ehi +
                         (sbr_work_elo == 0 ? 1u : 0u));
    }

    /* A negative integral exponent starts from the rounded FIXED reciprocal,
     * avoiding a huge positive intermediate that would overflow first. */
    if (negative) {
        sbn_blo = sbr_work_alo;
        sbn_bhi = sbr_work_ahi;
        sbn_bkind = sbr_work_akind;
        sbr_num_int(1);
        sbn_fdiv();
        if (sbn_err) return sbn_err;
        sbr_work_alo = sbn_alo;
        sbr_work_ahi = sbn_ahi;
        sbr_work_akind = sbn_akind;
    }

    sbr_work_rlo = 1;
    sbr_work_rhi = 0;
    sbr_work_rkind = SBN_LONG;
    while (sbr_work_elo != 0 || sbr_work_ehi != 0) {
        if (sbr_work_elo & 1u) {
            sbn_alo = sbr_work_rlo;
            sbn_ahi = sbr_work_rhi;
            sbn_akind = sbr_work_rkind;
            sbn_blo = sbr_work_alo;
            sbn_bhi = sbr_work_ahi;
            sbn_bkind = sbr_work_akind;
            sbn_mul();
            if (sbn_err) return sbn_err;
            sbr_work_rlo = sbn_alo;
            sbr_work_rhi = sbn_ahi;
            sbr_work_rkind = sbn_akind;
        }
        sbr_work_elo = (unsigned short)((sbr_work_elo >> 1) |
                         ((sbr_work_ehi & 1u) << 15));
        sbr_work_ehi >>= 1;
        if (sbr_work_elo != 0 || sbr_work_ehi != 0) {
            sbn_alo = sbr_work_alo;
            sbn_ahi = sbr_work_ahi;
            sbn_akind = sbr_work_akind;
            sbn_blo = sbr_work_alo;
            sbn_bhi = sbr_work_ahi;
            sbn_bkind = sbr_work_akind;
            sbn_mul();
            if (sbn_err) return sbn_err;
            sbr_work_alo = sbn_alo;
            sbr_work_ahi = sbn_ahi;
            sbr_work_akind = sbn_akind;
        }
    }
    sbr_num_load(sbr_work_rlo, sbr_work_rhi, sbr_work_rkind);
    return SBN_OK;
}

int sbr_num_binary(int op, unsigned left_lo, int left_hi, int left_kind)
{
    sbr_num_left(left_lo, left_hi, left_kind);
    if (op == SBR_NUM_ADD) sbn_add();
    else if (op == SBR_NUM_SUB) sbn_sub();
    else if (op == SBR_NUM_MUL) sbn_mul();
    else if (op == SBR_NUM_DIV) sbn_div();
    else if (op == SBR_NUM_FDIV) sbn_fdiv();
    else if (op == SBR_NUM_IDIV) {
        if (sbr_num_pair_long()) return sbn_err;
        sbn_div();
    }
    else if (op == SBR_NUM_MOD) return sbr_num_mod();
    else if (op == SBR_NUM_AND || op == SBR_NUM_OR ||
             op == SBR_NUM_XOR) {
        if (sbr_num_pair_long()) return sbn_err;
        if (op == SBR_NUM_AND) {
            sbn_alo &= sbn_blo;
            sbn_ahi &= sbn_bhi;
        } else if (op == SBR_NUM_OR) {
            sbn_alo |= sbn_blo;
            sbn_ahi |= sbn_bhi;
        } else {
            sbn_alo ^= sbn_blo;
            sbn_ahi ^= sbn_bhi;
        }
        sbn_akind = SBN_LONG;
        sbn_err = SBN_OK;
    }
    else if (op == SBR_NUM_POW) return sbr_num_power();
    else return sbr_num_badop();
    return sbn_err;
}

int sbr_num_unary(int op)
{
    if (op == SBR_NUM_NEG) sbn_neg();
    else if (op == SBR_NUM_FLOOR) sbn_floor();
    else if (op == SBR_NUM_FIX) sbn_fix();
    else if (op == SBR_NUM_CINT) sbn_cint();
    else if (op == SBR_NUM_FLOAT) sbn_float();
    else if (op == SBR_NUM_SQRT) sbn_sqrt();
    else if (op == SBR_NUM_SIN) sbn_sin();
    else if (op == SBR_NUM_COS) sbn_cos();
    else if (op == SBR_NUM_NOT) {
        sbn_cint();
        if (sbn_err) return sbn_err;
        sbn_alo = (unsigned short)~sbn_alo;
        sbn_ahi = (short)~sbn_ahi;
        sbn_akind = SBN_LONG;
    }
    else return sbr_num_badop();
    return sbn_err;
}

int sbr_num_convert(int kind)
{
    if (kind != SBN_LONG && kind != SBN_FIXED) return sbr_num_badop();
    if (sbn_akind == kind) {
        sbn_err = SBN_OK;
    } else if (kind == SBN_LONG) sbn_cint();
    else sbn_float();
    return sbn_err;
}

int sbr_num_relation(int relation, unsigned left_lo, int left_hi,
                     int left_kind)
{
    int order;
    int truth;
    sbr_num_left(left_lo, left_hi, left_kind);
    order = sbn_cmp();
    if (sbn_err) return sbn_err;
    if (relation == SBR_NUM_EQ) truth = order == 0;
    else if (relation == SBR_NUM_NE) truth = order != 0;
    else if (relation == SBR_NUM_LT) truth = order < 0;
    else if (relation == SBR_NUM_LE) truth = order <= 0;
    else if (relation == SBR_NUM_GT) truth = order > 0;
    else if (relation == SBR_NUM_GE) truth = order >= 0;
    else return sbr_num_badop();
    sbn_alo = truth ? 0xffffu : 0;
    sbn_ahi = truth ? -1 : 0;
    sbn_akind = SBN_LONG;
    sbn_err = SBN_OK;
    return SBN_OK;
}

int sbr_num_truth(void)
{
    return sbn_alo != 0 || sbn_ahi != 0;
}

int sbr_num_target_int(void)
{
    unsigned lo;
    int hi;
    int kind;
    int error;
    int result;
    lo = sbn_alo;
    hi = sbn_ahi;
    kind = sbn_akind;
    sbn_cint();
    result = (short)sbn_alo;
    error = sbn_err;
    sbn_alo = (unsigned short)lo;
    sbn_ahi = (short)hi;
    sbn_akind = (short)kind;
    sbn_err = (short)error;
    return result;
}

int sbr_num_array_load(unsigned cell)
{
    sbn_alo = (unsigned short)sbm_get_lo(cell);
    sbn_ahi = (short)sbm_get_hi(cell);
    sbn_akind = (short)sbm_kind_get(cell);
    sbn_err = SBN_OK;
    return 0;
}

int sbr_num_array_store(unsigned cell)
{
    if (sbm_set_pair(cell, sbn_alo, sbn_ahi) < 0) return -1;
    sbm_kind_set(cell, sbn_akind);
    return 0;
}
