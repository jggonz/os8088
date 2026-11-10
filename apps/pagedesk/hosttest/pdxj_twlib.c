/* the host twins as a shared library for hosttest/pdxj_asm.py (ctypes) */
#define PD_HOST 1
#include "../pdxj.h"
unsigned char pd_host_mem[0x110000];
#include "../pdxjtw.c"
unsigned short tw_a(void) { return mq_a; }
unsigned short tw_chi(void) { return mq_chi; }
unsigned short tw_clo(void) { return mq_clo; }
unsigned short tw_ct(void) { return mq_ct; }
unsigned long tw_bp(void) { return (unsigned long)mq_bp; }
