/* Compact native Speedy Basic core.  This file is included by sbasic.c; the
 * target has no linker, so the parser and VM bodies are included below. */
#include "sbasic.h"
#include "sbnum.h"

#define SBC_NEAR_SOURCE 0
#define SBC_FAR_SOURCE  1
#define SBC_MAX_SYMBOLS 96
#define SBC_NAME_MAX    16
#define SBC_STRINGS     32
#define SBC_STRING_MAX  96
#define SBC_LABELS      48
#define SBC_GOSUB       24
#define SBC_FOR         16
#define SBC_KEYS        16
#define SBC_NEAR_MAX    4096
#define SBC_ARRAYS      32
#define SBC_ARRAY_WORDS SBM_CELL_CAP
#define SBC_DATA_ITEMS  4096
#define SBC_LOOPS       16
#define SBC_SELECTS     8
#define SBC_IFS         16
#define SBC_CALLS       24
#define SBC_SAVES       48
#define SBC_DEFFNS       8

struct sbc_symbol {
    char name[SBC_NAME_MAX];
    int value;
    unsigned numlo;
    int numhi;
    int numkind;
    int strslot;
    int array;
};

struct sbc_array {
    int sym;
    int dim1;
    int dim2;
    unsigned base;
    unsigned count;
};

struct sbc_label {
    char name[SBC_NAME_MAX];
    unsigned pos;
    int ifdepth;
};

struct sbc_forframe {
    int sym;
    int limit;
    int step;
    unsigned body;
};

struct sbc_loopframe {
    int kind;
    unsigned body;
    unsigned cond_begin;
    unsigned cond_end;
};

struct sbc_selectframe { int value; int matched; };

static struct sb_host *sbc_host;
static int sbc_status;
static int sbc_lineno;
static char sbc_err[SB_ERROR_MAX];
#ifdef SB_HOST_TEST
static char sbc_near_source[SBC_NEAR_MAX];
#else
static char sbc_near_source[1];
#endif
static unsigned sbc_source_len;
static unsigned sbc_source_seg;
static int sbc_source_kind;
static unsigned sbc_pc;
static unsigned sbc_stmt_start;
static unsigned sbc_stmt_end;
static unsigned sbc_parse_pos;
static int sbc_cursor_row;
static int sbc_cursor_col;
static int sbc_fg;
static int sbc_bg;
static int sbc_gfx_mode;
static int sbc_ink;
static unsigned sbc_rand;
static unsigned sbc_defseg;
static int sbc_regs[10];
static int sbc_dac_index;
static int sbc_dac_chan;
static int sbc_dac_rgb[3];
static int sbc_option_base;
static int sbc_lpr_x;
static int sbc_lpr_y;
static int sbc_view_x1,sbc_view_y1,sbc_view_x2,sbc_view_y2;
static int sbc_draw_x,sbc_draw_y;
static unsigned sbc_error_target,sbc_error_resume;
static int sbc_jump_ifdepth;

static struct sbc_symbol sbc_symbols[SBC_MAX_SYMBOLS];
static int sbc_nsymbols;
#ifdef SB_HOST_TEST
static char sbc_strings[SBC_STRINGS][SBC_STRING_MAX];
#endif
static int sbc_nstrings;
static struct sbc_label sbc_labels[SBC_LABELS];
static int sbc_nlabels;
static unsigned sbc_gosub[SBC_GOSUB];
/* Four 4-bit control-stack bases paired with each return address.  RETURN may
 * legally occur inside a block in the subroutine; restoring these bases keeps
 * the caller's structured-control stacks balanced. */
static unsigned sbc_gosub_state[SBC_GOSUB];
static int sbc_ngosub;
static struct sbc_forframe sbc_for[SBC_FOR];
static int sbc_nfor;
static int sbc_key_ascii[SBC_KEYS];
static int sbc_key_scan[SBC_KEYS];
static int sbc_key_head;
static int sbc_key_count;
static struct sbc_array sbc_arrays[SBC_ARRAYS];
static int sbc_narrays;
static unsigned sbc_array_used;
static int sbc_mem_ready;
static unsigned sbc_data_base;
static unsigned sbc_ndata;
static unsigned sbc_data_pos;
static struct sbc_loopframe sbc_loops[SBC_LOOPS];
static int sbc_nloops;
static struct sbc_selectframe sbc_selects[SBC_SELECTS];
static int sbc_nselects;
static int sbc_if_taken[SBC_IFS];
static int sbc_nifs;
static unsigned sbc_proc_decl[SBC_LABELS];
struct sbc_callframe { unsigned ret; int savebase; int forbase; int loopbase; int ifbase; int selectbase; };
struct sbc_save { int sym; int value; int strslot; unsigned lo; int hi; int kind; };
static struct sbc_callframe sbc_calls[SBC_CALLS];
static struct sbc_save sbc_saves[SBC_SAVES];
static int sbc_ncalls;
static int sbc_nsaves;
struct sbc_deffn { char name[SBC_NAME_MAX]; char param[SBC_NAME_MAX]; unsigned begin; unsigned end; };
static struct sbc_deffn sbc_deffns[SBC_DEFFNS];
static int sbc_ndefs;
static int sbv_input_active;
static int sbv_input_sym;
static int sbv_input_string;
static int sbv_input_len;
static char sbv_input_buf[SBC_STRING_MAX];
#ifdef SB_HOST_TEST
static int sbc_ignored;
#define SBC_IGNORED() (++sbc_ignored)
#else
#define SBC_IGNORED() ((void)0)
#endif

static int ovl_sbc_index_program(void);
static int sbc_exec_one(void);
static void sbc_input_key(int ascii, int scan);
static int ovl_sbv_string_numeric(int fn);

/* os88.h supplies this on target.  The host test supplies the same symbol. */
int os88_peek(unsigned seg, unsigned off);

static int sbc_up(int c)
{
    if (c >= 'a' && c <= 'z') return c - ('a' - 'A');
    return c;
}

static int sbc_ch(unsigned p)
{
    if (p >= sbc_source_len) return 0;
    if (sbc_source_kind == SBC_FAR_SOURCE)
        return os88_peek(sbc_source_seg, p) & 255;
    return ((unsigned char *)sbc_near_source)[p];
}

static void sbc_copy_name(char *d, const char *s)
{
    int i;
    i = 0;
    while (i < SBC_NAME_MAX - 1 && s[i]) {
        d[i] = (char)sbc_up((unsigned char)s[i]);
        ++i;
    }
    d[i] = 0;
}

static int sbc_name_eq(const char *a, const char *b)
{
    int i;
    i = 0;
    while (a[i] && b[i]) {
        if (sbc_up((unsigned char)a[i]) != sbc_up((unsigned char)b[i]))
            return 0;
        ++i;
    }
    return a[i] == 0 && b[i] == 0;
}

static void sbc_set_error(const char *s)
{
    int i;
    i = 0;
    while (i < SB_ERROR_MAX - 1 && s[i]) {
        sbc_err[i] = s[i];
        ++i;
    }
    sbc_err[i] = 0;
    sbc_status = SB_STATE_ERROR;
}

static void sbc_notify(void)
{
    if (sbc_host && sbc_host->changed) sbc_host->changed();
}

static void sbc_clear_state(void)
{
    int i;
    sbc_pc = 0;
    sbc_lineno = 1;
    sbc_err[0] = 0;
    sbc_nsymbols = 0;
    sbc_nstrings = 0;
    sbc_nlabels = 0;
    sbc_ngosub = 0;
    sbc_nfor = 0;
    sbc_narrays = 0;
    sbc_array_used = 0;
    if (sbc_mem_ready) sbm_reset();
    sbc_ndata = 0;
    sbc_data_base = 0;
    sbc_data_pos = 0;
    sbc_nloops = 0;
    sbc_nselects = 0;
    sbc_nifs = 0;
    sbc_ncalls = 0;
    sbc_nsaves = 0;
    sbc_ndefs = 0;
    sbv_input_active = 0;
#ifdef SB_HOST_TEST
    sbc_ignored = 0;
#endif
    sbc_key_head = 0;
    sbc_key_count = 0;
    sbc_cursor_row = 1;
    sbc_cursor_col = 1;
    sbc_fg = 15;
    sbc_bg = 0;
    sbc_gfx_mode = 0;
    sbc_ink = 15;
    sbc_rand = 0x51a7u;
    sbc_defseg = 0x1000u;
    sbc_dac_index = 0;
    sbc_dac_chan = 0;
    sbc_option_base = 0;
    sbc_lpr_x = sbc_lpr_y = 0;
    sbc_draw_x=sbc_draw_y=0;sbc_error_target=(unsigned)0xffff;
    for (i = 0; i < 10; ++i) sbc_regs[i] = 0;
    for (i = 0; i < SBC_MAX_SYMBOLS; ++i) {
        sbc_symbols[i].strslot = -1;
        sbc_symbols[i].array = -1;
    }
}

static int sbc_find_symbol(const char *name, int create)
{
    int i;
    for (i = 0; i < sbc_nsymbols; ++i)
        if (sbc_name_eq(sbc_symbols[i].name, name)) return i;
    if (!create || sbc_nsymbols >= SBC_MAX_SYMBOLS) return -1;
    i = sbc_nsymbols++;
    sbc_copy_name(sbc_symbols[i].name, name);
    sbc_symbols[i].value = 0;
    sbc_symbols[i].numlo=0;sbc_symbols[i].numhi=0;sbc_symbols[i].numkind=SBN_LONG;
    sbc_symbols[i].strslot = -1;
    sbc_symbols[i].array = -1;
    return i;
}

static void sbc_num_long(int v){sbn_alo=(unsigned)v;sbn_ahi=v<0?-1:0;sbn_akind=SBN_LONG;sbn_err=0;}
static int sbc_num_int(void){unsigned lo;int hi;int kind;int v;lo=sbn_alo;hi=sbn_ahi;kind=sbn_akind;sbn_cint();
#ifdef SB_HOST_TEST
v=(int)sbn_ahi*65536+(int)sbn_alo;
#else
v=(int)sbn_alo;
#endif
sbn_alo=lo;sbn_ahi=hi;sbn_akind=kind;return v;}
static void sbc_num_save_symbol(int s){sbc_symbols[s].numlo=sbn_alo;sbc_symbols[s].numhi=sbn_ahi;sbc_symbols[s].numkind=sbn_akind;sbc_symbols[s].value=sbc_num_int();}
static void sbc_num_load_symbol(int s){sbn_alo=sbc_symbols[s].numlo;sbn_ahi=sbc_symbols[s].numhi;sbn_akind=sbc_symbols[s].numkind;}
static int sbc_symbol_decl_kind(int s){int n;n=0;while(sbc_symbols[s].name[n])++n;if(!n)return -1;if(sbc_symbols[s].name[n-1]=='%'||sbc_symbols[s].name[n-1]=='&')return SBN_LONG;if(sbc_symbols[s].name[n-1]=='!'||sbc_symbols[s].name[n-1]=='#')return SBN_FIXED;return -1;}
static void sbc_num_force(int kind){sbn_err=0;if(kind==SBN_LONG&&sbn_akind==SBN_FIXED)sbn_cint();else if(kind==SBN_FIXED&&sbn_akind==SBN_LONG)sbn_float();}
static void sbc_num_check(void){if(sbn_err==SBN_DIVZERO)sbc_set_error("Division by zero");else if(sbn_err==SBN_DOMAIN)sbc_set_error("Math domain error");}

static int sbc_make_array(int sym, int d1, int d2)
{
    unsigned count;
    int a;
    int i;
    if (d1 < 0 || d2 < -1) return -1;
    count = (unsigned)(d1 - sbc_option_base + 1);
    if (d2 >= 0) count *= (unsigned)(d2 - sbc_option_base + 1);
    if (count > SBC_ARRAY_WORDS - sbc_array_used) return -1;
    if (sbc_narrays >= SBC_ARRAYS) return -1;
    a = sbc_narrays++;
    sbc_arrays[a].sym = sym;
    sbc_arrays[a].dim1 = d1;
    sbc_arrays[a].dim2 = d2;
    i = sbm_make(count);
    if (i < 0) return -1;
    sbc_arrays[a].base = (unsigned)i;
    sbc_arrays[a].count = count;
    sbc_array_used += count;
    sbc_symbols[sym].array = a;
    return a;
}

static int sbc_array_cell(int sym, int i, int j)
{
    int a;
    unsigned off;
    a = sbc_symbols[sym].array;
    if (a < 0) return -1;
    if (i < sbc_option_base || i > sbc_arrays[a].dim1) return -1;
    i -= sbc_option_base;
    if (sbc_arrays[a].dim2 >= 0) {
        if (j < sbc_option_base || j > sbc_arrays[a].dim2) return -1;
        j -= sbc_option_base;
        off = (unsigned)i * (unsigned)(sbc_arrays[a].dim2 - sbc_option_base + 1) + (unsigned)j;
    } else off = (unsigned)i;
    return (int)(sbc_arrays[a].base + off);
}

static int sbc_alloc_string(int sym)
{
    int n;
    if (sym < 0) return -1;
    n = sbc_symbols[sym].strslot;
    if (n >= 0) return n;
    if (sbc_nstrings >= SBC_STRINGS) return -1;
    ++sbc_nstrings;
#ifdef SB_HOST_TEST
    n=sbc_nstrings-1;sbc_strings[n][0]=0;
#else
    n=sbm_make(1);if(n<0)return -1;sbm_str_set((unsigned)n,"",0);
#endif
    sbc_symbols[sym].strslot = n;
    return n;
}

static void sbc_string_set(int slot,const char *s)
{
    int n;n=0;while(s[n]&&n<SBC_STRING_MAX-1)++n;
#ifdef SB_HOST_TEST
    {int i;i=0;while(i<n){sbc_strings[slot][i]=s[i];++i;}sbc_strings[slot][i]=0;}
#else
    sbm_str_set((unsigned)slot,s,(unsigned)n);
#endif
}
static void sbc_string_copy(int slot,char *d)
{
#ifdef SB_HOST_TEST
    int i;i=0;while(sbc_strings[slot][i]&&i<SBC_STRING_MAX-1){d[i]=sbc_strings[slot][i];++i;}d[i]=0;
#else
    sbm_str_copy((unsigned)slot,d,SBC_STRING_MAX);
#endif
}

static unsigned sbc_next_rand(void)
{
    /* A 16-bit full-period LCG, cheap and deterministic on an 8088. */
    sbc_rand = (unsigned)(sbc_rand * 25173u + 13849u);
    return sbc_rand;
}

int sb_init(struct sb_host *host)
{
    sbc_host = host;
    if (!sbc_mem_ready) {
        if (sbm_init() < 0) { sbc_set_error("Runtime memory unavailable"); return -1; }
        sbc_mem_ready = 1;
    }
    sbc_source_len = 0;
    sbc_source_kind = SBC_NEAR_SOURCE;
    sbc_clear_state();
    sbc_status = SB_STATE_IDLE;
    return 0;
}

int sb_load(const char *source, unsigned length)
{
    unsigned i;
    if (!source) {
        sbc_set_error("No BASIC source");
        return -1;
    }
#ifdef SB_HOST_TEST
    if (length >= SBC_NEAR_MAX) {
        sbc_set_error("Program is too large");
        return -1;
    }
    for (i = 0; i < length; ++i) sbc_near_source[i] = source[i];
    sbc_near_source[length] = 0;
#else
    (void)i;
    if (length) {
        sbc_set_error("Use far program buffer");
        return -1;
    }
    sbc_near_source[0] = 0;
#endif
    sbc_source_kind = SBC_NEAR_SOURCE;
    sbc_source_len = length;
    sbc_clear_state();
    if (ovl_sbc_index_program() < 0) return -1;
    sbc_status = SB_STATE_READY;
    sbc_notify();
    return 0;
}

int sb_load_seg(unsigned segment, unsigned length)
{
    if ((!segment && length) || length > SB_SOURCE_MAX) {
        sbc_set_error("Invalid program buffer");
        return -1;
    }
    sbc_source_kind = SBC_FAR_SOURCE;
    sbc_source_seg = segment;
    sbc_source_len = length;
    sbc_clear_state();
    if (ovl_sbc_index_program() < 0) return -1;
    sbc_status = SB_STATE_READY;
    sbc_notify();
    return 0;
}

void sb_key(int ascii, int scan)
{
    int tail;
    if (sbc_status == SB_STATE_WAITING && sbv_input_active) {
        sbc_input_key(ascii, scan);
        return;
    }
    if (sbc_key_count >= SBC_KEYS) return;
    tail = (sbc_key_head + sbc_key_count) & (SBC_KEYS - 1);
    sbc_key_ascii[tail] = ascii;
    sbc_key_scan[tail] = scan;
    ++sbc_key_count;
}

void sb_stop(void)
{
    if (sbc_status == SB_STATE_RUNNING || sbc_status == SB_STATE_WAITING ||
        sbc_status == SB_STATE_READY)
        sbc_status = SB_STATE_STOPPED;
    sbc_notify();
}

void sb_reset(void)
{
    sbc_clear_state();
    sbc_status = sbc_source_len ? SB_STATE_READY : SB_STATE_IDLE;
    sbc_notify();
}

int sb_state(void) { return sbc_status; }
int sb_line(void) { return sbc_lineno; }
const char *sb_error(void) { return sbc_err; }
#ifdef SB_HOST_TEST
int sb_hosttest_ignored(void) { return sbc_ignored; }
unsigned sb_hosttest_pos(void) { return sbc_stmt_start; }
#endif

/* One translation unit on SmallerC. */
#include "sbparse.c"
#include "sbvm.c"
