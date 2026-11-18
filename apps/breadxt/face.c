/* Breadboard XT workbench face (SPEC.md 130.6). Original. Included by ui.c.
 *
 * The face is SECTIONS, each a rect the layout computes once. A repaint of
 * any rect is done in strips: one 4bpp band (draw.inc) per strip, into
 * which the SCENE draws the ground and every section that crosses the
 * strip, then one call puts it on the glass. A section's KEY is that same
 * drawing code run with bu_keying set: every primitive it would draw is
 * mixed into a 16-bit hash instead, so an incremental pass repaints exactly
 * the sections whose picture changed, and a full paint and an incremental
 * one are one code path (130.6.4). The board alone is driven by the dirty
 * row span the editor already keeps, and the scope by its own flag. */
#include "art.h"

int bx_rows(int x,int y,int w,int h);
void bx_new(int x,int y,int w,int h,int colour);
void bx_fill(int x1,int y1,int x2,int y2,int colour);
void bx_dither(int x1,int y1,int x2,int y2,int c1,int c2,int pat);
void bx_dots(int x1,int y1,int x2,int y2,int step,int colour);
unsigned bx_size(int id);
int bx_spr(int x,int y,int id);
int bx_artstr(int id,int n,char *buf);
int bx_text(int x,int y,const char *s,int colour,int font);
int bx_twidth(const char *s,int font);
int bx_fonth(int font);
int bx_out(void);
extern unsigned char bx_map[16];

#define C_BLACK 0
#define C_BLUE 1
#define C_GREEN 2
#define C_RED 4
#define C_BROWN 6
#define C_LGRAY 7
#define C_DGRAY 8
#define C_LBLUE 9
#define C_LGREEN 10
#define C_LCYAN 11
#define C_LRED 12
#define C_YELLOW 14
#define C_WHITE 15
#define BU_FULL 0
#define BU_MID 1
#define BU_COMPACT 2
#define BU_VBOARD 0
#define BU_VNETS 1
#define BU_VSCOPE 2
/* sections, in drawing order */
#define S_TBAR 0
#define S_BTN 1            /* nine: 1..9 */
#define S_STATE 10
#define S_CTABS 11
#define S_LEFT 12
#define S_CBOX 13
#define S_TABS 14
#define S_VIEW 15
#define S_HOLE 16
#define S_SHEAD 17
#define S_SCOPE 18
#define S_RBOX 19
#define S_INSP 20
#define S_PROBE 21
#define S_CIRC 22
#define S_STATUS 23
#define S_STIME 24          /* inside S_STATUS: what a run changes */
#define S_SMSG 25
#define S_PREAD 26          /* inside S_PROBE */
#define S_N 27

static int bu_lay,bu_tf,bu_bf,bu_th,bu_view,bu_cat,bu_example,bu_plain,bu_mono;
static int bu_tby,bu_sty,bu_lx2,bu_rx,bu_cx1,bu_cx2,bu_holey,bu_py,bu_rg;
static int bu_netscroll,bu_keying,bu_pbx1,bu_pbx2,bu_brx1,bu_bry1,bu_brx2,bu_bry2;
static BBWord bu_ptgen,bu_paintgen,bu_bdgen;
static char bu_hl[48];
#define BU_PEXT ((short *)((BBByte *)&bb_stage+1024)) /* 1024..1279 */
#define BU_WEXT ((short *)((BBByte *)&bb_stage+1280)) /* 1280..1791 */
#define BU_PTX ((short *)&bb_stage)            /* bytes 0..511 */
#define BU_PTY ((BBByte *)&bb_stage+512)        /* 512..1023 */
static BBWord bu_hash;
static int bu_sx1,bu_sy1,bu_sx2,bu_sy2;
static int bu_r1[S_N],bu_r2[S_N],bu_r3[S_N],bu_r4[S_N];
static BBWord bu_key[S_N];
/* The scope's header line - STALE, RUN or HOLD and the trigger - as last
 * painted; the simulation's own repaints cover only the plot under it. */
static BBWord bu_shead;
#ifdef BB_HOST
static int bu_host_sec=-1;   /* the host harness checks each section stays in its rect */
#endif
/* left panel and inspector rows, shared by drawing and hit tests */
static int bu_lcat,bu_litem,bu_lrow,bu_lex,bu_lexrow,bu_lexn,bu_lcol2;
static int bu_ival,bu_ibtn,bu_pbox,bu_pread,bu_tabx[4];
static const BBByte bu_cat_items[3][8]={
    {0,BB_RES,BB_CAP,BB_LED,BB_SWITCH,BB_555,BB_HC00,BB_POWER},
    {BB_HC00,BB_HC04,BB_HC08,BB_HC32,BB_HC86,BB_INPUT,BB_INDICATOR,BB_CLOCK},
    {BB_GROUND,BB_POT,BB_POWER,BB_CLOCK,BB_SWITCH,BB_CAP,255,255}};
static const char *bu_cat_names[]={"Basics","Logic","More"};
static const char *bu_type_names[]={"Wire","Resistor","LED","Switch","Power","Ground",
    "Potentiometer","Logic input","Logic lamp","Clock","74HC00 NAND","74HC04 NOT",
    "74HC08 AND","74HC32 OR","74HC86 XOR","Capacitor","555 timer"};
static const BBByte bu_type_icon[]={BA_P_WIRE,BA_P_RES,BA_P_LED,BA_P_SWITCH,BA_POWER,
    BA_GROUND,BA_P_POT,BA_INPUT,BA_LAMP,BA_CLOCK,BA_P_IC,BA_P_IC,BA_P_IC,BA_P_IC,
    BA_P_IC,BA_P_CAP,BA_P_555};
static const char *bu_ex_names[]={"LED test","Divider","Potentiometer","Switched LED",
    "Logic gates","Broken circuit","RC charge","555 flasher"};
static const BBByte bu_tb_icon[]={BA_T_SELECT,BA_T_WIRE,BA_T_PROBE,BA_T_ROTATE,
    BA_T_UNDO,BA_T_RUN,BA_T_PAUSE,BA_T_STEP,BA_T_RESET};
static const char *bu_tb_text[]={"Select","Wire","Probe","Rotate","Undo","Run","Pause","Step","Reset"};
static const char *bu_tb_short[]={"Sel","Wire","Prb","Rot","Undo","Run","Pause","Step","Reset"};
/* resistor colour code: black brown red orange yellow green blue violet grey white */
static const BBByte bu_band_col[]={0,6,4,12,14,2,1,5,8,15};

static int bu_max(int a,int b){return a>b?a:b;}
/* ---- the keyed primitives ---------------------------------------------- */
static void bu_mix(unsigned v){bu_hash=(bu_hash<<3)+(bu_hash>>13)+v+1;}
static void bu_f(int x1,int y1,int x2,int y2,int c)
{
    if(bu_keying){bu_mix(x1);bu_mix(y1);bu_mix(x2);bu_mix(y2+(c<<12));return;}
    bx_fill(x1,y1,x2,y2,c);
}
static void bu_d(int x1,int y1,int x2,int y2,int c1,int c2,int pat)
{
    if(bu_keying){bu_mix(x1);bu_mix(y1+(pat<<12));bu_mix(x2);bu_mix(y2+(c1<<8)+(c2<<12));return;}
    bx_dither(x1,y1,x2,y2,c1,c2,pat);
}
static void bu_dots(int x1,int y1,int x2,int y2,int step,int c)
{
    if(bu_keying){bu_mix(x1);bu_mix(y1);bu_mix(x2+(step<<10));bu_mix(y2+(c<<12));return;}
    bx_dots(x1,y1,x2,y2,step,c);
}
static int bu_s(int x,int y,int id)
{
    unsigned i;
    if(bu_keying){bu_mix(x);bu_mix(y);bu_mix(id);for(i=0;i<16;i++)bu_mix(bx_map[i]);return bx_size(id)&255;}
    return bx_spr(x,y,id);
}
static int bu_t(int x,int y,const char *s,int c,int font)
{
    if(bu_keying){bu_mix(x);bu_mix(y+(c<<12));while(*s)bu_mix(*s++);return 0;}
    return bx_text(x,y,s,c,font);
}
/* A light ground under text: a dither on colour, plain white on 1bpp,
 * where a checker behind a glyph reads as noise. */
static void bu_dg(int x1,int y1,int x2,int y2)
{if(bu_mono)bu_f(x1,y1,x2,y2,C_WHITE);else bu_d(x1,y1,x2,y2,C_LGRAY,C_WHITE,0);}
/* The desk's ground on a rect less an inner one: the four bands around a
 * panel, so no pixel of the ground is composed under a panel and then
 * overwritten. */
static void bu_band(int x1,int y1,int x2,int y2,int white)
{if(white)bu_f(x1,y1,x2,y2,C_WHITE);else bu_d(x1,y1,x2,y2,C_LGRAY,C_WHITE,0);}
static void bu_ring(int x1,int y1,int x2,int y2,int ix1,int iy1,int ix2,int iy2,int white)
{
    if(ix1<x1)ix1=x1;if(iy1<y1)iy1=y1;if(ix2>x2)ix2=x2;if(iy2>y2)iy2=y2;
    if(ix1>ix2||iy1>iy2){bu_band(x1,y1,x2,y2,white);return;}
    if(iy1>y1)bu_band(x1,y1,x2,iy1-1,white);
    if(iy2<y2)bu_band(x1,iy2+1,x2,y2,white);
    if(ix1>x1)bu_band(x1,iy1,ix1-1,iy2,white);
    if(ix2<x2)bu_band(ix2+1,iy1,x2,iy2,white);
}
static void bu_frame(int x1,int y1,int x2,int y2,int c)
{bu_f(x1,y1,x2,y1,c);bu_f(x1,y2,x2,y2,c);bu_f(x1,y1,x1,y2,c);bu_f(x2,y1,x2,y2,c);}
static int bu_in(int x1,int y1,int x2,int y2)
{return x2>=bu_sx1&&x1<=bu_sx2&&y2>=bu_sy1&&y1<=bu_sy2;}
static void bu_grey(int on)
{
    unsigned i;for(i=0;i<16;i++)bx_map[i]=(on&&i!=13&&i!=C_WHITE)?C_DGRAY:i;
}
/* An ink that stays an ink on 1bpp: black on white there, c on colour. */
static int bu_ink(int c){return bu_mono?C_BLACK:c;}

/* ---- geometry ----------------------------------------------------------- */
static int bu_gap(int row)
{return (row>=2?bu_rg:0)+(row>=7?bu_py:0)+(row>=12?bu_rg:0);}
static int bu_hx(unsigned hole){return bu_gx+((int)(hole%30)-(int)bb_doc.panx)*bu_pitch;}
static int bu_hy(unsigned hole)
{
    int r;r=hole/30;
    return bu_gy+(r-(int)bb_doc.pany)*bu_py+bu_gap(r)-bu_gap(bb_doc.pany);
}
static int bu_visible(unsigned hole)
{
    int x,y;if(hole>=BB_HOLES||(int)(hole/30)<(int)bb_doc.pany)return 0;x=bu_hx(hole);y=bu_hy(hole);
    return x>=bu_bx+4&&x<bu_bx+bu_bw-4&&y>=bu_by+3&&y<bu_by+bu_bh-3;
}
static BBWord bu_hit_hole(int x,int y)
{
    int col,row,hy,tol;
    if(x<bu_bx||x>=bu_bx+bu_bw||y<bu_by||y>=bu_by+bu_bh)return BB_NONE;
    col=(x-bu_gx+bu_pitch/2)/bu_pitch+bb_doc.panx;
    if(x<bu_gx-bu_pitch/2||col<0||col>=30)return BB_NONE;
    tol=bu_pitch*2/5;if(bu_abs(x-bu_hx(col))>tol)return BB_NONE;
    tol=bu_py*2/5;if(tol<3)tol=3;
    for(row=bb_doc.pany;row<14;row++){
        hy=bu_hy(row*30+col);
        if(bu_abs(y-hy)<=tol)return (row<<5)-(row<<1)+col;
    }
    return BB_NONE;
}
static void bu_rect(int s,int x1,int y1,int x2,int y2)
{bu_r1[s]=x1;bu_r2[s]=y1;bu_r3[s]=x2;bu_r4[s]=y2;}
static void bu_noset(int s){bu_rect(s,1,1,0,0);}

static int bu_layout(void *win)
{
    int i,x,y,x2,body,top;
    os88_wm_content(win,&bu_org);if(os88_wm_geom(win,&bu_size)!=0)return 0;
    os88_wm_display(win,&bu_video);
    bu_x=bu_org.x;bu_y=bu_org.y;bu_w=bu_size.w;bu_h=bu_size.h;bu_mono=bu_video.bpp<4;
    bu_bxsetup(bu_mono?1:4);
    bu_lay=bu_w>=600&&bu_h>=380?BU_FULL:bu_w>=560&&bu_h>=240?BU_MID:BU_COMPACT;
    bu_compact=bu_lay==BU_COMPACT;
    if(bu_plain){bu_tf=-1;bu_bf=-1;}
    else{bu_tf=bu_lay==BU_FULL?BA_F_TEXT13:bu_lay==BU_MID?BA_F_TEXT10:BA_F_TEXT08;
        bu_bf=bu_lay==BU_FULL?BA_F_BIG16:bu_lay==BU_MID?BA_F_BIG12:BA_F_TEXT08;}
    bu_th=bx_fonth(bu_tf);
    bu_pitch=12;bu_py=bu_lay==BU_FULL?12:bu_lay==BU_MID?10:8;
    if(bb_doc.zoom==2){bu_pitch=20;bu_py=bu_compact?12:20;}
    /* CGA's pixels are tall: 20 across by 8 down is a square hole pitch,
     * and it fills the width the compact board has (SPEC.md 130.6.1). */
    if(bu_compact&&bu_w>=624)bu_pitch=20;
    bu_rg=bu_py/2;
    x2=bu_x+bu_w-1;
    bu_tby=bu_y+(bu_lay==BU_FULL?32:bu_lay==BU_MID?28:17);
    bu_sty=bu_y+bu_h-bu_th-4;
    for(i=0;i<S_N;i++)bu_noset(i);
    bu_rect(S_TBAR,bu_x,bu_y,x2,bu_tby-1);
    for(i=0;i<9;i++){
        if(bu_compact)x=bu_x+4+(i<5?i*52:264+(i-5)*60);else x=bu_x+8+(i<5?i*40:208+(i-5)*72);
        bu_rect(S_BTN+i,x,bu_y+(bu_compact?2:3),x+(bu_compact?(i<5?48:56):(i<5?36:68))-1,bu_tby-(bu_compact?3:5));
    }
    x=bu_x+(bu_compact?512:512);
    bu_rect(S_STATE,x,bu_y+(bu_compact?2:5),x2-6,bu_tby-(bu_compact?3:7));
    bu_rect(S_STATUS,bu_x,bu_sty,x2,bu_y+bu_h-1);
    x=bu_x+6+(bu_compact||bu_plain?0:28);
    i=bx_twidth(bu_compact?"t 00000.00 s":"Simulation time: 00000.00 s",bu_tf);
    bu_rect(S_STIME,x,bu_sty+1,x+i,bu_y+bu_h-1);
    bu_rect(S_SMSG,x+i+12,bu_sty+1,x2-15-bx_twidth("F1 Help",bu_tf)-17,bu_y+bu_h-1);
    body=bu_sty-1;
    if(bu_compact){
        bu_lx2=bu_x;bu_cx1=bu_x;bu_cx2=x2;bu_rx=x2+1;
        bu_rect(S_CTABS,bu_x,bu_tby,x2,bu_tby+bu_th+2);top=bu_tby+bu_th+3;
        for(i=0;i<4;i++)bu_tabx[i]=bu_x+4+i*96;
        bu_bx=bu_x;bu_by=top;bu_bw=bu_w;bu_bh=body-top+1;
        bu_holey=0;bu_scope_h=0;
        if(bu_mode==BU_PALETTE)bu_rect(S_LEFT,bu_x,top,x2,body);
        else if(bu_mode==BU_INSPECT||bu_mode==BU_VALUE){
            bu_rect(S_RBOX,bu_x,top,x2,body);
            bu_rect(S_INSP,bu_x+4,top+2,bu_x+bu_w/3-1,body-2);
            bu_rect(S_PROBE,bu_x+bu_w/3,top+2,bu_x+2*bu_w/3-1,body-2);
            bu_rect(S_CIRC,bu_x+2*bu_w/3,top+2,x2-4,body-2);
        }else bu_rect(S_VIEW,bu_x,top,x2,body);
        bu_gx=bu_x+28;bu_gy=top+bu_th+12;
    }else{
        bu_lx2=bu_x+128;bu_cx1=bu_lx2;bu_cx2=x2-128;bu_rx=bu_cx2+1;
        bu_rect(S_LEFT,bu_x,bu_tby,bu_lx2-1,body);
        bu_rect(S_CBOX,bu_cx1,bu_tby,bu_cx2,body);
        /* one row more than the tabs: the box's top edge, which the open
         * tab breaks - its repaint must restore that row too */
        bu_rect(S_TABS,bu_cx1+4,bu_tby+4,bu_cx2-4,bu_tby+bu_th+9);
        top=bu_tby+bu_th+9;
        bu_tabx[0]=bu_cx1+8;
        bu_tabx[1]=bu_tabx[0]+bx_twidth("Breadboard",bu_tf)+22;
        bu_tabx[2]=bu_tabx[1]+bx_twidth("Net list",bu_tf)+22;
        bu_tabx[3]=bu_tabx[2]+(bu_lay==BU_MID?bx_twidth("Scope",bu_tf)+22:0);
        bu_bx=bu_cx1+5;bu_by=top+1;bu_bw=bu_cx2-bu_cx1-9;
        bu_gx=bu_cx1+28;bu_gy=bu_by+bu_th+10;
        if(bu_lay==BU_FULL){
            bu_holey=bu_gy+15*bu_py+12;bu_bh=bu_holey-bu_by-1;
            if(bu_holey+bu_th+2>body){bu_bh=body-bu_by-2;bu_holey=0;}
            else bu_rect(S_HOLE,bu_cx1+5,bu_holey,bu_cx2-5,bu_holey+bu_th+1);
            bu_scope_y=bu_holey?bu_holey+bu_th+6:body;
            bu_scope_h=body-4-bu_scope_y;
            if(bu_scope_h>=bu_th+40){
                bu_rect(S_SHEAD,bu_cx1+4,bu_scope_y,bu_cx2-4,bu_scope_y+bu_th+4);
                if(bu_scope_on)bu_rect(S_SCOPE,bu_cx1+4,bu_scope_y+bu_th+5,bu_cx2-4,body-4);
            }else bu_scope_h=0;
        }else{bu_holey=0;bu_scope_h=0;bu_bh=body-4-bu_by;}
        bu_rect(S_VIEW,bu_bx,bu_by,bu_bx+bu_bw-1,bu_by+bu_bh-1);
        bu_rect(S_RBOX,bu_rx,bu_tby,x2,body);
        y=bu_tby+4;
        i=bu_lay==BU_FULL?(bu_th+4)+36+22+24+6:(bu_th+4)+20+18+18+4;
        bu_rect(S_INSP,bu_rx+4,y,x2-4,y+i-1);y+=i;
        i=bu_lay==BU_FULL?(bu_th+4)+24+32+6:(bu_th+4)+18+22+4;
        bu_rect(S_PROBE,bu_rx+4,y,x2-4,y+i-1);y+=i;
        bu_rect(S_CIRC,bu_rx+4,y,x2-4,body-4);
    }
    /* left panel rows */
    bu_lcat=bu_r2[S_LEFT]+4+bu_th+6;
    bu_lrow=bu_plain?bu_th+5:bu_lay==BU_FULL?19:bu_lay==BU_MID?14:bu_th+3;
    bu_litem=bu_lcat+bu_th+12;
    bu_lexrow=bu_th+2;
    bu_lcol2=0;
    if(bu_compact){bu_lcol2=bu_x+bu_w/2;bu_lex=bu_lcat+2;}
    else bu_lex=bu_litem+8*bu_lrow+6+bu_th+6;
    bu_lexn=(bu_r4[S_LEFT]-6-bu_lex)/bu_lexrow;if(bu_compact)bu_lexn=bu_lexn>=4?8:bu_lexn+bu_lexn;
    if(bu_lexn>8)bu_lexn=8;if(bu_lexn<0)bu_lexn=0;
    /* inspector rows */
    y=bu_r2[S_INSP]+bu_th+4;
    bu_ival=y+(bu_compact?2:bu_lay==BU_FULL?36:20);
    bu_ibtn=bu_ival+(bu_compact?bu_th+6:bu_lay==BU_FULL?24:18);
    bu_pbox=bu_r2[S_PROBE]+bu_th+(bu_compact?6:8);
    bu_pread=bu_pbox+(bu_compact?bu_th+6:bu_lay==BU_FULL?24:18);
    if(bu_r1[S_PROBE]<=bu_r3[S_PROBE])bu_rect(S_PREAD,bu_r1[S_PROBE]+6,bu_pread,bu_r3[S_PROBE]-6,bu_pread+bx_fonth(bu_bf)+6);
    bu_cols=30;bu_rows=14;
    if(bb_doc.panx>29)bb_doc.panx=29;if(bb_doc.pany>13)bb_doc.pany=13;
    bu_brx1=bu_gx-18;bu_brx2=bu_gx+(29-(int)bb_doc.panx)*bu_pitch+8;
    if(bu_brx2>bu_bx+bu_bw-2)bu_brx2=bu_bx+bu_bw-2;
    bu_bry1=bu_hy(bb_doc.pany*30)-8;bu_bry2=bu_hy(13*30)+8;
    return 1;
}

/* ---- shared pieces ------------------------------------------------------ */
static void bu_fitw(char *s,int w)
{
    unsigned n;n=os88_strlen(s);
    while(n&&bx_twidth(s,bu_tf)>w)s[--n]=0;
}
static void bu_header(int x1,int y1,int x2,const char *title)
{
    int tw;
    tw=bx_twidth(title,bu_tf);
    bu_d(x1,y1,x2,y1+bu_th+3,C_DGRAY,C_WHITE,1);
    bu_f(x1,y1,x1+tw+7,y1+bu_th+3,C_WHITE);
    bu_t(x1+4,y1+2,title,C_BLACK,bu_tf);
    bu_f(x1,y1+bu_th+4,x2,y1+bu_th+4,C_BLACK);
}
static void bu_box(int x1,int y1,int x2,int y2,int shadow)
{
    if(shadow){bu_f(x1+2,y2+1,x2+1,y2+1,C_DGRAY);bu_f(x2+1,y1+2,x2+1,y2+1,C_DGRAY);}
    bu_f(x1+1,y1+1,x2-1,y2-1,C_WHITE);bu_frame(x1,y1,x2,y2,C_BLACK);
}
static void bu_tri(int x,int y,int down)
{
    int k;for(k=0;k<4;k++){if(down)bu_f(x+k,y+k,x+6-k,y+k,C_BLACK);else bu_f(x+k,y+3-k,x+6-k,y+3-k,C_BLACK);}
}
static void bu_button(int s,int icon,const char *text,int state)
{
    int x1,y1,x2,y2,iw,ih,tw,ix,sz;
    x1=bu_r1[s];y1=bu_r2[s];x2=bu_r3[s]-1;y2=bu_r4[s]-1;
    if(state&1){x1++;y1++;x2++;y2++;}
    else{bu_f(x1+2,y2+1,x2+1,y2+1,C_DGRAY);bu_f(x2+1,y1+2,x2+1,y2+1,C_DGRAY);}
    bu_frame(x1,y1,x2,y2,C_BLACK);
    if(state&1){bu_dg(x1+1,y1+1,x2-1,y2-1);if(bu_mono)bu_frame(x1+1,y1+1,x2-1,y2-1,C_BLACK);}else bu_f(x1+1,y1+1,x2-1,y2-1,C_WHITE);
    if(state&2)bu_grey(1);
    sz=icon>=0?bx_size(icon):0;iw=sz&255;ih=sz>>8;
    if(iw&&ih>y2-y1-2)iw=0;
    if(!iw&&!text)text=bu_compact?bu_tb_short[s-S_BTN]:bu_tb_text[s-S_BTN];
    tw=text?bx_twidth(text,bu_tf):0;
    ix=(x1+x2+1-iw-tw-(iw&&tw?5:0))/2;
    if(iw)bu_s(ix,(y1+y2+1-ih)/2,icon);
    if(tw)bu_t(ix+iw+(iw?5:0),(y1+y2+2-bu_th)/2,text,C_BLACK,bu_tf);
    if(state&2)bu_grey(0);
}
static void bu_lamp(int x,int y,int c){if(bu_mono)c=c==C_LGREEN||c==C_LRED?C_BLACK:C_WHITE;bu_f(x,y,x+9,y+9,C_BLACK);bu_f(x+1,y+1,x+8,y+8,c);if(!bu_mono)bu_f(x+2,y+2,x+3,y+3,C_WHITE);}

/* ---- tool bar and status bar -------------------------------------------- */
static int ovl_bu_s_tbar(void)
{
    int y1,y2;y1=bu_y+4;y2=bu_tby-5;
    bu_d(bu_r1[S_TBAR],bu_r2[S_TBAR],bu_r3[S_TBAR],bu_tby-2,C_LGRAY,C_WHITE,0);
    bu_f(bu_r1[S_TBAR],bu_tby-1,bu_r3[S_TBAR],bu_tby-1,C_BLACK);
    if(!bu_compact){bu_f(bu_x+210,y1,bu_x+210,y2,C_DGRAY);bu_f(bu_x+505,y1,bu_x+505,y2,C_DGRAY);}
    return 1;
}
static int ovl_bu_s_btn(int i)
{
    int state;
    state=(i==0&&bu_tool==BU_SELECT)||(i==1&&bu_tool==BU_WIRE)||(i==2&&bu_tool==BU_PROBE)||(i==5&&bb_running);
    if((i==6&&!bb_running)||(i==3&&bu_selected_kind!=BB_PART))state|=2;
    bu_button(S_BTN+i,bu_compact?-1:bu_tb_icon[i],i<5?0:(bu_compact?bu_tb_short[i]:bu_tb_text[i]),state);
    return 1;
}
static int ovl_bu_state_text(void)
{
    if(bb_result.diag&&!bb_recompile&&!bu_diag_stale){bu_begin("Error: ");bu_append(bb_diag_text(bb_result.diag));return C_LRED;}
    if(!bb_valid){bu_begin(bb_doc.np?"Stale - Run":"Empty board");return C_DGRAY;}
    bu_begin(bb_running?"Running":"Paused");return bb_running?C_LGREEN:C_YELLOW;
}
static int ovl_bu_s_state(void)
{
    int x1,y1,x2,y2,c;
    x1=bu_r1[S_STATE];y1=bu_r2[S_STATE];x2=bu_r3[S_STATE];y2=bu_r4[S_STATE];
    bu_dg(x1,y1,x2,y2);
    bu_f(x1,y1,x2,y1,C_DGRAY);bu_f(x1,y1,x1,y2,C_DGRAY);
    c=ovl_bu_state_text();
    bu_lamp(x1+6,(y1+y2-9)/2,c);bu_fitw(bu_line,x2-x1-26);
    bu_t(x1+22,(y1+y2+2-bu_th)/2,bu_line,C_BLACK,bu_tf);
    return 1;
}
static int ovl_bu_s_status(void)
{
    int x1,y1,x2,y2,x;
    x1=bu_r1[S_STATUS];y1=bu_r2[S_STATUS];x2=bu_r3[S_STATUS]-15;y2=bu_r4[S_STATUS];
    bu_f(x1,y1,x2+15,y2,C_WHITE);bu_f(x1,y1,x2+15,y1,C_BLACK);
    if(!bu_compact&&!bu_plain){
        if(!bb_running)bu_grey(1);bu_s(x1+6,(y1+y2+1-(int)(bx_size(BA_WAVE)>>8))/2,BA_WAVE);if(!bb_running)bu_grey(0);
    }
    x=bu_r1[S_SMSG]-8;      /* the window's grow box owns the corner */
    bu_f(bu_r3[S_SMSG]+1,y1,bu_r3[S_SMSG]+1,y2,C_BLACK);bu_f(x2,y1,x2,y2,C_BLACK);
    bu_t(bu_r3[S_SMSG]+9,y1+2,"F1 Help",C_BLACK,bu_tf);
    (void)x;return 1;
}
static int ovl_bu_s_stime(void)
{
    bu_begin(bu_compact?"t ":"Simulation time: ");bb_timefmt(bu_timebuf,bb_result.tlo,bb_result.thi);
    bu_timebuf[os88_strlen(bu_timebuf)>2?os88_strlen(bu_timebuf)-2:0]=0;
    bu_append(bu_timebuf);bu_append(" s");
    bu_t(bu_r1[S_STIME],bu_r2[S_STIME]+1,bu_line,C_BLACK,bu_tf);
    return 1;
}
static int ovl_bu_s_smsg(void)
{
    int err;
    err=bb_result.diag&&!bb_recompile&&!bu_diag_stale;
    bu_begin("");
    if(err)bu_append("Click for the error");
    else if(bu_msg[0])bu_append(bu_msg);
    else if(bu_ready)bu_append("Preview: Enter commit; Esc cancel");
    else if(bu_tool==BU_PART)bu_append("Click each pin's hole; Enter");
    else if(bu_tool==BU_WIRE)bu_append("Wire: click both ends; B bends");
    else if(bu_tool==BU_PROBE)bu_append("Probe: click a hole, pin or part");
    bu_fitw(bu_line,bu_r3[S_SMSG]-bu_r1[S_SMSG]-4);
    bu_t(bu_r1[S_SMSG],bu_r2[S_SMSG]+1,bu_line,err?bu_ink(C_RED):C_BLACK,bu_tf);
    return 1;
}

/* ---- left panel: parts and examples ------------------------------------- */
static int bu_item_on(int type)
{
    if(type==0)return bu_tool==BU_WIRE;
    return bu_tool==BU_PART&&bu_type==type;
}
/* A list slot: one column of eight, or on COMPACT two columns of four. */
static int bu_slx1,bu_slx2;
static int bu_lslot(int i,int base,int rowh,int xa,int xb)
{
    int w;
    if(!bu_compact){bu_slx1=xa;bu_slx2=xb;return base+i*rowh;}
    w=(xb-xa+1)/2;bu_slx1=xa+(i>>2)*w;bu_slx2=bu_slx1+w-1;return base+(i&3)*rowh;
}
static int bu_lhit(int x,int y,int base,int rowh,int xa,int xb)
{
    int r;if(y<base||x<xa||x>xb)return -1;r=(y-base)/rowh;
    if(!bu_compact)return r<8?r:-1;
    if(r>3)return -1;return r+(x>=xa+(xb-xa+1)/2?4:0);
}
static int ovl_bu_s_left(void)
{
    int x1,y1,x2,y2,i,y,type,on,ix,sz,ex,cx2;
    x1=bu_r1[S_LEFT]+4;y1=bu_r2[S_LEFT]+4;x2=bu_r3[S_LEFT]-4;y2=bu_r4[S_LEFT]-4;
    bu_ring(bu_r1[S_LEFT],bu_r2[S_LEFT],bu_r3[S_LEFT],bu_r4[S_LEFT],x1,y1,x2,y2,bu_compact);
    bu_f(x1+1,y1+1,x2-1,y2-1,C_WHITE);bu_frame(x1,y1,x2,y2,C_BLACK);
    cx2=bu_lcol2?bu_lcol2-6:x2-1;
    bu_header(x1+1,y1+1,cx2,"Parts");
    y=bu_lcat;
    bu_box(x1+6,y,cx2-7,y+bu_th+5,1);bu_t(x1+12,y+3,bu_cat_names[bu_cat],C_BLACK,bu_tf);
    bu_tri(cx2-20,y+bu_th/2,1);
    for(i=0;i<8;i++){
        type=bu_cat_items[bu_cat][i];if(type==255)break;
        y=bu_lslot(i,bu_litem,bu_lrow,x1,cx2);if(y+bu_lrow>y2)break;
        if(!bu_in(bu_slx1,y,bu_slx2,y+bu_lrow-1)&&!bu_keying)continue;
        on=bu_item_on(type);
        if(on)bu_f(bu_slx1+3,y,bu_slx2-3,y+bu_lrow-2,bu_mono?C_BLACK:C_BLUE);
        ix=bu_slx1+6;
        sz=bu_plain?0:bx_size(bu_type_icon[type]);
        if(!bu_plain&&!bu_compact){if(sz&&(int)(sz>>8)<=bu_lrow)bu_s(ix+(28-(int)(sz&255))/2,y+(bu_lrow-(int)(sz>>8))/2,bu_type_icon[type]);ix+=32;}
        bu_t(ix,y+(bu_lrow-1-bu_th)/2,bu_cat==0&&type==BB_HC00?"Logic IC":bu_type_names[type],on?C_WHITE:C_BLACK,bu_tf);
    }
    y=bu_lex-bu_th-8;ex=x1;
    if(bu_lcol2){y=y1+1;ex=bu_lcol2-2;bu_f(ex,y1,ex,y2,C_BLACK);}
    else bu_dots(x1+2,y-4,x2-2,y-4,2,C_BLACK);
    bu_header(ex+1,y,x2-1,"Examples");
    for(i=0;i<bu_lexn;i++){
        y=bu_lslot(i,bu_lex,bu_lexrow,ex,x2);on=bu_example==i+1;
        if(bu_lexn<8&&i==bu_lexn-1)on=bu_mode==BU_EXAMPLES;
        if(on)bu_f(bu_slx1+3,y,bu_slx2-3,y+bu_lexrow-2,bu_mono?C_BLACK:C_BLUE);
        ix=bu_slx1+6;
        if(!bu_plain&&(int)(bx_size(BA_DOC)>>8)<=bu_lexrow){
            if(on)bu_grey(0);bu_s(ix,y+(bu_lexrow-1-(int)(bx_size(BA_DOC)>>8))/2,BA_DOC);ix+=18;}
        bu_t(ix,y+(bu_lexrow-1-bu_th)/2,bu_lexn<8&&i==bu_lexn-1?"More...":bu_ex_names[i],on?C_WHITE:C_BLACK,bu_tf);
    }
    return 1;
}

/* ---- centre: box, tabs, hole line --------------------------------------- */
static int ovl_bu_s_cbox(void)
{
    int y2;y2=bu_holey?bu_holey+bu_th+2:bu_sty-5;
    bu_ring(bu_r1[S_CBOX],bu_r2[S_CBOX],bu_r3[S_CBOX],bu_r4[S_CBOX],bu_cx1+4,bu_by-1,bu_cx2-4,y2,0);
    bu_frame(bu_cx1+4,bu_by-1,bu_cx2-4,y2,C_BLACK);
    /* white around the board, which covers its own rect */
    if(bu_mode==BU_BOARD&&bu_view==BU_VBOARD)bu_ring(bu_cx1+5,bu_by,bu_cx2-5,y2-1,bu_brx1,bu_bry1,bu_brx2,bu_bry2,1);
    else bu_f(bu_cx1+5,bu_by,bu_cx2-5,y2-1,C_WHITE);
    return 1;
}
static int ovl_bu_s_tabs(void)
{
    int i,n,x1,x2,y1,y2,on;static const char *names[]={"Breadboard","Net list","Scope"};
    n=bu_lay==BU_MID?3:2;y1=bu_r2[S_TABS];y2=bu_r4[S_TABS]-1;
    for(i=0;i<n;i++){
        x1=bu_tabx[i];x2=bu_tabx[i+1]-3;on=bu_view==i&&bu_mode==BU_BOARD;
        if(!on)y1++;
        bu_f(x1,y1,x2,y1,C_BLACK);bu_f(x1,y1,x1,y2,C_BLACK);bu_f(x2,y1,x2,y2,C_BLACK);
        if(on)bu_f(x1+1,y1+1,x2-1,y2+1,C_WHITE);else bu_dg(x1+1,y1+1,x2-1,y2);
        bu_t(x1+10,y1+3,names[i],C_BLACK,bu_tf);
        if(!on)y1--;
    }
    return 1;
}
static int ovl_bu_s_hole(void)
{
    int x;x=bu_r1[S_HOLE]+4;
    x+=bu_t(x,bu_r2[S_HOLE]+1,"Hole: ",C_BLACK,bu_tf);
    bu_begin("");if(bu_hole<BB_HOLES){bu_hole_name(bu_hole);if(bu_line[0]>='a'&&bu_line[0]<='j')bu_line[0]-=32;}else bu_append("--");
    bu_t(x,bu_r2[S_HOLE]+1,bu_line,C_BLACK,bu_tf);
    return 1;
}
static int ovl_bu_s_ctabs(void)
{
    int i,x,on;static const char *names[]={"Board","Parts","Inspect","Scope","Nets"};
    bu_f(bu_r1[S_CTABS],bu_r2[S_CTABS],bu_r3[S_CTABS],bu_r4[S_CTABS]-1,C_WHITE);
    bu_f(bu_r1[S_CTABS],bu_r4[S_CTABS],bu_r3[S_CTABS],bu_r4[S_CTABS],C_BLACK);
    for(i=0;i<5;i++){
        x=bu_x+4+i*80;
        on=i==0?bu_mode==BU_BOARD&&bu_view==BU_VBOARD:i==1?bu_mode==BU_PALETTE:i==2?bu_mode==BU_INSPECT||bu_mode==BU_VALUE:
            i==3?bu_mode==BU_BOARD&&bu_view==BU_VSCOPE:bu_mode==BU_BOARD&&bu_view==BU_VNETS;
        if(on)bu_f(x,bu_r2[S_CTABS]+1,x+74,bu_r4[S_CTABS]-1,C_BLACK);
        bu_t(x+8,bu_r2[S_CTABS]+1,names[i],on?C_WHITE:C_BLACK,bu_tf);
    }
    return 1;
}

/* ---- the board ------------------------------------------------------------ */
static void bu_lead(int x1,int y1,int x2,int y2,int c)
{
    if(x1!=x2){bu_f(bu_min(x1,x2),y1,x1>x2?x1:x2,y1,c);}
    if(y1!=y2){bu_f(x2,bu_min(y1,y2),x2,y1>y2?y1:y2,c);}
}
static void bu_wseg(int x1,int y1,int x2,int y2,int c,int t)
{
    int a;if(x1>x2){a=x1;x1=x2;x2=a;}if(y1>y2){a=y1;y1=y2;y2=a;}
    bu_f(x1-t/2,y1-t/2,x2+(t-1)/2,y2+(t-1)/2,c);
}
static void bu_wpath(int x1,int y1,int x2,int y2,int c,int t)
{
    if(x1==x2||y1==y2)bu_wseg(x1,y1,x2,y2,c,t);
    else{bu_wseg(x1,y1,x2,y1,c,t);bu_wseg(x2,y1,x2,y2,c,t);}
}
static int ovl_bu_wire(const BBWire *wire,int preview)
{
    int x,y,nx,ny,i,c,t,top,bottom;BBWord h;
    top=bu_min(bu_hy(wire->a),bu_hy(wire->b));bottom=bu_max(bu_hy(wire->a),bu_hy(wire->b));
    for(i=0;i<wire->nbend;i++){h=wire->bend[i*2+1]*30+wire->bend[i*2];ny=bu_hy(h);if(ny<top)top=ny;if(ny>bottom)bottom=ny;}
    if(!bu_in(bu_cx1,top-5,bu_cx2,bottom+5))return 1;
    c=preview?C_DGRAY:bu_mono?C_BLACK:wire->color;t=bu_compact?1:2;
    x=bu_hx(wire->a);y=bu_hy(wire->a);
    for(i=0;i<wire->nbend;i++){
        h=wire->bend[i*2+1]*30+wire->bend[i*2];nx=bu_hx(h);ny=bu_hy(h);
        bu_wpath(x,y,nx,ny,c,t);x=nx;y=ny;
    }
    bu_wpath(x,y,bu_hx(wire->b),bu_hy(wire->b),c,t);
    if(!bu_compact){
        bu_f(bu_hx(wire->a)-1,bu_hy(wire->a)-1,bu_hx(wire->a)+1,bu_hy(wire->a)+1,C_BLACK);
        bu_f(bu_hx(wire->b)-1,bu_hy(wire->b)-1,bu_hx(wire->b)+1,bu_hy(wire->b)+1,C_BLACK);
    }
    if(!preview&&wire->id==bu_selected&&bu_selected_kind==BB_WIRE){
        bu_frame(bu_hx(wire->a)-4,bu_hy(wire->a)-4,bu_hx(wire->a)+4,bu_hy(wire->a)+4,C_BLACK);
        bu_frame(bu_hx(wire->b)-4,bu_hy(wire->b)-4,bu_hx(wire->b)+4,bu_hy(wire->b)+4,C_BLACK);
    }
    return 1;
}
/* A short tag: the reference and, for R and C, the value ("R1 10k"). */
static void bu_tagtext(const BBPart *part,int withvalue)
{
    unsigned v;
    bu_begin("");bu_ident(part);
    if(!withvalue)return;
    v=part->value;
    if(part->type==BB_RES||part->type==BB_POT){
        bu_append(" ");
        if(v<100)bu_number(v*10);
        else{bu_number(v/100);if(v<1000&&v%100){bu_append(".");bu_number((v%100)/10);}bu_append("k");}
    }else if(part->type==BB_CAP){
        bu_append(" ");
        if(v>=1000){bu_number(v/1000);if(v<10000&&v%1000){bu_append(".");bu_number((v%1000)/100);}bu_append("uF");}
        else{bu_number(v);bu_append("nF");}
    }
}
static void bu_tag(int cx,int y,const BBPart *part,int withvalue)
{
    int w;
    bu_tagtext(part,withvalue);w=bx_twidth(bu_line,bu_tf);
    bu_f(cx-w/2-3,y,cx-w/2+w+2,y+bu_th+1,C_WHITE);bu_frame(cx-w/2-3,y,cx-w/2+w+2,y+bu_th+1,C_BLACK);
    bu_t(cx-w/2,y+1,bu_line,C_BLACK,bu_tf);
}
/* bx_map for a resistor's bands (SPEC.md 130.6.2): cyan, magenta, light blue
 * and light cyan in the art become digit, digit, multiplier, tolerance. */
static void bu_bands(unsigned value)
{
    unsigned x,m,d1,d2;
    x=value;m=1;while(x>=100){x/=10;m++;}
    d1=x/10;d2=x%10;if(m>9)m=9;
    bu_grey(0);
    bx_map[3]=bu_band_col[d1];bx_map[5]=bu_band_col[d2];bx_map[9]=bu_band_col[m];bx_map[11]=C_BROWN;
    if(d1==4||d2==4||m==4){bx_map[C_YELLOW]=C_LBLUE;bx_map[C_BROWN]=C_BLUE;bx_map[11]=C_YELLOW;}
}
static void bu_body(int cx,int cy,int horiz,int idh,int idv)
{
    unsigned sz;int id;
    id=horiz||idv<0?idh:idv;sz=bx_size(id);
    if(sz)bu_s(cx-(int)(sz&255)/2,cy-(int)(sz>>8)/2,id);
    else{bu_f(cx-6,cy-3,cx+6,cy+3,C_WHITE);bu_frame(cx-6,cy-3,cx+6,cy+3,C_BLACK);}
}
static int bu_ledlevel(int idx)
{
    int ua;if(idx<0||!bb_valid||bb_result.diag)return 0;ua=bb_result.ua[idx];
    return ua>100?(ua>=8000?3:ua>=2000?2:1):0;
}
static int ovl_bu_dip(const BBPart *part,int preview)
{
    int n,i,x1,x2,ya,yb,cx;BBWord h;const char *name;
    n=bb_pincount(part->type);x1=32767;x2=-32767;
    for(i=1;i<=n;i++){h=bb_pin(part,i);if(h>=BB_HOLES)return 1;if(bu_hx(h)<x1)x1=bu_hx(h);if(bu_hx(h)>x2)x2=bu_hx(h);}
    ya=bu_hy(180);yb=bu_hy(210);           /* rows e and f */
    for(i=1;i<=n;i++){h=bb_pin(part,i);bu_f(bu_hx(h)-2,bu_hy(h)-1,bu_hx(h)+2,bu_hy(h)+1,preview?C_DGRAY:bu_mono?C_BLACK:C_LGRAY);}
    bu_f(x1-6,ya+2,x2+6,yb-2,preview?C_DGRAY:C_BLACK);
    if(!bu_mono)bu_f(x1-6,ya+2,x2+6,ya+2,C_DGRAY);
    h=bb_pin(part,1);
    if(bu_hx(h)==x1)bu_f(x1-6,(ya+yb)/2-2,x1-4,(ya+yb)/2+2,C_WHITE);else bu_f(x2+4,(ya+yb)/2-2,x2+6,(ya+yb)/2+2,C_WHITE);
    bu_f(bu_hx(h)-1,yb-5,bu_hx(h),yb-4,C_WHITE);
    name=part->type==BB_555?"NE555":part->type==BB_HC00?"74HC00":part->type==BB_HC04?"74HC04":
        part->type==BB_HC08?"74HC08":part->type==BB_HC32?"74HC32":"74HC86";
    cx=(x1+x2)/2;
    if(yb-ya-4>=bu_th)bu_t(cx-bx_twidth(name,bu_tf)/2,(ya+yb+2-bu_th)/2,name,C_WHITE,bu_tf);
    if(!preview&&!bu_compact)bu_tag(cx,ya-bu_th-6,part,0);
    return 1;
}
static int ovl_bu_part(const BBPart *part,int preview)
{
    int n,i,x1,y1,x2,y2,cx,cy,horiz,idx,lvl,lc,top,bottom,id;BBWord h;
    n=bb_pincount(part->type);if(n<1)return 1;
    top=32767;bottom=-32767;x1=32767;x2=-32767;
    for(i=1;i<=n;i++){h=bb_pin(part,i);if(h>=BB_HOLES)return 1;y1=bu_hy(h);if(y1<top)top=y1;if(y1>bottom)bottom=y1;
        if(bu_hx(h)<x1)x1=bu_hx(h);if(bu_hx(h)>x2)x2=bu_hx(h);}
    if(!bu_in(x1-30,top-bu_th-24,x2+30,bottom+bu_th+20))return 1;
    if(preview)bu_grey(1);
    if(n>3){ovl_bu_dip(part,preview);goto selected;}
    h=bb_pin(part,1);x1=bu_hx(h);y1=bu_hy(h);
    h=bb_pin(part,n>1?2:1);x2=bu_hx(h);y2=bu_hy(h);
    cx=(x1+x2)/2;cy=(y1+y2)/2;horiz=bu_abs(x2-x1)>=bu_abs(y2-y1);
    lc=preview?C_DGRAY:bu_mono?C_BLACK:C_DGRAY;
    idx=bu_part_index(part->id);
    if(n==1){
        if(part->type==BB_GROUND){
            bu_f(x1,y1,x1,y1+5,C_BLACK);if(!bu_s(x1-8,y1+4,BA_GROUND)){bu_f(x1-5,y1+6,x1+5,y1+6,C_BLACK);bu_f(x1-3,y1+8,x1+3,y1+8,C_BLACK);}
            goto selected;
        }else if(part->type==BB_CLOCK){
            bu_f(x1,y1-8,x1,y1,lc);bu_box(x1-11,y1-22,x1+10,y1-8,0);bu_s(x1-9,y1-21,BA_CLOCK);
        }else{
            bu_f(x1,y1-6,x1,y1,lc);
            if(part->type==BB_INPUT){id=BA_B_INPUT;}
            else{lvl=4;h=bb_pin(part,1);if(bb_valid&&bb_hnet[h]<BB_NETS)lvl=bb_result.logic[bb_hnet[h]];id=lvl==BB_HIGH?BA_B_LAMPON:BA_B_LAMPOFF;}
            if(!bu_s(x1-5,y1-16,id)){bu_f(x1-5,y1-16,x1+4,y1-7,C_WHITE);bu_frame(x1-5,y1-16,x1+4,y1-7,C_BLACK);}
            bu_begin("");
            if(part->type==BB_INPUT)bu_append(part->param==0?"0":part->param==1?"1":part->param==2?"X":"Z");
            else{if(!bb_valid||bb_hnet[h]>=BB_NETS)bu_append("?");else bu_append(lvl==0?"0":lvl==1?"1":lvl==2?"X":"Z");}
            bu_t(x1+7,y1-16,bu_line,C_BLACK,bu_tf);
        }
        cx=x1;cy=y1-12;goto tags;
    }
    if(part->type==BB_POWER){
        cx=(x1+x2)/2;cy=bu_min(y1,y2)-bu_th-8;
        /* inside the view's own rect: a box above it is drawn by a full
         * paint and missed by every strip that repaints only the tabs */
        if(cy-bu_th/2-2<bu_r2[S_VIEW])cy=bu_r2[S_VIEW]+bu_th/2+2;
        if(y2>y1){bu_wpath(x2,cy,x2,y2,bu_ink(C_BLUE),2);bu_wpath(x1,cy,x1,y1,bu_ink(C_RED),2);}
        else{bu_wpath(x1,cy,x1,y1,bu_ink(C_RED),2);bu_wpath(x2,cy,x2,y2,bu_ink(C_BLUE),2);}
        bu_begin("");if(part->value%1000==0){bu_number(part->value/1000);bu_append(".0");}
        else{bu_number(part->value/1000);bu_append(".");bu_number((part->value%1000)/100);}
        bu_append(" V");i=bx_twidth(bu_line,bu_tf);
        cx-=i/2+4;if(cx<bu_bx+2)cx=bu_bx+2;bu_pbx1=cx;bu_pbx2=cx+i+7;
        bu_box(cx,cy-bu_th/2-2,cx+i+7,cy+bu_th/2+2,0);
        bu_t(cx+4,cy-bu_th/2,bu_line,C_BLACK,bu_tf);
        goto selected;
    }
    /* two/three-lead parts: leads into the body at the midpoint */
    bu_lead(x1,y1,cx,cy,lc);bu_lead(x2,y2,cx,cy,lc);
    if(n==3){h=bb_pin(part,3);bu_lead(bu_hx(h),bu_hy(h),cx,cy,lc);}
    if(part->type==BB_RES){if(!preview)bu_bands(part->value);bu_body(cx,cy,horiz,BA_B_RESH,BA_B_RESV);if(!preview)bu_grey(0);}
    else if(part->type==BB_POT)bu_body(cx,cy,1,BA_B_POT,-1);
    else if(part->type==BB_CAP){
        bu_body(cx,cy-2,1,part->value>=1000?BA_B_ECAP:BA_B_CCAP,-1);
        if(part->value>=1000)bu_t(x1+(x1<=x2?-8:3),y1-bu_th-1,"+",bu_ink(C_RED),bu_tf);
    }else if(part->type==BB_LED){
        lvl=preview?0:bu_ledlevel(idx);
        if(lvl==1&&!bu_mono)bx_map[C_LRED]=C_RED;
        bu_body(cx,cy-3,1,lvl?BA_B_LEDON:BA_B_LEDOFF,-1);bx_map[C_LRED]=C_LRED;
        if(lvl>=2){if(!bu_s(cx-8,cy-22,BA_B_RAYS)){bu_f(cx-8,cy-12,cx-7,cy-10,C_LRED);bu_f(cx+7,cy-12,cx+8,cy-10,C_LRED);}}
        if(!bu_compact&&!preview){bu_t(x1-3,y1+2,"A",C_DGRAY,bu_tf<0?-1:BA_F_TEXT08);bu_t(x2-3,y2+2,"K",C_DGRAY,bu_tf<0?-1:BA_F_TEXT08);}
    }else if(part->type==BB_SWITCH)bu_body(cx,cy,1,part->flags&BB_CLOSED?BA_B_SWON:BA_B_SWOFF,-1);
tags:
    if(!preview&&!bu_compact){
        bu_tag(cx,cy-(n==1?14:9)-bu_th-2,part,0);
        if(bu_lay==BU_FULL&&(part->type==BB_RES||part->type==BB_CAP)&&part->id==bu_selected)bu_tag(cx,cy+8,part,1);
    }
selected:
    if(preview)bu_grey(0);
    if(!preview&&part->id==bu_selected&&bu_selected_kind==BB_PART){
        x1=32767;x2=-32767;top=32767;bottom=-32767;
        for(i=1;i<=n;i++){h=bb_pin(part,i);if(bu_hx(h)<x1)x1=bu_hx(h);if(bu_hx(h)>x2)x2=bu_hx(h);if(bu_hy(h)<top)top=bu_hy(h);if(bu_hy(h)>bottom)bottom=bu_hy(h);}
        x1-=8;x2+=8;top-=n>3?8:12;bottom+=n>3?8:6;
        bu_dots(x1,top,x2,top,2,C_BLACK);bu_dots(x1,bottom,x2,bottom,2,C_BLACK);
        bu_dots(x1,top,x1,bottom,2,C_BLACK);bu_dots(x2,top,x2,bottom,2,C_BLACK);
    }
    return 1;
}
static int ovl_bu_extents(void)
{
    unsigned i,k,n;int top,bottom,y;BBWord h;
    for(i=0;i<bb_doc.np;i++){
        top=32767;bottom=-32767;n=bb_pincount(bb_doc.part[i].type);
        for(k=1;k<=n;k++){h=bb_pin(&bb_doc.part[i],k);if(h<BB_HOLES){y=bu_hy(h);if(y<top)top=y;if(y>bottom)bottom=y;}}
        BU_PEXT[i+i]=top-bu_th-26;BU_PEXT[i+i+1]=bottom+bu_th+22;
    }
    for(i=0;i<bb_doc.nw;i++){
        top=bu_min(bu_hy(bb_doc.wire[i].a),bu_hy(bb_doc.wire[i].b));bottom=bu_max(bu_hy(bb_doc.wire[i].a),bu_hy(bb_doc.wire[i].b));
        for(k=0;k<bb_doc.wire[i].nbend;k++){h=bb_doc.wire[i].bend[k*2+1]*30+bb_doc.wire[i].bend[k*2];y=bu_hy(h);if(y<top)top=y;if(y>bottom)bottom=y;}
        BU_WEXT[i+i]=top-6;BU_WEXT[i+i+1]=bottom+6;
    }
    return 1;
}
static int ovl_bu_board(void)
{
    int row,col,y,x,hx1,hx2,c,sel,net,srow1,srow2,scol1,scol2,lx;BBWord h;unsigned i;
    int bx1,by1,bx2,by2;
    /* the section's rect, not the hole area's: the column numbers and the
     * supply's box stand above bu_by */
    if(!bu_keying&&!bu_in(bu_r1[S_VIEW],bu_r2[S_VIEW],bu_r3[S_VIEW],bu_r4[S_VIEW]))return 1;
    bx1=bu_brx1;bx2=bu_brx2;by1=bu_bry1;by2=bu_bry2;
    /* the board itself: cream (white with one yellow pixel in four) */
    bu_frame(bx1,by1,bx2,by2,C_BLACK);
    if(bu_mono)bu_f(bx1+1,by1+1,bx2-1,by2-1,C_WHITE);else bu_d(bx1+1,by1+1,bx2-1,by2-1,C_WHITE,C_YELLOW,2);
    /* column numbers, except under the supply's box */
    y=by1-bu_th-2;bu_pbx1=bu_pbx2=-1;
    for(i=0;i<bb_doc.np;i++)if(bb_doc.part[i].type==BB_POWER){bu_keying+=2;ovl_bu_part(&bb_doc.part[i],0);bu_keying-=2;break;}
    for(col=bb_doc.panx;col<30;col++)if(!col||(col+1)%5==0){
        x=bu_hx(col);if(x>bx2)break;bu_begin("");bu_number(col+1);
        hx2=bx_twidth(bu_line,bu_tf);hx1=x-hx2/2;if(hx1+hx2>bx2)hx1=bx2-hx2;  /* "30" stays on the board */
        if(hx1+hx2>=bu_pbx1-2&&hx1<=bu_pbx2+2)continue;
        bu_t(hx1,y,bu_line,C_BLACK,bu_tf);
    }
    /* trench */
    if(bb_doc.pany<=6){y=bu_hy(180)+bu_py/2+1;bu_d(bx1+6,y,bx2-6,y+bu_py-3,C_LGRAY,C_WHITE,0);}
    /* the selected hole's strip and net */
    sel=-1;net=BB_NNET;srow1=srow2=scol1=scol2=0;
    if(bu_hole<BB_HOLES){
        sel=bu_hole;row=sel/30;col=sel%30;
        if(row<2||row>11){srow1=srow2=row;scol1=col<15?0:15;scol2=scol1+14;}
        else{srow1=row<7?2:7;srow2=srow1+4;scol1=scol2=col;}
        if(!bb_recompile)net=bb_hnet[sel];
        if(bu_netshow&&net<BB_NETS)for(i=0;i<BB_HOLES;i++)if(bb_hnet[i]==net&&(int)(i/30)>=(int)bb_doc.pany){
            row=i/30;col=i%30;if(row>=srow1&&row<=srow2&&col>=scol1&&col<=scol2)continue;
            x=bu_hx(i);y=bu_hy(i);if(!bu_in(x-4,y-4,x+4,y+4)&&!bu_keying)continue;
            if(bu_mono)bu_frame(x-3,y-3,x+3,y+3,C_BLACK);else bu_f(x-3,y-3,x+3,y+3,C_LCYAN);
        }
        if(srow1>=(int)bb_doc.pany){
            x=bu_hx(srow1*30+scol1)-4;y=bu_hy(srow1*30+scol1)-4;hx2=bu_hx(srow2*30+scol2)+4;lx=bu_hy(srow2*30+scol2)+4;
            if(bu_mono)bu_frame(x,y,hx2,lx,C_BLACK);else{bu_f(x,y,hx2,lx,C_LGREEN);bu_frame(x,y,hx2,lx,C_GREEN);}
        }
    }
    /* rows: labels, rails, holes */
    for(row=bb_doc.pany;row<14;row++){
        y=bu_hy(row*30);if(y-bu_py>bu_sy2&&!bu_keying)break;
        if(!bu_in(bx1,y-bu_py,bx2,y+bu_py)&&!bu_keying)continue;
        lx=bu_gx-14;
        if(row<2||row>11){
            c=row==0||row==12?C_RED:C_BLUE;
            bu_t(lx,y-bu_th/2,row==0||row==12?"+":"-",bu_ink(c),bu_tf);
            hx1=bu_hx(row*30);hx2=bu_hx(row*30+14)+4;
            if(bb_doc.panx<=14)bu_f(hx1-4,y-1,hx2,y,bu_ink(c));
            hx1=bu_hx(row*30+15)-4;hx2=bu_hx(row*30+29)+4;
            bu_f(hx1,y-1,hx2>bx2-2?bx2-2:hx2,y,bu_ink(c));
        }else{
            static char letter[2];letter[0]='a'+row-2;letter[1]=0;
            bu_t(lx,y-bu_th/2,letter,C_BLACK,bu_tf);
        }
        hx1=bu_hx(row*30+bb_doc.panx);
        hx2=bu_hx(row*30+29);if(hx2>bx2-4)hx2=bx2-4;
        bu_dots(hx1-1,y-1,hx2-1,y-1,bu_pitch,C_BLACK);bu_dots(hx1,y-1,hx2,y-1,bu_pitch,C_BLACK);
        if(!bu_compact){bu_dots(hx1-1,y,hx2-1,y,bu_pitch,C_BLACK);bu_dots(hx1,y,hx2,y,bu_pitch,C_BLACK);}
    }
    /* every part's and wire's rows once per paint, so a strip asks two
     * compares of each and draws only what crosses it */
    if(bu_bdgen!=bu_paintgen){bu_bdgen=bu_paintgen;ovl_bu_extents();}
    for(i=0;i<bb_doc.nw;i++)if(BU_WEXT[i+i]<=bu_sy2&&BU_WEXT[i+i+1]>=bu_sy1)ovl_bu_wire(&bb_doc.wire[i],0);
    for(i=0;i<bb_doc.np;i++)if(BU_PEXT[i+i]<=bu_sy2&&BU_PEXT[i+i+1]>=bu_sy1)ovl_bu_part(&bb_doc.part[i],0);
    if(bu_ready&&bu_tool==BU_PART)ovl_bu_part(&bu_part,1);
    if(bu_ready&&bu_tool==BU_WIRE)ovl_bu_wire(&bu_wire,1);
    if(bu_hole<BB_HOLES&&bu_visible(bu_hole)){
        x=bu_hx(bu_hole);y=bu_hy(bu_hole);bu_frame(x-3,y-3,x+3,y+3,C_BLACK);
    }
    return 1;
}

/* ---- net list ------------------------------------------------------------- */
/* One net as three columns: name, volts, and every part pin on it. */
static int ovl_bu_netline(unsigned net,int x1,int x2,int y)
{
    unsigned i,p,n;BBWord h;int mv,c;
    bu_begin("N");bu_number(net+1);bu_t(x1+8,y,bu_line,C_BLACK,bu_tf);
    mv=bb_valid&&!bb_result.diag?bb_result.mv[net]:BB_MV_FAILED;bu_begin("");c=C_BLACK;
    if(mv>=0){bu_number(mv/1000);bu_append(".");if(mv%1000<100)bu_append("0");bu_number((mv%1000)/10);bu_append(" V");}
    else{bu_append(mv==BB_MV_FLOAT?"float":"--");c=C_DGRAY;}
    bu_t(x1+44,y,bu_line,c,bu_tf);
    bu_begin("");
    for(i=0;i<bb_doc.np;i++){
        n=bb_pincount(bb_doc.part[i].type);
        for(p=1;p<=n;p++){h=bb_pin(&bb_doc.part[i],p);if(h<BB_HOLES&&bb_hnet[h]==net){bu_ident(&bb_doc.part[i]);bu_append(".");bu_number(p);bu_append(" ");}}
    }
    bu_fitw(bu_line,x2-x1-112);bu_t(x1+104,y,bu_line,C_BLACK,bu_tf);
    return 1;
}
static int ovl_bu_nets(int x1,int y1,int x2,int y2)
{
    int y,row;unsigned net;
    y=y1+4;
    if(bb_recompile){bu_t(x1+8,y,"Nets are listed after Run or Step.",C_BLACK,bu_tf);return 1;}
    bu_f(x1+2,y-2,x2-2,y+bu_th+1,bu_mono?C_WHITE:C_LGRAY);
    bu_t(x1+8,y,"Net",C_BLACK,bu_tf);bu_t(x1+44,y,"Volts",C_BLACK,bu_tf);
    bu_begin("Connections (");bu_number(bb_nnet);bu_append(" nets)");bu_t(x1+104,y,bu_line,C_BLACK,bu_tf);
    bu_f(x1+2,y+bu_th+2,x2-2,y+bu_th+2,C_BLACK);y+=bu_th+5;
    if(bu_netscroll>=(int)bb_nnet)bu_netscroll=0;
    for(net=bu_netscroll,row=0;net<bb_nnet&&y+bu_th<=y2;net++,row++,y+=bu_th+2){
        if(!bu_in(x1,y,x2,y+bu_th)&&!bu_keying)continue;
        if(net==(bu_hole<BB_HOLES&&!bb_recompile?bb_hnet[bu_hole]:BB_NNET))bu_f(x1+2,y-1,x2-2,y+bu_th,bu_mono?C_WHITE:C_LCYAN);
        ovl_bu_netline(net,x1,x2,y);
    }
    if(net<bb_nnet)bu_t(x2-bx_twidth("PgDn: more",bu_tf)-8,y2-bu_th-2,"PgDn: more",C_DGRAY,bu_tf);
    return 1;
}

/* ---- help and examples pages --------------------------------------------- */
static int ovl_bu_help(int x1,int y1,int x2,int y2)
{
    int i,y,rows,start,total;
    rows=(y2-y1-bu_th-12)/(bu_th+2);if(rows<1)rows=1;if(rows>8)rows=8;
    total=(39/rows)+1;if(bu_help_page>=total)bu_help_page=0;
    start=bu_help_page*rows;y=y1+4;
    if(!bx_artstr(BA_HELP,0,bu_hl)){bu_t(x1+8,y,"Help is in HELP.TXT (no memory for the art).",C_BLACK,bu_tf);return 1;}
    for(i=0;i<rows&&bx_artstr(BA_HELP,start+i,bu_hl);i++){bu_t(x1+8,y,bu_hl,i==0&&start%8==0?C_BLUE:C_BLACK,bu_tf);y+=bu_th+2;}
    bu_t(x1+8,y2-bu_th-3,"Enter or click: next page   Esc: board",C_DGRAY,bu_tf);
    (void)x2;return 1;
}
static int ovl_bu_examples(int x1,int y1,int x2,int y2)
{
    int i,y;
    bu_t(x1+8,y1+4,"Examples - click one, or press 1-8:",C_BLACK,bu_tf);
    for(i=0;i<8;i++){
        y=y1+8+(i+1)*(bu_th+3);if(y+bu_th>y2)break;
        bu_begin("");bu_number(i+1);bu_append("  ");bu_append(bu_ex_names[i]);bu_append("  (");bu_append(bb_example_name(i+1));bu_append(")");
        bu_t(x1+16,y,bu_line,C_BLACK,bu_tf);
    }
    (void)x2;return 1;
}

/* ---- scope ------------------------------------------------------------------ */
static void bu_channel_name(unsigned channel)
{
    int i;
    bu_begin("");
    if(bb_doc.probe_kind[channel]==BB_PROBE_HOLE){if(bb_doc.probe[channel]<BB_HOLES){bu_hole_name(bb_doc.probe[channel]);if(bu_line[0]>='a'&&bu_line[0]<='j')bu_line[0]-=32;}else bu_append("--");return;}
    i=bu_part_index(bb_doc.probe_id[channel]);
    if(i<0){bu_append("--");return;}
    if(bb_doc.probe_kind[channel]==BB_PROBE_BRANCH){bu_append("I ");bu_ident(&bb_doc.part[i]);return;}
    bu_ident(&bb_doc.part[i]);bu_append(" ");
    if(bb_doc.part[i].type==BB_555||bb_doc.part[i].type>=BB_HC00&&bb_doc.part[i].type<=BB_HC86||bb_doc.part[i].type==BB_LED)
        ovl_bu_pin_caption(bb_doc.part[i].type,bb_doc.probe_pin[channel]);
    else{bu_append("pin ");bu_number(bb_doc.probe_pin[channel]);}
}
static int ovl_bu_s_shead(void)
{
    int x1,y1,x2,bx;
    x1=bu_r1[S_SHEAD];y1=bu_r2[S_SHEAD];x2=bu_r3[S_SHEAD];
    bu_channel_name(0);os88_strcpy(bu_title_scope,"Oscilloscope - ",sizeof(bu_title_scope));
    os88_strcpy(bu_title_scope+15,bu_line,sizeof(bu_title_scope)-15);
    bu_header(x1+1,y1,x2-1,bu_title_scope);     /* its rule is the frame's bottom */
    bu_frame(x1,y1,x2,y1+bu_th+4,C_BLACK);
    bx=x2-bu_th-4;bu_box(bx,y1+2,x2-3,y1+bu_th+2,0);
    bu_f(bx+3,y1+bu_th/2+2,x2-6,y1+bu_th/2+2,C_BLACK);
    if(!bu_scope_on)bu_f((bx+x2-3)/2,y1+5,(bx+x2-3)/2,y1+bu_th-1,C_BLACK);
    return 1;
}
static int bu_scope_x(unsigned idx,unsigned lo,unsigned hi,unsigned spanlo,unsigned spanhi,int w)
{
    unsigned distlo,disthi,sl,sh;
    distlo=lo-bb_scope.lo[idx];disthi=hi-bb_scope.hi[idx]-(lo<bb_scope.lo[idx]);
    if(disthi>spanhi||(disthi==spanhi&&distlo>spanlo))return -1;
    sl=spanlo;sh=spanhi;
    while(sh){sl=(sl>>1)|(sh<<15);sh>>=1;distlo=(distlo>>1)|(disthi<<15);disthi>>=1;}
    return w-1-bb_muldiv(distlo,w-1,sl?sl:1);
}
static int ovl_bu_scope(int x1,int y1,int x2,int y2)
{
    int px1,px2,py1,py2,w,h,i,j,ch,idx,px,py,ox,oy,have,mv,c,t,lw;
    unsigned newest,lo,hi,spanlo,spanhi;
    if(!bu_keying&&!bu_in(x1,y1,x2,y2))return 1;
    bu_f(x1,y1,x2,y2,C_BLACK);
    px1=x1+(bu_compact?28:34);px2=x2-8;py1=y1+bu_th+4;py2=y2-bu_th-5;
    w=px2-px1+1;h=py2-py1+1;if(w<20||h<8)return 1;
    c=bu_mono?C_WHITE:C_GREEN;
    for(i=0;i<=10;i++)bu_dots(px1+i*(w-1)/10,py1,px1+i*(w-1)/10,py2,i%5?3:2,c);
    for(i=0;i<=6;i++)bu_dots(px1,py2-i*(h-1)/6,px2,py2-i*(h-1)/6,i==0||i==5?2:4,c);
    bu_t(x1+4,py2-(h-1)*5/6-bu_th/2,"5 V",C_WHITE,bu_tf);bu_t(x1+4,py2-bu_th/2,"0 V",C_WHITE,bu_tf);
    /* legend */
    lw=x2-6;
    for(ch=1;ch>=0;ch--){
        bu_channel_name(ch);t=bx_twidth(bu_line,bu_tf);lw-=t;
        bu_t(lw,y1+2,bu_line,ch?C_YELLOW:C_LGREEN,bu_tf);lw-=18;
        if(ch&&bu_mono)bu_dots(lw,y1+2+bu_th/2,lw+12,y1+2+bu_th/2,2,C_WHITE);
        else bu_f(lw,y1+1+bu_th/2,lw+12,y1+2+bu_th/2,ch?C_YELLOW:C_LGREEN);
        lw-=12;
    }
    bu_begin(!bb_valid?"STALE":bb_running?"RUN ":"HOLD ");
    if(bb_valid)bu_append(bb_doc.trigger==1?"rise":bb_doc.trigger==2?"fall":"free");
    bu_t(x1+4,y1+2,bu_line,C_WHITE,bu_tf);
    bu_begin("");bu_number(bb_doc.scope_div/10);if(bb_doc.scope_div%10){bu_append(".");bu_number(bb_doc.scope_div%10);}bu_append(" ms/div");
    bu_t((px1+px2)/2-bx_twidth(bu_line,bu_tf)/2,y2-bu_th-2,bu_line,C_WHITE,bu_tf);
    if(bb_scope.undersampled)bu_t(px1+2,y2-bu_th-2,"UNDERSAMPLED",C_LRED,bu_tf);
    else if(bb_scope.count&&!bb_scope.cycles)bu_t(px1+2,y2-bu_th-2,"need cycles",C_LGRAY,bu_tf);
    if(bu_keying){bu_mix(bb_scope.count);bu_mix(bb_scope.head);return 1;}
    if(!bb_scope.count||!bu_in(px1,py1,px2,py2))return 1;
    if(h>255)h=255;
    /* The trace's points once per paint, not once per strip: every strip of
     * this paint then only walks them. bb_stage is free between commands -
     * UI callbacks are serialized, and no command spans a paint. */
    if(bu_ptgen!=bu_paintgen){
        bu_ptgen=bu_paintgen;
        newest=(bb_scope.head+255)&255;lo=bb_scope.lo[newest];hi=bb_scope.hi[newest];
        spanlo=bb_doc.scope_div;spanhi=0;
        for(i=1;i<10;i++){spanlo+=bb_doc.scope_div;if(spanlo<bb_doc.scope_div)spanhi++;}
        for(j=0;j<(int)bb_scope.count;j++){
            idx=(bb_scope.head+256-bb_scope.count+j)&255;
            BU_PTX[j]=bu_scope_x(idx,lo,hi,spanlo,spanhi,w);
            for(ch=0;ch<2;ch++){mv=bb_scope.mv[ch][idx];BU_PTY[(ch<<8)+j]=mv<0||mv>6000?255:h-1-bb_muldiv(mv,h-1,6000);}
        }
    }
    t=bu_lay==BU_FULL?2:1;
    for(ch=1;ch>=0;ch--){
        c=ch?C_YELLOW:C_LGREEN;have=0;ox=oy=0;
        for(j=0;j<(int)bb_scope.count;j++){
            px=BU_PTX[j];if(px<0){have=0;continue;}
            py=BU_PTY[(ch<<8)+j];if(py==255){have=0;continue;}
            px+=px1;py+=py1;
            if(have&&(oy<bu_sy1&&py<bu_sy1||oy>bu_sy2+1&&py>bu_sy2+1)){ox=px;oy=py;continue;}
            if(have){
                if(ch&&bu_mono){bx_dots(ox,oy,px,oy,2,C_WHITE);bx_dots(px,bu_min(oy,py),px,bu_max(oy,py),2,C_WHITE);}
                else{bx_fill(ox,oy,px+t-1,oy+t-1,c);bx_fill(px,bu_min(oy,py),px+t-1,bu_max(oy,py)+t-1,c);}
            }else bx_fill(px,py,px+t-1,py+t-1,c);
            ox=px;oy=py;have=1;
        }
    }
    return 1;
}

/* ---- inspector, probe and circuit ---------------------------------------- */
static void bu_valtext(const BBPart *part)
{
    unsigned v;v=part->value;bu_begin("");
    if(part->type==BB_RES||part->type==BB_POT){
        if(v<100){bu_number(v*10);bu_append(" ohm");}
        else{bu_number(v/100);if(v%100){bu_append(".");bu_number((v%100)/10);if(v%10)bu_number(v%10);}bu_append(" kohm");}
    }else if(part->type==BB_CAP){
        if(v>=1000){bu_number(v/1000);if(v%1000){bu_append(".");bu_number((v%1000)/100);}bu_append(" uF");}
        else{bu_number(v);bu_append(" nF");}
    }else if(part->type==BB_POWER){bu_number(v/1000);bu_append(".");bu_number((v%1000)/100);bu_append(" V");}
    else if(part->type==BB_CLOCK){if(v>=10){bu_number(v/10);bu_append(" ms period");}else{bu_number(v*100);bu_append(" us period");}}
    else if(part->type==BB_SWITCH)bu_append(part->flags&BB_CLOSED?"Closed":"Open");
    else if(part->type==BB_INPUT)bu_append(part->param==0?"Logic 0":part->param==1?"Logic 1":part->param==2?"X":"Z");
    else if(part->type==BB_LED)bu_append("Anode 1, cathode 2");
    else if(part->type==BB_GROUND)bu_append("0 V reference");
    else if(part->type==BB_INDICATOR)bu_append("Logic lamp");
    else if(part->type==BB_555)bu_append("NE555P timer");
    else{bu_append("SN");bu_append(bb_part_name(part->type));}
}
static int bu_has_value(unsigned type)
{return type==BB_RES||type==BB_POT||type==BB_CAP||type==BB_POWER||type==BB_CLOCK;}
static int bu_toggles(unsigned type){return type==BB_SWITCH||type==BB_INPUT;}
static int ovl_bu_s_insp(void)
{
    int x1,y1,x2,y2,i,y,cx,sz,id,w;BBPart *part;
    x1=bu_r1[S_INSP];y1=bu_r2[S_INSP];x2=bu_r3[S_INSP];y2=bu_r4[S_INSP];
    i=bu_selected_kind==BB_PART?bu_part_index(bu_selected):-1;part=i>=0?&bb_doc.part[i]:0;
    bu_begin("Inspector");
    if(part){bu_begin("");bu_ident(part);bu_append(" ");bu_append(bu_type_names[part->type]);}
    else if(bu_selected_kind==BB_WIRE){bu_begin("Wire ");bu_number(bu_selected);}
    bu_fitw(bu_line,x2-x1-14);bu_header(x1,y1,x2,bu_line);
    y=y1+bu_th+6;cx=(x1+x2)/2;
    if(!part){
        if(bu_selected_kind==BB_WIRE){
            i=bu_wire_index(bu_selected);
            bu_t(x1+6,y+2,"Color:",C_BLACK,bu_tf);
            if(i>=0){bu_f(x1+52,y+bu_th/2,x2-8,y+bu_th/2+2,bu_ink(bb_doc.wire[i].color));}
            bu_t(x1+6,bu_ibtn+2,"Edit > Wire color",C_DGRAY,bu_tf);
        }else{
            bu_t(x1+6,y+2,"Select a part",C_BLACK,bu_tf);
            bu_t(x1+6,y+bu_th+4,"or pick one from",C_DGRAY,bu_tf);
            bu_t(x1+6,y+2*bu_th+6,"Parts.",C_DGRAY,bu_tf);
        }
        return 1;
    }
    if(!bu_compact){
        id=part->type==BB_RES?BA_I_RES:bu_type_icon[part->type];sz=bx_size(id);
        if(sz&&(int)(sz>>8)<bu_ival-y-2){
            if(part->type==BB_RES)bu_bands(part->value);
            bu_s(cx-(int)(sz&255)/2,(y+bu_ival-(int)(sz>>8))/2,id);bu_grey(0);
        }
    }
    /* value box: the editor when E is up, else the value and a spinner */
    y=bu_ival;
    bu_box(x1+6,y,x2-6,y+bu_th+4,0);
    if(bu_mode==BU_VALUE){
        bu_begin(bu_edit+(bu_edit_len>14?bu_edit_len-14:0));bu_append("_");
        bu_t(x1+10,y+2,bu_line,C_BLACK,bu_tf);
        bu_t(x1+6,bu_ibtn+2,bu_edit_param?"Parameter; Enter":"Value; Enter accepts",C_DGRAY,bu_tf);
        return 1;
    }
    bu_valtext(part);
    if(bu_has_value(part->type)){
        w=x2-6-12;bu_f(w,y,w,y+bu_th+4,C_BLACK);
        bu_tri(w+3,y+2,0);bu_tri(w+3,y+bu_th-1,1);
        bu_fitw(bu_line,w-x1-14);
    }else bu_fitw(bu_line,x2-x1-18);
    bu_t(x1+10,y+2,bu_line,C_BLACK,bu_tf);
    /* Rotate, and Toggle for parts that switch */
    y=bu_ibtn;
    if(bu_toggles(part->type)){
        bu_box(x1+6,y,cx-3,y+bu_th+4,1);bu_t((x1+6+cx-3)/2-bx_twidth("Rotate",bu_tf)/2,y+2,"Rotate",C_BLACK,bu_tf);
        bu_box(cx+3,y,x2-7,y+bu_th+4,1);bu_t((cx+3+x2-7)/2-bx_twidth("Toggle",bu_tf)/2,y+2,"Toggle",C_BLACK,bu_tf);
    }else{
        bu_box(x1+14,y,x2-15,y+bu_th+4,1);bu_t(cx-bx_twidth("Rotate",bu_tf)/2,y+2,"Rotate",C_BLACK,bu_tf);
    }
    if(!bu_compact)bu_dots(x1,y2,x2,y2,2,C_BLACK);
    return 1;
}
static int ovl_bu_readout(void)
{
    int mv,ua,i;
    bu_begin("");
    if(!bb_valid){bu_append("stale");return 1;}
    if(bb_doc.probe_kind[bu_channel]==BB_PROBE_BRANCH){
        i=bu_part_index(bb_doc.probe_id[bu_channel]);
        if(i<0||bb_result.diag||bb_result.ua[i]==BB_UA_INVALID){bu_append("failed");return 1;}
        ua=bb_result.ua[i];if(ua<0){bu_append("-");ua=-ua;}
        if(ua>=1000){bu_number(ua/1000);bu_append(".");if(ua%1000<100)bu_append("0");bu_number((ua%1000)/10);bu_append(" mA");}
        else{bu_number(ua);bu_append(" uA");}
        return 1;
    }
    mv=ovl_bu_probe_mv(bu_channel);
    if(mv==BB_MV_FLOAT){bu_append("floating");return 1;}
    if(mv==BB_MV_UNKNOWN){bu_append("X");return 1;}
    if(mv<0){bu_append("failed");return 1;}
    bu_number(mv/1000);bu_append(".");if(mv%1000<100)bu_append("0");bu_number((mv%1000)/10);bu_append(" V");
    return 1;
}
static int ovl_bu_s_probe(void)
{
    int x1,y1,x2,y2,y,bh,w;
    x1=bu_r1[S_PROBE];y1=bu_r2[S_PROBE];x2=bu_r3[S_PROBE];y2=bu_r4[S_PROBE];
    bu_header(x1,y1,x2,"Probe");
    y=bu_pbox;bu_box(x1+6,y,x2-6,y+bu_th+5,1);
    bu_channel_name(bu_channel);os88_strcpy(bu_title_scope,bu_channel?"B: ":"A: ",4);
    os88_strcpy(bu_title_scope+3,bu_line,sizeof(bu_title_scope)-3);
    os88_strcpy(bu_line,bu_title_scope,sizeof(bu_line));bu_fitw(bu_line,x2-x1-34);
    bu_t(x1+10,y+3,bu_line,C_BLACK,bu_tf);bu_tri(x2-20,y+bu_th/2,1);
    (void)bh;(void)w;
    if(!bu_compact)bu_dots(x1,y2,x2,y2,2,C_BLACK);
    return 1;
}
static int ovl_bu_s_pread(void)
{
    int x1,y1,x2,y2,w;
    x1=bu_r1[S_PREAD];y1=bu_r2[S_PREAD];x2=bu_r3[S_PREAD];y2=bu_r4[S_PREAD];
    bu_dg(x1,y1,x2,y2);bu_frame(x1,y1,x2,y2,C_DGRAY);
    ovl_bu_readout();w=bx_twidth(bu_line,bu_bf);
    bu_t((x1+x2)/2-w/2,y1+3,bu_line,C_BLACK,bu_bf);
    return 1;
}
static int ovl_bu_s_circ(void)
{
    int x1,y1,x2,y,vx,i,row;unsigned k;
    x1=bu_r1[S_CIRC];y1=bu_r2[S_CIRC];x2=bu_r3[S_CIRC];
    bu_header(x1,y1,x2,"Circuit");
    y=y1+bu_th+(bu_compact?6:10);vx=x1+(bu_compact?56:68);
    for(row=0;row<5;row++,y+=bu_th+(bu_compact?1:4)){
        if(y+bu_th>bu_r4[S_CIRC])break;
        bu_begin("");
        if(row==0){bu_t(x1+8,y,"Parts",C_BLACK,bu_tf);bu_number(bb_doc.np);}
        else if(row==1){bu_t(x1+8,y,"Nets",C_BLACK,bu_tf);if(bb_recompile)bu_append("--");else bu_number(bb_nnet);}
        else if(row==2){bu_t(x1+8,y,"Supply",C_BLACK,bu_tf);
            for(k=0;k<bb_doc.np;k++)if(bb_doc.part[k].type==BB_POWER)break;
            if(k<bb_doc.np){bu_number(bb_doc.part[k].value/1000);bu_append(".");bu_number((bb_doc.part[k].value%1000)/100);bu_append(" V");}else bu_append("none");}
        else if(row==3){bu_t(x1+8,y,"Clock",C_BLACK,bu_tf);
            if(bb_scope.cycles&&bb_scope.period_hi==0&&bb_scope.period_lo){
                bu_number(10000/bb_scope.period_lo);bu_append(".");i=bb_muldiv(10000%bb_scope.period_lo,100,bb_scope.period_lo);if(i<10)bu_append("0");bu_number(i);bu_append(" Hz");
            }else bu_append(bb_scope.period_hi?"long":"--");}
        else{bu_t(x1+8,y,"Duty",C_BLACK,bu_tf);
            if(bb_scope.cycles&&bb_scope.period_hi==0&&bb_scope.period_lo&&bb_scope.high_hi==0){bu_number(bb_muldiv(bb_scope.high_lo,100,bb_scope.period_lo));bu_append("%");}
            else bu_append("--");}
        bu_t(vx,y,bu_line,C_BLACK,bu_tf);
    }
    (void)x2;return 1;
}

/* ---- the scene ------------------------------------------------------------ */
static int ovl_bu_s_view(void)
{
    int x1,y1,x2,y2;
    x1=bu_r1[S_VIEW];y1=bu_r2[S_VIEW];x2=bu_r3[S_VIEW];y2=bu_r4[S_VIEW];
    if(bu_compact){
        if(bu_mode==BU_BOARD&&bu_view==BU_VBOARD)bu_ring(x1,y1,x2,y2,bu_brx1,bu_bry1,bu_brx2,bu_bry2,1);
        else bu_f(x1,y1,x2,y2,C_WHITE);
    }
    if(bu_mode==BU_HELP)return ovl_bu_help(x1,y1,x2,y2);
    if(bu_mode==BU_EXAMPLES)return ovl_bu_examples(x1,y1,x2,y2);
    if(bu_view==BU_VNETS)return ovl_bu_nets(x1,y1,x2,y2);
    if(bu_view==BU_VSCOPE)return ovl_bu_scope(x1+2,y1+2,x2-2,y2-2);
    return ovl_bu_board();
}
static int ovl_bu_section(int s)
{
    if(s==S_TBAR)return ovl_bu_s_tbar();
    if(s>=S_BTN&&s<S_BTN+9)return ovl_bu_s_btn(s-S_BTN);
    if(s==S_STATE)return ovl_bu_s_state();
    if(s==S_CTABS)return ovl_bu_s_ctabs();
    if(s==S_LEFT)return ovl_bu_s_left();
    if(s==S_CBOX)return ovl_bu_s_cbox();
    if(s==S_TABS)return ovl_bu_s_tabs();
    if(s==S_VIEW)return ovl_bu_s_view();
    if(s==S_HOLE)return ovl_bu_s_hole();
    if(s==S_SHEAD)return ovl_bu_s_shead();
    if(s==S_SCOPE)return ovl_bu_scope(bu_r1[S_SCOPE],bu_r2[S_SCOPE],bu_r3[S_SCOPE],bu_r4[S_SCOPE]);
    if(s==S_RBOX){
        if(bu_compact){bu_f(bu_r1[s],bu_r2[s],bu_r3[s],bu_r4[s],C_WHITE);return 1;}
        bu_ring(bu_r1[s],bu_r2[s],bu_r3[s],bu_r4[s],bu_r1[s]+4,bu_r2[s]+4,bu_r3[s]-4,bu_r4[s]-4,0);
        bu_f(bu_r1[s]+5,bu_r2[s]+5,bu_r3[s]-5,bu_r4[s]-5,C_WHITE);bu_frame(bu_r1[s]+4,bu_r2[s]+4,bu_r3[s]-4,bu_r4[s]-4,C_BLACK);return 1;}
    if(s==S_INSP)return ovl_bu_s_insp();
    if(s==S_PROBE)return ovl_bu_s_probe();
    if(s==S_CIRC)return ovl_bu_s_circ();
    if(s==S_STATUS)return ovl_bu_s_status();
    if(s==S_STIME)return ovl_bu_s_stime();
    if(s==S_SMSG)return ovl_bu_s_smsg();
    if(s==S_PREAD)return ovl_bu_s_pread();
    return 1;
}
static int ovl_bu_scene(void)
{
    int s;
    /* no ground here: every pixel of the window belongs to a section, and
     * each section lays its own ground (bu_ring) - none is composed twice */
    for(s=0;s<S_N;s++)
        if(bu_r1[s]<=bu_r3[s]&&bu_in(bu_r1[s],bu_r2[s],bu_r3[s],bu_r4[s])){
#ifdef BB_HOST
            bu_host_sec=s;
#endif
            ovl_bu_section(s);
        }
#ifdef BB_HOST
    bu_host_sec=-1;
#endif
    return 1;
}
/* Repaint a rect of the glass, in strips. */
static int ovl_bu_paint(int x1,int y1,int x2,int y2)
{
    int rows,w,h;
    if(x1<bu_x)x1=bu_x;if(y1<bu_y)y1=bu_y;if(x2>bu_x+bu_w-1)x2=bu_x+bu_w-1;if(y2>bu_y+bu_h-1)y2=bu_y+bu_h-1;
    if(x1>x2||y1>y2)return 1;
    if(bu_paint_region){
        if(x1<bu_damage.x1)x1=bu_damage.x1;if(y1<bu_damage.y1)y1=bu_damage.y1;
        if(x2>bu_damage.x2)x2=bu_damage.x2;if(y2>bu_damage.y2)y2=bu_damage.y2;
        if(x1>x2||y1>y2)return 1;
    }
    x1&=~7;x2|=7;w=x2-x1+1;rows=bx_rows(x1,y1,w,y2-y1+1);if(rows<1)return 1;
    bu_paintgen++;
    bu_keying=0;
    while(y1<=y2){
        h=y2-y1+1;if(h>rows)h=rows;
        bu_sx1=x1;bu_sy1=y1;bu_sx2=x2;bu_sy2=y1+h-1;
        bx_new(x1,y1,w,h,-1);ovl_bu_scene();bx_out();
        y1+=h;
    }
    return 1;
}
static int ovl_bu_paint_s(int s){if(bu_r1[s]<=bu_r3[s])ovl_bu_paint(bu_r1[s],bu_r2[s],bu_r3[s],bu_r4[s]);return 1;}
static BBWord ovl_bu_skey(int s)
{
    if(bu_r1[s]>bu_r3[s])return 1;
    bu_keying=1;bu_hash=s*77;
    bu_sx1=bu_r1[s];bu_sy1=bu_r2[s];bu_sx2=bu_r3[s];bu_sy2=bu_r4[s];
    ovl_bu_section(s);bu_keying=0;bu_grey(0);
    return bu_hash==BB_NONE?1:bu_hash;
}
static int bu_keyed(int s)
{
    if(s==S_TBAR||s==S_CBOX||s==S_RBOX||s==S_SCOPE)return 0;
    if(s==S_VIEW)return bu_mode!=BU_BOARD||bu_view==BU_VNETS;
    return 1;
}
/* Every keyed section whose picture moved; then the board span and scope. */
static int ovl_bu_update(unsigned flags)
{
    int s;BBWord k;
    for(s=0;s<S_N;s++)if(bu_keyed(s)){
        /* the simulation alone moves only these; anything else waits for
         * a user event or a command (BU_UIKEY) */
        if(!(flags&BU_UIKEY)&&!(s==S_STATE||s==S_BTN+5||s==S_BTN+6||s==S_STATUS||s>=S_STIME||
            s==S_INSP||s==S_PROBE||s==S_CIRC||s==S_HOLE||s==S_SHEAD||s==S_VIEW))continue;
        k=ovl_bu_skey(s);
        if(k!=bu_key[s]){bu_key[s]=k;ovl_bu_paint_s(s);}
    }
    if((flags&BU_BOARD_DIRTY)&&bu_mode==BU_BOARD&&bu_view==BU_VBOARD&&bu_dirty_top<=bu_dirty_bottom)
        ovl_bu_paint(bu_bx,bu_dirty_top,bu_bx+bu_bw-1,bu_dirty_bottom);
    if((flags&BU_SCOPE_DIRTY)){
        /* the simulation moves only the plot and the line under it; a user
         * event or a command repaints the legend and run state too, and so
         * does the simulation when it changes the run state the header
         * reads: a Run that makes the results valid would otherwise leave
         * STALE there until the next click */
        k=(bb_valid?4:0)|(bb_running?8:0)|bb_doc.trigger;
        if(k!=bu_shead){bu_shead=k;flags|=BU_UIKEY;}
        if(bu_r1[S_SCOPE]<=bu_r3[S_SCOPE]){
            if(flags&BU_UIKEY)ovl_bu_paint_s(S_SCOPE);
            else ovl_bu_paint(bu_r1[S_SCOPE]+(bu_compact?26:32),bu_r2[S_SCOPE]+bu_th+3,bu_r3[S_SCOPE]-6,bu_r4[S_SCOPE]-1);
        }
        if(bu_mode==BU_BOARD&&bu_view==BU_VSCOPE)ovl_bu_paint_s(S_VIEW);
    }
    return 1;
}
static int ovl_bu_full(void)
{
    int s,y2;
    for(s=0;s<S_N;s++)bu_key[s]=bu_keyed(s)?ovl_bu_skey(s):0;
    y2=bu_y+bu_h-1;
    ovl_bu_paint(bu_x,bu_y,bu_x+bu_w-1,bu_tby-1);
    if(bu_compact)ovl_bu_paint(bu_x,bu_tby,bu_x+bu_w-1,bu_sty-1);
    else{ovl_bu_paint(bu_x,bu_tby,bu_lx2-1,bu_sty-1);ovl_bu_paint(bu_cx1,bu_tby,bu_cx2,bu_sty-1);ovl_bu_paint(bu_rx,bu_tby,bu_x+bu_w-1,bu_sty-1);}
    ovl_bu_paint(bu_x,bu_sty,bu_x+bu_w-1,y2);
    return 1;
}
static void bu_render(void *win,int full)
{
    unsigned flags;
    if(!bu_layout(win))return;
    flags=bu_flags;
    if(bu_mode==BU_ABOUT){if(full||flags&BU_ALL)os88_about_card_d(win,bu_about);bu_flags=0;return;}
    if(full||flags&BU_ALL){
        if(bu_paint_region){ovl_bu_paint(bu_damage.x1,bu_damage.y1,bu_damage.x2,bu_damage.y2);bu_flags=flags&~BU_ALL;return;}
        ovl_bu_full();
    }else ovl_bu_update(flags);
    bu_cache_ok=1;bu_flags=0;os88_wm_grow(win);
}

/* ---- damage spans on the board ----------------------------------------- */
static int ovl_bu_damage_span(int top,int bottom)
{
    top-=bu_th+12;bottom+=bu_th+12;
    if(bottom<bu_by||top>=bu_by+bu_bh)return 1;
    if(top<bu_by)top=bu_by;if(bottom>=bu_by+bu_bh)bottom=bu_by+bu_bh-1;
    if(!(bu_flags&BU_BOARD_DIRTY)){bu_dirty_top=top;bu_dirty_bottom=bottom;}
    else{if(top<bu_dirty_top)bu_dirty_top=top;if(bottom>bu_dirty_bottom)bu_dirty_bottom=bottom;}
    bu_flags|=BU_BOARD_DIRTY;return 1;
}
static int ovl_bu_damage_part(const BBPart *part)
{
    unsigned i,n;int top,bottom,y;BBWord h;
    top=32767;bottom=-32767;n=bb_pincount(part->type);
    for(i=1;i<=n;i++){h=bb_pin(part,i);if(h>=BB_HOLES)continue;y=bu_hy(h);if(y<top)top=y;if(y>bottom)bottom=y;}
    if(top<=bottom)ovl_bu_damage_span(top-24,bottom+12);return 1;
}
static int ovl_bu_damage_wire(const BBWire *wire)
{
    int top,bottom,y;unsigned i;
    top=bu_hy(wire->a);bottom=bu_hy(wire->b);if(top>bottom){y=top;top=bottom;bottom=y;}
    for(i=0;i<wire->nbend;i++){y=bu_hy(wire->bend[i*2+1]*30+wire->bend[i*2]);if(y<top)top=y;if(y>bottom)bottom=y;}
    ovl_bu_damage_span(top-5,bottom+5);return 1;
}
static int ovl_bu_damage_selection(void)
{
    int idx;
    idx=bu_pick_index();if(idx>=0){if(bu_selected_kind==BB_PART)ovl_bu_damage_part(&bb_doc.part[idx]);else ovl_bu_damage_wire(&bb_doc.wire[idx]);}
    if(bu_ready){if(bu_tool==BU_PART)ovl_bu_damage_part(&bu_part);else if(bu_tool==BU_WIRE)ovl_bu_damage_wire(&bu_wire);}
    if(bu_hole<BB_HOLES){
        /* the strip and, with net highlight, the whole net: the board */
        if(bu_netshow&&!bb_recompile&&bb_hnet[bu_hole]<BB_NETS)ovl_bu_damage_span(bu_by,bu_by+bu_bh);
        else{idx=bu_hole/30;ovl_bu_damage_span(bu_hy((idx<2||idx>11?idx:idx<7?2:7)*30),bu_hy((idx<2||idx>11?idx:idx<7?6:11)*30));}
    }
    return 1;
}
static int ovl_bu_damage_net(void)
{
    if(bu_netshow&&!bb_recompile&&bu_hole<BB_HOLES)ovl_bu_damage_span(bu_by,bu_by+bu_bh);
    return 1;
}
