/* Production engine, included into the package's single translation unit.
 * All arithmetic state is BTWord, including on the host. No OS, paint,
 * audio, file, allocation, or shared random generator is called here. */
BTStore bt_store;
BTSession bt_session;
static const BTWord bt_duration_seconds[3] = {60, 180, 300};
/* ceil(seconds * 1193182 / 65536), never a rounded 18-Hz approximation. */
static const BTWord bt_duration_ticks[3] = {1093, 3278, 5462};
static const char *bt_groups[BT_DIFFS] = {
    "asdfghjkl;", "abcdefghijklmnopqrstuvwxyz", "0123456789",
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789,.!?;:'\"-"
};
static char bt_practice_keys[BT_KEYS + 1];

/* Exact saturating floor(a*b/divisor), with explicit low/high product.
 * No C expression ever requires a value wider than one target word. */
unsigned bt_muldiv(BTWord a, BTWord b, BTWord divisor)
{
    BTWord hi, lo, mh, ml, old, rem, q, carry;
    int i;
    if (!divisor) return 65535u;
    if (!b) return 0;
    if (a <= 65535u / b) return (a * b) / divisor;
    hi = lo = mh = 0; ml = a;
    for (i = 0; i < 16; i++) {
        if (b & 1) {
            old = lo; lo = (BTWord)(lo + ml);
            hi = (BTWord)(hi + mh + (lo < old));
        }
        mh = (BTWord)((mh << 1) | (ml >> 15));
        ml = (BTWord)(ml << 1); b = (BTWord)(b >> 1);
    }
    if (hi >= divisor) return 65535u;
    rem = hi; q = 0;
    for (i = 0; i < 16; i++) {
        carry = rem >> 15;
        rem = (BTWord)((rem << 1) | (lo >> 15));
        lo = (BTWord)(lo << 1); q = (BTWord)(q << 1);
        if (carry || rem >= divisor) { rem = (BTWord)(rem - divisor); q |= 1; }
    }
    return q;
}

/* Exact floor(ticks*65536/1193182). The high word of ticks*3600 is an
 * upper estimate, less than one above the true quotient. Both high-word
 * products use byte factors, so their partial sums fit a target word.
 * For 0<q<=3599, q*13534 has a nonzero low word (gcd(13534,65536)=2).
 * Hence q*18 + high(q*13534) + 1 is the exact first tick of second q. */
unsigned bt_seconds(BTWord ticks)
{
    BTWord a, b, q, high;
    a = ticks >> 8; b = ticks & 255;
    q = a * 14 + ((a * 16 + b * 14 + (b >> 4)) >> 8);
    a = q >> 8; b = q & 255;
    high = a * 52 + ((a * 222 + b * 52 + ((b * 222) >> 8)) >> 8);
    if (q && ticks < q * 18 + high + 1) q--;
    return q;
}

static void bt_clear(BTByte *p, unsigned n)
{
    while (n) { *p++ = 0; n--; }
}

void bt_init(void)
{
    int i, j;
    static const char bt_default[] = "Player 1";
    bt_clear((BTByte *)&bt_store, sizeof(bt_store));
    bt_clear((BTByte *)&bt_session, sizeof(bt_session));
    for (i = 0; i < BT_PROFILES; i++) {
        for (j = 0; bt_default[j]; j++) bt_store.profile[i].name[j] = bt_default[j];
        bt_store.profile[i].name[7] = '1' + i;
    }
    bt_store.music = bt_store.effects = 1;
    bt_store.voice = 0; /* BTT v1 legacy flag; recordings follow Effects. */
    bt_store.roast = 1;
}

int bt_recommend(void)
{
    int i;
    BTProfile *p;
    p = &bt_store.profile[bt_store.selected < BT_PROFILES ? bt_store.selected : 0];
    for (i = 0; i < BT_LESSONS; i++) if (p->lesson_best[i] < BT_MASTERY) return i;
    return BT_LESSONS - 1;
}

/* Exact fraction comparison by continued fractions. Cross-products would
 * need 32 bits, and rounded percentages would mis-rank close weak keys. */
static int bt_ratio_compare(BTWord a, BTWord b, BTWord c, BTWord d)
{
    BTWord q, r, other;
    int direction;
    direction = 1;
    for (;;) {
        q = a / b; other = c / d;
        if (q != other) return q > other ? direction : -direction;
        r = a % b; other = c % d;
        if (!r || !other) return r == other ? 0 : r ? direction : -direction;
        a = b; b = r; c = d; d = other; direction = -direction;
    }
}

/* Expected-character statistics: repeated errors against F improve the
 * diagnosis of F, irrespective of which wrong key was pressed. Rank ties
 * prefer larger samples, then lower ASCII, and zero-error keys are omitted. */
int bt_weak(int rank)
{
    BTProfile *p;
    BTWord sample;
    int pass, i, best, previous, cmp;
    if (rank < 0 || rank >= BT_KEYS) return 0;
    p = &bt_store.profile[bt_store.selected < BT_PROFILES ? bt_store.selected : 0];
    previous = -1; best = -1;
    for (pass = 0; pass <= rank; pass++) {
        best = -1;
        for (i = 0; i < BT_KEYS; i++) {
            sample = p->key_attempt[i];
            if (sample < 20 || !p->key_error[i]) continue;
            if (previous >= 0) {
                cmp = bt_ratio_compare(p->key_error[i], sample, p->key_error[previous], p->key_attempt[previous]);
                if (cmp > 0 || (!cmp && (sample > p->key_attempt[previous] ||
                    (sample == p->key_attempt[previous] && i <= previous)))) continue;
            }
            if (best < 0) best = i;
            else {
                cmp = bt_ratio_compare(p->key_error[i], sample, p->key_error[best], p->key_attempt[best]);
                if (cmp > 0 || (!cmp && sample > p->key_attempt[best])) best = i;
            }
        }
        if (best < 0) return 0;
        previous = best;
    }
    return best + 32;
}

static BTWord bt_random(void)
{
    BTWord x;
    x = bt_session.seed;
    x ^= (BTWord)(x << 7); x ^= x >> 9; x ^= (BTWord)(x << 8);
    bt_session.seed = x ? x : 1;
    return bt_session.seed;
}

static void bt_fill(void)
{
    int i, n, k;
    const char *keys;
    if (bt_session.mode == BT_LESSON) {
        keys = bt_lessons[bt_session.lesson].drill;
        for (i = 0; keys[i] && i < BT_PASSAGE; i++) bt_session.passage[i] = keys[i];
    } else {
        keys = bt_practice_keys;
        for (n = 0; keys[n]; n++) { }
        i = 0;
        while (i < 159) {
            k = 2 + bt_random() % 6;
            while (k-- && i < 159) bt_session.passage[i++] = keys[bt_random() % n];
            if (i < 159) bt_session.passage[i++] = ' ';
        }
        if (i && bt_session.passage[i - 1] == ' ') i--;
    }
    bt_session.passage[i] = 0; bt_session.length = i; bt_session.pos = 0;
}

void bt_start(int mode, int lesson, int duration, int difficulty, BTWord now)
{
    int i, n, c;
    const char *keys;
    bt_clear((BTByte *)&bt_session, sizeof(bt_session));
    bt_session.mode = mode >= BT_LESSON && mode <= BT_CHALLENGE ? mode : BT_LESSON;
    bt_session.lesson = lesson >= 0 && lesson < BT_LESSONS ? lesson : 0;
    bt_session.duration = duration >= 0 && duration < 3 ? duration : 0;
    bt_session.difficulty = difficulty >= 0 && difficulty < BT_DIFFS ? difficulty : 0;
    bt_session.profile = bt_store.selected < BT_PROFILES ? bt_store.selected : 0;
    bt_session.status = BT_ACTIVE; bt_session.ranked = 1;
    bt_session.last_tick = now;
    /* Starting with identical parameters/time is reproducible, and only
     * this engine consumes this seed. Tests may also assign seed directly. */
    bt_session.seed = (BTWord)(now ^ (bt_session.lesson + 1) * 257 ^
        bt_session.difficulty * 17 ^ 0xB0A7);
    if (!bt_session.seed) bt_session.seed = 1;
    n = 0;
    if (bt_session.mode == BT_PRACTICE && lesson == -1) {
        for (i = 0; i < 5; i++) {
            c = bt_weak(i);
            if (!c) break;
            if (c != ' ') bt_practice_keys[n++] = c;
        }
    }
    if (!n) {
        keys = bt_groups[bt_session.difficulty];
        for (i = 0; keys[i]; i++) bt_practice_keys[n++] = keys[i];
    }
    bt_practice_keys[n] = 0;
    bt_fill();
    if (bt_session.mode == BT_CHALLENGE) {
        bt_session.status = BT_READY; bt_session.ready_ticks = 55;
        bt_session.countdown = 3;
        bt_session.remaining = bt_duration_seconds[bt_session.duration];
    }
}

/* Initial full paints can outlast the ready countdown on an XT. The UI calls
 * this once after the scene is actually visible, before its next clock
 * sample. ready_ticks is a REMAINING count, not an elapsed accumulator. */
void bt_ready_visible(BTWord now)
{
    if (bt_session.status != BT_READY) return;
    bt_session.last_tick = now;
    bt_session.ready_ticks = 55;
    bt_session.countdown = 3;
}

static void bt_metrics(void)
{
    BTWord tenth;
    bt_session.seconds = bt_seconds(bt_session.elapsed);
    bt_session.wpm = bt_session.seconds ? bt_muldiv(bt_session.net, 12, bt_session.seconds) : 0;
    /* Calculate tenths first: no addition of A/2 to a 32-bit product, and
     * rounding cannot overflow even at A=65535u. */
    tenth = bt_session.attempts ? bt_muldiv(bt_session.correct, 1000, bt_session.attempts) : 0;
    bt_session.accuracy = (tenth + 5) / 10;
    if (bt_session.mode == BT_CHALLENGE)
        bt_session.remaining = bt_session.seconds < bt_duration_seconds[bt_session.duration] ?
            bt_duration_seconds[bt_session.duration] - bt_session.seconds : 0;
}

static void bt_freeze(void)
{
    if (bt_session.status == BT_FINISHED || bt_session.status == BT_IDLE) return;
    bt_metrics(); bt_session.status = BT_FINISHED;
    bt_session.countdown = 0; bt_session.events |= BT_EV_FINISH;
}

unsigned bt_tick(BTWord now)
{
    BTWord delta, limit;
    /* A pause/focus transition may discover the deadline through the void
     * pause API. Preserve the result notification until the UI commits it. */
    if (bt_session.status == BT_FINISHED) {
        bt_session.events = bt_session.committed ? 0 : BT_EV_FINISH;
        return bt_session.events;
    }
    bt_session.events = 0;
    if (bt_session.status != BT_ACTIVE && bt_session.status != BT_READY) return 0;
    delta = (BTWord)(now - bt_session.last_tick); bt_session.last_tick = now;
    if (bt_session.status == BT_READY) {
        if (delta < bt_session.ready_ticks) {
            bt_session.ready_ticks -= delta;
            bt_session.countdown = bt_session.ready_ticks > 37 ? 3 : bt_session.ready_ticks > 18 ? 2 : 1;
            if (delta) bt_session.events = BT_EV_TICK;
            return bt_session.events;
        }
        delta -= bt_session.ready_ticks;
        bt_session.ready_ticks = 0; bt_session.countdown = 0;
        bt_session.status = BT_ACTIVE; bt_session.started = 1;
        bt_session.events = BT_EV_START;
    }
    if (!bt_session.started) return bt_session.events;
    limit = bt_session.mode == BT_CHALLENGE ? bt_duration_ticks[bt_session.duration] : 65535u;
    if (delta >= limit - bt_session.elapsed) {
        bt_session.elapsed = limit;
        if (bt_session.mode != BT_CHALLENGE) bt_session.ranked = 0;
        bt_freeze();
    } else {
        bt_session.elapsed += delta;
        if (delta) bt_session.events |= BT_EV_TICK;
        if (delta) bt_metrics();
    }
    return bt_session.events;
}

void bt_finish(BTWord now)
{
    bt_tick(now);
    if (bt_session.status == BT_FINISHED || bt_session.status == BT_IDLE) return;
    if (bt_session.mode == BT_CHALLENGE) bt_session.ranked = 0;
    bt_freeze();
}

unsigned bt_key(int ascii, BTWord now)
{
    unsigned ev;
    int key;
    ev = bt_tick(now);
    if (bt_session.status != BT_ACTIVE) return ev;
    if (ascii == 8) {
        bt_session.streak = 0;
        if (bt_session.backspaces < 65535u) bt_session.backspaces++;
        if (bt_session.pos) { bt_session.pos--; bt_session.net--; }
        bt_session.events |= BT_EV_BACK;
        bt_metrics(); return bt_session.events;
    }
    if (ascii < 32 || ascii > 126) return ev;
    if (bt_session.attempts == 65535u || bt_session.net == 65535u) {
        bt_session.ranked = 0; bt_freeze(); return bt_session.events;
    }
    if (!bt_session.started) {
        bt_session.started = 1; bt_session.last_tick = now;
        bt_session.events |= BT_EV_START;
    }
    key = (BTByte)bt_session.passage[bt_session.pos] - 32;
    bt_session.attempts++; bt_session.key_attempt[key]++;
    if (ascii != (BTByte)bt_session.passage[bt_session.pos]) {
        bt_session.errors++; bt_session.key_error[key]++;
        bt_session.last_wrong = ascii; bt_session.streak = 0;
        if (bt_session.error_run < 255) bt_session.error_run++;
        bt_session.events |= BT_EV_WRONG;
    } else {
        bt_session.correct++; bt_session.pos++; bt_session.net++;
        bt_session.streak++; bt_session.error_run = 0; bt_session.last_wrong = 0;
        if (bt_session.streak > bt_session.best_streak) bt_session.best_streak = bt_session.streak;
        bt_session.events |= BT_EV_CORRECT;
        if (bt_session.streak == 25 || bt_session.streak == 50) bt_session.events |= BT_EV_STREAK;
        if (bt_session.pos == bt_session.length) {
            bt_session.events |= BT_EV_PASSAGE;
            if (bt_session.mode == BT_CHALLENGE) {
                bt_session.completed += bt_session.length; bt_fill();
            } else bt_freeze();
        }
    }
    bt_metrics();
    return bt_session.events;
}

void bt_pause(BTWord now)
{
    if (bt_session.status != BT_ACTIVE && bt_session.status != BT_READY) return;
    bt_tick(now);
    if (bt_session.status == BT_FINISHED) return;
    bt_session.paused_ready = bt_session.status == BT_READY;
    bt_session.status = BT_PAUSED;
    if (bt_session.mode == BT_CHALLENGE) bt_session.ranked = 0;
}

void bt_resume(BTWord now)
{
    if (bt_session.status != BT_PAUSED) return;
    bt_session.last_tick = now;
    bt_session.status = bt_session.paused_ready ? BT_READY : BT_ACTIVE;
}

static BTWord bt_add_sat(BTWord a, BTWord b)
{
    return b > 65535u - a ? 65535u : a + b;
}

void bt_commit(void)
{
    BTProfile *p;
    BTWord a, e, old, award;
    int i, r;
    if (bt_session.status != BT_FINISHED || bt_session.committed) return;
    bt_session.committed = 1; p = &bt_store.profile[bt_session.profile];
    old = p->achievements; award = 0;
    for (i = 0; i < BT_KEYS; i++) {
        a = bt_session.key_attempt[i]; e = bt_session.key_error[i];
        while (a > 65535u - p->key_attempt[i]) {
            p->key_attempt[i] >>= 1; p->key_error[i] >>= 1; a >>= 1; e >>= 1;
        }
        p->key_attempt[i] += a; p->key_error[i] += e;
    }
    if (bt_session.best_streak >= 25) award |= BT_ACH_25;
    if (bt_session.best_streak >= 50) award |= BT_ACH_50;
    if (bt_session.best_streak > p->best_streak) p->best_streak = bt_session.best_streak;
    if (bt_session.mode == BT_LESSON && bt_session.pos == bt_session.length) {
        i = bt_session.lesson; p->lesson_done[i] = 1; award |= BT_ACH_FIRST;
        if (bt_session.accuracy > p->lesson_best[i]) p->lesson_best[i] = bt_session.accuracy;
        if (bt_session.accuracy >= BT_MASTERY) award |= BT_ACH_MASTER;
        if (bt_session.accuracy == 100 && bt_session.attempts) award |= BT_ACH_PERFECT;
    }
    if (bt_session.mode == BT_CHALLENGE && bt_session.ranked && bt_session.attempts) {
        i = bt_session.duration; r = bt_session.difficulty;
        if (bt_session.accuracy >= BT_MASTERY) award |= BT_ACH_95;
        if (bt_session.wpm > p->record_wpm[i][r] || (bt_session.wpm == p->record_wpm[i][r] &&
            bt_session.accuracy > p->record_accuracy[i][r])) {
            p->record_wpm[i][r] = bt_session.wpm;
            p->record_accuracy[i][r] = bt_session.accuracy;
            bt_session.new_record = 1;
        }
    }
    if (bt_session.wpm > p->best_wpm) {
        p->best_wpm = bt_session.wpm; award |= BT_ACH_BEST;
    }
    if (bt_session.attempts && bt_session.seconds) {
        if (!p->baseline_wpm) p->baseline_wpm = bt_session.wpm;
        else if (bt_session.wpm >= p->baseline_wpm) p->baseline_wpm += (bt_session.wpm - p->baseline_wpm) / 4;
        else p->baseline_wpm -= (p->baseline_wpm - bt_session.wpm) / 4;
    }
    p->practice_minutes = bt_add_sat(p->practice_minutes, bt_session.seconds / 60);
    r = p->recent_next;
    p->recent_wpm[r] = bt_session.wpm; p->recent_accuracy[r] = bt_session.accuracy;
    p->recent_seconds[r] = bt_session.seconds; p->recent_mode[r] = bt_session.mode;
    p->recent_next = (r + 1) & 3;
    p->achievements |= award;
    bt_session.achievement_new = p->achievements & ~old;
    /* Recommendation follows the session profile even if another is selected. */
    for (i = 0; i < BT_LESSONS; i++) if (p->lesson_best[i] < BT_MASTERY) break;
    p->next_lesson = i < BT_LESSONS ? i : BT_LESSONS - 1;
}
