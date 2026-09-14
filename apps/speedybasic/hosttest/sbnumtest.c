#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "sbnum.h"

static int checks;
static int fails;

static void put_a(int32_t v, int kind)
{
    sbn_alo = (unsigned short)v;
    sbn_ahi = (short)((uint32_t)v >> 16);
    sbn_akind = (short)kind;
}

static void put_b(int32_t v, int kind)
{
    sbn_blo = (unsigned short)v;
    sbn_bhi = (short)((uint32_t)v >> 16);
    sbn_bkind = (short)kind;
}

static int32_t get_a(void)
{
    return (int32_t)((uint32_t)sbn_alo | ((uint32_t)(uint16_t)sbn_ahi << 16));
}

static void ck(int yes, const char *name)
{
    ++checks;
    if (!yes) {
        ++fails;
        fprintf(stderr, "FAIL: %s\n", name);
    }
}

static void binary(void (*fn)(void), int32_t a, int ak, int32_t b, int bk,
                   int32_t want, int wk, int err, const char *name)
{
    put_a(a, ak);
    put_b(b, bk);
    fn();
    ck(get_a() == want && sbn_akind == wk && sbn_err == err, name);
}

static void unary(void (*fn)(void), int32_t a, int ak, int32_t want,
                  int wk, int err, const char *name)
{
    put_a(a, ak);
    fn();
    ck(get_a() == want && sbn_akind == wk && sbn_err == err, name);
}

static void test_reference(void)
{
    int i;
    static const double ang[] = {-20.0, -3.14159265, -1.57079633, -0.5,
                                 0.0, 0.5, 1.57079633, 3.14159265, 20.0};
    binary(sbn_add, INT32_C(2000000000),SBN_LONG,INT32_C(147483647),SBN_LONG,
           INT32_C(2147483647),SBN_LONG,SBN_OK,"LONG add boundary");
    binary(sbn_add, INT32_MAX,SBN_LONG,1,SBN_LONG,0,SBN_LONG,SBN_OVERFLOW,
           "LONG add overflow");
    binary(sbn_sub, INT32_MIN,SBN_LONG,1,SBN_LONG,0,SBN_LONG,SBN_OVERFLOW,
           "LONG sub overflow");
    binary(sbn_add,3,SBN_LONG,32768,SBN_FIXED,229376,SBN_FIXED,SBN_OK,
           "mixed promotion");
    binary(sbn_add,40000,SBN_LONG,32768,SBN_FIXED,0,SBN_FIXED,SBN_OVERFLOW,
           "mixed promotion overflow");
    binary(sbn_mul,-123456,SBN_LONG,7890,SBN_LONG,-974067840,SBN_LONG,SBN_OK,
           "LONG multiply exact");
    binary(sbn_mul,INT32_MAX,SBN_LONG,2,SBN_LONG,0,SBN_LONG,SBN_OVERFLOW,
           "LONG multiply overflow");
    binary(sbn_mul,98304,SBN_FIXED,-147456,SBN_FIXED,-221184,SBN_FIXED,SBN_OK,
           "FIXED multiply");
    binary(sbn_div,-7,SBN_LONG,3,SBN_LONG,-2,SBN_LONG,SBN_OK,
           "LONG division truncates");
    binary(sbn_div,65536,SBN_FIXED,196608,SBN_FIXED,21845,SBN_FIXED,SBN_OK,
           "FIXED division third");
    binary(sbn_div,1,SBN_LONG,0,SBN_LONG,0,SBN_LONG,SBN_DIVZERO,
           "division by zero");
    binary(sbn_fdiv,1,SBN_LONG,2,SBN_LONG,32768,SBN_FIXED,SBN_OK,
           "BASIC fractional LONG division");
    binary(sbn_fdiv,40000,SBN_LONG,10000,SBN_LONG,262144,SBN_FIXED,SBN_OK,
           "BASIC division with large LONG inputs");
    unary(sbn_floor,-117965,SBN_FIXED,-2,SBN_LONG,SBN_OK,"floor negative");
    unary(sbn_fix,-117965,SBN_FIXED,-1,SBN_LONG,SBN_OK,"fix negative");
    unary(sbn_cint,163840,SBN_FIXED,2,SBN_LONG,SBN_OK,"CINT positive tie even");
    unary(sbn_cint,229376,SBN_FIXED,4,SBN_LONG,SBN_OK,"CINT positive tie odd");
    unary(sbn_cint,-163840,SBN_FIXED,-2,SBN_LONG,SBN_OK,"CINT negative tie even");
    unary(sbn_cint,-229376,SBN_FIXED,-4,SBN_LONG,SBN_OK,"CINT negative tie odd");
    unary(sbn_float,3,SBN_LONG,196608,SBN_FIXED,SBN_OK,"LONG to FIXED");
    unary(sbn_float,-32768,SBN_LONG,INT32_MIN,SBN_FIXED,SBN_OK,
          "LONG to FIXED lower boundary");
    unary(sbn_float,40000,SBN_LONG,0,SBN_FIXED,SBN_OVERFLOW,
          "LONG to FIXED overflow");
    unary(sbn_float,98304,SBN_FIXED,98304,SBN_FIXED,SBN_OK,
          "FIXED conversion identity");
    unary(sbn_sqrt,2,SBN_LONG,92682,SBN_FIXED,SBN_OK,"sqrt 2");
    unary(sbn_sqrt,589824,SBN_FIXED,196608,SBN_FIXED,SBN_OK,"sqrt fixed 9");
    unary(sbn_sqrt,-1,SBN_LONG,0,SBN_FIXED,SBN_DOMAIN,"sqrt domain");
    put_a(-40000,SBN_LONG); put_b(0,SBN_FIXED);
    ck(sbn_cmp() == -1 && sbn_err == SBN_OK,"mixed compare below fixed range");
    put_a(40000,SBN_LONG); put_b(0,SBN_FIXED);
    ck(sbn_cmp() == 1 && sbn_err == SBN_OK,"mixed compare above fixed range");
    for (i = 0; i < (int)(sizeof(ang)/sizeof(ang[0])); ++i) {
        double got;
        put_a((int32_t)llround(ang[i] * 65536.0),SBN_FIXED);
        sbn_sin(); got = get_a() / 65536.0;
        ck(fabs(got - sin(ang[i])) < 0.00035,"sin accuracy");
        put_a((int32_t)llround(ang[i] * 65536.0),SBN_FIXED);
        sbn_cos(); got = get_a() / 65536.0;
        ck(fabs(got - cos(ang[i])) < 0.00035,"cos accuracy");
    }
}

struct tc {
    const char *name;
    int op;
    int32_t a;
    int ak;
    int32_t b;
    int bk;
    int neg;
};

static const struct tc cases[] = {
    {"addcarry",1,INT32_C(0x0001ffff),SBN_LONG,2,SBN_LONG,0},
    {"addmixed",1,3,SBN_LONG,32768,SBN_FIXED,0},
    {"addov",1,INT32_MAX,SBN_LONG,1,SBN_LONG,0},
    {"subborrow",2,INT32_C(0x10000),SBN_LONG,1,SBN_LONG,0},
    {"subov",2,INT32_MIN,SBN_LONG,1,SBN_LONG,0},
    {"mullong",3,-123456,SBN_LONG,7890,SBN_LONG,0},
    {"mulfix",3,98304,SBN_FIXED,-147456,SBN_FIXED,0},
    {"mulov",3,INT32_MAX,SBN_LONG,2,SBN_LONG,0},
    {"divlong",4,-7,SBN_LONG,3,SBN_LONG,0},
    {"divfix",4,65536,SBN_FIXED,196608,SBN_FIXED,0},
    {"divzero",4,1,SBN_LONG,0,SBN_LONG,0},
    {"neg",5,INT32_C(0x12345678),SBN_LONG,0,SBN_LONG,0},
    {"negov",5,INT32_MIN,SBN_LONG,0,SBN_LONG,0},
    {"floor",6,-117965,SBN_FIXED,0,SBN_LONG,0},
    {"fix",7,-117965,SBN_FIXED,0,SBN_LONG,0},
    {"cinteven",8,163840,SBN_FIXED,0,SBN_LONG,0},
    {"cintodd",8,-229376,SBN_FIXED,0,SBN_LONG,0},
    {"sqrt2",9,2,SBN_LONG,0,SBN_LONG,0},
    {"sqrt9",9,589824,SBN_FIXED,0,SBN_LONG,0},
    {"sqrtneg",9,-1,SBN_LONG,0,SBN_LONG,0},
    {"sinhalf",10,32768,SBN_FIXED,0,SBN_LONG,0},
    {"sinreduce",10,1310720,SBN_FIXED,0,SBN_LONG,0},
    {"coszero",11,0,SBN_FIXED,0,SBN_LONG,0},
    {"cospi",11,205887,SBN_FIXED,0,SBN_LONG,0},
    {"cmplt",12,-40000,SBN_LONG,0,SBN_FIXED,0},
    {"cmpgt",12,40000,SBN_LONG,0,SBN_FIXED,0},
    {"cmpeq",12,3,SBN_LONG,196608,SBN_FIXED,0},
    {"float3",13,3,SBN_LONG,0,SBN_LONG,0},
    {"floatmin",13,-32768,SBN_LONG,0,SBN_LONG,0},
    {"floatov",13,40000,SBN_LONG,0,SBN_LONG,0},
    {"fdivhalf",14,1,SBN_LONG,2,SBN_LONG,0},
    {"fdivlarge",14,40000,SBN_LONG,10000,SBN_LONG,0},
    {"negctl",1,1,SBN_LONG,2,SBN_LONG,1}
};

static int runop(int op)
{
    switch (op) {
    case 1:sbn_add();break; case 2:sbn_sub();break; case 3:sbn_mul();break;
    case 4:sbn_div();break; case 5:sbn_neg();break; case 6:sbn_floor();break;
    case 7:sbn_fix();break; case 8:sbn_cint();break; case 9:sbn_sqrt();break;
    case 10:sbn_sin();break; case 11:sbn_cos();break;
    case 12:return sbn_cmp();
    case 13:sbn_float();break;
    case 14:sbn_fdiv();break;
    }
    return 0;
}

static void emit(FILE *f)
{
    unsigned i;
    fprintf(f,"sbn_cases:\n");
    for(i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        int rv;
        int32_t av;
        put_a(cases[i].a,cases[i].ak); put_b(cases[i].b,cases[i].bk);
        rv=runop(cases[i].op); av=get_a();
        if(cases[i].neg) av ^= 1;       /* the target comparison must catch it */
        fprintf(f," dw sbn_name_%u,%d,0x%04x,0x%04x,%d,0x%04x,0x%04x,%d,"
                  "0x%04x,0x%04x,%d,%d,%d,%d\n",i,cases[i].op,
                  (unsigned)cases[i].a&65535u,(unsigned)((uint32_t)cases[i].a>>16),cases[i].ak,
                  (unsigned)cases[i].b&65535u,(unsigned)((uint32_t)cases[i].b>>16),cases[i].bk,
                  (unsigned)av&65535u,(unsigned)((uint32_t)av>>16),sbn_akind,sbn_err,rv,cases[i].neg);
    }
    fprintf(f,"sbn_cases_end:\n");
    for(i=0;i<sizeof(cases)/sizeof(cases[0]);++i)
        fprintf(f,"sbn_name_%u: db '%s',0\n",i,cases[i].name);
}

int main(int argc, char **argv)
{
    test_reference();
    if (fails) {
        fprintf(stderr,"sbnum reference: %d/%d failed\n",fails,checks);
        return 1;
    }
    if (argc == 3 && !strcmp(argv[1],"--emit")) {
        FILE *f=fopen(argv[2],"w");
        if(!f)return 2; emit(f); if(fclose(f))return 2;
    }
    printf("sbnum reference: %d checks - PASS\n",checks);
    return 0;
}
