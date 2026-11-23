/* Tests the actual included production C with word-sized state. The oracle
 * alone uses uint32_t; it is never compiled into the guest package. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../bt.h"
#include "../bt_lessons.c"
#include "../bt_core.c"
#include "../bt_codec.c"

static BTByte filebuf[BT_SAVE_CAP + 1], saved[BT_SAVE_CAP + 1];
static BTStore oldstore;
static BTSession oldsession;
static char drillcopy[BT_PASSAGE + 1];
static uint32_t rng = 0x12345678;
static uint32_t oracle_random(void)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng;
}
static BTWord tick_at_seconds(unsigned sec)
{
    return (BTWord)(((uint32_t)sec * 1193182 + 65535) / 65536);
}
static void fixture(const char *text)
{
    unsigned n = (unsigned)strlen(text);
    assert(n && n <= BT_PASSAGE);
    memcpy(bt_session.passage, text, n + 1); bt_session.length = (BTWord)n;
    bt_session.pos = bt_session.completed = bt_session.net = 0;
}
static void complete(BTWord now)
{
    unsigned watchdog = 0;
    while (bt_session.status == BT_ACTIVE) {
        assert(++watchdog <= BT_PASSAGE);
        bt_key(bt_session.passage[bt_session.pos], now++);
    }
    assert(bt_session.status == BT_FINISHED);
}
static void check_arithmetic(void)
{
    uint32_t i, a, b, d, want;
    for (i = 0; i <= 65535; i++)
        assert(bt_seconds((BTWord)i) == (i * 65536) / 1193182);
    assert(bt_muldiv(65535, 65535, 65535) == 65535);
    assert(bt_muldiv(65535, 65535, 1) == 65535);
    assert(bt_muldiv(0, 0, 0) == 65535);
    assert(bt_muldiv(65535, 0, 1) == 0);
    for (b = 1; b <= 65535; b++) {
        a = 65535 / b; d = 1 + b % 65535;
        assert(bt_muldiv((BTWord)a, (BTWord)b, (BTWord)d) == a * b / d);
        if (a < 65535) {
            a++; want = a * b / d; if (want > 65535) want = 65535;
            assert(bt_muldiv((BTWord)a, (BTWord)b, (BTWord)d) == want);
        }
    }
    for (i = 0; i < 200000; i++) {
        a = oracle_random() & 65535; b = oracle_random() & 65535; d = oracle_random() & 65535;
        want = d ? a * b / d : 65535;
        if (want > 65535) want = 65535;
        assert(bt_muldiv((BTWord)a, (BTWord)b, (BTWord)d) == want);
    }
    for (i = 0; i < 20000; i++) {
        a = oracle_random() & 65535; b = 1 + oracle_random() % 65535;
        d = 1 + oracle_random() % 65535;
        want = oracle_random() & 65535;
        assert((bt_ratio_compare((BTWord)a, (BTWord)b, (BTWord)want, (BTWord)d) > 0) ==
            (a * d > want * b));
    }
}
static void check_curriculum(void)
{
    int i;
    assert(bt_validate_lessons());
    bt_init();
    assert(bt_store.music && bt_store.effects && !bt_store.voice && !bt_store.keyclick);
    for (i = 0; i < BT_LESSONS; i++) {
        bt_start(BT_LESSON, i, 0, 0, 100);
        assert(!strcmp(bt_session.passage, bt_lessons[i].drill));
        assert(bt_recommend() == i);
        complete(100); bt_commit();
        assert(bt_store.profile[0].lesson_done[i]);
        assert(bt_store.profile[0].lesson_best[i] == 100);
        assert(bt_store.profile[0].next_lesson == (i == BT_LESSONS - 1 ? i : i + 1));
        memcpy(&oldstore, &bt_store, sizeof(oldstore)); bt_commit();
        assert(!memcmp(&oldstore, &bt_store, sizeof(oldstore)));
    }
    assert(bt_recommend() == 23);
    assert(bt_store.profile[0].achievements & BT_ACH_FIRST);
    assert(bt_store.profile[0].achievements & BT_ACH_MASTER);
    assert(bt_store.profile[0].achievements & BT_ACH_PERFECT);
    bt_start(BT_LESSON, 0, 0, 0, 100); complete(100); bt_commit();
    assert(!(bt_session.achievement_new & (BT_ACH_FIRST | BT_ACH_MASTER | BT_ACH_PERFECT)));
    bt_init(); bt_start(BT_LESSON, 0, 0, 0, 1); fixture("ffffffffffffffffffff");
    bt_key('j', 1); complete(1); bt_commit();
    assert(bt_session.accuracy == 95 && bt_store.profile[0].lesson_best[0] == 95);
    bt_init(); bt_start(BT_LESSON, 0, 0, 0, 1); fixture("ffffffffffffffffffff");
    bt_key('j', 1); bt_key('j', 1); complete(1); bt_commit();
    assert(bt_session.accuracy == 91 && bt_recommend() == 0);
}
static void check_input(void)
{
    int i;
    unsigned ev;
    static const int ignored[] = {0, 9, 13, 27, 31, 127, 255};
    bt_init(); bt_start(BT_LESSON, 0, 0, 0, 1); fixture("f F,!");
    bt_tick(1000); assert(!bt_session.elapsed && !bt_session.started);
    for (i = 0; i < (int)(sizeof(ignored) / sizeof(ignored[0])); i++) bt_key(ignored[i], 1000);
    assert(!bt_session.attempts && !bt_session.started);
    bt_key(8, 1000); assert(!bt_session.started && !bt_session.net && bt_session.backspaces == 1);
    ev = bt_key('F', 1001);
    assert((ev & (BT_EV_START | BT_EV_WRONG)) == (BT_EV_START | BT_EV_WRONG));
    assert(bt_session.started && !bt_session.elapsed && !bt_session.pos);
    assert(bt_session.attempts == 1 && bt_session.errors == 1 && !bt_session.correct);
    assert(bt_session.key_error['f' - 32] == 1 && !bt_session.key_error['F' - 32]);
    bt_key('f', 1002); assert(bt_session.pos == 1 && bt_session.streak == 1);
    bt_key(' ', 1003); assert(bt_session.pos == 2 && bt_session.net == 2);
    bt_key(8, 1004); assert(bt_session.pos == 1 && bt_session.net == 1 && !bt_session.streak);
    assert(bt_session.correct == 2 && bt_session.errors == 1);
    bt_key(' ', 1005); bt_key('F', 1006); bt_key(',', 1007); bt_key('!', 1008);
    assert(bt_session.status == BT_FINISHED && bt_session.net == 5);
    assert(bt_session.correct == 6 && bt_session.attempts == 7 && bt_session.accuracy == 86);
    memcpy(&oldsession, &bt_session, sizeof(oldsession));
    bt_key('x', 3000); bt_tick(6000); bt_finish(9000);
    assert(bt_session.net == oldsession.net && bt_session.elapsed == oldsession.elapsed);
    assert(bt_session.wpm == oldsession.wpm && bt_session.accuracy == oldsession.accuracy);
    /* Empty boundary Backspace never reaches the completed challenge page. */
    bt_start(BT_CHALLENGE, 0, 0, 0, 0); bt_tick(55); fixture("fj");
    bt_key('f', 55); bt_key('j', 55);
    assert(bt_session.completed == 2 && bt_session.pos == 0 && bt_session.net == 2);
    bt_key(8, 55); assert(bt_session.net == 2 && bt_session.completed == 2);
    bt_key(bt_session.passage[0], 55); assert(bt_session.net == 3);
    bt_key(8, 55); assert(bt_session.net == 2 && !bt_session.pos);
}
static void check_time_and_oracle(void)
{
    int i;
    BTWord now;
    bt_init(); bt_start(BT_CHALLENGE, 0, 0, 0, 0);
    bt_tick(54); assert(bt_session.status == BT_READY && bt_session.countdown == 1);
    bt_tick(55); assert(bt_session.status == BT_ACTIVE && !bt_session.elapsed && bt_session.started);
    for (i = 0; i < 10; i++) bt_key('~', 55);
    for (i = 0; i < 150; i++) bt_key(bt_session.passage[bt_session.pos], 55);
    bt_tick(55 + tick_at_seconds(60));
    assert(bt_session.status == BT_FINISHED && bt_session.seconds == 60);
    assert(bt_session.net == 150 && bt_session.correct == 150 && bt_session.errors == 10);
    assert(bt_session.attempts == 160 && bt_session.wpm == 30 && bt_session.accuracy == 94);
    bt_commit();
    assert(bt_store.profile[0].record_wpm[0][0] == 30 && bt_session.new_record);
    assert(bt_session.achievement_new & BT_ACH_25);
    assert(bt_session.achievement_new & BT_ACH_50);
    assert(!(bt_session.achievement_new & BT_ACH_95));
    for (i = 0; i < 3; i++) {
        assert(bt_duration_ticks[i] == tick_at_seconds(bt_duration_seconds[i]));
        bt_start(BT_CHALLENGE, 0, i, 3, 0);
        bt_tick(55 + tick_at_seconds(bt_duration_seconds[i]) - 1);
        assert(bt_session.status == BT_ACTIVE);
        bt_tick(55 + tick_at_seconds(bt_duration_seconds[i]));
        assert(bt_session.status == BT_FINISHED);
        bt_start(BT_CHALLENGE, 0, i, 3, 65000);
        now = (BTWord)(65000 + 55 + tick_at_seconds(bt_duration_seconds[i]) + 100);
        bt_tick(now);
        assert(bt_session.status == BT_FINISHED && bt_session.seconds == bt_duration_seconds[i]);
        assert(!bt_session.remaining && !bt_session.attempts && !bt_session.wpm);
        bt_commit(); assert(!bt_session.new_record);
    }
    bt_start(BT_LESSON, 0, 0, 0, 65530); fixture("ffff");
    bt_key('j', 65534); bt_tick(2); assert(bt_session.elapsed == 4);
    bt_pause(12); assert(bt_session.elapsed == 14 && bt_session.status == BT_PAUSED);
    bt_tick(60000); bt_resume(60000); bt_tick(60010);
    assert(bt_session.elapsed == 24);
    bt_finish(60020); assert(bt_session.elapsed == 34 && bt_session.status == BT_FINISHED);
    bt_start(BT_CHALLENGE, 0, 0, 0, 10); bt_tick(30); bt_pause(30);
    assert(!bt_session.ranked && bt_session.paused_ready && bt_session.ready_ticks == 35);
    bt_resume(3000); bt_tick(3035);
    assert(bt_session.status == BT_ACTIVE && !bt_session.elapsed);
    bt_tick(3036); bt_pause(3037); bt_resume(5000); bt_tick(5001);
    assert(bt_session.elapsed == 3 && !bt_session.ranked);
    bt_finish(5001); bt_commit(); assert(!bt_session.new_record);
    /* Delayed timer spans countdown and active play without losing time. */
    bt_start(BT_CHALLENGE, 0, 0, 0, 0); bt_tick(155); assert(bt_session.elapsed == 100);
    bt_tick(55 + tick_at_seconds(60) - 1); assert(bt_session.status == BT_ACTIVE);
    bt_key('f', 55 + tick_at_seconds(60)); assert(bt_session.status == BT_FINISHED && !bt_session.attempts);
    /* A void pause API must not swallow a deadline discovered in transition. */
    bt_start(BT_CHALLENGE, 0, 0, 0, 0);
    bt_pause(55 + tick_at_seconds(60));
    assert(bt_session.status == BT_FINISHED && (bt_tick(6000) & BT_EV_FINISH));
    bt_commit(); assert(!bt_tick(6001));
}
static void check_visible_countdown(void)
{
    int i;
    BTWord visible, active, deadline;
    for (i = 0; i < 3; i++) {
        bt_init(); bt_start(BT_CHALLENGE, 7, i, 2, 64000);
        memcpy(&oldsession, &bt_session, sizeof(oldsession));
        memcpy(&oldstore, &bt_store, sizeof(oldstore));
        visible = 65520; /* blocking initial paint lasted far longer than3s */
        bt_ready_visible(visible);
        oldsession.last_tick = visible; oldsession.ready_ticks = 55; oldsession.countdown = 3;
        assert(!memcmp(&bt_session, &oldsession, sizeof(oldsession)));
        assert(!memcmp(&bt_store, &oldstore, sizeof(oldstore)));
        active = (BTWord)(visible + 55);
        bt_tick((BTWord)(active - 1));
        assert(bt_session.status == BT_READY && bt_session.ready_ticks == 1 && !bt_session.elapsed);
        bt_tick(active);
        assert(bt_session.status == BT_ACTIVE && !bt_session.elapsed && bt_session.ranked);
        memcpy(&oldsession, &bt_session, sizeof(oldsession));
        bt_ready_visible((BTWord)(active + 500)); /* later paints must not rewind an active run */
        assert(!memcmp(&bt_session, &oldsession, sizeof(oldsession)));
        deadline = (BTWord)(active + tick_at_seconds(bt_duration_seconds[i]));
        bt_tick((BTWord)(deadline - 1)); assert(bt_session.status == BT_ACTIVE);
        bt_tick(deadline);
        assert(bt_session.status == BT_FINISHED && bt_session.seconds == bt_duration_seconds[i]);
        assert(bt_session.elapsed == tick_at_seconds(bt_duration_seconds[i]) && !bt_session.attempts);
        memcpy(&oldsession, &bt_session, sizeof(oldsession));
        bt_ready_visible(12);
        assert(!memcmp(&bt_session, &oldsession, sizeof(oldsession)));
    }
}
static void check_limits_and_profiles(void)
{
    unsigned i;
    BTProfile *p;
    bt_init(); bt_start(BT_PRACTICE, 0, 0, 0, 0);
    bt_key(bt_session.passage[0], 0);
    bt_session.attempts = bt_session.correct = bt_session.net = 65534;
    bt_key(bt_session.passage[bt_session.pos], 0);
    assert(bt_session.attempts == 65535 && bt_session.correct == 65535);
    bt_key('x', 0); assert(bt_session.status == BT_FINISHED && bt_session.net == 65535);
    assert((uint32_t)bt_session.correct + bt_session.errors == bt_session.attempts);
    bt_start(BT_PRACTICE, 0, 0, 0, 0); bt_key('~', 0);
    bt_session.attempts = bt_session.errors = 65534;
    bt_key('~', 0); bt_key('~', 0);
    assert(bt_session.status == BT_FINISHED && bt_session.errors == 65535);
    bt_start(BT_PRACTICE, 0, 0, 0, 0); bt_key('~', 0); bt_tick(65535);
    assert(bt_session.status == BT_FINISHED && bt_session.elapsed == 65535 && bt_session.seconds == 3599);
    assert(bt_muldiv(65535, 12, 1) == 65535);
    bt_init(); bt_store.selected = 1; bt_start(BT_LESSON, 0, 0, 0, 1);
    bt_store.selected = 2; complete(1); bt_commit();
    assert(bt_store.profile[1].lesson_done[0] && !bt_store.profile[2].lesson_done[0]);
    assert(bt_store.profile[1].recent_next == 1 && bt_store.profile[2].recent_next == 0);
    bt_store.selected = 1; p = &bt_store.profile[1];
    for (i = 0; i < BT_KEYS; i++) p->key_attempt[i] = p->key_error[i] = 65535;
    bt_start(BT_LESSON, 0, 0, 0, 1); complete(1); bt_commit();
    for (i = 0; i < BT_KEYS; i++) assert(p->key_error[i] <= p->key_attempt[i]);
}
static void check_practice(void)
{
    BTProfile *p;
    unsigned i, n;
    int k;
    bt_init(); p = &bt_store.profile[0];
    p->key_attempt['a' - 32] = 19; p->key_error['a' - 32] = 19;
    p->key_attempt['f' - 32] = 20; p->key_error['f' - 32] = 3;
    p->key_attempt['j' - 32] = 20000; p->key_error['j' - 32] = 3001;
    p->key_attempt['k' - 32] = 20; p->key_error['k' - 32] = 3;
    assert(bt_weak(0) == 'j' && bt_weak(1) == 'f' && bt_weak(2) == 'k' && !bt_weak(3));
    assert(!bt_weak(-1) && !bt_weak(95));
    bt_start(BT_PRACTICE, -1, 0, 0, 1234);
    strcpy(drillcopy, bt_session.passage);
    for (i = 0; drillcopy[i]; i++) assert(strchr("jfk ", drillcopy[i]));
    bt_start(BT_PRACTICE, -1, 0, 0, 1234); assert(!strcmp(drillcopy, bt_session.passage));
    bt_start(BT_PRACTICE, -1, 0, 0, 1235); assert(strcmp(drillcopy, bt_session.passage));
    for (k = 0; k < BT_DIFFS; k++) {
        bt_start(BT_PRACTICE, 0, 0, k, 1234); n = bt_session.length;
        for (i = 0; i < n; i++) assert(bt_session.passage[i] == ' ' || strchr(bt_groups[k], bt_session.passage[i]));
        complete(1234); bt_commit();
    }
    /* Every duration/difficulty has an independent comparable record. */
    for (i = 0; i < 3; i++) for (k = 0; k < BT_DIFFS; k++) {
        bt_start(BT_CHALLENGE, 0, (int)i, k, 0); bt_tick(55);
        bt_key(bt_session.passage[0], 55); bt_tick(55 + bt_duration_ticks[i]); bt_commit();
        assert(p->record_accuracy[i][k] == 100 && bt_session.new_record);
        assert(bt_session.achievement_new & BT_ACH_95 || p->achievements & BT_ACH_95);
    }
}
static void checksum_again(void)
{
    BTWord sum = bt_checksum(filebuf, BT_FILE_SIZE);
    filebuf[18] = sum & 255; filebuf[19] = sum >> 8;
}
static void expect_bad(unsigned n)
{
    assert(!bt_decode(filebuf, n));
    assert(!memcmp(&bt_store, &oldstore, sizeof(oldstore)));
    assert(!memcmp(&bt_session, &oldsession, sizeof(oldsession)));
}
static void check_codec(void)
{
    unsigned i, j, k, n;
    BTProfile *p;
    bt_init();
    bt_store.voice = 1; /* Accept older v1 saves without changing their layout. */
    bt_store.generation = 65535; bt_store.selected = 3; bt_store.roast = 2;
    bt_store.keyclick = bt_store.display = 1; bt_store.music = 0;
    for (i = 0; i < BT_PROFILES; i++) {
        p = &bt_store.profile[i];
        strcpy(p->name, "TwelveChars!"); p->name[0] = (char)('A' + i);
        for (j = 0; j < BT_LESSONS; j++) { p->lesson_done[j] = 1; p->lesson_best[j] = 80 + j % 21; }
        for (j = 0; j < BT_KEYS; j++) { p->key_attempt[j] = 65535 - j; p->key_error[j] = j; }
        for (j = 0; j < 3; j++) for (k = 0; k < BT_DIFFS; k++) {
            p->record_wpm[j][k] = (BTWord)(31 + j * 10 + k); p->record_accuracy[j][k] = 95 + k;
        }
        p->achievements = 127; p->best_streak = 65535; p->best_wpm = 51;
        p->practice_minutes = 65535; p->baseline_wpm = 42; p->next_lesson = 23;
        for (j = 0; j < 4; j++) {
            p->recent_wpm[j] = 39 + j; p->recent_accuracy[j] = 95 + j;
            p->recent_seconds[j] = (BTWord)(j * 1000); p->recent_mode[j] = j % 3;
        }
        p->recent_next = 3;
    }
    memset(filebuf, 0xA5, sizeof(filebuf));
    assert(!bt_encode(filebuf, BT_FILE_SIZE - 1) && filebuf[0] == 0xA5);
    n = bt_encode(filebuf, BT_SAVE_CAP); assert(n == BT_FILE_SIZE && filebuf[n] == 0xA5);
    memcpy(saved, filebuf, n); memcpy(&oldstore, &bt_store, sizeof(oldstore));
    bt_start(BT_LESSON, 0, 0, 0, 0); bt_key('x', 0);
    memcpy(&oldsession, &bt_session, sizeof(oldsession));
    memset(&bt_store, 0, sizeof(bt_store)); assert(bt_decode(filebuf, n));
    assert(!memcmp(&oldstore, &bt_store, sizeof(oldstore)));
    assert(!memcmp(&oldsession, &bt_session, sizeof(oldsession)));
    for (i = 0; i < n; i++) expect_bad(i);
    expect_bad(n + 1);
    for (i = 0; i < n; i++) {
        filebuf[i] ^= 1; expect_bad(n); filebuf[i] ^= 1;
    }
    /* Integrity-correct hostile fields must still fail semantic validation. */
    static const unsigned offsets[] = {4, 10, 11, 15, 16, 17, 20, 32, 33, 57,
        20 + 465, 20 + 489, 20 + 499, 20 + 508, 20 + 517,
        20 + 524, 20 + 528};
    for (i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
        memcpy(filebuf, saved, n); filebuf[offsets[i]] = 255;
        checksum_again(); expect_bad(n);
    }
    memcpy(filebuf, saved, n); filebuf[21] = 0; /* forbidden hidden name tail */
    checksum_again(); expect_bad(n);
    memcpy(filebuf, saved, n);
    filebuf[20 + 61] = filebuf[20 + 62] = 0; /* zero attempts, nonzero errors */
    filebuf[20 + 251] = 1;
    checksum_again(); expect_bad(n);
    memcpy(filebuf, saved, n); assert(bt_decode(filebuf, n));
    /* Editable name tails are normalized, and exact four-profile rows are stable. */
    bt_store.profile[0].name[1] = 0;
    assert(bt_encode(filebuf, BT_SAVE_CAP) == n && filebuf[22] == 0);
    assert(bt_decode(filebuf, n) && bt_store.profile[0].name[2] == 0);
    memcpy(saved, filebuf, n); assert(bt_encode(filebuf, BT_SAVE_CAP) == n);
    assert(!memcmp(saved, filebuf, n));
}
int main(void)
{
    check_arithmetic(); check_curriculum(); check_input(); check_time_and_oracle(); check_visible_countdown();
    check_limits_and_profiles(); check_practice(); check_codec();
    printf("boratype_host: production engine PASS; store=%zu session=%zu codec=%u bytes\n",
        sizeof(bt_store), sizeof(bt_session), BT_FILE_SIZE);
    return 0;
}
