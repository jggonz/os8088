/* Native Turbo BASIC session. Source, arrays and strings are far heap claims;
 * expression values use the shared 8086 software floating-point routines. */
#include "sbasic.h"
/* The larger parser/dispatcher lives in the SDK's relocatable code overlay.
 * Session data and host callbacks remain in the resident segment. */
#define sbc_exec_one ovl_sbc_exec_one
#define sbc_index_program ovl_sbc_index_program
#define sbc_builtin ovl_sbc_builtin
#define sbc_primary ovl_sbc_primary
#define sbv_call ovl_sbv_call
#define sbg_circle ovl_sbg_circle
#define sbg_paint ovl_sbg_paint
#define sbg_image ovl_sbg_image
#define sba_run ovl_sba_run
#define sbc_userfn ovl_sbc_userfn
#define sbe_case ovl_sbe_case
#define sbv_dim ovl_sbv_dim
#define sbv_read ovl_sbv_read
#define sbv_line ovl_sbv_line
#define sbv_print ovl_sbv_print
#define sbe_note ovl_sbe_note
#define sbe_play ovl_sbe_play
#define sbc_clear_state ovl_sbc_clear_state
#define sbn_trig ovl_sbn_trig
#define SBC_MAX_SYMBOLS 320
#define SBC_NAME_MAX 16
#define SBC_MAX_STMTS 3072
#define SBC_MAX_CLAIMS 64
#define SBC_FOR 32
#define SBC_CALLS 24
#define SBC_KEYS 16
struct sbc_symbol {
    char name[SBC_NAME_MAX];
    int scope, type;
    unsigned seg, off, rows, cols;
};
struct sbc_loop { int sym; unsigned off, body; unsigned char limit[8], step[8]; int lf,lv,sf,sv; };
struct sbc_call { unsigned pc; int scope, loops, bindings; };
static struct sb_host *sbc_host;
/* Near callback pointers are resident addresses. An overlay must cross the
 * generated far thunk before making an indirect call through this table. */
static void sbc_h_mode(int m,int w,int h){sbc_host->mode(m,w,h);}
static void sbc_h_clear(int c){sbc_host->clear(c);}
static void sbc_h_text_put(int r,int c,int ch,int f,int b){sbc_host->text_put(r,c,ch,f,b);}
static void sbc_h_text_cursor(int r,int c){sbc_host->text_cursor(r,c);}
static void sbc_h_pixel(int x,int y,int c){sbc_host->pixel(x,y,c);}
static void sbc_h_line(int x,int y,int xx,int yy,int c){sbc_host->line(x,y,xx,yy,c);}
static void sbc_h_sound(int hz,int ticks){sbc_host->sound(hz,ticks);}
static unsigned sbc_h_ticks(void){return sbc_host->ticks();}
static int sbc_status, sbc_lineno;
static char sbc_err[SB_ERROR_MAX];
static unsigned sbc_source_seg, sbc_source_len, sbc_pc, sbc_stmt_start, sbc_stmt_end, sbc_parse_pos;
static unsigned sbc_code_seg, sbc_code_count;
static unsigned sbc_claims[SBC_MAX_CLAIMS];
static unsigned sbc_claim_paras[SBC_MAX_CLAIMS];
static int sbc_nclaims;
static unsigned sbc_arena_seg, sbc_arena_used;
static unsigned sbc_scalar_seg, sbc_string_seg, sbc_string_used;
static unsigned sbc_bind_seg;
static struct sbc_symbol sbc_symbols[SBC_MAX_SYMBOLS];
static int sbc_nsymbols, sbc_scope, sbc_option_base, sbc_default_int;
static struct sbc_loop sbc_for[SBC_FOR];
static int sbc_nfor;
static struct sbc_call sbc_calls[SBC_CALLS];
static int sbc_ncalls;
static unsigned sbc_gosub[SBC_CALLS];
static int sbc_ngosub;
static int sbc_cursor_row, sbc_cursor_col, sbc_fg, sbc_bg, sbc_gfx_mode, sbc_ink;
static unsigned sbc_rand, sbc_defseg, sbc_data_pc, sbc_data_pos;
static int sbc_key_ascii[SBC_KEYS], sbc_key_scan[SBC_KEYS], sbc_key_head, sbc_key_count;
static unsigned sbc_wait_until;
static int sbc_wait_time;
static unsigned sbc_frames, sbc_keypolls;
static unsigned sbc_literal_pos[64];
static unsigned char sbc_literal_value[64][8];
static unsigned char sbc_literal_fast[64];
static int sbc_literal_word[64];
static unsigned sbv_find_cache[32];
static unsigned sbc_symbol_cache[32];
static int sbc_index_program(void);
static int sbc_exec_one(void);
static void sbe_reset(void);
void sb_memzero(unsigned seg, unsigned bytes);
static int sbc_up(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
static int sbc_ch(unsigned p) { return p >= sbc_source_len ? 0 : os88_peek(sbc_source_seg,p) & 255; }
static int sbc_name_eq(const char *a, const char *b)
{
    int i; i = 0;
    while (a[i] && b[i] && sbc_up(a[i]) == sbc_up(b[i])) ++i;
    return !a[i] && !b[i];
}
static void sbc_copy_name(char *d, const char *s)
{
    int i; i = 0; while (i < SBC_NAME_MAX-1 && s[i]) { d[i] = sbc_up(s[i]); ++i; } d[i] = 0;
}
static void sbc_set_error(const char *s)
{
    int i; i = 0; while (i < SB_ERROR_MAX-1 && s[i]) { sbc_err[i] = s[i]; ++i; }
    sbc_err[i] = 0; sbc_status = SB_STATE_ERROR;
}
static void sbc_notify(void) { if (sbc_host && sbc_host->changed) sbc_host->changed(); }
static unsigned sbc_claim(unsigned bytes)
{
    unsigned seg, need;
    if (bytes <= 8192u) {
        need = (bytes + 15u) & 65520u;
        if (!need) need = 16;
        if (!sbc_arena_seg || sbc_arena_used > 16384u - need) {
            sbc_arena_seg = sbc_claim(16384u);
            sbc_arena_used = 0;
            if (!sbc_arena_seg) return 0;
        }
        seg = sbc_arena_seg + (sbc_arena_used >> 4);
        sbc_arena_used += need;
        return seg;
    }
    if (sbc_nclaims == SBC_MAX_CLAIMS) { sbc_set_error("Too many arrays"); return 0; }
    seg = os88_mem_claim((bytes >> 10) + ((bytes & 1023) != 0));
    if (!seg) { sbc_set_error("Not enough BASIC memory"); return 0; }
    sbc_claim_paras[sbc_nclaims] = ((bytes >> 10) + ((bytes & 1023) != 0)) << 6;
    sbc_claims[sbc_nclaims++] = seg;
    sb_memzero(seg,bytes);
    return seg;
}
static int sbc_memory_owned(unsigned seg,unsigned off)
{
    int i;unsigned paragraph;
    paragraph=seg+(off>>4);
    for(i=0;i<sbc_nclaims;++i)
        if(paragraph>=sbc_claims[i]&&paragraph-sbc_claims[i]<sbc_claim_paras[i])return 1;
    sbc_set_error("Invalid BASIC memory address");return 0;
}
static unsigned sbc_getw(unsigned seg, unsigned off)
{ return os88_peek(seg,off) | (os88_peek(seg,off+1) << 8); }
static void sbc_putw(unsigned seg, unsigned off, unsigned v)
{ os88_poke(seg,off,v); os88_poke(seg,off+1,v >> 8); }
static unsigned sbc_rec(unsigned pc, int field)
{ return sbc_getw(sbc_code_seg, pc*8 + field*2); }
static void sbc_link(unsigned pc, unsigned dest) { sbc_putw(sbc_code_seg,pc*8+4,dest); }
#include "sbnum.c"
static int sbc_find_symbol(const char *name, int create)
{
    int i, type;unsigned bucket;
    bucket=0;for(i=0;name[i];++i)bucket=((bucket<<3)+bucket)^sbc_up(name[i]);bucket&=31;
    i=sbc_symbol_cache[bucket]-1;
    if(i>=0&&i<sbc_nsymbols&&sbc_symbols[i].scope==sbc_scope&&sbc_name_eq(name,sbc_symbols[i].name))return i;
    for (i = 0; i < sbc_nsymbols; ++i)
        if (sbc_symbols[i].scope == sbc_scope && sbc_name_eq(name,sbc_symbols[i].name)){sbc_symbol_cache[bucket]=i+1;return i;}
    if (!create) return -1;
    if (sbc_nsymbols == SBC_MAX_SYMBOLS) { sbc_set_error("Symbol table full"); return 0; }
    i = sbc_nsymbols++; sbc_copy_name(sbc_symbols[i].name,name);
    sbc_symbol_cache[bucket]=i+1;
    type = sbc_default_int ? 2 : 8;
    while (*name) { if (*name == '$') type = 256; else if (*name == '%') type = 2;
        else if (*name == '&' || *name == '!' || *name == '#') type = 8; ++name; }
    sbc_symbols[i].scope = sbc_scope; sbc_symbols[i].type = type;
    sbc_symbols[i].rows = sbc_symbols[i].cols = 0;
    sbc_symbols[i].seg = sbc_scalar_seg; sbc_symbols[i].off = i*8;
    if (type == 256) {
        if (!sbc_string_seg || sbc_string_used >= 4096u) { sbc_string_seg = sbc_claim(4096u); sbc_string_used = 0; }
        sbc_symbols[i].seg = sbc_string_seg; sbc_symbols[i].off = sbc_string_used; sbc_string_used += 256;
    }
    return i;
}
static int sbc_read_value(int sym, unsigned off)
{
    int n, i, type; unsigned seg;
    seg = sbc_symbols[sym].seg; off += sbc_symbols[sym].off; type = sbc_symbols[sym].type;
    if (type == 256) {
        n = sbn_string(""); if (sbc_status == SB_STATE_ERROR) return n;
        for (i = 0; i < 255; ++i) { sbn_strings[-n-1][i] = os88_peek(seg,off+i); if (!sbn_strings[-n-1][i]) break; }
        sbn_strings[-n-1][255] = 0; return n;
    }
    if (type == 2) return sbn_int((int)(short)sbc_getw(seg,off));
    n = sbn_new(); for (i = 0; i < 8; ++i) sbn_values[n][i] = os88_peek(seg,off+i); return n;
}
static void sbc_write_value(int sym, unsigned off, int n)
{
    int i, type; unsigned seg; char *s;
    if(sbc_status==SB_STATE_ERROR)return;
    seg = sbc_symbols[sym].seg; off += sbc_symbols[sym].off; type = sbc_symbols[sym].type;
    if (type == 256) {
        if (n >= 0) { sbc_set_error("Type mismatch"); return; }
        s = sbn_text(n); i = 0; do { os88_poke(seg,off+i,s[i]); } while (s[i] && ++i < 256);
    } else if (n < 0) sbc_set_error("Type mismatch");
    else if (type == 2) { sbn_unary(9,n); sbc_putw(seg,off,sbn_word(n)); }
    else for (i = 0; i < 8; ++i) os88_poke(seg,off+i,sbn_values[n][i]);
}
static unsigned sbc_next_rand(void) { sbc_rand = sbc_rand*25173u+13849u; return sbc_rand; }
static void sbc_clear_state(void)
{
    int i;
    for(i=0;i<64;++i)sbc_literal_pos[i]=65535u;
    for(i=0;i<32;++i){sbv_find_cache[i]=65535u;sbc_symbol_cache[i]=0;}
    while (sbc_nclaims) os88_mem_free(sbc_claims[--sbc_nclaims]);
    sbc_arena_seg = sbc_arena_used = 0;
    sbc_pc = 0; sbc_lineno = 1; sbc_err[0] = 0; sbc_status = SB_STATE_IDLE;
    sbc_nsymbols = sbc_scope = sbc_option_base = sbc_default_int = 0;
    sbc_nfor = sbc_ncalls = sbc_ngosub = 0; sbc_data_pc = sbc_data_pos = 0;
    sbc_key_head = sbc_key_count = sbc_wait_time = 0;
    sbc_frames = sbc_keypolls = 0;
    sbe_reset();
    sbc_cursor_row = sbc_cursor_col = 1; sbc_fg = sbc_ink = 15; sbc_bg = sbc_gfx_mode = 0;
    sbc_rand = 0x51a7; sbc_defseg = 0; sbc_string_seg = sbc_string_used = 0;
    sbc_scalar_seg = sbc_claim(SBC_MAX_SYMBOLS*8);
    sbc_bind_seg = sbc_claim(SBC_CALLS*72);
    sbc_code_seg = sbc_claim(SBC_MAX_STMTS*8);
    sbc_code_count = 0;
    if (sbc_host && sbc_host->mode) sbc_h_mode(0,80,25);
    sbs_bios_mode = 3;
    if (sbc_host && sbc_host->clear) sbc_h_clear(0);
}
int sb_init(struct sb_host *host)
{ sbc_host = host; sbc_status = SB_STATE_IDLE; sbc_source_len = 0; sbf_init(); return 0; }
int sb_load_seg(unsigned segment, unsigned length)
{
    if (!segment || length > SB_SOURCE_MAX) { sbc_set_error("Invalid program buffer"); return -1; }
    sbc_source_seg = segment; sbc_source_len = length; sbc_clear_state();
    if (sbc_status == SB_STATE_ERROR || sbc_index_program() < 0) return -1;
    sbc_status = SB_STATE_READY; sbc_notify(); return 0;
}
int sb_load(const char *source, unsigned length)
{
    unsigned i;
    if (length > SB_SOURCE_MAX) return -1;
    if (!sbc_source_seg) sbc_source_seg = os88_mem_claim(32);
    if (!sbc_source_seg) return -1;
    for (i = 0; i < length; ++i) os88_poke(sbc_source_seg,i,source[i]);
    return sb_load_seg(sbc_source_seg,length);
}
void sb_key(int ascii, int scan)
{
    int n; if (sbc_key_count == SBC_KEYS) return;
    n = (sbc_key_head+sbc_key_count)&15; sbc_key_ascii[n] = ascii; sbc_key_scan[n] = scan; ++sbc_key_count;
}
void sb_stop(void) { sbc_status = SB_STATE_STOPPED; sbc_notify(); }
void sb_reset(void) { if (sbc_source_seg) sb_load_seg(sbc_source_seg,sbc_source_len); }
int sb_state(void) { return sbc_status; }
int sb_line(void) { return sbc_lineno; }
const char *sb_error(void) { return sbc_err; }
#include "sbparse.c"
#include "sbcompile.c"
#include "sbvm.c"
