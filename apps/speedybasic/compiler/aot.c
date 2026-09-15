/* Direct 8086 emitter for the fixed SPEEDYCC.RT runtime template. */

#include "os88.h"
#include "aot.h"

#define SBAOT_TABLE       (SBAOT_CODE + 32u)
#define SBAOT_BODY        (SBAOT_TABLE + 256u)
#define SBAOT_PC          0x7e76u
#define SBAOT_MAX_STMTS   128

#define SBAOT_RT_PRINT    0x3284u
#define SBAOT_RT_PRINT_NL 0x3766u
#define SBAOT_RT_SCREEN   0x33c2u
#define SBAOT_RT_CLS      0x3430u
#define SBAOT_RT_COLOR    0x3458u
#define SBAOT_RT_LOCATE   0x3481u
#define SBAOT_RT_PSET     0x34eau
#define SBAOT_RT_LINE     0x3500u

int sbaot_arg[6];
char *sbaot_text;

static unsigned sbaot_seg;
static unsigned sbaot_pos;
static unsigned sbaot_strings;
static int sbaot_count;
static int sbaot_last;
static int sbaot_err;

static int ovl_sbaot_get(unsigned off)
{
    return os88_peek(sbaot_seg, off) & 255;
}

static void ovl_sbaot_put_at(unsigned off, int value)
{
    os88_poke(sbaot_seg, off, value & 255);
}

static void ovl_sbaot_word_at(unsigned off, unsigned value)
{
    ovl_sbaot_put_at(off, (int)value);
    ovl_sbaot_put_at(off + 1u, (int)(value >> 8));
}

static int ovl_sbaot_room(unsigned n)
{
    if (sbaot_pos + n > sbaot_strings) {
        sbaot_err = SBAOT_E_SPACE;
        return 0;
    }
    return 1;
}

static void ovl_sbaot_byte(int value)
{
    ovl_sbaot_put_at(sbaot_pos++, value);
}

static void ovl_sbaot_word(unsigned value)
{
    ovl_sbaot_byte((int)value);
    ovl_sbaot_byte((int)(value >> 8));
}

static int ovl_sbaot_push(int value)
{
    if (!ovl_sbaot_room(4)) return -1;
    ovl_sbaot_byte(0xb8);                 /* mov ax, imm16; push ax (8086) */
    ovl_sbaot_word((unsigned)value);
    ovl_sbaot_byte(0x50);
    return 0;
}

static int ovl_sbaot_call(unsigned target, int words)
{
    int rel;
    if (!ovl_sbaot_room(6)) return -1;
    ovl_sbaot_byte(0xe8);
    rel = (int)(target - (sbaot_pos + 2u));
    ovl_sbaot_word((unsigned)rel);
    if (words) {
        ovl_sbaot_byte(0x83);             /* add sp, byte argument count */
        ovl_sbaot_byte(0xc4);
        ovl_sbaot_byte(words << 1);
    }
    return 0;
}

static int ovl_sbaot_return(int result)
{
    if (!ovl_sbaot_room(5)) return -1;
    ovl_sbaot_byte(0xb8); ovl_sbaot_word((unsigned)result);
    ovl_sbaot_byte(0x5d);                 /* pop bp; ret */
    ovl_sbaot_byte(0xc3);
    return 0;
}

static int ovl_sbaot_continue(void)
{
    if (!ovl_sbaot_room(9)) return -1;
    ovl_sbaot_byte(0xff); ovl_sbaot_byte(0x06); ovl_sbaot_word(SBAOT_PC);
    return ovl_sbaot_return(0);
}

static int ovl_sbaot_string(void)
{
    int n, i;
    n = 0;
    if (!sbaot_text) { sbaot_err = SBAOT_E_PROGRAM; return -1; }
    while (n < 95 && sbaot_text[n]) ++n;
    if (sbaot_text[n] || sbaot_strings < sbaot_pos + (unsigned)n + 1u) {
        sbaot_err = SBAOT_E_SPACE;
        return -1;
    }
    sbaot_strings -= (unsigned)n + 1u;
    for (i = 0; i <= n; ++i)
        ovl_sbaot_put_at(sbaot_strings + (unsigned)i, sbaot_text[i]);
    return (int)sbaot_strings;
}

static int ovl_sbaot_name(const char *name)
{
    int i, ch;
    for (i = 0; i < 15 && name[i] && name[i] != '.'; ++i) {
        ch = (unsigned char)name[i];
        if (ch < 0x20 || ch > 0x7e) return -1;
        ovl_sbaot_put_at(16u + (unsigned)i, ch);
        ovl_sbaot_put_at(SBAOT_TITLE + (unsigned)i, ch);
    }
    if (!i || (name[i] && name[i] != '.')) return -1;
    while (i < 16) {
        ovl_sbaot_put_at(16u + (unsigned)i, 0);
        ovl_sbaot_put_at(SBAOT_TITLE + (unsigned)i, 0);
        ++i;
    }
    return 0;
}

int ovl_sbaot_begin(unsigned segment, const char *name)
{
    int i;
    sbaot_seg = segment;
    sbaot_err = SBAOT_OK;
    if (!segment || ovl_sbaot_get(0) != 'O' || ovl_sbaot_get(1) != '8'
        || ovl_sbaot_get(2) != 3 || ovl_sbaot_get(3) != 1
        || ovl_sbaot_get(8) != (SBAOT_TEMPLATE_SIZE & 255)
        || ovl_sbaot_get(9) != (SBAOT_TEMPLATE_SIZE >> 8)
        || ovl_sbaot_get(10) != (SBAOT_TEMPLATE_BSS & 255)
        || ovl_sbaot_get(11) != (SBAOT_TEMPLATE_BSS >> 8)
        || ovl_sbaot_get(12) != 0xff || ovl_sbaot_get(13) != 0xd5
        || ovl_sbaot_get(14) != 0xcb) {
        sbaot_err = SBAOT_E_TEMPLATE;
        return -1;
    }
    if (ovl_sbaot_name(name) < 0) {
        sbaot_err = SBAOT_E_NAME;
        return -1;
    }
    sbaot_pos = SBAOT_BODY;
    sbaot_strings = SBAOT_CODE + SBAOT_CODE_SIZE;
    sbaot_count = 0;
    sbaot_last = -1;
    for (i = 0; i < 32; ++i) ovl_sbaot_put_at(SBAOT_CODE + (unsigned)i, 0x90);
    return 0;
}

int ovl_sbaot_emit(int op)
{
    int text, i, words;
    unsigned call;
    if (sbaot_err || sbaot_count >= SBAOT_MAX_STMTS) {
        if (!sbaot_err) sbaot_err = SBAOT_E_PROGRAM;
        return -1;
    }
    ovl_sbaot_word_at(SBAOT_TABLE + (unsigned)(sbaot_count << 1), sbaot_pos);
    ++sbaot_count;
    sbaot_last = op;
    call = 0; words = 0;
    if (op == SBAOT_END) return ovl_sbaot_return(1);
    if (op == SBAOT_CLS) call = SBAOT_RT_CLS;
    else if (op == SBAOT_PRINT) {
        text = ovl_sbaot_string();
        if (text < 0 || ovl_sbaot_push(text) < 0) return -1;
        call = SBAOT_RT_PRINT; words = 1;
    } else if (op == SBAOT_SCREEN) { call = SBAOT_RT_SCREEN; words = 1; }
    else if (op == SBAOT_PSET) { call = SBAOT_RT_PSET; words = 3; }
    else if (op == SBAOT_LINE) { call = SBAOT_RT_LINE; words = 5; }
    else if (op == SBAOT_COLOR) { call = SBAOT_RT_COLOR; words = 2; }
    else if (op == SBAOT_LOCATE) { call = SBAOT_RT_LOCATE; words = 2; }
    else { sbaot_err = SBAOT_E_OP; return -1; }
    if (op != SBAOT_PRINT)
        for (i = words - 1; i >= 0; --i)
            if (ovl_sbaot_push(sbaot_arg[i]) < 0) return -1;
    if (ovl_sbaot_call(call, words) < 0) return -1;
    if (op == SBAOT_PRINT && ovl_sbaot_call(SBAOT_RT_PRINT_NL, 0) < 0)
        return -1;
    return ovl_sbaot_continue();
}

int ovl_sbaot_finish(void)
{
    unsigned p;
    if (sbaot_err) return -1;
    if (sbaot_last != SBAOT_END && ovl_sbaot_emit(SBAOT_END) < 0) return -1;
    p = SBAOT_CODE;
    ovl_sbaot_put_at(p++, 0x55);           /* push bp; mov bp,sp */
    ovl_sbaot_put_at(p++, 0x89); ovl_sbaot_put_at(p++, 0xe5);
    ovl_sbaot_put_at(p++, 0x8b); ovl_sbaot_put_at(p++, 0x1e); ovl_sbaot_word_at(p, SBAOT_PC); p += 2;
    ovl_sbaot_put_at(p++, 0x81); ovl_sbaot_put_at(p++, 0xfb); ovl_sbaot_word_at(p, (unsigned)sbaot_count); p += 2;
    ovl_sbaot_put_at(p++, 0x73); ovl_sbaot_put_at(p++, 6);
    ovl_sbaot_put_at(p++, 0xd1); ovl_sbaot_put_at(p++, 0xe3);
    ovl_sbaot_put_at(p++, 0xff); ovl_sbaot_put_at(p++, 0xa7); ovl_sbaot_word_at(p, SBAOT_TABLE); p += 2;
    ovl_sbaot_put_at(p++, 0xb8); ovl_sbaot_word_at(p, 1); p += 2;
    ovl_sbaot_put_at(p++, 0x5d); ovl_sbaot_put_at(p, 0xc3);
    return 0;
}

int ovl_sbaot_error(void)
{
    return sbaot_err;
}
