/* ============================================================================
 * os8088 - apps/paccman/pmc_intro.c    the attract screen
 *
 * DERIVED MATERIAL. Part of PACCMAN, a reimplementation of Andre Weissflog's
 * pacman.c (https://github.com/floooh/pacman.c), MIT, (c) 2020 Andre
 * Weissflog, at commit 0f5ec5a; this file carries its intro_tick
 * (pacman.c 2326-2399). See apps/paccman/README.md.
 *
 * #included by apps/paccman/paccman.c (one translation unit, SPEC.md 73.1).
 *
 * ---------------------------------------------------------------------------
 * THE REVEAL IS A LIST OF DELAYS AND IT IS THE REFERENCE'S LIST
 * ---------------------------------------------------------------------------
 * intro_tick draws nothing per frame: it draws ONCE at each of fourteen event
 * ticks and the two RAMs hold the picture in between, which is exactly what
 * this port's damage model wants (a tick that changes nothing marks nothing,
 * so an attract screen standing still costs no band at all). The delays are
 * accumulated in the same order and by the same amounts as the reference's
 * local `delay`, so the events land at
 *
 *      60 120 150 | 180 240 270 | 300 360 390 | 420 480 510 | 570 | 630
 *      block name nick   ...one group per ghost...            PTS   prompt
 *
 * and SPEC.md 91 lists them because the harness stamps a screen at every one.
 *
 * THE BLINK MUST USE THE WRAPPING `since`. `since(intro) & 0x20` alternates
 * for ever in the reference's 32-bit arithmetic; pmc_since() SATURATES at
 * 0x7FFE so that every compare in the program stays cheap, and 0x7FFE & 0x20
 * is a constant - a prompt written against it stops blinking nine minutes into
 * one attract screen and never starts again. pmc_since_lo() is the 16-bit
 * wrapping form and this is what it is for (pmc_time.c); the harness's
 * 40,000-tick attract drive is what catches the wrong one.
 *
 * IT IS ALSO THE FIRST ovl_* CANDIDATE (SPEC.md 73.14): it is once-per-attract
 * code that a keystroke never touches, so it is what moves out of the resident
 * image the day os88pkg's line passes 50,000. Nothing here may take the
 * address of a function, for that reason among others.
 * ==========================================================================*/

/* The four ghosts as the cabinet introduces them - the arcade prints the
 * NICKNAME on the left and the character name on the right, which is why the
 * reference's `names` are the hyphenated ones (pacman.c 2346-2347). */
static const char *pmc_gh_names[4] = {
    "-SHADOW", "-SPEEDY", "-BASHFUL", "-POKEY"
};
static const char *pmc_gh_nicks[4] = {
    "BLINKY", "PINKY", "INKY", "CLYDE"
};

/* The 1UP score on the attract screen is a literal zero in the reference
 * (`vid_color_score(i2(6,1), COLOR_DEFAULT, 0)`, pacman.c 2335) and NOT the
 * score of the game that just ended - which is still in pmc_score when GAME
 * OVER hands control back here. pmc_vid_score takes a digit array, so the
 * literal is one. */
static const unsigned char pmc_zero8[PMC_SCORE_DIGITS] = { 0, 0, 0, 0,
                                                           0, 0, 0, 0 };

/* THE BLINK'S PHASE, so the prompt row is written on the FLIP and not on every
 * game tick. The reference re-issues the whole row each tick (pacman.c 2393-
 * 2398) because there it is a display-list append; here the row's content
 * changes once every 32 game ticks, and the two RAM writes compare before they
 * store - so the tick cost was real and the drawing cost was zero. 23
 * characters through pmc_conv, pmc_vid_color_tile, pmc_vid_tile, pmc_vid_color
 * and pmc_ok is ~5 near calls a character (11 us each, PERFORMANCE.md's
 * table), call it 2 ms a game tick and 3.3 game ticks a frame: ~6 ms of every
 * 55 ms frame, on the screen an unattended machine sits on longest, spent
 * re-deciding 23 tiles that had not changed. -1 is "no phase yet", so a fresh
 * attract cycle always writes the row once. */
static int pmc_promptph = -1;

/* pmc_intro_init - the `now(state.intro.started)` arm of intro_tick
 * (pacman.c 2328-2341), split out because os88_main draws the first picture
 * with it: a window that is empty until the worker's first frame is a window
 * that looks broken, and every pmc_vid_* write compares before it stores, so
 * running it twice costs nothing and marks nothing the second time. */
static void pmc_intro_init(void)
{
    pmc_snd_clear();
    pmc_spr_clear();
    pmc_promptph = -1;                  /* the row is written on the flip, and
                                         * vid_clear below has just wiped it */
    pmc_start(PMC_T_FADEIN);
    pmc_input_on = 1;                   /* input_enable(), pacman.c 923-925 */

    pmc_vid_clear(PMC_TILE_SPACE, PMC_COLOR_DEFAULT);
    pmc_vid_text(3, 0, "1UP   HIGH SCORE   2UP");
    pmc_vid_score(6, 1, PMC_COLOR_DEFAULT, pmc_zero8);

    /* THE HISCORE FIELD IS ABSENT UNTIL THERE IS ONE, which is the machine's
     * own behaviour and not a saving: a fresh instance shows the two headings
     * and one score. */
    if (!pmc_score_zerop(pmc_hiscore))
        pmc_vid_score(16, 1, PMC_COLOR_DEFAULT, pmc_hiscore);

    pmc_vid_text(7, 5, "CHARACTER / NICKNAME");
    pmc_vid_text(3, 35, "CREDIT  0");
}

/* intro_tick, pacman.c 2326-2399.
 *
 * `2UP` and `CREDIT  0` are drawn and mean nothing: the reference has no
 * second player and no coin logic either (they are static text at its lines
 * 2335 and 2341), so the port carries the words and nothing behind them.
 * SPEC.md 91 and the README say so.
 *
 * The animated chase the arcade plays after the legend is NOT here, and not
 * because it was dropped: the reference has a `// FIXME: animated chase
 * sequence` at its line 2391 where it would go, and its own header lists it
 * among the things it does not do. */
static void pmc_intro_tick(void)
{
    int i, color, y;
    unsigned delay;

    if (pmc_now(PMC_T_INTRO))
        pmc_intro_init();

    delay = 30;
    for (i = 0; i < 4; i++) {
        color = 2 * i + 1;              /* BLINKY 1, PINKY 3, INKY 5, CLYDE 7 */
        y = 3 * i + 6;

        /* the 2x3 ghost picture, built from six TILES and not from a sprite -
         * the cabinet draws it into the tile RAM (pacman.c 2353-2357) */
        delay += 30;
        if (pmc_after_once(PMC_T_INTRO, delay)) {
            pmc_vid_color_tile(4, y,     color, PMC_TILE_GHOST + 0);
            pmc_vid_color_tile(5, y,     color, PMC_TILE_GHOST + 1);
            pmc_vid_color_tile(4, y + 1, color, PMC_TILE_GHOST + 2);
            pmc_vid_color_tile(5, y + 1, color, PMC_TILE_GHOST + 3);
            pmc_vid_color_tile(4, y + 2, color, PMC_TILE_GHOST + 4);
            pmc_vid_color_tile(5, y + 2, color, PMC_TILE_GHOST + 5);
        }

        /* ...a second later the name, and half a second after it the
         * nickname - each in that ghost's own colour, except on a 1bpp
         * adapter where two of the four collapse into an unreadable
         * checkerboard (pmc_text_ink, and it was photographed doing exactly
         * that before this call went through it). The PICTURE above keeps its
         * colour on every adapter: a dithered ghost is still a ghost. */
        delay += 60;
        if (pmc_after_once(PMC_T_INTRO, delay))
            pmc_vid_color_text(7, y + 1, pmc_text_ink(color), pmc_gh_names[i]);

        delay += 30;
        if (pmc_after_once(PMC_T_INTRO, delay))
            pmc_vid_color_text(17, y + 1, pmc_text_ink(color),
                               pmc_gh_nicks[i]);
    }

    /* the scoring legend: a dot worth 10 and an energizer worth 50. 0x5D-0x5F
     * are the arcade font's own "PTS" glyphs, which is why they are written as
     * character codes rather than as letters. */
    delay += 60;
    if (pmc_after_once(PMC_T_INTRO, delay)) {
        pmc_vid_color_tile(10, 24, PMC_COLOR_DOT, PMC_TILE_DOT);
        pmc_vid_text(12, 24, "10 \x5D\x5E\x5F");
        pmc_vid_color_tile(10, 26, PMC_COLOR_DOT, PMC_TILE_PILL);
        pmc_vid_text(12, 26, "50 \x5D\x5E\x5F");
    }

    /* ...and the prompt, blinking on bit 5 of the elapsed count. IT IS WRITTEN
     * ON THE FLIP: see pmc_promptph. pmc_since_lo is the WRAPPING helper and
     * not pmc_since, which saturates at 0x7FFE - a constant, so a prompt
     * written against it stops blinking after about nine minutes of one attract
     * screen (pmc_time.c; pmcuitest drives 40,000 ticks for exactly this).
     *
     * The BLANK arm is written in PMC_COLOR_DEFAULT rather than 3 because that
     * is what pmc_vid_clear left the row in: at tick 630 bit 5 is SET, so the
     * reference's own first frame of the prompt is the blank one, and writing
     * colour 3 over 23 already-blank cells marked a band that composed
     * pixel-identical output - one wasted band a cycle, which on CGA is a
     * 551 us compose plus its pack and blit for nothing. */
    delay += 60;
    if (pmc_after(PMC_T_INTRO, delay)) {
        int on = (pmc_since_lo(PMC_T_INTRO) & 0x20) == 0;
        if (on != pmc_promptph) {
            pmc_promptph = on;
            if (on)
                pmc_vid_color_text(3, 31, 3, "PRESS ANY KEY TO START!");
            else
                pmc_vid_color_text(3, 31, PMC_COLOR_DEFAULT,
                                   "                       ");
        }
    }

    /* ANY KEY STARTS THE GAME, after the fade. pmc_anyheld is the press latch
     * seen from the intro's side (paccman.c): a frame here is 3 to 11 game
     * ticks long, so a key press is remembered until the frame's poll folds it
     * in, exactly as a direction press is. F is not one of them - the reference
     * gives it its own case with no anykey beside it - and neither is a key
     * that only took the About card down. */
    if (pmc_anyheld) {
        pmc_anyheld = 0;
        pmc_input_dis();
        pmc_start(PMC_T_FADEOUT);
        pmc_start_after(PMC_T_GAME, PMC_FADE_TICKS);
    }
}

/* pmc_intro_start - what the program opens on, and what GAME OVER returns to.
 *
 * The reference's init() does `start(&state.intro.started)` and nothing else
 * (pacman.c 918-925); here the first picture is drawn straight away as well,
 * for pmc_intro_init's reason. Every trigger is disabled first, because this
 * is reachable from the middle of a round. */
static void pmc_intro_start(void)
{
    int i;

    for (i = 0; i < PMC_T_N; i++)
        pmc_disable(i);
    pmc_acc = 0;
    pmc_paused = 0;
    pmc_pause_item();
    pmc_black = 0;
    pmc_shblack = 0;
    pmc_mode = PMC_MODE_INTRO;
    pmc_intro_init();
    pmc_start(PMC_T_INTRO);
}
