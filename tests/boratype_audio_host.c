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
static int plays,fx,lastfx,song=-1,stops,held;
int gkm_init(int a,int b) { assert(a==256&&b==257);return 2; }
void gkm_play(int s,int loop) { (void)loop;plays++;song=s; }
void gkm_stop(void) {song=-1;stops++;}
int gkm_playing(void) {return song;}
void gkm_sfx(int e) { fx++;lastfx=e; }
void gkm_volume(int m,int e) { (void)e;if(!m)song=-1; }
void gkm_tick(void) {}
void gkm_done(void) {gkm_stop();}
void bt_mhold(int h) {held=h;}
#include "../apps/boratype/bt_audio.c"
static void reset(void)
{
 memset(&bt_store,0,sizeof(bt_store));memset(&bt_session,0,sizeof(bt_session));
 bt_master_sound=1;bt_readonly=0;bt_store.music=bt_store.effects=1;bt_store.voice=0;
 bt_store.roast=1;fx=plays=stops=0;
 bt_audio_init();bt_audio_tick(1,1);bt_audio_state(BT_S_PLAY);
}
int main(void)
{
 int i,j,roasts=0,old,prior[4],f;
 static BTSession before_welcome;
 reset();
 /* Automatic welcome uses actual first-focus ticks, never init's tick0.
  * It cannot claim sound before focus and must leave every score byte alone. */
 bt_audio_done();bt_audio_init();
 memcpy(&before_welcome,&bt_session,sizeof(bt_session));
 bt_audio_tick(500,0);assert(bta_welcome && !bta_have);
 bt_audio_tick(600,1);assert(!bta_welcome && bta_spoke==600 && bta_caption_at==600);
 assert(bta_lasttag==BT_R_WELCOME);
 assert(!memcmp(&before_welcome,&bt_session,sizeof(bt_session)));
 bt_audio_tick(601,1);assert(bta_spoke==600); /* exactly once */
 bt_audio_done();bt_audio_init();bt_master_sound=0;
 bt_audio_tick(700,1);assert(!bta_welcome && bta_have && bta_spoke==700);
 assert(!memcmp(&before_welcome,&bt_session,sizeof(bt_session)));
 bt_audio_done();assert(!bta_welcome);
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
 old=plays;bt_audio_test();assert(plays==old+1&&song==0);
 bt_audio_state(BT_S_PLAY);bt_session.status=BT_ACTIVE;
 bt_master_sound=0;bt_audio_tick(8,1);assert(!bta_on&&stops>0);
 f=fx;bt_audio_event(BT_EV_WRONG,300);assert(fx==f);
 /* Different sound toggles and no scoring mutation. */
 reset();bt_store.keyclick=0;f=fx;bt_audio_event(BT_EV_CORRECT,5);assert(fx==f);
 bt_store.keyclick=1;bt_audio_event(BT_EV_CORRECT,6);assert(lastfx==2);
 old=bt_session.attempts;bta_now=300;bta_react(2,0);assert(bt_session.attempts==old);
 /* Muted and readonly instances keep visual roasts and idle jokes. */
 reset();bt_master_sound=0;bt_audio_tick(220,1);f=fx;
 bt_session.error_run=3;memcpy(&before_welcome,&bt_session,sizeof(bt_session));
 bt_audio_event(BT_EV_WRONG,221);assert(bta_lasttag==BT_R_ERROR && fx==f);
 assert(!memcmp(&before_welcome,&bt_session,sizeof(bt_session)));
 bt_session.status=BT_ACTIVE;memcpy(&before_welcome,&bt_session,sizeof(bt_session));
 bt_audio_tick(1314,1);assert(bta_lasttag==BT_R_IDLE && fx==f);
 assert(!memcmp(&before_welcome,&bt_session,sizeof(bt_session)));
 reset();bt_readonly=1;bt_session.status=BT_ACTIVE;f=fx;
 memcpy(&before_welcome,&bt_session,sizeof(bt_session));
 bt_audio_tick(1314,1);assert(!bta_on && bta_focus && bta_lasttag==BT_R_IDLE && fx==f);
 assert(!memcmp(&before_welcome,&bt_session,sizeof(bt_session)));
 /* No-input completion must replace a recent caption with useful neutral
  * guidance, even if stale result flags are present. Never edit scoring. */
 reset();bt_session.accuracy=100;bt_session.new_record=1;
 bt_session.achievement_new=BT_ACH_BEST;
 memcpy(&before_welcome,&bt_session,sizeof(bt_session));
 bt_audio_event(BT_EV_FINISH,2);
 assert(bta_lasttag==BT_R_EMPTY && bta_stinger==4 && lastfx==8);
 assert(bt_jokes[bta_message].level==0);
 assert(!memcmp(&before_welcome,&bt_session,sizeof(bt_session)));
 old=bta_message;f=fx;bt_audio_event(BT_EV_FINISH,300);
 assert(bta_message==old && fx==f); /* completed guard remains one-shot */
 assert(!memcmp(&before_welcome,&bt_session,sizeof(bt_session)));
 /* Three empty runs followed by a cancelled streak can fill recent four
  * with all original neutral choices plus praise. A0 must still replace
  * that praise without selecting any of those four caption IDs. */
 reset();
 for(i=0;i<3;i++){bta_now=(BTWord)(300+i*300);bta_react(BT_R_EMPTY,1);}
 bta_now=1200;bta_react(BT_R_STREAK,1);assert(bta_lasttag==BT_R_STREAK);
 for(i=0;i<4;i++)prior[i]=bta_recent[i];
 bt_audio_event(BT_EV_START,1201);
 memcpy(&before_welcome,&bt_session,sizeof(bt_session));
 bt_audio_event(BT_EV_FINISH,1202);
 assert(bta_lasttag==BT_R_EMPTY && bta_stinger==4 && lastfx==8);
 for(i=0;i<4;i++)assert(bta_message!=prior[i]);
 assert(!memcmp(&before_welcome,&bt_session,sizeof(bt_session)));
 reset();bt_session.attempts=1;bt_session.accuracy=100;
 bt_audio_event(BT_EV_FINISH,300);
 assert(bta_lasttag==BT_R_SUCCESS && bta_stinger==3 && lastfx==7);
 reset();bt_session.attempts=1;bt_session.new_record=1;
 bt_audio_event(BT_EV_FINISH,2);
 assert(bta_lasttag==BT_R_RECORD && bta_stinger==5 && lastfx==9);
 printf("boratype audio:%dmessages,%droasts;threshold/cooldown/wrap/intensity/history/music/mute/readonly/empty-result checks passed\n",BT_MSGS,roasts);
 return 0;
}
