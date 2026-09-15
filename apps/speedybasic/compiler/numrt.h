#ifndef SPEEDYBASIC_COMPILED_NUMRT_H
#define SPEEDYBASIC_COMPILED_NUMRT_H

/*
 * Parser-free numeric seam for C emitted by speedybasic_compile.py.
 *
 * SmallerC has only 16-bit int and cannot safely dereference the address of
 * an automatic.  Numeric values therefore cross this seam as three scalar
 * words and results stay in sbnum's global A register.  sbr_num_store() is a
 * global-result handoff for generated assignments; it deliberately takes no
 * pointers.
 */
#include "speedybasic/sbnum.h"

#define SBR_NUM_ADD       0
#define SBR_NUM_SUB       1
#define SBR_NUM_MUL       2
#define SBR_NUM_DIV       3  /* same-kind division; LONG truncates to LONG */
#define SBR_NUM_FDIV      4  /* BASIC /; LONG/LONG produces FIXED */
#define SBR_NUM_IDIV      5  /* BASIC \\; CINT operands, exact LONG quotient */
#define SBR_NUM_MOD       6  /* CINT operands, dividend-sign LONG remainder */
#define SBR_NUM_AND       7
#define SBR_NUM_OR        8
#define SBR_NUM_XOR       9
#define SBR_NUM_POW      10  /* integral exponent; negative is reciprocal */

#define SBR_NUM_NEG       0
#define SBR_NUM_FLOOR     1
#define SBR_NUM_FIX       2
#define SBR_NUM_CINT      3
#define SBR_NUM_FLOAT     4
#define SBR_NUM_SQRT      5
#define SBR_NUM_SIN       6
#define SBR_NUM_COS       7
#define SBR_NUM_NOT       8

#define SBR_NUM_EQ        0
#define SBR_NUM_NE        1
#define SBR_NUM_LT        2
#define SBR_NUM_LE        3
#define SBR_NUM_GT        4
#define SBR_NUM_GE        5

extern unsigned short sbr_num_lo;
extern short          sbr_num_hi;
extern short          sbr_num_kind;

/* Load an exact encoded value or a sign-extended target int into A. */
void sbr_num_load(unsigned lo, int hi, int kind);
void sbr_num_int(int value);
#define sbr_num_literal sbr_num_load

/* Copy A through the pointer-free assignment latch, or restore that latch. */
void sbr_num_store(void);
void sbr_num_reload(void);

/*
 * Binary/relation calls take the saved LEFT operand.  A contains the RIGHT
 * operand on entry.  This argument order lets generated C evaluate both sides
 * without relying on C's expression evaluation order.  Arithmetic and unary
 * calls return SBN_OK or an SBN_* error and leave their result in A.
 * Relations leave BASIC true (-1 LONG) or false (0 LONG) in A.
 */
int sbr_num_binary(int op, unsigned left_lo, int left_hi, int left_kind);
int sbr_num_unary(int op);
int sbr_num_convert(int kind);
int sbr_num_relation(int relation, unsigned left_lo, int left_hi,
                     int left_kind);
int sbr_num_truth(void);

/* Banker's-round A, return its signed low target word, then restore A.  This
 * is the deliberate 16-bit boundary for coordinates, array subscripts, and
 * other C/OS calls; exact numeric loop state should remain in A or storage. */
int sbr_num_target_int(void);

/* Array cells use sbmem's lo/high/kind planes.  Store is transactional when a
 * first high-plane claim fails and returns -1; load returns zero. */
int sbr_num_array_load(unsigned cell);
int sbr_num_array_store(unsigned cell);

#endif
