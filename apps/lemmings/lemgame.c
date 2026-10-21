/* ============================================================================
 * os8088 - apps/lemmings/lemgame.c   (#included by lemmings.c)
 *
 * THE PER-FRAME STEP (SPEC.md 116.4.7): the opening, the clock, spawning, the
 * release rate, the nuke, the objects' own animation and the verdict - the
 * frame-level half of Lemmix's TLemmingGame.InternalUpdate (src/Game.pas:3408)
 * in the order it runs them. The eighteen actions are lemact.c's, the object
 * model lemobj.c's and the drawing lemdraw.c's; nothing in this file draws.
 *
 * ONE GAME FRAME IS SPREAD OVER SEVERAL TICKS WHEN IT HAS TO BE (SPEC.md 116.7,
 * point 2): ovl_frame_begin() does the frame-level work, lem_frame_chunk()
 * steps at most LEM_STEP_MAX lemmings and answers whether the frame is done,
 * and ovl_frame_end() does what follows the lemmings. The bracket's loop polls
 * the input and moves the pointer between chunks, so the pointer is repainted
 * between the chunks of a frame of sixty lemmings and not only once it is
 * stepped - once a glass update, which on the XT is a pass and not a tick
 * (the drawn pass is the long one: SPEC.md 116.7 point 3). Every
 * lemming is still stepped exactly once a frame, in the original's order, so
 * the MECHANICS are the original's whatever the machine - decision 7: the XT
 * plays at whatever rate it measures, and every clock in Lemmings counts game
 * frames, so nothing becomes unwinnable.
 *
 * ATTRIBUTION. Every rule here is the original's, transliterated from Lemmix
 * (Eric Langedijk, Lemmix's own licence - zlib-style, with the added clause
 * 'use it for free, use it for fun') src/Game.pas - Start, IncrementIteration,
 * CheckSpawnLemming, CheckUpdateNuking, UpdateInteractiveObjects,
 * CheckForGameFinished, CheckAdjustReleaseRate - and cross-checked against
 * lemmings_3ds (public domain) src/lemming.c and src/ingame.c. Lemmings is (C)
 * 1991 DMA Design / Psygnosis; README.TXT carries the full list.
 * ==========================================================================*/

/* --- one lemming: 32 bytes, so the pool's stride is a SHIFT (LESSONS.md 3) --
 * x and y are the FOOT, which is what every reference means by a lemming's
 * position; the sprite hangs off it by the action's foot offsets. */
struct lem_l {
    int x, y;
    unsigned char act;              /* LA_* */
    unsigned char frame;
    unsigned char fl;               /* LF_* */
    unsigned char timer;            /* the bomber's countdown, 79 .. 1, 0 off */
    unsigned char fallen;           /* saturating: only > 16 and > 60 matter */
    unsigned char bricks;
    unsigned char fidx;             /* the floater's table index */
    unsigned char below;            /* the object map under the foot */
    unsigned char front;            /* ...and 8 ahead, 8 up */
    /* THE EXPLOSION'S SPRAY (decision 5, SPEC.md 116.8), in bytes the record
     * already had: the 32-byte stride is unchanged and none of it is
     * mechanics - lemsim.py's R4 is Lemmix's, the bomber removed on the
     * explosion's first frame, and the spray is drawn after it at the foot
     * it left (Lemmings.ts ActionExplodingSystem; lemmings_3ds draw.c) */
    unsigned char pf;               /* the spray frame the frame shows, 0
                                     * none: 1 .. LEM_PTIME, one a game frame */
    unsigned char dpf;              /* ...and the one ON THE GLASS (VGA) or in
                                     * the shadow (CGA, Hercules), 0 none */
    unsigned char pad0;
    unsigned char saved[9];         /* the blocker's 3x3 of the object map */
    unsigned char pbox[4];          /* dpf's box: world bytes b0, b1, rows r0,
                                     * r1 - what the next frame's erasure
                                     * covers */
    unsigned char pad1[3];
};

#define LF_RTL     0x01             /* facing left: xDelta = -1 */
#define LF_GONE    0x02             /* removed */
#define LF_CLIMB   0x04
#define LF_FLOAT   0x08
#define LF_BLOCK   0x10             /* the blocker's field is in the map */
#define LF_NEWDIG  0x20             /* digs its first two rows on its first step */
#define LF_EOA     0x40             /* the animation reached its last frame */

/* THE POOL IS THE LEVEL'S OWN COUNT AND NO CAP (decision 7): the DOS levels go
 * to 80 (the most any of the 120 asks for, measured over the set), and 80 is
 * also the original engine's limit. */
#define LEM_MAXLEM   80

static struct lem_l lem_lm[LEM_MAXLEM];
static struct lem_l *L;             /* the lemming being handled */

/* --- one object instance (16 bytes, a shift) ------------------------------ */
struct lem_o {
    int x, y;                       /* Obj.Left, Obj.Top */
    unsigned char id;               /* the GROUND type, 0..15 */
    unsigned char fl;               /* odf_: 1 only on terrain, 2 upside down,
                                     * 4 no overwrite */
    unsigned char frame;            /* CurrentFrame */
    unsigned char trig;             /* Triggered */
    unsigned char ent;              /* 1 = a hatch (type 1) */
    unsigned char drawn;            /* the frame on the glass, 0xFF none */
    unsigned char dirty;            /* its background changed: draw again */
    unsigned char pad[5];
};

#define LEM_MAXOBJ  32
#define LEM_OTYPE_ENTRANCE 1        /* DOS_OBJECT_ID_ENTRANCE - "the game hard-
                                     * codes entrances to always be the first
                                     * object id" after the exit (ccexplore) */

static struct lem_o lem_ob[LEM_MAXOBJ];
static int lem_nob;
static unsigned char lem_ent[4];    /* the hatches, by object slot */
static int lem_nent;
static unsigned char lem_entord[4]; /* DosEntranceOrderTable */
static unsigned char lem_oinfo[16]; /* ObjectInfos index -> object slot, for
                                     * the 16 the map can name */
static int lem_noinfo;              /* ...and how many non-hatch objects */

/* --- the sixteen object TYPES of this level's graphic set (the style bank's
 * object table, LEM_GR_OBJTAB, and the two arrays wave 3 added at 896/912) */
struct lem_ot {
    unsigned char w, h, frames, eff;
    unsigned char anim;             /* GRND animation_flags: 1 triggered,
                                     * 2 continuous, 3 the hatch, 0 none */
    unsigned char start;            /* start_animation_frame_index */
    unsigned char sound;
    unsigned char pad;
    unsigned int off_lo, off_hi;    /* the first frame, from the bank's start */
    unsigned int per;               /* bytes a frame: w * h * 5 / 8 */
    int tl, tt, tw, th;             /* the trigger, in PIXELS, decoded */
    unsigned char pad2[10];         /* 32 bytes: the stride is a shift */
};

static struct lem_ot lem_ty[16];

/* --- the lemmings' animation rows and the stencils (LEMMAIN items 8 and 9) */
struct lem_ar {
    unsigned int off;               /* from the start of item 3 (or 4) */
    unsigned int per;               /* bytes a frame */
    unsigned char w, h, bpp, frames;
};

#define LEM_NANIM 30
#define LEM_NMASK 6
#define LEM_MK_BASHR 0
#define LEM_MK_BASHL 1
#define LEM_MK_MINER 2
#define LEM_MK_MINEL 3
#define LEM_MK_BOOM  4
#define LEM_MK_DIGIT 5              /* the countdown digits, 9 down to 0 */

static struct lem_ar lem_ar[LEM_NANIM];
static struct lem_ar lem_mk[LEM_NMASK];
static unsigned lem_anseg, lem_anoff;   /* item 3's first byte */
static unsigned lem_mkseg, lem_mkoff;   /* item 4's */
static unsigned lem_ptseg, lem_ptoff;   /* item 10's, the spray (wave 6), and
                                         * lem_ptseg 0 when the bank has none */
static int lem_gfx_ok;              /* all three tables were read */


/* --- the level's state (Lemmix TLemmingGame's fields of the same names) --- */
static int lem_iter;                /* CurrentIteration */
static int lem_clockf;              /* fClockFrame, 0..16 */
static int lem_min, lem_sec;
static int lem_maxlem;              /* MaxNumLemmings */
static int lem_save;                /* RescueCount */
static int lem_released, lem_out, lem_saved, lem_removed;
static int lem_inpct;               /* the status line's IN, as Lemmix's two
                                     * SetInfoLemmingsSaved calls leave it
                                     * (lemdraw.c ovl_stat_build) */
static int lem_rr, lem_rrmin;       /* CurrReleaseRate and the level's own */
static int lem_rrdir;               /* -1 slower, 1 faster, 0 neither */
static int lem_cnt[8];              /* the eight skill counts, panel order */
static int lem_skill;               /* the selected button, LB_CLIMBER.. */
static int lem_countdown;           /* NextLemmingCountdown */
static int lem_opened;              /* EntrancesOpened */
static int lem_entdone;             /* fEntranceAnimationCompleted */
static int lem_paused;
static int lem_glitchp;             /* the PAUSE GLITCH's frames still owed */
static int lem_pfin;                /* fParticleFinishTimer: frames the end
                                     * still waits for the last spray */
static int lem_super;               /* the level is a SUPERLEMMING level */
static int lem_nuked;               /* fIsNukedByUser */
static int lem_nukeing;             /* fIsExploderAssignInProgress */
static int lem_nukeix;              /* fIndexOfLemmingToBeNuked */
static int lem_finished;            /* the verdict is in */
static int lem_timeup;
static int lem_cursor;              /* the round robin's next lemming */
static int lem_infr;                /* a frame is part-stepped */

/* the STEP budget (SPEC.md 116.7.1 conclusion 1): a batched probe is 141 us
 * on a 4.77 MHz 8088, so about twenty lemmings fill a tick at full rate. It
 * gates nothing - decision 7 - it only decides how many ticks one frame of a
 * crowded level is spread across, so the pointer moves between chunks. */
#define LEM_STEP_MAX 20

/* lem_snd_cue - one tone a frame, the most important one asked for (SPEC.md
 * 116.11): the speaker has one voice and Lemmix's own queue holds eight, so
 * the frame keeps the cue with the LOWEST event number that arrives first in
 * the table's order of importance - which is the table's order. */
static int lem_sndq = -1;

static void lem_snd_cue(int ev)
{
    if (lem_sndq < 0 || ev < lem_sndq)
        lem_sndq = ev;
}

static void ovl_snd_flush(void)
{
    if (lem_sndq >= 0 && lem_snd)
        os88_snd_tone((int)lem_sndhz[lem_sndq], (int)lem_sndt[lem_sndq], 0x40);
    lem_sndq = -1;
}

/* --- the release rate (Lemmix BtnSlower/BtnFaster/AdjustReleaseRate) ------
 * A press starts the rate moving one a FRAME and a release stops it, which is
 * how a held F1 or a held Slower button behaves in the original. */
static void ovl_rr_adjust(void)
{
    int n;

    if (lem_rrdir == 0)
        return;
    n = lem_rr + lem_rrdir;
    if (n < lem_rrmin)
        n = lem_rrmin;
    if (n > 99)
        n = 99;
    if (n != lem_rr) {
        lem_rr = n;
        ovl_dirty_count(LB_FASTER);
    }
}

/* --- the frame ------------------------------------------------------------ */

/* ovl_increment - IncrementIteration (src/Game.pas:3498), word for word: the
 * clock loses a second every 17 frames and the status line is told on the
 * frame AFTER (fClockFrame = 1), and the opening is three hard-coded frames. */
static void ovl_increment(void)
{
    int i;

    lem_iter++;
    /* THE PAUSE GLITCH (Lemmix TMechanic.PauseGlitch, Dos.Consts.pas:206-208,
     * "#EL 2020: finally"; IncrementIteration :3502-3506): a pause begun
     * before frame 34 holds the clock for 34 - that frame frames once play
     * resumes. Carried as the 1991 game's, as the other two are (SPEC.md
     * 116.4.7) */
    if (lem_glitchp <= 0)
        lem_clockf++;
    else
        lem_glitchp--;
    if (lem_pfin > 0)
        lem_pfin--;                 /* :3508 */
    if (lem_clockf >= LEM_FPS) {
        lem_clockf = 0;
        lem_sec--;
        if (lem_sec < 0) {
            if (lem_min > 0) {
                lem_min--;
                lem_sec = 59;
            } else {
                lem_min = 0;
                lem_sec = 0;
            }
        }
    } else if (lem_clockf == 1) {
        ovl_stat_time();
    }

    if (lem_iter == LEM_IT_LETSGO && !lem_paused)
        lem_snd_cue(SND_LETSGO);
    else if (lem_iter == LEM_IT_DOORSND && !lem_paused)
        lem_snd_cue(SND_ENTRANCE);
    else if (lem_iter == LEM_IT_OPEN) {
        lem_opened = 1;
        for (i = 0; i < lem_nent; i++) {
            lem_ob[(int)lem_ent[i]].trig = 1;
            lem_ob[(int)lem_ent[i]].frame = 1;   /* "#EL 2020 corrected from 2
                                                  * to 1" (:3535) */
        }
    }
}

/* ovl_spawn - CheckSpawnLemming (src/Game.pas:4001). The interval is ccexplore's
 * "(99 - RR) / 2 + 4" with 256 added below zero, the first countdown 20
 * (Start), and the new lemming falls from the hatch's left + 24, top + 14,
 * facing right - in the ABBA order of an original with two hatches. */
static void ovl_spawn(void)
{
    int n, ix;
    struct lem_l *p;

    if (!lem_opened || lem_nuked)
        return;
    lem_countdown--;
    if (lem_countdown != 0)
        return;
    n = 99 - lem_rr;
    if (n < 0)
        n += 256;
    lem_countdown = n / 2 + 4;
    if (lem_released >= lem_maxlem || lem_nent == 0)
        return;
    ix = (int)lem_entord[lem_released & 3];
    p = &lem_lm[lem_released];
    os88_memset(p, 0, sizeof(struct lem_l));
    p->x = lem_ob[(int)lem_ent[ix]].x + 24;
    p->y = lem_ob[(int)lem_ent[ix]].y + 14;
    p->act = LA_FALL;
    p->fallen = 3;                  /* Transition to Falling, FallerStartsWith3 */
    p->below = DOM_NONE;
    p->front = DOM_NONE;
    lem_released++;
    lem_out++;
    lem_dirty_stat();
}

/* ovl_nuke_step - CheckUpdateNuking (:4061): one lemming a frame is given a
 * five-second fuse, in release order, skipping the removed. */
static void ovl_nuke_step(void)
{
    struct lem_l *p;

    if (!lem_nuked || !lem_nukeing)
        return;
    while (lem_nukeix < lem_released && (lem_lm[lem_nukeix].fl & LF_GONE))
        lem_nukeix++;
    if (lem_nukeix > lem_released - 1) {
        lem_nukeing = 0;
        return;
    }
    p = &lem_lm[lem_nukeix];
    if (p->timer == 0 && p->act != LA_SPLAT && p->act != LA_BOMB)
        p->timer = 79;
    lem_nukeix++;
}

/* ovl_finished_check - CheckForGameFinished (:2232): the clock, or every
 * lemming accounted for, or a nuke with nobody left out - BUT NOT WHILE A
 * SPRAY FLIES: `if fParticleFinishTimer > 0 then Exit` (:2236), the timer
 * HandleExploding sets to 52 when ShowParticles is on (:3375-3377; on in
 * Lemmix's TGameOptions DEFAULT, Base.Types.pas:161) and IncrementIteration
 * spends (:3508). So the last bomber's spray is seen whole, and the clock's
 * end waits too - lemmings keep stepping, and can be saved, for those frames
 * (wave 6's second review; lemsim.py carries the same rule). */
static void ovl_finished_check(void)
{
    if (lem_finished || lem_pfin > 0)
        return;
    if (lem_min <= 0 && lem_sec <= 0) {
        lem_timeup = 1;
        lem_finished = 1;
        return;
    }
    if (lem_saved >= lem_maxlem || lem_removed >= lem_maxlem ||
        (lem_nuked && lem_out == 0))
        lem_finished = 1;
}

/* ovl_frame_begin - everything InternalUpdate does before the lemmings move.
 * 0 = no lemming steps this frame (paused, or finished). */
static int ovl_frame_begin(void)
{
    ovl_q_apply();                  /* a press or a nuke that arrived mid-frame:
                                     * Lemmix applies both BETWEEN frames */
    if (lem_finished)
        return 0;
    ovl_finished_check();
    if (lem_finished)
        return 0;
    ovl_rr_adjust();                /* CheckAdjustReleaseRate runs paused too */
    if (lem_paused)
        return 0;
    ovl_increment();
    ovl_spawn();
    lem_cursor = 0;
    lem_infr = 1;
    return 1;
}

/* lem_frame_chunk - CheckLemmings (:4440), at most `n` lemmings of it. 1 =
 * the frame's lemmings are all done. */
static int lem_frame_chunk(int n)
{
    int handled;

    while (lem_cursor < lem_released && n > 0) {
        L = &lem_lm[lem_cursor];
        lem_cursor++;
        if (L->fl & LF_GONE) {
            /* a spray moves on a frame, in the pool's order with the rest
             * (lem_bomb starts it at 1 on the exploder's last frame): 52
             * frames, ParticleTimer's (Game.pas:4452-4455) */
            if (L->pf && ++L->pf > LEM_PTIME)
                L->pf = 0;
            continue;
        }
        n--;
        if (L->timer != 0 && lem_fuse())
            continue;
        handled = lem_handle();
        if (handled)
            lem_objects();
    }
    return lem_cursor >= lem_released;
}

/* ovl_frame_end - and what follows them: the nuke's next fuse, the objects'
 * own animation, the sound. */
static void ovl_frame_end(void)
{
    lem_infr = 0;
    ovl_nuke_step();
    ovl_obj_step();
    ovl_snd_flush();
}

/* --- the player's own controls (Lemmix's Btn*) ----------------------------- */

/* ovl_select_skill - a skill button or F3..F10, which also cues the click. */
static void ovl_select_skill(int b)
{
    if (b < LB_CLIMBER || b > LB_DIGGER || b == lem_skill)
        return;
    ovl_dirty_sel(lem_skill);
    lem_skill = b;
    ovl_dirty_sel(b);
    lem_snd_cue(SND_SKILLBTN);
}

static void ovl_toggle_pause(void)
{
    lem_paused = !lem_paused;
    /* BtnPause (:3941-3958): the glitch is armed once, by the first pause
     * that begins before frame 34 */
    if (lem_paused && lem_iter < 34 && lem_glitchp == 0)
        lem_glitchp = 34 - lem_iter;
}

/* ovl_nuke - BtnNuke (:3987), WITH THE ORIGINAL'S NUKE GLITCH. Lemmix makes
 * it an option (TOptionalMechanic.NukeGlitch, off in its ORIG set), but its
 * own comment at :3990-3992 says what the 1991 game does - "Changing
 * MaxNumLemmings also allows IN % to be calculated and displayed in-game using
 * the glitch calculation, just like the actual game" - and decision 1 is the
 * original and not Lemmix's defaults: MaxNumLemmings becomes the number
 * released so far, so the IN field, the finish test and the postview's
 * "You rescued" all count against it (SetGameResult :4477, SPEC.md 116.8). */
static void ovl_nuke(void)
{
    if (lem_nuked)
        return;
    lem_maxlem = lem_released;
    lem_nuked = 1;
    lem_nukeing = 1;
    lem_nukeix = 0;
}

/* ovl_rr_extreme - Ctrl+F1 / Ctrl+F2, only while paused (Player.pas:476-487). */
static void ovl_rr_extreme(int up)
{
    if (!lem_paused)
        return;
    lem_rr = up ? 99 : lem_rrmin;
    lem_rrdir = 0;
    ovl_dirty_count(LB_FASTER);
}
