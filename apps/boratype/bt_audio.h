/* Original visual-reaction tags and game-kit music bridge. */
#ifndef BT_AUDIO_H
#define BT_AUDIO_H
#define BT_R_WELCOME 1
#define BT_R_ERROR 2
#define BT_R_LOW 3
#define BT_R_POOR 4
#define BT_R_BACK 5
#define BT_R_IDLE 6
#define BT_R_RECOVER 7
#define BT_R_STREAK 8
#define BT_R_SUCCESS 9
#define BT_R_RECORD 10
const char *bt_reaction_history(int back);
void bt_mhold(int held); /* phase-preserving music pause, near cdecl */
#endif
