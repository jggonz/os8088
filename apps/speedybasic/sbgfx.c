/* BASIC graphics operate on the retained surface, including GET/PUT images. */
static int sbg_get(int x,int y)
{
    int v;
    x=sbs_scale_x(x);y=sbs_scale_y(y);if(x<0||y<0)return 0;
    v=os88_peek(sbs_gseg,(unsigned)y*160+x/2);v=(x&1)?v&15:v>>4;
    if(sbs_bios_mode==1)return v==11?1:v==13?2:v==15?3:0;return v;
}
static void sbg_circle(void)
{
    int radius,color,aspect,a,co,si,x,y,lx,ly,steps,i,mark;
    sbc_lex_next();sbv_coords(0);sbv_comma();radius=sbc_expr(0);color=sbc_ink;
    if(sbv_comma()&&sbt_kind!=SBT_COMMA)color=sbv_arg();
    /* Optional start/end are consumed; default demos use a full ellipse. */
    if(sbv_comma()&&sbt_kind!=SBT_COMMA)(void)sbc_expr(0);
    if(sbv_comma()&&sbt_kind!=SBT_COMMA)(void)sbc_expr(0);
    aspect=sbn_parse("0.833333333333333");if(sbv_comma())aspect=sbc_expr(0);
    steps=sbn_word(radius);steps=(steps<<3)-steps;if(steps<24)steps=24;if(steps>1200)steps=1200;
    mark=sbn_top;lx=ly=0;
    for(i=0;i<=steps;++i){
        sbn_top=mark;a=sbn_int(i);co=sbn_parse("6.283185307179586");sbn_math('*',a,co);co=sbn_int(steps);sbn_math('/',a,co);
        co=sbn_trig(2,sbn_copy(a));si=sbn_trig(1,a);sbn_math('*',co,radius);sbn_math('*',si,radius);sbn_math('*',si,aspect);
        x=sbv_args[0]+sbn_word(co);y=sbv_args[1]-sbn_word(si);
        if(i)sbc_h_line(lx,ly,x,y,color);lx=x;ly=y;
    }
}
static int sbg_fx[512],sbg_fy[512];
static int sbg_raw_get(int x,int y)
{int v;v=os88_peek(sbs_gseg,(unsigned)y*160+x/2);return (x&1)?v&15:v>>4;}
static void sbg_raw_pixel(int x,int y,int color)
{unsigned p;int v;p=(unsigned)y*160+x/2;v=os88_peek(sbs_gseg,p);os88_poke(sbs_gseg,p,(x&1)?(v&240)|color:(v&15)|(color<<4));}
static void sbg_paint(void)
{
    int color,border,n,x,y,left,up,down,v;
    sbc_lex_next();sbv_coords(0);color=sbc_ink;if(sbv_comma())color=sbv_arg();border=color;if(sbv_comma())border=sbv_arg();
    color&=15;border&=15;
    if(sbs_bios_mode==1){color=color==1?11:color==2?13:color==3?15:0;border=border==1?11:border==2?13:border==3?15:0;}
    n=1;sbg_fx[0]=sbs_scale_x(sbv_args[0]);sbg_fy[0]=sbs_scale_y(sbv_args[1]);
    while(n&&sbc_status!=SB_STATE_ERROR){
        --n;x=sbg_fx[n];y=sbg_fy[n];if(x<0||y<0||x>=320||y>=200)continue;
        v=sbg_raw_get(x,y);if(v==color||v==border)continue;
        left=x;while(left>0&&sbg_raw_get(left-1,y)!=border&&sbg_raw_get(left-1,y)!=color)--left;
        up=down=0;
        for(x=left;x<320&&sbg_raw_get(x,y)!=border&&sbg_raw_get(x,y)!=color;++x){
            sbg_raw_pixel(x,y,color);
            if(y>0){v=sbg_raw_get(x,y-1);if(v!=border&&v!=color){if(!up&&n<511){sbg_fx[n]=x;sbg_fy[n++]=y-1;}up=1;}else up=0;}
            if(y+1<200){v=sbg_raw_get(x,y+1);if(v!=border&&v!=color){if(!down&&n<511){sbg_fx[n]=x;sbg_fy[n++]=y+1;}down=1;}else down=0;}
        }
    }
    sbc_notify();
}
static void sbg_image(int put)
{
    int sym,x,y,w,h,c,op,b;unsigned seg,off,p,size;
    sbc_lex_next();sbv_coords(0);
    if(!put){if(sbt_kind==SBT_OP&&sbt_op=='-')sbc_lex_next();sbv_coords(2);}
    sbv_comma();sym=sbc_find_symbol(sbt_text,1);sbc_lex_next();seg=sbc_symbols[sym].seg;off=sbc_symbols[sym].off;
    size=sbc_symbols[sym].rows*sbc_symbols[sym].cols*sbc_symbols[sym].type;
    if(put){w=sbc_getw(seg,off);h=sbc_getw(seg,off+2);}
    else {w=sbv_args[2]-sbv_args[0]+1;h=sbv_args[3]-sbv_args[1]+1;}
    if(w<=0||h<=0||(unsigned)w>64000u/(unsigned)h||(unsigned)w*h/2+5>size){sbc_set_error("GET/PUT image too large");return;}
    op='X';if(sbv_comma()){if(sbc_token("PSET"))op='=';else if(sbc_token("PRESET"))op='~';else if(sbt_kind==SBT_OP)op=sbt_op;}
    if(!put){sbc_putw(seg,off,w);sbc_putw(seg,off+2,h);}
    p=0;
    for(y=0;y<h;++y)for(x=0;x<w;++x){
        b=os88_peek(seg,off+4+(p>>1));
        if(put){c=(p&1)?b&15:b>>4;b=sbg_get(sbv_args[0]+x,sbv_args[1]+y);
            if(op=='X')c^=b;else if(op=='A')c&=b;else if(op=='O')c|=b;else if(op=='~')c=(~c)&15;
            sbc_h_pixel(sbv_args[0]+x,sbv_args[1]+y,c);
        }else{c=sbg_get(sbv_args[0]+x,sbv_args[1]+y);b=(p&1)?(b&240)|c:(b&15)|(c<<4);os88_poke(seg,off+4+(p>>1),b);}
        ++p;
    }
}
