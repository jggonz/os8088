#ifndef SPEEDYBASIC_SBNUM_H
#define SPEEDYBASIC_SBNUM_H

/*
 * The numeric register shared by the C interpreter and sbnum.inc.
 *
 * A LONG is an exact signed two's-complement 32-bit integer.  A FIXED is a
 * signed Q16.16 value.  The two words always hold the raw value, low word
 * first.  Operations consume A and B and leave their result in A; B is
 * scratch.  This avoids structs, 32-bit C types, and addresses of automatics,
 * all three of which are unavailable in the SmallerC target (C-TOOLCHAIN §4).
 */
#define SBN_LONG       0
#define SBN_FIXED      1

#define SBN_OK         0
#define SBN_DIVZERO    1
#define SBN_OVERFLOW   2
#define SBN_DOMAIN     3
#define SBN_BADKIND    4
#define SBN_NOMODULE   5

extern unsigned short sbn_alo;
extern short          sbn_ahi;
extern short          sbn_akind;
extern unsigned short sbn_blo;
extern short          sbn_bhi;
extern short          sbn_bkind;
extern short          sbn_err;

/* Binary operations promote a LONG to Q16.16 when either operand is FIXED.
 * That promotion is defined only for LONGs in [-32768,32767].  Add, subtract,
 * and LONG multiply are exact or report SBN_OVERFLOW.  FIXED multiply/divide
 * round to nearest, ties to even.  LONG divide truncates toward zero. */
void sbn_add(void);
void sbn_sub(void);
void sbn_mul(void);
void sbn_div(void);
/* BASIC `/` uses this when both inputs are LONG: it forms a rounded FIXED
 * quotient directly, so large operands need not fit a FIXED input first. */
void sbn_fdiv(void);
int  sbn_cmp(void);
void sbn_neg(void);

/* INT floors, FIX truncates, and CINT rounds to nearest with ties to even.
 * Their result is LONG.  A LONG input is already integral. */
void sbn_floor(void);
void sbn_fix(void);
void sbn_cint(void);

/* Convert an exact LONG in [-32768,32767] to FIXED.  A FIXED input is
 * unchanged.  This is the explicit coercion used by `/` and fixed variables;
 * changing sbn_akind alone would reinterpret the raw bits. */
void sbn_float(void);

/* These return FIXED.  SQR rejects negative input.  SIN/COS take radians.
 * A LONG argument is accepted when it can be promoted to Q16.16. */
void sbn_sqrt(void);
void sbn_sin(void);
void sbn_cos(void);

#endif
