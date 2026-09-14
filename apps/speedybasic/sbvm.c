/* Statement runner.  It parses one source statement immediately before
 * executing it and returns to the shell after a bounded number of statements.
 * Branch tables make GOTO/GOSUB independent of source size. */

static char sbv_name[SBC_NAME_MAX];
static char sbv_numtext[16];
static unsigned sbv_wait_until;
static int sbv_wait_time;
static int sbv_wait_sym = -1;
static char sbv_stra[SBC_STRING_MAX];
static char sbv_strb[SBC_STRING_MAX];
static char sbv_strc[SBC_STRING_MAX];
static int sbv_call_args[8];
static unsigned sbv_call_lo[8];
static int sbv_call_hi[8];
static int sbv_call_kind[8];

static void sbv_put_char(int c);
static void sbv_puts(const char *s);
static void ovl_sbv_eval_string(unsigned begin,unsigned end,char *out);

static void sbv_out(unsigned port, int value)
{
    if (sbc_host && sbc_host->port_out) sbc_host->port_out(port, value);
    if (port == 0x3c8u) { sbc_dac_index = value & 255; sbc_dac_chan = 0; }
    else if (port == 0x3c9u) {
        sbc_dac_rgb[sbc_dac_chan++] = value & 63;
        if (sbc_dac_chan == 3) {
            if (sbc_host && sbc_host->palette)
                sbc_host->palette(sbc_dac_index, sbc_dac_rgb[0], sbc_dac_rgb[1], sbc_dac_rgb[2]);
            sbc_dac_chan = 0;
            sbc_dac_index = (sbc_dac_index + 1) & 255;
        }
    }
}

static void ovl_sbv_draw(void)
{
    int i,cmd,n,nx,ny;
    ovl_sbv_eval_string(sbc_parse_pos,sbc_stmt_end,sbv_stra);i=0;
    while(sbv_stra[i]){cmd=sbc_up((unsigned char)sbv_stra[i++]);n=0;while(sbv_stra[i]>='0'&&sbv_stra[i]<='9')n=sbc_mul10(n)+sbv_stra[i++]-'0';if(!n)n=1;nx=sbc_draw_x;ny=sbc_draw_y;if(cmd=='R')nx+=n;else if(cmd=='L')nx-=n;else if(cmd=='D')ny+=n;else if(cmd=='U')ny-=n;else continue;if(sbc_host&&sbc_host->line)sbc_host->line(sbc_draw_x,sbc_draw_y,nx,ny,sbc_ink);sbc_draw_x=nx;sbc_draw_y=ny;}
}

static int sbv_atoi(const char *s)
{
    int i;
    int sign;
    int n;
    i = 0; sign = 1; n = 0;
    while (s[i] == ' ' || s[i] == '\t') ++i;
    if (s[i] == '-') { sign = -1; ++i; }
    else if (s[i] == '+') ++i;
    while (s[i] >= '0' && s[i] <= '9') n = sbc_mul10(n) + s[i++] - '0';
    return sign * n;
}

static void sbc_input_key(int ascii, int scan)
{
    int slot;
    (void)scan;
    if (!sbv_input_active) return;
    if (ascii == 8) {
        if (sbv_input_len) --sbv_input_len;
        sbv_input_buf[sbv_input_len] = 0;
        return;
    }
    if (ascii != 13 && ascii != 10) {
        if (ascii >= 32 && ascii <= 255 && sbv_input_len < SBC_STRING_MAX - 1) {
            sbv_input_buf[sbv_input_len++] = (char)ascii;
            sbv_input_buf[sbv_input_len] = 0;
            sbv_put_char(ascii);
        }
        return;
    }
    if (sbv_input_string) {
        slot = sbc_alloc_string(sbv_input_sym);
        if (slot < 0) { sbc_set_error("String space full"); return; }
        sbc_string_set(slot,sbv_input_buf);
    } else {sbc_num_long(sbv_atoi(sbv_input_buf));sbc_num_save_symbol(sbv_input_sym);}
    sbv_put_char('\n');
    sbv_input_active = 0;
    sbc_status = SB_STATE_RUNNING;
}

static void sbv_begin_input(void)
{
    int isstr;
    int i;
    int found;
    found = -1; isstr = 0;
    while (sbt_kind != SBT_EOF) {
        if (sbt_kind == SBT_STR) sbv_puts(sbt_text);
        if (sbt_kind == SBT_ID) {
            found = sbc_find_symbol(sbt_text, 1);
            isstr = 0; i = 0;
            while (sbt_text[i]) { if (sbt_text[i] == '$') isstr = 1; ++i; }
        }
        sbc_lex_next();
    }
    if (found < 0) { sbc_set_error("INPUT needs a variable"); return; }
    sbv_input_sym = found;
    sbv_input_string = isstr;
    sbv_input_len = 0;
    sbv_input_buf[0] = 0;
    sbv_input_active = 1;
    sbc_status = SB_STATE_WAITING;
    /* Keystrokes can arrive while the VM is between slices.  Feed queued
     * input through the same editor until this INPUT completes. */
    while (sbv_input_active && sbc_key_count) {
        i = sbc_key_head;
        sbc_key_head = (sbc_key_head + 1) & (SBC_KEYS - 1);
        --sbc_key_count;
        sbc_input_key(sbc_key_ascii[i], sbc_key_scan[i]);
    }
}

static unsigned sbv_skip_space(unsigned p, unsigned e)
{
    while (p < e && (sbc_ch(p) == ' ' || sbc_ch(p) == '\t')) ++p;
    return p;
}

static int sbv_word_at(unsigned p, const char *word)
{
    int i;
    i = 0;
    while (word[i] && sbc_up(sbc_ch(p + i)) == word[i]) ++i;
    if (word[i]) return 0;
    if (sbc_isalpha(sbc_ch(p + i)) || sbc_isdigit(sbc_ch(p + i))) return 0;
    return 1;
}

static unsigned sbv_find_word(unsigned p, unsigned e, const char *word)
{
    int quote;
    int c;
    quote = 0;
    while (p < e) {
        c = sbc_ch(p);
        if (c == '"') quote = !quote;
        if (!quote && (p == 0 || (!sbc_isalpha(sbc_ch(p - 1)) &&
            !sbc_isdigit(sbc_ch(p - 1)))) && sbv_word_at(p, word)) return p;
        ++p;
    }
    return e;
}

static void sbv_put_char(int c)
{
    if (c == '\r') return;
    if (c == '\n') {
        ++sbc_cursor_row;
        sbc_cursor_col = 1;
        if (sbc_cursor_row > 25) {
            sbc_cursor_row = 25;
            if (sbc_host && sbc_host->text_scroll) sbc_host->text_scroll();
        }
    } else {
        if (sbc_host && sbc_host->text_put)
            sbc_host->text_put(sbc_cursor_row, sbc_cursor_col, c, sbc_fg, sbc_bg);
        ++sbc_cursor_col;
        if (sbc_cursor_col > 80) {
            sbc_cursor_col = 1;
            ++sbc_cursor_row;
            if (sbc_cursor_row > 25) {
                sbc_cursor_row = 25;
                if (sbc_host && sbc_host->text_scroll) sbc_host->text_scroll();
            }
        }
    }
    if (sbc_host && sbc_host->text_cursor)
        sbc_host->text_cursor(sbc_cursor_row, sbc_cursor_col);
}

static void sbv_puts(const char *s)
{
    int i;
    i = 0;
    while (s[i]) sbv_put_char((unsigned char)s[i++]);
}

static void sbv_putnum(int v)
{
    int i;
    unsigned n;
    i = 15;
    sbv_numtext[i] = 0;
    if (v < 0) n = (unsigned)(-v); else n = (unsigned)v;
    do {
        sbv_numtext[--i] = (char)('0' + n % 10);
        n /= 10;
    } while (n && i > 1);
    if (v < 0) sbv_numtext[--i] = '-';
    else sbv_numtext[--i] = ' ';
    sbv_puts(sbv_numtext + i);
}

static int sbv_strcat(char *out, int used, const char *s)
{
    int i;
    i = 0;
    while (s[i] && used < SBC_STRING_MAX - 1) out[used++] = s[i++];
    out[used] = 0;
    return used;
}

static void sbv_numstr(int v)
{
    int i;
    unsigned n;
    i = 15; sbv_numtext[i] = 0;
    n = v < 0 ? (unsigned)(-v) : (unsigned)v;
    do { sbv_numtext[--i] = (char)('0' + n % 10); n /= 10; } while (n && i > 1);
    if (v < 0) sbv_numtext[--i] = '-'; else sbv_numtext[--i] = ' ';
    /* Move to the start; the buffer is global so taking its address is safe. */
    n = 0;
    while (sbv_numtext[i]) sbv_numtext[n++] = sbv_numtext[i++];
    sbv_numtext[n] = 0;
}

static int sbv_string_term(char *out)
{
    int kind;
    int n;
    int start;
    int sym;
    int slot;
    int v;
    int c;
    int def;
    unsigned resume;
    unsigned oldend;
    unsigned ulo;
    int uhi;
    int uk;
    out[0] = 0;
    if (sbt_kind == SBT_STR) { sbv_strcat(out, 0, sbt_text); sbc_lex_next(); return 1; }
    if (sbt_kind != SBT_ID) return 0;
    kind = 0;
    if (sbc_name_eq(sbt_text, "INKEY$")) kind = 1;
    else if (sbc_name_eq(sbt_text, "CHR$")) kind = 2;
    else if (sbc_name_eq(sbt_text, "STR$")) kind = 3;
    else if (sbc_name_eq(sbt_text, "LEFT$")) kind = 4;
    else if (sbc_name_eq(sbt_text, "RIGHT$")) kind = 5;
    else if (sbc_name_eq(sbt_text, "MID$")) kind = 6;
    else if (sbc_name_eq(sbt_text, "SPACE$")) kind = 7;
    else if (sbc_name_eq(sbt_text, "UCASE$")) kind = 8;
    else if (sbc_name_eq(sbt_text, "LCASE$")) kind = 9;
    else if (sbc_name_eq(sbt_text,"STRING$")) kind=10;
    else if (sbc_name_eq(sbt_text,"HEX$")) kind=11;
    else if (sbc_name_eq(sbt_text,"OCT$")) kind=12;
    else if (sbc_name_eq(sbt_text,"BIN$")) kind=13;
    else if (sbc_name_eq(sbt_text,"MKI$")||sbc_name_eq(sbt_text,"MKL$")||sbc_name_eq(sbt_text,"MKS$")||sbc_name_eq(sbt_text,"MKD$")) kind=14;
    def=sbc_find_def(sbt_text);
    if(def>=0){
        sbc_lex_next();if(sbt_kind==SBT_LP)sbc_lex_next();v=sbc_expr_or();resume=sbc_parse_pos;oldend=sbc_stmt_end;
        sym=sbc_find_symbol(sbc_deffns[def].param,1);c=sbc_symbols[sym].value;ulo=sbc_symbols[sym].numlo;uhi=sbc_symbols[sym].numhi;uk=sbc_symbols[sym].numkind;sbc_num_long(v);sbc_num_save_symbol(sym);
        ovl_sbv_eval_string(sbc_deffns[def].begin,sbc_deffns[def].end,out);sbc_symbols[sym].value=c;
        sbc_symbols[sym].numlo=ulo;sbc_symbols[sym].numhi=uhi;sbc_symbols[sym].numkind=uk;sbc_stmt_end=oldend;sbc_parse_pos=resume;sbc_lex_next();return 1;
    }
    if (!kind) {
        sym = sbc_find_symbol(sbt_text, 0);
        if(sym>=0){slot=sbc_symbols[sym].strslot;if(slot>=0){sbc_string_copy(slot,sbv_strc);sbv_strcat(out,0,sbv_strc);}}
        sbc_lex_next(); return 1;
    }
    sbc_lex_next();
    if (kind == 1) {
        if (sbc_key_count) { c=sbc_key_ascii[sbc_key_head]; if(!c)c=sbc_key_scan[sbc_key_head]; sbc_key_head=(sbc_key_head+1)&(SBC_KEYS-1); --sbc_key_count; out[0]=(char)c; out[1]=0; }
        return 1;
    }
    if (sbt_kind == SBT_LP) sbc_lex_next();
    if(kind>=10){
        v=sbc_expr_or();
        if(kind==10){if(sbt_kind==SBT_COMMA)sbc_lex_next();c=sbc_expr_or();n=0;while(n<v&&n<SBC_STRING_MAX-1)out[n++]=(char)c;out[n]=0;}
        else if(kind==14){out[0]=(char)(v&255);out[1]=(char)((unsigned)v>>8);out[2]=1;out[3]=0;}
        else {unsigned u;int base;int p;u=(unsigned)v;base=kind==11?16:(kind==12?8:10);p=SBC_STRING_MAX-1;out[p]=0;do{c=(int)(u%(unsigned)base);out[--p]=(char)(c<10?'0'+c:'A'+c-10);u/=(unsigned)base;}while(u&&p);n=0;while(out[p])out[n++]=out[p++];out[n]=0;}
    } else if (kind == 2 || kind == 3 || kind == 7) {
        v = sbc_expr_or();
        if (kind == 2) { out[0]=(char)v; out[1]=0; }
        else if (kind == 3) { sbv_numstr(v); sbv_strcat(out,0,sbv_numtext); }
        else { n=0; while(n<v && n<SBC_STRING_MAX-1) out[n++]=' '; out[n]=0; }
    } else {
        sbv_string_term(sbv_strc);
        if (kind == 8 || kind == 9) {
            n=0; while(sbv_strc[n]) { c=sbv_strc[n]; if(kind==8&&c>='a'&&c<='z')c-=32; if(kind==9&&c>='A'&&c<='Z')c+=32; out[n++]=(char)c; } out[n]=0;
        } else {
            if (sbt_kind == SBT_COMMA) sbc_lex_next();
            v=sbc_expr_or(); n=0;
            if (kind==4) { while(n<v&&sbv_strc[n]&&n<SBC_STRING_MAX-1){out[n]=sbv_strc[n];++n;} }
            else if (kind==5) { c=0;while(sbv_strc[c])++c;start=c-v;if(start<0)start=0;while(sbv_strc[start]&&n<SBC_STRING_MAX-1)out[n++]=sbv_strc[start++]; }
            else { start=v-1;if(start<0)start=0;v=SBC_STRING_MAX;if(sbt_kind==SBT_COMMA){sbc_lex_next();v=sbc_expr_or();}while(sbv_strc[start]&&n<v&&n<SBC_STRING_MAX-1)out[n++]=sbv_strc[start++]; }
            out[n]=0;
        }
    }
    if (sbt_kind == SBT_RP) sbc_lex_next();
    return 1;
}

static void ovl_sbv_eval_string(unsigned begin, unsigned end, char *out)
{
    int used;
    unsigned oldend;
    oldend=sbc_stmt_end;used=0; out[0]=0; sbc_parse_pos=begin; sbc_stmt_end=end; sbc_lex_next();
    for (;;) {
        if (!sbv_string_term(sbv_strb)) break;
        used=sbv_strcat(out,used,sbv_strb);
        if (sbt_kind==SBT_OP && sbt_op=='+') sbc_lex_next(); else break;
    }
    sbc_stmt_end=oldend;
}

static int ovl_sbv_string_numeric(int fn)
{
    int i;
    int j;
    int r;
    sbv_string_term(sbv_stra);
    if (fn==9) { i=0;while(sbv_stra[i])++i;r=i; }
    else if(fn==10) r=(unsigned char)sbv_stra[0];
    else if(fn==11) r=sbv_atoi(sbv_stra);
    else {
        if(sbt_kind==SBT_COMMA)sbc_lex_next();
        sbv_string_term(sbv_strb);r=0;
        for(i=0;sbv_stra[i]&&!r;++i){j=0;while(sbv_strb[j]&&sbv_stra[i+j]==sbv_strb[j])++j;if(!sbv_strb[j])r=i+1;}
    }
    if(sbt_kind==SBT_RP)sbc_lex_next();
    return r;
}

static int sbv_streq(const char *a, const char *b)
{
    int i;
    i = 0;
    while (a[i] && b[i] && a[i] == b[i]) ++i;
    return a[i] == b[i];
}

static int sbv_condition(unsigned begin, unsigned end)
{
    unsigned p;
    int quote;
    int op;
    int has_string;
    p = begin; quote = 0; op = 0; has_string = 0;
    while (p < end) {
        if (sbc_ch(p) == '"') quote = !quote;
        if (sbc_ch(p) == '$' || sbc_ch(p) == '"') has_string = 1;
        if (!quote && (sbc_ch(p) == '=' || (sbc_ch(p) == '<' && sbc_ch(p+1) == '>'))) {
            op = sbc_ch(p) == '=' ? 1 : 2; break;
        }
        ++p;
    }
    if (p < end && has_string) {
        ovl_sbv_eval_string(begin, p, sbv_stra);
        ovl_sbv_eval_string(p + (op == 2 ? 2 : 1), end, sbv_strb);
        return op == 2 ? !sbv_streq(sbv_stra, sbv_strb) : sbv_streq(sbv_stra, sbv_strb);
    }
    return sbc_eval_at(begin, end) != 0;
}

static void sbv_statement_bounds(void)
{
    unsigned p;
    int quote;
    int c;
    p = sbc_pc;
    for (;;) {
        while (p < sbc_source_len && (sbc_ch(p) == '\r' || sbc_ch(p) == '\n')) {
            if (sbc_ch(p) == '\n') ++sbc_lineno;
            ++p;
        }
        p = sbv_skip_space(p, sbc_source_len);
        if (sbc_isdigit(sbc_ch(p))) {
            while (sbc_isdigit(sbc_ch(p))) ++p;
            p = sbv_skip_space(p, sbc_source_len);
        }
        if (sbc_ch(p) == '\'') {
            while (p < sbc_source_len && sbc_ch(p) != '\n') ++p;
            continue;
        }
        break;
    }
    sbc_stmt_start = p;
    quote = 0;
    while (p < sbc_source_len) {
        c = sbc_ch(p);
        if (c == '"') quote = !quote;
        if (!quote && (c == ':' || c == '\r' || c == '\n' || c == '\'')) break;
        ++p;
    }
    sbc_stmt_end = p;
    if (p < sbc_source_len && sbc_ch(p) == ':') ++p;
    else {
        while (p < sbc_source_len && sbc_ch(p) != '\n') ++p;
        if (p < sbc_source_len) { ++p; ++sbc_lineno; }
    }
    sbc_pc = p;
}

static void sbv_jump_token(void)
{
    unsigned p;
    unsigned n;
    unsigned div;
    int i;
    if (sbt_kind == SBT_NUM) {
        sbv_name[0] = '#';
        i = 1;
        n = (unsigned)sbt_num;
        div = 10000u;
        while (div > n && div > 1u) div /= 10u;
        do {
            sbv_name[i++] = (char)('0' + (n / div) % 10u);
            div /= 10u;
        } while (div && i < SBC_NAME_MAX - 1);
        sbv_name[i] = 0;
    } else if (sbt_kind == SBT_ID) sbc_copy_name(sbv_name, sbt_text);
    else { sbc_set_error("Expected line or label"); return; }
    p = sbc_find_label(sbv_name);
    if (p == (unsigned)0xffff) sbc_set_error("Undefined line or label");
    else if(sbc_jump_ifdepth>sbc_nifs)sbc_set_error("GOTO into IF block");
    else{sbc_nifs=sbc_jump_ifdepth;sbc_pc=p;}
}

static void sbv_print(void)
{
    unsigned p;
    unsigned q;
    int quote;
    int depth;
    int c;
    int newline;
    p = sbc_parse_pos;
    newline = 1;
    while (p < sbc_stmt_end) {
        p = sbv_skip_space(p, sbc_stmt_end);
        if (p >= sbc_stmt_end) break;
        q = p;
        quote = 0;
        depth = 0;
        while (q < sbc_stmt_end) {
            c = sbc_ch(q);
            if (c == '"') quote = !quote;
            if (!quote) {
                if (c == '(') ++depth;
                else if (c == ')') --depth;
                else if (depth == 0 && (c == ',' || c == ';')) break;
            }
            ++q;
        }
        if (sbc_ch(p) == '"') {
            ++p;
            while (p < q && sbc_ch(p) != '"') sbv_put_char(sbc_ch(p++));
        } else sbv_putnum(sbc_eval_at(p, q));
        if (q < sbc_stmt_end) {
            newline = 0;
            if (sbc_ch(q) == ',') {
                do { sbv_put_char(' '); } while (((sbc_cursor_col - 1) % 14) != 0);
            }
            p = q + 1;
        } else p = q;
    }
    if (newline || sbc_stmt_end == sbc_parse_pos) sbv_put_char('\n');
}

static void sbv_assignment(int had_let)
{
    int sym;
    int isstr;
    int slot;
    int i;
    int ai;
    int aj;
    int declkind;
    int cell;
    unsigned value_begin;
    (void)had_let;
    if (sbt_kind != SBT_ID) { sbc_set_error("Expected variable"); return; }
    isstr = 0;
    i = 0;
    while (sbt_text[i]) { if (sbt_text[i] == '$') isstr = 1; ++i; }
    sym = sbc_find_symbol(sbt_text, 1);
    sbc_lex_next();
    ai = -1;
    aj = 0;
    if (sbt_kind == SBT_LP) {
        sbc_lex_next();
        ai = sbc_expr_or();
        if (sbt_kind == SBT_COMMA) { sbc_lex_next(); aj = sbc_expr_or(); }
        if (sbt_kind == SBT_RP) sbc_lex_next();
    }
    if (sbt_kind != SBT_OP || sbt_op != '=') { sbc_set_error("Expected ="); return; }
    value_begin = sbc_parse_pos;
    sbc_lex_next();
    if (isstr) {
        slot = sbc_alloc_string(sym);
        if (slot < 0) { sbc_set_error("String space full"); return; }
        ovl_sbv_eval_string(value_begin, sbc_stmt_end, sbv_stra);
        sbc_string_set(slot,sbv_stra);
    } else {
        i = sbc_expr_or();
        declkind=sbc_symbol_decl_kind(sym);if(declkind>=0)sbc_num_force(declkind);sbc_num_check();i=sbc_num_int();
        if (ai >= 0) {
            cell = sbc_array_cell(sym, ai, aj);
            if (cell < 0) sbc_set_error("Subscript out of range");
            else if(sbm_set_pair((unsigned)cell,sbn_alo,sbn_ahi)<0)sbc_set_error("Array space full");
            else sbm_kind_set((unsigned)cell,sbn_akind);
        } else sbc_num_save_symbol(sym);
    }
}

static void sbv_dim(void)
{
    int sym;
    int d1;
    int d2;
    for (;;) {
        if (sbt_kind != SBT_ID) { sbc_set_error("Bad DIM"); return; }
        sym = sbc_find_symbol(sbt_text, 1);
        sbc_lex_next();
        if (sbt_kind != SBT_LP) {
            /* DIM also declares scalars, and demos commonly append a scalar
             * string after one or more array declarators. */
            if (sbt_kind != SBT_COMMA) break;
            sbc_lex_next(); continue;
        }
        sbc_lex_next(); d1 = sbc_expr_or(); d2 = -1;
        if (sbt_kind == SBT_COMMA) { sbc_lex_next(); d2 = sbc_expr_or(); }
        if (sbt_kind == SBT_RP) sbc_lex_next();
        if (sbc_make_array(sym, d1, d2) < 0) { sbc_set_error("Array space full"); return; }
        if (sbt_kind != SBT_COMMA) break;
        sbc_lex_next();
    }
}

static void sbv_read(void)
{
    int sym;
    int ai;
    int aj;
    int cell;
    for (;;) {
        if (sbt_kind != SBT_ID) { sbc_set_error("Bad READ"); return; }
        sym = sbc_find_symbol(sbt_text, 1);
        sbc_lex_next(); ai = -1; aj = 0;
        if (sbt_kind == SBT_LP) {
            sbc_lex_next(); ai = sbc_expr_or();
            if (sbt_kind == SBT_COMMA) { sbc_lex_next(); aj = sbc_expr_or(); }
            if (sbt_kind == SBT_RP) sbc_lex_next();
        }
        if (sbc_data_pos >= sbc_ndata) { sbc_set_error("Out of DATA"); return; }
        if (ai >= 0) {
            cell = sbc_array_cell(sym, ai, aj);
            if (cell < 0) { sbc_set_error("Subscript out of range"); return; }
            sbn_alo=sbm_get_lo(sbc_data_base+sbc_data_pos*2);sbn_ahi=sbm_get_hi(sbc_data_base+sbc_data_pos*2);sbn_akind=sbm_get(sbc_data_base+sbc_data_pos*2+1);aj=sbc_symbol_decl_kind(sym);if(aj>=0)sbc_num_force(aj);if(sbm_set_pair((unsigned)cell,sbn_alo,sbn_ahi)<0){sbc_set_error("Array space full");return;}sbm_kind_set((unsigned)cell,sbn_akind);++sbc_data_pos;
        } else {sbn_alo=sbm_get_lo(sbc_data_base+sbc_data_pos*2);sbn_ahi=sbm_get_hi(sbc_data_base+sbc_data_pos*2);sbn_akind=sbm_get(sbc_data_base+sbc_data_pos*2+1);++sbc_data_pos;ai=sbc_symbol_decl_kind(sym);if(ai>=0)sbc_num_force(ai);sbc_num_save_symbol(sym);}
        if (sbt_kind != SBT_COMMA) break;
        sbc_lex_next();
    }
}

static void sbv_screen(void)
{
    int mode;
    int w;
    int h;
    mode = sbc_expr_or();
    w = 80; h = 25;
    if (mode == 1 || mode == 7) { w = 320; h = 200; }
    else if (mode == 2 || mode == 8) { w = 640; h = 200; }
    else if (mode == 9 || mode == 10) { w = 640; h = 350; }
    else if (mode == 12) { w = 640; h = 480; }
    else if (mode == 13) { w = 320; h = 200; }
    sbc_gfx_mode = mode;
    sbc_ink = mode == 1 ? 3 : (mode == 2 ? 1 : 15);
    if (sbc_host && sbc_host->mode)
        sbc_host->mode(mode, w, h);
}

/* Advance through parsed statements until a control delimiter at the current
 * nesting depth.  WANT: 1 WEND, 2 LOOP, 3 CASE/END SELECT, 4 END SELECT. */
static int sbv_skip_control(int want)
{
    int depth;
    int hit;
    depth = 0;
    while (sbc_pc < sbc_source_len) {
        sbv_statement_bounds();
        sbc_parse_pos = sbc_stmt_start;
        sbc_lex_next();
        if (sbt_kind != SBT_ID) continue;
        hit = 0;
        if (want == 1) {
            if (sbc_name_eq(sbt_text, "WHILE")) ++depth;
            else if (sbc_name_eq(sbt_text, "WEND")) { if (!depth) hit = 1; else --depth; }
        } else if (want == 2) {
            if (sbc_name_eq(sbt_text, "DO")) ++depth;
            else if (sbc_name_eq(sbt_text, "LOOP")) { if (!depth) hit = 1; else --depth; }
        } else {
            if (sbc_name_eq(sbt_text, "SELECT")) ++depth;
            else if (sbc_name_eq(sbt_text, "END")) {
                sbc_lex_next();
                if (sbt_kind == SBT_ID && sbc_name_eq(sbt_text, "SELECT")) {
                    if (!depth) hit = 2; else --depth;
                }
            } else if (want == 3 && !depth && sbc_name_eq(sbt_text, "CASE")) hit = 1;
        }
        if (hit) {
            if (want == 3 && hit == 1) sbc_pc = sbc_stmt_start;
            return hit;
        }
    }
    sbc_set_error("Missing control terminator");
    return 0;
}

static void sbv_skip_for(void)
{
    unsigned p;
    int depth;
    int prev;
    p=sbc_pc;depth=0;
    while(p<sbc_source_len){prev=p?sbc_ch(p-1):' ';if(!sbc_isalpha(prev)&&!sbc_isdigit(prev)){if(sbv_word_at(p,"FOR"))++depth;else if(sbv_word_at(p,"NEXT")){if(!depth){sbc_pc=p;sbv_statement_bounds();return;}--depth;}}++p;}
    sbc_set_error("Missing NEXT");
}

static void sbv_skip_sub(void)
{
    while (sbc_pc < sbc_source_len) {
        sbv_statement_bounds();
        sbc_parse_pos = sbc_stmt_start;
        sbc_lex_next();
        if (sbt_kind == SBT_ID && sbc_name_eq(sbt_text, "END")) {
            sbc_lex_next();
            if (sbt_kind == SBT_ID && (sbc_name_eq(sbt_text, "SUB") ||
                sbc_name_eq(sbt_text, "DEF"))) return;
        }
    }
    sbc_set_error("Missing END SUB");
}

static void sbv_gosub_push(void)
{
    unsigned s;
    s=(unsigned)sbc_nfor;
    s|=(unsigned)sbc_nloops<<4;
    s|=(unsigned)sbc_nifs<<8;
    s|=(unsigned)sbc_nselects<<12;
    sbc_gosub[sbc_ngosub]=sbc_pc;
    sbc_gosub_state[sbc_ngosub++]=s;
}

static void sbv_gosub_return(void)
{
    unsigned s;
    --sbc_ngosub;
    s=sbc_gosub_state[sbc_ngosub];
    sbc_nfor=s&15;
    sbc_nloops=(s>>4)&15;
    sbc_nifs=(s>>8)&15;
    sbc_nselects=(s>>12)&15;
    sbc_pc=sbc_gosub[sbc_ngosub];
}

static int sbv_save_local(int sym)
{
    int i;
    int base;
    if (!sbc_ncalls) return 0;
    base=sbc_calls[sbc_ncalls-1].savebase;
    for(i=base;i<sbc_nsaves;++i) if(sbc_saves[i].sym==sym)return 0;
    if(sbc_nsaves>=SBC_SAVES){sbc_set_error("Procedure locals full");return -1;}
    sbc_saves[sbc_nsaves].sym=sym;
    sbc_saves[sbc_nsaves].value=sbc_symbols[sym].value;
    sbc_saves[sbc_nsaves].strslot=sbc_symbols[sym].strslot;
    sbc_saves[sbc_nsaves].lo=sbc_symbols[sym].numlo;sbc_saves[sbc_nsaves].hi=sbc_symbols[sym].numhi;sbc_saves[sbc_nsaves].kind=sbc_symbols[sym].numkind;
    ++sbc_nsaves;
    return 0;
}

static void sbv_end_call(void)
{
    int base;
    int s;
    if(!sbc_ncalls)return;
    base=sbc_calls[sbc_ncalls-1].savebase;
    while(sbc_nsaves>base){--sbc_nsaves;s=sbc_saves[sbc_nsaves].sym;sbc_symbols[s].value=sbc_saves[sbc_nsaves].value;sbc_symbols[s].strslot=sbc_saves[sbc_nsaves].strslot;sbc_symbols[s].numlo=sbc_saves[sbc_nsaves].lo;sbc_symbols[s].numhi=sbc_saves[sbc_nsaves].hi;sbc_symbols[s].numkind=sbc_saves[sbc_nsaves].kind;}
    sbc_nfor=sbc_calls[sbc_ncalls-1].forbase;sbc_nloops=sbc_calls[sbc_ncalls-1].loopbase;sbc_nifs=sbc_calls[sbc_ncalls-1].ifbase;sbc_nselects=sbc_calls[sbc_ncalls-1].selectbase;sbc_pc=sbc_calls[--sbc_ncalls].ret;
}

static void sbv_call_sub(void)
{
    int li;
    int narg;
    int sym;
    unsigned decl;
    unsigned target;
    unsigned oldend;
    if(sbt_kind!=SBT_ID){sbc_set_error("Expected SUB name");return;}
    li=-1;for(sym=0;sym<sbc_nlabels;++sym)if(sbc_name_eq(sbc_labels[sym].name,sbt_text)){li=sym;break;}
    if(li<0||sbc_proc_decl[li]==(unsigned)0xffff){sbc_set_error("Undefined SUB");return;}
    sbc_lex_next();narg=0;
    if(sbt_kind==SBT_LP){sbc_lex_next();while(sbt_kind!=SBT_RP&&sbt_kind!=SBT_EOF){if(narg<8){sbv_call_args[narg]=sbc_expr_or();sbv_call_lo[narg]=sbn_alo;sbv_call_hi[narg]=sbn_ahi;sbv_call_kind[narg]=sbn_akind;++narg;}else (void)sbc_expr_or();if(sbt_kind==SBT_COMMA)sbc_lex_next();else break;}if(sbt_kind==SBT_RP)sbc_lex_next();}
    if(sbc_ncalls>=SBC_CALLS){sbc_set_error("CALL stack full");return;}
    sbc_calls[sbc_ncalls].ret=sbc_pc;sbc_calls[sbc_ncalls].savebase=sbc_nsaves;sbc_calls[sbc_ncalls].forbase=sbc_nfor;sbc_calls[sbc_ncalls].loopbase=sbc_nloops;sbc_calls[sbc_ncalls].ifbase=sbc_nifs;sbc_calls[sbc_ncalls].selectbase=sbc_nselects;++sbc_ncalls;
    decl=sbc_proc_decl[li];target=sbc_labels[li].pos;oldend=sbc_stmt_end;sbc_parse_pos=decl;sbc_stmt_end=target;sbc_lex_next();
    if(sbt_kind==SBT_LP)sbc_lex_next();sym=0;
    while(sbt_kind==SBT_ID){li=sbc_find_symbol(sbt_text,1);if(sbv_save_local(li)<0)break;if(sym<narg){sbc_symbols[li].value=sbv_call_args[sym];sbc_symbols[li].numlo=sbv_call_lo[sym];sbc_symbols[li].numhi=sbv_call_hi[sym];sbc_symbols[li].numkind=sbv_call_kind[sym];}else{sbc_symbols[li].value=0;sbc_symbols[li].numlo=0;sbc_symbols[li].numhi=0;sbc_symbols[li].numkind=SBN_LONG;}++sym;sbc_lex_next();if(sbt_kind==SBT_COMMA)sbc_lex_next();else break;}
    sbc_stmt_end=oldend;sbc_pc=target;
}

/* Stop before ELSEIF/ELSE so normal dispatch decides the branch.  END IF is
 * consumed because there is no branch left to enter. */
static int sbv_skip_if_clause(void)
{
    int depth;
    unsigned thenp;
    depth = 0;
    while (sbc_pc < sbc_source_len) {
        sbv_statement_bounds();
        sbc_parse_pos = sbc_stmt_start;
        sbc_lex_next();
        if (sbt_kind != SBT_ID) continue;
        if (sbc_name_eq(sbt_text, "IF")) {
            thenp = sbv_find_word(sbc_parse_pos, sbc_stmt_end, "THEN");
            if (thenp < sbc_stmt_end && sbv_skip_space(thenp + 4, sbc_stmt_end) >= sbc_stmt_end)
                ++depth;
        } else if (sbc_name_eq(sbt_text, "END")) {
            sbc_lex_next();
            if (sbt_kind == SBT_ID && sbc_name_eq(sbt_text, "IF")) {
                if (!depth) { if (sbc_nifs) --sbc_nifs; return 0; }
                --depth;
            }
        } else if (!depth && (sbc_name_eq(sbt_text, "ELSE") ||
                   sbc_name_eq(sbt_text, "ELSEIF"))) {
            sbc_pc = sbc_stmt_start;
            return 1;
        }
    }
    sbc_set_error("Missing END IF");
    return 0;
}

static int sbc_exec_one(void)
{
    unsigned a;
    unsigned b;
    unsigned cpos;
    int a1;
    int a2;
    int a3;
    int a4;
    int sym;
    int i;
    sbv_statement_bounds();
    if (sbc_stmt_start >= sbc_source_len) { sbc_status = SB_STATE_DONE; return 0; }
    if (sbc_stmt_start >= sbc_stmt_end) return 1;
    if (sbc_ch(sbc_stmt_start) == '$') return 1;
    sbc_parse_pos = sbc_stmt_start;
    sbc_lex_next();
    if (sbt_kind == SBT_EOF) return 1;
    if (sbt_kind != SBT_ID) { sbc_set_error("Statement expected"); return 0; }

    if (sbc_name_eq(sbt_text, "REM")) return 1;
    if (sbc_name_eq(sbt_text, "END")) {
        sbc_lex_next();
        if (sbt_kind == SBT_ID && (sbc_name_eq(sbt_text, "SUB") ||
            sbc_name_eq(sbt_text, "DEF"))) {
            if (sbc_ncalls) sbv_end_call();
            else if (sbc_ngosub) sbv_gosub_return();
            return 1;
        }
        if (sbt_kind == SBT_ID && sbc_name_eq(sbt_text, "IF")) {
            if (sbc_nifs) --sbc_nifs;
            return 1;
        }
        if (sbt_kind == SBT_ID && sbc_name_eq(sbt_text, "SELECT")) {
            if (sbc_nselects) --sbc_nselects;
            return 1;
        }
        sbc_status = SB_STATE_DONE; return 0;
    }
    if (sbc_name_eq(sbt_text, "SYSTEM") || sbc_name_eq(sbt_text, "STOP"))
        { sbc_status = SB_STATE_DONE; return 0; }
    if (sbc_name_eq(sbt_text, "LET")) { sbc_lex_next(); sbv_assignment(1); return 1; }
    if (sbc_name_eq(sbt_text, "PRINT") || sbc_name_eq(sbt_text, "LPRINT") ||
        sbc_name_eq(sbt_text, "WRITE")) { sbv_print(); return 1; }
    if (sbc_name_eq(sbt_text, "CLS")) {
        sbc_lex_next();
        a1 = sbt_kind == SBT_EOF ? 0 : sbc_expr_or();
        if (sbc_host && sbc_host->clear) sbc_host->clear(a1);
        sbc_cursor_row = sbc_cursor_col = 1;
        return 1;
    }
    if (sbc_name_eq(sbt_text, "COLOR")) {
        sbc_lex_next(); sbc_fg = sbc_expr_or();
        if (sbt_kind == SBT_COMMA) { sbc_lex_next(); sbc_bg = sbc_expr_or(); }
        return 1;
    }
    if (sbc_name_eq(sbt_text, "LOCATE")) {
        sbc_lex_next(); sbc_cursor_row = sbc_expr_or();
        if (sbt_kind == SBT_COMMA) { sbc_lex_next(); sbc_cursor_col = sbc_expr_or(); }
        if (sbc_host && sbc_host->text_cursor)
            sbc_host->text_cursor(sbc_cursor_row, sbc_cursor_col);
        return 1;
    }
    if (sbc_name_eq(sbt_text, "SCREEN")) { sbc_lex_next(); sbv_screen(); return 1; }
    if (sbc_name_eq(sbt_text, "DIM") || sbc_name_eq(sbt_text, "REDIM"))
        { sbc_lex_next(); sbv_dim(); return 1; }
    if (sbc_name_eq(sbt_text, "READ")) { sbc_lex_next(); sbv_read(); return 1; }
    if (sbc_name_eq(sbt_text, "RESTORE")) { sbc_data_pos = 0; return 1; }
    if (sbc_name_eq(sbt_text, "OPTION")) {
        sbc_lex_next();
        if (sbt_kind == SBT_ID && sbc_name_eq(sbt_text, "BASE")) {
            sbc_lex_next(); sbc_option_base = sbc_expr_or() ? 1 : 0;
            return 1;
        }
        sbc_set_error("Unsupported OPTION"); return 0;
    }
    if (sbc_name_eq(sbt_text, "PSET") || sbc_name_eq(sbt_text, "PRESET")) {
        i = sbc_name_eq(sbt_text, "PRESET") ? 0 : sbc_ink;
        sbc_lex_next(); if (sbt_kind == SBT_LP) sbc_lex_next();
        a1 = sbc_expr_or(); if (sbt_kind == SBT_COMMA) sbc_lex_next(); a2 = sbc_expr_or();
        if (sbt_kind == SBT_RP) sbc_lex_next();
        if (sbt_kind == SBT_COMMA) { sbc_lex_next(); i = sbc_expr_or(); }
        if (sbc_host && sbc_host->pixel) sbc_host->pixel(a1, a2, i);
        sbc_lpr_x = a1; sbc_lpr_y = a2;
        return 1;
    }
    if (sbc_name_eq(sbt_text, "CIRCLE")) {
        sbc_lex_next(); if (sbt_kind == SBT_LP) sbc_lex_next();
        a1 = sbc_expr_or(); if (sbt_kind == SBT_COMMA) sbc_lex_next(); a2 = sbc_expr_or();
        if (sbt_kind == SBT_RP) sbc_lex_next();
        if (sbt_kind == SBT_COMMA) sbc_lex_next(); a3 = sbc_expr_or(); a4 = sbc_ink;
        if (sbt_kind == SBT_COMMA) { sbc_lex_next(); a4 = sbc_expr_or(); }
        if (sbc_host && sbc_host->circle) sbc_host->circle(a1, a2, a3, a4);
        return 1;
    }
    if (sbc_name_eq(sbt_text, "PAINT")) {
        sbc_lex_next(); if (sbt_kind == SBT_LP) sbc_lex_next();
        a1 = sbc_expr_or(); if (sbt_kind == SBT_COMMA) sbc_lex_next(); a2 = sbc_expr_or();
        if (sbt_kind == SBT_RP) sbc_lex_next(); a3 = sbc_ink; a4 = a3;
        if (sbt_kind == SBT_COMMA) { sbc_lex_next(); a3 = sbc_expr_or(); }
        if (sbt_kind == SBT_COMMA) { sbc_lex_next(); a4 = sbc_expr_or(); }
        if (sbc_host && sbc_host->paint) sbc_host->paint(a1, a2, a3, a4);
        return 1;
    }
    if (sbc_name_eq(sbt_text, "LINE")) {
        sbc_lex_next();
        if (sbt_kind == SBT_ID && sbc_name_eq(sbt_text, "INPUT")) {
            sbc_lex_next();if(sbt_kind==SBT_OP&&sbt_op=='#')return 1;sbv_begin_input(); return 1;
        }
        if (sbt_kind == SBT_LP) sbc_lex_next();
        a1 = sbc_expr_or(); if (sbt_kind == SBT_COMMA) sbc_lex_next(); a2 = sbc_expr_or();
        if (sbt_kind == SBT_RP) sbc_lex_next();
        if (sbt_kind == SBT_OP && sbt_op == '-') sbc_lex_next();
        if (sbt_kind == SBT_LP) sbc_lex_next();
        a3 = sbc_expr_or(); if (sbt_kind == SBT_COMMA) sbc_lex_next(); a4 = sbc_expr_or();
        if (sbt_kind == SBT_RP) sbc_lex_next();
        i = sbc_ink; sym = 0;
        if (sbt_kind == SBT_COMMA) {
            sbc_lex_next();
            if (sbt_kind != SBT_COMMA && sbt_kind != SBT_EOF) i = sbc_expr_or();
        }
        if (sbt_kind == SBT_COMMA) {
            sbc_lex_next();
            if (sbt_kind == SBT_ID && sbc_name_eq(sbt_text,"B")) sym = 1;
            else if (sbt_kind == SBT_ID && sbc_name_eq(sbt_text,"BF")) sym = 2;
            if (sbt_kind != SBT_EOF) sbc_lex_next();
            /* Turbo BASIC permits a final 16-bit line-style mask.  The host
             * has no patterned-line primitive, but consume it so following
             * syntax is parsed correctly. */
            if (sbt_kind == SBT_COMMA) { sbc_lex_next(); (void)sbc_expr_or(); }
        }
        if (sbc_host && sbc_host->line) {
            if (!sym) sbc_host->line(a1, a2, a3, a4, i);
            else if (sym == 1) {
                sbc_host->line(a1,a2,a3,a2,i);sbc_host->line(a3,a2,a3,a4,i);
                sbc_host->line(a3,a4,a1,a4,i);sbc_host->line(a1,a4,a1,a2,i);
            } else {
                if(a2>a4){sym=a2;a2=a4;a4=sym;}
                while(a2<=a4){sbc_host->line(a1,a2,a3,a2,i);++a2;}
            }
        }
        sbc_lpr_x = a3; sbc_lpr_y = a4;
        return 1;
    }
    if (sbc_name_eq(sbt_text, "GOTO")) { sbc_lex_next(); sbv_jump_token(); return 1; }
    if (sbc_name_eq(sbt_text, "GOSUB")) {
        if (sbc_ngosub >= SBC_GOSUB) { sbc_set_error("GOSUB stack full"); return 0; }
        sbv_gosub_push();
        sbc_lex_next(); sbv_jump_token(); return 1;
    }
    if (sbc_name_eq(sbt_text, "RETURN")) {
        if (!sbc_ngosub) { sbc_set_error("RETURN without GOSUB"); return 0; }
        sbv_gosub_return(); return 1;
    }
    if (sbc_name_eq(sbt_text,"ON")) {
        sbc_lex_next();
        if(sbt_kind==SBT_ID&&sbc_name_eq(sbt_text,"ERROR")){sbc_lex_next();if(sbt_kind==SBT_ID&&sbc_name_eq(sbt_text,"GOTO"))sbc_lex_next();if(sbt_kind==SBT_NUM||sbt_kind==SBT_ID){if(sbt_kind==SBT_NUM){sbv_name[0]='#';sbv_numstr(sbt_num);i=0;while(sbv_numtext[i]==' ')++i;a=1;while(sbv_numtext[i]&&a<SBC_NAME_MAX-1)sbv_name[a++]=sbv_numtext[i++];sbv_name[a]=0;}else sbc_copy_name(sbv_name,sbt_text);sbc_error_target=sbc_find_label(sbv_name);}return 1;}
        if(sbt_kind==SBT_ID&&sbc_name_eq(sbt_text,"KEY")) return 1;
        a1=sbc_expr_or();if(sbt_kind!=SBT_ID){sbc_set_error("Bad ON");return 0;}
        a2=sbc_name_eq(sbt_text,"GOSUB");if(!a2&&!sbc_name_eq(sbt_text,"GOTO")){sbc_set_error("Bad ON");return 0;}sbc_lex_next();
        i=1;while(sbt_kind!=SBT_EOF){if(i==a1){if(a2){if(sbc_ngosub>=SBC_GOSUB){sbc_set_error("GOSUB stack full");return 0;}sbv_gosub_push();}sbv_jump_token();return 1;}while(sbt_kind!=SBT_COMMA&&sbt_kind!=SBT_EOF)sbc_lex_next();if(sbt_kind==SBT_COMMA)sbc_lex_next();++i;}return 1;
    }
    if (sbc_name_eq(sbt_text, "CALL")) {
        sbc_lex_next();
        if (sbt_kind == SBT_ID && sbc_name_eq(sbt_text, "INTERRUPT")) {
            sbc_lex_next();
            if (sbt_kind != SBT_EOF) (void)sbc_expr_or();
            if ((sbc_regs[1] & 255) == 0x13) {
                sbc_parse_pos = sbc_stmt_end;
                sbc_gfx_mode = 13;
                if (sbc_host && sbc_host->mode) sbc_host->mode(13, 320, 200);
            } else if ((sbc_regs[1] & 255) == 3) {
                sbc_gfx_mode = 0;
                if (sbc_host && sbc_host->mode) sbc_host->mode(0, 80, 25);
            }
            return 1;
        }
        if (sbt_kind == SBT_ID && sbc_name_eq(sbt_text, "ABSOLUTE")) return 1;
        sbv_call_sub(); return 1;
    }
    if (sbc_name_eq(sbt_text, "IF")) {
        a = sbc_parse_pos;
        b = sbv_find_word(a, sbc_stmt_end, "THEN");
        if (b == sbc_stmt_end) { sbc_set_error("IF without THEN"); return 0; }
        a1 = sbv_condition(a, b);
        cpos = sbv_skip_space(b + 4, sbc_stmt_end);
        if (cpos >= sbc_stmt_end) {
            if (sbc_nifs >= SBC_IFS) { sbc_set_error("IF stack full"); return 0; }
            sbc_if_taken[sbc_nifs++] = a1 != 0;
            if (!a1) sbv_skip_if_clause();
        } else if (a1) sbc_pc = b + 4;
        else {
            cpos = sbv_find_word(b + 4, sbc_stmt_end, "ELSE");
            if (cpos < sbc_stmt_end) sbc_pc = cpos + 4;
            else if (sbc_pc && sbc_ch(sbc_pc - 1) == ':') {
                /* Bounds already leave pc on the following line for a normal
                 * single-line IF.  Only a colon leaves another statement on
                 * this physical line that the false arm must discard. */
                while(sbc_pc<sbc_source_len&&sbc_ch(sbc_pc)!='\n'&&sbc_ch(sbc_pc)!='\r')++sbc_pc;
            }
        }
        return 1;
    }
    if (sbc_name_eq(sbt_text, "ELSEIF")) {
        if (!sbc_nifs) { sbc_set_error("ELSEIF without IF"); return 0; }
        i = sbc_nifs - 1;
        if (sbc_if_taken[i]) { sbv_skip_if_clause(); return 1; }
        a = sbc_parse_pos;
        b = sbv_find_word(a, sbc_stmt_end, "THEN");
        if (b == sbc_stmt_end) { sbc_set_error("ELSEIF without THEN"); return 0; }
        a1 = sbv_condition(a, b);
        if (a1) sbc_if_taken[i] = 1; else sbv_skip_if_clause();
        return 1;
    }
    if (sbc_name_eq(sbt_text, "ELSE")) {
        if (!sbc_nifs) { sbc_set_error("ELSE without IF"); return 0; }
        i = sbc_nifs - 1;
        if (sbc_if_taken[i]) sbv_skip_if_clause(); else sbc_if_taken[i] = 1;
        return 1;
    }
    if (sbc_name_eq(sbt_text, "FOR")) {
        sbc_lex_next();
        if (sbt_kind != SBT_ID || sbc_nfor >= SBC_FOR) { sbc_set_error("Bad FOR"); return 0; }
        sym = sbc_find_symbol(sbt_text, 1); sbc_lex_next();
        if (sbt_kind != SBT_OP || sbt_op != '=') { sbc_set_error("Bad FOR"); return 0; }
        a = sbc_parse_pos;
        b = sbv_find_word(a, sbc_stmt_end, "TO");
        cpos = sbv_find_word(b + 2, sbc_stmt_end, "STEP");
        sbc_for[sbc_nfor].sym = sym;
        sbc_symbols[sym].value = sbc_eval_at(a, b);sbc_num_save_symbol(sym);
        sbc_for[sbc_nfor].limit = sbc_eval_at(b + 2, cpos);
        sbc_for[sbc_nfor].step = cpos < sbc_stmt_end ? sbc_eval_at(cpos + 4, sbc_stmt_end) : 1;
        sbc_for[sbc_nfor].body = sbc_pc;
        if((sbc_for[sbc_nfor].step>=0&&sbc_symbols[sym].value>sbc_for[sbc_nfor].limit)||(sbc_for[sbc_nfor].step<0&&sbc_symbols[sym].value<sbc_for[sbc_nfor].limit)){sbv_skip_for();return 1;}
        ++sbc_nfor;
        return 1;
    }
    if (sbc_name_eq(sbt_text, "NEXT")) {
        if (!sbc_nfor) { sbc_set_error("NEXT without FOR"); return 0; }
        i = sbc_nfor - 1;
        sym = sbc_for[i].sym;
        sbc_symbols[sym].value += sbc_for[i].step;
        sbc_num_long(sbc_symbols[sym].value);sbc_num_save_symbol(sym);
        if ((sbc_for[i].step >= 0 && sbc_symbols[sym].value <= sbc_for[i].limit) ||
            (sbc_for[i].step < 0 && sbc_symbols[sym].value >= sbc_for[i].limit))
            sbc_pc = sbc_for[i].body;
        else --sbc_nfor;
        return 1;
    }
    if (sbc_name_eq(sbt_text, "INCR") || sbc_name_eq(sbt_text, "DECR")) {
        a1 = sbc_name_eq(sbt_text, "INCR") ? 1 : -1;
        sbc_lex_next();
        if (sbt_kind != SBT_ID) { sbc_set_error("Expected variable"); return 0; }
        sym = sbc_find_symbol(sbt_text, 1); sbc_lex_next();
        if (sbt_kind == SBT_COMMA) { sbc_lex_next(); a1 *= sbc_expr_or(); }
        sbc_symbols[sym].value += a1;sbc_num_long(sbc_symbols[sym].value);sbc_num_save_symbol(sym); return 1;
    }
    if (sbc_name_eq(sbt_text, "SWAP")) {
        sbc_lex_next();
        if (sbt_kind != SBT_ID) { sbc_set_error("Bad SWAP"); return 0; }
        a1 = sbc_find_symbol(sbt_text, 1); sbc_lex_next();
        if (sbt_kind == SBT_COMMA) sbc_lex_next();
        if (sbt_kind != SBT_ID) { sbc_set_error("Bad SWAP"); return 0; }
        a2 = sbc_find_symbol(sbt_text, 1);
        a3=sbc_symbols[a1].value;sbn_alo=sbc_symbols[a1].numlo;sbn_ahi=sbc_symbols[a1].numhi;sbn_akind=sbc_symbols[a1].numkind;
        sbc_symbols[a1].value=sbc_symbols[a2].value;sbc_symbols[a1].numlo=sbc_symbols[a2].numlo;sbc_symbols[a1].numhi=sbc_symbols[a2].numhi;sbc_symbols[a1].numkind=sbc_symbols[a2].numkind;
        sbc_symbols[a2].value=a3;sbc_symbols[a2].numlo=sbn_alo;sbc_symbols[a2].numhi=sbn_ahi;sbc_symbols[a2].numkind=sbn_akind;return 1;
    }
    if (sbc_name_eq(sbt_text, "SELECT")) {
        if (sbc_nselects >= SBC_SELECTS) { sbc_set_error("SELECT stack full"); return 0; }
        sbc_lex_next();
        if (sbt_kind == SBT_ID && sbc_name_eq(sbt_text, "CASE")) sbc_lex_next();
        sbc_selects[sbc_nselects].value = sbc_expr_or();
        sbc_selects[sbc_nselects].matched = 0;
        ++sbc_nselects;
        return 1;
    }
    if (sbc_name_eq(sbt_text, "CASE")) {
        if (!sbc_nselects) { sbc_set_error("CASE without SELECT"); return 0; }
        i = sbc_nselects - 1;
        if (sbc_selects[i].matched) {
            sbv_skip_control(4);
            --sbc_nselects;
            return 1;
        }
        sbc_lex_next();
        a1 = 0;
        if (sbt_kind == SBT_ID && sbc_name_eq(sbt_text, "ELSE")) a1 = 1;
        while (!a1 && sbt_kind != SBT_EOF) {
            a2 = sbc_expr_or();
            if (sbt_kind == SBT_ID && sbc_name_eq(sbt_text, "TO")) {
                sbc_lex_next(); a3 = sbc_expr_or();
                if (sbc_selects[i].value >= a2 && sbc_selects[i].value <= a3) a1 = 1;
            } else if (sbc_selects[i].value == a2) a1 = 1;
            if (sbt_kind == SBT_COMMA) sbc_lex_next(); else break;
        }
        if (a1) sbc_selects[i].matched = 1;
        else {
            a2 = sbv_skip_control(3);
            if (a2 == 2) --sbc_nselects;
        }
        return 1;
    }
    if (sbc_name_eq(sbt_text, "WHILE")) {
        a = sbc_parse_pos;
        a1 = sbc_eval_at(a, sbc_stmt_end);
        if (!a1) { sbv_skip_control(1); return 1; }
        if (sbc_nloops >= SBC_LOOPS) { sbc_set_error("Loop stack full"); return 0; }
        sbc_loops[sbc_nloops].kind = 1;
        sbc_loops[sbc_nloops].body = sbc_pc;
        sbc_loops[sbc_nloops].cond_begin = a;
        sbc_loops[sbc_nloops].cond_end = sbc_stmt_end;
        ++sbc_nloops;
        return 1;
    }
    if (sbc_name_eq(sbt_text, "WEND")) {
        if (!sbc_nloops || sbc_loops[sbc_nloops-1].kind != 1) {
            sbc_set_error("WEND without WHILE"); return 0;
        }
        i = sbc_nloops - 1;
        if (sbc_eval_at(sbc_loops[i].cond_begin, sbc_loops[i].cond_end))
            sbc_pc = sbc_loops[i].body;
        else --sbc_nloops;
        return 1;
    }
    if (sbc_name_eq(sbt_text, "DO")) {
        if (sbc_nloops >= SBC_LOOPS) { sbc_set_error("Loop stack full"); return 0; }
        sbc_loops[sbc_nloops].kind = 2;
        sbc_loops[sbc_nloops].body = sbc_pc;
        ++sbc_nloops;
        return 1;
    }
    if (sbc_name_eq(sbt_text, "LOOP")) {
        if (!sbc_nloops || sbc_loops[sbc_nloops-1].kind != 2) {
            sbc_set_error("LOOP without DO"); return 0;
        }
        i = sbc_nloops - 1;
        sbc_lex_next();
        a1 = -1;
        if (sbt_kind == SBT_ID && sbc_name_eq(sbt_text, "UNTIL")) {
            sbc_lex_next(); a1 = !sbc_expr_or();
        } else if (sbt_kind == SBT_ID && sbc_name_eq(sbt_text, "WHILE")) {
            sbc_lex_next(); a1 = sbc_expr_or();
        }
        if (a1) sbc_pc = sbc_loops[i].body; else --sbc_nloops;
        return 1;
    }
    if (sbc_name_eq(sbt_text,"EXIT")) {
        sbc_lex_next();
        if(sbt_kind==SBT_ID&&sbc_name_eq(sbt_text,"FOR")){if(sbc_nfor)--sbc_nfor;while(sbc_pc<sbc_source_len){sbv_statement_bounds();sbc_parse_pos=sbc_stmt_start;sbc_lex_next();if(sbt_kind==SBT_ID&&sbc_name_eq(sbt_text,"NEXT"))break;}return 1;}
        if(sbt_kind==SBT_ID&&sbc_name_eq(sbt_text,"DO")){if(sbc_nloops)--sbc_nloops;sbv_skip_control(2);return 1;}
        if(sbt_kind==SBT_ID&&(sbc_name_eq(sbt_text,"SUB")||sbc_name_eq(sbt_text,"FUNCTION"))){sbv_end_call();return 1;}
        sbc_status=SB_STATE_DONE;return 0;
    }
    if (sbc_name_eq(sbt_text, "DELAY") || sbc_name_eq(sbt_text, "SLEEP")) {
        sbc_lex_next(); a1 = sbc_expr_or();
        if (sbc_host && sbc_host->ticks) {
            sbv_wait_until = sbc_host->ticks() + (unsigned)(a1 < 1 ? 1 : a1);
            sbv_wait_time = 1; sbc_status = SB_STATE_WAITING;
        }
        return 1;
    }
    if (sbc_name_eq(sbt_text, "BEEP")) {
        if (sbc_host && sbc_host->sound) sbc_host->sound(800, 4); return 1;
    }
    if (sbc_name_eq(sbt_text, "SOUND")) {
        sbc_lex_next(); a1 = sbc_expr_or(); a2 = 1;
        if (sbt_kind == SBT_COMMA) { sbc_lex_next(); a2 = sbc_expr_or(); }
        if (sbc_host && sbc_host->sound) sbc_host->sound(a1, a2); return 1;
    }
    if (sbc_name_eq(sbt_text, "RANDOMIZE")) {
        sbc_lex_next();
        sbc_rand = sbt_kind == SBT_EOF ? (sbc_host && sbc_host->ticks ? sbc_host->ticks() : 1u) : (unsigned)sbc_expr_or();
        return 1;
    }
    if(sbc_name_eq(sbt_text,"ERROR")){sbc_error_resume=sbc_pc;if(sbc_error_target!=(unsigned)0xffff){sbc_pc=sbc_error_target;return 1;}sbc_set_error("BASIC ERROR");return 0;}
    if(sbc_name_eq(sbt_text,"RESUME")){if(sbc_error_resume!=(unsigned)0xffff){sbc_pc=sbc_error_resume;sbc_error_resume=(unsigned)0xffff;}return 1;}
    if (sbc_name_eq(sbt_text, "INPUT")) {
        sbc_lex_next();if(sbt_kind==SBT_OP&&sbt_op=='#')return 1;sbv_begin_input(); return 1;
    }
    if (sbc_name_eq(sbt_text, "DEF")) {
        sbc_lex_next();
        if (sbt_kind == SBT_ID && sbc_name_eq(sbt_text, "SEG")) {
            sbc_lex_next();
            if (sbt_kind == SBT_OP && sbt_op == '=') { sbc_lex_next(); sbc_defseg = (unsigned)sbc_expr_or(); }
            else sbc_defseg = 0x1000u;
            return 1;
        }
        return 1;
    }
    if (sbc_name_eq(sbt_text, "POKE")) {
        sbc_lex_next(); a1 = sbc_expr_or();
        if (sbt_kind == SBT_COMMA) sbc_lex_next(); a2 = sbc_expr_or();
        if (sbc_host && sbc_host->video_poke) sbc_host->video_poke(sbc_defseg, (unsigned)a1, a2);
        return 1;
    }
    if(sbc_name_eq(sbt_text,"WAIT")){sbc_lex_next();a1=sbc_expr_or();if(sbt_kind==SBT_COMMA)sbc_lex_next();a2=sbc_expr_or();(void)a1;(void)a2;return 1;}
    if(sbc_name_eq(sbt_text,"MEMSET")){sbc_lex_next();a1=sbc_expr_or();if(sbt_kind==SBT_COMMA)sbc_lex_next();a2=sbc_expr_or();if(sbt_kind==SBT_COMMA)sbc_lex_next();a3=sbc_expr_or();if(sbc_host&&sbc_host->video_poke)for(i=0;i<a2;++i)sbc_host->video_poke(sbc_defseg,(unsigned)(a1+i),a3);return 1;}
    if (sbc_name_eq(sbt_text, "OUT")) {
        sbc_lex_next(); a1 = sbc_expr_or();
        if (sbt_kind == SBT_COMMA) sbc_lex_next(); a2 = sbc_expr_or();
        sbv_out((unsigned)a1, a2); return 1;
    }
    if (sbc_name_eq(sbt_text, "REG")) {
        sbc_lex_next(); a1 = sbc_expr_or();
        if (sbt_kind == SBT_COMMA) sbc_lex_next(); a2 = sbc_expr_or();
        if (a1 >= 0 && a1 < 10) sbc_regs[a1] = a2;
        return 1;
    }
    if (sbc_name_eq(sbt_text, "DATA")) return 1;
    if (sbc_name_eq(sbt_text, "GET")) {
        sbc_lex_next(); if (sbt_kind==SBT_LP) sbc_lex_next();
        a1=sbc_expr_or(); if(sbt_kind==SBT_COMMA)sbc_lex_next(); a2=sbc_expr_or();
        if(sbt_kind==SBT_RP)sbc_lex_next(); if(sbt_kind==SBT_OP&&sbt_op=='-')sbc_lex_next();
        if(sbt_kind==SBT_LP)sbc_lex_next(); a3=sbc_expr_or(); if(sbt_kind==SBT_COMMA)sbc_lex_next(); a4=sbc_expr_or();
        if(sbt_kind==SBT_RP)sbc_lex_next(); if(sbt_kind==SBT_COMMA)sbc_lex_next();
        sym=sbt_kind==SBT_ID?sbc_find_symbol(sbt_text,1):0;i=(sym>=0?sbc_symbols[sym].array:-1);
        if(sbc_host&&sbc_host->gfx_get)sbc_host->gfx_get(a1,a2,a3,a4,sbm_lo_segment(),(unsigned)(i<0?0:sbc_arrays[i].base<<1));
        return 1;
    }
    if (sbc_name_eq(sbt_text, "PUT")) {
        sbc_lex_next(); if(sbt_kind==SBT_LP)sbc_lex_next(); a1=sbc_expr_or();
        if(sbt_kind==SBT_COMMA)sbc_lex_next(); a2=sbc_expr_or(); if(sbt_kind==SBT_RP)sbc_lex_next();
        if(sbt_kind==SBT_COMMA)sbc_lex_next(); sym=sbt_kind==SBT_ID?sbc_find_symbol(sbt_text,1):0;
        sbc_lex_next(); if(sbt_kind==SBT_COMMA)sbc_lex_next(); a3=SB_PUT_PSET;
        if(sbt_kind==SBT_ID){if(sbc_name_eq(sbt_text,"XOR"))a3=SB_PUT_XOR;else if(sbc_name_eq(sbt_text,"OR"))a3=SB_PUT_OR;else if(sbc_name_eq(sbt_text,"AND"))a3=SB_PUT_AND;else if(sbc_name_eq(sbt_text,"PRESET"))a3=SB_PUT_PRESET;}
        i=(sym>=0?sbc_symbols[sym].array:-1);if(sbc_host&&sbc_host->gfx_put)sbc_host->gfx_put(a1,a2,sbm_lo_segment(),(unsigned)(i<0?0:sbc_arrays[i].base<<1),a3);
        return 1;
    }
    if (sbc_name_eq(sbt_text, "ERASE")) {sbc_lex_next();while(sbt_kind==SBT_ID){sym=sbc_find_symbol(sbt_text,0);if(sym>=0)sbc_symbols[sym].array=-1;sbc_lex_next();if(sbt_kind==SBT_COMMA)sbc_lex_next();else break;}return 1;}
    if (sbc_name_eq(sbt_text, "DEFINT") || sbc_name_eq(sbt_text, "DEFLNG") ||
        sbc_name_eq(sbt_text, "DEFSNG") || sbc_name_eq(sbt_text, "DEFDBL") ||
        sbc_name_eq(sbt_text, "DEFSTR")) return 1;
    if (sbc_name_eq(sbt_text, "SUB")) { sbv_skip_sub(); return 1; }
    if (sbc_name_eq(sbt_text,"DEF")) return 1;
    if (sbc_name_eq(sbt_text, "LOCAL") || sbc_name_eq(sbt_text, "STATIC")) {
        sbc_lex_next();while(sbt_kind==SBT_ID){sym=sbc_find_symbol(sbt_text,1);if(sbv_save_local(sym)<0)return 0;sbc_symbols[sym].value=0;sbc_symbols[sym].numlo=0;sbc_symbols[sym].numhi=0;sbc_symbols[sym].numkind=SBN_LONG;sbc_symbols[sym].strslot=-1;sbc_lex_next();if(sbt_kind==SBT_COMMA)sbc_lex_next();else break;}return 1;
    }
    if (sbc_name_eq(sbt_text, "SHARED")) return 1;
    if (sbc_name_eq(sbt_text,"WIDTH")){sbc_lex_next();a1=sbc_expr_or();if(sbc_host&&sbc_host->mode)sbc_host->mode(sbc_gfx_mode,a1,sbc_gfx_mode?200:25);return 1;}
    if (sbc_name_eq(sbt_text,"VIEW")||sbc_name_eq(sbt_text,"WINDOW")){sbc_lex_next();if(sbt_kind==SBT_LP)sbc_lex_next();sbc_view_x1=sbc_expr_or();if(sbt_kind==SBT_COMMA)sbc_lex_next();sbc_view_y1=sbc_expr_or();if(sbt_kind==SBT_RP)sbc_lex_next();if(sbt_kind==SBT_OP&&sbt_op=='-')sbc_lex_next();if(sbt_kind==SBT_LP)sbc_lex_next();sbc_view_x2=sbc_expr_or();if(sbt_kind==SBT_COMMA)sbc_lex_next();sbc_view_y2=sbc_expr_or();return 1;}
    if(sbc_name_eq(sbt_text,"PALETTE")){sbc_lex_next();a1=sbc_expr_or();if(sbt_kind==SBT_COMMA)sbc_lex_next();a2=sbc_expr_or();if(sbc_host&&sbc_host->palette)sbc_host->palette(a1,a2&63,a2&63,a2&63);return 1;}
    if (sbc_name_eq(sbt_text,"KEY")){sbc_lex_next();while(sbt_kind!=SBT_EOF)sbc_lex_next();return 1;}
    if (sbc_name_eq(sbt_text, "ON")) { SBC_IGNORED(); return 1; }
    if(sbc_name_eq(sbt_text,"DRAW")){sbc_lex_next();ovl_sbv_draw();return 1;}
    if(sbc_name_eq(sbt_text,"PLAY")){if(sbc_host&&sbc_host->sound)sbc_host->sound(440,4);return 1;}
    if(sbc_name_eq(sbt_text,"PEN")||sbc_name_eq(sbt_text,"STRIG")||sbc_name_eq(sbt_text,"COMMON")||sbc_name_eq(sbt_text,"ENVIRON")){sbc_lex_next();while(sbt_kind!=SBT_EOF)sbc_lex_next();return 1;}
    if(sbc_name_eq(sbt_text,"RESET")){sbc_data_pos=0;return 1;}
    if(sbc_name_eq(sbt_text,"BLOAD")||sbc_name_eq(sbt_text,"BSAVE")||sbc_name_eq(sbt_text,"IOCTL")){sbc_lex_next();while(sbt_kind!=SBT_EOF)sbc_lex_next();return 1;}
    if(sbc_name_eq(sbt_text,"OPEN")||sbc_name_eq(sbt_text,"CLOSE")||sbc_name_eq(sbt_text,"SEEK")||sbc_name_eq(sbt_text,"FILES")||sbc_name_eq(sbt_text,"NAME")||sbc_name_eq(sbt_text,"KILL")||sbc_name_eq(sbt_text,"MKDIR")||sbc_name_eq(sbt_text,"CHDIR")||sbc_name_eq(sbt_text,"RMDIR")){sbc_lex_next();while(sbt_kind!=SBT_EOF)sbc_lex_next();return 1;}

    /* A label occupies an empty statement before its colon. */
    a = sbc_parse_pos;
    a = sbv_skip_space(a, sbc_stmt_end);
    if (a >= sbc_stmt_end) return 1;
    sbc_parse_pos = sbc_stmt_start;
    sbc_lex_next();
    sbv_assignment(0);
    return 1;
}

int sb_run_slice(int budget)
{
    int n;
    if (budget <= 0) budget = SB_SLICE_OPS;
    if (sbc_status == SB_STATE_READY) sbc_status = SB_STATE_RUNNING;
    if (sbc_status == SB_STATE_WAITING && sbv_wait_time && sbc_host && sbc_host->ticks) {
        if ((int)(sbc_host->ticks() - sbv_wait_until) >= 0) {
            sbv_wait_time = 0;
            sbc_status = SB_STATE_RUNNING;
        }
    }
    if (sbc_status != SB_STATE_RUNNING) return sbc_status;
    n = 0;
    while (n < budget && sbc_status == SB_STATE_RUNNING) {
        if (!sbc_exec_one()) break;
        ++n;
    }
    if (sbc_host && sbc_host->changed && n) sbc_host->changed();
    return sbc_status;
}
