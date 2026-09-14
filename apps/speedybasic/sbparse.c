/* Streaming tokens and precedence parser. Values are handles into a bounded
 * expression arena; arithmetic mutates its left operand to keep nesting cheap. */
#define SBT_EOF 0
#define SBT_NUM 1
#define SBT_ID 2
#define SBT_STR 3
#define SBT_OP 4
#define SBT_COMMA 5
#define SBT_SEMI 6
#define SBT_LP 7
#define SBT_RP 8
static int sbt_kind, sbt_op;
static unsigned sbt_begin;
static char sbt_text[256];
static int sbc_expr(int prec);
static int sbc_userfn(const char *name);
static int sbc_isalpha(int c) { return (sbc_up(c)>='A' && sbc_up(c)<='Z') || c=='_'; }
static int sbc_isdigit(int c) { return c>='0' && c<='9'; }
static int sbc_isname(int c) { return sbc_isalpha(c)||sbc_isdigit(c)||c=='$'||c=='%'||c=='&'||c=='!'||c=='#'; }
static void sbc_lex_next(void)
{
    int c,n;
    while (sbc_parse_pos<sbc_stmt_end && (sbc_ch(sbc_parse_pos)==' '||sbc_ch(sbc_parse_pos)=='\t')) ++sbc_parse_pos;
    sbt_begin=sbc_parse_pos;
    if (sbc_parse_pos>=sbc_stmt_end) { sbt_kind=SBT_EOF; return; }
    c=sbc_ch(sbc_parse_pos++); n=0;
    if (sbc_isdigit(c)||c=='.'||(c=='&'&&(sbc_up(sbc_ch(sbc_parse_pos))=='H'))) {
        sbt_kind=SBT_NUM; sbt_text[n++]=c;
        while (sbc_parse_pos<sbc_stmt_end) {
            c=sbc_up(sbc_ch(sbc_parse_pos));
            if (!sbc_isdigit(c)&&c!='.'&&!(c>='A'&&c<='F')&&c!='H'&&
                !((c=='+'||c=='-')&&(sbt_text[n-1]=='E'||sbt_text[n-1]=='D'))) break;
            if (n<254) sbt_text[n++]=c; ++sbc_parse_pos;
        }
        sbt_text[n]=0; return;
    }
    if (sbc_isalpha(c)) {
        sbt_kind=SBT_ID;
        do { if(n<254) sbt_text[n++]=sbc_up(c); if(sbc_parse_pos>=sbc_stmt_end)break; c=sbc_ch(sbc_parse_pos);
            if (!sbc_isname(c)) break; ++sbc_parse_pos; } while(1);
        sbt_text[n]=0;
        if (sbc_name_eq(sbt_text,"AND")||sbc_name_eq(sbt_text,"OR")||sbc_name_eq(sbt_text,"XOR")||sbc_name_eq(sbt_text,"NOT")||sbc_name_eq(sbt_text,"MOD")) {
            sbt_kind=SBT_OP; sbt_op=sbt_text[0];
        }
        return;
    }
    if(c=='"') {
        sbt_kind=SBT_STR;
        while(sbc_parse_pos<sbc_stmt_end) {
            c=sbc_ch(sbc_parse_pos++);
            if(c=='"') { if(sbc_ch(sbc_parse_pos)!='"') break; ++sbc_parse_pos; }
            if(n<255) sbt_text[n++]=c;
        }
        sbt_text[n]=0; return;
    }
    if(c==',') sbt_kind=SBT_COMMA;
    else if(c==';') sbt_kind=SBT_SEMI;
    else if(c=='(') sbt_kind=SBT_LP;
    else if(c==')') sbt_kind=SBT_RP;
    else {
        sbt_kind=SBT_OP; sbt_op=c;
        if ((c=='<'||c=='>')&&sbc_ch(sbc_parse_pos)=='=') { sbt_op=c=='<'?'l':'g'; ++sbc_parse_pos; }
        else if(c=='<'&&sbc_ch(sbc_parse_pos)=='>') { sbt_op='n'; ++sbc_parse_pos; }
        else if(c=='\\'&&sbc_ch(sbc_parse_pos)=='\\') ++sbc_parse_pos;
    }
}
static int sbc_token(const char *s) { return sbt_kind==SBT_ID&&sbc_name_eq(sbt_text,s); }
static void sbc_expect(int kind)
{ if(sbt_kind!=kind) sbc_set_error("Syntax error"); else sbc_lex_next(); }
static unsigned sbc_subscript(int sym)
{
    int a,b; unsigned idx;
    if(sbt_kind!=SBT_LP) return 0;
    sbc_lex_next(); a=sbn_word(sbc_expr(0))-sbc_option_base; b=0;
    if(sbt_kind==SBT_COMMA) { sbc_lex_next(); b=sbn_word(sbc_expr(0))-sbc_option_base; }
    sbc_expect(SBT_RP);
    if(a<0||b<0||(unsigned)a>=sbc_symbols[sym].rows||(unsigned)b>=sbc_symbols[sym].cols) {
        sbc_set_error("Subscript out of range"); return 0;
    }
    idx=(unsigned)a*sbc_symbols[sym].cols+b;
    return idx*sbc_symbols[sym].type;
}
static int sbc_builtin(int fn,int a,int b,int c)
{
    int i,j,k,n,mark,r,negative,inverse; char *s,*t;
    if(fn==1) {if(a>=0&&(sbn_values[a][7]&128))sbn_unary('-',a);return a;}
    if(fn==2) return sbn_int(sbn_true(a)?((sbn_values[a][7]&128)?-1:1):0);
    if(fn==3) return sbn_unary(6,a);
    if(fn==4) return sbn_unary(7,a);
    if(fn==5) return sbn_unary(9,a);
    if(fn==6) return sbn_trig(1,a);
    if(fn==7) return sbn_trig(2,a);
    if(fn==8) return sbn_unary(8,a);
    if(fn==9) {
        /* Reduce to |x| <= sqrt(2)-1 before the odd series. */
        mark=sbn_top;negative=(sbn_values[a][7]&128)!=0;if(negative)sbn_unary('-',a);
        n=sbn_int(1);inverse=sbn_cmp(a,n)>0;
        if(inverse){sbn_math('/',n,a);for(i=0;i<8;++i)sbn_values[a][i]=sbn_values[n][i];sbn_fast[a]=0;}
        j=sbn_copy(a);sbn_math('*',j,a);n=sbn_int(1);sbn_math('+',j,n);sbn_unary(8,j);sbn_math('+',j,n);sbn_math('/',a,j);
        j=sbn_copy(a);sbn_math('*',j,a);n=sbn_copy(a);k=sbn_copy(a);r=sbn_top;
        for(c=1;c<12;++c){b=sbn_int(c*2+1);sbn_math('*',n,j);sbn_unary('-',n);b=sbn_math('/',sbn_copy(n),b);sbn_math('+',k,b);sbn_top=r;}
        n=sbn_int(2);sbn_math('*',k,n);
        if(inverse){n=sbn_parse("1.570796326794897");sbn_math('-',n,k);k=n;}
        if(negative)sbn_unary('-',k);
        for(i=0;i<8;++i)sbn_values[a][i]=sbn_values[k][i];sbn_fast[a]=0;
        sbn_top=mark;return a;
    }
    if(fn==10) { i=sbc_next_rand()&32767; n=sbn_int(i); j=sbn_parse("32768"); sbn_math('/',n,j); sbn_top=j; return n; }
    if(fn==11) { n=sbn_int((int)(sbc_h_ticks()&32767)); j=sbn_parse("18.2"); sbn_math('/',n,j); sbn_top=j; return n; }
    if(fn==12) return sbn_int(sbs_mem_read(sbc_defseg,(unsigned)sbn_word(a)));
    if(fn==13) return sbn_int(8);
    if(fn==14) { s=sbn_text(a); i=0; while(s[i]) ++i; return sbn_int(i); }
    if(fn==15) {i=(unsigned char)sbn_text(a)[0];return sbn_int(i==255?0:i);}
    if(fn==16) { n=sbn_string("");i=sbn_word(a);sbn_strings[-n-1][0]=i?i:255; sbn_strings[-n-1][1]=0; return n; }
    if(fn==17) { s=sbn_text(a); return sbn_string(s); }
    if(fn==18) return sbn_parse(sbn_text(a));
    if(fn==19||fn==20||fn==21) {
        s=sbn_text(a); i=0; while(s[i]) ++i;
        j=fn==19?0:fn==20?i-sbn_word(b):sbn_word(b)-1;
        k=fn==21?(c==32767?255:sbn_word(c)):sbn_word(b);
        if(j<0) j=0; if(j>i) j=i; if(k<0) k=0;
        n=sbn_string(""); t=sbn_strings[-n-1]; i=0;
        while(i<k&&i<255&&s[j]) t[i++]=s[j++]; t[i]=0; return n;
    }
    if(fn==22||fn==23) {
        n=sbn_string(""); i=sbn_word(a); if(i>255)i=255; if(i<0)i=0;
        j=fn==22?' ':b<0?(unsigned char)sbn_text(b)[0]:sbn_word(b);
        for(k=0;k<i;++k) sbn_strings[-n-1][k]=j; sbn_strings[-n-1][i]=0; return n;
    }
    if(fn==24||fn==25) {
        s=sbn_text(a); for(i=0;s[i];++i) { if(fn==24)s[i]=sbc_up(s[i]); else if(s[i]>='A'&&s[i]<='Z')s[i]+=32; } return a;
    }
    if(fn==26||fn==27||fn==28) {
        s=sbn_text(a); i=0; while(s[i])++i; j=0;
        if(fn!=27)while(s[j]==' ')++j;
        if(fn!=26)while(i>j&&s[i-1]==' ')--i;
        s[i]=0; n=sbn_string(s+j); return n;
    }
    if(fn==29) {
        ++sbc_keypolls;
        n=sbn_string(""); if(sbc_key_count) {
            i=sbc_key_head; sbc_key_head=(i+1)&15; --sbc_key_count;
            sbn_strings[-n-1][0]=sbc_key_ascii[i]?sbc_key_ascii[i]:255; sbn_strings[-n-1][1]=sbc_key_ascii[i]?0:sbc_key_scan[i]; sbn_strings[-n-1][2]=0;
        } return n;
    }
    if(fn==30){
        i=a<0?1:sbn_word(a);s=sbn_text(a<0?a:b);t=sbn_text(a<0?b:c);if(i<1)i=1;
        for(j=i-1;s[j];++j){k=0;while(t[k]&&s[j+k]&&t[k]==s[j+k])++k;if(!t[k])return sbn_int(j+1);}return sbn_int(0);
    }
    sbc_set_error("Unknown function"); return sbn_int(0);
}
static const char *sbc_builtins[]={"ABS","SGN","INT","FIX","CINT","SIN","COS","SQR","ATN","RND","TIMER","PEEK","INP","LEN","ASC","CHR$","STR$","VAL","LEFT$","RIGHT$","MID$","SPACE$","STRING$","UCASE$","LCASE$","LTRIM$","RTRIM$","TRIM$","INKEY$","INSTR",0};
static int sbc_primary(void)
{
    int a,b,c,fn,sym,op,n,mark; unsigned off;
    if(sbt_kind==SBT_OP&&(sbt_op=='+'||sbt_op=='-'||sbt_op=='N')) {
        op=sbt_op; sbc_lex_next(); a=sbc_expr(op=='N'?4:8); return sbn_unary(op,a);
    }
    if(sbt_kind==SBT_NUM) {
        fn=(sbt_begin>>1)&63;
        if(sbc_literal_pos[fn]==sbt_begin){a=sbn_new();for(b=0;b<8;++b)sbn_values[a][b]=sbc_literal_value[fn][b];sbn_fast[a]=sbc_literal_fast[fn];sbn_fast_value[a]=sbc_literal_word[fn];sbc_lex_next();return a;}
        if(sbt_text[0]=='&') { n=0; for(a=2;sbt_text[a];++a) n=(n<<4)+(sbt_text[a]<='9'?sbt_text[a]-'0':sbt_text[a]-'A'+10); a=sbn_int(n); }
        else {n=0;b=0;while(sbc_isdigit(sbt_text[b])&&n<3276){n=n*10+sbt_text[b++]-'0';}a=!sbt_text[b]?sbn_int(n):sbn_parse(sbt_text);}
        sbc_literal_pos[fn]=sbt_begin;for(b=0;b<8;++b)sbc_literal_value[fn][b]=sbn_values[a][b];
        sbc_literal_fast[fn]=sbn_fast[a];sbc_literal_word[fn]=sbn_fast_value[a];
        sbc_lex_next(); return a;
    }
    if(sbt_kind==SBT_STR) { a=sbn_string(sbt_text); sbc_lex_next(); return a; }
    if(sbt_kind==SBT_LP) { sbc_lex_next(); a=sbc_expr(0); sbc_expect(SBT_RP); return a; }
    if(sbt_kind!=SBT_ID) { sbc_set_error("Expected expression"); return sbn_int(0); }
    if(sbt_text[0]=='F'&&sbt_text[1]=='N')return sbc_userfn(sbt_text);
    if(sbc_token("VARSEG")||sbc_token("VARPTR")) {
        fn=sbc_token("VARSEG"); sbc_lex_next(); sbc_expect(SBT_LP);
        sym=sbc_find_symbol(sbt_text,1); sbc_lex_next(); off=sbc_subscript(sym); sbc_expect(SBT_RP);
        return sbn_int(fn?sbc_symbols[sym].seg:sbc_symbols[sym].off+off);
    }
    fn=0; while(sbc_builtins[fn]&&!sbc_name_eq(sbt_text,sbc_builtins[fn]))++fn;
    if(sbc_builtins[fn]) {
        ++fn; sbc_lex_next(); a=b=c=32767;
        if(sbt_kind==SBT_LP) {
            sbc_lex_next(); a=sbc_expr(0);
            if(sbt_kind==SBT_COMMA){sbc_lex_next();b=sbc_expr(0);}
            if(sbt_kind==SBT_COMMA){sbc_lex_next();c=sbc_expr(0);}
            sbc_expect(SBT_RP);
        }
        return sbc_builtin(fn,a,b,c);
    }
    sym=sbc_find_symbol(sbt_text,1); sbc_lex_next();
    mark=sbn_top; off=sbc_subscript(sym); sbn_top=mark;
    return sbc_read_value(sym,off);
}
static int sbc_prec(int op)
{
    if(op=='O')return 1; if(op=='X')return 2; if(op=='A')return 3;
    if(op=='='||op=='<'||op=='>'||op=='l'||op=='g'||op=='n')return 4;
    if(op=='+'||op=='-')return 5;
    if(op=='*'||op=='/'||op=='\\'||op=='M')return 6;
    if(op=='^')return 8; return -1;
}
static int sbc_expr(int prec)
{
    int a,b,op,p,mark,v,i;
    a=sbc_primary();
    while(sbc_status!=SB_STATE_ERROR&&sbt_kind==SBT_OP&&(p=sbc_prec(sbt_op))>=prec) {
        op=sbt_op; sbc_lex_next(); mark=sbn_top; b=sbc_expr(p+(op!='^'));
        if(p==4) { v=sbn_cmp(a,b); v=op=='='?v==0:op=='<'?v<0:op=='>'?v>0:op=='l'?v<=0:op=='g'?v>=0:v!=0;
            if(a<0)a=sbn_int(0); sbn_set_int(a,v?-1:0); }
        else if(op=='^') { v=sbn_word(b); b=sbn_copy(a); sbn_set_int(a,1); for(i=0;i<v;++i)sbn_math('*',a,b); }
        else a=sbn_math(op,a,b);
        if(a>=0&&a<mark)sbn_top=mark;
    }
    return a;
}
static int sbc_expr_or(void) { return sbc_expr(0); }
