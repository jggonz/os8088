/* Native Borat face. BORATYPE-SPEC 3. Panels/text are opaque composed bands;
 * flat key indicators use the contract's existing clipped native primitives.
 * Menu/body, portrait, caption, prompt cells, counters and physical keycaps
 * have independent caches. No game state or audio changes from paint.
 * Included in the package's one C translation unit after os88.h and bt.h. */
#include "bt_face.h"
#include "art.h"
#include "gk.h"

int bx_rows(int x,int y,int w,int h);
void bx_new(int x,int y,int w,int h,int colour);
void bx_fill(int x1,int y1,int x2,int y2,int colour);
int bx_spr(int x,int y,int id);
int bx_text(int x,int y,const char *s,int colour,int font);
int bx_twidth(const char *s,int font);
int bt_key_pending(void);
int bx_fonth(int font);
int bx_out(void);
void bx_setup(unsigned seg,void *band,unsigned cap,unsigned artseg,int bpp);
void bx_setart2(unsigned seg);
extern unsigned bx_bseg;
/* Pure audio status/review getters, matching bt_audio.h. */
const char *bt_reaction_history(int back);

#define TF_NAVY 1
#define TF_BLUE 9
#define TF_CYAN 11
#define TF_YELLOW 14
#define TF_WHITE 15
#define TF_GREEN 10
#define TF_RED 12
#define TF_KEYS 60
#define TF_CELLS 144
#define TF_HITS 32
static struct os88_pt tf_org;
static struct os88_size tf_sz;
static struct os88_video tf_vid;
static unsigned tf_art,tf_band,tf_cap;
static int tf_x,tf_y,tf_w,tf_h,tf_mono,tf_compact,tf_font,tf_title,tf_th;
static int tf_top,tf_bottom,tf_px,tf_py,tf_pw,tf_ph,tf_ky,tf_kh,tf_kw;
static int tf_cols,tf_cw,tf_ch,tf_promptx,tf_prompty,tf_inputy,tf_statsy;
static int tf_inputx,tf_inputcols,tf_hinty,tf_hinth,tf_stath,tf_progressfill;
static int tf_caret_on=1,tf_savedcaret=-1,tf_go_left,tf_clockready;
static unsigned tf_clocklast;
#define BT_PLAY_CLOCK 1
static void tf_play_clock(unsigned now)
{
    unsigned delta;
    delta=(unsigned short)(now-tf_clocklast);tf_clocklast=now;
    tf_caret_on=(now/9)&1;
    if(tf_clockready&&bt_session.status==BT_ACTIVE)tf_go_left=9;
    else if(delta>=(unsigned)tf_go_left)tf_go_left=0;
    else tf_go_left-=delta;
    tf_clockready=bt_session.status==BT_READY;
}
static int tf_lx,tf_ly,tf_lw,tf_lh,tf_lastscreen,tf_view,tf_inputview;
static int tf_sx,tf_sy,tf_sw,tf_sh;
static unsigned char tf_glyphw[95];
static int tf_keyx[TF_KEYS],tf_keyw[TF_KEYS],tf_rowy[5];
static unsigned tf_savedpos,tf_savedlen,tf_savedcomplete;
static int tf_savedwrong,tf_savedmods;
static unsigned tf_savedattempt,tf_savedback;
/* The directory remains in movable parts; only one record and five strip
 * signatures are resident. No frame buffer is retained. */
static BTWord tf_arec[4],tf_ahead[4],tf_atarget[5][4];
static unsigned tf_asig[5],tf_astart,tf_aframe_at,tf_ablink_at;
static int tf_aready,tf_bigready,tf_aframe,tf_aseq,tf_aexpr=-1,tf_apos,tf_abase=-1;
static int tf_adone,tf_aload=-1,tf_aphase,tf_ashown=-1,tf_amask,tf_acapturn,tf_idleturn;
static int tf_ax,tf_ay,tf_aw,tf_ah,tf_avariant,tf_avisible;
static const char *tf_acaption,*tf_capmsg;
static unsigned char tf_capstart[16],tf_caplen[16];
static int tf_capwidth,tf_capfont,tf_capcount;
int bt_anim_unpack(unsigned seg,unsigned off,unsigned packed,unsigned raw);
int bt_anim_planar(unsigned seg,unsigned off,int x,int y,int height);
void bt_anim_rawstrip(unsigned seg,unsigned off);
int os88_part_fetch(int i);int os88_part_movable(int i);void os88_part_drop(int i);
unsigned os88_part_seg(int i);
void os88_seg_get(void *dst,unsigned seg,unsigned off,unsigned n);
static int tf_cosneeded,tf_capline,tf_capneeded;
static int tf_statsx,tf_statsw,tf_hintpending,tf_hintrow,tf_statpend,tf_statnext,tf_footpending,tf_footrow;
static unsigned char tf_statrow[5];
static int tf_dirtyfirst[3],tf_dirtylast[3];
static int tf_vsx,tf_vsy; /* independent sound-settings status line */
static unsigned tf_vstatus,tf_settings;
extern int bt_music_phase;
static int tf_meterlast;
static int tf_nhit,tf_hit[TF_HITS][5];
static unsigned tf_header,tf_bodykey,tf_footer,tf_portrait,tf_captionkey,tf_hintkey;
static unsigned tf_statkey[7];
static unsigned char tf_keycache[TF_KEYS],tf_cellchar[TF_CELLS],tf_cellstyle[TF_CELLS];
static char tf_buf[96],tf_num[8],tf_char[3],tf_wrap[80];
/* x and width are quarter key units. Five rows total15 key units each. */
static const unsigned char tf_keys[TF_KEYS][6] = {
    {96,126,0,0,4,1},
    {49,33,0,4,4,1},
    {50,64,0,8,4,2},
    {51,35,0,12,4,3},
    {52,36,0,16,4,4},
    {53,37,0,20,4,4},
    {54,94,0,24,4,5},
    {55,38,0,28,4,5},
    {56,42,0,32,4,6},
    {57,40,0,36,4,7},
    {48,41,0,40,4,8},
    {45,95,0,44,4,8},
    {61,43,0,48,4,8},
    {113,81,1,6,4,1},
    {119,87,1,10,4,2},
    {101,69,1,14,4,3},
    {114,82,1,18,4,4},
    {116,84,1,22,4,4},
    {121,89,1,26,4,5},
    {117,85,1,30,4,5},
    {105,73,1,34,4,6},
    {111,79,1,38,4,7},
    {112,80,1,42,4,8},
    {91,123,1,46,4,8},
    {93,125,1,50,4,8},
    {92,124,1,54,4,8},
    {97,65,2,7,4,1},
    {115,83,2,11,4,2},
    {100,68,2,15,4,3},
    {102,70,2,19,4,4},
    {103,71,2,23,4,4},
    {104,72,2,27,4,5},
    {106,74,2,31,4,5},
    {107,75,2,35,4,6},
    {108,76,2,39,4,7},
    {59,58,2,43,4,8},
    {39,34,2,47,4,8},
    {122,90,3,9,4,1},
    {120,88,3,13,4,2},
    {99,67,3,17,4,3},
    {118,86,3,21,4,4},
    {98,66,3,25,4,4},
    {110,78,3,29,4,5},
    {109,77,3,33,4,5},
    {44,60,3,37,4,6},
    {46,62,3,41,4,7},
    {47,63,3,45,4,8},
    {8,0,0,52,8,8},
    {9,0,1,0,6,1},
    {20,0,2,0,7,1},
    {13,0,2,51,9,8},
    {16,0,3,0,9,1},
    {17,0,3,49,11,8},
    {32,32,4,9,42,0},
    {18,0,4,0,5,1},
    {19,0,4,5,4,1},
    {21,0,4,51,4,8},
    {22,0,4,55,5,8},
    {23,0,4,60,0,0},
    {24,0,4,60,0,0},
};
static const unsigned char tf_neighbor[TF_KEYS][2]={
    {255,1},
    {0,2},
    {1,3},
    {2,4},
    {3,5},
    {4,6},
    {5,7},
    {6,8},
    {7,9},
    {8,10},
    {9,11},
    {10,12},
    {11,47},
    {48,14},
    {13,15},
    {14,16},
    {15,17},
    {16,18},
    {17,19},
    {18,20},
    {19,21},
    {20,22},
    {21,23},
    {22,24},
    {23,25},
    {24,255},
    {49,27},
    {26,28},
    {27,29},
    {28,30},
    {29,31},
    {30,32},
    {31,33},
    {32,34},
    {33,35},
    {34,36},
    {35,50},
    {51,38},
    {37,39},
    {38,40},
    {39,41},
    {40,42},
    {41,43},
    {42,44},
    {43,45},
    {44,46},
    {45,52},
    {12,255},
    {255,13},
    {255,26},
    {36,255},
    {255,37},
    {46,255},
    {55,56},
    {255,55},
    {54,53},
    {53,57},
    {56,255},
    {255,255},
    {255,255},
};

static const char *tf_fingers[]={"Thumb","Left little","Left ring","Left middle","Left index","Right index","Right middle","Right ring","Right little"};
static const unsigned char tf_zones[]={7,12,6,14,10,11,9,5,13};
static const char *tf_group[]={"Home row","All letters","Digits","Mixed punctuation"};
static const char *tf_screenname[]={"WELCOME","LESSONS","TYPE WITH BORAT","RESULTS","PRACTICE","TIMED CHALLENGE","RECORDS","SOUND & DISPLAY","HELP","PAUSED","PROFILES"};
static const unsigned char tf_roles[]={0,7,15,8,1,0,1,9,14,11,0,14,0,8,0,7};
static const unsigned char tf_monoroles[]={0,15,15,0,15,0,0,0,15,0,15,15,0,0,0,15};
static const char *tf_soundstatus(void)
{return bt_readonly?"Other instance owns sound":bt_master_sound?"Master sound: ON (F3 toggles)":"Master sound: MUTED (F3 toggles)";}
static int tf_ink(int c){return tf_mono?0:c;}
static int tf_ground(void){return tf_mono?15:TF_NAVY;}
static unsigned tf_mix(unsigned h,unsigned v){return ((h<<5)+h)^v;}
static unsigned tf_string(unsigned h,const char *s){while(*s)h=tf_mix(h,(unsigned char)*s++);return h;}
static int tf_append(int n,const char *s){while(*s&&n<94)tf_buf[n++]=*s++;tf_buf[n]=0;return n;}
static int tf_number(int n,unsigned v){os88_utoa(v,tf_num);return tf_append(n,tf_num);}
static void tf_text(int x,int y,int w,const char *s,int c,int font)
{if(y>=tf_sy+tf_sh||y+24<=tf_sy||x>=tf_sx+tf_sw||x+w<=tf_sx)return;gk_text(x,y,w,s,tf_ink(c),font,GK_LEFT|GK_FIT);}
static void tf_line(int x,int y,int w,const char *s)
{tf_text(x,y,w,s,TF_WHITE,tf_font);}
/* Word wrap bounded to80 characters per line. Caption reads real director text. */
static void ovl_tf_lines_ink(int x,int y,int w,int rows,const char *s,int ink)
{
    int i,n,last,advance;
    if(y>=tf_sy+tf_sh||y+rows*(tf_th+2)<=tf_sy)return;
    while(rows-->0&&*s){n=0;last=0;advance=0;
        while(s[n]&&n<78){i=(unsigned char)s[n];
            if(i<32||i>126)i=32;advance+=tf_glyphw[i-32];
            if(advance>w)break;
            if(s[n]==' ')last=n;n++;}
        if(s[n]&&last>0)n=last;if(n==0)n=1;
        for(i=0;i<n;i++)tf_wrap[i]=s[i];tf_wrap[n]=0;
        tf_text(x,y,w,tf_wrap,ink,tf_font);s+=n;while(*s==' ')s++;y+=tf_th+2;
    }
}
#define ovl_tf_lines(x,y,w,n,s) ovl_tf_lines_ink(x,y,w,n,s,TF_WHITE)
static void ovl_tf_button(int x,int y,int w,int h,const char *s,int a,int selected)
{
    if(tf_nhit<TF_HITS){tf_hit[tf_nhit][0]=x;tf_hit[tf_nhit][1]=y;tf_hit[tf_nhit][2]=w;tf_hit[tf_nhit][3]=h;tf_hit[tf_nhit++][4]=a;}
    if(x+w<=tf_sx||x>=tf_sx+tf_sw||y+h<=tf_sy||y>=tf_sy+tf_sh)return;
    if(!tf_mono && h>=22 && w>=45){
        int face, shade, font;
        face=selected?TF_YELLOW:(a==BT_A_WEAK||a==BT_A_PRACTICE?TF_CYAN:7);
        shade=face==TF_YELLOW?6:face==TF_CYAN?3:8;
        /* Stepped shoulders, lit lip and lower bevel inside the hit box. */
        bx_fill(x+3,y,x+w-4,y+h-1,0);
        bx_fill(x+1,y+2,x+w-2,y+h-3,0);
        bx_fill(x,y+4,x+w-1,y+h-5,0);
        bx_fill(x+3,y+2,x+w-4,y+h-4,15);
        bx_fill(x+2,y+4,x+w-3,y+h-5,15);
        bx_fill(x+4,y+3,x+w-4,y+h-4,face);
        bx_fill(x+3,y+5,x+w-3,y+h-6,face);
        bx_fill(x+4,y+h-5,x+w-5,y+h-3,shade);
        bx_fill(x+w-4,y+5,x+w-3,y+h-5,shade);
        font=bx_twidth(s,BT_F_TITLE)<=w-8?BT_F_TITLE:tf_font;
        gk_textbox(x+3,y+1,w-6,h-3,s,0,font,GK_CENTRE|GK_FIT);
    }else{
        gk_button(x,y,w,h,s,tf_font,
            (selected?(tf_mono?GK_B_FOCUS:GK_B_HILITE):GK_B_NORMAL)|GK_B_FLAT);
    }
}
#define tf_button ovl_tf_button
static int tf_kx,tf_ky0,tf_kww,tf_khh;
static int tf_ktarget,tf_kshift,tf_kwrong,tf_kcorrect;
static void tf_keyrect(int i)
{
    tf_kx=tf_keyx[i];tf_ky0=tf_rowy[tf_keys[i][2]];
    tf_kww=tf_keyw[i];tf_khh=tf_kh-2;
}
static int tf_keyid(int c)
{
    int i;if(c==32)return 53;
    for(i=0;i<47;i++)if(tf_keys[i][0]==c||tf_keys[i][1]==c)return i;return -1;
}
static int tf_target(void)
{return bt_session.pos<bt_session.length?(unsigned char)bt_session.passage[bt_session.pos]:0;}
static int tf_shift(int c)
{
    int i;i=tf_keyid(c);if(i<0||c==32||c==tf_keys[i][0])return -1;
    return tf_keys[i][5]<=4?52:51;
}
int bt_key_base(int c)
{int i;i=tf_keyid(c);return i<0?0:tf_keys[i][0];}
const char *bt_key_finger(int c)
{int i;i=tf_keyid(c);return i<0?"None":tf_fingers[tf_keys[i][5]];}
int bt_key_shift(int c)
{int i;i=tf_shift(c);return i<0?0:i==51?1:2;}
static void tf_keyrefresh(int force)
{
    int c,old;old=tf_ktarget;
    if(force||tf_savedpos!=bt_session.pos||tf_savedlen!=bt_session.length||tf_savedcomplete!=bt_session.completed){
        c=tf_target();tf_ktarget=tf_keyid(c);tf_kshift=-1;
        if(tf_ktarget>=0&&c!=32&&c!=tf_keys[tf_ktarget][0])tf_kshift=tf_keys[tf_ktarget][5]<=4?52:51;
    }
    if(force||tf_savedwrong!=bt_session.last_wrong)tf_kwrong=bt_session.last_wrong?tf_keyid(bt_session.last_wrong):-1;
    tf_kcorrect=-1;
    if(bt_session.pos>0&&!bt_session.last_wrong)tf_kcorrect=!force&&bt_session.pos==tf_savedpos+1?old:tf_keyid((unsigned char)bt_session.passage[bt_session.pos-1]);
}
static int tf_keystate(int i)
{
    int s;s=0;if(i==tf_ktarget)s|=1;if(i==tf_kshift)s|=2;
    if(i==tf_kwrong)s|=4;if(i==tf_kcorrect)s|=16;
    if((i==51&&(bt_modifiers&1))||(i==52&&(bt_modifiers&2))||(i==49&&(bt_modifiers&4)))s|=8;
    return s;
}
static const char *tf_keylabel(int i)
{
    static const char *modlabels[]={"Back","Tab","Caps","Enter","Shift","Shift","SPACE","Ctrl","Alt","Alt","Ctrl","",""};
    if(i<47){tf_char[0]=tf_keys[i][0];if(tf_char[0]>='a'&&tf_char[0]<='z')tf_char[0]-=32;tf_char[1]=0;return tf_char;}
    return modlabels[i-47];
}
static int tf_keycolor(int i)
{return tf_mono?15:i>=47?(i==53?15:7):tf_zones[tf_keys[i][5]];}
/* Static cap depth belongs to the cold full/compact renderer. Hot state
 * changes touch only the reserved frame, badge and held-modifier line. */
static void ovl_tf_keycap(int i,int state)
{
    int x,y,w,h,c,dark,e,b;
    x=tf_kx;y=tf_ky0;w=tf_kww;h=tf_khh;c=tf_keycolor(i);
    if(h<20)gk_box(x,y,w,h,GK_RAISED2,c);
    else{
        dark=tf_mono?0:8;
        bx_fill(x+3,y,x+w-4,y+h-1,0);bx_fill(x,y+3,x+w-1,y+h-4,0);
        bx_fill(x+3,y+1,x+w-5,y+h-5,c);bx_fill(x+1,y+3,x+w-5,y+h-5,c);
        bx_fill(x+4,y+1,x+w-5,y+2,15);bx_fill(x+1,y+4,x+2,y+h-6,15);
        bx_fill(x+4,y+h-4,x+w-4,y+h-3,dark);bx_fill(x+w-4,y+4,x+w-3,y+h-4,dark);
    }
    if(state&3){c=tf_mono?0:TF_YELLOW;e=h<20?3:4;b=h<20?4:6;
        bx_fill(x+e,y+e,x+w-b,y+e,c);bx_fill(x+e,y+h-b,x+w-b,y+h-b,c);
        bx_fill(x+e,y+e+1,x+e,y+h-b-1,c);bx_fill(x+w-b,y+e+1,x+w-b,y+h-b-1,c);
    }
    if(h<12){
        if(state&4){c=tf_mono?0:TF_RED;bx_fill(x+w-7,y+1,x+w-7,y+2,c);bx_fill(x+w-7,y+4,x+w-7,y+4,c);}
        else if((state&16)&&!(state&1)){c=tf_mono?0:TF_GREEN;bx_fill(x+w-8,y+3,x+w-6,y+3,c);bx_fill(x+w-7,y+2,x+w-7,y+4,c);}
    }else{
        e=h>=20?4:3;
        if(state&4){c=tf_mono?0:TF_RED;bx_fill(x+w-7,y+e,x+w-7,y+e+3,c);bx_fill(x+w-7,y+e+6,x+w-7,y+e+6,c);}
        else if((state&16)&&!(state&1)){c=tf_mono?0:TF_GREEN;bx_fill(x+w-9,y+e+3,x+w-5,y+e+3,c);bx_fill(x+w-7,y+e+1,x+w-7,y+e+5,c);}
    }
    if(state&8)bx_fill(x+5,y+h-(h>=20?7:5),x+w-7,y+h-(h>=20?7:5),tf_mono?0:15);
    if(i==29||i==32)bx_fill(x+w/2-3,y+h-(h>=20?5:6),x+w/2+2,y+h-(h>=20?5:6),0);
}
static void tf_drawkey(int i,int selective)
{
    int state,f,fh,lx,ly,lw;const char *s;
    tf_keyrect(i);if(tf_kww<=0||tf_kx+tf_kww<=tf_sx||tf_kx>=tf_sx+tf_sw||tf_ky0+tf_khh<=tf_sy||tf_ky0>=tf_sy+tf_sh)return;
    state=tf_keystate(i);ovl_tf_keycap(i,state);
    s=tf_keylabel(i);f=tf_compact?BT_F_SMALL:(i<47&&tf_kww>=24&&tf_khh>=22?BT_F_TITLE:tf_kww<34?-1:tf_khh>=26?BT_F_TITLE:tf_font);fh=f==BT_F_TITLE?16:f<0||tf_compact?8:10;
    ly=tf_ky0+1+(tf_khh-3-fh)/2;
    if(!selective||(ly<tf_sy+tf_sh&&ly+fh>tf_sy)){
        lw=bx_twidth(s,f);lx=tf_kx+4+(tf_kww-8-lw)/2;
        if(!selective||(lx<tf_sx+tf_sw&&lx+lw>tf_sx))gk_textbox(tf_kx+4,tf_ky0+1,tf_kww-8,tf_khh-3,s,0,f,GK_CENTRE|GK_FIT);
    }
    if(i<47&&tf_khh>=26&&tf_keys[i][1]!=tf_keys[i][0]&&tf_keys[i][0]<'a'&&tf_kx+5<tf_sx+tf_sw&&tf_kx+13>tf_sx&&tf_ky0+5<tf_sy+tf_sh&&tf_ky0+13>tf_sy){
        tf_char[0]=tf_keys[i][1];tf_char[1]=0;bx_text(tf_kx+5,tf_ky0+5,tf_char,0,BT_F_SMALL);}

}
static int ovl_tf_arrange(void)
{
    int space,i;
    tf_lx=tf_org.x;tf_ly=tf_org.y;tf_lw=tf_sz.w;tf_lh=tf_sz.h;
    tf_x=(tf_org.x+7)&~7;tf_y=tf_org.y;tf_w=(tf_sz.w-(tf_x-tf_org.x))&~7;tf_h=tf_sz.h;
    if(tf_w<480||tf_h<128)return 0;
    tf_mono=tf_vid.bpp<4;tf_compact=tf_h<280;
    tf_font=tf_compact?BT_F_SMALL:BT_F_TEXT;tf_title=tf_compact?BT_F_TEXT:(tf_h>=400?BT_F_LARGE:BT_F_TITLE);tf_th=tf_compact?8:10;
    tf_top=tf_y+(tf_compact?24:(tf_h>=400?58:46));tf_bottom=tf_y+tf_h-(tf_compact?10:17);
    tf_kw=(tf_w-(tf_compact?16:32))/15;
    tf_kh=tf_compact?(tf_h<154?10:12):(tf_bottom-tf_top)*45/500;
    if(!tf_compact){if(tf_kh<18)tf_kh=18;if(tf_kh>52)tf_kh=52;}
    if(tf_h>=400&&tf_kh>(tf_bottom-tf_top-190)/5)tf_kh=(tf_bottom-tf_top-190)/5;
    tf_ky=tf_bottom-5*tf_kh-(tf_compact?2:6);
    tf_stath=tf_compact?11:tf_h>=380?30:20;tf_statsy=tf_ky-tf_stath-(tf_compact?0:4);
    tf_px=tf_x+8;tf_py=tf_top+4;tf_pw=tf_compact?0:(tf_w/4)&~7;
    if(!tf_compact){if(tf_pw<136)tf_pw=136;if(tf_pw>196)tf_pw=196;}
    tf_ph=tf_ky-tf_py-6;
    tf_promptx=tf_x+(tf_compact?8:tf_pw+24);tf_prompty=tf_compact?tf_top+2:tf_py+(tf_h>=400?24:18);
    space=tf_x+tf_w-12-tf_promptx;
    tf_cw=tf_compact?8:(tf_h>=400?16:12);tf_ch=tf_compact?8:tf_h>=400?28:tf_h<300?14:16;
    tf_cols=space/tf_cw;if(tf_cols>40)tf_cols=40;if(tf_cols<12)tf_cols=12;
    tf_inputy=tf_prompty+2*tf_ch+(tf_compact?2:tf_h>=400?7:1);
    tf_hinty=tf_compact?tf_y+2:tf_statsy-(tf_h>=400?29:21);tf_hinth=tf_compact?8:tf_h>=400?16:10;
    bx_setup(tf_band,0,tf_cap,tf_art,tf_mono?1:4);gk_init(tf_mono?1:4);gk_theme(tf_mono?tf_monoroles:tf_roles);
    space=tf_compact?0:(bx_twidth("YOU TYPE",BT_F_SMALL)+10+tf_cw-1)/tf_cw;
    tf_inputx=tf_promptx+space*tf_cw;tf_inputcols=tf_cols-space;
    for(i=0;i<95;i++){tf_char[0]=i+32;tf_char[1]=0;tf_glyphw[i]=bx_twidth(tf_char,tf_font);}
    for(i=0;i<TF_KEYS;i++){tf_keyx[i]=tf_x+(tf_compact?8:16)+tf_keys[i][3]*tf_kw/4;tf_keyw[i]=tf_keys[i][4]*tf_kw/4-2;}
    for(i=0;i<5;i++)tf_rowy[i]=tf_ky+i*tf_kh;
    tf_statsx=tf_compact?tf_x:tf_promptx-4;tf_statsw=tf_compact?tf_w:tf_x+tf_w-8-tf_statsx;
    return 2;
}
static int tf_layout(void *win)
{
    int changed;
    os88_wm_content(win,&tf_org);if(os88_wm_geom(win,&tf_sz)!=0)return 0;
    os88_wm_display(win,&tf_vid);
    changed=tf_lx!=tf_org.x||tf_ly!=tf_org.y||tf_lw!=tf_sz.w||tf_lh!=tf_sz.h||tf_mono!=(tf_vid.bpp<4);
    if(!changed)return tf_w<480||tf_h<128?0:1;
    return ovl_tf_arrange();
}
static int ovl_tf_tabx(int i)
{
    static const unsigned char stops[]={0,10,21,34,45,57,64};
    int span,unit;
    span=tf_w-16;unit=stops[i];
    return tf_x+8+(span/64)*unit+((span%64)*unit)/64;
}
static void ovl_tf_drawheader(void)
{
    static const char *tabs[] = {"LESSONS","PRACTICE","CHALLENGE","RECORDS","SETTINGS","HELP"};
    static const unsigned char pages[] = {BT_S_LESSONS,BT_S_PRACTICE,BT_S_CHALLENGE,BT_S_RECORDS,BT_S_SETTINGS,BT_S_HELP};
    int tab_y, tab_h, w, x, i, active, font;
    tab_h = tf_compact ? 12 : 22;
    tab_y = tf_top - tab_h;
    if (tf_compact)
        tf_text(tf_x+8,tf_y,212,"BORAT TEACHES TYPING",TF_YELLOW,tf_title);
    else {
        bx_text(tf_x+(tf_w-432)/2,tf_y+(tab_y-tf_y-24)/2,
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ[",tf_ink(TF_YELLOW),BT_F_MASTHEAD);
        if (tf_w>=560) tf_text(tf_x+tf_w-55,tf_y+5,53,"os8088",TF_WHITE,tf_font);
    }
    bx_fill(tf_x,tab_y,tf_x+tf_w-1,tab_y,tf_ink(TF_CYAN));
    bx_fill(tf_x,tf_top-2,tf_x+tf_w-1,tf_top-1,tf_ink(TF_CYAN));
    for(i=0;i<6;i++){
        x=ovl_tf_tabx(i);w=ovl_tf_tabx(i+1)-x;
        font=tf_compact?BT_F_SMALL:(bx_twidth(tabs[i],BT_F_TITLE)<=w-3?BT_F_TITLE:BT_F_TEXT);
        active=bt_screen==pages[i];
        if(bt_screen==BT_S_PLAY||bt_screen==BT_S_PAUSE||bt_screen==BT_S_RESULTS)
            active=i==(bt_session.mode==BT_CHALLENGE?2:bt_session.mode==BT_PRACTICE?1:0);
        if(active) bx_fill(x+1,tab_y+1,x+w-2,tf_top-3,tf_mono?0:TF_YELLOW);
        gk_textbox(x,tab_y+1,w,tab_h-3,tabs[i],active?(tf_mono?15:0):tf_ink(TF_WHITE),font,GK_CENTRE|GK_FIT);
    }
}
static void ovl_tf_drawfooter(void)
{
    int y, paper;
    y=tf_bottom;paper=tf_mono?0:5;
    bx_fill(tf_x,y,tf_x+tf_w-1,tf_y+tf_h-1,paper);
    if(!tf_compact)bx_fill(tf_x,y,tf_x+tf_w-1,y,tf_mono?0:TF_CYAN);
    if(!tf_compact){
        bx_spr(tf_x+8,y+3,BT_ART_METER+bt_music_phase);
        tf_meterlast=bt_music_phase;
    }
    if(bt_notice[0]){
        bx_text(tf_x+(tf_compact?8:32),y+2,bt_notice,15,tf_font);
        return;
    }
    gk_textbox(tf_x+(tf_compact?4:32),y+1,tf_w-(tf_compact?8:40),tf_y+tf_h-y-1,
        bt_readonly?"READ ONLY    F1 Help    F2 Lessons    F4 Pause    F6 Full    Esc Menu":
        bt_master_sound?"F1 Help    F2 Lessons    F3 Sound ON    F4 Pause    F6 Full    Esc Menu":
        "F1 Help    F2 Lessons    F3 Sound OFF    F4 Pause    F6 Full    Esc Menu",
        15,tf_compact?BT_F_SMALL:BT_F_TEXT,GK_CENTRE|GK_FIT);
}
/* bx_out respects the current window clip. An idle pulse redraws exactly
 * the music icon, so animating it never re-measures the footer text. */
static void ovl_tf_meter(void)
{
    bx_new(tf_x+8,tf_bottom+3,16,10,tf_mono?0:5);
    bx_spr(tf_x+8,tf_bottom+3,BT_ART_METER+bt_music_phase);
    bx_out();
    tf_meterlast=bt_music_phase;
}
/* Band composition only: callers own the panel frame and caption. */
void ovl_bt_portrait_scene(int x,int y,int w,int h)
{
    int id,pw,ph,e;
    e=bt_expression();if(e<0||e>6)e=0;
    pw=(!tf_mono&&w>=96&&h>=120&&(tf_bigready||!tf_aready))?96:40;ph=pw==96?120:48;
    if(w<pw||h<ph)return;
    id=tf_mono?BT_ART_MONO+e:(pw==96?BT_ART_BIG+e:BT_ART_SMALL+e);
    x+=(w-pw)/2;y+=(h-ph)/2;x&=~7;
    if(x>=tf_sx+tf_sw||x+pw<=tf_sx||y>=tf_sy+tf_sh||y+ph<=tf_sy)return;
    if(tf_ax!=x||tf_ay!=y||tf_aw!=pw||tf_ah!=ph){tf_abase=-1;tf_apos=1;}
    tf_ax=x;tf_ay=y;tf_aw=pw;tf_ah=ph;tf_avisible=1;
    tf_avariant=tf_mono?2:pw==96?0:1;
    /* Full-band static poses correspond to these authoritative source cells. */
    tf_abase=e==2?17:e==3?18:e==4?29:e==5?21:e==6?24:0;
    tf_adone=0;tf_aload=-1;
    bx_spr(x,y,id);
}
static void ovl_tf_drawportrait(void)
{
    if(tf_compact||tf_px>=tf_sx+tf_sw||tf_px+tf_pw<=tf_sx||tf_py>=tf_sy+tf_sh||tf_py+tf_ph<=tf_sy)return;
    gk_box(tf_px,tf_py,tf_pw,tf_ph,GK_RAISED,tf_ground());
    ovl_bt_portrait_scene(tf_px,tf_py+6,tf_pw,tf_ph>=150?120:48);
}
static void ovl_tf_caption_prepare(const char *text,int width)
{
    int pos,n,last,advance,c;
    if(tf_capmsg==text&&tf_capwidth==width&&tf_capfont==tf_font)return;
    tf_capmsg=text;tf_capwidth=width;tf_capfont=tf_font;tf_capcount=0;pos=0;
    while(text[pos]&&tf_capcount<16){
        n=0;last=0;advance=0;
        while(text[pos+n]&&n<78){
            c=(unsigned char)text[pos+n];if(c<32||c>126)c=32;
            advance+=tf_glyphw[c-32];if(advance>width)break;
            if(text[pos+n]==' ')last=n;n++;
        }
        if(text[pos+n]&&last>0)n=last;if(!n)n=1;
        tf_capstart[tf_capcount]=pos;tf_caplen[tf_capcount]=n;tf_capcount++;
        pos+=n;while(text[pos]==' ')pos++;
    }
}
static void ovl_tf_drawcaption(void)
{
    int x,y,w,rows,i,k,line;
    if(tf_compact){x=tf_x+8;y=tf_statsy-10;w=tf_w-16;rows=1;}
    else{x=tf_px+6;y=tf_py+(tf_ph>=150?132:60);w=tf_pw-12;rows=(tf_py+tf_ph-y-5)/(tf_th+2);}
    ovl_tf_caption_prepare(bt_caption(),w);
    if(rows>tf_capcount)rows=tf_capcount;
    for(i=0;i<rows;i++){
        line=y+i*(tf_th+2);
        if(line>=tf_sy+tf_sh||line+tf_th<=tf_sy)continue;
        for(k=0;k<tf_caplen[i];k++)tf_wrap[k]=tf_capmsg[tf_capstart[i]+k];tf_wrap[k]=0;
        /* The cached line already fits. Avoid a second full metric/fit pass;
         * bx_text composes into the same opaque clipped drawing band. */
        bx_text(x,line,tf_wrap,tf_ink(TF_WHITE),tf_font);
    }
}
static void ovl_tf_drawhint(void)
{
    int c,i,n,font,x,w;
    if(tf_compact){x=tf_x+(tf_w>=600?280:220);w=tf_x+tf_w-70-x;}
    else{x=tf_promptx;w=tf_cols*tf_cw;if(w>344)w=344;}
    if(tf_hinty>=tf_sy+tf_sh||tf_hinty+tf_hinth<=tf_sy)return;
    c=tf_target();i=tf_keyid(c);n=0;font=BT_F_SMALL;
    if(bt_session.status==BT_READY){n=tf_append(0,"GET READY  ");tf_number(n,bt_session.countdown);font=tf_compact?BT_F_SMALL:tf_h>=400?BT_F_TITLE:BT_F_TEXT;}
    else if(tf_go_left&&bt_session.mode==BT_CHALLENGE){tf_append(0,"GO!  GREAT SUCCESS AWAITS.");font=tf_compact?BT_F_SMALL:tf_h>=400?BT_F_TITLE:BT_F_TEXT;}
    else if(i>=0){n=tf_append(0,"NEXT: ");tf_char[0]=c;tf_char[1]=0;n=tf_append(n,c==32?"SPACE":tf_char);
        n=tf_append(n,"  ");n=tf_append(n,tf_fingers[tf_keys[i][5]]);
        if(tf_shift(c)>=0)n=tf_append(n,tf_shift(c)==51?" + left Shift":" + right Shift");
        if(bt_session.last_wrong){n=tf_append(n,"  WRONG: ");tf_char[0]=bt_session.last_wrong;tf_char[1]=0;tf_append(n,tf_char);}}
    else tf_append(0,"F4 Resume / Pause");
    gk_textbox(x,tf_hinty,w,tf_hinth,tf_buf,tf_ink(TF_YELLOW),font,GK_LEFT|GK_FIT);
}
static int tf_cellc(int j)
{
    int p;p=(j<2*tf_cols?tf_view:tf_inputview)+(j<2*tf_cols?j:j-2*tf_cols);
    if(p>=bt_session.length)return 32;
    if(j>=2*tf_cols&&p>=bt_session.pos)return 32;
    return (unsigned char)bt_session.passage[p];
}
static int tf_cellstate(int j)
{
    int p;p=(j<2*tf_cols?tf_view:tf_inputview)+(j<2*tf_cols?j:j-2*tf_cols);
    if(p==bt_session.pos)return j<2*tf_cols||tf_caret_on?2:0;
    return p<bt_session.pos?1:0;
}
static void tf_cache_cell(int i,int force)
{
    int c,s,row;c=tf_cellc(i);s=tf_cellstate(i);
    if(force||c!=tf_cellchar[i]||s!=tf_cellstyle[i]){
        tf_cellchar[i]=c;tf_cellstyle[i]=s;row=i/tf_cols;
        if(tf_dirtyfirst[row]<0)tf_dirtyfirst[row]=tf_dirtylast[row]=i;
        else{if(i<tf_dirtyfirst[row])tf_dirtyfirst[row]=i;
            if(i>tf_dirtylast[row])tf_dirtylast[row]=i;}
    }
}
static void tf_cellrect(int j)
{
    tf_sx=(j<2*tf_cols?tf_promptx:tf_inputx)+(j%tf_cols)*tf_cw;
    tf_sy=j<2*tf_cols?tf_prompty+(j/tf_cols)*tf_ch:tf_inputy;
    tf_sw=tf_cw;tf_sh=tf_ch;
}
static void tf_drawcell(int j)
{
    int c,s,x,y,col,font;
    if((tf_compact&&tf_h<140&&j>=2*tf_cols)||j>=2*tf_cols+tf_inputcols)return;
    c=tf_cellc(j);s=tf_cellstate(j);x=(j<2*tf_cols?tf_promptx:tf_inputx)+(j%tf_cols)*tf_cw;
    y=j<2*tf_cols?tf_prompty+(j/tf_cols)*tf_ch:tf_inputy;
    if(x+tf_cw<=tf_sx||x>=tf_sx+tf_sw||y+tf_ch<=tf_sy||y>=tf_sy+tf_sh)return;
    col=s==1?TF_GREEN:TF_WHITE;font=tf_compact?-1:(tf_h>=400?BT_F_LARGE:BT_F_TEXT);
    if(s==2){bx_fill(x,y,x+tf_cw-1,y+tf_ch-1,tf_mono?0:TF_YELLOW);col=0;}
    tf_char[0]=c;tf_char[1]=0;
    bx_text(x+(tf_cw-bx_twidth(tf_char,font))/2,y+(tf_compact?0:2),tf_char,tf_mono?(s==2?15:0):col,font);
    if(s==1&&tf_mono)bx_fill(x,y+tf_ch-1,x+tf_cw-2,y+tf_ch-1,0);
    if(s==2)bx_fill(x+1,y+tf_ch-2,x+tf_cw-2,y+tf_ch-2,tf_mono?15:0);
}
static int ovl_tf_progress_width(void)
{
    unsigned total,value;
    total=bt_session.length;value=bt_session.pos;
    if(bt_session.mode==BT_CHALLENGE){total=bt_session.duration==0?60:bt_session.duration==1?180:300;value=bt_session.remaining;}
    return total?bt_muldiv(value,tf_cols*tf_cw-4,total):0;
}
static void ovl_tf_drawprogress(void)
{
    int x,y,w;
    if(tf_compact)return;
    x=tf_promptx-1;y=tf_statsy-10;w=tf_cols*tf_cw+2;
    gk_box(x,y,w,6,GK_SUNKEN,tf_mono?15:5);
    if(tf_progressfill)bx_fill(x+2,y+2,x+1+tf_progressfill,y+3,tf_mono?0:TF_GREEN);
}
static void ovl_tf_drawclock(void)
{
    unsigned sec;
    if(tf_compact||bt_session.mode!=BT_CHALLENGE)return;
    sec=bt_session.remaining;
    tf_buf[0]='0';tf_buf[1]='0'+sec/60;tf_buf[2]=':';
    tf_buf[3]='0'+(sec%60)/10;tf_buf[4]='0'+sec%10;tf_buf[5]=0;
    gk_textbox(tf_x+tf_w-88,tf_py+1,72,16,tf_buf,tf_ink(TF_YELLOW),BT_F_TITLE,GK_RIGHT|GK_FIT);
}
static void ovl_tf_drawprompt(void)
{
    int i,x,w,y,h,n;
    if(!tf_compact){
        x=tf_promptx-8;w=tf_x+tf_w-8-x;y=tf_py;h=tf_statsy-y-4;
        gk_box(x,y,w,h,GK_SUNKEN2,tf_ground());
        bx_fill(x+3,y+17,x+w-4,y+18,tf_ink(TF_CYAN));
        n=0;
        if(bt_session.mode==BT_CHALLENGE){n=tf_number(0,bt_session.duration==0?1:bt_session.duration==1?3:5);tf_append(n," MINUTE CHALLENGE");}
        else if(bt_session.mode==BT_LESSON)tf_append(0,bt_lessons[bt_session.lesson].name);
        else{n=tf_append(0,tf_group[bt_session.difficulty]);tf_append(n," PRACTICE");}
        tf_text(x+7,y+1,w-(bt_session.mode==BT_CHALLENGE?100:14),tf_buf,TF_YELLOW,BT_F_TITLE);
        ovl_tf_drawclock();
        gk_box(tf_promptx-2,tf_inputy-1,tf_cols*tf_cw+4,tf_ch+2,GK_SUNKEN,tf_ground());
        tf_text(tf_promptx+3,tf_inputy+(tf_ch-8)/2,tf_inputx-tf_promptx-6,"YOU TYPE",TF_WHITE,BT_F_SMALL);
        ovl_tf_drawprogress();
    }
    ovl_tf_drawhint();
    if(tf_prompty<tf_sy+tf_sh&&tf_prompty+tf_ch>tf_sy)for(i=0;i<tf_cols;i++)tf_drawcell(i);
    if(tf_prompty+tf_ch<tf_sy+tf_sh&&tf_prompty+2*tf_ch>tf_sy)for(i=tf_cols;i<2*tf_cols;i++)tf_drawcell(i);
    if(tf_inputy<tf_sy+tf_sh&&tf_inputy+tf_ch>tf_sy)for(i=2*tf_cols;i<2*tf_cols+tf_inputcols;i++)tf_drawcell(i);
}
static void ovl_tf_drawstat(int i)
{
    int x,w,n,font,y;static const char *labels[]={"SPEED","ACCURACY","STREAK","TIME","ERRORS"};
    static const char *shortlabels[]={"WPM ","ACC ","STREAK ","TIME ","ERR "};
    w=tf_statsw/5;x=tf_statsx+i*w;
    if(x+w<=tf_sx||x>=tf_sx+tf_sw||tf_statsy+tf_stath<=tf_sy||tf_statsy>=tf_sy+tf_sh)return;
    if(!tf_compact)gk_box(x,tf_statsy,w,tf_stath,GK_SUNKEN,tf_ground());
    n=tf_append(0,tf_stath>=30?"":shortlabels[i]);
    if(i==0){n=bt_session.seconds?tf_number(n,bt_session.wpm):tf_append(n,"-");if(tf_stath>=30)tf_append(n," WPM");}
    if(i==1){if(bt_session.attempts){n=tf_number(n,bt_session.accuracy);tf_append(n,"%");}else tf_append(n,"-");}
    if(i==2)tf_number(n,bt_session.streak);
    if(i==3){n=tf_number(n,bt_session.mode==BT_CHALLENGE?bt_session.remaining:bt_session.seconds);tf_append(n,"s");}
    if(i==4)tf_number(n,bt_session.errors);
    font=tf_font;y=tf_statsy+(tf_compact?1:4);
    if(tf_stath>=30){if(tf_statsy+2<tf_sy+tf_sh&&tf_statsy+10>tf_sy)tf_text(x+4,tf_statsy+2,w-8,labels[i],TF_WHITE,BT_F_SMALL);font=bx_twidth(tf_buf,BT_F_TITLE)<=w-8?BT_F_TITLE:tf_font;y=tf_statsy+12;}
    tf_text(x+4,y,w-8,tf_buf,TF_YELLOW,font);
}
static void ovl_tf_playbase(void)
{
    int i;
    if(!tf_compact)gk_box(tf_x+8,tf_ky-4,tf_w-16,5*tf_kh+8,GK_SUNKEN2,tf_mono?15:TF_BLUE);
    ovl_tf_drawportrait();ovl_tf_drawcaption();ovl_tf_drawprompt();
    for(i=0;i<5;i++)ovl_tf_drawstat(i);
    for(i=0;i<TF_KEYS;i++)tf_drawkey(i,0);
}
#include "bt_menus.c"

/* Render only the requested rectangle, rounded out to the band's byte width.
 * Adjacent cells sharing a byte are recomposed, so neither clipping nor a
 * proportional-font cell can erase its neighbour. */
static void tf_render(int x,int y,int w,int h,int kind,int item)
{
    int right,bottom,rows,n,i,lo,hi;
    right=x+w;bottom=y+h;x&=~7;right=(right+7)&~7;
    if(x<tf_x)x=tf_x;if(right>tf_x+tf_w)right=tf_x+tf_w;
    if(y<tf_y)y=tf_y;if(bottom>tf_y+tf_h)bottom=tf_y+tf_h;
    w=right-x;if(w<=0||bottom<=y)return;
    rows=bx_rows(x,y,w,bottom-y);if(rows<=0)return;
    while(y<bottom){n=bottom-y;if(n>rows)n=rows;
        tf_sx=x;tf_sy=y;tf_sw=w;tf_sh=n;
        bx_new(x,y,w,n,tf_ground());
        if(kind==1)ovl_tf_drawheader();
        else if(kind==2)ovl_tf_drawfooter();
        else if(kind==3)ovl_tf_drawbody();
        else if(kind==4)ovl_tf_playbase();
        else if(kind==5){ovl_tf_drawportrait();ovl_tf_drawcaption();}
        else if(kind==6){if(!tf_compact)ovl_tf_drawportrait();ovl_tf_drawcaption();}
        else if(kind==7)ovl_tf_drawhint();
        else if(kind==8){lo=(item/tf_cols)*tf_cols;hi=lo+tf_cols;
            i=item-1;if(i<lo)i=lo;
            while(i<hi&&(i<2*tf_cols?tf_promptx:tf_inputx)+(i%tf_cols)*tf_cw<x+w){tf_drawcell(i);i++;}}
        else if(kind==9){lo=item?item-1:0;hi=item<4?item+1:4;for(i=lo;i<=hi;i++)ovl_tf_drawstat(i);}
        else if(kind==10){tf_drawkey(item,1);i=tf_neighbor[item][0];if(i!=255)tf_drawkey(i,1);i=tf_neighbor[item][1];if(i!=255)tf_drawkey(i,1);}
        else if(kind==11)tf_text(tf_vsx,tf_vsy,tf_w-24,tf_soundstatus(),TF_YELLOW,tf_font);
        else if(kind==12)ovl_tf_drawprogress();
        else if(kind==13)ovl_tf_drawclock();
        bx_out();y+=n;
    }
}
static unsigned tf_settings_mask(void)
{return bt_store.music+2*bt_store.effects+8*bt_store.keyclick+16*bt_store.roast+64*bt_store.display+128*bt_full;}
/* Clock service selects state only. No paint, disk, random or score writes. */
static void ovl_tf_anim_target(int frame,unsigned now)
{
    tf_aframe=frame;tf_aframe_at=now;tf_adone=0;tf_aload=-1;
    tf_apos=frame>0&&frame<4?1:0;if(tf_avisible)tf_cosneeded=1;
}
static void ovl_tf_anim_clock(unsigned now)
{
    int e;unsigned dwell,elapsed;const char *caption;
    e=bt_expression();caption=bt_caption();
    if(e!=tf_aexpr||caption!=tf_acaption){
        tf_aexpr=e;tf_acaption=caption;tf_astart=now;tf_aphase=0;
        tf_aseq=e==2?2:(e==3||e==5)?3:(e==4||e==6)?4:1;
        ovl_tf_anim_target(tf_aseq*6,now);return;
    }
    if(!tf_aready||!tf_avisible)tf_adone=1;
    if(!tf_adone)return;
    elapsed=(BTWord)(now-tf_aframe_at);dwell=tf_aseq==1?3:tf_aseq?5:2;
    if(tf_aseq){
        if(elapsed<dwell)return;
        tf_aphase++;
        if(tf_aphase<(tf_aseq==1?12:6)){ovl_tf_anim_target(tf_aseq*6+tf_aphase%6,now);return;}
        tf_aseq=0;tf_ablink_at=now;ovl_tf_anim_target(0,now);return;
    }
    if(tf_aframe){
        if(elapsed<2)return;
        if(tf_aframe<3)ovl_tf_anim_target(tf_aframe+1,now);
        else {tf_ablink_at=now;ovl_tf_anim_target(0,now);}
    }else if((BTWord)(now-tf_ablink_at)>=91)ovl_tf_anim_target(1,now);
}
void bt_face_clock(unsigned now)
{
#ifdef BT_PLAY_CLOCK
    tf_play_clock(now);
#endif
    ovl_tf_anim_clock(now);
}
static int ovl_tf_claimart(int i)
{
    if(os88_part_fetch(i)==0 && os88_part_movable(i)==0)return 1;
    os88_part_drop(i);return 0;
}
static void ovl_tf_anim_load(int bpp)
{
    tf_aready=0;tf_bigready=0;if(bt_readonly)return;
    tf_aready=ovl_tf_claimart(3);
    if(bpp<4)return;
    ovl_tf_claimart(5);
    if(!tf_aready)return;
    tf_bigready=ovl_tf_claimart(4)&&ovl_tf_claimart(6)&&ovl_tf_claimart(7);
    if(!tf_bigready){os88_part_drop(4);os88_part_drop(6);os88_part_drop(7);}
}
static int ovl_tf_anim_record(int frame,int strip)
{
    unsigned seg,at;
    at=frame*8;at+=frame;at+=(tf_avariant==0?0:tf_avariant==1?5:7)+strip;at=16+at*8;
    seg=os88_part_seg(3);if(!seg)return 0;
    os88_seg_get(tf_arec,seg,at,8);
    if(tf_arec[0]<3||tf_arec[0]>5||!tf_arec[2]||tf_arec[2]>1154||!tf_arec[3])return 0;
    seg=os88_part_seg(tf_arec[0]);if(!seg)return 0;
    os88_seg_get(tf_ahead,seg,0,8);
    if(tf_ahead[0]!=0x5442||tf_ahead[1]!=0x4e41||tf_ahead[2]!=1||tf_ahead[3]>65000u)return 0;
    if(tf_arec[2]>tf_ahead[3]||tf_arec[1]<16||tf_arec[1]>tf_ahead[3]-tf_arec[2])return 0;
    return 1;
}
/* The large native faces are already in video-plane order. The complete
 * five-entry directory for one pose fits a single cross-segment read. */
static int ovl_tf_bigstep(void)
{
    int i,first,last,y,rows;
    unsigned dir,seg,off;
    dir=os88_part_seg(3);
    if(!dir){tf_aready=0;return 0;}
    if(tf_abase>=0){
        os88_seg_get(tf_atarget,dir,16+72*tf_abase,40);
        for(i=0;i<5;i++)tf_asig[i]=tf_atarget[i][3];
        tf_abase=-1;tf_aload=-1;
    }
    if(tf_aload!=tf_aframe){
        os88_seg_get(tf_atarget,dir,16+72*tf_aframe,40);
        i=tf_atarget[0][0];
        if(i!=4&&i!=6&&i!=7){tf_aready=0;return 0;}
        seg=os88_part_seg(i);if(!seg){tf_aready=0;return 0;}
        os88_seg_get(tf_ahead,seg,0,8);off=tf_atarget[0][1];
        if(tf_ahead[0]!=0x5442||tf_ahead[1]!=0x4e41||tf_ahead[3]<5776u||
           off<16||off>tf_ahead[3]-5760u){tf_aready=0;return 0;}
        tf_amask=0;
        for(i=0;i<5;i++){
            if(tf_atarget[i][0]!=tf_atarget[0][0]||tf_atarget[i][1]!=off||
               tf_atarget[i][2]||!tf_atarget[i][3]){tf_aready=0;return 0;}
            if(tf_asig[i]!=tf_atarget[i][3])tf_amask|=1<<i;
            off+=288;
        }
        tf_aload=tf_aframe;
    }
    if(tf_amask){
        for(first=0;!(tf_amask&(1<<first));first++){}
        for(last=4;!(tf_amask&(1<<last));last--){}
        y=tf_ay+24*first;rows=24*(last-first+1);
        seg=os88_part_seg(tf_atarget[0][0]);if(!seg){tf_aready=0;return 0;}
        off=tf_atarget[first][1];
        if(bt_anim_planar(seg,off,tf_ax,y,rows)!=0){
            if(bx_rows(tf_ax,y,96,24)<24)return 0;
            bx_new(tf_ax,y,96,24,-1);bt_anim_rawstrip(seg,off);bx_out();last=first;
        }
        for(i=first;i<=last;i++){tf_asig[i]=tf_atarget[i][3];tf_amask&=~(1<<i);}
    }
    if(!tf_amask){tf_adone=1;tf_ashown=tf_aframe;tf_cosneeded=tf_capneeded;}
    return 1;
}
static int ovl_tf_anim_step(void)
{
    int i,j,k,n,raw,y;unsigned seg;
    if(!tf_aready||!tf_avisible){tf_adone=1;tf_cosneeded=tf_capneeded;return 0;}
    if(!tf_avariant)return ovl_tf_bigstep();
    n=2;
    if(tf_abase>=0){
        for(i=0;i<n;i++){if(!ovl_tf_anim_record(tf_abase,i)){tf_aready=0;return 0;}tf_asig[i]=tf_arec[3];}
        tf_abase=-1;tf_aload=-1;
    }
    if(tf_aload!=tf_aframe){
        tf_amask=0;
        for(i=0;i<n;i++){
            if(!ovl_tf_anim_record(tf_aframe,i)){tf_aready=0;return 0;}
            for(k=0;k<4;k++)tf_atarget[i][k]=tf_arec[k];
            if(tf_asig[i]!=tf_arec[3])tf_amask|=1<<i;
        }
        tf_aload=tf_aframe;
    }
    for(i=0;i<n&&tf_amask;i++){
        j=tf_apos;tf_apos++;if(tf_apos>=n)tf_apos=0;
        if(!(tf_amask&(1<<j)))continue;
        y=j*16;y+=j*8;y+=tf_ay;
        if(bx_rows(tf_ax,y,tf_aw,24)<24)return 0;
        /* Existing no-fill band form: the bounded decoder covers every bit. */
        bx_new(tf_ax,y,tf_aw,24,-1);
        raw=tf_aw*2;raw+=tf_aw;if(!tf_mono)raw*=4;seg=os88_part_seg(tf_atarget[j][0]);
        if(!seg||bt_anim_unpack(seg,tf_atarget[j][1],tf_atarget[j][2],raw)!=0){tf_aready=0;return 0;}
        bx_out();tf_asig[j]=tf_atarget[j][3];tf_amask&=~(1<<j);
        if(!tf_amask){tf_adone=1;tf_ashown=tf_aframe;tf_cosneeded=tf_capneeded;}
        return 1;
    }
    tf_adone=1;tf_ashown=tf_aframe;tf_cosneeded=tf_capneeded;return 0;
}
static int ovl_tf_cosmetics(void)
{
    int y,h;
    if(tf_compact){tf_render(tf_x+8,tf_statsy-10,tf_w-16,10,6,0);tf_cosneeded=0;tf_capneeded=0;return 1;}
    /* Two portrait jobs then one caption job; active blink gets its eye
     * patches first. Neither changed captions nor counters starve a pose. */
    if(tf_aready&&tf_avisible&&!tf_adone&&(tf_acapturn<2||!tf_capneeded||(!tf_aseq&&tf_aframe))){
        tf_acapturn++;if(ovl_tf_anim_step())return 1;
    }
    if(tf_capneeded){
        y=tf_py+(tf_ph>=150?132:60)+tf_capline;h=tf_py+tf_ph-4-y;if(h>4)h=4;
        if(h>0){tf_render(tf_px,y,tf_pw,h,5,0);tf_capline+=h;tf_acapturn=0;return 1;}
        tf_capneeded=0;
    }
    return ovl_tf_anim_step();
}
static unsigned tf_menukey(void)
{
    unsigned h;int i;BTProfile *p;
    p=&bt_store.profile[bt_store.selected];h=tf_mix(5381,bt_screen);
    h=tf_mix(h,bt_selection);h=tf_mix(h,bt_store.selected);h=tf_mix(h,bt_ch_diff);h=tf_mix(h,bt_ch_duration);
    h=tf_mix(h,bt_store.music+2*bt_store.effects+8*bt_store.keyclick+16*bt_store.roast+64*bt_store.display);
    h=tf_mix(h,bt_practice_weak+2*bt_full+4*bt_profile_edit+8*bt_readonly+16*bt_master_sound);
    h=tf_string(h,bt_notice);h=tf_string(h,p->name);
    if(bt_screen==BT_S_PROFILE)for(i=0;i<4;i++)h=tf_string(h,bt_store.profile[i].name);
    if(bt_screen==BT_S_RESULTS){h=tf_mix(h,bt_session.wpm);h=tf_mix(h,bt_session.accuracy);h=tf_string(h,bt_caption());for(i=0;i<4;i++)h=tf_string(h,bt_reaction_history(i));}
    return h;
}
void bt_face_init(unsigned art,unsigned band,unsigned cap,int bpp)
{
    int i;
    tf_art=art;tf_band=band;tf_cap=cap;tf_lastscreen=-1;tf_lw=-1;tf_lh=-1;tf_aexpr=-1;tf_acaption=0;tf_avisible=0;tf_aframe=0;tf_adone=0;tf_aload=-1;tf_ashown=-1;tf_acapturn=0;tf_idleturn=0;tf_capmsg=0;
    ovl_tf_anim_load(bpp);
    for(i=0;i<TF_KEYS;i++)tf_keycache[i]=255;
    for(i=0;i<TF_CELLS;i++){tf_cellchar[i]=255;tf_cellstyle[i]=255;}
    bx_setup(band,0,cap,art,bpp);gk_init(bpp);
}
void bt_face_move(unsigned was,unsigned now)
{
    /* Assignment only, including the shared band's cached segment. Root
     * updates the second art bank through bx_setart2 when its own part moves. */
    if(tf_art==was)tf_art=now;
    if(tf_band==was){tf_band=now;bx_bseg=now;}
    /* bx_setup refreshes the art base at the next layout/paint. */
    if(tf_art==now)tf_lw=-1;
}
void bt_face_paint(void *win,int full)
{
    int layout,i,c,s,x,y,w,h,oldview,oldinput,ncells,textchange,keychange,urgent;
    unsigned key;
    layout=tf_layout(win);if(!layout){tf_nhit=0;return;}
    if(layout==2||tf_lastscreen!=bt_screen)full=1;
    tf_lastscreen=bt_screen;if(full)tf_avisible=0;
    urgent=bt_screen==BT_S_PLAY&&(tf_savedattempt!=bt_session.attempts||tf_savedback!=bt_session.backspaces||tf_savedpos!=bt_session.pos||tf_savedmods!=bt_modifiers);
    if(full&&(tf_x!=tf_org.x||tf_x+tf_w!=tf_org.x+tf_sz.w)){
        os88_set_color(tf_ground());
        if(tf_x>tf_org.x)os88_gfx_fill(tf_org.x,tf_y,tf_x-1,tf_y+tf_h-1);
        if(tf_x+tf_w<tf_org.x+tf_sz.w)os88_gfx_fill(tf_x+tf_w,tf_y,tf_org.x+tf_sz.w-1,tf_y+tf_h-1);
    }
    key=tf_mix(5381,bt_screen);key=tf_mix(key,bt_store.selected);
    if(full||key!=tf_header){tf_render(tf_x,tf_y,tf_w,tf_top-tf_y,1,0);tf_header=key;}
    key=tf_string(tf_mix(5381,bt_readonly+2*bt_master_sound),bt_notice);
    if(full||key!=tf_footer){if(full||bt_screen!=BT_S_PLAY){tf_render(tf_x,tf_bottom,tf_w,tf_h-(tf_bottom-tf_y),2,0);tf_footpending=0;}
        else {tf_footpending=1;tf_footrow=0;}tf_footer=key;}
    if(bt_screen!=BT_S_PLAY){key=tf_menukey();
        if(full||key!=tf_bodykey){if(!full&&bt_screen==BT_S_SETTINGS)ovl_tf_settings_delta(tf_settings^tf_settings_mask());
            else tf_render(tf_x,tf_top,tf_w,tf_bottom-tf_top,3,0);tf_bodykey=key;}
        if(bt_screen==BT_S_SETTINGS){tf_settings=tf_settings_mask();key=tf_string(5381,tf_soundstatus());
            if(!full&&key!=tf_vstatus)tf_render(tf_vsx,tf_vsy,256,tf_th,11,0);tf_vstatus=key;}
        if(!full&&tf_cosneeded)ovl_tf_anim_step();
        if(!tf_compact&&tf_meterlast!=bt_music_phase)ovl_tf_meter();
        return;
    }
    tf_nhit=0;oldview=tf_view;oldinput=tf_inputview;
    textchange=full||tf_savedpos!=bt_session.pos||tf_savedlen!=bt_session.length||tf_savedcomplete!=bt_session.completed;
    keychange=textchange||tf_savedwrong!=bt_session.last_wrong||tf_savedmods!=bt_modifiers;
    urgent=urgent||keychange;
    if(keychange)tf_keyrefresh(full);
    if(textchange){tf_view=(bt_session.pos/(2*tf_cols))*(2*tf_cols);
    tf_inputview=(bt_session.pos/tf_inputcols)*tf_inputcols;
    }
    ncells=tf_compact&&tf_h<140?2*tf_cols:2*tf_cols+tf_inputcols;
    if(full)tf_progressfill=ovl_tf_progress_width();
    if(full){tf_render(tf_x,tf_top,tf_w,tf_bottom-tf_top,4,0);}
    key=bt_expression();
    if(full||tf_portrait!=key){tf_portrait=key;if(!tf_compact)tf_cosneeded=1;}
    key=tf_string(5381,bt_caption());
    if(full||key!=tf_captionkey){tf_captionkey=key;tf_cosneeded=1;tf_capneeded=1;tf_capline=0;}
    key=tf_mix(tf_target(),bt_session.status);key=tf_mix(key,bt_session.countdown);key=tf_mix(key,tf_go_left!=0);key=tf_mix(key,bt_session.last_wrong);
    if(full||key!=tf_hintkey){if(full&&tf_compact){i=tf_w>=600?280:220;tf_render(tf_x+i,tf_y+2,tf_w-i-70,8,7,0);}tf_hintpending=!full;tf_hintrow=0;tf_hintkey=key;}
    if(textchange){
        for(i=0;i<2;i++){tf_dirtyfirst[i]=-1;tf_dirtylast[i]=-1;}if(full){tf_dirtyfirst[2]=-1;tf_dirtylast[2]=-1;}
        if(full||oldview!=tf_view||oldinput!=tf_inputview||tf_savedlen!=bt_session.length||tf_savedcomplete!=bt_session.completed||tf_savedpos+1<bt_session.pos||bt_session.pos+1<tf_savedpos){
            for(i=0;i<ncells;i++)tf_cache_cell(i,full);
        }else{
            i=tf_savedpos-tf_view;if(i>=0&&i<2*tf_cols)tf_cache_cell(i,0);
            i=bt_session.pos-tf_view;if(i>=0&&i<2*tf_cols)tf_cache_cell(i,0);
            i=2*tf_cols+tf_savedpos-tf_inputview;if(i>=2*tf_cols&&i<ncells)tf_cache_cell(i,0);
            i=2*tf_cols+bt_session.pos-tf_inputview;if(i>=2*tf_cols&&i<ncells)tf_cache_cell(i,0);
        }
        if(!full)for(i=0;i<2;i++)if(tf_dirtyfirst[i]>=0){
            tf_cellrect(tf_dirtyfirst[i]);x=tf_sx;y=tf_sy;h=tf_sh;w=(tf_dirtylast[i]-tf_dirtyfirst[i]+1)*tf_cw;
            if(w>tf_cw){tf_render(x,y,tf_cw,h,8,tf_dirtyfirst[i]);tf_render(x+tf_cw,y,w-tf_cw,h,8,tf_dirtyfirst[i]+1);}
            else tf_render(x,y,w,h,8,tf_dirtyfirst[i]);
        }
    }
    if(tf_savedcaret!=tf_caret_on&&(full||tf_dirtyfirst[2]<0)){i=2*tf_cols+bt_session.pos-tf_inputview;if(i>=2*tf_cols&&i<ncells)tf_cache_cell(i,1);tf_savedcaret=tf_caret_on;}
    for(i=0;i<7;i++){
        if(i==0)key=bt_session.seconds?bt_session.wpm:65535u;
        else if(i==1)key=bt_session.attempts?bt_session.accuracy:65535u;
        else if(i==2)key=bt_session.streak;
        else if(i==3)key=bt_session.mode==BT_CHALLENGE?bt_session.remaining:bt_session.seconds;
        else if(i==4)key=bt_session.errors;
        else if(i==5)key=bt_session.mode==BT_CHALLENGE?bt_session.remaining:bt_session.pos;
        else key=bt_session.mode==BT_CHALLENGE?bt_session.remaining:0;
        if(full||key!=tf_statkey[i]){if(!full){tf_statpend|=1<<i;if(i<5)tf_statrow[i]=0;}tf_statkey[i]=key;}
    }
    if(keychange)for(i=0;i<TF_KEYS;i++){s=tf_keystate(i);if(full||s!=tf_keycache[i]){
        if(!full){c=s^tf_keycache[i];tf_keyrect(i);x=tf_kx;y=tf_ky0;w=tf_kww;h=tf_khh;
            if(h>=20){
                if(c&3){os88_set_color(s&3?(tf_mono?0:TF_YELLOW):tf_keycolor(i));os88_gfx_frame(x+4,y+4,x+w-6,y+h-6);}
                if(c&20){os88_set_color(tf_keycolor(i));os88_gfx_fill(x+w-9,y+4,x+w-5,y+11);
                    if(s&3){os88_set_color(tf_mono?0:TF_YELLOW);os88_gfx_hline(x+w-9,x+w-6,y+4);os88_gfx_fill(x+w-6,y+5,x+w-6,y+11);}
                    if(s&4){os88_set_color(tf_mono?0:TF_RED);os88_gfx_fill(x+w-7,y+4,x+w-7,y+7);os88_gfx_fill(x+w-7,y+10,x+w-7,y+10);}
                    else if((s&16)&&!(s&1)){os88_set_color(tf_mono?0:TF_GREEN);os88_gfx_hline(x+w-9,x+w-5,y+7);os88_gfx_fill(x+w-7,y+5,x+w-7,y+9);}}
                if(c&8){os88_set_color(s&8?(tf_mono?0:15):tf_keycolor(i));os88_gfx_hline(x+5,x+w-7,y+h-7);}
            }else tf_render(x,y,w,h,10,i);
        }tf_keycache[i]=s;}}
    tf_savedpos=bt_session.pos;tf_savedlen=bt_session.length;tf_savedcomplete=bt_session.completed;
    tf_savedwrong=bt_session.last_wrong;tf_savedmods=bt_modifiers;
    tf_savedattempt=bt_session.attempts;tf_savedback=bt_session.backspaces;
    if(full){tf_cosneeded=tf_aready&&tf_avisible;tf_capneeded=0;tf_statpend=0;tf_hintpending=0;tf_dirtyfirst[2]=-1;}
    else if(!urgent&&!bt_key_pending()&&!os88_evq_pending()){
        tf_idleturn^=1;
        if(tf_cosneeded&&(tf_idleturn||(!tf_aseq&&tf_aframe>0&&tf_aframe<4)))ovl_tf_cosmetics();
        else if(tf_dirtyfirst[2]>=0){i=tf_dirtyfirst[2];tf_cellrect(i);x=tf_sx;y=tf_sy;h=tf_sh;tf_render(x,y,tf_cw,h,8,i);tf_dirtyfirst[2]++;if(tf_dirtyfirst[2]>tf_dirtylast[2])tf_dirtyfirst[2]=-1;}
        else if(tf_footpending){h=tf_h-(tf_bottom-tf_y)-tf_footrow;if(h>4)h=4;tf_render(tf_x,tf_bottom+tf_footrow,tf_w,h,2,0);tf_footrow+=h;if(tf_footrow>=tf_h-(tf_bottom-tf_y))tf_footpending=0;}
        else if(tf_hintpending&&(!tf_statpend||(bt_session.last_tick&1))){h=tf_hinth-tf_hintrow;if(h>4)h=4;
            if(tf_compact){i=tf_w>=600?280:220;tf_render(tf_x+i,tf_hinty+tf_hintrow,tf_w-i-70,h,7,0);}
            else tf_render(tf_promptx,tf_hinty+tf_hintrow,tf_cols*tf_cw<344?tf_cols*tf_cw:344,h,7,0);
            tf_hintrow+=h;if(tf_hintrow>=tf_hinth)tf_hintpending=0;}
        else if(tf_statpend){for(c=0;c<7;c++){i=tf_statnext;tf_statnext++;if(tf_statnext>=7)tf_statnext=0;if(tf_statpend&(1<<i)){
            if(i<5){s=tf_stath>=30?16:tf_compact?8:10;h=s-tf_statrow[i];if(h>4)h=4;
                tf_render(tf_statsx+i*(tf_statsw/5)+4,tf_statsy+(tf_stath>=30?12:tf_compact?1:4)+tf_statrow[i],tf_statsw/5-8,h,9,i);
                tf_statrow[i]+=h;if(tf_statrow[i]<s)break;}
            else if(!tf_compact&&i==5){s=ovl_tf_progress_width();x=tf_progressfill<s?tf_progressfill:s;w=tf_progressfill<s?s-tf_progressfill:tf_progressfill-s;tf_progressfill=s;if(w)tf_render(tf_promptx+1+x,tf_statsy-8,w,2,12,0);}
            else if(!tf_compact&&bt_session.mode==BT_CHALLENGE)tf_render(tf_x+tf_w-88,tf_py+1,72,16,13,0);
            tf_statpend&=~(1<<i);break;}}}
        else if(tf_cosneeded)ovl_tf_cosmetics();
        else if(!tf_compact&&tf_meterlast!=bt_music_phase)ovl_tf_meter();
    }
}
static int ovl_tf_click(int x,int y)
{
    int i;
    if(y>=tf_top-(tf_compact?12:22)&&y<tf_top&&x>=tf_x+8){
        for(i=0;i<6;i++)if(x<ovl_tf_tabx(i+1))return BT_A_LESSONS+i;
    }
    if(bt_screen==BT_S_PLAY){for(i=0;i<TF_KEYS;i++){tf_keyrect(i);if(tf_kww>0&&x>=tf_kx&&x<tf_kx+tf_kww&&y>=tf_ky0&&y<tf_ky0+tf_khh)return BT_A_HINT_BASE+tf_keys[i][0];}return 0;}
    for(i=0;i<tf_nhit;i++)if(x>=tf_hit[i][0]&&x<tf_hit[i][0]+tf_hit[i][2]&&y>=tf_hit[i][1]&&y<tf_hit[i][1]+tf_hit[i][3])return tf_hit[i][4];
    return 0;
}

int bt_face_click(int x,int y)
{return ovl_tf_click(x,y);}
