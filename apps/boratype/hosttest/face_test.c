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
#include "../bt.h"
BTStore bt_store;BTSession bt_session;
#define TESTLESSON {"F and J","fj ","fj","Rest index fingers on F and J.","fj jf"}
const BTLesson bt_lessons[BT_LESSONS]={
 TESTLESSON,TESTLESSON,TESTLESSON,TESTLESSON,TESTLESSON,TESTLESSON,
 TESTLESSON,TESTLESSON,TESTLESSON,TESTLESSON,TESTLESSON,TESTLESSON,
 TESTLESSON,TESTLESSON,TESTLESSON,TESTLESSON,TESTLESSON,TESTLESSON,
 TESTLESSON,TESTLESSON,TESTLESSON,TESTLESSON,TESTLESSON,TESTLESSON};
int bt_screen,bt_selection,bt_ch_duration,bt_ch_diff,bt_master_sound=1;
int bt_full,bt_readonly,bt_profile_edit,bt_modifiers,bt_practice_weak;
char bt_notice[64];
static int hw=640,hh=440,hbpp=4;
void os88_set_color(int c){(void)c;}
void os88_gfx_fill(int a,int b,int c,int d){(void)a;(void)b;(void)c;(void)d;}
static unsigned flushes,area;
static int bandx,bandy,bandw,bandh;
unsigned bx_bseg;
static const char *caption="Your fingers have returned from holiday.";
const char *bt_caption(void){return caption;}int bt_expression(void){return 1;}
int bt_weak(int rank){return rank==0?'f':0;}
void os88_wm_content(void *w,struct os88_pt *p){(void)w;p->x=8;p->y=24;}
int os88_wm_geom(void *w,struct os88_size *s){(void)w;s->w=hw;s->h=hh;return 0;}
void os88_wm_display(void *w,struct os88_video *v){(void)w;v->bpp=hbpp;}
char *os88_utoa(unsigned v,char *s){snprintf(s,8,"%u",v);return s;}
#include "../bt_face.c"
int bx_rows(int x,int y,int w,int h){(void)x;(void)y;(void)h;return 8192/(hbpp==1?w/8:w/2);}
void bx_new(int x,int y,int w,int h,int c){(void)c;assert(!(x&7)&&!(w&7));assert(x>=8&&x+w<=8+hw);assert(y>=24&&y+h<=24+hh);bandx=x;bandy=y;bandw=w;bandh=h;}
void bx_fill(int a,int b,int c,int d,int col){(void)a;(void)b;(void)c;(void)d;(void)col;}
int bx_spr(int x,int y,int id){(void)x;(void)y;(void)id;return 40;}
int bx_twidth(const char *s,int f){return (int)strlen(s)*(f==BT_F_TITLE?9:f==BT_F_TEXT?6:5);}
int bx_fonth(int f){return f==BT_F_TITLE?16:f==BT_F_TEXT?10:8;}
int bx_text(int x,int y,const char *s,int c,int f){(void)x;(void)y;(void)c;return bx_twidth(s,f);}
int bx_out(void){flushes++;area+=bandw*bandh;return 0;}
void bx_setup(unsigned a,void *b,unsigned c,unsigned d,int e){(void)b;(void)c;(void)d;(void)e;bx_bseg=a;}
void bx_setart2(unsigned a){(void)a;}
void gk_init(int a){(void)a;}void gk_theme(const unsigned char *a){(void)a;}
int gk_text(int x,int y,int w,const char *s,int c,int f,int fl){(void)x;(void)y;(void)w;(void)c;(void)fl;return bx_twidth(s,f);}
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
 assert(tf_keys[tf_keyid('i')][5]==6);assert(tf_keys[tf_keyid('j')][5]==5);
 assert(tf_shift('A')==52&&tf_shift('J')==51&&tf_shift('?')==51&&tf_shift('!')==52);
 for(i=32;i<127;i++)assert(tf_keyid(i)>=0);
}
static void screens(void)
{
 int s,i;
 for(s=0;s<=BT_S_PROFILE;s++){
  bt_screen=s;bt_face_paint(0,1);
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
 }
 hw=304;hh=130;bt_face_init(1,2,8192,4);area=flushes=0;bt_face_paint(0,1);assert(!area&&!flushes&&!tf_nhit);
 puts("face: all printable keys, opposite Shift, I/J finger map, bounded caches pass");return 0;
}
