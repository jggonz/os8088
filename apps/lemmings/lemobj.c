/* ============================================================================
 * os8088 - apps/lemmings/lemobj.c   (#included by lemmings.c)
 *
 * THE INTERACTIVE OBJECTS AND THE FRAMES EVERYTHING IS DRAWN FROM (SPEC.md
 * 114.4.7): the level's up-to-32 objects, the four hatches among them, the
 * object map's traps, the objects' own animation, and the three registration
 * tables - the lemmings' 30 animation rows, the six destruction stencils and
 * the sixteen object types - read once a level into bss so a frame never has
 * to walk a bank header to find a bitmap.
 *
 * THE TRIGGER LOOKUP IS THE OBJECT MAP and not a list of rectangles: the plan
 * proposed a per-column bucket of trigger rectangles because the reference
 * walks every trigger for every lemming every tick, but the DOS original
 * itself keeps a 4-pixel-resolution MAP - Lemmix ReadObjectMap, lemmings_3ds
 * object_map[] - and that is one byte read a lemming a probe, cheaper than any
 * bucket walk and exact by construction (blockers write into it too). It lives
 * in the claim the level's raw record was read into (lemmask.inc's lem_o_*).
 *
 * ATTRIBUTION. Transliterated from Lemmix (Eric Langedijk, Lemmix's own
 * licence - zlib-style, with an added clause) src/Game.pas
 * Start, InitializeObjectMap, UpdateInteractiveObjects and DrawAnimatedObjects,
 * and src/Level.Loader.pas TranslateLevel for the record's object and steel
 * fields; the GROUND object fields are ccexplore's document in
 * lemmings_3ds/doc/data/. Lemmings is (C) 1991 DMA Design / Psygnosis.
 * ==========================================================================*/

/* THE DATA - the instances, the types, the registration rows - is declared in
 * lemgame.c with the rest of the level's state, because lemgame.c's frame
 * reads it and one translation unit wants every variable declared before its
 * first use. What is here is what is DONE with it. */

/* lem_far_add - lem_far_seg/off advanced by `n`, renormalised (it was
 * ovl_far_add, and a frame now needs it every time it draws) */
static void lem_far_add(unsigned n)
{
    unsigned o = lem_far_off + n;

    lem_far_seg += o >> 4;
    lem_far_off = o & 15;
}

/* lem_far_mul - ...by `n` times `per`, without a 32-bit product: the step is
 * taken `n` times, and n is a frame number (at most 37 in the five sets). */
static void lem_far_mul(unsigned n, unsigned per)
{
    while (n > 0) {
        lem_far_add(per);
        n--;
    }
}

/* lem_anim_at - frame `f` of the action's animation for lemming p, into
 * lem_far_seg/off. 0 = the bank does not carry it. */
static int lem_anim_at(int act, int rtl, int f)
{
    int row;

    if (!lem_gfx_ok)
        return 0;
    row = rtl ? lem_an[act].rowl : lem_an[act].rowr;
    if (lem_ar[row].w == 0)
        return 0;                   /* refused by ovl_types' bound */
    lem_far_seg = lem_anseg;
    lem_far_off = lem_anoff;
    lem_far_add(lem_ar[row].off);
    lem_far_mul((unsigned)f, lem_ar[row].per);
    return 1;
}

/* lem_mask_at - frame `f` of stencil row `row`. */
static int lem_mask_at(int row, int f)
{
    if (!lem_gfx_ok || lem_mk[row].w == 0)
        return 0;
    lem_far_seg = lem_mkseg;
    lem_far_off = lem_mkoff;
    lem_far_add(lem_mk[row].off);
    lem_far_mul((unsigned)f, lem_mk[row].per);
    return 1;
}

/* lem_trap - a lemming walked into triggered trap `i` (an ObjectInfos index):
 * if it is not already running it starts, the lemming is gone - "the trap
 * animation graphics actually includes the lemming itself" (ccexplore) - and
 * the trap's own sound plays. TriggeredTrapLemmixBugSolved: it starts at 0. */
static void lem_trap(int i)
{
    struct lem_o *o;

    if (i > 15)
        return;
    o = &lem_ob[(int)lem_oinfo[i]];
    if (o->trig)
        return;
    o->trig = 1;
    o->frame = 0;
    if (o->frame >= lem_ty[o->id].frames)
        o->frame = 0;
    lem_remove();
    lem_snd_cue(SND_TRAP);
}

/* ovl_obj_step - UpdateInteractiveObjects (:4533): the hatches open once from
 * iteration 35, a triggered trap runs to its end and stops, and a continuous
 * object loops for ever. */
static void ovl_obj_step(void)
{
    int i, n;
    int entstep;
    struct lem_o *o;

    /* the hatches' test is OUTSIDE the walk, as UpdateInteractiveObjects has
     * it: when the first hatch finishes, the others still step this frame */
    entstep = !lem_entdone && lem_iter >= LEM_IT_OPEN;
    for (i = 0; i < lem_nob; i++) {
        o = &lem_ob[i];
        n = lem_ty[o->id].frames;
        if (o->ent) {
            if (!entstep || !o->trig)
                continue;
            o->frame++;
            if (o->frame >= n) {
                o->frame = 0;
                o->trig = 0;
                lem_entdone = 1;
            }
            continue;
        }
        if (o->trig || lem_ty[o->id].anim == 2)
            o->frame++;
        if (o->frame >= n) {
            o->frame = 0;
            o->trig = 0;
        }
    }
}

/* ovl_obj_draw - object `i` at its current frame, into the background. */
static void ovl_obj_draw(int i)
{
    struct lem_o *o = &lem_ob[i];
    struct lem_ot *t = &lem_ty[o->id];

    if (t->w == 0)
        return;
    ovl_far(lem_cl_bank, (int)t->off_lo, (int)t->off_hi);
    lem_far_mul((unsigned)o->frame, t->per);
    lem_r_obj(lem_far_seg, lem_far_off, o->x, o->y, (int)t->w, (int)t->h,
              (int)o->fl);
    o->drawn = o->frame;
    o->dirty = 0;
}

/* ovl_obj_hits - does object i's picture touch the rectangle? */
static int ovl_obj_hits(int i, int x, int y, int w, int h)
{
    struct lem_o *o = &lem_ob[i];
    struct lem_ot *t = &lem_ty[o->id];

    return o->x < x + w && x < o->x + (int)t->w &&
           o->y < y + h && y < o->y + (int)t->h;
}

/* THE DRAWING ORDER, built once a level: DrawAnimatedObjects (:2520) draws
 * the hatches only-on-terrain, the rest only-on-terrain, the hatches, the
 * rest - four passes over every object. A frame redraws objects over up to
 * forty rectangles, and four passes of 32 tests each, in C, for every one of
 * them was the frame's largest bookkeeping cost on an XT; the order is a fact
 * of the level and is taken once. lem_ovis is the frame's own short list of
 * it (lemdraw.c ovl_obj_vis): the objects any of its rectangles can touch. */
static unsigned char lem_oord[32];
static int lem_nord = -1;           /* -1: not built for this level */
static unsigned char lem_ovis[32];
static int lem_nvis = -1;           /* -1: walk the whole order */

static void ovl_obj_order(void)
{
    int pass, i;
    struct lem_o *o;

    if (lem_nord >= 0)
        return;
    lem_nord = 0;
    for (pass = 0; pass < 4; pass++)
        for (i = 0; i < lem_nob; i++) {
            o = &lem_ob[i];
            if ((pass & 1) != (o->ent ? 0 : 1))
                continue;
            if (((o->fl & 1) != 0) != (pass < 2))
                continue;
            lem_oord[lem_nord++] = (unsigned char)i;
        }
}

/* ovl_obj_redraw_rect - every object over the rectangle, again, in that
 * order. AN OBJECT THAT REACHES OUTSIDE THE RECTANGLE IS ONLY PART-DRAWN: on
 * VGA the compose clips it to the rectangle (lemsprite.inc lem_r_cclip), and
 * on the shadow backends lem_r_show() moves only the rectangle onto the glass.
 * So its `drawn` and `dirty` are put back as they were; the frame marks every
 * object it owed a redraw once all its rectangles are done (lemdraw.c
 * ovl_objs_mark), which is what a moved object's own rectangle guarantees. */
static void ovl_obj_redraw_rect(int x, int y, int w, int h)
{
    int k, n, i, d, dy;
    struct lem_o *o;
    unsigned char *list;

    ovl_obj_order();
    if (lem_nvis >= 0) {
        list = lem_ovis;
        n = lem_nvis;
    } else {
        list = lem_oord;
        n = lem_nord;
    }
    for (k = 0; k < n; k++) {
        i = list[k];
        if (!ovl_obj_hits(i, x, y, w, h))
            continue;
        o = &lem_ob[i];
        d = o->drawn;
        dy = o->dirty;
        ovl_obj_draw(i);
        if (o->x < x || o->y < y ||
            o->x + (int)lem_ty[o->id].w > x + w ||
            o->y + (int)lem_ty[o->id].h > y + h) {
            o->drawn = (unsigned char)d;
            o->dirty = (unsigned char)dy;
        }
    }
}

/* ovl_obj_all - every object, which is the level's first frame and the way
 * back in after 'f'. */
static void ovl_obj_all(void)
{
    ovl_obj_redraw_rect(-16000, -16000, 32000, 32000);
}
