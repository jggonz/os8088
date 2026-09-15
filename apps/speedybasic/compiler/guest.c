/* Source-to-package driver that runs inside Speedy BASIC on os8088. */

#include "os88.h"
#include "writer.h"
#include "aot.h"

#define SBG_LINE_MAX 79

static unsigned sbg_source_seg;
static unsigned sbg_source_len;
static unsigned sbg_source_pos;
static int sbg_line_no;
static char sbg_line[SBG_LINE_MAX + 1];
static char *sbg_at;
static int sbg_num;
static int sbg_a, sbg_b;
static char sbg_number[8];
static struct os88_place sbg_template_place;
static struct os88_place sbg_output_place;

static void ovl_sbg_error(char *error, unsigned cap, const char *message)
{
    unsigned n;
    if (!error || !cap) return;
    n = 0;
    while (message[n] && n + 1u < cap) { error[n] = message[n]; ++n; }
    error[n] = 0;
}

static int ovl_sbg_up(int ch)
{
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 'A';
    return ch;
}

static char *ovl_sbg_space(char *p)
{
    while (*p == ' ' || *p == '\t') ++p;
    return p;
}

static int ovl_sbg_kw(const char *word)
{
    char *p;
    int i;
    p = ovl_sbg_space(sbg_at);
    i = 0;
    while (word[i] && ovl_sbg_up((unsigned char)p[i]) == word[i]) ++i;
    if (word[i] || (p[i] && p[i] != ' ' && p[i] != '\t')) return 0;
    sbg_at = p + i;
    return 1;
}

static int ovl_sbg_number(void)
{
    char *p;
    unsigned value;
    int sign, base, digit, any;
    p = ovl_sbg_space(sbg_at);
    sign = 1;
    if (*p == '-') { sign = -1; ++p; }
    else if (*p == '+') ++p;
    base = 10;
    if (p[0] == '&' && ovl_sbg_up((unsigned char)p[1]) == 'H') {
        base = 16; p += 2;
    } else if (p[0] == '&' && ovl_sbg_up((unsigned char)p[1]) == 'O') {
        base = 8; p += 2;
    }
    value = 0; any = 0;
    for (;;) {
        if (*p >= '0' && *p <= '9') digit = *p - '0';
        else if (ovl_sbg_up((unsigned char)*p) >= 'A'
                 && ovl_sbg_up((unsigned char)*p) <= 'F')
            digit = ovl_sbg_up((unsigned char)*p) - 'A' + 10;
        else break;
        if (digit >= base) break;
        value = value * (unsigned)base + (unsigned)digit;
        any = 1; ++p;
    }
    if (!any || value > (sign < 0 ? 32768u : 65535u)) return 0;
    sbg_num = sign < 0 ? -(int)value : (int)value;
    sbg_at = p;
    return 1;
}

static int ovl_sbg_ch(int wanted)
{
    char *p;
    p = ovl_sbg_space(sbg_at);
    if ((unsigned char)*p != wanted) return 0;
    sbg_at = p + 1;
    return 1;
}

static int ovl_sbg_end(void)
{
    return *ovl_sbg_space(sbg_at) == 0;
}

static int ovl_sbg_next_line(void)
{
    int ch, quote;
    unsigned n;
    if (sbg_source_pos >= sbg_source_len) return 0;
    n = 0; quote = 0; ++sbg_line_no;
    while (sbg_source_pos < sbg_source_len) {
        ch = os88_peek(sbg_source_seg, sbg_source_pos++) & 255;
        if (ch == '\r') continue;
        if (ch == '\n') break;
        if (ch == '"') quote = !quote;
        if (ch == '\'' && !quote) {
            while (sbg_source_pos < sbg_source_len) {
                ch = os88_peek(sbg_source_seg, sbg_source_pos++) & 255;
                if (ch == '\n') break;
            }
            break;
        }
        if (n >= SBG_LINE_MAX) return -1;
        sbg_line[n++] = (char)ch;
    }
    sbg_line[n] = 0;
    return 1;
}

static int ovl_sbg_pair(void)
{
    if (!ovl_sbg_number()) return 0;
    sbg_a = sbg_num;
    if (!ovl_sbg_ch(',') || !ovl_sbg_number()) return 0;
    sbg_b = sbg_num;
    return 1;
}

static int ovl_sbg_print(char *p)
{
    char *text;
    p = ovl_sbg_space(p);
    if (!*p) sbaot_text = p;
    else {
        if (*p++ != '"') return -1;
        text = p;
        while (*p && *p != '"') ++p;
        if (*p != '"') return -1;
        *p++ = 0;
        sbg_at = p;
        if (!ovl_sbg_end()) return -1;
        sbaot_text = text;
    }
    return ovl_sbaot_emit(SBAOT_PRINT);
}

static int ovl_sbg_statement(char *p)
{
    int a, b, c, d, e;
    p = ovl_sbg_space(p);
    if (!*p) return 0;
    sbg_at = p;
    if (ovl_sbg_kw("REM")) return 0;
    sbg_at = p;
    if (ovl_sbg_kw("CLS") && ovl_sbg_end())
        return ovl_sbaot_emit(SBAOT_CLS);
    sbg_at = p;
    if (ovl_sbg_kw("END") && ovl_sbg_end())
        return ovl_sbaot_emit(SBAOT_END);
    sbg_at = p;
    if (ovl_sbg_kw("STOP") && ovl_sbg_end())
        return ovl_sbaot_emit(SBAOT_END);
    sbg_at = p;
    if (ovl_sbg_kw("PRINT")) return ovl_sbg_print(sbg_at);
    sbg_at = p;
    if (ovl_sbg_kw("SCREEN")) {
        if (!ovl_sbg_number()) return -1;
        a = sbg_num;
        if (!ovl_sbg_end()) return -1;
        sbaot_arg[0] = a; return ovl_sbaot_emit(SBAOT_SCREEN);
    }
    sbg_at = p;
    if (ovl_sbg_kw("COLOR")) {
        if (!ovl_sbg_pair()) return -1;
        a = sbg_a; b = sbg_b;
        if (!ovl_sbg_end()) return -1;
        sbaot_arg[0] = a; sbaot_arg[1] = b;
        return ovl_sbaot_emit(SBAOT_COLOR);
    }
    sbg_at = p;
    if (ovl_sbg_kw("LOCATE")) {
        if (!ovl_sbg_pair()) return -1;
        a = sbg_a; b = sbg_b;
        if (!ovl_sbg_end()) return -1;
        sbaot_arg[0] = a; sbaot_arg[1] = b;
        return ovl_sbaot_emit(SBAOT_LOCATE);
    }
    sbg_at = p;
    if (ovl_sbg_kw("PSET")) {
        if (!ovl_sbg_ch('(') || !ovl_sbg_pair()) return -1;
        a = sbg_a; b = sbg_b;
        if (!ovl_sbg_ch(')') || !ovl_sbg_ch(',')
            || !ovl_sbg_number()) return -1;
        c = sbg_num;
        if (!ovl_sbg_end()) return -1;
        sbaot_arg[0] = a; sbaot_arg[1] = b; sbaot_arg[2] = c;
        return ovl_sbaot_emit(SBAOT_PSET);
    }
    sbg_at = p;
    if (ovl_sbg_kw("LINE")) {
        if (!ovl_sbg_ch('(') || !ovl_sbg_pair()) return -1;
        a = sbg_a; b = sbg_b;
        if (!ovl_sbg_ch(')') || !ovl_sbg_ch('-') || !ovl_sbg_ch('(')
            || !ovl_sbg_pair()) return -1;
        c = sbg_a; d = sbg_b;
        if (!ovl_sbg_ch(')') || !ovl_sbg_ch(',')
            || !ovl_sbg_number()) return -1;
        e = sbg_num;
        if (!ovl_sbg_end()) return -1;
        sbaot_arg[0] = a; sbaot_arg[1] = b; sbaot_arg[2] = c;
        sbaot_arg[3] = d; sbaot_arg[4] = e;
        return ovl_sbaot_emit(SBAOT_LINE);
    }
    return -1;
}

static void ovl_sbg_line_error(char *error, unsigned cap)
{
    unsigned n;
    ovl_sbg_error(error, cap, "Unsupported BASIC at line ");
    os88_utoa((unsigned)sbg_line_no, sbg_number);
    n = os88_strlen(error);
    if (n < cap) os88_strcpy(error + n, sbg_number, cap - n);
}

static void ovl_sbg_package_name(const char *file, char *name)
{
    int i;
    i = 0;
    while (i < 15 && file[i] && file[i] != '.') { name[i] = file[i]; ++i; }
    name[i] = 0;
}

int ovl_sbg_compile(unsigned source_segment, unsigned source_length,
                    const char *output_name, char *error,
                    unsigned error_capacity)
{
    unsigned image_seg, got;
    int next, answer;
    answer = -1;
    image_seg = os88_mem_claim(SBAOT_TEMPLATE_KB);
    if (!image_seg) {
        ovl_sbg_error(error, error_capacity, "Compiler: needs 27K free");
        return -1;
    }
    /* Save As deliberately moves the instance to the user's destination.
     * The runtime template lives beside Speedy BASIC, so visit the launch
     * folder only for this read and restore the chosen destination before
     * the package writer runs. */
    os88_file_here(&sbg_output_place);
    if (os88_file_goto(&sbg_template_place) != 0) {
        os88_file_goto(&sbg_output_place);
        ovl_sbg_error(error, error_capacity,
                      "Compiler: template folder unavailable");
        goto done;
    }
    got = os88_file_read_seg(SBAOT_TEMPLATE_FILE, image_seg,
                             SBAOT_TEMPLATE_KB << 10);
    if (os88_file_goto(&sbg_output_place) != 0) {
        ovl_sbg_error(error, error_capacity,
                      "Compiler: output folder unavailable");
        goto done;
    }
    if (got != SBAOT_TEMPLATE_SIZE) {
        ovl_sbg_error(error, error_capacity,
                      "Compiler: missing/old SPEEDYCC.RT");
        goto done;
    }
    /* Reuse the source-line scratch space while the template is prepared.
     * The name has already been copied into the package before line parsing
     * starts, so carrying a second resident buffer would only consume the
     * application's very tight 60K image+BSS budget. */
    ovl_sbg_package_name(output_name, sbg_line);
    if (sbw_bind(image_seg, SBAOT_TEMPLATE_KB << 10) != SBW_OK
        || sbw_header(SBAOT_TEMPLATE_SIZE, SBAOT_TEMPLATE_BSS,
                      SBAOT_TEMPLATE_ENTRY, SBW_F_ICON) != SBW_OK
        || sbw_name(sbg_line) != SBW_OK
        || ovl_sbaot_begin(image_seg, sbg_line) < 0) {
        ovl_sbg_error(error, error_capacity, "Compiler: invalid template/name");
        goto done;
    }
    sbg_source_seg = source_segment;
    sbg_source_len = source_length;
    sbg_source_pos = 0;
    sbg_line_no = 0;
    for (;;) {
        next = ovl_sbg_next_line();
        if (!next) break;
        if (next < 0 || ovl_sbg_statement(sbg_line) < 0) {
            ovl_sbg_line_error(error, error_capacity);
            goto done;
        }
    }
    if (ovl_sbaot_finish() < 0) {
        ovl_sbg_error(error, error_capacity, "Compiler: program too large");
        goto done;
    }
    if (sbw_write(output_name) != SBW_OK) {
        ovl_sbg_error(error, error_capacity, "Compiler: package write failed");
        goto done;
    }
    answer = 0;
done:
    os88_mem_free(image_seg);
    return answer;
}

void sb_compile_set_home(void)
{
    os88_file_here(&sbg_template_place);
}
