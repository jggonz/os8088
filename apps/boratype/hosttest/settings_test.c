/* The runner inserts the exact production bt_action below, without UI/OS
 * emulation. These spies verify direct choices and idempotent mode requests. */
#define BT_HOST
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bt.h"
#include "bt_face.h"
#define OS88_VID_CGA 2
BTStore bt_store;
BTSession bt_session;
int bt_screen,bt_selection,bt_ch_duration,bt_ch_diff,bt_master_sound;
int bt_full,bt_readonly,bt_profile_edit,bt_practice_weak;
static int bt_in_cga,bt_return_screen;
static void *bt_win;
static struct {int kind;} bt_video;
static int saves,syncs,toggles;
static BTSession before;
static void bt_message(const char *s){(void)s;}
int bt_recommend(void){return 0;}
static void bt_schedule_save(void){saves++;}
static void bt_begin(int mode){(void)mode;}
static void bt_setscreen(int screen){bt_screen=screen;}
static void bt_pause_ui(void){}
static void bt_fullscreen_toggle(void){toggles++;bt_full^=1;}
static void bt_audio_sync(void){syncs++;}
void bt_audio_test(void){}
static void os88_wm_display(void *w,void *v){(void)w;(void)v;}
static void bt_redraw(int all){(void)all;}
/* BT_ACTION_BODY */
int main(void)
{
 int choice;
 bt_screen=BT_S_SETTINGS;bt_store.music=bt_store.effects=1;bt_store.roast=1;
 memset(&bt_session,0x55,sizeof(bt_session));memcpy(&before,&bt_session,sizeof(before));
 for(choice=0;choice<3;choice++){
  bt_action(BT_A_ROAST_BASE+choice);assert(bt_store.roast==choice);
  bt_action(BT_A_ROAST_BASE+choice);assert(bt_store.roast==choice);
 }
 assert(saves==6&&syncs==6&&bt_store.music&&bt_store.effects);
 bt_action(BT_A_ROAST);assert(bt_store.roast==0); /* old R cycle retained */
 bt_action(BT_A_WINDOWED);bt_action(BT_A_WINDOWED);assert(!bt_full&&!toggles);
 bt_action(BT_A_FULLSCREEN);assert(bt_full&&toggles==1);
 bt_action(BT_A_FULLSCREEN);assert(bt_full&&toggles==1);
 bt_action(BT_A_WINDOWED);assert(!bt_full&&toggles==2);
 bt_action(BT_A_WINDOWED);assert(!bt_full&&toggles==2);
 bt_action(BT_A_FULL);bt_action(BT_A_FULL);assert(!bt_full&&toggles==4);
 bt_in_cga=1;bt_action(BT_A_FULLSCREEN);assert(!bt_full&&toggles==4);
 assert(bt_screen==BT_S_SETTINGS&&!memcmp(&before,&bt_session,sizeof(before)));
 puts("settings: production direct roast choices, old cycle and idempotent Windowed/Full requests pass");
 return 0;
}
