/* Cold menu actions/selection and whole-state immutability. Shared native
 * band mocks are the existing face harness; no guest pixels are claimed. */
#define main existing_face_main
#include "face_test.c"
#undef main
static BTStore menu_store_before;
static BTSession menu_session_before;
static int menu_has(int action)
{
 int i;for(i=0;i<tf_nhit;i++)if(tf_hit[i][4]==action)return 1;return 0;
}
int main(void)
{
 int dims[][2]={{480,128},{480,150},{624,128},{624,160},{624,240},{624,280},{624,300},{624,420},{784,540},{1008,708}};
 strcpy(bt_store.profile[0].name,"Learner");
 bt_store.profile[0].lesson_best[0]=95;bt_store.profile[0].lesson_done[0]=1;
 bt_store.profile[0].achievements=127;bt_store.profile[0].best_streak=50;
 bt_store.music=bt_store.effects=1;
 strcpy(bt_session.passage,"fj jf");bt_session.length=5;
 memcpy(&menu_store_before,&bt_store,sizeof(bt_store));memcpy(&menu_session_before,&bt_session,sizeof(bt_session));
 for(int mono=0;mono<2;mono++)for(unsigned d=0;d<sizeof(dims)/sizeof(dims[0]);d++){
  hw=dims[d][0];hh=dims[d][1];hbpp=mono?1:4;bt_face_init(1,2,8192,hbpp);
  screens();assert(!memcmp(&menu_store_before,&bt_store,sizeof(bt_store)));
  assert(!memcmp(&menu_session_before,&bt_session,sizeof(bt_session)));
  if(!tf_compact)assert(menu_has(BT_A_BACK)); /* final screen is Profile */
  if(hw>=600&&hh>=360){
   bt_screen=BT_S_LESSONS;bt_selection=23;bt_face_paint(0,1);
   assert(menu_has(BT_A_LESSON_BASE+23)&&menu_has(BT_A_START)&&menu_has(BT_A_BACK));
   assert(!menu_has(BT_A_LESSON_BASE)); /* selected page is not the first page */
   bt_selection=0;bt_face_paint(0,0);assert(menu_has(BT_A_LESSON_BASE));
   assert(!menu_has(BT_A_LESSON_BASE+23));
   bt_screen=BT_S_RECORDS;bt_face_paint(0,1);
   for(int i=0;i<3;i++)assert(menu_has(BT_A_DURATION_BASE+i));
   for(int i=0;i<4;i++)assert(menu_has(BT_A_DIFFICULTY_BASE+i));
   bt_screen=BT_S_SETTINGS;bt_face_paint(0,1);area=0;
   bt_store.effects=0;bt_face_paint(0,0);assert(area<(unsigned)(hw*hh/2));bt_store.effects=1;
  }
  assert(!memcmp(&menu_store_before,&bt_store,sizeof(bt_store)));
  assert(!memcmp(&menu_session_before,&bt_session,sizeof(bt_session)));
 }
 /* Badge drawing must survive a one-row clip and restore font/sound bank1. */
 tf_mono=0;tf_sx=0;tf_sy=108;tf_sw=640;tf_sh=1;animlen[2]=1;
 for(int earned=0;earned<2;earned++)for(int icon=0;icon<5;icon++){
  last_sprite=-1;ovl_tm_badge(100,100,icon,earned);
  assert(last_sprite==266+icon+(earned?0:5)&&last_art2==1);
 }
 tf_sy=116;last_sprite=-1;ovl_tm_badge(100,100,0,0);assert(last_sprite==-1);
 puts("menus: geometry/state, one-row clipped badges and restored art bank pass");
 return 0;
}
