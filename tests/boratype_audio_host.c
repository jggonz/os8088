/* Production director with a word-width host and observable sound bridge. */
#define BT_HOST
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../apps/boratype/bt.h"
#include "../apps/boratype/bt_audio.h"
BTStore bt_store;
BTSession bt_session;
int bt_screen,bt_selection,bt_ch_duration,bt_ch_diff,bt_master_sound,bt_full,bt_readonly,bt_profile_edit;
char bt_notice[64];
static int plays,fx,lastfx,song=-1,stops,held,caps=1,vmatches=1,vplays,vplaying;
int gkm_init(int a,int b) { assert(a==256&&b==257);return 2; }
void gkm_play(int s,int loop) { (void)loop;plays++;song=s; }
void gkm_stop(void) {song=-1;stops++;}
int gkm_playing(void) {return song;}
void gkm_sfx(int e) { fx++;lastfx=e; }
void gkm_volume(int m,int e) { (void)e;if(!m)song=-1; }
void gkm_tick(void) {}
void gkm_done(void) {gkm_stop();}
void bt_mhold(int h) {held=h;}
int bt_vcaps(void) {return caps;}
int bt_vvalidate(unsigned seg,unsigned len) {(void)seg;(void)len;return 1;}
void bt_vset(unsigned seg) {(void)seg;}
void bt_vmove(unsigned seg) {(void)seg;}
int bt_vplay(int tag) {(void)tag;vplays++;vplaying=vmatches;return vmatches;}
int bt_vtick(void) {return vplaying;}
void bt_vstop(void) {vplaying=0;}
const char *bt_vcaption(void) {return "test reaction";}
unsigned os88_mem_claim_p(unsigned n) {(void)n;return 1;}
int os88_mem_free(unsigned seg) {(void)seg;return 0;}
int os88_mem_movable(unsigned seg,int on) {(void)seg;(void)on;return 0;}
unsigned os88_file_read_seg(const char *p,unsigned s,unsigned c) {(void)p;(void)s;(void)c;return 0;}
int os88_ferr(void) {return 4;}
#include "../apps/boratype/bt_audio.c"
static void reset(void)
{
 memset(&bt_store,0,sizeof(bt_store));memset(&bt_session,0,sizeof(bt_session));
 bt_master_sound=1;bt_readonly=0;bt_store.music=bt_store.effects=bt_store.voice=1;
 bt_store.roast=1;caps=1;vmatches=1;vplaying=0;fx=plays=stops=vplays=0;
 bt_audio_init();bt_audio_tick(1,1);bt_audio_state(BT_S_PLAY);
}
int main(void)
{
 int i,j,roasts=0,old,prior[4],f;
 static BTSession before_welcome;
 reset();
 /* Automatic welcome uses actual first-focus ticks, never init's tick0.
  * It cannot claim sound before focus and must leave every score byte alone. */
 bt_audio_done();bt_audio_init();bta_pack=1;vplays=0;
 memcpy(&before_welcome,&bt_session,sizeof(bt_session));
 bt_audio_tick(500,0);assert(bta_welcome && !bta_have && vplays==0);
 bt_audio_tick(600,1);assert(!bta_welcome && bta_spoke==600 && bta_caption_at==600);
 assert(bta_lasttag==BT_R_WELCOME && vplays==1 && bta_voice_at==600);
 assert(!memcmp(&before_welcome,&bt_session,sizeof(bt_session)));
 bt_audio_tick(601,1);assert(vplays==1); /* exactly once */
 bt_audio_done();bt_audio_init();bta_pack=1;vplays=0;bt_master_sound=0;
 bt_audio_tick(700,1);assert(!bta_welcome && !bta_have && vplays==0);
 bt_master_sound=1;bt_audio_tick(701,1);assert(vplays==0); /* mute cancelled */
 bt_audio_done();bt_audio_init();bta_pack=1;vplays=0;bt_audio_done();
 bt_audio_tick(800,1);assert(!bta_welcome && vplays==0); /* close cancelled */
 reset();
 for(i=0;i<BT_MSGS;i++) {
  assert(strlen(bt_jokes[i].text)<48);
  if(bt_jokes[i].level)roasts++;
  for(j=0;j<i;j++)assert(strcmp(bt_jokes[i].text,bt_jokes[j].text));
 }
 assert(BT_MSGS>=48 && roasts>=24);
 /* Exact threshold:3 consecutive errors, sample floor20. */
 bt_session.error_run=2;bt_audio_event(BT_EV_WRONG,220);assert(bta_lasttag==1);
 bt_session.error_run=3;bt_audio_event(BT_EV_WRONG,221);assert(bta_lasttag==2);
 old=bta_message;for(i=222;i<400;i++)bt_audio_event(BT_EV_WRONG,(BTWord)i);
 assert(bta_message==old); /* burst coalesced, no queue */
 bt_session.error_run=0;bt_session.accuracy=20;bt_session.attempts=19;
 bt_audio_event(BT_EV_TICK,500);assert(bta_lasttag==2);
 bt_session.attempts=20;bt_audio_event(BT_EV_TICK,501);assert(bta_lasttag==3);
 /*2s captions cannot preempt12s cooldown; arithmetic crosses65535. */
 bta_spoke=65520;bta_have=1;old=bta_message;
 bt_audio_event(BT_EV_WRONG,50);assert(bta_message==old);
 /* Intensity never chooses a roast above its setting. Four recent IDs
  * cannot repeat; several event categories guarantee an available line. */
 for(i=0;i<3;i++) {
  reset();bt_store.roast=(BTByte)i;
  for(j=0;j<4;j++)prior[j]=-1;
  for(j=0;j<30;j++) {
   bta_now=(BTWord)(300+j*220);bta_react(2+j%5,0);
   assert(bt_jokes[bta_message].level<=i+1);
   for(old=0;old<4;old++)assert(prior[old]!=bta_message);
   prior[j%4]=bta_message;
  }
 }
 reset();bt_session.backspaces=7;bt_audio_event(BT_EV_BACK,220);assert(bta_lasttag==1);
 bt_session.backspaces=8;bt_audio_event(BT_EV_BACK,221);assert(bta_lasttag==5);
 bta_haderror=1;bt_session.streak=10;bt_audio_event(BT_EV_CORRECT,450);assert(bta_lasttag==7);
 reset();bt_session.status=BT_ACTIVE;bt_audio_tick(1094,1);assert(bta_lasttag==6);
 old=bta_message;bt_audio_tick(1300,1);assert(bta_message==old);
 /* On/off, held focus and music phase selection. */
 reset();bt_audio_tick(3,1);old=plays;bt_audio_tick(4,1);assert(plays==old);
 bt_audio_tick(5,0);assert(held);bt_audio_tick(6,1);assert(!held);
 assert(plays==old); /* director did not restart unchanged tune */
 bt_store.music=0;bt_audio_tick(7,1);assert(song==-1);
 bt_store.music=1;bt_audio_tick(7,1);bt_audio_state(BT_S_SETTINGS);
 bt_session.status=BT_PAUSED;bt_audio_tick(7,1);assert(!held);
 old=plays;bt_audio_test(0);assert(plays==old+1&&song==0);
 bt_audio_state(BT_S_PLAY);bt_session.status=BT_ACTIVE;
 bt_master_sound=0;bt_audio_tick(8,1);assert(!bta_on&&stops>0);
 f=fx;bt_audio_event(BT_EV_WRONG,300);assert(fx==f);
 /* Different sound toggles and no scoring mutation. */
 reset();bt_store.keyclick=0;f=fx;bt_audio_event(BT_EV_CORRECT,5);assert(fx==f);
 bt_store.keyclick=1;bt_audio_event(BT_EV_CORRECT,6);assert(lastfx==2);
 old=bt_session.attempts;bta_now=300;bta_react(2,0);assert(bt_session.attempts==old);
 /* Loaded pack, spoken12s minimum even forced record, focus/mute cleanup. */
 reset();bta_pack=1;bta_now=300;bta_react(2,0);assert(vplays==1&&bta_voice);
 vplaying=0;bt_audio_tick(301,1);bta_now=302;bta_react(10,1);assert(vplays==1);
 bta_now=600;bta_react(9,1);assert(vplays==2);
 bt_audio_tick(601,0);assert(!bta_voice&&!vplaying);
 bt_audio_done();assert(!bta_pack&&!bta_voice);
 reset();bt_readonly=1;bt_audio_tick(3,1);assert(!bta_on);
 printf("boratype audio:60messages,%droasts;threshold/cooldown/wrap/intensity/history/music/mute/voice checks passed\n",roasts);
 return 0;
}
