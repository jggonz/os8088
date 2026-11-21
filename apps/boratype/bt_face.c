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
#define TF_COS_MAX 640
static struct os88_pt tf_org;
static struct os88_size tf_sz;
static struct os88_video tf_vid;
static unsigned tf_art,tf_band,tf_cap;
static int tf_x,tf_y,tf_w,tf_h,tf_mono,tf_compact,tf_font,tf_title,tf_th;
static int tf_top,tf_bottom,tf_px,tf_py,tf_pw,tf_ph,tf_ky,tf_kh,tf_kw;
static int tf_cols,tf_cw,tf_ch,tf_promptx,tf_prompty,tf_inputy,tf_statsy;
static int tf_lx,tf_ly,tf_lw,tf_lh,tf_lastscreen,tf_view,tf_inputview;
static int tf_sx,tf_sy,tf_sw,tf_sh;
static unsigned char tf_glyphw[95];
static int tf_keyx[TF_KEYS],tf_keyw[TF_KEYS],tf_rowy[5];
static unsigned tf_savedpos,tf_savedlen,tf_savedcomplete;
static int tf_savedwrong,tf_savedmods;
static unsigned tf_savedattempt,tf_savedback;
static unsigned char tf_coskey[TF_COS_MAX];
static int tf_cosrows,tf_cosn,tf_cospos,tf_cosneeded,tf_cosearch,tf_capepoch,tf_capline,tf_capneeded;
static int tf_statsx,tf_statsw,tf_hintpending,tf_statpend,tf_statnext,tf_footpending,tf_footrow;
static int tf_dirtyfirst[3],tf_dirtylast[3];
static int tf_vsx,tf_vsy; /* independent sound-settings status line */
static unsigned tf_vstatus,tf_settings;
extern int bt_music_phase;
static int tf_meterlast;
static int tf_nhit,tf_hit[TF_HITS][5];
static unsigned tf_header,tf_bodykey,tf_footer,tf_portrait,tf_captionkey,tf_hintkey;
static unsigned tf_statkey[5];
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
    {32,32,4,14,28,0},
    {18,0,4,0,7,1},
    {19,0,4,7,7,1},
    {21,0,4,42,7,8},
    {22,0,4,49,11,8},
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
static const unsigned char tf_zones[]={7,12,6,14,10,11,9,5,12};
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
static void ovl_tf_lines(int x,int y,int w,int rows,const char *s)
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
        tf_line(x,y,w,tf_wrap);s+=n;while(*s==' ')s++;y+=tf_th+2;
    }
}
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
    if(i<47){tf_char[0]=tf_keys[i][0];if(tf_char[0]>='a'&&tf_char[0]<='z')tf_char[0]-=32;tf_char[1]=0;return tf_char;}
    if(i==47)return "Back";if(i==48)return "Tab";if(i==49)return "Caps";
    if(i==50)return "Enter";if(i==51||i==52)return "Shift";
    if(i==53)return "SPACE";if(i==54||i==57)return "Ctrl";
    if(i==55||i==56)return "Alt";return "";
}
static void tf_drawkey(int i,int selective)
{
    int state,c,f,fh,lx,ly,lw;const char *s;
    tf_keyrect(i);if(tf_kww<=0||tf_kx+tf_kww<=tf_sx||tf_kx>=tf_sx+tf_sw||tf_ky0+tf_khh<=tf_sy||tf_ky0>=tf_sy+tf_sh)return;
    state=tf_keystate(i);c=tf_mono?15:tf_zones[tf_keys[i][5]];
    gk_box(tf_kx,tf_ky0,tf_kww,tf_khh,GK_RAISED2,c);
    if(state&3){c=tf_mono?0:TF_YELLOW;
        bx_fill(tf_kx+3,tf_ky0+3,tf_kx+tf_kww-4,tf_ky0+3,c);
        bx_fill(tf_kx+3,tf_ky0+tf_khh-4,tf_kx+tf_kww-4,tf_ky0+tf_khh-4,c);
        bx_fill(tf_kx+3,tf_ky0+4,tf_kx+3,tf_ky0+tf_khh-5,c);
        bx_fill(tf_kx+tf_kww-4,tf_ky0+4,tf_kx+tf_kww-4,tf_ky0+tf_khh-5,c);}
    s=tf_keylabel(i);f=tf_compact?BT_F_SMALL:(tf_kww<34?-1:tf_khh>=26?BT_F_TITLE:tf_font);fh=f==BT_F_TITLE?16:f<0||tf_compact?8:10;
    ly=tf_ky0+1+(tf_khh-3-fh)/2;
    if(!selective||(ly<tf_sy+tf_sh&&ly+fh>tf_sy)){
        lw=bx_twidth(s,f);lx=tf_kx+2+(tf_kww-12-lw)/2;
        if(!selective||(lx<tf_sx+tf_sw&&lx+lw>tf_sx))gk_textbox(tf_kx+2,tf_ky0+1,tf_kww-12,tf_khh-3,s,0,f,GK_CENTRE|GK_FIT);
    }
    if(i<47&&tf_khh>=26&&tf_keys[i][1]!=tf_keys[i][0]&&tf_keys[i][0]<'a'&&tf_kx+6<tf_sx+tf_sw&&tf_kx+14>tf_sx&&tf_ky0+5<tf_sy+tf_sh&&tf_ky0+13>tf_sy){
        tf_char[0]=tf_keys[i][1];tf_char[1]=0;bx_text(tf_kx+6,tf_ky0+5,tf_char,0,BT_F_SMALL);}
    if(tf_kx+tf_kww-12<tf_sx+tf_sw&&tf_kx+tf_kww-4>tf_sx&&tf_ky0+4<tf_sy+tf_sh&&tf_ky0+12>tf_sy){
        if((state&16)&&!(state&5))bx_text(tf_kx+tf_kww-12,tf_ky0+4,"+",tf_mono?0:TF_GREEN,-1);
        if(state&4)bx_text(tf_kx+tf_kww-12,tf_ky0+4,"!",tf_mono?0:TF_RED,-1);
    }
    if(state&8)bx_fill(tf_kx+4,tf_ky0+tf_khh-5,tf_kx+tf_kww-5,tf_ky0+tf_khh-5,tf_mono?0:15);
    if(i==29||i==32)bx_fill(tf_kx+tf_kww/2-3,tf_ky0+tf_khh-6,tf_kx+tf_kww/2+2,tf_ky0+tf_khh-6,0);
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
    tf_kw=(tf_w-16)/15;tf_kh=tf_compact?(tf_h<154?10:12):(tf_h>=400?28:22);
    tf_ky=tf_bottom-5*tf_kh-2;tf_statsy=tf_ky-(tf_compact?11:24);
    tf_px=tf_x+8;tf_py=tf_top+4;tf_pw=tf_compact?0:(tf_w>=760?180:136);tf_ph=tf_ky-tf_py-5;
    tf_promptx=tf_x+(tf_compact?8:tf_pw+24);tf_prompty=tf_top+(tf_compact?2:30);
    space=tf_x+tf_w-12-tf_promptx;
    tf_cw=tf_compact?8:(tf_h>=400?16:12);tf_ch=tf_compact?8:(tf_h>=400?28:16);
    tf_cols=space/tf_cw;if(tf_cols>40)tf_cols=40;if(tf_cols<12)tf_cols=12;
    tf_inputy=tf_prompty+2*tf_ch+(tf_compact?2:12);
    bx_setup(tf_band,0,tf_cap,tf_art,tf_mono?1:4);gk_init(tf_mono?1:4);gk_theme(tf_mono?tf_monoroles:tf_roles);
    for(i=0;i<95;i++){tf_char[0]=i+32;tf_char[1]=0;tf_glyphw[i]=bx_twidth(tf_char,tf_font);}
    for(i=0;i<TF_KEYS;i++){tf_keyx[i]=tf_x+8+tf_keys[i][3]*tf_kw/4;tf_keyw[i]=tf_keys[i][4]*tf_kw/4-2;}
    for(i=0;i<5;i++)tf_rowy[i]=tf_ky+i*tf_kh;
    tf_statsx=tf_compact?tf_x:tf_promptx-4;tf_statsw=tf_compact?tf_w:tf_x+tf_w-8-tf_statsx;
    tf_cosrows=1;while((tf_ph+tf_cosrows-1)/tf_cosrows>TF_COS_MAX)tf_cosrows++;
    tf_cosn=(tf_ph+tf_cosrows-1)/tf_cosrows;if(tf_cosn<1)tf_cosn=1;
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
    w=(tf_w-16)/6;
    for(i=0;i<6;i++){
        font=tf_compact?BT_F_SMALL:(bx_twidth(tabs[i],BT_F_TITLE)<=w-3?BT_F_TITLE:BT_F_TEXT);
        x=tf_x+8+i*w;
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
static void ovl_tf_drawportrait(void)
{
    int id,w,h,e;
    if(tf_compact||tf_px>=tf_sx+tf_sw||tf_px+tf_pw<=tf_sx||tf_py>=tf_sy+tf_sh||tf_py+tf_ph<=tf_sy)return;
    e=bt_expression();if(e<0||e>6)e=0;
    id=tf_mono?BT_ART_MONO+e:(tf_ph>=150?BT_ART_BIG+e:BT_ART_SMALL+e);
    w=(!tf_mono&&tf_ph>=150)?96:40;h=(!tf_mono&&tf_ph>=150)?120:48;
    if(e==1&&bt_blink_frame)id=tf_mono?BT_ART_BLINK_MONO:(tf_ph>=150?BT_ART_BLINK_BIG:BT_ART_BLINK_SMALL);
    gk_box(tf_px,tf_py,tf_pw,tf_ph,GK_RAISED,tf_ground());
    if(tf_py+6<tf_sy+tf_sh&&tf_py+6+h>tf_sy)bx_spr(tf_px+(tf_pw-w)/2,tf_py+6,id);

}
static void ovl_tf_drawcaption(void)
{
    int x,y,w,rows;
    if(tf_compact){x=tf_x+8;y=tf_statsy-10;w=tf_w-16;rows=1;}
    else{x=tf_px+6;y=tf_py+(tf_ph>=150?132:60);w=tf_pw-12;rows=(tf_py+tf_ph-y-5)/(tf_th+2);}
    if(rows>0)ovl_tf_lines(x,y,w,rows,bt_caption());
}
static void ovl_tf_drawhint(void)
{
    int c,i,n;
    if(tf_compact){if(tf_y+2>=tf_sy+tf_sh||tf_y+12<=tf_sy)return;}
    else if(tf_top+6>=tf_sy+tf_sh||tf_top+24<=tf_sy)return;
    c=tf_target();i=tf_keyid(c);n=0;
    if(bt_session.status==BT_READY){n=tf_append(n,"READY - starts in ");tf_number(n,bt_session.countdown);}
    else if(i>=0){n=tf_append(n,"NEXT ");tf_char[0]=c;tf_char[1]=0;n=tf_append(n,c==32?"SPACE":tf_char);
        n=tf_append(n,"  ");n=tf_append(n,tf_fingers[tf_keys[i][5]]);
        if(tf_shift(c)>=0)tf_append(n,tf_shift(c)==51?" + left Shift":" + right Shift");}
    else tf_append(0,"F4 Resume / Pause");
    if(tf_compact){i=tf_w>=600?280:220;tf_text(tf_x+i,tf_y+2,tf_w-i-70,tf_buf,TF_YELLOW,BT_F_SMALL);}
    else tf_text(tf_promptx,tf_top+6,tf_w-(tf_promptx-tf_x)-12,tf_buf,TF_YELLOW,tf_font);
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
    if(p==bt_session.pos)return 2;
    return p<bt_session.pos?1:0;
}
static void tf_cache_cell(int i,int force)
{
    int c,s,row;c=tf_cellc(i);s=tf_cellstate(i);
    if(force||c!=tf_cellchar[i]||s!=tf_cellstyle[i]){
        tf_cellchar[i]=c;tf_cellstyle[i]=s;row=i/tf_cols;
        if(tf_dirtyfirst[row]<0||i<tf_dirtyfirst[row])tf_dirtyfirst[row]=i;
        if(i>tf_dirtylast[row])tf_dirtylast[row]=i;
    }
}
static void tf_cellrect(int j)
{
    tf_sx=tf_promptx+(j%tf_cols)*tf_cw;
    tf_sy=j<2*tf_cols?tf_prompty+(j/tf_cols)*tf_ch:tf_inputy;
    tf_sw=tf_cw;tf_sh=tf_ch;
}
static void tf_drawcell(int j)
{
    int c,s,x,y,col,font;
    if(tf_compact&&tf_h<140&&j>=2*tf_cols)return;
    c=tf_cellc(j);s=tf_cellstate(j);x=tf_promptx+(j%tf_cols)*tf_cw;
    y=j<2*tf_cols?tf_prompty+(j/tf_cols)*tf_ch:tf_inputy;
    if(x+tf_cw<=tf_sx||x>=tf_sx+tf_sw||y+tf_ch<=tf_sy||y>=tf_sy+tf_sh)return;
    col=s==1?TF_GREEN:TF_WHITE;font=tf_compact?-1:(tf_h>=400?BT_F_LARGE:BT_F_TEXT);
    if(s==2){bx_fill(x,y,x+tf_cw-1,y+tf_ch-1,tf_mono?0:TF_YELLOW);col=0;}
    tf_char[0]=c;tf_char[1]=0;
    bx_text(x+(tf_cw-bx_twidth(tf_char,font))/2,y+(tf_compact?0:2),tf_char,tf_mono?(s==2?15:0):col,font);
    if(s==1&&tf_mono)bx_fill(x,y+tf_ch-1,x+tf_cw-2,y+tf_ch-1,0);
    if(s==2)bx_fill(x+1,y+tf_ch-2,x+tf_cw-2,y+tf_ch-2,tf_mono?15:0);
}
static void ovl_tf_drawprompt(void)
{
    int i,x,w;
    ovl_tf_drawhint();
    if(!tf_compact){x=tf_promptx-4;w=tf_w-(x-tf_x)-8;
        gk_box(x,tf_prompty-5,w,2*tf_ch+10,GK_SUNKEN2,tf_ground());
        if(tf_inputy+tf_ch+13<tf_statsy)tf_text(x,tf_inputy+tf_ch+5,w,"ACCEPTED INPUT  |  Wrong keys stay on the same target",TF_WHITE,BT_F_SMALL);
        gk_box(x,tf_inputy-3,w,tf_ch+6,GK_SUNKEN,tf_ground());
        if(bt_session.mode==BT_LESSON&&tf_inputy+tf_ch+20+2*(tf_th+2)<tf_statsy)ovl_tf_lines(x,tf_inputy+tf_ch+20,w,2,bt_lessons[bt_session.lesson].instruction);
    }
    if(tf_prompty<tf_sy+tf_sh&&tf_prompty+tf_ch>tf_sy)for(i=0;i<tf_cols;i++)tf_drawcell(i);
    if(tf_prompty+tf_ch<tf_sy+tf_sh&&tf_prompty+2*tf_ch>tf_sy)for(i=tf_cols;i<2*tf_cols;i++)tf_drawcell(i);
    if(tf_inputy<tf_sy+tf_sh&&tf_inputy+tf_ch>tf_sy)for(i=2*tf_cols;i<3*tf_cols;i++)tf_drawcell(i);
}
static void ovl_tf_drawstat(int i)
{
    int x,w,n;static const char *labels[]={"WPM ","ACC ","STREAK ","TIME ","ERR "};
    w=tf_statsw/5;x=tf_statsx+i*w;
    if(x+w<=tf_sx||x>=tf_sx+tf_sw||tf_statsy+24<=tf_sy||tf_statsy>=tf_sy+tf_sh)return;
    n=tf_append(0,labels[i]);
    if(i==0){if(bt_session.seconds)tf_number(n,bt_session.wpm);else tf_append(n,"-");}
    if(i==1){if(bt_session.attempts){n=tf_number(n,bt_session.accuracy);tf_append(n,"%");}else tf_append(n,"-");}
    if(i==2)tf_number(n,bt_session.streak);
    if(i==3){n=tf_number(n,bt_session.mode==BT_CHALLENGE?bt_session.remaining:bt_session.seconds);tf_append(n,"s");}
    if(i==4)tf_number(n,bt_session.errors);
    if(x+w<=tf_sx||x>=tf_sx+tf_sw||tf_statsy+24<=tf_sy||tf_statsy>=tf_sy+tf_sh)return;
    tf_text(x+4,tf_statsy+(tf_compact?1:5),w-8,tf_buf,TF_YELLOW,tf_font);
}
static void tf_playbase(void)
{
    int i;
    ovl_tf_drawportrait();ovl_tf_drawcaption();ovl_tf_drawprompt();
    for(i=0;i<5;i++)ovl_tf_drawstat(i);
    for(i=0;i<TF_KEYS;i++)tf_drawkey(i,0);
}
static const char *tf_achnames[]={"First lesson","95% mastery","25-key streak","50-key streak","95% challenge","Perfect lesson","Personal best"};
static void ovl_tf_resultcard(int x,int y,int w,BTProfile *p)
{
    static const char *labels[]={"SPEED","ACCURACY","ERRORS","BEST STREAK","TIME"};
    int pw,ph,sx,sw,sy,rh,i,n,k,e,iw,ih;
    pw=w>=740?180:136;ph=tf_bottom-y-36;sx=x+pw+12;sw=w-pw-12;
    e=bt_expression();if(e<0||e>6)e=0;
    iw=!tf_mono&&ph>=188?96:40;ih=iw==96?120:48;
    if(x+pw>tf_sx&&x<tf_sx+tf_sw&&y+ph>tf_sy&&y<tf_sy+tf_sh){
        gk_box(x,y,pw,ph,GK_RAISED,tf_ground());
        bx_spr(x+(pw-iw)/2,y+8,tf_mono?BT_ART_MONO+e:iw==96?BT_ART_BIG+e:BT_ART_SMALL+e);
        tf_append(tf_append(0,"Borat: "),bt_caption());
        ovl_tf_lines(x+6,y+ih+16,pw-12,(ph-ih-22)/(tf_th+2),tf_buf);
    }
    n=tf_append(0,!bt_session.attempts?"No keys entered":bt_session.ranked?"Completed":"Practice / unranked");
    if(bt_session.new_record)tf_append(n,"  PERSONAL BEST!");tf_line(sx,y,sw,tf_buf);
    sy=y+14;rh=ph>=250?26:18;
    gk_box(sx,sy,sw,rh*5,GK_SUNKEN2,tf_ground());
    for(i=0;i<5;i++){
        if(sy+i*rh+rh<=tf_sy||sy+i*rh>=tf_sy+tf_sh)continue;
        tf_text(sx+6,sy+i*rh+2,sw/2-8,labels[i],TF_WHITE,BT_F_TITLE);
        n=0;
        if(i==0){n=bt_session.seconds?tf_number(0,bt_session.wpm):tf_append(0,"-");tf_append(n," WPM");}
        else if(i==1){if(bt_session.attempts){n=tf_number(0,bt_session.accuracy);tf_append(n,"%");}else tf_append(0,"-");}
        else if(i==2)tf_number(0,bt_session.errors);
        else if(i==3)tf_number(0,bt_session.best_streak);
        else{n=tf_number(0,bt_session.seconds);tf_append(n," sec");}
        tf_text(sx+sw/2,sy+i*rh+2,sw/2-6,tf_buf,TF_YELLOW,BT_F_TITLE);
        if(i<4)bx_fill(sx+3,sy+(i+1)*rh-1,sx+sw-4,sy+(i+1)*rh-1,tf_ink(TF_CYAN));
    }
    sy+=5*rh+6;n=tf_append(0,"Weak keys: ");
    for(i=0;i<3;i++){k=bt_weak(i);if(k){tf_char[0]=k;tf_char[1]=0;n=tf_append(n,tf_char);n=tf_append(n," (");n=tf_number(n,p->key_error[k-32]);n=tf_append(n,"/");n=tf_number(n,p->key_attempt[k-32]);n=tf_append(n,") ");}}
    if(n==11)tf_append(n,"20 samples per key needed");
    k=bx_twidth(tf_buf,tf_font)>sw?2:1;ovl_tf_lines(sx,sy,sw,k,tf_buf);sy+=k*(tf_th+2)+2;
    k=(tf_bottom-34-sy)/(tf_th+2);if(k>2)k=2;
    ovl_tf_lines(sx,sy,sw,k,!bt_session.attempts?"No keys were entered. Try F and J, or Retry the test.":bt_session.accuracy>=95?"Great success! Continue, or practice the next keys.":"Aim for 95% accuracy. Practice these keys before adding speed.");sy+=k*(tf_th+2)+3;
    if(sy+tf_th+2<=tf_bottom-34){
        n=tf_append(0,"New achievements: ");
        for(i=0;i<7;i++)if(bt_session.achievement_new&(1<<i)){n=tf_append(n,tf_achnames[i]);n=tf_append(n,"! ");}
        if(!bt_session.achievement_new)tf_append(n,"Keep practicing");
        k=sy+2*(tf_th+2)<=tf_bottom-34?2:1;ovl_tf_lines(sx,sy,sw,k,tf_buf);sy+=k*(tf_th+2)+3;
    }
    for(i=0;i<4&&sy+tf_th<=tf_bottom-34;i++){
        if(!bt_reaction_history(i)[0])break;
        n=tf_append(0,"Borat: ");tf_append(n,bt_reaction_history(i));tf_line(sx,sy,sw,tf_buf);sy+=tf_th+2;
    }
    n=(w-12)/4;y=tf_bottom-29;
    tf_button(x,y,n,25,"Retry",BT_A_RETRY,1);tf_button(x+n+4,y,n,25,"Next lesson",BT_A_NEXT,0);
    tf_button(x+2*(n+4),y,n,25,"Weak keys",BT_A_WEAK,0);tf_button(x+3*(n+4),y,n,25,"Menu",BT_A_BACK,0);
}

/* Cold menu screens share this body; all coordinates are in live content. */
static void ovl_tf_drawbody(void)
{
    int x,y,w,bh,row,i,n,c,cols,rh,d,k;
    BTProfile *p;
    p=&bt_store.profile[bt_store.selected];tf_nhit=0;
    x=tf_x+12;y=tf_top+5;w=tf_w-24;
    bh=tf_compact?(tf_h<140?13:15):25;rh=tf_compact?11:20;
    tf_text(x,y,w,tf_screenname[bt_screen],TF_YELLOW,tf_title);y+=tf_compact?14:26;
    if(bt_screen==BT_S_HOME){
        if(!tf_compact){ovl_tf_drawportrait();x+=tf_pw+16;w-=tf_pw+16;ovl_tf_lines(x,y,w,2,"Put your fingers on F and J. Borat will supervise the great success.");y+=34;}
        n=tf_append(0,"Profile: ");n=tf_append(n,p->name);tf_append(n,bt_readonly?"  (read only)":"");tf_line(x,y,w,tf_buf);y+=rh+4;
        c=(w-8)/2;
        tf_button(x,y,c,bh,"Continue lesson",BT_A_START,1);tf_button(x+c+8,y,c,bh,"Choose lesson",BT_A_LESSONS,0);y+=bh+4;
        tf_button(x,y,c,bh,"Practice",BT_A_PRACTICE,0);tf_button(x+c+8,y,c,bh,"Timed challenge",BT_A_CHALLENGE,0);y+=bh+4;
        tf_button(x,y,c,bh,"Profiles",BT_A_PROFILE,0);tf_button(x+c+8,y,c,bh,"Records",BT_A_RECORDS,0);
    }else if(bt_screen==BT_S_LESSONS){
        cols=tf_compact?(tf_h<150?4:3):2;c=(w-8*(cols-1))/cols;row=(BT_LESSONS+cols-1)/cols;
        rh=(tf_bottom-y-4)/row;if(rh>28)rh=28;if(rh<10)rh=10;
        for(i=0;i<BT_LESSONS;i++){
            n=0;if(p->lesson_best[i]>=95)n=tf_append(n,"* ");else if(p->lesson_done[i])n=tf_append(n,"+ ");
            n=tf_number(n,i+1);n=tf_append(n," ");tf_append(n,bt_lessons[i].name);
            tf_button(x+(i/row)*(c+8),y+(i%row)*rh,c,rh-1,tf_buf,BT_A_LESSON_BASE+i,bt_selection==i);
        }
    }else if(bt_screen==BT_S_PRACTICE){
        c=(w-8)/2;
        for(i=0;i<4;i++)tf_button(x+(i%2)*(c+8),y+(i/2)*(bh+4),c,bh,tf_group[i],BT_A_PRACTICE_BASE+i,!bt_practice_weak&&bt_ch_diff==i);
        y+=2*(bh+4);n=tf_append(0,"Weak keys: ");
        for(i=0;i<3;i++){k=bt_weak(i);if(k){tf_char[0]=k;tf_char[1]=' ';tf_char[2]=0;n=tf_append(n,tf_char);}}
        if(n==11)tf_append(n,"20 attempts per key needed");
        if(!tf_compact){tf_line(x,y,w,tf_buf);y+=rh+2;tf_line(x,y,w,"Recommended by errors / attempts after at least 20 samples.");y+=rh+3;}
        tf_button(x,y,c,bh,"Practice weak keys",BT_A_WEAK,bt_practice_weak);tf_button(x+c+8,y,c,bh,"Start practice",BT_A_START,1);
    }else if(bt_screen==BT_S_CHALLENGE){
        c=(w-8)/3;
        for(i=0;i<3;i++){n=tf_number(0,i==0?1:i==1?3:5);tf_append(n," minute");tf_button(x+i*(c+4),y,c,bh,tf_buf,BT_A_DURATION_BASE+i,bt_ch_duration==i);}
        y+=bh+5;c=(w-12)/4;
        for(i=0;i<4;i++)tf_button(x+i*(c+4),y,c,bh,tf_group[i],BT_A_DIFFICULTY_BASE+i,bt_ch_diff==i);
        y+=bh+5;
        if(!tf_compact){ovl_tf_lines(x,y,w,2,"Three seconds to get ready. Pausing, losing focus or changing display makes a challenge unranked.");y+=32;}
        tf_button(x,y,w,bh,"Start challenge",BT_A_START,1);
    }else if(bt_screen==BT_S_RESULTS){
        if(!tf_compact){ovl_tf_resultcard(x,y,w,p);return;}
        n=tf_append(0,!bt_session.attempts?"No keys entered":bt_session.ranked?"Completed":"Practice / unranked");if(bt_session.new_record)tf_append(n,"  PERSONAL BEST!");tf_line(x,y,w,tf_buf);y+=rh;
        n=tf_append(0,"WPM ");n=bt_session.seconds?tf_number(n,bt_session.wpm):tf_append(n,"-");n=tf_append(n,"   Accuracy ");if(bt_session.attempts){n=tf_number(n,bt_session.accuracy);n=tf_append(n,"%");}else n=tf_append(n,"-");n=tf_append(n,"   Errors ");tf_number(n,bt_session.errors);tf_line(x,y,w,tf_buf);y+=rh;
        n=tf_append(0,"Time ");n=tf_number(n,bt_session.seconds);n=tf_append(n,"s   Best streak ");tf_number(n,bt_session.best_streak);tf_line(x,y,w,tf_buf);y+=rh;
        n=tf_append(0,"Weak keys: ");
        for(i=0;i<3;i++){k=bt_weak(i);if(k){tf_char[0]=k;tf_char[1]=0;n=tf_append(n,tf_char);n=tf_append(n," (");n=tf_number(n,p->key_error[k-32]);n=tf_append(n,"/");n=tf_number(n,p->key_attempt[k-32]);n=tf_append(n,") ");}}
        if(n==11)tf_append(n,"more samples needed");tf_line(x,y,w,tf_buf);y+=rh;
        if(!tf_compact){n=tf_append(0,"Achievements: ");
            if(!bt_session.achievement_new)tf_append(n,"Keep practicing!");
            if(bt_session.achievement_new&BT_ACH_FIRST)n=tf_append(n,"First lesson! ");
            if(bt_session.achievement_new&BT_ACH_MASTER)n=tf_append(n,"Mastery! ");
            if(bt_session.achievement_new&BT_ACH_25)n=tf_append(n,"25 streak! ");
            if(bt_session.achievement_new&BT_ACH_50)n=tf_append(n,"50 streak! ");
            if(bt_session.achievement_new&BT_ACH_95)n=tf_append(n,"95% challenge! ");
            if(bt_session.achievement_new&BT_ACH_PERFECT)n=tf_append(n,"Perfect! ");
            if(bt_session.achievement_new&BT_ACH_BEST)tf_append(n,"Personal best! ");tf_line(x,y,w,tf_buf);y+=rh;
            ovl_tf_lines(x,y,w,2,bt_session.accuracy>=95?"Great success! Keep your fingers on their home keys. Continue to the next lesson.":"Slow down and aim for 95% accuracy. Practice the keys that caused the most errors.");y+=32;}
        for(i=0;i<4&&y+tf_th+2+bh+4<=tf_bottom;i++){
            if(!bt_reaction_history(i)[0])break;n=tf_append(0,"Borat: ");tf_append(n,bt_reaction_history(i));tf_line(x,y,w,tf_buf);y+=tf_th+2;}
        c=(w-12)/4;tf_button(x,y,c,bh,"Retry",BT_A_RETRY,1);tf_button(x+c+4,y,c,bh,"Next lesson",BT_A_NEXT,0);tf_button(x+2*(c+4),y,c,bh,"Weak keys",BT_A_WEAK,0);tf_button(x+3*(c+4),y,c,bh,"Menu",BT_A_BACK,0);
    }else if(bt_screen==BT_S_RECORDS){
        if(tf_compact){
            n=tf_append(0,p->name);tf_append(n,"  H-home L-letters D-digits M-mixed");
            tf_text(x+78,y-13,w-78,tf_buf,TF_WHITE,BT_F_SMALL);
            rh=9;c=(w-40)/4;
            for(d=0;d<3;d++){
                n=tf_number(0,d==0?1:d==1?3:5);tf_append(n,"m");tf_text(x,y,38,tf_buf,TF_YELLOW,BT_F_SMALL);
                for(i=0;i<4;i++){
                    tf_char[0]="HLDM"[i];tf_char[1]=' ';tf_char[2]=0;n=tf_append(0,tf_char);
                    n=tf_number(n,p->record_wpm[d][i]);n=tf_append(n,"/");n=tf_number(n,p->record_accuracy[d][i]);tf_append(n,"%");
                    tf_text(x+40+i*c,y,c,tf_buf,TF_WHITE,BT_F_SMALL);
                }y+=rh;
            }
            y+=2;
            for(i=0;i<7;i++){n=tf_append(0,(p->achievements&(1<<i))?"[*] ":"[ ] ");tf_append(n,tf_achnames[i]);tf_text(x+(i%2)*(w/2),y+(i/2)*rh,w/2,tf_buf,TF_YELLOW,BT_F_SMALL);}
        }else{
            n=tf_append(0,"Profile ");n=tf_append(n,p->name);n=tf_append(n,"   Best streak ");tf_number(n,p->best_streak);tf_line(x,y,w,tf_buf);y+=rh;
            tf_line(x,y,w,"DURATION       HOME ROW     LETTERS     DIGITS      MIXED");y+=rh;
            for(d=0;d<3;d++){
                n=tf_number(0,d==0?1:d==1?3:5);n=tf_append(n," min");tf_line(x,y,80,tf_buf);
                for(i=0;i<4;i++){n=tf_number(0,p->record_wpm[d][i]);n=tf_append(n," WPM ");n=tf_number(n,p->record_accuracy[d][i]);tf_append(n,"%");tf_line(x+86+i*(w-86)/4,y,(w-86)/4,tf_buf);}y+=rh;
            }
            y+=4;tf_line(x,y,w,"ACHIEVEMENTS");y+=tf_th+3;rh=tf_th+3;
            for(i=0;i<7;i++){n=tf_append(0,(p->achievements&(1<<i))?"[*] ":"[ ] ");tf_append(n,tf_achnames[i]);tf_line(x+(i%2)*(w/2),y+(i/2)*rh,w/2,tf_buf);}
        }
    }else if(bt_screen==BT_S_SETTINGS){
        c=(w-8)/3;row=bh+2;
        n=tf_append(0,"M Music: ");tf_append(n,bt_store.music?"ON":"OFF");tf_button(x,y,c,bh,tf_buf,BT_A_MUSIC,bt_store.music);
        n=tf_append(0,"E Effects: ");tf_append(n,bt_store.effects?"ON":"OFF");tf_button(x+c+4,y,c,bh,tf_buf,BT_A_EFFECTS,bt_store.effects);
        n=tf_append(0,"K Key ticks: ");tf_append(n,bt_store.keyclick?"ON":"OFF");tf_button(x+2*(c+4),y,c,bh,tf_buf,BT_A_KEYCLICK,bt_store.keyclick);y+=row;
        n=tf_append(0,"R Roast: ");tf_append(n,bt_store.roast==0?"Gentle":bt_store.roast==1?"Classic":"Extra");tf_button(x,y,c,bh,tf_buf,BT_A_ROAST,0);
        n=tf_append(0,"D Display: ");tf_append(n,bt_store.display?"CGA color":"Native");tf_button(x+c+4,y,c,bh,tf_buf,BT_A_DISPLAY,0);
        tf_button(x+2*(c+4),y,c,bh,"T Test music",BT_A_TEST_MUSIC,0);y+=row;
        tf_button(x,y,c,bh,bt_full?"F6 Leave full":"F6 Fullscreen",BT_A_FULL,0);tf_button(x+c+4,y,c,bh,"Save settings",BT_A_SAVE,0);tf_button(x+2*(c+4),y,c,bh,"Back",BT_A_BACK,0);y+=row;
        tf_vsx=x;tf_vsy=y;tf_text(x,y,w,tf_soundstatus(),TF_YELLOW,tf_font);
    }else if(bt_screen==BT_S_PROFILE){
        for(i=0;i<4;i++){
            n=tf_number(0,i+1);n=tf_append(n,". ");n=tf_append(n,bt_store.profile[i].name);
            if(i==bt_store.selected)tf_append(n,bt_profile_edit?"  [type name, Enter saves]":"  < selected");
            tf_button(x,y,w,bh,tf_buf,BT_A_PROFILE_BASE+i,i==bt_store.selected);y+=bh+3;
        }
        if(!tf_compact){tf_line(x,y,w,"Select a profile, then type a short name (12 characters). Enter saves.");y+=rh;tf_button(x,y,w,bh,"Continue",BT_A_BACK,1);}
    }else if(bt_screen==BT_S_PAUSE){
        tf_line(x,y,w,"Your passage and score are safe. Resume intentionally to keep typing.");y+=rh+5;
        if(bt_session.mode==BT_CHALLENGE){tf_line(x,y,w,"This challenge is now practice / unranked.");y+=rh+3;}
        c=(w-8)/2;tf_button(x,y,c,bh,"Resume (F4)",BT_A_PAUSE,1);tf_button(x+c+8,y,c,bh,"Restart",BT_A_RETRY,0);y+=bh+4;
        tf_button(x,y,c,bh,"Display / Fullscreen",BT_A_FULL,0);tf_button(x+c+8,y,c,bh,"Exit to menu",BT_A_BACK,0);
    }else if(bt_screen==BT_S_HELP){
        static const char *help[]={"F/J bumps: left index on F, right index on J. Keep eyes on the prompt.","Correct keys advance. Wrong keys count as errors and keep the same target.","Backspace retracts this passage. Earlier mistakes and attempts stay counted.","WPM = accepted characters x 12 / active seconds; spaces count. Aim for 95%.","Target: outline. Wrong: !. Shifted symbols use opposite-hand Shift.","Challenges last 1/3/5 minutes; pause/focus/display changes make unranked.","F1 Help, F2 Lessons, F3 Sound, F4 Pause, F6 Fullscreen, Escape Menu.","Settings: M/E/K/R/D/T. Practice: 1-4 or Up/Down, Enter starts."};
        rh=(tf_bottom-y-3)/8;if(rh>28)rh=28;
        for(i=0;i<8;i++)tf_text(x,y+i*rh,w,help[i],TF_WHITE,tf_compact?BT_F_SMALL:BT_F_TEXT);
    }

}
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
        else if(kind==4)tf_playbase();
        else if(kind==5){ovl_tf_drawportrait();ovl_tf_drawcaption();}
        else if(kind==6){if(!tf_compact)ovl_tf_drawportrait();ovl_tf_drawcaption();}
        else if(kind==7)ovl_tf_drawhint();
        else if(kind==8){lo=(item/tf_cols)*tf_cols;hi=lo+tf_cols;
            i=item-1;if(i<lo)i=lo;
            while(i<hi&&tf_promptx+(i%tf_cols)*tf_cw<x+w){tf_drawcell(i);i++;}}
        else if(kind==9){lo=item?item-1:0;hi=item<4?item+1:4;for(i=lo;i<=hi;i++)ovl_tf_drawstat(i);}
        else if(kind==10){tf_drawkey(item,1);i=tf_neighbor[item][0];if(i!=255)tf_drawkey(i,1);i=tf_neighbor[item][1];if(i!=255)tf_drawkey(i,1);}
        else if(kind==11)tf_text(tf_vsx,tf_vsy,tf_w-24,tf_soundstatus(),TF_YELLOW,tf_font);
        bx_out();y+=n;
    }
}
static unsigned tf_settings_mask(void)
{return bt_store.music+2*bt_store.effects+8*bt_store.keyclick+16*bt_store.roast+64*bt_store.display+128*bt_full;}
static int ovl_tf_settings_delta(unsigned diff)
{
    int i,a,bit;
    for(i=0;i<tf_nhit;i++){a=tf_hit[i][4];bit=0;
        if(a==BT_A_MUSIC)bit=1;else if(a==BT_A_EFFECTS)bit=2;
        else if(a==BT_A_KEYCLICK)bit=8;else if(a==BT_A_ROAST)bit=48;else if(a==BT_A_DISPLAY)bit=64;else if(a==BT_A_FULL)bit=128;
        if(diff&bit)tf_render(tf_hit[i][0],tf_hit[i][1],tf_hit[i][2],tf_hit[i][3],3,0);
    }
    return 1;
}
/* A cosmetic update is one bounded band per idle paint. Per-band glass
 * signatures coalesce new expressions immediately; no animation queue exists.
 * Eye rows go first, so a short idle blink is visible without a full bust. */
static unsigned tf_cos_signature(int band)
{
    unsigned key;int y;
    key=tf_portrait;y=tf_py+band*tf_cosrows;
    if(y+tf_cosrows>tf_py+(tf_ph>=150?132:60))key|=tf_capepoch<<4;
    return key;
}
static int ovl_tf_cosmetics(void)
{
    int i,j,y,h,end;unsigned key;
    if(tf_compact){tf_render(tf_x+8,tf_statsy-10,tf_w-16,10,6,0);tf_cosneeded=0;tf_capneeded=0;return 1;}
    if(tf_capneeded){
        /* tf_capline is a physical scanline offset. A complete text line
         * can monopolize an XT timer; coalesce it in four-row strips. */
        y=tf_py+(tf_ph>=150?132:60)+tf_capline;h=tf_py+tf_ph-4-y;if(h>4)h=4;
        if(h>0){tf_render(tf_px,y,tf_pw,h,5,0);
            end=(y+h-tf_py+tf_cosrows-1)/tf_cosrows;
            for(j=(y-tf_py)/tf_cosrows;j<end;j++)tf_coskey[j]=tf_cos_signature(j);
            tf_capline+=h;return 1;}
        tf_capneeded=0;
    }
    for(i=0;i<32&&tf_cosearch<tf_cosn;i++){
        j=tf_cospos;tf_cospos++;if(tf_cospos>=tf_cosn)tf_cospos=0;tf_cosearch++;
        key=tf_cos_signature(j);
        if(tf_coskey[j]!=key){y=tf_py+j*tf_cosrows;h=tf_py+tf_ph-y;if(h>tf_cosrows)h=tf_cosrows;
            tf_render(tf_px,y,tf_pw,h,5,0);tf_coskey[j]=key;tf_cosearch=0;return 1;}
    }
    if(tf_cosearch>=tf_cosn)tf_cosneeded=0;return 1;
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
    tf_art=art;tf_band=band;tf_cap=cap;tf_lastscreen=-1;tf_lw=-1;tf_lh=-1;
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
    tf_lastscreen=bt_screen;
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
        if(!tf_compact&&tf_meterlast!=bt_music_phase)ovl_tf_meter();
        return;
    }
    tf_nhit=0;oldview=tf_view;oldinput=tf_inputview;
    textchange=full||tf_savedpos!=bt_session.pos||tf_savedlen!=bt_session.length||tf_savedcomplete!=bt_session.completed;
    keychange=textchange||tf_savedwrong!=bt_session.last_wrong||tf_savedmods!=bt_modifiers;
    urgent=urgent||keychange;
    if(keychange)tf_keyrefresh(full);
    if(textchange){tf_view=(bt_session.pos/(2*tf_cols))*(2*tf_cols);
    tf_inputview=(bt_session.pos/tf_cols)*tf_cols;
    }
    ncells=tf_compact&&tf_h<140?2*tf_cols:3*tf_cols;
    if(full){tf_render(tf_x,tf_top,tf_w,tf_bottom-tf_top,4,0);}
    key=bt_expression();if(key==1&&bt_blink_frame)key|=8;
    if(full||tf_portrait!=key){tf_portrait=key;if(!tf_compact)tf_cosneeded=1;tf_cosearch=0;tf_cospos=(tf_ph>=150?38:14)/tf_cosrows;if(tf_cospos>=tf_cosn)tf_cospos=0;}
    key=tf_string(5381,bt_caption());
    if(full||key!=tf_captionkey){tf_captionkey=key;tf_cosneeded=1;tf_cosearch=0;tf_capneeded=1;tf_capline=0;
        tf_capepoch++;if(tf_capepoch>=16){tf_capepoch=0;for(i=0;i<tf_cosn;i++)tf_coskey[i]=255;}}
    key=tf_mix(tf_target(),bt_session.status);key=tf_mix(key,bt_session.countdown);
    if(full||key!=tf_hintkey){if(full&&tf_compact){i=tf_w>=600?280:220;tf_render(tf_x+i,tf_y+2,tf_w-i-70,8,7,0);}tf_hintpending=!full;tf_hintkey=key;}
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
    for(i=0;i<5;i++){
        if(i==0)key=bt_session.seconds?bt_session.wpm:65535u;
        else if(i==1)key=bt_session.attempts?bt_session.accuracy:65535u;
        else if(i==2)key=bt_session.streak;
        else if(i==3)key=bt_session.mode==BT_CHALLENGE?bt_session.remaining:bt_session.seconds;
        else key=bt_session.errors;
        if(full||key!=tf_statkey[i]){if(!full)tf_statpend|=1<<i;tf_statkey[i]=key;}
    }
    if(keychange)for(i=0;i<TF_KEYS;i++){s=tf_keystate(i);if(full||s!=tf_keycache[i]){
        if(!full){c=s^tf_keycache[i];tf_keyrect(i);x=tf_kx;y=tf_ky0;w=tf_kww;h=tf_khh;
            if(h>=20){
                if(c&3){os88_set_color(s&3?(tf_mono?0:TF_YELLOW):(tf_mono?15:tf_zones[tf_keys[i][5]]));os88_gfx_frame(x+3,y+3,x+w-4,y+h-4);}
                if(c&20)os88_font_run(x+w-12,y+4,s&4?"!":(s&16)&&!(s&5)?"+":" ",tf_mono?0:s&4?TF_RED:TF_GREEN,tf_mono?15:tf_zones[tf_keys[i][5]]);
                if(c&8){os88_set_color(s&8?(tf_mono?0:15):(tf_mono?15:tf_zones[tf_keys[i][5]]));os88_gfx_hline(x+4,x+w-5,y+h-5);}
            }else tf_render(x,y,w,h,10,i);
        }tf_keycache[i]=s;}}
    tf_savedpos=bt_session.pos;tf_savedlen=bt_session.length;tf_savedcomplete=bt_session.completed;
    tf_savedwrong=bt_session.last_wrong;tf_savedmods=bt_modifiers;
    tf_savedattempt=bt_session.attempts;tf_savedback=bt_session.backspaces;
    if(full){for(i=0;i<tf_cosn;i++)tf_coskey[i]=tf_cos_signature(i);tf_cosneeded=0;tf_capneeded=0;tf_statpend=0;tf_hintpending=0;tf_dirtyfirst[2]=-1;}
    else if(!urgent){
        if(tf_dirtyfirst[2]>=0){i=tf_dirtyfirst[2];c=tf_dirtylast[2]-i+1;if(c>2)c=2;tf_cellrect(i);x=tf_sx;y=tf_sy;h=tf_sh;tf_render(x,y,c*tf_cw,h,8,i);tf_dirtyfirst[2]+=c;if(tf_dirtyfirst[2]>tf_dirtylast[2])tf_dirtyfirst[2]=-1;}
        else if(tf_footpending){h=tf_h-(tf_bottom-tf_y)-tf_footrow;if(h>4)h=4;tf_render(tf_x,tf_bottom+tf_footrow,tf_w,h,2,0);tf_footrow+=h;if(tf_footrow>=tf_h-(tf_bottom-tf_y))tf_footpending=0;}
        else if(tf_hintpending&&(!tf_statpend||(bt_session.last_tick&1))){if(tf_compact){i=tf_w>=600?280:220;tf_render(tf_x+i,tf_y+2,tf_w-i-70,8,7,0);}else tf_render(tf_promptx,tf_top+6,224,tf_th,7,0);tf_hintpending=0;}
        else if(tf_statpend){for(c=0;c<5;c++){i=tf_statnext;tf_statnext++;if(tf_statnext>=5)tf_statnext=0;if(tf_statpend&(1<<i)){tf_render(tf_statsx+i*(tf_statsw/5),tf_statsy+(tf_compact?1:5),tf_statsw/5,tf_th,9,i);tf_statpend&=~(1<<i);break;}}}
        else if(tf_cosneeded)ovl_tf_cosmetics();
        else if(!tf_compact&&tf_meterlast!=bt_music_phase)ovl_tf_meter();
    }
}
static int ovl_tf_click(int x,int y)
{
    int i,w;
    if(y>=tf_y&&y<tf_top&&y>=tf_top-(tf_compact?12:22)){
        w=(tf_w-16)/6;i=(x-tf_x-8)/w;
        if(x>=tf_x+8&&i>=0&&i<6)return i==0?BT_A_LESSONS:i==1?BT_A_PRACTICE:i==2?BT_A_CHALLENGE:i==3?BT_A_RECORDS:i==4?BT_A_SETTINGS:BT_A_HELP;
    }
    if(bt_screen==BT_S_PLAY){for(i=0;i<TF_KEYS;i++){tf_keyrect(i);if(tf_kww>0&&x>=tf_kx&&x<tf_kx+tf_kww&&y>=tf_ky0&&y<tf_ky0+tf_khh)return BT_A_HINT_BASE+tf_keys[i][0];}return 0;}
    for(i=0;i<tf_nhit;i++)if(x>=tf_hit[i][0]&&x<tf_hit[i][0]+tf_hit[i][2]&&y>=tf_hit[i][1]&&y<tf_hit[i][1]+tf_hit[i][3])return tf_hit[i][4];
    return 0;
}

int bt_face_click(int x,int y)
{return ovl_tf_click(x,y);}
