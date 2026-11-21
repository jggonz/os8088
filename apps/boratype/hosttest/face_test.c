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
#define OS88_VID_HERC 1
#define OS88_VID_CGA 2
#define OS88_VID_EGA 3
void os88_wm_content(void *,struct os88_pt *);
int os88_wm_geom(void *,struct os88_size *);
void os88_wm_display(void *,struct os88_video *);
char *os88_utoa(unsigned,char *);
void os88_set_color(int);void os88_gfx_fill(int,int,int,int);
void os88_gfx_frame(int,int,int,int);void os88_gfx_hline(int,int,int);
void os88_font_run(int,int,const char *,int,int);
int os88_evq_pending(void);
#include "../bt.h"
BTStore bt_store;BTSession bt_session;
#define TESTLESSON {"F and J","fj ","fj","Rest index fingers on F and J.","fj jf"}
const BTLesson bt_lessons[BT_LESSONS]={
 TESTLESSON,TESTLESSON,TESTLESSON,TESTLESSON,TESTLESSON,TESTLESSON,
 TESTLESSON,TESTLESSON,TESTLESSON,TESTLESSON,TESTLESSON,TESTLESSON,
 TESTLESSON,TESTLESSON,TESTLESSON,TESTLESSON,TESTLESSON,TESTLESSON,
 TESTLESSON,TESTLESSON,TESTLESSON,TESTLESSON,TESTLESSON,TESTLESSON};
int bt_music_phase;
int bt_screen,bt_selection,bt_ch_duration,bt_ch_diff,bt_master_sound=1;
int bt_full,bt_readonly,bt_profile_edit,bt_modifiers,bt_practice_weak,bt_blink_frame;
char bt_notice[64];
static int hw=640,hh=440,hbpp=4;
static int pending_key,pending_event;
int bt_key_pending(void){return pending_key;}
int os88_evq_pending(void){return pending_event;}
void os88_set_color(int c){(void)c;}
void os88_gfx_fill(int a,int b,int c,int d){(void)a;(void)b;(void)c;(void)d;}
void os88_gfx_frame(int a,int b,int c,int d){assert(a>=8&&c<8+hw&&b>=24&&d<24+hh);}
void os88_gfx_hline(int a,int b,int c){assert(a>=8&&b<8+hw&&c>=24&&c<24+hh);}
void os88_font_run(int x,int y,const char *s,int ink,int paper){(void)s;(void)ink;(void)paper;assert(x>=8&&x+8<=8+hw&&y>=24&&y+8<=24+hh);}
static unsigned flushes,area;
static int bandx,bandy,bandw,bandh;
unsigned bx_bseg;
static unsigned last_art2;
static int expression=1,last_sprite=-1,status_seen,reaction_seen,accuracy_y,accuracy_dash;
static unsigned achievement_seen;
static const char *caption="Your fingers have returned from holiday.";
const char *bt_caption(void){return caption;}int bt_expression(void){return expression;}
const char *bt_reaction_history(int back){return back<4?"Your fingers have returned from holiday.":"";}
int bt_weak(int rank){return rank==0?'f':0;}
unsigned bt_muldiv(BTWord a,BTWord b,BTWord d){return d?(BTWord)((uint32_t)a*b/d):65535;}
void os88_wm_content(void *w,struct os88_pt *p){(void)w;p->x=8;p->y=24;}
int os88_wm_geom(void *w,struct os88_size *s){(void)w;s->w=hw;s->h=hh;return 0;}
void os88_wm_display(void *w,struct os88_video *v){(void)w;v->bpp=hbpp;v->w=hw;v->h=hh;v->kind=hbpp==1?OS88_VID_HERC:0;}
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
void bt_scene_copy(unsigned seg,int card,int src,int dst,int stride,int plane,int width,int rows){
 assert(seg==105&&card>=0&&card<4&&width>0&&rows>0&&width<=stride);
 assert(src>=0&&src+(rows-1)*stride+width<=plane);
 assert(dst>=0&&dst+(rows-1)*(tf_sw/8)+width<=(tf_sw/8)*tf_sh);
}
void bx_setart2(unsigned a){last_art2=a;}
void gk_init(int a){(void)a;}void gk_theme(const unsigned char *a){(void)a;}
int gk_text(int x,int y,int w,const char *s,int c,int f,int fl){if(!strcmp(s,tf_soundstatus()))status_seen++;if(strstr(s,"Your fingers"))reaction_seen++;if(!strcmp(s,"ACCURACY"))accuracy_y=y;if((!strcmp(s,"-")&&y==accuracy_y)||strstr(s,"Accuracy -"))accuracy_dash++;for(int a=0;a<7;a++)if(strstr(s,tf_achnames[a]))achievement_seen|=1u<<a;(void)x;(void)y;(void)w;(void)c;(void)fl;return bx_twidth(s,f);}
int gk_textbox(int x,int y,int w,int h,const char *s,int c,int f,int fl){
 if(bt_screen==BT_S_PLAY&&(!strcmp(s,"F")||!strcmp(s,"J"))){
  tf_keyrect(!strcmp(s,"F")?29:32);assert(x*2+w==tf_kx*2+tf_kww);assert(fl&GK_CENTRE);
  if(!tf_compact&&tf_khh>=26&&tf_kww>=34)assert(f==BT_F_TITLE);
 }
 (void)h;return gk_text(x,y,w,s,c,f,fl);
}
void gk_box(int x,int y,int w,int h,int s,int f){(void)x;(void)y;(void)w;(void)h;(void)s;(void)f;}
void gk_button(int x,int y,int w,int h,const char *s,int f,int st){(void)x;(void)y;(void)w;(void)h;(void)s;(void)f;(void)st;}
void gk_radio(int x,int y,int size,int on){(void)x;(void)y;(void)size;(void)on;}
static void keyboard(void)
{
 int i,j,act;
 for(i=0;i<58;i++){
  tf_keyrect(i);assert(tf_kx>=tf_x&&tf_kx+tf_kww<=tf_x+tf_w);assert(tf_ky0>=tf_top&&tf_ky0+tf_khh<=tf_bottom);
  assert(tf_kww>0&&tf_khh>=8);
  act=bt_face_click(tf_kx+tf_kww/2,tf_ky0+tf_khh/2);
  assert(act==BT_A_HINT_BASE+tf_keys[i][0]);
  for(j=i+1;j<58;j++)if(tf_keys[j][2]==tf_keys[i][2]){
   int left=tf_x+(tf_compact?8:16)+tf_keys[j][3]*tf_kw/4;
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
static void play_geometry(void)
{
 static BTSession saved;int i;
 assert(tf_inputcols>0&&tf_inputcols<=tf_cols);
 assert(tf_inputx+tf_inputcols*tf_cw<=tf_x+tf_w-8);
 if(!tf_compact){
  assert(tf_inputx-tf_promptx-6>=bx_twidth("YOU TYPE",BT_F_SMALL));
  assert(tf_kw*15>=tf_w-47&&tf_kw*15<=tf_w-32);
  assert(tf_pw>=136&&tf_pw<=196&&tf_promptx>=tf_px+tf_pw+8);
  assert(tf_prompty+2*tf_ch<=tf_inputy-1);
  assert(tf_inputy+tf_ch+1<=tf_hinty);
  assert(tf_hinty+tf_hinth<=tf_statsy-10);
  assert(tf_statsy+tf_stath+4==tf_ky);
  assert(tf_rowy[4]+tf_kh<=tf_bottom);
  if(tf_h==420&&tf_w==624){assert(tf_pw==152&&tf_ph==174&&tf_kh==31);}
 }
 memcpy(&saved,&bt_session,sizeof(saved));
 bt_session.status=BT_READY;tf_play_clock(100);assert(tf_clockready);
 bt_session.status=BT_ACTIVE;tf_play_clock(101);assert(tf_go_left==9);
 tf_play_clock(109);assert(tf_go_left==1);tf_play_clock(110);assert(!tf_go_left);
 tf_play_clock(117);assert(tf_caret_on==1);tf_play_clock(126);assert(tf_caret_on==0);
 memcpy(&bt_session,&saved,sizeof(saved));tf_caret_on=1;
 for(i=0;i<3;i++){
  bt_session.mode=BT_CHALLENGE;bt_session.duration=i;bt_session.remaining=i==0?60:i==1?180:300;
  assert(ovl_tf_progress_width()==tf_cols*tf_cw-4);bt_session.remaining=0;assert(!ovl_tf_progress_width());
 }
 memcpy(&bt_session,&saved,sizeof(saved));
}
static void mirror_and_stat_damage(void)
{
 static BTSession before;int old,steps=0;
 memcpy(&before,&bt_session,sizeof(before));bt_face_paint(0,1);
 if(!(tf_compact&&tf_h<140)){
  tf_dirtyfirst[2]=-1;tf_dirtylast[2]=2*tf_cols+tf_inputcols-1;
  tf_cache_cell(2*tf_cols,1);
  assert(tf_dirtyfirst[2]==2*tf_cols&&tf_dirtylast[2]==2*tf_cols);
  tf_dirtylast[2]=2*tf_cols+tf_inputcols-1;
  while(tf_dirtyfirst[2]>=0){
   old=tf_dirtyfirst[2];tf_caret_on=!tf_caret_on;
   bt_face_paint(0,0);
   assert(tf_dirtyfirst[2]<0||tf_dirtyfirst[2]>=old);
   assert(++steps<=2*tf_inputcols+20);
  }
 }
 bt_face_paint(0,1);tf_cosneeded=tf_capneeded=0;tf_statpend=1;tf_statrow[0]=0;steps=0;
 while(tf_statpend){area=flushes=0;bt_face_paint(0,0);assert(flushes==1);
  assert(area<=(unsigned)((tf_statsw/5+8)*4));assert(++steps<=4);}
 tf_hintpending=1;tf_hintrow=0;steps=0;
 pending_key=1;area=flushes=0;bt_face_paint(0,0);assert(!flushes&&tf_hintpending&&tf_hintrow==0);pending_key=0;
 pending_event=1;area=flushes=0;bt_face_paint(0,0);assert(!flushes&&tf_hintpending&&tf_hintrow==0);pending_event=0;
 while(tf_hintpending){area=flushes=0;bt_face_paint(0,0);assert(flushes==1);
  assert(area<=(unsigned)(tf_w*4));assert(++steps<=4);}
 assert(!memcmp(&before,&bt_session,sizeof(before)));bt_face_paint(0,1);
}
static unsigned char animbank[5][65000];static unsigned animlen[5],unpacks;
int os88_part_fetch(int i){char path[1024];FILE *f;assert(i>=3&&i<8);snprintf(path,sizeof(path),"%s/BTAN%d.BIN",getenv("BT_ANIM_DIR")?getenv("BT_ANIM_DIR"):"build",i-3);f=fopen(path,"rb");assert(f);animlen[i-3]=(unsigned)fread(animbank[i-3],1,65000,f);fclose(f);return 0;}
int os88_part_movable(int i){assert(i>=3&&i<8);return 0;}
void os88_part_drop(int i){assert(i>=3&&i<8);animlen[i-3]=0;}
unsigned os88_part_seg(int i){if(i==1)return 1;assert(i>=3&&i<8);return animlen[i-3]?100+i:0;}
void os88_seg_get(void *dst,unsigned seg,unsigned off,unsigned n){assert(seg>=103&&seg<=107&&off+n<=animlen[seg-103]);memcpy(dst,animbank[seg-103]+off,n);}
int bt_anim_unpack(unsigned seg,unsigned off,unsigned packed,unsigned raw){assert(seg>=103&&seg<=107&&off+packed<=animlen[seg-103]);assert(raw>0&&raw<=1152);unpacks++;return 0;}
/* Exercise direct planes at full VGA size and the packed fallback at smaller sizes. */
static unsigned direct_height;
int bt_anim_planar(unsigned seg,unsigned off,int x,int y,int height){
 assert((seg==104||seg==106||seg==107)&&off>=16&&off+4320+height*12<=animlen[seg-103]);
 assert(x==tf_ax&&y>=tf_ay&&y+height<=tf_ay+120&&height%24==0);
 direct_height=0;if(hw<600||hh<400)return -1;
 direct_height=height;area+=96*height;flushes++;unpacks++;return 0;
}
void bt_anim_rawstrip(unsigned seg,unsigned off){
 assert((seg==104||seg==106||seg==107)&&off+4320+288<=animlen[seg-103]);
 assert(bandw==96&&bandh==24);direct_height=0;unpacks++;
}
static void settle_anim(void)
{
 int budget=2*((tf_ph+3)/4+TF_CELLS/2+16);
 for(int i=0;i<budget&&(tf_cosneeded||tf_capneeded||tf_statpend||tf_hintpending||tf_footpending||tf_dirtyfirst[2]>=0||(!tf_compact&&tf_meterlast!=bt_music_phase));i++){
  unsigned prior=unpacks;area=flushes=0;bt_face_paint(0,0);assert(flushes<=1);
  if(unpacks!=prior){assert(unpacks==prior+1);assert(area<=(unsigned)(tf_aw*(direct_height?direct_height:24)));}
 }
 assert(!tf_cosneeded&&!tf_capneeded&&!tf_statpend&&!tf_hintpending&&!tf_footpending&&tf_dirtyfirst[2]<0);assert(tf_aready);
 if(!tf_compact&&tf_avisible){
  assert(tf_adone&&tf_ashown==tf_aframe);
  for(int i=0;i<(tf_avariant==0?5:2);i++){
   unsigned off=16+(tf_aframe*9+(tf_avariant==0?0:tf_avariant==1?5:7)+i)*8+6;
   unsigned expected=animbank[0][off]|(animbank[0][off+1]<<8);
   assert(tf_asig[i]==expected);
  }
 }
}
static void blink(void)
{
 static BTSession before;unsigned prior;
 memcpy(&before,&bt_session,sizeof(before));expression=1;
 bt_face_clock(0);bt_face_paint(0,1);settle_anim();
 for(unsigned now=1;now<=80;now++){bt_face_clock(now);settle_anim();}
 prior=unpacks;bt_face_clock(127);assert(tf_aframe==1);settle_anim();
 if(!tf_compact&&tf_avisible)assert(unpacks>prior&&unpacks-prior<=2);
 bt_face_clock(129);assert(tf_aframe==2);settle_anim();
 bt_face_clock(131);assert(tf_aframe==3);settle_anim();
 bt_face_clock(133);assert(tf_aframe==0);settle_anim();
 area=flushes=0;bt_face_paint(0,0);assert(!area&&!flushes);
 /* A delayed painter must never advance a not-yet-completed frame. */
 expression=3;bt_face_clock(200);assert(tf_aframe==18);
 if(!tf_compact&&tf_avisible){for(unsigned now=201;now<=220;now++){bt_face_clock(now);assert(tf_aframe==18);}settle_anim();}
 for(int seq=2;seq<=4;seq++){
  unsigned now=400;prior=unpacks;expression=seq==2?2:seq==3?3:4;bt_face_clock(now);
  for(int phase=0;phase<6;phase++){
   assert(tf_aframe==seq*6+phase);settle_anim();now+=5;bt_face_clock(now);
  }
  if(!tf_compact&&tf_avisible)assert(unpacks>prior);
  assert(tf_aframe==0);settle_anim();prior=unpacks;
  for(int t=1;t<30;t++){bt_face_clock(now+t);assert(tf_aframe==0);settle_anim();}
  assert(unpacks==prior);
 }
 expression=3;bt_face_clock(65530);assert(tf_aframe==18);settle_anim();
 bt_face_clock(65535);assert(tf_aframe==19);settle_anim();bt_face_clock(4);assert(tf_aframe==20);settle_anim();
 assert(!memcmp(&before,&bt_session,sizeof(before)));expression=1;bt_face_clock(140);
}
static void caption_change(void)
{
 static BTSession before;const char *old=caption;
 memcpy(&before,&bt_session,sizeof(before));bt_face_paint(0,1);
 caption="Your fingers called a meeting. The keyboard was not invited.";
 for(int n=0;n<2*((tf_ph+3)/4+24);n++){
  unsigned prior=unpacks;area=flushes=0;bt_face_paint(0,0);assert(flushes<=1);
  if(!tf_compact)assert(area<=(unsigned)(unpacks==prior?((tf_pw+7)&~7)*4:tf_aw*(direct_height?direct_height:24)));
  if(!tf_cosneeded&&!tf_capneeded)break;
  assert(n<2*((tf_ph+3)/4+24)-1);
 }
 area=flushes=0;bt_face_paint(0,0);assert(!area&&!flushes);
 assert(tf_capmsg==caption&&tf_capcount>0);
 {int pos=0;for(int i=0;i<tf_capcount;i++){
   assert(tf_caplen[i]>0&&tf_caplen[i]<=78);
   for(int k=0;k<tf_caplen[i];k++)tf_wrap[k]=caption[tf_capstart[i]+k];tf_wrap[tf_caplen[i]]=0;
   assert(bx_twidth(tf_wrap,tf_font)<=tf_capwidth);
   for(int k=0;k<tf_caplen[i];k++)if(tf_wrap[k]!=' '){while(caption[pos]==' ')pos++;assert(tf_wrap[k]==caption[pos++]);}
  }while(caption[pos]==' ')pos++;assert(!caption[pos]);}
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
   int rich=tf_w>=600&&tf_h>=360;
   assert(tf_nhit==(rich?12:9));for(unsigned a=0;a<sizeof(required)/sizeof(required[0]);a++){int found=0;if(rich&&(required[a]==BT_A_ROAST||required[a]==BT_A_FULL))continue;for(i=0;i<tf_nhit;i++)if(tf_hit[i][4]==required[a])found=1;assert(found);}
   if(rich){int direct[]={BT_A_ROAST_BASE,BT_A_ROAST_BASE+1,BT_A_ROAST_BASE+2,BT_A_WINDOWED,BT_A_FULLSCREEN};for(unsigned a=0;a<5;a++){int found=0;for(i=0;i<tf_nhit;i++)if(tf_hit[i][4]==direct[a])found=1;assert(found);}}}
  for(i=0;i<tf_nhit;i++){
   assert(tf_hit[i][0]>=tf_x&&tf_hit[i][0]+tf_hit[i][2]<=tf_x+tf_w);
   if(tf_hit[i][1]<tf_top||tf_hit[i][1]+tf_hit[i][3]>tf_bottom){fprintf(stderr,"outside screen%d %dx%d hit%d y%d h%d top%d bottom%d\n",s,hw,hh,i,tf_hit[i][1],tf_hit[i][3],tf_top,tf_bottom);abort();}
   assert(bt_face_click(tf_hit[i][0]+tf_hit[i][2]/2,tf_hit[i][1]+tf_hit[i][3]/2)==tf_hit[i][4]);
  }
 }
}
int main(void)
{
 int dims[][2]={{624,128},{624,160},{624,240},{624,280},{624,300},{624,380},{624,399},{624,400},{624,420},{616,410},{640,480},{784,540},{1008,708},{480,128},{480,150},{480,280},{480,300},{480,400}};
 unsigned fullarea;
 strcpy(bt_store.profile[0].name,"Learner");strcpy(bt_session.passage,"fj jf FJ!? My name is Borat.");bt_session.length=strlen(bt_session.passage);bt_session.status=BT_ACTIVE;
 for(int m=0;m<2;m++)for(unsigned d=0;d<sizeof(dims)/sizeof(dims[0]);d++){
  hw=dims[d][0];hh=dims[d][1];hbpp=m?1:4;bt_face_init(1,2,8192,hbpp);bt_screen=BT_S_PLAY;bt_session.pos=0;bt_session.last_wrong=0;bt_session.attempts=0;bt_session.correct=0;bt_session.streak=0;
  bt_face_paint(0,1);assert(!tf_bigready);ovl_bt_face_prepare();assert(tf_bigready==!m);bt_face_paint(0,1);
  keyboard();play_geometry();screens();bt_screen=BT_S_PLAY;mirror_and_stat_damage();bt_face_paint(0,1);
  area=flushes=0;bt_face_paint(0,0);assert(area==0&&flushes==0);
  fullarea=hw*hh;bt_session.pos=1;bt_session.streak=1;bt_session.correct=1;bt_session.attempts=1;bt_face_paint(0,0);
  if(area>fullarea/6){fprintf(stderr,"too much repaint %dx%d %u/%u\n",hw,hh,area,fullarea);abort();}
  printf("face %dx%d %dbpp: geometry/screens/clicks pass, key %u pixels, %u bands\n",hw,hh,hbpp,area,flushes);
  blink();caption_change();
  {static BTSession before;memcpy(&before,&bt_session,sizeof(before));
   bt_face_release();assert(!tf_bigready&&!os88_part_seg(4)&&!os88_part_seg(6)&&!os88_part_seg(7));
   assert(os88_part_seg(3));if(!m)assert(os88_part_seg(5));
   ovl_bt_face_prepare();assert(tf_bigready==!m);assert(!memcmp(&before,&bt_session,sizeof(before)));
   bt_face_release();}
 }
 hw=304;hh=130;bt_face_init(1,2,8192,4);area=flushes=0;bt_face_paint(0,1);assert(!area&&!flushes&&!tf_nhit);
 puts("face: original-grid blink/talk/one-shot sequences, bounded strips and session immutability pass");
 puts("face: all printable keys, opposite Shift, I/J finger map, bounded caches pass");return 0;
}
