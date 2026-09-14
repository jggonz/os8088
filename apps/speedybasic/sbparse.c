/* Streaming lexer and expression parser.  Keeping only the current token is
 * deliberate: a 30KB BASIC file remains a 30KB file instead of becoming a
 * several-times-larger AST on a machine with a 64KB data segment. */

#define SBT_EOF    0
#define SBT_NUM    1
#define SBT_ID     2
#define SBT_STR    3
#define SBT_OP     4
#define SBT_COMMA  5
#define SBT_SEMI   6
#define SBT_LP     7
#define SBT_RP     8

static int sbt_kind;
static int sbt_num;
static unsigned sbt_lo;
static int sbt_hi;
static int sbt_nkind;
static int sbt_op;
static char sbt_text[96];
static char sbt_index_name[SBC_NAME_MAX];

static int sbc_isalpha(int c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static int sbc_isdigit(int c) { return c >= '0' && c <= '9'; }

/* Keep constant multiplication outside condition-controlled loops.  SmallerC
 * otherwise leaves FLAGS live across an `imul ax,ax,10`, which the 8086 gate
 * correctly refuses because its rewrite changes FLAGS. */
static int sbc_mul10(int n) { return (n << 3) + (n << 1); }

static void sbc_lex_next(void)
{
    int c;
    int n;
    int sign;
    int frac;
    int dot;
    while (sbc_parse_pos < sbc_stmt_end) {
        c = sbc_ch(sbc_parse_pos);
        if (c != ' ' && c != '\t') break;
        ++sbc_parse_pos;
    }
    if (sbc_parse_pos >= sbc_stmt_end) {
        sbt_kind = SBT_EOF;
        return;
    }
    c = sbc_ch(sbc_parse_pos++);
    if (sbc_isdigit(c) || (c == '.' && sbc_isdigit(sbc_ch(sbc_parse_pos)))) {
        sbc_num_long(0);frac=0;dot=0;
        for(;;){if(c=='.'){dot=1;c=sbc_ch(sbc_parse_pos++);continue;}if(!sbc_isdigit(c))break;if(!dot||frac<3){sbn_blo=10;sbn_bhi=0;sbn_bkind=SBN_LONG;sbn_mul();sbn_blo=(unsigned)(c-'0');sbn_bhi=0;sbn_bkind=SBN_LONG;sbn_add();if(dot)++frac;}c=sbc_ch(sbc_parse_pos);if(sbc_isdigit(c)||c=='.')++sbc_parse_pos;else break;}
        while(frac-->0){sbn_blo=0;sbn_bhi=10;sbn_bkind=SBN_FIXED;sbn_div();}
        c = sbc_ch(sbc_parse_pos);
        if (c == 'E' || c == 'e' || c == 'D' || c == 'd') {
            ++sbc_parse_pos;
            sign = 1;
            c = sbc_ch(sbc_parse_pos);
            if (c == '+' || c == '-') {
                if (c == '-') sign = -1;
                ++sbc_parse_pos;
            }
            c = 0;
            while (sbc_isdigit(sbc_ch(sbc_parse_pos)))
                c = sbc_mul10(c) + sbc_ch(sbc_parse_pos++) - '0';
            while(c-->0){if(sign>0){sbn_blo=10;sbn_bhi=0;sbn_bkind=SBN_LONG;sbn_mul();}else{sbn_blo=0;sbn_bhi=10;sbn_bkind=SBN_FIXED;sbn_div();}}
        }
        c=sbc_ch(sbc_parse_pos);if(c=='!'||c=='#'||c=='%'||c=='&')++sbc_parse_pos;
        sbt_kind = SBT_NUM;
        sbt_lo=sbn_alo;sbt_hi=sbn_ahi;sbt_nkind=sbn_akind;sbt_num=sbc_num_int();
        return;
    }
    if (c == '&' && (sbc_ch(sbc_parse_pos) == 'H' || sbc_ch(sbc_parse_pos) == 'h')) {
        ++sbc_parse_pos;
        sbc_num_long(0);
        for (;;) {
            c = sbc_up(sbc_ch(sbc_parse_pos));
            if (sbc_isdigit(c)) c -= '0';
            else if (c >= 'A' && c <= 'F') c -= 'A' - 10;
            else break;
            sbn_blo=16;sbn_bhi=0;sbn_bkind=SBN_LONG;sbn_mul();sbn_blo=(unsigned)c;sbn_bhi=0;sbn_bkind=SBN_LONG;sbn_add();
            ++sbc_parse_pos;
        }
        sbt_kind = SBT_NUM;
        sbt_lo=sbn_alo;sbt_hi=sbn_ahi;sbt_nkind=SBN_LONG;sbt_num=sbc_num_int();
        return;
    }
    if (sbc_isalpha(c)) {
        n = 0;
        do {
            if (n < (int)sizeof(sbt_text) - 1) sbt_text[n++] = (char)sbc_up(c);
            c = sbc_ch(sbc_parse_pos);
            if (!sbc_isalpha(c) && !sbc_isdigit(c) && c != '$' && c != '%' &&
                c != '&' && c != '!' && c != '#') break;
            ++sbc_parse_pos;
        } while (1);
        sbt_text[n] = 0;
        if (sbc_name_eq(sbt_text, "MOD") || sbc_name_eq(sbt_text, "AND") ||
            sbc_name_eq(sbt_text, "OR") || sbc_name_eq(sbt_text, "XOR") ||
            sbc_name_eq(sbt_text, "NOT")) {
            sbt_kind = SBT_OP;
            sbt_op = sbt_text[0] == 'M' ? 'M' : sbt_text[0];
        } else sbt_kind = SBT_ID;
        return;
    }
    if (c == '"') {
        n = 0;
        while (sbc_parse_pos < sbc_stmt_end) {
            c = sbc_ch(sbc_parse_pos++);
            if (c == '"') {
                if (sbc_ch(sbc_parse_pos) == '"') ++sbc_parse_pos;
                else break;
            }
            if (n < (int)sizeof(sbt_text) - 1) sbt_text[n++] = (char)c;
        }
        sbt_text[n] = 0;
        sbt_kind = SBT_STR;
        return;
    }
    if (c == ',') sbt_kind = SBT_COMMA;
    else if (c == ';') sbt_kind = SBT_SEMI;
    else if (c == '(') sbt_kind = SBT_LP;
    else if (c == ')') sbt_kind = SBT_RP;
    else {
        sbt_kind = SBT_OP;
        sbt_op = c;
        if(c=='\\'&&sbc_ch(sbc_parse_pos)=='\\')++sbc_parse_pos;
        if ((c == '<' || c == '>') && sbc_ch(sbc_parse_pos) == '=') {
            sbt_op = c == '<' ? 'l' : 'g';
            ++sbc_parse_pos;
        } else if (c == '<' && sbc_ch(sbc_parse_pos) == '>') {
            sbt_op = 'n';
            ++sbc_parse_pos;
        }
    }
}

static int sbc_truth(int v) { return v ? -1 : 0; }

static int sbc_expr_or(void);
static int sbc_eval_at(unsigned begin,unsigned end);

static int sbc_find_def(const char *name)
{int i;for(i=0;i<sbc_ndefs;++i)if(sbc_name_eq(sbc_deffns[i].name,name))return i;return -1;}

static int sbc_primary(void)
{
    int v;
    int s;
    int a;
    int a2;
    int fn;
    int cell;
    int k;
    unsigned flo;
    int fhi;
    int fkind;
    if (sbt_kind == SBT_NUM) {
        v = sbt_num;
        sbn_alo=sbt_lo;sbn_ahi=sbt_hi;sbn_akind=sbt_nkind;
        sbc_lex_next();
        return v;
    }
    if (sbt_kind == SBT_STR) {
        k = 0;
        while (sbt_text[k]) ++k;
        sbc_lex_next();
        sbc_num_long(k);
        return k;
    }
    if (sbt_kind == SBT_LP) {
        sbc_lex_next();
        v = sbc_expr_or();
        if (sbt_kind == SBT_RP) sbc_lex_next();
        return v;
    }
    if (sbt_kind == SBT_ID) {
        fn = 0;
        if (sbc_name_eq(sbt_text, "ABS")) fn = 1;
        else if (sbc_name_eq(sbt_text, "SGN")) fn = 2;
        else if(sbc_name_eq(sbt_text,"FIX")||sbc_name_eq(sbt_text,"CLNG"))fn=3;
        else if(sbc_name_eq(sbt_text,"INT"))fn=13;
        else if(sbc_name_eq(sbt_text,"CINT"))fn=14;
        else if(sbc_name_eq(sbt_text,"SIN"))fn=15;
        else if(sbc_name_eq(sbt_text,"COS"))fn=16;
        else if(sbc_name_eq(sbt_text,"SQR"))fn=17;
        else if(sbc_name_eq(sbt_text,"CEIL"))fn=18;
        else if(sbc_name_eq(sbt_text,"CSNG")||sbc_name_eq(sbt_text,"CDBL"))fn=19;
        else if(sbc_name_eq(sbt_text,"TAN"))fn=20;
        else if(sbc_name_eq(sbt_text,"ATN"))fn=21;
        else if(sbc_name_eq(sbt_text,"EXP"))fn=22;
        else if(sbc_name_eq(sbt_text,"EXP2"))fn=23;
        else if(sbc_name_eq(sbt_text,"EXP10"))fn=24;
        else if(sbc_name_eq(sbt_text,"LOG"))fn=25;
        else if(sbc_name_eq(sbt_text,"LOG2"))fn=26;
        else if(sbc_name_eq(sbt_text,"LOG10"))fn=27;
        else if (sbc_name_eq(sbt_text, "RND")) fn = 4;
        else if (sbc_name_eq(sbt_text, "TIMER")) fn = 5;
        else if (sbc_name_eq(sbt_text, "POINT")) fn = 6;
        else if (sbc_name_eq(sbt_text, "PEEK")) fn = 7;
        else if (sbc_name_eq(sbt_text, "INP")) fn = 8;
        else if (sbc_name_eq(sbt_text, "LEN")) fn = 9;
        else if (sbc_name_eq(sbt_text, "ASC")) fn = 10;
        else if (sbc_name_eq(sbt_text, "VAL")) fn = 11;
        else if (sbc_name_eq(sbt_text, "INSTR")) fn = 12;
        k=sbc_find_def(sbt_text);
        s = sbc_find_symbol(sbt_text, 1);
        sbc_lex_next();
        if (sbt_kind == SBT_LP) {
            sbc_lex_next();
            if(k>=0){
                unsigned resume;unsigned oldend;int ps;int old;unsigned olo;int ohi;int ok;
                a=sbc_expr_or();resume=sbc_parse_pos;oldend=sbc_stmt_end;
                ps=sbc_find_symbol(sbc_deffns[k].param,1);old=sbc_symbols[ps].value;olo=sbc_symbols[ps].numlo;ohi=sbc_symbols[ps].numhi;ok=sbc_symbols[ps].numkind;sbc_num_save_symbol(ps);
                v=sbc_eval_at(sbc_deffns[k].begin,sbc_deffns[k].end);sbc_symbols[ps].value=old;sbc_symbols[ps].numlo=olo;sbc_symbols[ps].numhi=ohi;sbc_symbols[ps].numkind=ok;
                sbc_stmt_end=oldend;sbc_parse_pos=resume;sbc_lex_next();return v;
            }
            if(fn>=9&&fn<=12){v=ovl_sbv_string_numeric(fn);sbc_num_long(v);return v;}
            a = sbc_expr_or();
            a2 = 0;
            while (sbt_kind == SBT_COMMA) {
                sbc_lex_next();
                a2 = sbc_expr_or();
            }
            if (sbt_kind == SBT_RP) sbc_lex_next();
            if(fn==1){sbn_blo=0;sbn_bhi=0;sbn_bkind=SBN_LONG;if(sbn_cmp()<0)sbn_neg();return sbc_num_int();}
            if(fn==2){sbn_blo=0;sbn_bhi=0;sbn_bkind=SBN_LONG;a=sbn_cmp();sbc_num_long(a<0?-1:(a>0));return sbc_num_int();}
            if(fn==3){sbn_fix();return sbc_num_int();}if(fn==13){sbn_floor();return sbc_num_int();}if(fn==14){sbn_cint();return sbc_num_int();}
            if(fn==15){sbn_sin();sbc_num_check();return sbc_num_int();}if(fn==16){sbn_cos();sbc_num_check();return sbc_num_int();}if(fn==17){sbn_sqrt();sbc_num_check();return sbc_num_int();}
            if(fn==18){flo=sbn_alo;fhi=sbn_ahi;fkind=sbn_akind;sbn_floor();v=sbc_num_int();sbn_blo=sbn_alo;sbn_bhi=sbn_ahi;sbn_bkind=sbn_akind;sbn_alo=flo;sbn_ahi=fhi;sbn_akind=fkind;if(sbn_cmp()>0)++v;sbc_num_long(v);return v;}
            if(fn==19){sbn_float();sbc_num_check();return sbc_num_int();}
            if(fn==20){sbn_sin();return sbc_num_int();}
            if(fn==21){if(a==1){sbn_alo=51472u;sbn_ahi=0;sbn_akind=SBN_FIXED;}return sbc_num_int();}
            if(fn==22){if(a==0)sbc_num_long(1);else{sbn_alo=47121u;sbn_ahi=2;sbn_akind=SBN_FIXED;}return sbc_num_int();}
            if(fn==23||fn==24){v=1;k=a;while(k-->0)v*=fn==23?2:10;sbc_num_long(v);return v;}
            if(fn==25){sbc_num_long(a>1?1:0);return sbc_num_int();}
            if(fn==26||fn==27){v=0;k=a;while(k>1){k/=fn==26?2:10;++v;}sbc_num_long(v);return v;}
            if(fn==4){sbn_alo=(sbc_next_rand()&0x7fffu)<<1;sbn_ahi=0;sbn_akind=SBN_FIXED;return sbc_num_int();}
            if(fn==6&&sbc_host&&sbc_host->point){v=sbc_host->point(a,a2);sbc_num_long(v);return v;}
            if (fn == 7 && sbc_host && sbc_host->video_peek)
                {v=sbc_host->video_peek(sbc_defseg,(unsigned)a);sbc_num_long(v);return v;}
            if(fn==8){sbc_num_long(0);return 0;}
            if (!fn && s >= 0) {
                cell=sbc_array_cell(s,a,a2);
                if(cell>=0){sbn_alo=sbm_get_lo((unsigned)cell);sbn_ahi=sbm_get_hi((unsigned)cell);sbn_akind=sbm_kind_get((unsigned)cell);return sbc_num_int();}
            }
        }
        if(fn==4){sbn_alo=(sbc_next_rand()&0x7fffu)<<1;sbn_ahi=0;sbn_akind=SBN_FIXED;return sbc_num_int();}
        if (fn == 5 && sbc_host && sbc_host->ticks)
            {sbc_num_long((int)sbc_host->ticks());return sbc_num_int();}
        if (s < 0) {
            sbc_set_error("Symbol table full");
            return 0;
        }
        sbc_num_load_symbol(s);return sbc_num_int();
    }
    sbc_num_long(0);
    return 0;
}

static int sbc_unary(void)
{
    int op;
    int v;
    if (sbt_kind == SBT_OP && (sbt_op == '+' || sbt_op == '-' || sbt_op == 'N')) {
        op = sbt_op;
        sbc_lex_next();
        v = sbc_unary();
        if(op=='-'){sbn_neg();sbc_num_check();return sbc_num_int();}if(op=='N'){sbc_num_long(~v);return sbc_num_int();}
        return v;
    }
    return sbc_primary();
}

static int sbc_power(void)
{
    int a;
    int b;
    int r;
    unsigned lo;
    int hi;
    int kind;
    a = sbc_unary();
    if (sbt_kind == SBT_OP && sbt_op == '^') {
        lo=sbn_alo;hi=sbn_ahi;kind=sbn_akind;
        sbc_lex_next();
        b=sbc_power();
        r = 1;
        sbc_num_long(1);while(b-->0){sbn_blo=lo;sbn_bhi=hi;sbn_bkind=kind;sbn_mul();if(sbn_err)break;}r=sbc_num_int();
        return r;
    }
    return a;
}

static int sbc_mul(void)
{
    int a;
    int b;
    int op;
    unsigned lo;
    int hi;
    int kind;
    a = sbc_power();
    while (sbt_kind == SBT_OP && (sbt_op == '*' || sbt_op == '/' ||
           sbt_op == '\\' || sbt_op == 'M')) {
        op = sbt_op;
        lo=sbn_alo;hi=sbn_ahi;kind=sbn_akind;
        sbc_lex_next();
        b = sbc_power();
        if(op=='M'||op=='\\'){b=sbc_num_int();sbn_alo=lo;sbn_ahi=hi;sbn_akind=kind;a=sbc_num_int();if(!b)sbc_set_error("Division by zero");else{sbc_num_long(op=='M'?a%b:a/b);a=sbc_num_int();}}
        else{sbn_blo=sbn_alo;sbn_bhi=sbn_ahi;sbn_bkind=sbn_akind;sbn_alo=lo;sbn_ahi=hi;sbn_akind=kind;if(op=='*')sbn_mul();else sbn_fdiv();sbc_num_check();a=sbc_num_int();}
    }
    return a;
}

static int sbc_add(void)
{
    int a;
    int op;
    unsigned lo;
    int hi;
    int kind;
    a = sbc_mul();
    while (sbt_kind == SBT_OP && (sbt_op == '+' || sbt_op == '-')) {
        op = sbt_op;
        lo=sbn_alo;hi=sbn_ahi;kind=sbn_akind;
        sbc_lex_next();
        (void)sbc_mul();
        sbn_blo=sbn_alo;sbn_bhi=sbn_ahi;sbn_bkind=sbn_akind;sbn_alo=lo;sbn_ahi=hi;sbn_akind=kind;if(op=='+')sbn_add();else sbn_sub();sbc_num_check();a=sbc_num_int();
    }
    return a;
}

static int sbc_relation(void)
{
    int a;
    int op;
    unsigned lo;
    int hi;
    int kind;
    a = sbc_add();
    if (sbt_kind != SBT_OP || (sbt_op != '=' && sbt_op != '<' && sbt_op != '>' &&
        sbt_op != 'l' && sbt_op != 'g' && sbt_op != 'n')) return a;
    op = sbt_op;
    lo=sbn_alo;hi=sbn_ahi;kind=sbn_akind;
    sbc_lex_next();
    (void)sbc_add();
    sbn_blo=sbn_alo;sbn_bhi=sbn_ahi;sbn_bkind=sbn_akind;sbn_alo=lo;sbn_ahi=hi;sbn_akind=kind;a=sbn_cmp();if(op=='=')a=sbc_truth(a==0);else if(op=='<')a=sbc_truth(a<0);else if(op=='>')a=sbc_truth(a>0);else if(op=='l')a=sbc_truth(a<=0);else if(op=='g')a=sbc_truth(a>=0);else a=sbc_truth(a!=0);sbc_num_long(a);return a;
}

static int sbc_expr_or(void)
{
    int a;
    int b;
    int op;
    a = sbc_relation();
    while (sbt_kind == SBT_OP && (sbt_op == 'A' || sbt_op == 'O' || sbt_op == 'X')) {
        op = sbt_op;
        sbc_lex_next();
        b = sbc_relation();
        b=sbc_num_int();
        if (op == 'A') a &= b; else if (op == 'O') a |= b; else a ^= b;
        sbc_num_long(a);
    }
    return a;
}

static int sbc_eval_at(unsigned begin, unsigned end)
{
    unsigned oldend;
    int v;
    oldend = sbc_stmt_end;
    sbc_parse_pos = begin;
    sbc_stmt_end = end;
    sbc_lex_next();
    v = sbc_expr_or();
    sbc_stmt_end = oldend;
    return v;
}

static void sbc_add_label(const char *name, unsigned pos, int ifdepth)
{
    int n;
    if (sbc_nlabels >= SBC_LABELS) return;
    n = sbc_nlabels++;
    sbc_copy_name(sbc_labels[n].name, name);
    sbc_labels[n].pos = pos;
    sbc_labels[n].ifdepth = ifdepth;
    sbc_proc_decl[n] = (unsigned)0xffff;
}

static int sbc_collect_data(unsigned begin, unsigned end)
{
    unsigned p;
    unsigned q;
    int quote;
    int n;
    p = begin;
    while (p < end && sbc_ndata < SBC_DATA_ITEMS) {
        q = p;
        quote = 0;
        while (q < end) {
            if (sbc_ch(q) == '"') quote = !quote;
            if (!quote && sbc_ch(q) == ',') break;
            ++q;
        }
        if(!sbc_ndata){n=sbm_make(2);if(n<0){sbc_set_error("Array space full");return -1;}sbc_data_base=(unsigned)n;}
        else if(sbm_make(2)<0){sbc_set_error("Array space full");return -1;}
        (void)sbc_eval_at(p,q);if(sbm_set_pair(sbc_data_base+sbc_ndata*2,sbn_alo,sbn_ahi)<0){sbc_set_error("Array space full");return -1;}sbm_set(sbc_data_base+sbc_ndata*2+1,sbn_akind);++sbc_ndata;
        p = q + 1;
    }
    return 0;
}

static int ovl_sbc_index_program(void)
{
    unsigned p;
    unsigned q;
    unsigned body;
    int ifdepth;
    int ifdelta;
    int n;
    int c;
    int div;
    int is_sub;
    unsigned data_begin;
    unsigned proc_decl;
    unsigned de;
    p = 0;
    ifdepth = 0;
    while (p < sbc_source_len) {
        body = p;
        is_sub = 0;
        ifdelta = 0;
        data_begin = (unsigned)0xffff;
        proc_decl = (unsigned)0xffff;
        while (sbc_ch(body) == ' ' || sbc_ch(body) == '\t') ++body;
        q = body;
        n = 0;
        while (sbc_isdigit(sbc_ch(q))) n = sbc_mul10(n) + sbc_ch(q++) - '0';
        if (q > body && (sbc_ch(q) == ' ' || sbc_ch(q) == '\t')) {
            while (sbc_ch(q) == ' ' || sbc_ch(q) == '\t') ++q;
            sbt_index_name[0] = '#';
            body = q;
            q = 1;
            if (n == 0) sbt_index_name[q++] = '0';
            else {
                div = 10000;
                while (div > n && div > 1) div /= 10;
                while (div) { sbt_index_name[q++] = (char)('0' + (n / div) % 10); div /= 10; }
            }
            sbt_index_name[q] = 0;
            sbc_add_label(sbt_index_name, body, ifdepth);
        } else if (sbc_isalpha(sbc_ch(body))) {
            q = body;
            n = 0;
            while ((c = sbc_ch(q)) != 0 && (sbc_isalpha(c) || sbc_isdigit(c))) {
                if (n < SBC_NAME_MAX - 1) sbt_index_name[n++] = (char)sbc_up(c);
                ++q;
            }
            sbt_index_name[n] = 0;
            if (sbc_ch(q) == ':') {
                ++q;
                sbc_add_label(sbt_index_name, q, ifdepth);
            } else if (sbc_name_eq(sbt_index_name, "SUB")) {
                while (q < sbc_source_len && (sbc_ch(q) == ' ' || sbc_ch(q) == '\t')) ++q;
                n = 0;
                while ((c = sbc_ch(q)) != 0 && (sbc_isalpha(c) || sbc_isdigit(c) || c == '_')) {
                    if (n < SBC_NAME_MAX - 1) sbt_index_name[n++] = (char)sbc_up(c);
                    ++q;
                }
                sbt_index_name[n] = 0;
                is_sub = n != 0;
                proc_decl = q;
            } else if (sbc_name_eq(sbt_index_name, "DATA")) {
                data_begin = q;
            } else if (sbc_name_eq(sbt_index_name,"END")) {
                while(sbc_ch(q)==' '||sbc_ch(q)=='\t')++q;
                if(sbc_up(sbc_ch(q))=='I'&&sbc_up(sbc_ch(q+1))=='F'&&!sbc_isalpha(sbc_ch(q+2)))ifdelta=-1;
            } else if (sbc_name_eq(sbt_index_name,"IF")) {
                while(q<sbc_source_len&&sbc_ch(q)!='\r'&&sbc_ch(q)!='\n'){
                    if(sbc_up(sbc_ch(q))=='T'&&sbc_up(sbc_ch(q+1))=='H'&&sbc_up(sbc_ch(q+2))=='E'&&sbc_up(sbc_ch(q+3))=='N'&&!sbc_isalpha(sbc_ch(q+4))){q+=4;while(sbc_ch(q)==' '||sbc_ch(q)=='\t')++q;if(sbc_ch(q)=='\r'||sbc_ch(q)=='\n'||sbc_ch(q)=='\''||!sbc_ch(q))ifdelta=1;break;}++q;
                }
            } else if (sbc_name_eq(sbt_index_name,"DEF") && sbc_ndefs<SBC_DEFFNS) {
                while(sbc_ch(q)==' '||sbc_ch(q)=='\t')++q;
                n=0;while((c=sbc_ch(q))&&(sbc_isalpha(c)||sbc_isdigit(c)||c=='$')){if(n<SBC_NAME_MAX-1)sbc_deffns[sbc_ndefs].name[n++]=(char)sbc_up(c);++q;}sbc_deffns[sbc_ndefs].name[n]=0;
                while(sbc_ch(q)!='('&&sbc_ch(q)!='\n'&&sbc_ch(q))++q;if(sbc_ch(q)=='(')++q;
                n=0;while((c=sbc_ch(q))&&(sbc_isalpha(c)||sbc_isdigit(c)||c=='$')){if(n<SBC_NAME_MAX-1)sbc_deffns[sbc_ndefs].param[n++]=(char)sbc_up(c);++q;}sbc_deffns[sbc_ndefs].param[n]=0;
                while(sbc_ch(q)!='='&&sbc_ch(q)!='\n'&&sbc_ch(q))++q;if(sbc_ch(q)=='=')++q;de=q;while(de<sbc_source_len&&sbc_ch(de)!='\n'&&sbc_ch(de)!='\r')++de;
                sbc_deffns[sbc_ndefs].begin=q;sbc_deffns[sbc_ndefs].end=de;++sbc_ndefs;
            }
        }
        while (p < sbc_source_len && sbc_ch(p) != '\n' && sbc_ch(p) != '\r') ++p;
        if (data_begin != (unsigned)0xffff && sbc_collect_data(data_begin, p)<0) return -1;
        while (p < sbc_source_len && (sbc_ch(p) == '\n' || sbc_ch(p) == '\r')) ++p;
        if (is_sub) {
            sbc_add_label(sbt_index_name, p, ifdepth);
            sbc_proc_decl[sbc_nlabels-1] = proc_decl;
        }
        ifdepth += ifdelta;
        if(ifdepth<0)ifdepth=0;
    }
    if (sbc_nlabels >= SBC_LABELS) {
        sbc_set_error("Too many labels");
        return -1;
    }
    return 0;
}

static unsigned sbc_find_label(const char *name)
{
    int i;
    for (i = 0; i < sbc_nlabels; ++i)
        if (sbc_name_eq(sbc_labels[i].name, name)) {sbc_jump_ifdepth=sbc_labels[i].ifdepth;return sbc_labels[i].pos;}
    return (unsigned)0xffff;
}
