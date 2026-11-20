/* BTT1 canonical codec. Fixed 2136-byte image, little-endian words, 20-byte
 * header, Fletcher-16 over the complete image with checksum bytes zero.
 * Scalar loops deliberately avoid structure assignment and native dumps.
 * The staging store is UI-task-only, shared by neither paint nor a worker. */
#define BT_FILE_VERSION 1
#define BT_FILE_SIZE 2136
static BTStore bt_decode_stage;
static unsigned bt_codec_pos;
static BTByte *bt_codec_out;
static const BTByte *bt_codec_in;

static void bt_put_byte(unsigned v) { bt_codec_out[bt_codec_pos++] = v; }
static void bt_put_word(BTWord v) { bt_put_byte(v & 255); bt_put_byte(v >> 8); }
static unsigned bt_get_byte(void) { return bt_codec_in[bt_codec_pos++]; }
static BTWord bt_get_word(void)
{
    BTWord v;
    v = bt_get_byte(); return v | (bt_get_byte() << 8);
}

static BTWord bt_checksum(const BTByte *buf, unsigned n)
{
    BTWord a, b;
    unsigned i;
    a = b = 0;
    for (i = 0; i < n; i++) {
        if (i != 18 && i != 19) a += buf[i];
        if (a >= 255) a -= 255;
        b += a; if (b >= 255) b -= 255;
    }
    return a | (b << 8);
}

/* Every validation also applies before encoding, except name padding:
 * the user's editable name buffer may retain bytes after its first NUL;
 * the serializer always normalizes that tail to zero. */
static int bt_store_valid(const BTStore *s)
{
    const BTProfile *p;
    int i, j, k, ended;
    if (s->selected >= BT_PROFILES || s->music > 1 || s->effects > 1 || s->voice > 1 ||
        s->keyclick > 1 || s->roast > 2 || s->display > 1) return 0;
    for (i = 0; i < BT_PROFILES; i++) {
        p = &s->profile[i]; ended = 0;
        for (j = 0; j < 13; j++) {
            if (!p->name[j]) ended = 1;
            else if (!ended && ((BTByte)p->name[j] < 32 || (BTByte)p->name[j] > 126)) return 0;
        }
        if (!ended || p->next_lesson >= BT_LESSONS || p->recent_next > 3 ||
            (p->achievements & ~127)) return 0;
        for (j = 0; j < BT_LESSONS; j++)
            if (p->lesson_best[j] > 100 || p->lesson_done[j] > 1 ||
                (!p->lesson_done[j] && p->lesson_best[j])) return 0;
        for (j = 0; j < BT_KEYS; j++) if (p->key_error[j] > p->key_attempt[j]) return 0;
        for (j = 0; j < 3; j++) for (k = 0; k < BT_DIFFS; k++)
            if (p->record_accuracy[j][k] > 100) return 0;
        for (j = 0; j < 4; j++) if (p->recent_accuracy[j] > 100 ||
            p->recent_mode[j] > BT_CHALLENGE || p->recent_seconds[j] > 3599) return 0;
    }
    return 1;
}

unsigned ovl_bt_encode(BTByte *buf, unsigned cap)
{
    const BTProfile *p;
    BTWord sum;
    int i, j, k, ended;
    if (cap < BT_FILE_SIZE || !bt_store_valid(&bt_store)) return 0;
    bt_codec_out = buf; bt_codec_pos = 0;
    bt_put_byte('B'); bt_put_byte('T'); bt_put_byte('T'); bt_put_byte('1');
    bt_put_word(BT_FILE_VERSION); bt_put_word(BT_FILE_SIZE); bt_put_word(bt_store.generation);
    bt_put_byte(bt_store.selected); bt_put_byte(bt_store.music); bt_put_byte(bt_store.effects);
    bt_put_byte(bt_store.voice); bt_put_byte(bt_store.keyclick); bt_put_byte(bt_store.roast);
    bt_put_byte(bt_store.display); bt_put_byte(0); bt_put_word(0);
    for (i = 0; i < BT_PROFILES; i++) {
        p = &bt_store.profile[i]; ended = 0;
        for (j = 0; j < 13; j++) {
            if (!p->name[j]) ended = 1;
            bt_put_byte(ended ? 0 : p->name[j]);
        }
        for (j = 0; j < BT_LESSONS; j++) bt_put_byte(p->lesson_best[j]);
        for (j = 0; j < BT_LESSONS; j++) bt_put_byte(p->lesson_done[j]);
        for (j = 0; j < BT_KEYS; j++) bt_put_word(p->key_attempt[j]);
        for (j = 0; j < BT_KEYS; j++) bt_put_word(p->key_error[j]);
        for (j = 0; j < 3; j++) for (k = 0; k < BT_DIFFS; k++) bt_put_word(p->record_wpm[j][k]);
        for (j = 0; j < 3; j++) for (k = 0; k < BT_DIFFS; k++) bt_put_word(p->record_accuracy[j][k]);
        bt_put_word(p->achievements); bt_put_word(p->best_streak); bt_put_word(p->practice_minutes);
        bt_put_word(p->baseline_wpm); bt_put_word(p->best_wpm); bt_put_byte(p->next_lesson);
        for (j = 0; j < 4; j++) bt_put_word(p->recent_wpm[j]);
        for (j = 0; j < 4; j++) bt_put_word(p->recent_accuracy[j]);
        for (j = 0; j < 4; j++) bt_put_word(p->recent_seconds[j]);
        for (j = 0; j < 4; j++) bt_put_byte(p->recent_mode[j]);
        bt_put_byte(p->recent_next);
    }
    if (bt_codec_pos != BT_FILE_SIZE) return 0;
    sum = bt_checksum(buf, BT_FILE_SIZE); buf[18] = sum & 255; buf[19] = sum >> 8;
    return BT_FILE_SIZE;
}

int ovl_bt_decode(const BTByte *buf, unsigned len)
{
    BTProfile *p;
    BTWord sum;
    unsigned n;
    int i, j, k, ended;
    BTByte *dst;
    const BTByte *src;
    if (len != BT_FILE_SIZE || buf[0] != 'B' || buf[1] != 'T' || buf[2] != 'T' || buf[3] != '1' ||
        buf[4] != BT_FILE_VERSION || buf[5] || buf[6] != (BT_FILE_SIZE & 255) ||
        buf[7] != (BT_FILE_SIZE >> 8) || buf[17]) return 0;
    sum = bt_checksum(buf, len);
    if (buf[18] != (sum & 255) || buf[19] != (sum >> 8)) return 0;
    bt_codec_in = buf; bt_codec_pos = 8;
    bt_decode_stage.generation = bt_get_word();
    bt_decode_stage.selected = bt_get_byte(); bt_decode_stage.music = bt_get_byte();
    bt_decode_stage.effects = bt_get_byte(); bt_decode_stage.voice = bt_get_byte();
    bt_decode_stage.keyclick = bt_get_byte(); bt_decode_stage.roast = bt_get_byte();
    bt_decode_stage.display = bt_get_byte(); bt_codec_pos = 20;
    for (i = 0; i < BT_PROFILES; i++) {
        p = &bt_decode_stage.profile[i]; ended = 0;
        for (j = 0; j < 13; j++) {
            p->name[j] = bt_get_byte();
            if (!p->name[j]) ended = 1;
            else if (ended) return 0; /* noncanonical hidden suffix */
        }
        for (j = 0; j < BT_LESSONS; j++) p->lesson_best[j] = bt_get_byte();
        for (j = 0; j < BT_LESSONS; j++) p->lesson_done[j] = bt_get_byte();
        for (j = 0; j < BT_KEYS; j++) p->key_attempt[j] = bt_get_word();
        for (j = 0; j < BT_KEYS; j++) p->key_error[j] = bt_get_word();
        for (j = 0; j < 3; j++) for (k = 0; k < BT_DIFFS; k++) p->record_wpm[j][k] = bt_get_word();
        for (j = 0; j < 3; j++) for (k = 0; k < BT_DIFFS; k++) p->record_accuracy[j][k] = bt_get_word();
        p->achievements = bt_get_word(); p->best_streak = bt_get_word();
        p->practice_minutes = bt_get_word(); p->baseline_wpm = bt_get_word();
        p->best_wpm = bt_get_word(); p->next_lesson = bt_get_byte();
        for (j = 0; j < 4; j++) p->recent_wpm[j] = bt_get_word();
        for (j = 0; j < 4; j++) p->recent_accuracy[j] = bt_get_word();
        for (j = 0; j < 4; j++) p->recent_seconds[j] = bt_get_word();
        for (j = 0; j < 4; j++) p->recent_mode[j] = bt_get_byte();
        p->recent_next = bt_get_byte();
    }
    if (bt_codec_pos != len || !bt_store_valid(&bt_decode_stage)) return 0;
    /* Layout is used only for an in-memory scalar copy after validation;
     * it has no relationship to the file's explicit 529-byte profile rows. */
    src = (const BTByte *)&bt_decode_stage; dst = (BTByte *)&bt_store;
    n = sizeof(bt_store); while (n) { *dst++ = *src++; n--; }
    return 1;
}

/* Startup and boundary saves run on the ordinary UI stack, outside FSX.
 * The cold bodies can live in the eagerly loaded embedded C overlay. */
unsigned bt_encode(BTByte *buf, unsigned cap) { return ovl_bt_encode(buf, cap); }
int bt_decode(const BTByte *buf, unsigned len) { return ovl_bt_decode(buf, len); }
