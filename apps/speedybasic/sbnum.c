/* Packed IEEE doubles, operated on by the shared 8086 software FP library.
 * C never takes the address of a stack object and requires no coprocessor. */
#define SBN_MAX 96
static unsigned char sbn_values[SBN_MAX][8];
static unsigned char sbn_fast[SBN_MAX];
static int sbn_fast_value[SBN_MAX];
static int sbn_top;
static char sbn_strings[16][256];
static int sbn_stop;
static char sbn_format[32];
void sbf_integer(void *out, int value);
void sbf_init(void);
void sbf_parse(void *out, const char *text);
void sbf_format(void *value, char *out);
int sbf_op(int op, void *left, void *right);
int sbf_word(void *value);

static int sbn_new(void)
{
    int n;
    n = sbn_top++;
    if (n >= SBN_MAX) { sbc_set_error("Expression too complex"); sbn_top = SBN_MAX; return 0; }
    sbn_fast[n]=0;
    return n;
}
static void sbn_set_int(int n,int v){v=(short)v;sbn_fast[n]=1;sbn_fast_value[n]=v;sbf_integer(sbn_values[n],v);}
static int sbn_int(int v) { int n; n = sbn_new(); sbn_set_int(n,v); return n; }
static int sbn_parse(const char *s) { int n; n = sbn_new(); sbf_parse(sbn_values[n], s); return n; }
static int sbn_word(int n) { return n < 0 ? 0 : sbn_fast[n]?sbn_fast_value[n]:sbf_word(sbn_values[n]); }
static int sbn_copy(int n)
{
    int r, i;
    r = sbn_new();
    for (i = 0; i < 8; ++i) sbn_values[r][i] = sbn_values[n][i];
    sbn_fast[r]=sbn_fast[n];sbn_fast_value[r]=sbn_fast_value[n];
    return r;
}
static int sbn_string(const char *s)
{
    int n, i;
    if (sbn_stop >= 16) { sbc_set_error("String expression too complex"); return -1; }
    n = sbn_stop++; i = 0;
    while (s[i] && i < 255) { sbn_strings[n][i] = s[i]; ++i; }
    sbn_strings[n][i] = 0;
    return -n-1;
}
static char *sbn_text(int n)
{
    if (n < 0) return sbn_strings[-n-1];
    sbf_format(sbn_values[n], sbn_format);
    return sbn_format;
}
static int sbn_cmp(int a, int b)
{
    int i;
    char *s, *t;
    if (a >= 0 && b >= 0) {if(sbn_fast[a]&&sbn_fast[b])return sbn_fast_value[a]<sbn_fast_value[b]?-1:sbn_fast_value[a]>sbn_fast_value[b]?1:0;return sbf_op(5, sbn_values[a], sbn_values[b]);}
    if (a >= 0 || b >= 0) { sbc_set_error("Type mismatch"); return 0; }
    s = sbn_strings[-a-1]; t = sbn_strings[-b-1]; i = 0;
    while (s[i] && s[i] == t[i]) ++i;
    return (unsigned char)s[i] - (unsigned char)t[i];
}
static int sbn_true(int n)
{
    int i;
    if (n < 0) return sbn_strings[-n-1][0] != 0;
    if(sbn_fast[n])return sbn_fast_value[n]!=0;
    for (i = 0; i < 7; ++i) if (sbn_values[n][i]) return 1;
    if(sbn_values[n][7]&127)return 1;
    return 0;
}
static int sbn_math(int op, int a, int b)
{
    int i, j, v, safe, ai, aj;
    if (a < 0 || b < 0) {
        if (op != '+' || a >= 0 || b >= 0) { sbc_set_error("Type mismatch"); return a; }
        i = 0; while (sbn_strings[-a-1][i]) ++i;
        j = 0; while (i < 255 && sbn_strings[-b-1][j]) sbn_strings[-a-1][i++] = sbn_strings[-b-1][j++];
        sbn_strings[-a-1][i] = 0;
        return a;
    }
    if(sbn_fast[a]&&sbn_fast[b]){
        i=sbn_fast_value[a];j=sbn_fast_value[b];safe=0;v=0;
        if(op=='+'){v=(short)((unsigned)i+(unsigned)j);safe=((i^j)<0)||((i^v)>=0);}
        else if(op=='-'){v=(short)((unsigned)i-(unsigned)j);safe=((i^j)>=0)||((i^v)>=0);}
        else if(op=='*'&&i!=(-32767-1)&&j!=(-32767-1)){ai=i<0?-i:i;aj=j<0?-j:j;if(!aj||ai<=32767/aj){v=i*j;safe=1;}}
        else if((op=='\\'||op=='/')&&j&&!(i==(-32767-1)&&j==-1)){if(op=='\\'||i%j==0){v=i/j;safe=1;}}
        if(safe){sbn_set_int(a,v);return a;}
    }
    if (op == '+' || op == '-' || op == '*' || op == '/' || op == '\\') {
        if ((op == '/' || op == '\\') && !sbn_true(b)) { sbc_set_error("Division by zero"); return a; }
        sbf_op(op == '+' ? 1 : op == '-' ? 2 : op == '*' ? 3 : 4, sbn_values[a], sbn_values[b]);
        sbn_fast[a]=0;
        if(op=='\\')sbf_op(7,sbn_values[a],sbn_values[a]);
    } else {
        i = sbn_word(a); j = sbn_word(b);
        if (op == '\\' || op == 'M') {
            if (!j) { sbc_set_error("Division by zero"); return a; }
            i = op == 'M' ? i % j : i / j;
        } else if (op == 'A') i &= j;
        else if (op == 'O') i |= j;
        else if (op == 'X') i ^= j;
        sbn_set_int(a,i);
    }
    return a;
}
static int sbn_unary(int op, int a)
{
    int b;
    if (a < 0) { sbc_set_error("Type mismatch"); return a; }
    if((op==6||op==7||op==9)&&sbn_fast[a])return a;
    if (op == '-') {sbn_values[a][7] ^= 128;if(sbn_fast[a]){if(sbn_fast_value[a]==(-32767-1))sbn_fast[a]=0;else sbn_fast_value[a]=-sbn_fast_value[a];}}
    else if (op == 'N') sbn_set_int(a, ~sbn_word(a));
    else if (op == 6 || op == 7 || op == 8) {sbf_op(op, sbn_values[a], sbn_values[a]);sbn_fast[a]=0;}
    else if (op == 9) {
        b = sbn_parse("0.5");
        sbn_math('+', a, b); sbf_op(6, sbn_values[a], sbn_values[a]);
        sbn_top = b;
    }
    return a;
}
/* Range-reduced Taylor series. All temporaries are packed doubles too. */
static int sbn_trig(int fn, int a)
{
    int mark, pi, tau, q, x2, term, sum, k, div, tmp;
    mark = sbn_top;
    pi = sbn_parse("3.141592653589793"); tau = sbn_parse("6.283185307179586");
    if (fn == 2) { tmp = sbn_parse("1.570796326794897"); sbn_math('+', a, tmp); sbn_top = tmp; }
    q = sbn_copy(a); sbn_math('+', q, pi); sbn_math('/', q, tau); sbn_unary(6, q);
    sbn_math('*', q, tau); sbn_math('-', a, q);
    x2 = sbn_copy(a); sbn_math('*', x2, a);
    term = sbn_copy(a); sum = sbn_copy(a);
    for (k = 1; k <= 10; ++k) {
        div = sbn_int((2*k)*(2*k+1));
        sbn_math('*', term, x2); sbn_math('/', term, div); sbn_unary('-', term);
        sbn_math('+', sum, term); sbn_top = div;
    }
    for (k = 0; k < 8; ++k) sbn_values[a][k] = sbn_values[sum][k];
    sbn_fast[a]=sbn_fast[sum];sbn_fast_value[a]=sbn_fast_value[sum];
    sbn_top = mark;
    return a;
}
