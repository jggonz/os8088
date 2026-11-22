/* BORATYPE-SPEC 5. Both files are resident before the first scoring event.
 * The UI owns the card and files. The worker is the only running producer. */
static unsigned bt_pcm_music, bt_pcm_fx;
static int bt_pcm_worker, bt_pcm_failed;
static unsigned bt_pcm_retry;
static unsigned char bt_pcm_index[8], bt_pcm_header[64];
static unsigned char bt_pcm_wanted, bt_pcm_clip, bt_pcm_live, bt_pcm_broken;
static unsigned char bt_pcm_cursor[3];
static unsigned bt_pcm_since;
static int bt_pcm_spoken;

int bt_pcm_open(void);
void bt_pcm_close(void);
void bt_pcm_feed(void);
int bt_pcm_caps(void);

static unsigned ovl_bt_le16(unsigned char *p)
{
    return p[0] | ((unsigned)p[1] << 8);
}

static unsigned ovl_bt_bank(const char *name, unsigned size, int music)
{
    unsigned seg, pos, len;
    int i, count;
    count = music ? 1 : 13;
    if (size <= 1088 || size > 65024u) return 0;
    seg = os88_mem_claim_p((size + 15) >> 4);
    if (!seg) return 0;
    if (os88_file_read_seg(name, seg, size) == size && !os88_ferr()) {
        os88_seg_get(bt_pcm_header, seg, 0, 64);
        pos = 1088;
        if (bt_pcm_header[0]=='B' && bt_pcm_header[1]=='T' &&
            bt_pcm_header[2]=='P' && bt_pcm_header[3]=='1' &&
            ovl_bt_le16(bt_pcm_header+4)==8000 && ovl_bt_le16(bt_pcm_header+6)==count &&
            ovl_bt_le16(bt_pcm_header+8)==size && ovl_bt_le16(bt_pcm_header+10)==music) {
            for (i=0; i<count; i++) {
                len = ovl_bt_le16(bt_pcm_header+14+4*i);
                if (!len || ovl_bt_le16(bt_pcm_header+12+4*i)!=pos || len>size-pos) break;
                pos += len;
            }
            if (i==count && pos==size) return seg;
        }
    }
    os88_mem_free(seg);
    return 0;
}

static void ovl_bt_pcm_load(void)
{
    if (bt_readonly || os88_file_read("BTAUDIO.IDX",bt_pcm_index,8)!=8 || os88_ferr()) return;
    if (bt_pcm_index[0]!='B' || bt_pcm_index[1]!='T' ||
        bt_pcm_index[2]!='P' || bt_pcm_index[3]!='1') return;
    bt_pcm_music=ovl_bt_bank("BTMUSIC.BTA",ovl_bt_le16(bt_pcm_index+4),1);
    if (!bt_pcm_music) return;
    bt_pcm_fx=ovl_bt_bank("BTCLIPS.BTA",ovl_bt_le16(bt_pcm_index+6),0);
    if (!bt_pcm_fx) {os88_mem_free(bt_pcm_music); bt_pcm_music=0;}
}

static void ovl_bt_pcm_unload(void)
{
    if (bt_pcm_music) {
        bt_pcm_close();
        os88_mem_free(bt_pcm_fx); os88_mem_free(bt_pcm_music);
        bt_pcm_music=bt_pcm_fx=0;
    }
}

/* Each family advances only when its cue is accepted. Completion and first
 * welcome may replace a current cue; ordinary typing has a two-second gap. */
static void ovl_bt_pcm_cue(int family,unsigned now)
{
    static const unsigned char mistakes[]={2,3,4};
    static const unsigned char praise[]={5,6,8,9};
    static const unsigned char wins[]={12,10,7};
    int id,slot;
    if (!bt_pcm_music || !bt_store.effects || !bt_master_sound) return;
    if (family!=1 && family!=12 && family!=13 && bt_pcm_spoken &&
        (BTWord)(now-bt_pcm_since)<36) return;
    id=family;
    if (family==2 || family==5 || family==12) {
        slot=family==2?0:family==5?1:2;
        id=slot==0?mistakes[bt_pcm_cursor[slot]]:slot==1?praise[bt_pcm_cursor[slot]]:wins[bt_pcm_cursor[slot]];
        if (++bt_pcm_cursor[slot]>=(slot==1?4:3)) bt_pcm_cursor[slot]=0;
    }
    bt_pcm_clip=id; bt_pcm_since=now; bt_pcm_spoken=1;
}

static int ovl_bt_pcm_sync(int enabled, int held, unsigned now)
{
    int flags;
    flags=(bt_store.music?1:0) | (bt_store.effects?2:0);
    if (!enabled || held || !flags || !bt_pcm_worker) {
        bt_pcm_close(); bt_pcm_failed=0; return 0;
    }
    if (bt_pcm_failed && (BTWord)(now-bt_pcm_retry)<36) return 0;
    bt_pcm_wanted=flags;
    if (bt_pcm_broken) bt_pcm_close();
    if (bt_pcm_caps() && (bt_pcm_live || bt_pcm_open())) {bt_pcm_failed=0;return 1;}
    bt_pcm_close();bt_pcm_failed=1;bt_pcm_retry=now;return 0;
}
