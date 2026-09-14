/* Bounded 8086 execution for CALL ABSOLUTE. Guest segment registers address
 * virtual BASIC memory and video buffers, never the desktop's real hardware.
 * Implements the ordinary MOV/stack/string instructions used by the shipped
 * assembly examples, rather than recognizing a particular blitter's bytes. */
static unsigned sba_reg[8],sba_ds,sba_es,sba_ip,sba_code;
static unsigned char sba_stack[256];
static int sba_args[12],sba_argc,sba_dir;
static int sba_fetch(void){return sbs_mem_read(sba_code,sba_ip++);}
static unsigned sba_fetchw(void){unsigned v;v=sba_fetch();return v|(sba_fetch()<<8);}
static void sba_push(unsigned v){sba_reg[4]-=2;sba_stack[sba_reg[4]&255]=v;sba_stack[(sba_reg[4]+1)&255]=v>>8;}
static unsigned sba_pop(void){unsigned v;v=sba_stack[sba_reg[4]&255]|(sba_stack[(sba_reg[4]+1)&255]<<8);sba_reg[4]+=2;return v;}
static unsigned sba_read(unsigned off,int stack)
{
    if(stack)return sba_stack[off&255]|(sba_stack[(off+1)&255]<<8);
    if(off>=0xf000&&off<0xf000+sba_argc*2)return sbn_word(sba_args[(off-0xf000)/2]);
    return sbs_mem_read(sba_ds,off)|(sbs_mem_read(sba_ds,off+1)<<8);
}
static void sba_run(unsigned code,unsigned entry)
{
    int op,mod,reg,rm,mode,stack,guard,repeat,i;unsigned off,v;
    sba_code=code;sba_ip=entry;sba_ds=code;sba_es=code;sba_dir=1;
    for(i=0;i<8;++i)sba_reg[i]=0;sba_reg[4]=250;
    for(i=0;i<sba_argc;++i)sba_push(0xf000+i*2);sba_push(65535u);
    for(guard=0;guard<4096&&sbc_status!=SB_STATE_ERROR;++guard){
        op=sba_fetch();repeat=0;if(op==0xf3){repeat=1;op=sba_fetch();}
        if(op>=0x50&&op<=0x57){sba_push(sba_reg[op-0x50]);continue;}
        if(op>=0x58&&op<=0x5f){sba_reg[op-0x58]=sba_pop();continue;}
        if(op>=0xb8&&op<=0xbf){sba_reg[op-0xb8]=sba_fetchw();continue;}
        if(op==0x90)continue;
        if(op==0xfc){sba_dir=1;continue;}if(op==0xfd){sba_dir=-1;continue;}
        if(op==0xc3||op==0xc2)return;
        if(op==0xaa||op==0xab){
            i=repeat?sba_reg[1]:1;if(i<0){sbc_set_error("CALL ABSOLUTE repeat too large");return;}
            while(i--){sbs_mem_write(sba_es,sba_reg[7],sba_reg[0]&255);sba_reg[7]+=sba_dir;
                if(op==0xab){sbs_mem_write(sba_es,sba_reg[7],sba_reg[0]>>8);sba_reg[7]+=sba_dir;}}
            if(repeat)sba_reg[1]=0;continue;
        }
        if(op==0x8b||op==0x89||op==0x8e){
            mod=sba_fetch();reg=(mod>>3)&7;rm=mod&7;mode=mod>>6;stack=0;off=0;
            if(mode!=3){
                if(rm==0)off=sba_reg[3]+sba_reg[6];else if(rm==1)off=sba_reg[3]+sba_reg[7];
                else if(rm==2){off=sba_reg[5]+sba_reg[6];stack=1;}else if(rm==3){off=sba_reg[5]+sba_reg[7];stack=1;}
                else if(rm==4)off=sba_reg[6];else if(rm==5)off=sba_reg[7];else if(rm==6){if(!mode)off=sba_fetchw();else{off=sba_reg[5];stack=1;}}
                else off=sba_reg[3];
                if(mode==1){i=sba_fetch();off+=i>=128?i-256:i;}else if(mode==2)off+=sba_fetchw();
            }
            if(op==0x89){v=sba_reg[reg];if(mode==3)sba_reg[rm]=v;else if(stack){sba_stack[off&255]=v;sba_stack[(off+1)&255]=v>>8;}
                else{sbs_mem_write(sba_ds,off,v);sbs_mem_write(sba_ds,off+1,v>>8);}}
            else{v=mode==3?sba_reg[rm]:sba_read(off,stack);if(op==0x8e){if(reg==0)sba_es=v;else if(reg==3)sba_ds=v;else{sbc_set_error("Unsupported guest segment");return;}}
                else sba_reg[reg]=v;}
            continue;
        }
        sbc_set_error("Unsupported CALL ABSOLUTE opcode");return;
    }
    if(sbc_status!=SB_STATE_ERROR)sbc_set_error("CALL ABSOLUTE instruction limit");
}
static void sba_call(void)
{
    unsigned entry;
    sbc_lex_next();
    /* The bare code-address variable is followed by the argument list, not
     * by an array subscript. */
    if(sbt_kind==SBT_ID){entry=sbn_word(sbc_read_value(sbc_find_symbol(sbt_text,1),0));sbc_lex_next();}
    else entry=sbv_arg();
    sba_argc=0;if(sbt_kind==SBT_LP){sbc_lex_next();while(sbt_kind!=SBT_RP&&sbt_kind!=SBT_EOF&&sba_argc<12){sba_args[sba_argc++]=sbc_expr(0);if(!sbv_comma())break;}sbc_expect(SBT_RP);}
    sba_run(sbc_defseg,entry);
}
