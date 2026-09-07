/* ============================================================================
 * os8088 - apps/paccman/pmc_time.c    the two-word tick and the triggers
 *
 * DERIVED MATERIAL. Part of PACCMAN, a reimplementation of Andre Weissflog's
 * pacman.c (https://github.com/floooh/pacman.c), MIT, (c) 2020 Andre
 * Weissflog, at commit 0f5ec5a; this file carries its time-trigger vocabulary
 * (pacman.c 322-325, 420-428, 844-915) and the fixed-step loop of its frame()
 * (744-780). See apps/paccman/README.md.
 *
 * #included by apps/paccman/paccman.c, which is one translation unit
 * (SPEC.md 73.1).
 *
 * ---------------------------------------------------------------------------
 * WHY A TICK IS TWO WORDS AND A `since` IS TWO FUNCTIONS
 * ---------------------------------------------------------------------------
 * The reference counts 60 Hz ticks in a `uint32_t` and there is no 32-bit
 * integer in this C at all (SPEC.md 73.7, and `long` is #defined to something
 * that cannot parse). So the tick and every trigger are a lo/hi pair of words,
 * and the arithmetic that has to cross the boundary - the add in
 * pmc_start_after, the compare in pmc_since - is written out by hand.
 *
 * A 16-bit `since` cannot answer every question the reference asks of a 32-bit
 * one, so there are TWO of them and choosing wrongly is a bug that takes nine
 * minutes to appear:
 *
 *   pmc_since()     SATURATES at PMC_SAT (0x7FFE) and answers PMC_NEVER for a
 *                   trigger that is disabled or has not fired yet. Every
 *                   COMPARE uses it - after(), before(), between(),
 *                   after_once() - and saturation is harmless there because
 *                   the largest interval any of them names is 240 ticks.
 *   pmc_since_lo()  WRAPS in 16 bits, and is what a BLINK MASK must use:
 *                   `since(round_won) & 0x10` alternates for ever off the
 *                   wrapping form and FREEZES off the saturating one, because
 *                   0x7FFE & 0x10 is a constant. The harness's 40,000-tick
 *                   attract drive is what catches the wrong one.
 * ==========================================================================*/

/* --- the trigger table ----------------------------------------------------
 * The reference scatters `trigger_t` fields through its state struct; here
 * they are two parallel word arrays indexed by this enum, because an array of
 * a two-field struct would put a multiply on every access (SPEC.md 91's
 * stride note one level down) and because pmc_disable_all can then be a loop.
 *
 * The four per-ghost pairs are RANGES: PMC_T_FRIGHT0 + i and PMC_T_GEATEN0 + i
 * for ghost i, which is what lets the ghost code index them from a loop. */
enum {
    PMC_T_INTRO,        /* intro.started      */
    PMC_T_GAME,         /* game.started       */
    PMC_T_READY,        /* game.ready_started */
    PMC_T_ROUND,        /* game.round_started */
    PMC_T_WON,          /* game.round_won     */
    PMC_T_OVER,         /* game.game_over     */
    PMC_T_DOT,          /* game.dot_eaten     */
    PMC_T_PILL,         /* game.pill_eaten    */
    PMC_T_GEATEN,       /* game.ghost_eaten   */
    PMC_T_PMEATEN,      /* game.pacman_eaten  */
    PMC_T_FRUITEAT,     /* game.fruit_eaten   */
    PMC_T_LEAVE,        /* game.force_leave_house */
    PMC_T_FRUIT,        /* game.fruit_active  */
    PMC_T_FADEIN,       /* gfx.fadein         */
    PMC_T_FADEOUT,      /* gfx.fadeout        */
    PMC_T_FRIGHT0,      /* ghost[i].frightened, i = 0..3 */
    PMC_T_EATEN0 = PMC_T_FRIGHT0 + 4,   /* ghost[i].eaten, i = 0..3 */
    PMC_T_N      = PMC_T_EATEN0 + 4
};

#define PMC_NEVER   0xFFFF      /* pmc_since's "disabled, or not yet"       */
#define PMC_SAT     0x7FFE      /* ...and what it saturates a real gap at   */

static unsigned pmc_tick_lo, pmc_tick_hi;
static unsigned pmc_trg_lo[PMC_T_N];
static unsigned pmc_trg_hi[PMC_T_N];

/* --- the 18.2 Hz -> 60 Hz accumulator -------------------------------------
 * 60 game ticks a second against the OS's 18.2, which is 600/182 exactly as
 * the OS tick is 182/10 Hz. PMC_CATCHUP_MAX caps how much game time ONE drawn
 * frame may advance: a frame that took four OS ticks runs two ticks' worth of
 * game and re-anchors, so every sprite stays within a tile of where it was
 * last drawn and a slow adapter runs the game slowly rather than in jumps
 * (SPEC.md 91). It is a feel decision with the arithmetic beside it, not a
 * tuning knob. */
#define PMC_CATCHUP_MAX  2
#define PMC_ACC_PER_OS   600
#define PMC_ACC_PER_GAME 182

static unsigned pmc_acc;

static void pmc_tick_inc(void)
{
    pmc_tick_lo++;
    if (pmc_tick_lo == 0)
        pmc_tick_hi++;
}

static void pmc_disable(int t)
{
    pmc_trg_lo[t] = 0xFFFF;
    pmc_trg_hi[t] = 0xFFFF;
}

static void pmc_start_after(int t, unsigned n)
{
    unsigned lo;

    lo = pmc_tick_lo + n;
    pmc_trg_hi[t] = pmc_tick_hi + (lo < pmc_tick_lo ? 1 : 0);
    pmc_trg_lo[t] = lo;
}

static void pmc_start(int t)
{
    pmc_start_after(t, 1);
}

static int pmc_now(int t)
{
    return pmc_trg_lo[t] == pmc_tick_lo && pmc_trg_hi[t] == pmc_tick_hi;
}

/* pmc_since - ticks since the trigger fired, PMC_NEVER while it has not.
 *
 * The `hi` difference is the whole of the 32-bit part: a gap of one whole
 * 65,536-tick word is 18 minutes of play and already far past PMC_SAT, so the
 * only case worth spelling out is the one where the low word has just wrapped
 * and the real gap is small. There, `tick_lo - trg_lo` in 16-bit unsigned
 * arithmetic IS the gap. */
static unsigned pmc_since(int t)
{
    unsigned d;

    if (pmc_trg_hi[t] == 0xFFFF && pmc_trg_lo[t] == 0xFFFF)
        return PMC_NEVER;
    if (pmc_tick_hi < pmc_trg_hi[t])
        return PMC_NEVER;
    if (pmc_tick_hi == pmc_trg_hi[t]) {
        if (pmc_tick_lo < pmc_trg_lo[t])
            return PMC_NEVER;
        d = pmc_tick_lo - pmc_trg_lo[t];
        return d > PMC_SAT ? PMC_SAT : d;
    }
    if (pmc_tick_hi - pmc_trg_hi[t] > 1)
        return PMC_SAT;
    if (pmc_tick_lo >= pmc_trg_lo[t])
        return PMC_SAT;                     /* a whole word and more */
    d = pmc_tick_lo - pmc_trg_lo[t];        /* wraps to the real gap */
    return d > PMC_SAT ? PMC_SAT : d;
}

/* pmc_since_lo - the 16-bit WRAPPING gap, for blink masks only. A disabled
 * trigger answers a number nobody may test for enablement, so every caller
 * has already asked pmc_since or pmc_after first. */
static unsigned pmc_since_lo(int t)
{
    return pmc_tick_lo - pmc_trg_lo[t];
}

static int pmc_after(int t, unsigned n)
{
    unsigned s = pmc_since(t);
    return s != PMC_NEVER && s >= n;
}

static int pmc_after_once(int t, unsigned n)
{
    return pmc_since(t) == n;
}

static int pmc_before(int t, unsigned n)
{
    unsigned s = pmc_since(t);
    return s != PMC_NEVER && s < n;
}

/* --- the random-number generator ------------------------------------------
 * The reference's xorshift32 seeded 0x12345678 becomes a xorshift16 seeded
 * 0x1234 (SPEC.md 91): there is no 32-bit type, and what the number is USED
 * for is a frightened ghost's target tile, where the only requirement is that
 * successive draws do not correlate. The triple 7/9/8 is the classic 16-bit
 * full-period set.
 *
 * The two range reductions do NOT divide. `x % 28` is a `div` the compiler
 * will emit and 28 is not a power of two; masking to the next power of two and
 * folding the overflow costs two instructions and biases the low values
 * slightly, which for a random corner to run towards is not a property
 * anything can observe. */
static unsigned pmc_rndstate = 0x1234;

static unsigned pmc_rand16(void)
{
    unsigned x;

    x = pmc_rndstate;
    x ^= x << 7;
    x ^= x >> 9;
    x ^= x << 8;
    pmc_rndstate = x;
    return x;
}

static int pmc_rand_tx(void)            /* 0 .. PMC_TILES_X - 1 */
{
    int v = (int) (pmc_rand16() & 31);
    if (v >= PMC_TILES_X)
        v -= PMC_TILES_X;
    return v;
}

static int pmc_rand_ty(void)            /* 0 .. PMC_TILES_Y - 1 */
{
    int v = (int) (pmc_rand16() & 63);
    if (v >= PMC_TILES_Y)
        v -= PMC_TILES_Y;
    return v;
}

/* --- the score, as BCD digits ---------------------------------------------
 * The arcade score is the displayed number divided by ten and it passes
 * 65,535 in ordinary play (the perfect game is 3,333,360, so 333,336 here),
 * which no 16-bit integer holds. Eight digits, least significant first, is
 * what the reference's own right-to-left printer wants anyway.
 *
 * pmc_score_add takes n < 100,000,000 and carries by hand. The `% 10` and
 * `/ 10` a digit split would need are a `div` per digit; the loop below peels
 * the units digit with subtraction instead, which for the values this game
 * actually adds - 1 a dot, 5 a pill, 20/40/80/160 a ghost, up to 500 a fruit -
 * is at most fifty iterations on the one call a bonus fruit makes. */
#define PMC_SCORE_DIGITS 8

static void pmc_score_zero(unsigned char *d)
{
    int i;
    for (i = 0; i < PMC_SCORE_DIGITS; i++)
        d[i] = 0;
}

static void pmc_score_add(unsigned char *d, int n)
{
    int i, s, c, u;

    c = 0;
    for (i = 0; i < PMC_SCORE_DIGITS; i++) {
        if (n == 0 && c == 0)
            break;
        u = 0;
        while (n >= 10) {               /* n becomes this digit, u the rest */
            n -= 10;
            u++;
        }
        s = d[i] + n + c;
        c = 0;
        if (s >= 10) {
            s -= 10;
            c = 1;
        }
        d[i] = (unsigned char) s;
        n = u;
    }
}

/* 1 when a is above b, which is the only comparison the hiscore needs. */
static int pmc_score_above(const unsigned char *a, const unsigned char *b)
{
    int i;

    for (i = PMC_SCORE_DIGITS - 1; i >= 0; i--) {
        if (a[i] != b[i])
            return a[i] > b[i];
    }
    return 0;
}

static void pmc_score_copy(unsigned char *d, const unsigned char *s)
{
    int i;
    for (i = 0; i < PMC_SCORE_DIGITS; i++)
        d[i] = s[i];
}

static int pmc_score_zerop(const unsigned char *d)
{
    int i;
    for (i = 0; i < PMC_SCORE_DIGITS; i++)
        if (d[i])
            return 0;
    return 1;
}
