/* Stateful language features used by the text tools as well as the demos. */
static unsigned sbe_select_pc[16];
static int sbe_select_done[16],sbe_nselect;
static void sbe_select(void)
{
    if(sbe_nselect==16){sbc_set_error("SELECT stack full");return;}
    sbe_select_pc[sbe_nselect]=sbv_here;sbe_select_done[sbe_nselect++]=0;
}
static void sbe_case(void)
{
    unsigned save,end,p,start;int a,b,c,match;
    if(!sbe_nselect){sbc_set_error("CASE without SELECT");return;}
    start=sbe_select_pc[sbe_nselect-1];end=sbc_rec(start,2);
    if(sbe_select_done[sbe_nselect-1]){sbc_pc=end-1;return;}
    sbc_lex_next();match=sbc_token("ELSE");
    if(!match){
        save=sbt_begin;p=sbc_stmt_end;
        sbc_parse_pos=sbc_rec(start,0);sbc_stmt_end=sbc_rec(start,1);sbc_lex_next();sbc_lex_next();sbc_lex_next();a=sbc_expr(0);
        sbc_parse_pos=save;sbc_stmt_end=p;sbc_lex_next();
        do{b=sbc_expr(0);if(sbc_token("TO")){sbc_lex_next();c=sbc_expr(0);if(sbn_cmp(a,b)>=0&&sbn_cmp(a,c)<=0)match=1;}
            else if(sbn_cmp(a,b)==0)match=1;
        }while(sbv_comma()&&sbc_status!=SB_STATE_ERROR);
    }
    if(match){sbe_select_done[sbe_nselect-1]=1;return;}
    p=sbc_pc;while(p<end-1){save=sbc_rec(p,0);if(sbp_word(save,"CASE"))break;if(sbp_word(save,"SELECT"))p=sbc_rec(p,2);else ++p;}sbc_pc=p;
}
static void sbe_input(void)
{
    sbc_lex_next();if(sbc_token("INPUT"))sbc_lex_next();
    if(sbt_kind==SBT_STR){sbv_puts(sbt_text);sbc_lex_next();if(sbt_kind==SBT_SEMI||sbt_kind==SBT_COMMA)sbc_lex_next();}
    if(sbt_kind!=SBT_ID){sbc_set_error("INPUT needs a variable");return;}
    sbv_input_sym=sbc_find_symbol(sbt_text,1);sbv_input_len=0;sbv_input[0]=0;sbc_status=SB_STATE_WAITING;sbc_wait_time=0;
}
static void sbe_input_poll(void)
{
    int c,n;
    while(sbc_key_count&&sbv_input_sym>=0){n=sbc_key_head;sbc_key_head=(n+1)&15;--sbc_key_count;c=sbc_key_ascii[n];
        if(c==13){sbn_top=sbn_stop=0;n=sbc_symbols[sbv_input_sym].type==256?sbn_string(sbv_input):sbn_parse(sbv_input);sbc_write_value(sbv_input_sym,0,n);sbv_input_sym=-1;sbc_status=SB_STATE_RUNNING;sbv_put_char('\n');}
        else if(c==8&&sbv_input_len){sbv_input[--sbv_input_len]=0;if(sbc_cursor_col>1)--sbc_cursor_col;sbc_h_text_put(sbc_cursor_row,sbc_cursor_col,' ',sbc_fg,sbc_bg);}
        else if(c>=32&&c<127&&sbv_input_len<255){sbv_input[sbv_input_len++]=c;sbv_input[sbv_input_len]=0;sbv_put_char(c);}
    }
}
static int sbc_fn_depth;
static int sbc_fn_args[8][12];
static int sbc_userfn(const char *name)
{
    unsigned target,savepc,saveend,savepos,p;int scope,depth,n,i,sym,result,top,strings,lineno;
    target=sbv_find(name,2);if(sbc_status==SB_STATE_ERROR)return sbn_int(0);
    depth=sbc_fn_depth++;if(depth>=8){sbc_set_error("Function nesting too deep");--sbc_fn_depth;return sbn_int(0);}
    sbc_lex_next();n=0;
    if(sbt_kind==SBT_LP){sbc_lex_next();while(sbt_kind!=SBT_RP&&sbt_kind!=SBT_EOF&&n<12){sbc_fn_args[depth][n++]=sbc_expr(0);if(!sbv_comma())break;}sbc_expect(SBT_RP);}
    savepc=sbc_pc;saveend=sbc_stmt_end;savepos=sbt_begin;scope=sbc_scope;lineno=sbc_lineno;top=sbn_top;strings=sbn_stop;
    sbc_scope=target+1;sbc_stmt_end=sbc_rec(target,1);sbc_parse_pos=sbc_rec(target,0);sbc_lex_next();sbc_lex_next();
    sym=sbc_find_symbol(sbt_text,1);result=sym;sbc_lex_next();i=0;
    if(sbt_kind==SBT_LP){sbc_lex_next();while(sbt_kind==SBT_ID&&i<n){sym=sbc_find_symbol(sbt_text,1);sbc_write_value(sym,0,sbc_fn_args[depth][i++]);sbc_lex_next();if(!sbv_comma())break;}sbc_expect(SBT_RP);}
    if(sbt_kind==SBT_OP&&sbt_op=='='){sbc_lex_next();i=sbc_expr(0);sbc_write_value(result,0,i);}
    else{p=sbc_rec(target,2);sbc_pc=target+1;while(sbc_pc<p-1&&sbc_status!=SB_STATE_ERROR)sbc_exec_one();}
    sbn_top=top;sbn_stop=strings;i=sbc_read_value(result,0);
    sbc_pc=savepc;sbc_scope=scope;sbc_lineno=lineno;sbc_stmt_end=saveend;sbc_parse_pos=savepos;sbc_lex_next();--sbc_fn_depth;return i;
}
static char sbe_music[128];
static int sbe_music_pos,sbe_octave,sbe_tempo,sbe_length;
static int sbe_music_number(void)
{
    int n;n=0;while(sbc_isdigit(sbe_music[sbe_music_pos]))n=n*10+sbe_music[sbe_music_pos++]-'0';return n;
}
static void sbe_note(void)
{
    static int frequency[12]={262,277,294,311,330,349,370,392,415,440,466,494};
    int c,n,len,hz,ticks;
    while((c=sbc_up(sbe_music[sbe_music_pos]))!=0){
        ++sbe_music_pos;
        if(c=='T'){sbe_tempo=sbe_music_number();continue;}if(c=='O'){sbe_octave=sbe_music_number();continue;}if(c=='L'){sbe_length=sbe_music_number();continue;}
        if(c=='>'){++sbe_octave;continue;}if(c=='<'){--sbe_octave;continue;}
        if(c!='P'&&(c<'A'||c>'G'))continue;
        n=c=='C'?0:c=='D'?2:c=='E'?4:c=='F'?5:c=='G'?7:c=='A'?9:11;
        if(sbe_music[sbe_music_pos]=='+'||sbe_music[sbe_music_pos]=='#'){++n;++sbe_music_pos;}
        else if(sbe_music[sbe_music_pos]=='-'){--n;++sbe_music_pos;}
        len=sbe_music_number();if(!len)len=sbe_length;if(len<1)len=4;if(sbe_tempo<32)sbe_tempo=120;
        ticks=4368/(sbe_tempo*len);if(sbe_music[sbe_music_pos]=='.'){ticks=ticks*3/2;++sbe_music_pos;}if(ticks<1)ticks=1;
        hz=frequency[(n+12)%12];if(sbe_octave<0)sbe_octave=0;if(sbe_octave>6)sbe_octave=6;
        if(sbe_octave<4)hz>>=4-sbe_octave;else hz<<=sbe_octave-4;
        if(c!='P')sbc_h_sound(hz,ticks);
        sbc_wait_until=sbc_h_ticks()+ticks;sbc_wait_time=1;sbc_status=SB_STATE_WAITING;return;
    }
}
static void sbe_play(void)
{
    int n,i;char *s;
    sbc_lex_next();n=sbc_expr(0);s=sbn_text(n);i=0;
    while(s[i]&&i<127){sbe_music[i]=s[i];++i;}sbe_music[i]=0;sbe_music_pos=0;sbe_octave=4;sbe_tempo=120;sbe_length=4;sbe_note();
}
static void sbe_reset(void)
{
    sbv_input_sym=-1;sbv_input_len=0;sbe_nselect=0;sbc_fn_depth=0;sbe_music[0]=0;sbe_music_pos=0;
}
