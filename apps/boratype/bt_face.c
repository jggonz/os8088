/* Native Borat face. BORATYPE-SPEC 3. All output is opaque composed bands.
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
static int tf_lx,tf_ly,tf_lw,tf_lh,tf_lastscreen,tf_view,tf_inputview;
static int tf_sx,tf_sy,tf_sw,tf_sh;
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
static const char *tf_fingers[]={"Thumb","Left little","Left ring","Left middle","Left index","Right index","Right middle","Right ring","Right little"};
static const unsigned char tf_zones[]={7,12,6,14,10,11,9,5,12};
static const char *tf_group[]={"Home row","All letters","Letters + digits","Mixed punctuation"};
static const char *tf_screenname[]={"WELCOME","LESSONS","TYPE WITH BORAT","RESULTS","PRACTICE","TIMED CHALLENGE","RECORDS","SOUND & DISPLAY","HELP","PAUSED","PROFILES"};
static const unsigned char tf_roles[]={0,1,11,3,1,15,1,9,14,9,15,14,0,7,0,7};
static const unsigned char tf_monoroles[]={0,15,15,0,15,0,0,0,15,0,15,15,0,0,0,15};
static int tf_ink(int c){return tf_mono?0:c;}
static int tf_ground(void){return tf_mono?15:TF_NAVY;}
static unsigned tf_mix(unsigned h,unsigned v){return ((h<<5)+h)^v;}
static unsigned tf_string(unsigned h,const char *s){while(*s)h=tf_mix(h,(unsigned char)*s++);return h;}
static int tf_append(int n,const char *s){while(*s&&n<94)tf_buf[n++]=*s++;tf_buf[n]=0;return n;}
static int tf_number(int n,unsigned v){os88_utoa(v,tf_num);return tf_append(n,tf_num);}
static void tf_text(int x,int y,int w,const char *s,int c,int font)
{gk_text(x,y,w,s,tf_ink(c),font,GK_LEFT|GK_FIT);}
static void tf_line(int x,int y,int w,const char *s)
{tf_text(x,y,w,s,TF_WHITE,tf_font);}
/* Word wrap bounded to80 characters per line. Caption reads real director text. */
static void tf_lines(int x,int y,int w,int rows,const char *s)
{
    int i,n,last;
    while(rows-->0&&*s){n=0;last=0;
        while(s[n]&&n<78){tf_wrap[n]=s[n];tf_wrap[n+1]=0;
            if(bx_twidth(tf_wrap,tf_font)>w)break;
            if(s[n]==' ')last=n;n++;}
        if(s[n]&&last>0)n=last;
        if(n==0)n=1;
        for(i=0;i<n;i++)tf_wrap[i]=s[i];tf_wrap[n]=0;
        tf_line(x,y,w,tf_wrap);s+=n;while(*s==' ')s++;y+=tf_th+2;
    }
}
static void tf_button(int x,int y,int w,int h,const char *s,int a,int selected)
{
    if(tf_nhit<TF_HITS){tf_hit[tf_nhit][0]=x;tf_hit[tf_nhit][1]=y;tf_hit[tf_nhit][2]=w;tf_hit[tf_nhit][3]=h;tf_hit[tf_nhit++][4]=a;}
    gk_button(x,y,w,h,s,tf_font,(selected?GK_B_HILITE:GK_B_NORMAL)|GK_B_FLAT);
}
static int tf_kx,tf_ky0,tf_kww,tf_khh;
static int tf_ktarget,tf_kshift,tf_kwrong,tf_kcorrect;
static void tf_keyrect(int i)
{
    tf_kx=tf_x+8+tf_keys[i][3]*tf_kw/4;
    tf_ky0=tf_ky+tf_keys[i][2]*tf_kh;
    tf_kww=tf_keys[i][4]*tf_kw/4-2;tf_khh=tf_kh-2;
}
static int tf_keyid(int c)
{
    int i;for(i=0;i<47;i++)if(tf_keys[i][0]==c||tf_keys[i][1]==c)return i;
    if(c==32)return 53;return -1;
}
static int tf_target(void)
{return bt_session.pos<bt_session.length?(unsigned char)bt_session.passage[bt_session.pos]:0;}
static int tf_shift(int c)
{
    int i;i=tf_keyid(c);if(i<0||c==32||c==tf_keys[i][0])return -1;
    return tf_keys[i][5]<=4?52:51;
}
static void tf_keyrefresh(void)
{
    int c;c=tf_target();tf_ktarget=tf_keyid(c);tf_kshift=tf_shift(c);
    tf_kwrong=bt_session.last_wrong?tf_keyid(bt_session.last_wrong):-1;
    tf_kcorrect=bt_session.pos>0&&!bt_session.last_wrong?tf_keyid((unsigned char)bt_session.passage[bt_session.pos-1]):-1;
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
static void tf_drawkey(int i)
{
    int state,c;const char *s;
    tf_keyrect(i);if(tf_kww<=0||tf_kx+tf_kww<=tf_sx||tf_kx>=tf_sx+tf_sw||tf_ky0+tf_khh<=tf_sy||tf_ky0>=tf_sy+tf_sh)return;
    state=tf_keystate(i);c=tf_mono?15:tf_zones[tf_keys[i][5]];
    if(state&16)c=tf_mono?15:TF_GREEN;
    if(state&4)c=tf_mono?0:TF_RED;else if(state&3)c=tf_mono?15:TF_YELLOW;
    gk_box(tf_kx,tf_ky0,tf_kww,tf_khh,GK_RAISED2,c);
    if(state&3)gk_box(tf_kx+3,tf_ky0+2,tf_kww-6,tf_khh-4,GK_FLAT,-1);
    s=tf_keylabel(i);
    gk_textbox(tf_kx+2,tf_ky0+1,tf_kww-4,tf_khh-3,s,(tf_mono&&(state&4))?15:0,tf_compact?BT_F_SMALL:tf_font,GK_CENTRE|GK_FIT);
    if(i<47&&tf_khh>=26&&tf_keys[i][1]!=tf_keys[i][0]&&tf_keys[i][0]<'a'){
        tf_char[0]=tf_keys[i][1];tf_char[1]=0;bx_text(tf_kx+4,tf_ky0+3,tf_char,0,BT_F_SMALL);}
    if((state&16)&&!(state&5))bx_text(tf_kx+tf_kww-9,tf_ky0+2,"+",0,BT_F_SMALL);
    if(state&4)bx_text(tf_kx+tf_kww-9,tf_ky0+2,"!",tf_mono?15:0,BT_F_SMALL);
    if(state&8)bx_fill(tf_kx+4,tf_ky0+tf_khh-4,tf_kx+tf_kww-5,tf_ky0+tf_khh-3,tf_mono?0:15);
    if(i==29||i==32)bx_fill(tf_kx+tf_kww/2-3,tf_ky0+tf_khh-4,tf_kx+tf_kww/2+2,tf_ky0+tf_khh-4,0);
}
static int tf_layout(void *win)
{
    int changed,space;
    os88_wm_content(win,&tf_org);if(os88_wm_geom(win,&tf_sz)!=0)return 0;
    os88_wm_display(win,&tf_vid);
    changed=tf_lx!=tf_org.x||tf_ly!=tf_org.y||tf_lw!=tf_sz.w||tf_lh!=tf_sz.h||tf_mono!=(tf_vid.bpp<4);
    tf_lx=tf_org.x;tf_ly=tf_org.y;tf_lw=tf_sz.w;tf_lh=tf_sz.h;
    tf_x=(tf_org.x+7)&~7;tf_y=tf_org.y;tf_w=(tf_sz.w-(tf_x-tf_org.x))&~7;tf_h=tf_sz.h;
    if(tf_w<480||tf_h<128)return 0;
    tf_mono=tf_vid.bpp<4;tf_compact=tf_h<280;
    tf_font=tf_compact?BT_F_SMALL:BT_F_TEXT;tf_title=tf_compact?BT_F_TEXT:BT_F_TITLE;tf_th=tf_compact?8:10;
    tf_top=tf_y+(tf_compact?24:46);tf_bottom=tf_y+tf_h-(tf_compact?10:17);
    tf_kw=(tf_w-16)/15;tf_kh=tf_compact?(tf_h<154?10:12):(tf_h>=390?30:22);
    tf_ky=tf_bottom-5*tf_kh-2;tf_statsy=tf_ky-(tf_compact?11:24);
    tf_px=tf_x+8;tf_py=tf_top+4;tf_pw=tf_compact?0:(tf_w>=760?180:136);tf_ph=tf_statsy-tf_py-5;
    tf_promptx=tf_x+(tf_compact?8:tf_pw+24);tf_prompty=tf_top+(tf_compact?2:30);
    space=tf_x+tf_w-12-tf_promptx;
    tf_cw=tf_compact?8:(tf_h>=390?16:12);tf_ch=tf_compact?8:(tf_h>=390?20:16);
    tf_cols=space/tf_cw;if(tf_cols>40)tf_cols=40;if(tf_cols<12)tf_cols=12;
    tf_inputy=tf_prompty+2*tf_ch+(tf_compact?2:12);
    if(changed){bx_setup(tf_band,0,tf_cap,tf_art,tf_mono?1:4);gk_init(tf_mono?1:4);gk_theme(tf_mono?tf_monoroles:tf_roles);}
    return changed?2:1;
}
static void tf_drawheader(void)
{
    int w,x,i;static const char *tabs[]={"Lessons","Practice","Challenge","Records"};
    tf_text(tf_x+8,tf_y+2,tf_w-70,"BORAT TEACHES TYPING",TF_YELLOW,tf_title);
    tf_text(tf_x+tf_w-64,tf_y+3,60,"os8088",TF_WHITE,tf_font);
    bx_fill(tf_x,tf_top-2,tf_x+tf_w-1,tf_top-1,tf_ink(TF_CYAN));
    w=(tf_w-16)/6;x=tf_x+8;
    for(i=0;i<4;i++)gk_textbox(x+i*w,tf_top-(tf_compact?12:22),w,tf_compact?10:18,tabs[i],tf_ink(TF_WHITE),tf_font,GK_CENTRE|GK_FIT);
    gk_textbox(x+4*w,tf_top-(tf_compact?12:22),w,tf_compact?10:18,"Settings",tf_ink(TF_WHITE),tf_font,GK_CENTRE);
    gk_textbox(x+5*w,tf_top-(tf_compact?12:22),w,tf_compact?10:18,"Help",tf_ink(TF_WHITE),tf_font,GK_CENTRE);
}
static void tf_drawfooter(void)
{
    if(bt_notice[0]){tf_line(tf_x+6,tf_bottom+1,tf_w-12,bt_notice);return;}
    tf_line(tf_x+6,tf_bottom+1,tf_w-12,bt_readonly?"READ ONLY  F1 Help  F2 Lessons  F3 Sound  F4 Pause  F6 Full  Esc Menu":"F1 Help   F2 Lessons   F3 Sound   F4 Pause   F6 Full   Esc Menu");
}
static void tf_drawportrait(void)
{
    int id,w,h,e;
    if(tf_compact)return;
    e=bt_expression();if(e<0||e>6)e=0;
    id=tf_mono?BT_ART_MONO+e:(tf_ph>=150?BT_ART_BIG+e:BT_ART_SMALL+e);
    w=(id<7)?96:40;h=(id<7)?120:48;
    gk_box(tf_px,tf_py,tf_pw,tf_ph,GK_RAISED,tf_ground());
    bx_spr(tf_px+(tf_pw-w)/2,tf_py+6,id);
    tf_text(tf_px+6,tf_py+h+12,tf_pw-12,bt_store.profile[bt_store.selected].name,TF_YELLOW,tf_font);
}
static void tf_drawcaption(void)
{
    int x,y,w,rows;
    if(tf_compact){x=tf_x+8;y=tf_statsy-10;w=tf_w-16;rows=1;}
    else{x=tf_px+6;y=tf_py+(tf_ph>=150?146:74);w=tf_pw-12;rows=(tf_py+tf_ph-y-5)/(tf_th+2);}
    if(rows>0)tf_lines(x,y,w,rows,bt_caption());
}
static void tf_drawhint(void)
{
    int c,i,n; c=tf_target();i=tf_keyid(c);n=0;
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
    col=s==1?TF_GREEN:TF_WHITE;font=tf_compact?-1:(tf_h>=390?BT_F_TITLE:BT_F_TEXT);
    if(s==2){bx_fill(x,y,x+tf_cw-1,y+tf_ch-1,tf_mono?0:TF_YELLOW);col=0;}
    tf_char[0]=c;tf_char[1]=0;
    bx_text(x+(tf_cw-bx_twidth(tf_char,font))/2,y+(tf_compact?0:2),tf_char,tf_mono?(s==2?15:0):col,font);
    if(s==1&&tf_mono)bx_fill(x,y+tf_ch-1,x+tf_cw-2,y+tf_ch-1,0);
    if(s==2)bx_fill(x+1,y+tf_ch-2,x+tf_cw-2,y+tf_ch-2,tf_mono?15:0);
}
static void tf_drawprompt(void)
{
    int i,x,w;
    tf_drawhint();
    if(!tf_compact){x=tf_promptx-4;w=tf_w-(x-tf_x)-8;
        gk_box(x,tf_prompty-5,w,2*tf_ch+10,GK_SUNKEN2,tf_ground());
        if(tf_inputy+tf_ch+13<tf_statsy)tf_text(x,tf_inputy+tf_ch+5,w,"ACCEPTED INPUT  |  Wrong keys stay on the same target",TF_WHITE,BT_F_SMALL);
        gk_box(x,tf_inputy-3,w,tf_ch+6,GK_SUNKEN,tf_ground());
        if(bt_session.mode==BT_LESSON&&tf_inputy+tf_ch+20+2*(tf_th+2)<tf_statsy)tf_lines(x,tf_inputy+tf_ch+20,w,2,bt_lessons[bt_session.lesson].instruction);
    }
    for(i=0;i<3*tf_cols;i++)tf_drawcell(i);
}
static void tf_drawstat(int i)
{
    int x,w,n;static const char *labels[]={"WPM ","ACCURACY ","STREAK ","TIME ","ERRORS "};
    w=tf_w/5;x=tf_x+i*w;n=tf_append(0,labels[i]);
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
    tf_drawportrait();tf_drawcaption();tf_drawprompt();
    for(i=0;i<5;i++)tf_drawstat(i);
    for(i=0;i<TF_KEYS;i++)tf_drawkey(i);
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
        if(!tf_compact){tf_drawportrait();x+=tf_pw+16;w-=tf_pw+16;tf_lines(x,y,w,2,"Put your fingers on F and J. Borat will supervise the great success.");y+=34;}
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
        if(!tf_compact){tf_lines(x,y,w,2,"Three seconds to get ready. Pausing, losing focus or changing display makes a challenge unranked.");y+=32;}
        tf_button(x,y,w,bh,"Start challenge",BT_A_START,1);
    }else if(bt_screen==BT_S_RESULTS){
        n=tf_append(0,bt_session.ranked?"Completed":"Practice / unranked");if(bt_session.new_record)tf_append(n,"  PERSONAL BEST!");tf_line(x,y,w,tf_buf);y+=rh;
        n=tf_append(0,"WPM ");n=tf_number(n,bt_session.wpm);n=tf_append(n,"   Accuracy ");n=tf_number(n,bt_session.accuracy);n=tf_append(n,"%   Errors ");tf_number(n,bt_session.errors);tf_line(x,y,w,tf_buf);y+=rh;
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
            tf_lines(x,y,w,2,bt_session.accuracy>=95?"Great success! Keep your fingers on their home keys. Continue to the next lesson.":"Slow down and aim for 95% accuracy. Practice the keys that caused the most errors.");y+=32;tf_lines(x,y,w,2,bt_caption());y+=28;}
        c=(w-12)/4;tf_button(x,y,c,bh,"Retry",BT_A_RETRY,1);tf_button(x+c+4,y,c,bh,"Next lesson",BT_A_NEXT,0);tf_button(x+2*(c+4),y,c,bh,"Weak keys",BT_A_WEAK,0);tf_button(x+3*(c+4),y,c,bh,"Menu",BT_A_BACK,0);
    }else if(bt_screen==BT_S_RECORDS){
        n=tf_append(0,"Profile ");n=tf_append(n,p->name);n=tf_append(n,"   Best streak ");tf_number(n,p->best_streak);tf_line(x,y,w,tf_buf);y+=rh;
        tf_line(x,y,w,"DURATION       HOME ROW     LETTERS     DIGITS      MIXED");y+=rh;
        for(d=0;d<3;d++){
            n=tf_number(0,d==0?1:d==1?3:5);n=tf_append(n," min");tf_line(x,y,80,tf_buf);
            for(i=0;i<4;i++){n=tf_number(0,p->record_wpm[d][i]);n=tf_append(n," WPM ");n=tf_number(n,p->record_accuracy[d][i]);tf_append(n,"%");tf_line(x+86+i*(w-86)/4,y,(w-86)/4,tf_buf);}y+=rh;
        }
        if(!tf_compact){y+=8;tf_line(x,y,w,"ACHIEVEMENTS");y+=rh;
            static const char *names[]={"First lesson","95% mastery","25-key streak","50-key streak","95% challenge","Perfect lesson","Personal best"};
            for(i=0;i<7;i++){n=tf_append(0,(p->achievements&(1<<i))?"[*] ":"[ ] ");tf_append(n,names[i]);tf_line(x+(i%2)*(w/2),y+(i/2)*rh,w/2,tf_buf);}}
    }else if(bt_screen==BT_S_SETTINGS){
        c=(w-8)/2;row=0;
        n=tf_append(0,"Music: ");tf_append(n,bt_store.music?"ON":"OFF");tf_button(x,y,c,bh,tf_buf,BT_A_MUSIC,bt_store.music);
        n=tf_append(0,"Effects: ");tf_append(n,bt_store.effects?"ON":"OFF");tf_button(x+c+8,y,c,bh,tf_buf,BT_A_EFFECTS,bt_store.effects);y+=bh+3;
        n=tf_append(0,"Voice: ");tf_append(n,bt_store.voice?"ON":"OFF");tf_button(x,y,c,bh,tf_buf,BT_A_VOICE,bt_store.voice);
        n=tf_append(0,"Key ticks: ");tf_append(n,bt_store.keyclick?"ON":"OFF");tf_button(x+c+8,y,c,bh,tf_buf,BT_A_KEYCLICK,bt_store.keyclick);y+=bh+3;
        n=tf_append(0,"Roast: ");tf_append(n,bt_store.roast==0?"Gentle":bt_store.roast==1?"Classic":"Extra Roast");tf_button(x,y,c,bh,tf_buf,BT_A_ROAST,0);
        tf_button(x+c+8,y,c,bh,bt_full?"Leave fullscreen":"Enter fullscreen",BT_A_FULL,0);y+=bh+3;
        tf_button(x,y,c,bh,"Test music",BT_A_TEST_MUSIC,0);tf_button(x+c+8,y,c,bh,"Test voice",BT_A_TEST_VOICE,0);y+=bh+3;
        if(!tf_compact){tf_line(x,y,w,"Voice captions always work. Optional local PCM pack: see status below.");y+=rh;
            tf_line(x,y,w,bt_master_sound?"Master sound: ON (F3 toggles)":"Master sound: MUTED (F3 toggles)");y+=rh;
            tf_button(x,y,c,bh,"Display preference",BT_A_DISPLAY,0);tf_button(x+c+8,y,c,bh,"Save settings",BT_A_SAVE,0);}
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
        static const char *help[]={"F/J bumps: left index on F, right index on J. Keep eyes on the prompt.","Correct keys advance. Wrong keys count as errors and keep the same target.","Backspace retracts this passage. Earlier mistakes and attempts stay counted.","WPM = accepted characters x 12 / active seconds; spaces count. Aim for 95%.","Target: double outline. Wrong: !. Underline: accepted or latched modifier.","Shifted symbols: use the outlined key plus the Shift on the opposite hand.","Challenges last 1, 3 or 5 minutes. Pause/focus/display changes make unranked.","F1 Help, F2 Lessons, F3 Sound, F4 Pause, F6 Fullscreen, Escape Menu."};
        rh=(tf_bottom-y-3)/8;if(rh>28)rh=28;
        for(i=0;i<8;i++)tf_text(x,y+i*rh,w,help[i],TF_WHITE,tf_compact?BT_F_SMALL:BT_F_TEXT);
    }
    if(bt_notice[0]&&!tf_compact)tf_text(tf_x+12,tf_bottom-14,tf_w-24,bt_notice,TF_YELLOW,tf_font);
}
/* Render only the requested rectangle, rounded out to the band's byte width.
 * Adjacent cells sharing a byte are recomposed, so neither clipping nor a
 * proportional-font cell can erase its neighbour. */
static void tf_render(int x,int y,int w,int h,int kind,int item)
{
    int right,bottom,rows,n,i;
    (void)item;
    right=x+w;bottom=y+h;x&=~7;right=(right+7)&~7;
    if(x<tf_x)x=tf_x;if(right>tf_x+tf_w)right=tf_x+tf_w;
    if(y<tf_y)y=tf_y;if(bottom>tf_y+tf_h)bottom=tf_y+tf_h;
    w=right-x;if(w<=0||bottom<=y)return;
    rows=bx_rows(x,y,w,bottom-y);if(rows<=0)return;
    while(y<bottom){n=bottom-y;if(n>rows)n=rows;
        tf_sx=x;tf_sy=y;tf_sw=w;tf_sh=n;
        bx_new(x,y,w,n,tf_ground());
        if(kind==1)tf_drawheader();
        else if(kind==2)tf_drawfooter();
        else if(kind==3)ovl_tf_drawbody();
        else if(kind==4)tf_playbase();
        else if(kind==5){tf_drawportrait();tf_drawcaption();}
        else if(kind==6){if(!tf_compact)tf_drawportrait();tf_drawcaption();}
        else if(kind==7)tf_drawhint();
        else if(kind==8){for(i=0;i<3*tf_cols;i++)tf_drawcell(i);}
        else if(kind==9){for(i=0;i<5;i++)tf_drawstat(i);}
        else if(kind==10){for(i=0;i<TF_KEYS;i++)tf_drawkey(i);}
        bx_out();y+=n;
    }
}
static unsigned tf_menukey(void)
{
    unsigned h;int i;BTProfile *p;
    p=&bt_store.profile[bt_store.selected];h=tf_mix(5381,bt_screen);
    h=tf_mix(h,bt_selection);h=tf_mix(h,bt_store.selected);h=tf_mix(h,bt_ch_diff);h=tf_mix(h,bt_ch_duration);
    h=tf_mix(h,bt_store.music+2*bt_store.effects+4*bt_store.voice+8*bt_store.keyclick+16*bt_store.roast+64*bt_store.display);
    h=tf_mix(h,bt_practice_weak+2*bt_full+4*bt_profile_edit+8*bt_readonly+16*bt_master_sound);
    h=tf_string(h,bt_notice);h=tf_string(h,p->name);
    if(bt_screen==BT_S_PROFILE)for(i=0;i<4;i++)h=tf_string(h,bt_store.profile[i].name);
    if(bt_screen==BT_S_RESULTS){h=tf_mix(h,bt_session.wpm);h=tf_mix(h,bt_session.accuracy);h=tf_string(h,bt_caption());}
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
    int layout,i,c,s,x,y,w,h,oldview,oldinput,ncells;
    unsigned key;
    layout=tf_layout(win);if(!layout){tf_nhit=0;return;}
    if(layout==2||tf_lastscreen!=bt_screen)full=1;
    tf_lastscreen=bt_screen;
    if(full&&(tf_x!=tf_org.x||tf_x+tf_w!=tf_org.x+tf_sz.w)){
        os88_set_color(tf_ground());
        if(tf_x>tf_org.x)os88_gfx_fill(tf_org.x,tf_y,tf_x-1,tf_y+tf_h-1);
        if(tf_x+tf_w<tf_org.x+tf_sz.w)os88_gfx_fill(tf_x+tf_w,tf_y,tf_org.x+tf_sz.w-1,tf_y+tf_h-1);
    }
    key=tf_mix(5381,bt_screen);key=tf_mix(key,bt_store.selected);
    if(full||key!=tf_header){tf_render(tf_x,tf_y,tf_w,tf_top-tf_y,1,0);tf_header=key;}
    key=tf_string(tf_mix(5381,bt_readonly+2*bt_master_sound),bt_notice);
    if(full||key!=tf_footer){tf_render(tf_x,tf_bottom,tf_w,tf_h-(tf_bottom-tf_y),2,0);tf_footer=key;}
    if(bt_screen!=BT_S_PLAY){key=tf_menukey();if(full||key!=tf_bodykey){tf_render(tf_x,tf_top,tf_w,tf_bottom-tf_top,3,0);tf_bodykey=key;}return;}
    tf_nhit=0;oldview=tf_view;oldinput=tf_inputview;tf_keyrefresh();
    tf_view=(bt_session.pos/(2*tf_cols))*(2*tf_cols);
    tf_inputview=(bt_session.pos/tf_cols)*tf_cols;
    ncells=tf_compact&&tf_h<140?2*tf_cols:3*tf_cols;
    if(full){tf_render(tf_x,tf_top,tf_w,tf_bottom-tf_top,4,0);}
    key=bt_expression();if(full||tf_portrait!=key){if(!full&&!tf_compact)tf_render(tf_px,tf_py,tf_pw,tf_ph,5,0);tf_portrait=key;}
    key=tf_string(5381,bt_caption());
    if(full||key!=tf_captionkey){if(!full){if(tf_compact)tf_render(tf_x+8,tf_statsy-10,tf_w-16,10,6,0);
        else tf_render(tf_px+4,tf_py+(tf_ph>=150?146:74),tf_pw-8,tf_ph-(tf_ph>=150?146:74),6,0);}tf_captionkey=key;}
    key=tf_mix(tf_target(),bt_session.status);key=tf_mix(key,bt_session.countdown);
    if(full||key!=tf_hintkey){if(tf_compact){i=tf_w>=600?280:220;tf_render(tf_x+i,tf_y+2,tf_w-i-70,10,7,0);}
        else if(!full)tf_render(tf_promptx,tf_top+4,tf_w-(tf_promptx-tf_x)-8,18,7,0);tf_hintkey=key;}
    for(i=0;i<ncells;i++){
        c=tf_cellc(i);s=tf_cellstate(i);
        if(full||c!=tf_cellchar[i]||s!=tf_cellstyle[i]||oldview!=tf_view||(i>=2*tf_cols&&oldinput!=tf_inputview)){
            if(!full){tf_cellrect(i);x=tf_sx;y=tf_sy;w=tf_sw;h=tf_sh;tf_render(x,y,w,h,8,i);}
            tf_cellchar[i]=c;tf_cellstyle[i]=s;
        }
    }
    for(i=0;i<5;i++){
        if(i==0)key=bt_session.seconds?bt_session.wpm:65535u;
        else if(i==1)key=bt_session.attempts?bt_session.accuracy:65535u;
        else if(i==2)key=bt_session.streak;
        else if(i==3)key=bt_session.mode==BT_CHALLENGE?bt_session.remaining:bt_session.seconds;
        else key=bt_session.errors;
        if(full||key!=tf_statkey[i]){if(!full)tf_render(tf_x+i*(tf_w/5),tf_statsy,tf_w/5,tf_compact?10:22,9,i);tf_statkey[i]=key;}
    }
    for(i=0;i<TF_KEYS;i++){s=tf_keystate(i);if(full||s!=tf_keycache[i]){
        if(!full){tf_keyrect(i);tf_render(tf_kx,tf_ky0,tf_kww,tf_khh,10,i);}tf_keycache[i]=s;}}
}
int bt_face_click(int x,int y)
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
