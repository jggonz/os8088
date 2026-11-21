/* Host geometry and repaint contract against the production face.
 * gcc -std=c99 -Iapps/cc apps/boratype/hosttest/face_test.c -o /tmp/btface */
#define BT_HOST
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
struct os88_pt {int x,y;};struct os88_size {int w,h;};
struct os88_video {int w,h,dock_top,kind,bpp;};
void os88_wm_content(void *,struct os88_pt *);
int os88_wm_geom(void *,struct os88_size *);
void os88_wm_display(void *,struct os88_video *);
char *os88_utoa(unsigned,char *);
void os88_set_color(int);void os88_gfx_fill(int,int,int,int);
void os88_gfx_frame(int,int,int,int);void os88_gfx_hline(int,int,int);
void os88_font_run(int,int,const char *,int,int);
#include "../bt.h"
BTStore bt_store;BTSession bt_session;
#define TESTLESSON {"F and J","fj ","fj","Rest index fingers on F and J.","fj jf"}
const BTLesson bt_lessons[BT_LESSONS]={
 TESTLESSON,TESTLESSON,TESTLESSON,TESTLESSON,TESTLESSON,TESTLESSON,
 TESTLESSON,TESTLESSON,TESTLESSON,TESTLESSON,TESTLESSON,TESTLESSON,
 TESTLESSON,TESTLESSON,TESTLESSON,TESTLESSON,TESTLESSON,TESTLESSON,
 TESTLESSON,TESTLESSON,TESTLESSON,TESTLESSON,TESTLESSON,TESTLESSON};
unsigned os88_part_seg(int i){(void)i;return 0;}
int bt_music_phase;
int bt_screen,bt_selection,bt_ch_duration,bt_ch_diff,bt_master_sound=1;
int bt_full,bt_readonly,bt_profile_edit,bt_modifiers,bt_practice_weak,bt_blink_frame;
char bt_notice[64];
static int hw=640,hh=440,hbpp=4;
void os88_set_color(int c){(void)c;}
void os88_gfx_fill(int a,int b,int c,int d){(void)a;(void)b;(void)c;(void)d;}
void os88_gfx_frame(int a,int b,int c,int d){assert(a>=8&&c<8+hw&&b>=24&&d<24+hh);}
void os88_gfx_hline(int a,int b,int c){assert(a>=8&&b<8+hw&&c>=24&&c<24+hh);}
void os88_font_run(int x,int y,const char *s,int ink,int paper){(void)s;(void)ink;(void)paper;assert(x>=8&&x+8<=8+hw&&y>=24&&y+8<=24+hh);}
static unsigned flushes,area;
static int bandx,bandy,bandw,bandh;
unsigned bx_bseg;
static int expression=1,last_sprite=-1,status_seen,reaction_seen,accuracy_y,accuracy_dash;
static unsigned achievement_seen;
static const char *caption="Your fingers have returned from holiday.";
const char *bt_caption(void){return caption;}int bt_expression(void){return expression;}
const char *bt_reaction_history(int back){return back<4?"Your fingers have returned from holiday.":"";}
int bt_weak(int rank){return rank==0?'f':0;}
void os88_wm_content(void *w,struct os88_pt *p){(void)w;p->x=8;p->y=24;}
int os88_wm_geom(void *w,struct os88_size *s){(void)w;s->w=hw;s->h=hh;return 0;}
void os88_wm_display(void *w,struct os88_video *v){(void)w;v->bpp=hbpp;}
char *os88_utoa(unsigned v,char *s){snprintf(s,8,"%u",v);return s;}
#include "../bt_face.c"
int bx_rows(int x,int y,int w,int h){(void)x;(void)y;(void)h;return 8192/(hbpp==1?w/8:w/2);}
void bx_new(int x,int y,int w,int h,int c){(void)c;assert(!(x&7)&&!(w&7));assert(x>=8&&x+w<=8+hw);assert(y>=24&&y+h<=24+hh);bandx=x;bandy=y;bandw=w;bandh=h;}
void bx_fill(int a,int b,int c,int d,int col){(void)a;(void)b;(void)c;(void)d;(void)col;}
int bx_spr(int x,int y,int id){(void)x;(void)y;last_sprite=id;return 40;}
int bx_twidth(const char *s,int f){return (int)strlen(s)*(f==BT_F_LARGE?16:f==BT_F_TITLE?9:f==BT_F_TEXT?6:5);}
int bx_fonth(int f){return f==BT_F_LARGE?24:f==BT_F_TITLE?16:f==BT_F_TEXT?10:8;}
int bx_text(int x,int y,const char *s,int c,int f){(void)x;(void)y;(void)c;return bx_twidth(s,f);}
int bx_out(void){flushes++;area+=bandw*bandh;return 0;}
void bx_setup(unsigned a,void *b,unsigned c,unsigned d,int e){(void)b;(void)c;(void)d;(void)e;bx_bseg=a;}
void bx_setart2(unsigned a){(void)a;}
void gk_init(int a){(void)a;}void gk_theme(const unsigned char *a){(void)a;}
int gk_text(int x,int y,int w,const char *s,int c,int f,int fl){if(!strcmp(s,tf_soundstatus()))status_seen++;if(!strncmp(s,"Borat: ",7))reaction_seen++;if(!strcmp(s,"ACCURACY"))accuracy_y=y;if((!strcmp(s,"-")&&y==accuracy_y)||strstr(s,"Accuracy -"))accuracy_dash++;for(int a=0;a<7;a++)if(strstr(s,tf_achnames[a]))achievement_seen|=1u<<a;(void)x;(void)y;(void)w;(void)c;(void)fl;return bx_twidth(s,f);}
int gk_textbox(int x,int y,int w,int h,const char *s,int c,int f,int fl){(void)h;return gk_text(x,y,w,s,c,f,fl);}
void gk_box(int x,int y,int w,int h,int s,int f){(void)x;(void)y;(void)w;(void)h;(void)s;(void)f;}
void gk_button(int x,int y,int w,int h,const char *s,int f,int st){(void)x;(void)y;(void)w;(void)h;(void)s;(void)f;(void)st;}
static void keyboard(void)
{
 int i,j,act;
 for(i=0;i<58;i++){
  tf_keyrect(i);assert(tf_kx>=tf_x&&tf_kx+tf_kww<=tf_x+tf_w);assert(tf_ky0>=tf_top&&tf_ky0+tf_khh<=tf_bottom);
  assert(tf_kww>0&&tf_khh>=8);
  act=bt_face_click(tf_kx+tf_kww/2,tf_ky0+tf_khh/2);
  assert(act==BT_A_HINT_BASE+tf_keys[i][0]);
  for(j=i+1;j<58;j++)if(tf_keys[j][2]==tf_keys[i][2]){
   int left=tf_x+8+tf_keys[j][3]*tf_kw/4;
   int width=tf_keys[j][4]*tf_kw/4-2;
   assert(tf_kx+tf_kww<=left||left+width<=tf_kx);
  }
 }
 assert(tf_ch>=(tf_compact?8:(tf_h>=400?24:10))+(tf_compact?0:2));
 if(!tf_compact)assert(tf_statsx>=tf_px+tf_pw);
 if(tf_h>=400&&tf_w>=600)assert((tf_ph-132-5)/(tf_th+2)>=3);
 for(i=32;i<127;i++){assert(bt_key_base(i)==tf_keys[tf_keyid(i)][0]);assert(strcmp(bt_key_finger(i),"None"));assert(bt_key_shift(i)==(tf_shift(i)<0?0:tf_shift(i)==51?1:2));}
 assert(tf_keys[tf_keyid('i')][5]==6);assert(tf_keys[tf_keyid('j')][5]==5);
 assert(tf_shift('A')==52&&tf_shift('J')==51&&tf_shift('?')==51&&tf_shift('!')==52);
 for(i=32;i<127;i++)assert(tf_keyid(i)>=0);
}
static void blink(void)
{
 static BTSession before;
 int expected;
 memcpy(&before,&bt_session,sizeof(before));
 bt_blink_frame=0;expression=1;bt_face_paint(0,1);
 area=flushes=0;last_sprite=-1;bt_blink_frame=1;bt_face_paint(0,0);
 if(!tf_compact){
  expected=tf_mono?BT_ART_BLINK_MONO:(tf_ph>=150?BT_ART_BLINK_BIG:BT_ART_BLINK_SMALL);
  assert(last_sprite==expected);assert(flushes>0&&area<=(unsigned)(((tf_pw+7)&~7)*tf_ph));
 }else assert(!area&&!flushes);
 for(int n=0;tf_cosneeded&&n<TF_COS_MAX+1;n++){area=flushes=0;bt_face_paint(0,0);assert(area<=(unsigned)(((tf_pw+7)&~7)*tf_cosrows));}
 area=flushes=0;bt_face_paint(0,0);assert(!area&&!flushes);
 bt_blink_frame=0;last_sprite=-1;bt_face_paint(0,0);
 if(!tf_compact)assert(last_sprite==(tf_mono?BT_ART_MONO+1:(tf_ph>=150?BT_ART_BIG+1:BT_ART_SMALL+1)));
 while(tf_cosneeded)bt_face_paint(0,0);
 expression=2;bt_blink_frame=1;bt_face_paint(0,1);area=flushes=0;
 bt_blink_frame=0;bt_face_paint(0,0);assert(!area&&!flushes);
 assert(!memcmp(&before,&bt_session,sizeof(before)));expression=1;
}
static void caption_change(void)
{
 static BTSession before;const char *old=caption;
 memcpy(&before,&bt_session,sizeof(before));bt_face_paint(0,1);
 caption="Your fingers called a meeting. The keyboard was not invited.";
 for(int n=0;n<TF_COS_MAX+2;n++){
  area=flushes=0;bt_face_paint(0,0);
  if(!tf_compact)assert(area<=(unsigned)(((tf_pw+7)&~7)*4));
  if(!tf_cosneeded&&!tf_capneeded)break;
  assert(n<TF_COS_MAX);
 }
 area=flushes=0;bt_face_paint(0,0);assert(!area&&!flushes);
 assert(!memcmp(&before,&bt_session,sizeof(before)));caption=old;bt_face_paint(0,1);
}
static void screens(void)
{
 int s,i;
 for(s=0;s<=BT_S_PROFILE;s++){
  bt_screen=s;status_seen=reaction_seen=accuracy_dash=0;accuracy_y=-1;achievement_seen=0;bt_face_paint(0,1);
  if(s==BT_S_SETTINGS)assert(status_seen);
  if(s==BT_S_RESULTS){assert(reaction_seen);if(!bt_session.attempts)assert(accuracy_dash);}
  if(s==BT_S_RECORDS)assert(achievement_seen==127);
  if(s==BT_S_SETTINGS){int required[]={BT_A_MUSIC,BT_A_EFFECTS,BT_A_KEYCLICK,BT_A_ROAST,BT_A_DISPLAY,BT_A_TEST_MUSIC,BT_A_FULL,BT_A_SAVE,BT_A_BACK};
   assert(tf_nhit==9);for(unsigned a=0;a<sizeof(required)/sizeof(required[0]);a++){int found=0;for(i=0;i<tf_nhit;i++)if(tf_hit[i][4]==required[a])found=1;assert(found);}}
  for(i=0;i<tf_nhit;i++){
   assert(tf_hit[i][0]>=tf_x&&tf_hit[i][0]+tf_hit[i][2]<=tf_x+tf_w);
   if(tf_hit[i][1]<tf_top||tf_hit[i][1]+tf_hit[i][3]>tf_bottom){fprintf(stderr,"outside screen%d %dx%d hit%d y%d h%d top%d bottom%d\n",s,hw,hh,i,tf_hit[i][1],tf_hit[i][3],tf_top,tf_bottom);abort();}
   assert(bt_face_click(tf_hit[i][0]+tf_hit[i][2]/2,tf_hit[i][1]+tf_hit[i][3]/2)==tf_hit[i][4]);
  }
 }
}
int main(void)
{
 int dims[][2]={{624,128},{624,160},{624,240},{624,280},{624,300},{624,420},{616,410},{640,480},{784,540},{1008,708},{480,128},{480,150}};
 unsigned fullarea;
 strcpy(bt_store.profile[0].name,"Learner");strcpy(bt_session.passage,"fj jf FJ!? My name is Borat.");bt_session.length=strlen(bt_session.passage);bt_session.status=BT_ACTIVE;
 for(int m=0;m<2;m++)for(unsigned d=0;d<sizeof(dims)/sizeof(dims[0]);d++){
  hw=dims[d][0];hh=dims[d][1];hbpp=m?1:4;bt_face_init(1,2,8192,hbpp);bt_screen=BT_S_PLAY;bt_session.pos=0;bt_session.last_wrong=0;bt_session.attempts=0;bt_session.correct=0;bt_session.streak=0;
  bt_face_paint(0,1);keyboard();screens();bt_screen=BT_S_PLAY;bt_face_paint(0,1);
  area=flushes=0;bt_face_paint(0,0);assert(area==0&&flushes==0);
  fullarea=hw*hh;bt_session.pos=1;bt_session.streak=1;bt_session.correct=1;bt_session.attempts=1;bt_face_paint(0,0);
  if(area>fullarea/6){fprintf(stderr,"too much repaint %dx%d %u/%u\n",hw,hh,area,fullarea);abort();}
  printf("face %dx%d %dbpp: geometry/screens/clicks pass, key %u pixels, %u bands\n",hw,hh,hbpp,area,flushes);
  blink();caption_change();
 }
 hw=304;hh=130;bt_face_init(1,2,8192,4);area=flushes=0;bt_face_paint(0,1);assert(!area&&!flushes&&!tf_nhit);
 puts("face: attentive blink selection/cache/restoration and session immutability pass");
 puts("face: all printable keys, opposite Shift, I/J finger map, bounded caches pass");return 0;
}
