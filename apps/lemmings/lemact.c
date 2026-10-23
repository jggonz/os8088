/* ============================================================================
 * os8088 - apps/lemmings/lemact.c   (#included by lemmings.c)
 *
 * THE EIGHTEEN ACTIONS, THE EIGHT SKILLS AND THE CURSOR'S PICK (SPEC.md
 * 116.4.7). The hottest C in the program: every live lemming comes through
 * lem_handle() once a game frame, and every terrain probe it makes is a near
 * call into lemmask.inc - 178 us apiece on a 4.77 MHz 8088 (SPEC.md 116.7.1),
 * which is why the handlers below stop probing the moment they have their
 * answer, exactly as the originals' loops do.
 *
 * THE LEMMING BEING HANDLED IS THE STATIC `L` (lemgame.c), not an argument: a
 * SmallerC cdecl argument is a push, a [bp+4] and an add sp on every one of
 * the ~30 calls a handler makes, and there is only ever one lemming in hand.
 *
 * ATTRIBUTION. Transliterated from Lemmix (Eric Langedijk, Lemmix's own
 * licence - zlib-style, with the added clause 'use it for free, use it for fun')
 * src/Game.pas - Transition, TurnAround, HandleLemming and the eighteen
 * Handle*, CheckForInteractiveObjects, the Assign* and PrioritizedHitTest -
 * with ccexplore's mechanics notes as Lemmix quotes them, and cross-checked
 * line for line against lemmings_3ds (public domain) src/lemming.c. Where the
 * two disagree this follows Lemmix, which models the DOS original's own bugs
 * (its ORIG mechanics set: the miner's one-way-right bug, the faller that
 * starts at 3, the climber assigned to a shrugger, objects past the 15th
 * inert). Lemmings is (C) 1991 DMA Design / Psygnosis.
 * ==========================================================================*/

/* xDelta, and the foot offsets of the current animation (FrameTopDy is minus
 * the foot's y, FrameLeftDx minus its x - Lemmix Transition, "ccexplore code
 * compatible") */
#define LDX        ((L->fl & LF_RTL) ? -1 : 1)
#define LTOPDY     (-(int)lem_an[L->act].footy)
#define LLEFTDX    (-(int)lem_an[L->act].footx)

/* lem_hp - HasPixelAt_ClipY: a probe above `miny` reads row `miny` instead. */
static int lem_hp(int x, int y, int miny)
{
    if (y < miny)
        y = miny;
    return lem_has_pixel(x, y);
}

static void lem_remove(void)
{
    L->fl |= LF_GONE;
    lem_out--;
    lem_removed++;
    lem_dirty_stat();
}

/* lem_turn - TurnAround: the animation's other facing, same frame. */
static void lem_turn(void)
{
    L->fl ^= LF_RTL;
}

/* lem_trans - Transition (src/Game.pas:1927). */
static void lem_trans(int act, int turn)
{
    if (L->act == act && !turn)
        return;
    if (turn)
        L->fl ^= LF_RTL;
    if (L->act == act)
        return;
    L->act = (unsigned char)act;
    L->frame = 0;
    L->fl &= ~LF_EOA;
    L->fallen = 0;
    L->bricks = 0;
    switch (act) {
    case LA_SPLAT:
        L->timer = 0;
        lem_snd_cue(SND_SPLAT);
        break;
    case LA_BLOCK:
        L->fl |= LF_BLOCK;
        lem_field(1);
        break;
    case LA_EXIT:
        lem_snd_cue(SND_YIPPEE);
        break;
    case LA_DIG:
        L->fl |= LF_NEWDIG;
        break;
    case LA_FALL:
        L->fallen = 3;              /* FallerStartsWith3 */
        break;
    case LA_BUILD:
        L->bricks = 12;
        break;
    case LA_OHNO:
        if (!lem_nuked)
            lem_snd_cue(SND_OHNO);
        break;
    case LA_BOMB:
        lem_snd_cue(SND_EXPLODE);
        break;
    case LA_FLOAT:
        L->fidx = 0;
        break;
    case LA_MINE:
        L->y++;
        break;
    }
}

/* lem_topcheck - CheckForLevelTopBoundary (:3381). */
static void lem_topcheck(void)
{
    int dy = LTOPDY;

    if (L->y + dy < LEM_HEADMINY) {
        L->y = LEM_HEADMINY - 2 - dy;
        lem_turn();
        if (L->act == LA_JUMP)
            lem_trans(LA_WALK, 0);
    }
}

/* --- the blocker's field (SaveMap / RestoreMap / SetBlockerField, :1873) ---
 * Nine cells of the object map around the lemming, at x - 4, x, x + 4 and
 * y - 6, y - 2, y + 2: the left column pushes left, the right one right, and
 * the middle is the blocker itself. */
static const signed char lem_fdx[3] = { -4, 0, 4 };
static const signed char lem_fdyy[3] = { -6, -2, 2 };
static const unsigned char lem_fval[3] = {
    DOM_FORCELEFT, DOM_BLOCKER, DOM_FORCERIGHT
};

/* lem_field - 1 saves the map and writes the field, 0 puts the map back. */
static void lem_field(int on)
{
    int i, j, k, x, y;

    for (j = 0; j < 3; j++)
        for (i = 0; i < 3; i++) {
            k = j * 3 + i;
            x = L->x + lem_fdx[i];
            y = L->y + lem_fdyy[j];
            if (on) {
                L->saved[k] = (unsigned char)lem_o_get(x, y);
                lem_o_set(x, y, (int)lem_fval[i]);
            } else {
                lem_o_set(x, y, (int)L->saved[k]);
            }
        }
}

/* lem_field_over - CheckForOverlappingField: any of the nine cells already a
 * blocker's. */
static int lem_field_over(void)
{
    int i, j, v;

    for (j = 0; j < 3; j++)
        for (i = 0; i < 3; i++) {
            v = lem_o_get(L->x + lem_fdx[i], L->y + lem_fdyy[j]);
            if (v == DOM_FORCELEFT || v == DOM_BLOCKER || v == DOM_FORCERIGHT)
                return 1;
        }
    return 0;
}

/* --- the destruction (DigOneRow, LayBrick, Apply*Mask) ---------------------
 * Each writes the MASK now and records the rectangle, so lemdraw.c can put the
 * picture right at the end of the frame (SPEC.md 116.4.6: the sprites are on
 * the glass until then, and the background under them is what changes). The
 * MINIMAP's cells go with them, Lemmix's "fake draw mask in fMiniMap". */
static int lem_dig_row(int y)
{
    int hit;

    if (y < 0)
        y = 0;
    hit = lem_m_dig(L->x - 4, y, 9);
    ovl_mm_clear(L->x, L->y, 0);
    if (hit)
        lem_damage(L->x - 4, y, 9, 1);
    return hit;
}

static void lem_lay_brick(void)
{
    int x = (L->fl & LF_RTL) ? L->x - 4 : L->x;

    lem_m_span(x, L->y - 1, 6, 1);
    lem_damage(x, L->y - 1, 6, 1);
}

/* lem_stencil - one of LEMMAIN's destruction masks (item 4) at (x, y). */
static void lem_stencil(int row, int frame, int x, int y)
{
    if (!lem_mask_at(row, frame))
        return;
    lem_m_apply(lem_far_seg, lem_far_off, x, y, (int)lem_mk[row].w,
                (int)lem_mk[row].h);
    lem_damage(x, y, (int)lem_mk[row].w, (int)lem_mk[row].h);
}

/* --- the eighteen handlers (:2826-3380) ----------------------------------- */

static int lem_walk(void)
{
    int dy, ny;

    L->x += LDX;
    if (L->x < LEM_MINX || L->x > LEM_MAXX) {
        lem_turn();
        return 1;
    }
    if (lem_hp(L->x, L->y, 0)) {
        dy = 0;
        ny = L->y;
        while (dy <= 6 && lem_hp(L->x, ny - 1, -dy - 1)) {
            dy++;
            ny--;
        }
        if (dy > 6) {
            if (L->fl & LF_CLIMB)
                lem_trans(LA_CLIMB, 0);
            else
                lem_turn();
            return 1;
        }
        if (dy >= 3) {
            lem_trans(LA_JUMP, 0);
            ny = L->y - 2;
        }
        L->y = ny;
        lem_topcheck();
        return 1;
    }
    dy = 1;
    while (dy <= 3) {
        L->y++;
        if (lem_hp(L->x, L->y, dy))
            break;
        dy++;
    }
    if (dy > 3) {
        L->y++;
        lem_trans(LA_FALL, 0);
    }
    if (L->y > LEM_MAXY) {
        lem_remove();
        return 0;
    }
    return 1;
}

static int lem_jump(void)
{
    int dy = 0;

    while (dy < 2 && lem_hp(L->x, L->y - 1, -dy - 1)) {
        dy++;
        L->y--;
    }
    if (dy < 2)
        lem_trans(LA_WALK, 0);
    lem_topcheck();
    return 1;
}

static int lem_dig(void)
{
    int y;

    if (L->fl & LF_NEWDIG) {
        lem_dig_row(L->y - 2);
        lem_dig_row(L->y - 1);
        L->fl &= ~LF_NEWDIG;
    } else {
        L->frame++;
        if (L->frame >= 16)
            L->frame -= 16;
    }
    if (L->frame != 0 && L->frame != 8)
        return 0;
    y = L->y;
    L->y++;
    if (L->y > LEM_MAXY) {
        lem_remove();
        return 0;
    }
    if (!lem_dig_row(y)) {
        lem_trans(LA_FALL, 0);
    } else if (lem_o_get(L->x, L->y) == DOM_STEEL) {
        lem_snd_cue(SND_STEEL);
        lem_trans(LA_WALK, 0);
    }
    return 1;
}

static int lem_climb(void)
{
    if (L->frame <= 3) {
        if (!lem_hp(L->x, L->y - 7 - (int)L->frame, 0)) {
            L->y = L->y - (int)L->frame + 2;
            lem_trans(LA_HOIST, 0);
            lem_topcheck();
        }
        return 1;
    }
    L->y--;
    if (L->y + LTOPDY < LEM_HEADMINY ||
        lem_hp(L->x - LDX, L->y - 8, -8)) {
        lem_trans(LA_FALL, 1);
        L->x += LDX * 2;
    }
    return 1;
}

static int lem_drown(void)
{
    if (L->fl & LF_EOA)
        lem_remove();
    else if (!lem_has_pixel(L->x + 8 * LDX, L->y))
        L->x += LDX;
    return 0;
}

static int lem_hoist(void)
{
    if (L->frame <= 4) {
        L->y -= 2;
        lem_topcheck();
        return 1;
    }
    if (L->fl & LF_EOA) {
        lem_trans(LA_WALK, 0);
        lem_topcheck();
        return 1;
    }
    return 0;
}

static int lem_build(void)
{
    if (L->frame == 10 && L->bricks <= 3)
        lem_snd_cue(SND_BRICKS);
    if (L->frame == 9 || (L->frame == 10 && L->bricks == 9)) {
        lem_lay_brick();
        return 0;
    }
    if (L->frame != 0)
        return 1;
    L->x += LDX;
    L->y--;
    if (L->x <= LEM_MINX || L->x > LEM_MAXX ||
        lem_hp(L->x, L->y - 1, -1)) {
        lem_trans(LA_WALK, 1);
        lem_topcheck();
        return 1;
    }
    L->x += LDX;
    if (lem_hp(L->x, L->y - 1, -1)) {
        lem_trans(LA_WALK, 1);
        lem_topcheck();
        return 1;
    }
    L->bricks--;
    if (L->bricks == 0) {
        lem_trans(LA_SHRUG, 0);
        lem_topcheck();
        return 1;
    }
    if (lem_hp(L->x + LDX * 2, L->y - 9, -9) || L->x <= LEM_MINX ||
        L->x > LEM_MAXX) {
        lem_trans(LA_WALK, 1);
        lem_topcheck();
        return 1;
    }
    /* "if builder too high he becomes a walker. will *not* turn around" */
    if (L->y + LTOPDY < LEM_HEADMINY) {
        lem_trans(LA_WALK, 0);
        lem_topcheck();
    }
    return 1;
}

/* the object a wall-cutter is about to cut into stops it: steel, or a one-way
 * wall pointing the other way */
static int lem_stopped(int obj)
{
    if (obj == DOM_STEEL)
        return 1;
    if (obj == DOM_ONEWAYLEFT && LDX != -1)
        return 1;
    if (obj == DOM_ONEWAYRIGHT && LDX != 1)
        return 1;
    return 0;
}

static int lem_bash(void)
{
    int index, dy, n, x, y, front;

    index = L->frame;
    if (index >= 16)
        index -= 16;
    if (index >= 11 && index <= 15) {
        L->x += LDX;
        if (L->x < LEM_MINX || L->x > LEM_MAXX) {
            lem_trans(LA_WALK, 1);
            return 1;
        }
        dy = 0;
        while (dy < 3 && !lem_hp(L->x, L->y, dy)) {
            dy++;
            L->y++;
        }
        if (dy == 3) {
            lem_trans(LA_FALL, 0);
            return 1;
        }
        front = lem_o_get(L->x + LDX * 8, L->y - 8);
        if (front == DOM_STEEL)
            lem_snd_cue(SND_STEEL);
        if (lem_stopped(front))
            lem_trans(LA_WALK, 1);
        return 1;
    }
    if (index >= 2 && index <= 5) {
        lem_stencil((L->fl & LF_RTL) ? LEM_MK_BASHL : LEM_MK_BASHR, index - 2,
                    L->x + LLEFTDX, L->y + LTOPDY);
        ovl_mm_clear(L->x, L->y, 0);
        if (L->frame == 5) {
            n = 0;
            x = L->x + LDX * 8;
            y = L->y - 6;
            while (n < 4 && !lem_has_pixel(x, y)) {
                n++;
                x += LDX;
            }
            if (n == 4)
                lem_trans(LA_WALK, 0);
        }
    }
    return 0;
}

static int lem_mine(void)
{
    int below, row;

    row = (L->fl & LF_RTL) ? LEM_MK_MINEL : LEM_MK_MINER;
    if (L->frame == 1) {
        lem_stencil(row, 0, L->x + LLEFTDX, L->y + LTOPDY);
        ovl_mm_clear(L->x, L->y, 0);
        return 0;
    }
    if (L->frame == 2) {
        lem_stencil(row, 1, L->x + LDX + LLEFTDX, L->y + 1 + LTOPDY);
        ovl_mm_clear(L->x, L->y, 0);
        return 0;
    }
    if (L->frame == 3 || L->frame == 15) {
        L->x += LDX;
        if (L->x < LEM_MINX || L->x > LEM_MAXX) {
            lem_trans(LA_WALK, 1);
            return 1;
        }
        L->x += LDX;
        if (L->x < LEM_MINX || L->x > LEM_MAXX) {
            lem_trans(LA_WALK, 1);
            return 1;
        }
        if (L->frame == 3) {
            L->y++;
            if (L->y > LEM_MAXY) {
                lem_remove();
                return 0;
            }
        }
        if (!lem_hp(L->x, L->y, 0)) {
            lem_trans(LA_FALL, 0);
            return 1;
        }
        below = lem_o_get(L->x, L->y);
        if (below == DOM_STEEL)
            lem_snd_cue(SND_STEEL);
        /* MinerOneWayRightBug - "a one-way wall pointing right being
         * unminable in either direction" is the DOS original's own bug */
        if (below == DOM_STEEL || (below == DOM_ONEWAYLEFT && LDX != -1) ||
            below == DOM_ONEWAYRIGHT)
            lem_trans(LA_WALK, 1);
        return 1;
    }
    if (L->frame == 0) {
        L->y++;
        if (L->y > LEM_MAXY) {
            lem_remove();
            return 0;
        }
        return 1;
    }
    return 0;
}

static int lem_fall(void)
{
    int dy;

    if (L->fallen > 16 && (L->fl & LF_FLOAT)) {
        lem_trans(LA_FLOAT, 0);
        return 1;
    }
    dy = 0;
    while (dy < 3 && !lem_hp(L->x, L->y, dy)) {
        dy++;
        L->y++;
        if (L->y > LEM_MAXY) {
            lem_remove();
            return 0;
        }
    }
    if (dy == 3) {
        if (L->fallen < 250)
            L->fallen += 3;
        return 1;
    }
    /* "the return true after SetToSplattering is actually correct ... the bug
     * in DOS Lemmings that enables the direct drop to exit" (ccexplore) */
    if (L->fallen > LEM_MAXFALL)
        lem_trans(LA_SPLAT, 0);
    else
        lem_trans(LA_WALK, 0);
    return 1;
}

static int lem_float(void)
{
    int dy, miny, i;

    i = L->fidx;
    L->frame = lem_ffr[i];
    dy = lem_fdy[i];
    L->fidx++;
    if (L->fidx >= 16)
        L->fidx = 8;
    if (dy <= 0) {
        L->y += dy;
    } else {
        miny = 0;
        while (dy > 0) {
            if (lem_hp(L->x, L->y, miny)) {
                lem_trans(LA_WALK, 0);
                return 1;
            }
            L->y++;
            dy--;
            miny++;
        }
    }
    if (L->y > LEM_MAXY) {
        lem_remove();
        return 0;
    }
    return 1;
}

static int lem_block(void)
{
    if (!lem_hp(L->x, L->y, 0)) {
        lem_trans(LA_WALK, 0);
        L->fl &= ~LF_BLOCK;
        lem_field(0);
    }
    return 0;
}

static int lem_ohno(void)
{
    int dy;

    if (L->fl & LF_EOA) {
        lem_trans(LA_BOMB, 0);
        return 0;
    }
    dy = 0;
    while (dy < 3 && !lem_hp(L->x, L->y, dy)) {
        dy++;
        L->y++;
    }
    if (L->y > LEM_MAXY) {
        lem_remove();
        return 0;
    }
    return 1;
}

static int lem_bomb(void)
{
    int o;

    if (!(L->fl & LF_EOA))
        return 0;
    if (L->fl & LF_BLOCK) {
        L->fl &= ~LF_BLOCK;
        lem_field(0);
    }
    o = lem_o_get(L->x, L->y);
    if (o != DOM_STEEL && o != DOM_WATER) {
        lem_stencil(LEM_MK_BOOM, 0, L->x - 8, L->y - 14);
        ovl_mm_clear(L->x, L->y, 1);
    }
    lem_remove();
    lem_pfin = LEM_PTIME;           /* the end waits for it (Game.pas:3377) */
    L->pf = 1;                      /* the spray's first frame is this one's
                                     * (Lemmings.ts: frame 0 the fireball,
                                     * then particles 0..50), wherever it went
                                     * off - steel and water keep their
                                     * terrain, not their sparks */
    return 0;
}

/* lem_handle - HandleLemming (:2795): the next frame of the animation (the
 * floater and the digger count their own), then the action. 1 = the objects
 * under it are checked. */
static int lem_handle(void)
{
    const struct lem_anim *a = &lem_an[L->act];

    if (L->act != LA_FLOAT && L->act != LA_DIG) {
        if (L->frame < a->frames - 1) {
            L->fl &= ~LF_EOA;
            L->frame++;
        } else {
            L->fl |= LF_EOA;
            if (a->loop)
                L->frame = 0;
        }
    }
    switch (L->act) {
    case LA_WALK:  return lem_walk();
    case LA_JUMP:  return lem_jump();
    case LA_DIG:   return lem_dig();
    case LA_CLIMB: return lem_climb();
    case LA_DROWN: return lem_drown();
    case LA_HOIST: return lem_hoist();
    case LA_BUILD: return lem_build();
    case LA_BASH:  return lem_bash();
    case LA_MINE:  return lem_mine();
    case LA_FALL:  return lem_fall();
    case LA_FLOAT: return lem_float();
    case LA_BLOCK: return lem_block();
    case LA_OHNO:  return lem_ohno();
    case LA_BOMB:  return lem_bomb();
    case LA_SHRUG:
        if (L->fl & LF_EOA) {
            lem_trans(LA_WALK, 0);
            return 1;
        }
        return 0;
    case LA_EXIT:
        if (L->fl & LF_EOA) {
            lem_remove();
            lem_saved++;
            /* HandleExiting (:3308): the status line's IN is set HERE */
            lem_inpct = (lem_saved * 100) / lem_maxlem;
        }
        return 0;
    default:                        /* LA_SPLAT, LA_FRY */
        if (L->fl & LF_EOA)
            lem_remove();
        return 0;
    }
}

/* lem_fuse - UpdateExplosionTimer (:2218). 1 = it went off this frame, and
 * the lemming does nothing else. */
static int lem_fuse(void)
{
    L->timer--;
    if (L->timer > 0)
        return 0;
    if (L->act == LA_FRY || L->act == LA_DROWN || L->act == LA_FLOAT ||
        L->act == LA_FALL)
        lem_trans(LA_BOMB, 0);
    else
        lem_trans(LA_OHNO, 0);
    return 1;
}

/* lem_objects - CheckForInteractiveObjects (:2342). */
static void lem_objects(void)
{
    int below;

    below = lem_o_get(L->x, L->y);
    L->below = (unsigned char)below;
    L->front = (unsigned char)lem_o_get(L->x + 8 * LDX, L->y - 8);
    if (below == DOM_NONE)
        return;
    if (below < 128) {
        lem_trap(below);
        return;
    }
    switch (below) {
    case DOM_EXIT:
        if (L->act != LA_FALL) {
            lem_trans(LA_EXIT, 0);
            lem_snd_cue(SND_YIPPEE);
        }
        break;
    case DOM_FORCELEFT:
        if (LDX > 0)
            lem_turn();
        break;
    case DOM_FORCERIGHT:
        if (LDX < 0)
            lem_turn();
        break;
    case DOM_WATER:
        lem_trans(LA_DROWN, 0);
        lem_snd_cue(SND_DROWN);
        break;
    case DOM_FIRE:
        lem_trans(LA_FRY, 0);
        lem_snd_cue(SND_FRY);
        break;
    }
}

/* --- the skills (AssignClimber .. AssignDigger, :2039-2216) ---------------
 * `L` is Lemming1, `l2` the hit test's non-prioritised Lemming2 or 0. 1 = the
 * skill was given (to L or to l2), and the count goes down. */
static int ovl_assign_to(int b, struct lem_l *l1, struct lem_l *l2)
{
    int flags, need;

    L = l1;
    flags = lem_an[L->act].flags;
    switch (b) {
    case LB_CLIMBER:
        if ((L->fl & LF_CLIMB) || (flags & LAF_NOCF))
            return 0;
        L->fl |= LF_CLIMB;
        /* AssignClimberShruggerActionBug, "take this bug literally" */
        if (L->act == LA_SHRUG)
            L->act = LA_WALK;
        return 1;
    case LB_UMBRELLA:
        if ((L->fl & LF_FLOAT) || (flags & LAF_NOCF))
            return 0;
        L->fl |= LF_FLOAT;
        return 1;
    case LB_EXPLODE:
        if (L->timer != 0 || (flags & LAF_NOBMB))
            return 0;
        L->timer = 79;
        return 1;
    case LB_BLOCKER:
        if (!(flags & LAF_BLK) || lem_field_over())
            return 0;
        lem_trans(LA_BLOCK, 0);
        return 1;
    case LB_BUILDER:
        if (L->y + LTOPDY < LEM_HEADMINY)
            return 0;
        need = LAF_BLD;
        break;
    case LB_BASHER:
        need = LAF_BSH;
        break;
    case LB_MINER:
        need = LAF_MIN;
        break;
    default:                        /* LB_DIGGER */
        if (L->below == DOM_STEEL)
            return 0;
        need = LAF_DIG;
        break;
    }
    /* the four that may fall through to Lemming2 */
    if (!(flags & need)) {
        if (l2 == 0)
            return 0;
        L = l2;
        if (!(lem_an[L->act].flags & need))
            return 0;
    }
    if (b == LB_BASHER || b == LB_MINER) {
        if (L->front == DOM_STEEL) {
            lem_snd_cue(SND_STEEL);
            return 0;
        }
        if (b == LB_MINER && L->below == DOM_STEEL)
            return 0;
        if ((L->front == DOM_ONEWAYLEFT && LDX != -1) ||
            (L->front == DOM_ONEWAYRIGHT && LDX != 1))
            return 0;
    }
    lem_trans(b == LB_BUILDER ? LA_BUILD : b == LB_BASHER ? LA_BASH
              : b == LB_MINER ? LA_MINE : LA_DIG, 0);
    return 1;
}

/* ovl_assign - ProcessSkillAssignment (:3672) + AssignSkill: the selected
 * skill to whatever the cursor's prioritised hit test found, if there is a
 * count left - WITH THE ORIGINAL'S RIGHT-CLICK GLITCH (:3685-3689). The right
 * button held over nothing but working lemmings leaves Lemming1 nil, and the
 * 1991 game then assigns to the LAST non-prioritised lemming any hit test saw,
 * wherever it is now. Lemmix makes that an option outside its ORIG set;
 * decision 1 is the original, and SPEC.md 116.8 carries the glitches levels
 * are solved with. 1 = something was assigned. */
static int lem_hit_l1, lem_hit_l2;  /* the hit test's two answers, or -1 */
static int lem_hit_n;               /* ...and how many it found */
static int lem_hit_last = -1;       /* fLastNonPrioritizedLemming, or -1 */

static int ovl_assign(void)
{
    int k, l1;
    struct lem_l *l2;

    if (lem_hit_n == 0)
        return 0;
    l1 = lem_hit_l1 >= 0 ? lem_hit_l1 : lem_hit_last;
    if (l1 < 0)
        return 0;
    k = lem_skill - LB_CLIMBER;
    if (k < 0 || k > 7 || lem_cnt[k] <= 0)
        return 0;
    l2 = (lem_hit_l2 >= 0) ? &lem_lm[lem_hit_l2] : 0;
    if (!ovl_assign_to(lem_skill, &lem_lm[l1], l2))
        return 0;
    lem_cnt[k]--;
    ovl_dirty_count(lem_skill);
    lem_snd_cue(SND_ASSIGN);
    return 1;
}

/* ovl_hittest - PrioritizedHitTest (:3595) at WORLD (cx, cy), which is the
 * pointer's hotspot moved to the game's own (Player.pas:590, "4,9 instead of
 * 7,7"). A lemming is hit when the point is inside the 13x13 box hung off its
 * frame's top left (x + FrameLeftDx, y + FrameTopDy); the last prioritised
 * one wins unless the right button is down.
 *
 * `rec`: this call is one Lemmix makes, so it RECORDS the last non-prioritised
 * lemming for the right-click glitch (:3627). Lemmix calls it from three
 * places and no others - HitTest at the end of UpdateLemmings (:2646, a frame
 * boundary), HitTest from Img_MouseMove while paused (Player.pas:654), and
 * ProcessSkillAssignment's own (:3683). The port's pointer also asks on every
 * TICK, between frames and in the middle of a part-stepped one, to keep the
 * status word and the pointer's shape current; those calls pass 0, or the
 * glitch could assign to a walker Lemmix's own hit tests never saw (wave 3's
 * second review). */
static void ovl_hittest(int cx, int cy, int right, int rec)
{
    int i, x, y, prio, norm;
    struct lem_l *p;

    prio = -1;
    norm = -1;
    lem_hit_n = 0;
    for (i = 0; i < lem_released; i++) {
        p = &lem_lm[i];
        if (p->fl & LF_GONE)
            continue;
        x = p->x - (int)lem_an[p->act].footx;
        y = p->y - (int)lem_an[p->act].footy;
        if (x <= cx && cx <= x + 12 && y <= cy && cy <= y + 12) {
            lem_hit_n++;
            if (lem_an[p->act].flags & LAF_PRIO)
                prio = i;
            else
                norm = i;
        }
    }
    if (norm >= 0 && rec)
        lem_hit_last = norm;        /* "save for RightClickGlitch emulation" */
    lem_hit_l1 = (right || prio < 0) ? norm : prio;
    lem_hit_l2 = norm;
}

/* ovl_word - the status line's word for lemming i, in Lemmix's test order
 * (src/Game.pas:3645-3653): climber and floater, climber, floater, else the
 * action. Uppercase in the band, because the panel's font has no lowercase. */
static int ovl_word(int i)
{
    struct lem_l *p = &lem_lm[i];

    if ((p->fl & LF_CLIMB) && (p->fl & LF_FLOAT))
        return LEMS_LEM_ATHLETE;
    if (p->fl & LF_CLIMB)
        return LEMS_LEM_CLIMBER;
    if (p->fl & LF_FLOAT)
        return LEMS_LEM_FLOATER;
    return (int)lem_an[p->act].word;
}
