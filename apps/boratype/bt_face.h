#ifndef BT_FACE_H
#define BT_FACE_H
#define BT_A_START 1
#define BT_A_LESSONS 2
#define BT_A_PRACTICE 3
#define BT_A_CHALLENGE 4
#define BT_A_RECORDS 5
#define BT_A_SETTINGS 6
#define BT_A_HELP 7
#define BT_A_BACK 8
#define BT_A_RETRY 9
#define BT_A_NEXT 10
#define BT_A_WEAK 11
#define BT_A_PAUSE 12
#define BT_A_FULL 13
#define BT_A_SAVE 14
#define BT_A_PROFILE 15
#define BT_A_TEST_MUSIC 16
#define BT_A_LESSON_BASE 32
#define BT_A_PROFILE_BASE 64
#define BT_A_DURATION_BASE 80
#define BT_A_DIFFICULTY_BASE 84
#define BT_A_MUSIC 90
#define BT_A_EFFECTS 91
#define BT_A_KEYCLICK 93
#define BT_A_ROAST 94
#define BT_A_DISPLAY 95
#define BT_A_HINT_BASE 256
/* UI globals: Shift left/right and Caps latch; weak-key practice selection. */
extern int bt_modifiers, bt_practice_weak;
/* Root advances this from OS ticks independently of scoring. */
extern int bt_blink_frame;
#define BT_A_PRACTICE_BASE 100
#define BT_A_ROAST_BASE 104
#define BT_A_WINDOWED 107
#define BT_A_FULLSCREEN 108

/* Compose only, into the caller's current Breadboard band. */
void ovl_bt_portrait_scene(int x,int y,int w,int h);
/* UI pulse, after reaction service and before optional drawing. */
void bt_face_clock(unsigned now);
int bt_key_base(int c);
const char *bt_key_finger(int c);
int bt_key_shift(int c);

#endif
