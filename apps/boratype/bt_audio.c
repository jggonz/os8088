/* Reactions and audio policy, BORATYPE-SPEC 6; original jokes/music.
 * Included in the app's single C translation unit. No scoring mutation,
 * lesson randomness, file I/O or host-clock waits in event/tick/paint. */
#include "bt_audio.h"
#ifndef BT_HOST
#include "gk.h"
#endif
#define BT_ROAST_GAP 219 /* ceil(12 * 18.2065), wrap-safe unsigned delta */
#define BT_IDLE_GAP 1093
#define BT_CAPTION_TICKS 146
#define BT_MSGS 60
/* Severity: 0 praise/help, 1 gentle, 2 classic, 3 extra roast. */
typedef struct { const char *text; BTByte tag, level, expression; } BTReaction;
static const BTReaction bt_jokes[BT_MSGS] = {
 {"Welcome! Put fingers on F and J.",1,0,0},
 {"The letters have assembled for you.",1,0,0},
 {"Your keyboard awaits its big break.",1,0,3},
 {"Today we make the alphabet proud.",1,0,0},
 {"Three misses. Give fingers a map.",2,1,4},
 {"The target key is still waiting.",2,1,1},
 {"Your fingers have voted to wander.",2,2,6},
 {"This key is getting false visitors.",2,2,3},
 {"You are typing in a secret alphabet.",2,3,5},
 {"The keyboard requests a referee.",2,3,6},
 {"Slow down. Accuracy gets first turn.",3,1,1},
 {"Letters first; speed can wait.",3,1,1},
 {"Your fingers are taking side roads.",3,2,4},
 {"The keys are not a guessing game.",3,2,3},
 {"Your accuracy has left the building.",3,3,6},
 {"Even the space bar looks worried.",3,3,5},
 {"A useful warmup. Try fewer detours.",4,1,1},
 {"This run needs another coat of paint.",4,1,4},
 {"The keyboard has filed a complaint.",4,2,4},
 {"Many letters were harmed in this run.",4,2,6},
 {"A spectacular tour of wrong keys.",4,3,5},
 {"Your score needs a recovery vehicle.",4,3,6},
 {"Backspace is getting quite a workout.",5,1,3},
 {"Forward is also a typing direction.",5,1,1},
 {"You have promoted Backspace to boss.",5,2,3},
 {"Backspace asks for a lunch break.",5,2,4},
 {"Your best key is the undo key.",5,3,5},
 {"This lesson is travelling backwards.",5,3,6},
 {"F and J kept your seats warm.",6,1,1},
 {"The next letter has packed a lunch.",6,1,3},
 {"Are we waiting for keys to ripen?",6,2,3},
 {"The cursor has started a diary.",6,2,4},
 {"Your keyboard is considering retirement.",6,3,5},
 {"The letters have grown beards.",6,3,6},
 {"Ten clean keys! A fine recovery.",7,0,2},
 {"Your fingers found the road again.",7,0,2},
 {"A comeback! The keys forgive you.",7,0,5},
 {"Look! Accuracy is back in fashion.",7,0,3},
 {"Steady hands. Keep that rhythm.",8,0,2},
 {"Twenty-five! That is a proper streak.",8,0,2},
 {"The alphabet is applauding quietly.",8,0,3},
 {"Fifty clean keys. Fingers have talent.",8,0,5},
 {"You have made these letters behave.",9,0,2},
 {"Lesson complete. Take a proud breath.",9,0,0},
 {"Careful work has paid its rent.",9,0,3},
 {"The keyboard sends its compliments.",9,0,2},
 {"A new best! The letters look nervous.",10,0,6},
 {"Your old record has packed its bags.",10,0,5},
 {"Personal best. Frame these fingers.",10,0,0},
 {"A record! We need a bigger trophy.",10,0,2},
 {"Your finger went sightseeing again.",2,2,3},
 {"That key was minding its business.",2,3,5},
 {"The target is the one with the mark.",3,2,3},
 {"You are seasoning the page with errors.",3,3,5},
 {"Next run, invite the correct letters.",4,2,4},
 {"The result printer has asked for mercy.",4,3,6},
 {"Backspace is winning this election.",5,2,3},
 {"You are polishing the previous letter.",5,3,5},
 {"The cursor would like some company.",6,2,1},
 {"We can hear the dust settling.",6,3,5}
};
static BTWord bta_now, bta_spoke, bta_lastkey, bta_idle, bta_caption_at;
static BTWord bta_errorfx, bta_backmark, bta_samplemark, bta_voice_at;
static BTByte bta_voice_have;
static BTByte bta_recent[4], bta_recentpos, bta_pick, bta_have, bta_haderror;
static BTByte bta_low, bta_finished, bta_focus, bta_on, bta_hold, bta_voice;
static int bta_screen, bta_song, bta_stinger, bta_message;
static unsigned bta_pack;
static int bta_packstate; /* 0 missing,1 loaded,2 bad,3 memory,4 PCM missing */
static int bta_lasttag;
static unsigned bta_delta(BTWord a, BTWord b) { return (BTWord)(a-b); }

const char *bt_caption(void)
{
    if (bta_message < 0) return "Put index fingers on F and J.";
    return bt_jokes[bta_message].text;
}
int bt_expression(void)
{
    if (bta_message < 0 || bta_delta(bta_now,bta_caption_at)>BT_CAPTION_TICKS)
        return bta_screen==BT_S_PLAY ? 1 : 0;
    return bt_jokes[bta_message].expression;
}
const char *bt_reaction_history(int back)
{
    int i;
    if (back<0 || back>3) return "";
    i=(bta_recentpos+3-back)&3;
    if (bta_recent[i]>=BT_MSGS) return "";
    return bt_jokes[bta_recent[i]].text;
}
const char *bt_voice_status(void)
{
    if (bt_readonly) return "Other instance owns sound";
    if (!bt_master_sound) return "Master sound muted";
    if (!bt_store.voice) return "Voice disabled";
    if (bta_packstate==2) return "Invalid BORAT.VPK";
    if (bta_packstate==3) return "Voice pack needs memory";
    if (!bta_pack) return "Copy BORAT.VPK beside app";
    if (!bt_vcaps()) return "Voice needs a Sound Blaster";
    if (bta_voice) return "Voice reaction playing";
    return "Local voice pack ready";
}
const char *bt_voice_caption(void) { return bt_vcaption(); }

/* No queue. A reaction that cannot speak now is discarded, not delayed
 * until it interrupts a later passage. Anti-repeat covers every reaction. */
static void bta_react(int tag, int force)
{
    int i,j,n,level,repeated;
    if (!force && bta_have && bta_delta(bta_now,bta_spoke)<BT_ROAST_GAP) return;
    level=bt_store.roast+1;
    if (level>3) level=3;
    for(n=0;n<BT_MSGS;n++) {
        i=(bta_pick+n)%BT_MSGS;
        if (bt_jokes[i].tag!=tag || bt_jokes[i].level>level) continue;
        repeated=0;
        for(j=0;j<4;j++) if(bta_recent[j]==i) repeated=1;
        if(repeated) continue;
        bta_pick=(i+7)%BT_MSGS;
        bta_message=i;
        bta_recent[bta_recentpos]=i;
        bta_recentpos=(bta_recentpos+1)&3;
        bta_spoke=bta_now; bta_have=1; bta_caption_at=bta_now;
        bta_lasttag=tag;
        if(bta_on && bta_focus && bt_store.voice && bta_pack && !bta_voice &&
           (!bta_voice_have || bta_delta(bta_now,bta_voice_at)>=BT_ROAST_GAP)) {
            bta_voice=bt_vplay(tag)==1;
            if(bta_voice) {
                bta_voice_at=bta_now; bta_voice_have=1;
                if(!bta_hold) { bt_mhold(1); bta_hold=1; }
            }
        }
        if(!bta_voice && tag>=BT_R_ERROR && tag<=BT_R_IDLE && bta_on &&
           bta_focus && bt_store.effects) gkm_sfx(10);
        return;
    }
}

int bt_audio_reload(const char *path)
{
    unsigned n;
    bt_vstop(); bta_voice=0;
    bt_vset(0);
    if(bta_pack) { os88_mem_free(bta_pack); bta_pack=0; }
    /* Rounded by the allocator to 512B, base aligned for disk transfers.
     * Pinned by default: the stream's source is immutable and freed at close.
     * One 28KB source plus the driver's 4KB staging grant, no RAM framebuffer. */
    bta_pack=os88_mem_claim_p(1792);
    if(!bta_pack) { bta_packstate=3; return 0; }
    n=os88_file_read_seg(path,bta_pack,28672);
    if(!n) { bta_packstate=(os88_ferr()==1 || os88_ferr()==4)?0:2; }
    else if(bt_vvalidate(bta_pack,n)) { bta_packstate=1; bt_vset(bta_pack); return 1; }
    else bta_packstate=2;
    os88_mem_free(bta_pack); bta_pack=0;
    return 0;
}

void bt_audio_init(void)
{
    int i;
    bta_now=0; bta_have=0; bta_lastkey=0; bta_idle=0;
    bta_recentpos=0; bta_pick=0; bta_message=-1; bta_screen=BT_S_HOME;
    bta_song=-1; bta_stinger=-1; bta_on=0; bta_focus=0; bta_hold=0;
    bta_voice=0; bta_pack=0; bta_packstate=0; bta_voice_have=0;
    bta_errorfx=65530u; bta_backmark=0; bta_samplemark=0;
    bta_low=0; bta_haderror=0; bta_finished=0;
    for(i=0;i<4;i++) bta_recent[i]=255;
    gkm_init(256,257);
    if(!bt_readonly) bt_audio_reload("BORAT.VPK");
    bta_react(BT_R_WELCOME,1);
}
static void bta_fx(int n) { if(bta_on && bta_focus && bt_store.effects) gkm_sfx(n); }
void bt_audio_state(int screen)
{
    if(screen==bta_screen) return;
    if(screen==BT_S_PAUSE) bta_fx(5);
    else if(bta_screen==BT_S_PAUSE) bta_fx(6);
    else bta_fx(0);
    bta_screen=screen;
}
void bt_audio_event(unsigned ev, BTWord now)
{
    bta_now=now;
    if(ev&BT_EV_START) {
        bta_haderror=0; bta_low=0; bta_finished=0;
        bta_lastkey=now; bta_idle=now; bta_backmark=0; bta_samplemark=0;
        bta_fx(1);
    }
    if(ev&(BT_EV_CORRECT|BT_EV_WRONG|BT_EV_BACK)) bta_lastkey=now;
    if(ev&BT_EV_WRONG) {
        bta_haderror=1;
        if(bta_delta(now,bta_errorfx)>=3) { bta_fx(3); bta_errorfx=now; }
        if(bt_session.error_run>=3) bta_react(BT_R_ERROR,0);
    }
    if(ev&BT_EV_BACK) {
        if(bt_session.backspaces>=bta_backmark+8) {
            bta_backmark=bt_session.backspaces;
            bta_react(BT_R_BACK,0);
        }
    }
    if(ev&BT_EV_CORRECT) {
        if(bt_store.keyclick) bta_fx(2);
        if(bta_haderror && bt_session.streak>=10) {
            bta_react(BT_R_RECOVER,0); bta_haderror=0;
        }
    }
    if(ev&BT_EV_STREAK) { bta_fx(4); bta_react(BT_R_STREAK,0); }
    if(bt_session.attempts>=20 && bt_session.accuracy<85) {
        if(!bta_low || bt_session.attempts>=bta_samplemark+20) {
            bta_react(BT_R_LOW,0); bta_samplemark=bt_session.attempts;
        }
        bta_low=1;
    } else if(bt_session.accuracy>=90) bta_low=0;
    if((ev&BT_EV_FINISH) && !bta_finished) {
        bta_finished=1;
        if(bt_session.new_record || (bt_session.achievement_new&BT_ACH_BEST)) {
            bta_stinger=5; bta_fx(9); bta_react(BT_R_RECORD,1);
        } else if(bt_session.accuracy<85 && bt_session.attempts>=20) {
            bta_stinger=4; bta_fx(8); bta_react(BT_R_POOR,0);
        } else { bta_stinger=3; bta_fx(7); bta_react(BT_R_SUCCESS,0); }
        bta_song=-1;
    }
}
void bt_audio_tick(BTWord now, int focused)
{
    int wanted,on,held;
    bta_now=now;
    on=bt_master_sound && !bt_readonly;
    bta_focus=focused && !bt_readonly;
    if(!on && bta_on) {
        bt_mhold(0); bta_hold=0; gkm_done(); bt_vstop();
        bta_voice=0; bta_song=-1;
    }
    bta_on=on;
    if(bta_voice) bta_voice=bt_vtick();
    if((!on || !bt_store.voice || !bta_focus) && bta_voice) {
        bt_vstop(); bta_voice=0;
    }
    held=!bta_focus || bta_screen==BT_S_PAUSE || bt_session.status==BT_PAUSED || bta_voice;
    if(!on) return;
    gkm_volume(bt_store.music,bt_store.effects);
    if(!bt_store.music) { bt_mhold(0); bta_hold=0; bta_song=-1; }
    else {
        if(held!=bta_hold) { bt_mhold(held); bta_hold=held; }
        if(!held) {
            wanted=0;
            if(bta_screen==BT_S_PLAY) wanted=bt_session.mode==BT_CHALLENGE?2:1;
            if(bta_stinger>=0) wanted=bta_stinger;
            if(wanted!=bta_song) {
                bta_song=wanted; gkm_play(wanted,wanted<3);
            } else if(wanted>=3 && gkm_playing()<0) {
                bta_stinger=-1; bta_song=-1;
            }
        }
    }
    if(!held) gkm_tick();
    if(bta_focus && bta_screen==BT_S_PLAY && bt_session.status==BT_ACTIVE &&
       bta_delta(now,bta_lastkey)>=547 && bta_delta(now,bta_idle)>=BT_IDLE_GAP) {
        bta_react(BT_R_IDLE,0); bta_idle=now;
    }
}
void bt_audio_test(int voice)
{
    if(voice) {
        if(bta_on && bta_focus && bt_store.voice && bta_pack) {
            bt_vstop(); bta_voice=bt_vplay(BT_R_WELCOME)==1;
            if(bta_voice && !bta_hold) { bt_mhold(1); bta_hold=1; }
        }
    } else bta_fx(9);
}
void bt_audio_done(void)
{
    bt_mhold(0); bta_hold=0; gkm_done(); bt_vstop(); bt_vset(0);
    if(bta_pack) { os88_mem_free(bta_pack); bta_pack=0; }
    bta_voice=0; bta_on=0;
}
