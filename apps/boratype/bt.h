/* Borat Teaches Typing: shared engine contract, BORATYPE-SPEC sections 1-6.
 * No OS calls here. All addressable objects are static at the call site. */
#ifndef BT_H
#define BT_H
#ifdef BT_HOST
#include <stdint.h>
typedef uint16_t BTWord;
#else
typedef unsigned BTWord;
#endif
typedef unsigned char BTByte;
#define BT_LESSONS 24
#define BT_PROFILES 4
#define BT_KEYS 95
#define BT_PASSAGE 192
#define BT_DIFFS 4
#define BT_SAVE_CAP 4096
#define BT_LESSON 0
#define BT_PRACTICE 1
#define BT_CHALLENGE 2
#define BT_IDLE 0
#define BT_READY 1
#define BT_ACTIVE 2
#define BT_PAUSED 3
#define BT_FINISHED 4
#define BT_EV_CORRECT 1
#define BT_EV_WRONG 2
#define BT_EV_BACK 4
#define BT_EV_PASSAGE 8
#define BT_EV_FINISH 16
#define BT_EV_STREAK 32
#define BT_EV_START 64
#define BT_EV_TICK 128
#define BT_ACH_FIRST 1
#define BT_ACH_MASTER 2
#define BT_ACH_25 4
#define BT_ACH_50 8
#define BT_ACH_95 16
#define BT_ACH_PERFECT 32
#define BT_ACH_BEST 64
typedef struct {
    const char *name, *allowed, *introduced, *instruction, *drill;
} BTLesson;
typedef struct {
    char name[13];
    BTByte lesson_best[BT_LESSONS];
    BTByte lesson_done[BT_LESSONS];
    BTWord key_attempt[BT_KEYS], key_error[BT_KEYS];
    BTWord record_wpm[3][BT_DIFFS], record_accuracy[3][BT_DIFFS];
    BTWord achievements, best_streak, practice_minutes, baseline_wpm;
    BTByte next_lesson;
} BTProfile;
typedef struct {
    BTProfile profile[BT_PROFILES];
    BTByte selected, music, effects, voice, keyclick, roast, display;
    BTWord generation;
} BTStore;
typedef struct {
    BTByte mode, status, lesson, difficulty, duration, ranked, started, committed;
    BTByte last_wrong, error_run, countdown, new_record;
    BTWord attempts, correct, errors, net, pos, length, completed;
    BTWord streak, best_streak, backspaces, elapsed, last_tick, ready_ticks;
    BTWord wpm, accuracy, seconds, remaining, seed, events, achievement_new;
    char passage[BT_PASSAGE + 1];
} BTSession;
extern BTStore bt_store;
extern BTSession bt_session;
extern const BTLesson bt_lessons[BT_LESSONS];
void bt_init(void);
void bt_start(int mode, int lesson, int duration, int difficulty, BTWord now);
unsigned bt_key(int ascii, BTWord now);
unsigned bt_tick(BTWord now);
void bt_pause(BTWord now);
void bt_resume(BTWord now);
void bt_finish(BTWord now);
void bt_commit(void);
unsigned bt_seconds(BTWord ticks);
unsigned bt_muldiv(BTWord a, BTWord b, BTWord divisor);
int bt_weak(int rank); /* ASCII or zero, >=20 attempts, highest error ratio first */
int bt_recommend(void);
unsigned bt_encode(BTByte *buf, unsigned cap);
int bt_decode(const BTByte *buf, unsigned len); /* validate before live replacement */
int bt_validate_lessons(void);

/* UI state belongs to platform; face reads it and never advances session. */
#define BT_S_HOME 0
#define BT_S_LESSONS 1
#define BT_S_PLAY 2
#define BT_S_RESULTS 3
#define BT_S_PRACTICE 4
#define BT_S_CHALLENGE 5
#define BT_S_RECORDS 6
#define BT_S_SETTINGS 7
#define BT_S_HELP 8
#define BT_S_PAUSE 9
#define BT_S_PROFILE 10
extern int bt_screen, bt_selection, bt_ch_duration, bt_ch_diff, bt_master_sound;
extern int bt_full, bt_readonly, bt_profile_edit;
extern char bt_notice[64];
/* Face bridge owned by UI lane. Main paints with full=1 for W_PAINT and
 * full=0 for state changes. Mouse coordinates are absolute, as OS events. */
void bt_face_init(unsigned art, unsigned band, unsigned cap, int bpp);
void bt_face_paint(void *win, int full);
int bt_face_click(int x, int y); /* action ID, defined by bt_face.h */
void bt_face_move(unsigned was, unsigned now);
/* Audio/reactions bridge owned by audio lane. Events never mutate scoring. */
void bt_audio_init(void);
void bt_audio_tick(BTWord now, int focused);
void bt_audio_event(unsigned events, BTWord now);
void bt_audio_state(int screen);
void bt_audio_done(void);
const char *bt_caption(void);
int bt_expression(void); /* 0 welcome, 1 attentive, 2 pleased, 3 smug,
                         * 4 disappointed, 5 laughing, 6 shocked */
void bt_audio_test(int voice);
#endif
