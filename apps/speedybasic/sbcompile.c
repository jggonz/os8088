/* Index source statements and pair structured branches once, before running.
 * Records are eight bytes in a far claim: begin, end, jump, physical line. */
static unsigned sbp_open[64],sbp_chain[64];
static int sbp_kind[64],sbp_inline[64],sbp_depth;
static int sbp_word(unsigned p,const char *s)
{
    while(*s&&sbc_up(sbc_ch(p))==*s){++p;++s;}
    return !*s&&!sbc_isname(sbc_ch(p));
}
static unsigned sbp_space(unsigned p,unsigned e)
{while(p<e&&(sbc_ch(p)==' '||sbc_ch(p)=='\t'))++p;return p;}
static unsigned sbp_emit(unsigned b,unsigned e,int line)
{
    unsigned n;
    n=sbc_code_count;
    if(n>=SBC_MAX_STMTS){sbc_set_error("Too many statements");return 0;}
    sbc_putw(sbc_code_seg,n*8,b);sbc_putw(sbc_code_seg,n*8+2,e);
    sbc_putw(sbc_code_seg,n*8+4,65535u);sbc_putw(sbc_code_seg,n*8+6,line);
    ++sbc_code_count;return n;
}
static void sbp_push(int kind,unsigned n,int inl)
{
    if(sbp_depth>=64){sbc_set_error("Blocks nested too deeply");return;}
    sbp_kind[sbp_depth]=kind;sbp_open[sbp_depth]=n;sbp_chain[sbp_depth]=65535u;sbp_inline[sbp_depth++]=inl;
}
static void sbp_close_if(void)
{
    unsigned p,q;
    --sbp_depth;if(sbp_open[sbp_depth]!=65535u)sbc_link(sbp_open[sbp_depth],sbc_code_count);
    p=sbp_chain[sbp_depth];
    while(p!=65535u){q=sbc_rec(p,2);sbc_link(p,sbc_code_count);p=q;}
}
static int sbc_index_program(void)
{
    unsigned p,b,e,q,n,cond,tail;
    int line,quote,kind,inl,d;
    sbp_depth=0;p=0;line=1;
    while(p<sbc_source_len&&sbc_status!=SB_STATE_ERROR){
        sbc_lineno=line;
        b=sbp_space(p,sbc_source_len);p=b;
        if(sbc_ch(p)=='\r'){++p;continue;}
        if(sbc_ch(p)=='\n'){
            while(sbp_depth&&sbp_inline[sbp_depth-1])sbp_close_if();
            ++line;++p;continue;
        }
        if(sbc_ch(p)=='\''||sbp_word(p,"REM")){
            while(p<sbc_source_len&&sbc_ch(p)!='\n')++p;continue;
        }
        /* A physical line number is retained as a label record. */
        if(sbc_isdigit(sbc_ch(p))){
            while(sbc_isdigit(sbc_ch(p)))++p;
            if(p<sbc_source_len&&(sbc_ch(p)==' '||sbc_ch(p)=='\t')){
                sbp_emit(b,p,line);continue;
            }
        }
        p=b;quote=0;cond=65535u;
        if(sbp_word(b,"ELSE"))p=b+4;
        while(p<sbc_source_len){
            if(sbp_word(b,"ELSE"))break;
            kind=sbc_ch(p);
            if(kind=='"')quote=!quote;
            if(!quote&&(kind==':'||kind=='\n'||kind=='\r'||kind=='\''))break;
            if(!quote&&p>b&&!sbp_word(b,"CASE")&&!sbc_isname(sbc_ch(p-1))&&sbp_word(p,"ELSE"))break;
            if(!quote&&(sbp_word(b,"IF")||sbp_word(b,"ELSEIF"))&&sbp_word(p,"THEN")){cond=p;p+=4;break;}
            ++p;
        }
        e=p;tail=sbp_space(p,sbc_source_len);
        n=sbp_emit(b,e,line);
        if(sbp_word(b,"IF")){
            if(cond==65535u){sbc_set_error("IF without THEN");break;}
            inl=sbc_ch(tail)!='\n'&&sbc_ch(tail)!='\r'&&sbc_ch(tail)!='\''&&tail<sbc_source_len;
            sbp_push(1,n,inl);
        }else if(sbp_word(b,"ELSE")||sbp_word(b,"ELSEIF")){
            if(!sbp_depth||sbp_kind[sbp_depth-1]!=1){sbc_set_error("ELSE without IF");break;}
            d=sbp_depth-1;sbc_link(sbp_open[d],n+1);
            sbc_link(n,sbp_chain[d]);sbp_chain[d]=n;
            /* ELSEIF supplies a second IF record, with its own false edge. */
            if(sbp_word(b,"ELSEIF")){
                sbc_putw(sbc_code_seg,n*8+2,b+4);
                q=sbp_emit(b+4,e,line);sbp_open[d]=q;
            }else{
                sbp_open[d]=65535u;
            }
        }else if(sbp_word(b,"END")&&sbp_word(sbp_space(b+3,e),"IF")){
            if(!sbp_depth||sbp_kind[sbp_depth-1]!=1){sbc_set_error("END IF without IF");break;}
            sbp_close_if();
        }else if(sbp_word(b,"FOR"))sbp_push(3,n,0);
        else if(sbp_word(b,"DO")||sbp_word(b,"WHILE"))sbp_push(2,n,0);
        else if(sbp_word(b,"SUB"))sbp_push(4,n,0);
        else if(sbp_word(b,"DEF")&&!sbp_word(sbp_space(b+3,e),"SEG")){
            q=b;while(q<e&&sbc_ch(q)!='=')++q;if(q==e)sbp_push(4,n,0);
        }
        else if(sbp_word(b,"SELECT"))sbp_push(5,n,0);
        else if(sbp_word(b,"NEXT")||sbp_word(b,"LOOP")||sbp_word(b,"WEND")||
            (sbp_word(b,"END")&&(sbp_word(sbp_space(b+3,e),"SUB")||sbp_word(sbp_space(b+3,e),"DEF")||sbp_word(sbp_space(b+3,e),"SELECT")))){
            if(!sbp_depth){sbc_set_error("Unmatched block end");break;}
            --sbp_depth;sbc_link(n,sbp_open[sbp_depth]);sbc_link(sbp_open[sbp_depth],n+1);
        }
        if(p==b){ /* ELSE starts a fresh statement at this byte. */
            p+=4;sbc_putw(sbc_code_seg,n*8+2,p);
        }
        if(sbc_ch(p)==':')++p;
    }
    while(sbp_depth&&sbp_inline[sbp_depth-1])sbp_close_if();
    if(sbp_depth&&sbc_status!=SB_STATE_ERROR)sbc_set_error("Unclosed BASIC block");
    return sbc_status==SB_STATE_ERROR?-1:0;
}
