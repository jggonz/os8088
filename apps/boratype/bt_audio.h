/* Local audio contract. Event numbers are also BTV1 voice-pack tags. */
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
/* Settings/result helpers; captions are original jokes, voice captions quote
 * only the imported sound. Display the latter as "Voice reaction: ...". */
const char *bt_voice_status(void);
const char *bt_voice_caption(void);
const char *bt_reaction_history(int back);
int bt_audio_reload(const char *path); /* UI entry/onwake ONLY: disk I/O */
void bt_audio_move(unsigned was,unsigned now); /* relocation assignments only */
/* Assembly bridge. All are near cdecl, preserve BP/DS/ES and clear DF. */
int bt_vvalidate(unsigned seg, unsigned len);
int bt_vplay(int tag);
int bt_vtick(void);
void bt_vstop(void);
void bt_vset(unsigned seg);
void bt_vmove(unsigned seg); /* preserve playback offsets/caption, change base */
const char *bt_vcaption(void);
int bt_vcaps(void);
void bt_mhold(int held); /* phase-preserving sequencer pause */
#endif
