/* Cooperative statement execution over the indexed source. */
static unsigned sbv_here;
static char sbv_name[16];
static int sbv_args[12];
static int sbv_arg_ref[12];
static unsigned sbv_arg_off[12];
static unsigned sbv_regs[10];
static int sbv_select[16],sbv_nselect;
static unsigned sbv_select_end[16];
static int sbv_input_sym=-1;
static char sbv_input[256];
static int sbv_input_len;
static void sbv_put_char(int c)
{
    if(c=='\r')return;
    if(c=='\n'){sbc_cursor_col=1;if(sbc_cursor_row<25)++sbc_cursor_row;}
    else {sbc_h_text_put(sbc_cursor_row,sbc_cursor_col,c,sbc_fg,sbc_bg);
        if(++sbc_cursor_col>80){sbc_cursor_col=1;if(sbc_cursor_row<25)++sbc_cursor_row;}}
    sbc_h_text_cursor(sbc_cursor_row,sbc_cursor_col);
}
static void sbv_puts(const char *s) { while(*s)sbv_put_char((unsigned char)*s++); }
static void sbv_print(void)
{
    int n,last,format,width,length,p;char *f,*s;
    last=0;format=0;sbc_lex_next();
    if(sbc_token("USING")){sbc_lex_next();format=sbc_expr(0);if(format>=0){sbc_set_error("Expected format string");return;}sbc_expect(SBT_SEMI);}
    while(sbt_kind!=SBT_EOF&&sbc_status!=SB_STATE_ERROR){
        if(sbt_kind==SBT_SEMI||sbt_kind==SBT_COMMA){last=1;if(sbt_kind==SBT_COMMA)do{sbv_put_char(' ');}while((sbc_cursor_col-1)%14);sbc_lex_next();continue;}
        n=sbc_expr(0);
        if(format){
            f=sbn_text(format);p=0;while(f[p]&&f[p]!='#')sbv_put_char(f[p++]);
            width=0;while(f[p]=='#'){++width;++p;}
            if(!width){sbc_set_error("Unsupported PRINT USING format");return;}
            if(n>=0)sbn_unary(9,n);s=sbn_text(n);length=0;while(s[length])++length;
            if(length>width)sbv_put_char('%');while(length++<width)sbv_put_char(' ');sbv_puts(s);
            while(f[p]){if(f[p]=='#'){sbc_set_error("Unsupported PRINT USING format");return;}sbv_put_char(f[p++]);}
        }else{if(n>=0&&!(sbn_values[n][7]&128))sbv_put_char(' ');sbv_puts(sbn_text(n));}last=0;
    }
    if(!last)sbv_put_char('\n');
}
static void sbv_assignment(void)
{
    int sym,v,mark;unsigned off;
    if(sbt_kind!=SBT_ID){sbc_set_error("Expected variable");return;}
    sym=sbc_find_symbol(sbt_text,1);sbc_lex_next();mark=sbn_top;off=sbc_subscript(sym);sbn_top=mark;
    if(sbt_kind!=SBT_OP||sbt_op!='='){sbc_set_error("Expected =");return;}
    sbc_lex_next();v=sbc_expr(0);sbc_write_value(sym,off,v);
}
static int sbv_arg(void) { return sbn_word(sbc_expr(0)); }
static int sbv_comma(void) { if(sbt_kind!=SBT_COMMA)return 0;sbc_lex_next();return 1; }
static void sbv_coords(int slot)
{
    sbc_expect(SBT_LP);sbv_args[slot]=sbv_arg();sbv_comma();sbv_args[slot+1]=sbv_arg();sbc_expect(SBT_RP);
}
#include "sbgfx.c"
static void sbv_line(void)
{
    int c,box,y,t;
    sbc_lex_next();sbv_coords(0);
    if(sbt_kind==SBT_OP&&sbt_op=='-')sbc_lex_next();
    sbv_coords(2);c=sbc_ink;box=0;if(sbv_comma())c=sbv_arg();
    if(sbv_comma()){box=sbc_token("BF")?2:sbc_token("B")?1:0;}
    if(box){
        if(sbv_args[0]>sbv_args[2]){t=sbv_args[0];sbv_args[0]=sbv_args[2];sbv_args[2]=t;}
        if(sbv_args[1]>sbv_args[3]){t=sbv_args[1];sbv_args[1]=sbv_args[3];sbv_args[3]=t;}
        if(box==2){for(y=sbv_args[1];y<=sbv_args[3];++y)sbc_h_line(sbv_args[0],y,sbv_args[2],y,c);}
        else {sbc_h_line(sbv_args[0],sbv_args[1],sbv_args[2],sbv_args[1],c);sbc_h_line(sbv_args[0],sbv_args[3],sbv_args[2],sbv_args[3],c);
            sbc_h_line(sbv_args[0],sbv_args[1],sbv_args[0],sbv_args[3],c);sbc_h_line(sbv_args[2],sbv_args[1],sbv_args[2],sbv_args[3],c);}
    }else sbc_h_line(sbv_args[0],sbv_args[1],sbv_args[2],sbv_args[3],c);
}
static unsigned sbv_find(const char *name,int procedure)
{
    unsigned i,p,e,bucket,entry;int n;
    bucket=procedure;for(n=0;name[n];++n)bucket=(bucket<<1)^name[n];bucket&=31;
    entry=sbv_find_cache[bucket];i=entry&4095;
    if((entry>>12)==procedure&&i<sbc_code_count){
        p=sbc_rec(i,0);e=sbc_rec(i,1);if(procedure)p=sbp_space(p+3,e);
        n=0;while(name[n]&&sbc_up(sbc_ch(p+n))==name[n])++n;
        if(!name[n]&&!sbc_isname(sbc_ch(p+n))&&(procedure||sbp_space(p+n,e)==e))return i;
    }
    for(i=0;i<sbc_code_count;++i){
        p=sbc_rec(i,0);e=sbc_rec(i,1);
        if(procedure){if(!sbp_word(p,procedure==2?"DEF":"SUB"))continue;p=sbp_space(p+3,e);}
        n=0;while(name[n]&&sbc_up(sbc_ch(p+n))==name[n])++n;
        if(!name[n]&&!sbc_isname(sbc_ch(p+n))&&(procedure||sbp_space(p+n,e)==e)){sbv_find_cache[bucket]=i|(procedure<<12);return i;}
    }
    sbc_set_error(procedure?"Undefined SUB":"Undefined label");return sbc_code_count;
}
static void sbv_return(void)
{
    int i,sym;unsigned p;
    if(!sbc_ncalls){sbc_set_error("END SUB without CALL");return;}
    --sbc_ncalls;
    for(i=0;i<sbc_calls[sbc_ncalls].bindings;++i){p=sbc_ncalls*72+i*6;sym=sbc_getw(sbc_bind_seg,p);
        sbc_symbols[sym].seg=sbc_getw(sbc_bind_seg,p+2);sbc_symbols[sym].off=sbc_getw(sbc_bind_seg,p+4);}
    sbc_pc=sbc_calls[sbc_ncalls].pc;sbc_scope=sbc_calls[sbc_ncalls].scope;sbc_nfor=sbc_calls[sbc_ncalls].loops;
}
#include "sbcpu.c"
static void sbv_call(void)
{
    unsigned target,params,start,end,ref,p;int n,i,sym,candidate,top,strings;
    sbc_lex_next();
    if(sbc_token("INTERRUPT")){
        sbc_lex_next();i=sbv_arg();if(i==16){i=sbv_regs[1]&255;sbc_gfx_mode=i==3?0:i;sbs_video_mode(i);}return;
    }
    if(sbc_token("ABSOLUTE")){sba_call();return;}
    sbc_copy_name(sbv_name,sbt_text);target=sbv_find(sbv_name,1);sbc_lex_next();n=0;
    if(sbt_kind==SBT_LP){sbc_lex_next();while(sbt_kind!=SBT_RP&&sbt_kind!=SBT_EOF&&n<12){
        start=sbt_begin;candidate=sbt_kind==SBT_ID?sbc_find_symbol(sbt_text,0):-1;
        sbv_args[n]=sbc_expr(0);end=sbt_begin;sbv_arg_ref[n]=-1;
        if(candidate>=0){top=sbn_top;strings=sbn_stop;sbc_parse_pos=start;sbc_lex_next();sbc_lex_next();ref=sbc_subscript(candidate);
            if(sbt_begin==end){sbv_arg_ref[n]=candidate;sbv_arg_off[n]=ref;}
            sbn_top=top;sbn_stop=strings;sbc_parse_pos=end;sbc_lex_next();}
        ++n;if(!sbv_comma())break;}sbc_expect(SBT_RP);}
    if(sbc_status==SB_STATE_ERROR)return;
    if(sbc_ncalls>=SBC_CALLS){sbc_set_error("CALL stack full");return;}
    sbc_calls[sbc_ncalls].pc=sbc_pc;sbc_calls[sbc_ncalls].scope=sbc_scope;sbc_calls[sbc_ncalls].bindings=0;sbc_calls[sbc_ncalls++].loops=sbc_nfor;
    sbc_scope=target+1;sbc_stmt_end=sbc_rec(target,1);params=sbp_space(sbc_rec(target,0)+3,sbc_stmt_end);
    sbc_parse_pos=params;sbc_lex_next();sbc_lex_next();i=0;
    if(sbt_kind==SBT_LP){sbc_lex_next();while(sbt_kind==SBT_ID&&i<n){sym=sbc_find_symbol(sbt_text,1);
        if(sbv_arg_ref[i]>=0&&sbc_symbols[sym].type==sbc_symbols[sbv_arg_ref[i]].type){
            p=(sbc_ncalls-1)*72+sbc_calls[sbc_ncalls-1].bindings*6;
            sbc_putw(sbc_bind_seg,p,sym);sbc_putw(sbc_bind_seg,p+2,sbc_symbols[sym].seg);sbc_putw(sbc_bind_seg,p+4,sbc_symbols[sym].off);++sbc_calls[sbc_ncalls-1].bindings;
            candidate=sbv_arg_ref[i];sbc_symbols[sym].seg=sbc_symbols[candidate].seg;sbc_symbols[sym].off=sbc_symbols[candidate].off+sbv_arg_off[i];
        }else sbc_write_value(sym,0,sbv_args[i]);
        ++i;sbc_lex_next();if(!sbv_comma())break;}}
    sbc_pc=target+1;
}
static void sbv_dim(void)
{
    int sym,a,b,base;unsigned count,bytes,seg;
    sbc_lex_next();
    while(sbt_kind==SBT_ID&&sbc_status!=SB_STATE_ERROR){
        sym=sbc_find_symbol(sbt_text,1);sbc_lex_next();if(sbt_kind!=SBT_LP){if(sbv_comma())continue;break;}sbc_expect(SBT_LP);base=sbc_option_base;
        a=sbv_arg()-base+1;b=1;if(sbv_comma())b=sbv_arg()-base+1;sbc_expect(SBT_RP);
        if(a<1||b<1||(unsigned)a>65535u/(unsigned)b){sbc_set_error("Array too large");return;}
        count=(unsigned)a*b;if(count>64512u/sbc_symbols[sym].type){sbc_set_error("Array too large");return;}
        bytes=count*sbc_symbols[sym].type;seg=sbc_claim(bytes);if(!seg)return;
        sbc_symbols[sym].seg=seg;sbc_symbols[sym].off=0;sbc_symbols[sym].rows=a;sbc_symbols[sym].cols=b;
        if(!sbv_comma())break;
    }
}
static void sbv_read(void)
{
    unsigned savepos,saveend,p,e,off;int sym,v;
    sbc_lex_next();
    while(sbt_kind==SBT_ID&&sbc_status!=SB_STATE_ERROR){
        sym=sbc_find_symbol(sbt_text,1);sbc_lex_next();off=sbc_subscript(sym);savepos=sbt_begin;saveend=sbc_stmt_end;
        while(!sbc_data_pos&&sbc_data_pc<sbc_code_count){p=sbc_rec(sbc_data_pc,0);if(sbp_word(p,"DATA"))sbc_data_pos=p+4;else ++sbc_data_pc;}
        if(sbc_data_pc>=sbc_code_count){sbc_set_error("Out of DATA");return;}
        e=sbc_rec(sbc_data_pc,1);sbc_parse_pos=sbc_data_pos;sbc_stmt_end=e;sbc_lex_next();v=sbc_expr(0);sbc_write_value(sym,off,v);
        if(sbt_kind==SBT_COMMA)sbc_data_pos=sbc_parse_pos;else{sbc_data_pos=0;++sbc_data_pc;}
        sbc_stmt_end=saveend;sbc_parse_pos=savepos;sbc_lex_next();if(!sbv_comma())break;
    }
}
#include "sbextra.c"
static int sbc_exec_one(void)
{
    int a,b,c,i,sym,mark;unsigned off,jump,p;
    if(sbc_pc>=sbc_code_count){sbc_status=SB_STATE_DONE;return 0;}
    sbv_here=sbc_pc++;sbc_stmt_start=sbc_rec(sbv_here,0);sbc_stmt_end=sbc_rec(sbv_here,1);
    jump=sbc_rec(sbv_here,2);sbc_lineno=sbc_rec(sbv_here,3);sbc_parse_pos=sbc_stmt_start;
    if(!sbc_fn_depth)sbn_top=sbn_stop=0;sbc_lex_next();
    if(sbt_kind==SBT_EOF||sbt_kind==SBT_NUM)return 1;
    if(sbc_token("IF")){sbc_lex_next();a=sbc_expr(0);if(!sbn_true(a))sbc_pc=jump;return 1;}
    if(sbc_token("ELSE")){sbc_pc=jump;return 1;}
    if(sbc_token("END")){
        sbc_lex_next();if(sbc_token("SUB"))sbv_return();else if(sbc_token("SELECT")){if(sbe_nselect)--sbe_nselect;}else if(sbt_kind==SBT_EOF)sbc_status=SB_STATE_DONE;
        return 1;
    }
    if(sbc_token("SYSTEM")||sbc_token("STOP")){sbc_status=SB_STATE_DONE;return 0;}
    if(sbc_token("SUB")){sbc_pc=jump;return 1;}
    if(sbc_token("CALL")){sbv_call();return 1;}
    if(sbc_token("SHARED")){
        sbc_lex_next();while(sbt_kind==SBT_ID){sbc_copy_name(sbv_name,sbt_text);a=sbc_scope;sbc_scope=0;sym=sbc_find_symbol(sbv_name,1);sbc_scope=a;
            b=sbc_find_symbol(sbv_name,1);sbc_symbols[b].seg=sbc_symbols[sym].seg;sbc_symbols[b].off=sbc_symbols[sym].off;sbc_symbols[b].type=sbc_symbols[sym].type;
            sbc_symbols[b].rows=sbc_symbols[sym].rows;sbc_symbols[b].cols=sbc_symbols[sym].cols;
            sbc_lex_next();if(sbt_kind==SBT_LP){sbc_lex_next();sbc_expect(SBT_RP);}if(!sbv_comma())break;}return 1;
    }
    if(sbc_token("PRINT")||sbc_token("LPRINT")||sbc_token("WRITE")){sbv_print();return 1;}
    if(sbc_token("LET")){sbc_lex_next();sbv_assignment();return 1;}
    if(sbc_token("DIM")||sbc_token("REDIM")){sbv_dim();return 1;}
    if(sbc_token("READ")){sbv_read();return 1;}
    if(sbc_token("DATA")||sbc_token("REM"))return 1;
    if(sbc_token("RESTORE")){sbc_data_pc=sbc_data_pos=0;sbc_lex_next();if(sbt_kind==SBT_ID||sbt_kind==SBT_NUM){sbc_copy_name(sbv_name,sbt_text);sbc_data_pc=sbv_find(sbv_name,0)+1;}return 1;}
    if(sbc_token("OPTION")){sbc_lex_next();sbc_lex_next();sbc_option_base=sbv_arg();return 1;}
    if(sbc_token("DEFINT")){sbc_default_int=1;return 1;}
    if(sbc_token("DEFSNG")||sbc_token("DEFDBL")||sbc_token("DEFLNG")){sbc_default_int=0;return 1;}
    if(sbc_token("DEF")){sbc_lex_next();if(sbc_token("SEG")){sbc_lex_next();sbc_defseg=0;if(sbt_kind==SBT_OP&&sbt_op=='='){sbc_lex_next();sbc_defseg=sbv_arg();}}else if(jump!=65535u)sbc_pc=jump;return 1;}
    if(sbc_token("POKE")){sbc_lex_next();a=sbv_arg();sbv_comma();b=sbv_arg();sbs_mem_write(sbc_defseg,(unsigned)a,b);return 1;}
    if(sbc_token("OUT")){sbc_lex_next();a=sbv_arg();sbv_comma();b=sbv_arg();sbs_out(a,b);return 1;}
    if(sbc_token("REG")){sbc_lex_next();a=sbv_arg();sbv_comma();b=sbv_arg();if(a>=0&&a<10)sbv_regs[a]=b;return 1;}
    if(sbc_token("CLS")){sbc_h_clear(0);sbc_cursor_row=sbc_cursor_col=1;return 1;}
    if(sbc_token("SCREEN")){
        sbc_lex_next();a=sbv_arg();sbc_gfx_mode=a;sbc_ink=a==1?3:a==2?1:15;
        sbs_video_mode(a==0?3:a);return 1;
    }
    if(sbc_token("COLOR")){sbc_lex_next();sbc_fg=sbv_arg();if(sbv_comma())sbc_bg=sbv_arg();return 1;}
    if(sbc_token("LOCATE")){sbc_lex_next();sbc_cursor_row=sbv_arg();if(sbv_comma())sbc_cursor_col=sbv_arg();return 1;}
    if(sbc_token("WIDTH"))return 1;
    if(sbc_token("PSET")||sbc_token("PRESET")){a=sbc_token("PRESET")?0:sbc_ink;sbc_lex_next();sbv_coords(0);if(sbv_comma())a=sbv_arg();sbc_h_pixel(sbv_args[0],sbv_args[1],a);return 1;}
    if(sbc_token("LINE")){if(sbp_word(sbp_space(sbc_parse_pos,sbc_stmt_end),"INPUT"))sbe_input();else sbv_line();return 1;}
    if(sbc_token("INPUT")){sbe_input();return 1;}
    if(sbc_token("SELECT")){sbe_select();return 1;}
    if(sbc_token("CASE")){sbe_case();return 1;}
    if(sbc_token("CIRCLE")){sbg_circle();return 1;}
    if(sbc_token("PAINT")){sbg_paint();return 1;}
    if(sbc_token("GET")){sbg_image(0);return 1;}
    if(sbc_token("PUT")){sbg_image(1);return 1;}
    if(sbc_token("GOTO")||sbc_token("GOSUB")){
        a=sbc_token("GOSUB");sbc_lex_next();sbc_copy_name(sbv_name,sbt_text);
        if(a){if(sbc_ngosub==SBC_CALLS){sbc_set_error("GOSUB stack full");return 0;}sbc_gosub[sbc_ngosub++]=sbc_pc;}
        sbc_pc=sbv_find(sbv_name,0)+1;return 1;
    }
    if(sbc_token("RETURN")){if(!sbc_ngosub)sbc_set_error("RETURN without GOSUB");else sbc_pc=sbc_gosub[--sbc_ngosub];return 1;}
    if(sbc_token("FOR")){
        if(sbc_nfor==SBC_FOR){sbc_set_error("FOR stack full");return 0;}
        sbc_lex_next();sym=sbc_find_symbol(sbt_text,1);sbc_lex_next();sbc_lex_next();a=sbc_expr(0);sbc_write_value(sym,0,a);
        if(!sbc_token("TO")){sbc_set_error("FOR without TO");return 0;}
        sbc_lex_next();b=sbc_expr(0);c=sbn_int(1);if(sbc_token("STEP")){sbc_lex_next();c=sbc_expr(0);}
        if(!sbn_true(c)){sbc_set_error("FOR step is zero");return 0;}
        if((sbn_values[c][7]&128)?sbn_cmp(a,b)<0:sbn_cmp(a,b)>0){sbc_pc=jump;return 1;}
        sbc_for[sbc_nfor].sym=sym;sbc_for[sbc_nfor].body=sbc_pc;
        for(i=0;i<8;++i){sbc_for[sbc_nfor].limit[i]=sbn_values[b][i];sbc_for[sbc_nfor].step[i]=sbn_values[c][i];}
        sbc_for[sbc_nfor].lf=sbn_fast[b];sbc_for[sbc_nfor].lv=sbn_fast_value[b];sbc_for[sbc_nfor].sf=sbn_fast[c];sbc_for[sbc_nfor].sv=sbn_fast_value[c];
        ++sbc_nfor;return 1;
    }
    if(sbc_token("NEXT")){
        if(!sbc_nfor){sbc_set_error("NEXT without FOR");return 0;}i=sbc_nfor-1;sym=sbc_for[i].sym;
        a=sbc_read_value(sym,0);b=sbn_new();c=sbn_new();for(mark=0;mark<8;++mark){sbn_values[b][mark]=sbc_for[i].limit[mark];sbn_values[c][mark]=sbc_for[i].step[mark];}
        sbn_fast[b]=sbc_for[i].lf;sbn_fast_value[b]=sbc_for[i].lv;sbn_fast[c]=sbc_for[i].sf;sbn_fast_value[c]=sbc_for[i].sv;
        sbn_math('+',a,c);sbc_write_value(sym,0,a);
        if((sbn_values[c][7]&128)?sbn_cmp(a,b)>=0:sbn_cmp(a,b)<=0)sbc_pc=sbc_for[i].body;else --sbc_nfor;return 1;
    }
    if(sbc_token("DO")||sbc_token("WHILE")){
        a=sbc_token("WHILE");sbc_lex_next();b=sbc_token("UNTIL");
        if(a||b||sbc_token("WHILE")){if(!a)sbc_lex_next();c=sbn_true(sbc_expr(0));if(b?c:!c)sbc_pc=jump;}return 1;
    }
    if(sbc_token("LOOP")||sbc_token("WEND")){
        sbc_lex_next();a=sbc_token("UNTIL");b=sbc_token("WHILE");c=1;if(a||b){sbc_lex_next();c=sbn_true(sbc_expr(0));if(a)c=!c;}if(c)sbc_pc=jump;return 1;
    }
    if(sbc_token("EXIT")){
        sbc_lex_next();if(sbc_token("SUB")){sbv_return();return 1;}
        a=sbc_token("FOR");if(a&&sbc_nfor)--sbc_nfor;
        /* The enclosing loop is the closest preceding opener whose end
         * still lies beyond this statement. */
        p=sbv_here;while(p){--p;off=sbc_rec(p,0);if((a?sbp_word(off,"FOR"):sbp_word(off,"DO")||sbp_word(off,"WHILE"))&&sbc_rec(p,2)>sbv_here&&sbc_rec(p,2)!=65535u){sbc_pc=sbc_rec(p,2);return 1;}}
        sbc_set_error("EXIT without loop");return 1;
    }
    if(sbc_token("DELAY")||sbc_token("SLEEP")){
        ++sbc_frames;
        sbc_lex_next();a=sbc_expr(0);b=sbn_parse("18.2");sbn_math('*',a,b);i=sbn_word(a);if(i<1)i=1;
        sbc_wait_until=sbc_h_ticks()+i;sbc_wait_time=1;sbc_status=SB_STATE_WAITING;return 1;
    }
    if(sbc_token("WAIT")){sbc_wait_until=sbc_h_ticks()+1;sbc_wait_time=1;sbc_status=SB_STATE_WAITING;return 1;}
    if(sbc_token("RANDOMIZE")){sbc_rand=sbc_h_ticks();return 1;}
    if(sbc_token("BEEP")){sbc_h_sound(800,4);return 1;}
    if(sbc_token("SOUND")){sbc_lex_next();a=sbv_arg();b=1;if(sbv_comma())b=sbv_arg();sbc_h_sound(a,b);return 1;}
    if(sbc_token("PLAY")){sbe_play();return 1;}
    /* An identifier alone followed by ':' is an indexed label. */
    p=sbc_parse_pos;if(sbt_kind==SBT_ID&&sbp_space(p,sbc_stmt_end)==sbc_stmt_end&&sbc_ch(sbc_stmt_end)==':')return 1;
    sbv_assignment();return 1;
}
int sb_run_slice(int budget)
{
    int n;unsigned started;
    started=sbc_h_ticks();
    if(budget<=0)budget=SB_SLICE_OPS;
    if(sbc_status==SB_STATE_READY)sbc_status=SB_STATE_RUNNING;
    if(sbc_status==SB_STATE_WAITING&&sbv_input_sym>=0)sbe_input_poll();
    if(sbc_status==SB_STATE_WAITING&&sbc_wait_time&&(int)(sbc_h_ticks()-sbc_wait_until)>=0){sbc_wait_time=0;sbc_status=SB_STATE_RUNNING;if(sbe_music[sbe_music_pos])sbe_note();}
    for(n=0;n<budget&&sbc_status==SB_STATE_RUNNING;++n){
        if(!sbc_exec_one())break;
        /* Bound wall time on a real XT without leaving a fast CPU idle
         * after a tiny fixed number of BASIC statements. */
        if((n&31)==31&&sbc_h_ticks()!=started)break;
    }
    if(n)sbc_notify();return sbc_status;
}
