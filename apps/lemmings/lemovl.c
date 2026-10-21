/* ============================================================================
 * os8088 - apps/lemmings/lemovl.c   (#included by lemmings.c)
 *
 * EVERYTHING ovl_*, WHICH IS EVERYTHING IN THE OVERLAY PART (SPEC.md 73.14.2,
 * 116.6).
 *
 * A function whose name begins with `ovl_` has its CODE emitted into a second
 * segment that ships INSIDE the package as part 0 and is far-called both
 * ways; every global, literal and bss byte it names stays RESIDENT and
 * DS-relative, which is what makes the mechanism possible in C at all. The
 * rename is the entire mechanism - there is nothing else to declare
 * (LESSONS.md 5).
 *
 * THE SPLIT IS BY FREQUENCY AND NEVER BY SIZE, and it is NOT "nothing here
 * runs on a game frame" any more - that sentence was wave 2's and wave 3 made
 * it false. In the module, by how often each runs:
 *
 *   once        the About card, the sentence behind a greyed control, the
 *               preview and postview text, the launcher's chrome, the
 *               progress file, the level's load and its compose
 *               (ovl_compose), the object setup
 *   per key     the bracket's key and mouse handlers, the assignment and the
 *               hit test, the queue
 *   per FRAME   the play loop and the frame's whole drawing side - lemdraw.c's
 *               ovl_tick_draw, ovl_vframe and its rectangles, ovl_sprites_all
 *               and ovl_sprite_one, the minimap's dots and the panel's live
 *               cells - plus ovl_frame_begin/end and the objects' step
 *
 * What stays resident is what runs per LEMMING per frame on the mechanics
 * side: lem_handle and the eighteen handlers, the probes, lem_frame_chunk.
 * The drawing side's per-lemming call (ovl_sprite_one) is in the module and
 * crosses into the resident raster through cc_ovthunk two or three times a
 * lemming (lem_anim_at, lem_r_sprite, and lem_mask_at + lem_r_sprite for a
 * fuse) and five or six times a rectangle - a cost nobody has measured yet,
 * which is why SPEC.md 116.7 names "module-to-resident crossings per frame"
 * among the counters the final MartyPC pass reports. The once-a-level split
 * was from wave 1 rather than from the wave the ceiling binds in: at cword's
 * MEASURED 5.7 bytes a line of resident C this program does not fit one
 * segment and never could (SPEC.md 116.6).
 *
 * THE MODULE IS A PART AND AN EAGER ONE (CC_OVL_PART, SPEC.md 73.14.2): it is
 * read with LEMMINGS.O88 at launch and bound before os88_main() runs, so no
 * ovl_ call here can refuse - a machine without the RAM refuses the LAUNCH,
 * in the kernel's own words (op_load), before a window exists.
 *
 * NEVER FROM A WORKER, AND NEVER ITS ADDRESS. The worker gate is decided from
 * SP rather than from a flag (LESSONS.md 5); this package has no worker, which
 * is deliberate, and tools/cc8086.py refuses an ovl_ address by name.
 *
 * ATTRIBUTION. The preview wording, the level names and the greying sentences
 * this file assembles are the original's own strings and the converter's own
 * prose, read out of LEMSTR.LEM (SPEC.md 116.3). Lemmings is (C) 1991 DMA Design
 * / Psygnosis; the layout of the preview screen is Lemmix
 * GameScreen.Preview.pas GetScreenLinesAndColors and its wording
 * Base.Strings.pas:411-418. README.TXT carries the full list.
 * ==========================================================================*/

/* --- the chrome ------------------------------------------------------------
 * The launcher's two rules: the one between the level list and the preview
 * fields, and the one above the state rows. They are drawn on a FULL repaint
 * only, which is what keeps them out of the per-keystroke cost - and they are
 * two drawing calls, not a loop of primitives, because anything drawn from a
 * loop of primitives is 756 us each (PERFORMANCE.md, LESSONS.md 6).
 *
 * BOTH LIVE IN PIXELS NO GLYPH BAND REACHES, and the first version of each did
 * not. The rules are not in the shadow, so anything that letters over them
 * takes them down for good:
 *
 *   - the vertical one was at lem_colx(lem_listw) - 2, which is pixel column 6
 *     of cell listw-1 - inside the span lem_row_level() pads for the selection
 *     bar. The first paint cut a hole in it wherever a row carried the
 *     selection or a 31-character title, and every selection move cut another,
 *     because a partial repaint does not come through here. It is now in the
 *     GUTTER cell (listw), which lem_brk() keeps every composed run out of, so
 *     nothing can ever draw it;
 *   - the horizontal one was at lem_rowy(lem_playrow) - 2, and a row's glyph
 *     band is 8 pixels of a 9-pixel pitch, so that is the LAST pixel row of the
 *     band above it rather than the leading pixel between them. -1 is the gap.
 *
 * WHICH SIDE OF THE ROW LOOP THIS IS CALLED FROM is lem_repaint()'s decision
 * and it is not free: on the list the rules go down AFTER the rows, because a
 * full repaint re-letters the blanked gutter cell; leaving the list they come
 * down BEFORE, because the paragraph's text runs across both columns. */
static void ovl_chrome(int screen)
{
    int x, y;

    /* It ERASES as well as draws. The two rules are not in the shadow, so a
     * screen change has to take them down explicitly - and drawing them in the
     * background colour is one call rather than a special case. */
    os88_set_color(screen == LEM_SC_LIST ? OS88_LGRAY : OS88_WHITE);
    if (lem_pvw > 2) {
        x = lem_colx(lem_listw) + 3;
        os88_gfx_vline(x, lem_rowy(LEM_LISTTOP) - 1,
                       lem_rowy(LEM_LISTTOP + lem_nlist) - 2);
    }
    y = lem_rowy(lem_playrow) - 1;
    os88_gfx_hline(lem_colx(0), lem_colx(lem_cols) - 1, y);
    os88_set_color(OS88_BLACK);
}

/* --- the preview fields ----------------------------------------------------
 * ONE line of the original's seven, formatted into `dst`. The wording and the
 * order are Lemmix's (GetScreenLinesAndColors, Base.Strings.pas:411-418) and
 * the strings are the band's; what this adds is the values.
 *
 * `indent` is honoured by the caller and not here, so the same seven lines
 * serve the launcher's pane (unindented, beside the list) and the preview
 * SCREEN (indented by the original's own ten spaces). */
static void ovl_fmt(int line, int level, char *dst, unsigned cap)
{
    dst[0] = 0;
    if (line < 0 || line >= LEM_PV_LINES || !lem_rat_ok)
        return;
    ovl_arg_reset();
    if (line == 0) {
        ovl_arg_num((unsigned)(level + 1));
        ovl_arg_add(lem_trim(lem_ent_name(level)));
    } else if (line == 1) {
        ovl_arg_num((unsigned)lem_ent_u16(level, LEM_E_COUNT));
    } else if (line == 2) {
        /* A PERCENTAGE, WHICH IS WHAT THE ORIGINAL'S SCREEN SAYS - "10% To Be
         * Saved" on Fun 1 and not "1". Lemmix GameScreen.Preview.pas:275-278
         * with LemmingsPercentages, which is in TMiscOptions.DEFAULT; the raw
         * count is the non-default arm. See ovl_arg_pct(). */
        ovl_arg_pct((unsigned)lem_ent_u16(level, LEM_E_SAVE),
                    (unsigned)lem_ent_u16(level, LEM_E_COUNT));
    } else if (line == 3) {
        ovl_arg_num((unsigned)lem_ent_u16(level, LEM_E_RATE));
    } else if (line == 4) {
        ovl_arg_num((unsigned)lem_ent_u16(level, LEM_E_MINUTES));
    } else if (line == 5) {
        ovl_arg_add(lem_str((int)lem_rating_str[lem_rating]));
    } else {
        /* A SPECIAL LEVEL HAS NO TERRAIN GRAPHIC SET: its terrain is the
         * 960x160 picture and the set lends only the objects, so the row
         * that carries the set (the stated divergence from Lemmix's style
         * pack description, SPEC.md 116.1) would say something false there
         * - and is left out, rather than given a name of the port's own
         * (wave 4 review) */
        if (lem_ent_u8(level, LEM_E_SPECIAL) != 0xFF)
            return;
        ovl_arg_add(lem_str(ovl_style_name(lem_ent_u8(level, LEM_E_STYLE))));
    }
    ovl_sfmt(dst, cap, lem_str((int)lem_pv[line].strid));
}

/* ovl_pvline - one of the seven lines for the launcher's PANE, which is
 * narrower than the original's 40 columns: "Level 27 Call in the bomb squad"
 * is 31 characters and the pane on the authored window is 28, so the first
 * version cut it mid-word ("...the bomb s", wave 4's verifier). A line that
 * does not fit is ELIDED - cut three cells short and closed with three full
 * stops, which is how this launcher already spells an ellipsis ("Level
 * Code...", SPEC.md 116's state rows; latin-1 and the OS face have no ellipsis
 * character). The original has no pane and so no rule of its own here; the
 * whole name is on the preview SCREEN, inside the bracket, where the 40-column
 * surface holds every title of the 120 (the longest, "Level 15 Don't let your
 * eyes deceive you" and "Level 27 The Island of the Wicker people", are 40
 * exactly - measured over the converted set). */
static void ovl_pvline(int line, int level, char *dst, unsigned cap, int cols)
{
    int n;

    ovl_fmt(line, level, dst, cap);
    n = (int)os88_strlen(dst);
    if (cols < 4 || n <= cols || cols >= (int)cap)
        return;
    dst[cols - 3] = '.';
    dst[cols - 2] = '.';
    dst[cols - 1] = '.';
    dst[cols] = 0;
}

/* ovl_preview - the level preview screen's TEXT, into the paragraph buffer.
 *
 * WAVE 1 DRAWS IT IN THE WINDOW, in the kernel's own face. Wave 5 draws this
 * same content inside SPEC.md 53's bracket, in the second foreign mode each
 * adapter needs to hold the original's 640x350 surface, lettered in MAIN.DAT's
 * own 16x16 purple font over its own brown background (SPEC.md 116.4.3) - the
 * CONTENT and the transition are here from wave 1 and only the surface changes.
 *
 * The layout is the original's: line 0 is the level's number and name and is
 * NOT indented, a blank line follows, and the six value rows carry the ten
 * spaces Lemmix's GameScreen.Preview.pas IndentStr puts in front of them
 * (LEMS_PV_INDENT is those ten spaces, verbatim). The footer - "Press mouse
 * button to continue" - is the row composer's, because it is centred on the
 * LIVE window width. */
static void ovl_preview(int level)
{
    int i, spaced;

    /* THE ORIGINAL PUTS A BLANK LINE BETWEEN EVERY VALUE ROW, not only after
     * the title. GetScreenLinesAndColors fills aLines[00], [03], [05], [07],
     * [09], [11], [13] and [15] and leaves [01], [02], [04], [06], [08], [10],
     * [12] and [14] empty - sixteen lines at a 16-pixel pitch from y=82, which
     * is why the surface has to be 640x350. Compressing that spacing is exactly
     * what LEMS_GREY_EGA tells the user is a CGA-and-EGA consequence of a
     * 640x200 mode, so compressing it everywhere would make that sentence false
     * on VGA and Hercules.
     *
     * SO IT IS DECIDED BY THE SURFACE AND NOT BY THE PORT. Thirteen rows are
     * needed - one title, one blank, six values and five blanks between them -
     * and lem_row_text() draws rows 1 .. lem_rows-2 with the footer on the
     * last, so lem_rows - 2 is what there is. A box that holds them gets the
     * original's spacing; one that does not gets the compressed layout and the
     * greying sentence that describes it. Wave 5's 640x350 surface always
     * holds it.
     *
     * THE TITLE GAP IS TWO ROWS AND NOT ONE. GetScreenLinesAndColors leaves
     * BOTH [01] and [02] empty - [02] is the Replay string, which is empty in
     * ordinary play - and puts the first value at [03]; the enumeration three
     * paragraphs up says so and the first version of this loop then dropped
     * [02], which moved every value row and the footer 16 pixels up from the
     * original's screen on the 640x350 surface. So the spaced block is
     * FOURTEEN rows - one title, TWO blanks, six values and five blanks
     * between them - which is LEM_PV_LINES * 2, and the threshold is that.
     * The compressed layout keeps one blank after the title, which is what
     * LEMS_GREY_EGA describes as compressing the spacing out. */
    spaced = (lem_rows - 2) >= (LEM_PV_LINES * 2);

    ovl_para_reset();
    for (i = 0; i < LEM_PV_LINES; i++) {
        if (i == 1 || (spaced && i > 1))
            ovl_para_nl();
        if (i == 1 && spaced)
            ovl_para_nl();          /* the original's aLines[02] */
        ovl_fmt(i, level, lem_line, sizeof(lem_line));
        if (lem_pv[i].indent && lem_line[0])
            ovl_para_add(lem_str(LEMS_PV_INDENT));
        ovl_para_add(lem_line);
        ovl_para_nl();
    }
}

/* --- the fact behind a greyed control (SPEC.md 47) -------------------------
 * A greyed control names the FACT that greys it, and a fact is a sentence the
 * CONVERTER wrote rather than one this program composed. Four of them - Music,
 * Level Code, the 320x200x16 mode, Save Progress - are whole sentences in the
 * band and take no arguments at all. The other three are TEMPLATES whose only
 * marker is %s, because a level row greyed for a missing graphic set has to say
 * which file and what it would have cost, and those numbers are about a bank
 * that is NOT on this disk - so they cannot be measured from the disk and are
 * read out of LEMMAN.LEM's cost table instead (docs/lemband-format.md,
 * tools/os88lem.py build_manifest).
 *
 * The marker ORDER for each template is fixed by the converter's own comment,
 * repeated in build/lemstr.h, and it is the one thing here that a reader has to
 * check against that file rather than against this one.
 *
 * 0 = nothing was built and the caller stays where it is. */
static int ovl_greyfact(int labelid, int strid, int level)
{
    int style, spec, head;

    if (strid == LEMS_NONE)
        return 0;

    /* THE HEADING FIRST: ovl_fmt resets the argument list it formats with,
     * and the template's own arguments are taken after it */
    ovl_para_reset();
    if (labelid != LEMS_NONE) {
        ovl_para_add(lem_str(labelid));
        ovl_para_nl();
        ovl_para_nl();
    } else if (level >= 0 && lem_rat_ok) {
        /* THE LEVEL IS NAMED THE WAY EVERY OTHER SURFACE NAMES IT - "Level 1
         * Just dig!", LEMS_PV_LEVEL, Lemmix SPreviewScreen_Level_ss - and not
         * "1 Just dig!", which was this port's own form and nobody else's
         * (wave 4's third review): ovl_fmt's line 0, word for word. */
        ovl_fmt(0, level, lem_line, sizeof(lem_line));
        ovl_para_add(lem_line);
        ovl_para_nl();
        ovl_para_nl();
    }
    head = lem_paran;
    ovl_arg_reset();

    if (strid == LEMS_MISS_STYLE) {
        /* "%s is not on a %s disk: the %s graphic set is %s bytes = %s clusters
         *  of this disk's %s, with %s already spent." */
        style = lem_ent_u8(level, LEM_E_STYLE);
        if (style < 0 || style > 4)
            style = 0;                  /* the byte comes off a disk and every
                                         * byte read off one is hostile
                                         * (CLAUDE.md); ovl_cost_row would
                                         * answer -1 and the u32 read would be
                                         * one byte before the buffer */
        ovl_arg_add(ovl_bandname(LEM_BANK_STYLE, style));
        ovl_arg_add(ovl_geom());
        ovl_arg_add(lem_str(ovl_style_name(style)));
        ovl_arg_u32(lem_manbuf, ovl_cost_row(LEM_BANK_STYLE, style) +
                                LEM_COST_BYTES);
        ovl_arg_num((unsigned)ovl_cost_clus(LEM_BANK_STYLE, style));
        ovl_arg_num((unsigned)ovl_man_u16(LEM_MAN_DISKCLUS));
        ovl_arg_num((unsigned)ovl_man_u16(LEM_MAN_SETCLUS));
    } else if (strid == LEMS_MISS_SPEC) {
        /* "%s is not on a %s disk: its special picture is %s bytes = %s
         *  clusters of this disk's %s, with %s already spent."
         *
         * ONE PICTURE'S COST, under that picture's file name (wave 4's third
         * review): the first form quoted all four pictures' total (LEMMAN 132
         * and 136) beside one file, and LEVELS.TXT listed that file at a
         * quarter of it. The cost table carries every bank's own row, absent
         * or not, which is what it is there for. */
        spec = lem_ent_u8(level, LEM_E_SPECIAL);
        if (spec > 3)
            spec = 0;
        ovl_arg_add(ovl_bandname(LEM_BANK_SPEC, spec));
        ovl_arg_add(ovl_geom());
        ovl_arg_u32(lem_manbuf, ovl_cost_row(LEM_BANK_SPEC, spec) +
                                LEM_COST_BYTES);
        ovl_arg_num((unsigned)ovl_cost_clus(LEM_BANK_SPEC, spec));
        ovl_arg_num((unsigned)ovl_man_u16(LEM_MAN_DISKCLUS));
        ovl_arg_num((unsigned)ovl_man_u16(LEM_MAN_SETCLUS));
    } else if (strid == LEMS_NO_MODE) {
        /* "...on the level list. %s" - what happened to the level, which the
         * caller knows and the sentence must not guess (SPEC.md 47) */
        ovl_arg_add(lem_str(lem_mode_end));
    } else if (strid == LEMS_MISS_ROOM) {
        /* "This level's own record is not on a %s disk: what is on it takes
         *  %s of this disk's %s clusters, LEMMINGS.SAV keeps two, and a group
         *  of eight more records costs %s." */
        ovl_arg_add(ovl_geom());
        ovl_arg_num((unsigned)ovl_man_u16(LEM_MAN_SETCLUS));
        ovl_arg_num((unsigned)ovl_man_u16(LEM_MAN_DISKCLUS));
        ovl_arg_num((unsigned)ovl_man_u16(LEM_MAN_LEVCLUS));
    }

    /* Straight into the paragraph and not through lem_line: a fact is 200-260
     * characters and lem_line is one screen row wide. The Mode row's mode
     * sentence only where the MODE is what greys it (below). */
    if (labelid != LEMS_ROW_MODE || !lem_mode_ok)
        ovl_para_fmt(lem_str(strid));

    /* ...AND A ROW THAT IS GREYED FOR MORE THAN ONE REASON SAYS EVERY ONE
     * (SPEC.md 116.8, wave 5). The Mode row carries three: the 320x200x16
     * mode, the CGA/EGA screens' layout (the CGA640 surface holds 12 of the
     * original's 22 rows), and an 83-key keyboard's missing F11, F12 and Pause
     * - "the Mode row's second line" in SPEC.md 116.8's table. The Music row
     * carries the speaker's own when os88_snd_caps() answered none. Each is
     * shown only where it is TRUE of this machine (SPEC.md 47: a fact, never
     * a guess) - which is lem_state_grey's own predicate, so a row the reader
     * can click always has at least one. */
    if (labelid == LEMS_ROW_MODE) {
        ovl_grey_more(head, ovl_scr_cga(), LEMS_GREY_EGA);
        ovl_grey_more(head, !lem_kbd101(), LEMS_GREY_KEYS);
    } else if (labelid == LEMS_ROW_MUSIC) {
        ovl_grey_more(head, !lem_snd, LEMS_GREY_SOUND);
    }
    return 1;
}

/* ovl_grey_more - one more fact on a line of its own, when `on`.
 *
 * A LINE BREAK AND NOT A BLANK LINE, because the three Mode facts have to fit
 * the narrowest launcher there is: a CGA's 137-pixel content box is 15 rows,
 * 13 of them between the label's and the footer's, and the label, its blank,
 * the CGA mode's four wrapped lines, the layout's four and the keyboard's three
 * are 13 exactly with no blank between them (the harness asserts it). The
 * paragraph is lem_para's 640 bytes and the longest form - an EGA on an 83-key
 * keyboard, all three with the label - is 564 (tools/os88lem.py's table). */
static void ovl_grey_more(int head, int on, int strid)
{
    if (!on)
        return;
    if (lem_paran > head)
        ovl_para_nl();
    ovl_para_add(lem_str(strid));
}

/* --- the About card (SPEC.md 12.2, 20.5.1.1, 116.1's last row) --------------
 * TWELVE ROWS, AND TWELVE IS THE CEILING: it is a 640x200 number (LESSONS.md
 * 8 - a 19-row card put its OK button on the DESKTOP on CGA and Hercules). This
 * card is apps/os88ui.inc's standard one, measured from its lines and clamped
 * to the content box, and with OS88UI_ABFIT (lemmings.asm) its pitch steps
 * down from 12 to fit: a CGA's 137-pixel box takes twelve lines at 10, 134
 * pixels with the frame, so every row is inside the card and the card inside
 * the window. The card has no button of its own: a click or a key anywhere
 * dismisses it (lemmings.c lem_abdismiss).
 *
 *   0  Lemmings for os8088            the product, in the house form
 *   1  The DOS release, 1991          what it is a port OF
 *   3  the port line, in two halves
 *   6  Lemmings By DMA Design         the original's two principals and its
 *   7  Programming By Russell Kay     rightsholder - Lemmix SCredits' "Original
 *   8  Copyright 1991 Psygnosis Ltd.  credits..." block, verbatim
 *   9  Screens, strings and level order:
 *  10  Lemmix By Eric Langedijk        the second principal (decision 4); its
 *                                     licence is named in README.TXT, not
 *                                     here: Lemmix's own, zlib-style with an
 *                                     added clause, and never plain 'zlib'
 *  11  See README.TXT for the full credits.
 *
 * AND NOTHING ABOUT HOW THE BUILD RENDERS: the frame rate goes to
 * PERFORMANCE.md and SPEC.md 116.7, the synthesised tones to the greyed Music
 * row and to README.TXT (LESSONS.md 8).
 *
 * The lines array is RESIDENT (only code moves to the module) and is filled
 * here so that os88_paint() can redraw the card with os88_about_card_d()
 * without entering the module again. */
static void ovl_about(void *win)
{
    const char *s;
    unsigned n, cut;

    /* LEMS_PORTLINE is one sentence and a CGA About card is the narrow one, so
     * it is broken at the last space before the halfway mark rather than
     * clipped at the card's edge. */
    s = lem_str(LEMS_PORTLINE);
    n = os88_strlen(s);
    cut = n >> 1;
    while (cut > 0 && s[cut] != ' ')
        cut--;
    if (cut == 0 || cut > sizeof(lem_ab1) - 1)
        cut = (n < sizeof(lem_ab1) - 1) ? n : sizeof(lem_ab1) - 1;
    lem_fit(lem_ab1, sizeof(lem_ab1), s, (int)cut);
    lem_fit(lem_ab2, sizeof(lem_ab2), s + cut + (s[cut] == ' ' ? 1 : 0),
            (int)sizeof(lem_ab2) - 1);

    lem_ab[0] = lem_prodline;
    lem_ab[1] = lem_str(LEMS_VERSION);
    lem_ab[2] = "";
    lem_ab[3] = lem_ab1;
    lem_ab[4] = lem_ab2;
    lem_ab[5] = "";
    lem_ab[6] = lem_str(LEMS_CREDIT1);
    lem_ab[7] = lem_str(LEMS_CREDIT2);
    lem_ab[8] = lem_str(LEMS_COPYRIGHT);
    lem_ab[9] = lem_str(LEMS_CREDIT3);
    lem_ab[10] = lem_str(LEMS_CREDIT4);
    lem_ab[11] = lem_str(LEMS_README);
    lem_ab[12] = 0;
    os88_about_card(win, lem_ab);
}

/* --- progress (SPEC.md 19.9, 116.9) -----------------------------------------
 * SYSTEM/APPDATA/LEMMINGS.SAV, reached by SPEC.md 19.9's bank / GOTO / act /
 * come-back, which is apps/weave/wstate.c's worked example. The instance's
 * current directory is where every unqualified name it passes the file API
 * resolves and where its next dialog opens, so leaving it moved is a defect
 * that shows up two commands later.
 *
 * THE RECORD IS FOUR BYTES OF PROGRESS AND A HEADER: one byte per rating, the
 * furthest level reached in it plus one, 0 meaning none. It is not `.PRG`, and
 * that is not an aesthetic choice - apps/c64/c64cmd.c opens the Standard File
 * dialog on '*.PRG' and C64-SPEC 11.3 is Smart attach, so a Lemmings progress
 * blob with that extension would be offerable to a 6510 (LESSONS.md 1).
 *
 * WHAT GREYS THE ROW is whether this path answered at all, and the fact shown
 * is LEMS_GREY_SAVE, which says exactly that (wave 4 broadened it from the live
 * CD alone, which is one case of it). */
#define LEM_SAV_SIZE 8
static const char lem_f_sav[] = "LEMMINGS.SAV";
static unsigned char lem_savbuf[LEM_SAV_SIZE];
static struct os88_place lem_here;
static struct os88_find  lem_findbuf;

static int ovl_samename(const char *a, const char *b)
{
    int i;

    for (i = 0; i < 13; i++) {
        if (a[i] != b[i])
            return 0;
        if (a[i] == 0)
            return 1;
    }
    return 1;
}

static int ovl_dive(const char *name)
{
    int ord;

    ord = 0;
    while (ord >= 0) {
        ord = os88_file_find(ord, &lem_findbuf);
        if (ord < 0)
            break;
        if (lem_findbuf.type != OS88_FT_DIR)
            continue;
        if (!ovl_samename(lem_findbuf.name, name))
            continue;
        return os88_file_goto_q_mark(lem_findbuf.clus, lem_here.vol) == 0;
    }
    return 0;
}

/* THE FOUR FOLDER CHANGES ARE QUIET ONES, AND THAT IS THE WHOLE OF THIS DIVE'S
 * COST. os88_file_goto() is documented as "A REMOUNT: real floppy I/O" and this
 * walk makes four of them; os88_file_goto_q_mark() is the same-volume form and
 * "inside the volume you are on it is a word - no disk I/O", which is exactly
 * this case: every clus below is one this instance's own volume answered with,
 * so the vol never changes. The two ordinal walks that FIND those clusters are
 * the remaining disk time, and they are why this runs on W_ONWAKE rather than
 * in the paint (lemmings.c). apps/weave/wstate.c is the pattern and it uses the
 * plain form because it is reached from a menu command; this is not. */
static int ovl_data_enter(void)
{
    os88_file_here(&lem_here);
    if (os88_file_goto_q_mark(0, lem_here.vol) != 0)   /* the ROOT of it */
        return 0;
    if (!ovl_dive("SYSTEM") || !ovl_dive("APPDATA")) {
        ovl_data_leave();
        return 0;
    }
    return 1;
}

static void ovl_data_leave(void)
{
    os88_file_goto_q_mark(lem_here.clus, lem_here.vol);
}

/* ovl_progress_read - 1 = the path is there and lem_progress is set from it (or
 * left at -1 because there is no file yet), 0 = the path refused and the Save
 * Progress row greys. */
static int ovl_progress_read(void)
{
    unsigned n;
    int i;

    for (i = 0; i < LEM_RATINGS; i++)
        lem_prog[i] = -1;
    if (!ovl_data_enter())
        return 0;
    n = os88_file_read(lem_f_sav, lem_savbuf, LEM_SAV_SIZE);
    ovl_data_leave();
    if (n >= (unsigned)LEM_SAV_SIZE && lem_savbuf[0] == 'L' &&
        lem_savbuf[1] == 'E' && lem_savbuf[2] == 'M' && lem_savbuf[3] == 1) {
        /* every byte came off a disk (CLAUDE.md): a level past a rating's
         * thirty is a file this program never wrote, and taking it would
         * letter nonsense and record nothing after it (ovl_post_target's
         * compare) - so the record is ignored whole, as a bad magic is */
        for (i = 0; i < LEM_RATINGS; i++)
            if (lem_savbuf[4 + i] > LEM_PERRAT)
                return 1;
        for (i = 0; i < LEM_RATINGS; i++)
            lem_prog[i] = (int)lem_savbuf[4 + i] - 1;
    }
    return 1;
}

/* ovl_progress_write - 1 = LEMMINGS.SAV holds the new record. 0 = it does
 * not: the progress goes back to what the file holds, the Save Progress row
 * is greyed (lem_savable) and lem_savfail is raised, and the CALLER says so -
 * because since wave 5 one caller is inside SPEC.md 53's bracket, where a
 * kernel toast would paint desktop geometry into a foreign mode. The window
 * side's two callers (os88_onwake, ovl_launch) toast LEMS_SAV_FAIL - 24
 * characters, which is TOAST_MAX; it used to be the row's whole 100-character
 * sentence, cut to "LEMMINGS.SAV could not b" on the glass (wave 4's third
 * review) - and the greyed row carries the sentence. A replace puts its new
 * chain down before it frees the old one (SPEC.md 18.4), which is why the
 * disks keep two clusters for the file (SAV_CLUSTERS); a write that fails
 * anyway - a write-protected floppy, a disk filled from outside - used to be
 * dropped by os88_onwake while the glass showed the level as recorded (wave 4
 * review). */
static int ovl_progress_write(int rating, int level)
{
    int i, ok, was;

    if (rating < 0 || rating >= LEM_RATINGS)
        return 0;
    was = lem_prog[rating];
    if (level > lem_prog[rating])
        lem_prog[rating] = level;
    lem_savbuf[0] = 'L';
    lem_savbuf[1] = 'E';
    lem_savbuf[2] = 'M';
    lem_savbuf[3] = 1;
    for (i = 0; i < LEM_RATINGS; i++)
        lem_savbuf[4 + i] = (unsigned char)(lem_prog[i] + 1);
    ok = 0;
    if (ovl_data_enter()) {
        ok = os88_file_write(lem_f_sav, lem_savbuf, LEM_SAV_SIZE) == 0;
        ovl_data_leave();
    }
    if (!ok) {
        lem_prog[rating] = was;
        lem_savable = 0;
        lem_savfail = 1;
    }
    return ok;
}

/* ============================================================================
 * WAVE 2: ovl_compose - THE TERRAIN LIST, ONCE PER LEVEL (SPEC.md 116.4)
 *
 * IT IS IN THE MODULE BECAUSE IT RUNS ONCE PER LEVEL AND NEVER DURING PLAY,
 * which is SPEC.md 73.14's split by FREQUENCY exactly. It is also the largest
 * single piece of decision-making in the program - 400 slots, four flag bits,
 * a bank table lookup and a clip per piece - and none of it is on any path a
 * keystroke or a frame reaches.
 *
 * IT IS CALLED FROM INSIDE THE BRACKET, and that is safe for one reason and
 * one only: the module was forced RESIDENT before the bracket was entered
 * (lem_play, SPEC.md 116.5). cc_ovneed toasts its own refusal, and a kernel
 * toast paints DESKTOP geometry into a foreign mode - so the load may not
 * happen in here, and once the module is in, cc_ovneed is a four-byte no-op
 * that cannot reach that path.
 *
 * WHY IT HAS TO BE IN HERE AT ALL. On VGA the picture IS video memory, and
 * there is none until os88_fsx_mode() has run. Composing outside the bracket
 * would mean composing into a buffer this program has no room for: four planes
 * of 32,000 bytes is 128 KB.
 *
 * EVERY MULTI-BYTE FIELD OF THE RECORD IS BIG-ENDIAN - it is the original's own
 * .LVL, untouched by the converter (docs/lemband-format.md) - so it is read a
 * byte at a time and never with lem_u16().
 *
 * ATTRIBUTION. The record layout is lemmings_3ds/doc/data/
 * lemmings_lvl_file_format.txt's; the three rulings this walk makes where the
 * readers disagree - skip a 0xFFFF slot rather than break, the 12-bit x mask,
 * the y boundary at 0x100 - are SPEC.md 116.3.1's, measured over all 120 levels
 * in lemtool/REPORT.md. Lemmings is (C) 1991 DMA Design / Psygnosis.
 * ==========================================================================*/

/* ovl_spec_compose - a VGASPEC picture into the world, then its level's style
 * bank over it (wave 4, SPEC.md 116.3.1's VGASPEC row).
 *
 * THE PICTURE IS PIECES (docs/lemband-format.md's LEMSP version 2), each a
 * terrain piece in all but name, so it goes through lem_r_wpiece - the same
 * routine, the same one pass into T and the mask, no new assembly - at
 * x = 304 + column * width, y = row * height: Lemmix and lemmings_3ds both
 * draw the 960-pixel picture at 304, which leaves 304 pixels of empty level
 * to its left and 320 to its right. Every geometry field is read off the
 * header and checked, because every byte read off a disk is hostile
 * (CLAUDE.md): a piece wider than lem_r_wpiece's 96-byte shifted row, or one
 * whose size is not whole paragraphs, refuses rather than drawing.
 *
 * THEN THE STYLE BANK, mid-bracket, which SPEC.md 53.1 allows: the picture
 * needs no bank, and the objects need nothing else. A read that refuses
 * returns 0, which leaves the bracket and says so in the window - the
 * compose's ordinary refusal. The picture's palette is the level's (Lemmix
 * AssemblePalette's special arm), so the 24 bytes of it are carried across
 * the read and put back over the bank's own custom palette, where lem_r_pal
 * reads it when 'f' brings the level back. */
static unsigned char lem_spal[24];

static int ovl_spec_compose(int style)
{
    int cols, rows, pw, ph, x0, r, c, k, psz;
    unsigned seg;

    cols = ovl_pk(lem_cl_bank, LEM_SP_COLS);
    rows = ovl_pk(lem_cl_bank, LEM_SP_ROWS);
    pw = ovl_pk16(lem_cl_bank, LEM_SP_PW);
    ph = ovl_pk16(lem_cl_bank, LEM_SP_PH);
    x0 = ovl_pk16(lem_cl_bank, LEM_SP_X);
    /* ovl_pk16 is SIGNED: every field is bounded on both sides first, and
     * the grid is checked by DIVISION before any product - cols and rows are
     * header bytes up to 255, and 255 x 760 wraps a 16-bit int (wave 4
     * review: the old comment claimed the bounds kept the products inside an
     * int, and they did not). Once cols = 960 / pw and rows = 160 / ph, each
     * product is at most its target, and only then is it taken. */
    if (pw <= 0 || ph <= 0 || x0 < 0 || ph > LEM_WORLD_H ||
        (pw & 7) != 0 || (pw >> 3) + 1 > 96 || x0 > LEM_WORLD_W - 960 ||
        cols != 960 / pw || cols * pw != 960 ||
        rows != LEM_WORLD_H / ph || rows * ph != LEM_WORLD_H)
        return 0;
    psz = (pw >> 3) * ph;           /* one plane */
    if ((psz & 3) != 0)             /* four planes, whole paragraphs */
        return 0;
    seg = lem_cl_bank + (LEM_SP_HEAD >> 4);
    k = 0;
    for (r = 0; r < rows; r++)
        for (c = 0; c < cols; c++) {
            lem_r_wpiece(seg, 0, x0 + c * pw, r * ph, pw, ph, 0);
            seg += (unsigned)psz >> 2;  /* 4 x psz bytes, in paragraphs */
            k++;
            ovl_load_bar((k << 6) / (rows * cols));
        }
    for (k = 0; k < 24; k++)
        lem_spal[k] = (unsigned char)ovl_pk(lem_cl_bank, LEM_GR_CUSTOM + k);
    /* the bank read has the bar's 64..75 to itself, a step a part
     * (ovl_parts_read): two parts of up to 94 KB off a floppy is many seconds
     * on an XT, and a bar that sat at 64 through them looked hung (wave 4
     * review). Only here - everywhere else the read is outside the bracket,
     * where the well is not on the glass. */
    lem_pbar = 64;
    k = ovl_bank_read(style);
    lem_pbar = 0;
    if (!k)
        return 0;
    for (k = 0; k < 24; k++)
        os88_poke(lem_cl_bank, LEM_GR_CUSTOM + k, lem_spal[k]);
    /* THE BANK IS NO LONGER THE SET'S OWN: its custom palette is the
     * picture's now, so the next ovl_bank_read of this style must read it
     * again - Continue on the postview and a pick from the list both keep
     * the claims, and an ordinary level of the same set came up in the
     * picture's colours (wave 4 review). 'f' back into THIS level never asks
     * ovl_bank_read, so the resume keeps the poked palette. */
    lem_bank_have = -1;
    ovl_load_bar(75);
    return 1;
}

/* ovl_compose - 0 = nothing was composed, and the caller leaves the bracket
 * and says so IN THE WINDOW (finding (1) of wave 2's last review: the status
 * used to be thrown away and the refusal happened in here, where nothing can
 * be said - a fully drawn panel over a black level, and no sentence).
 *
 * NOTHING HERE CAN REFUSE FOR WANT OF A RESOURCE ANY MORE. Every claim was
 * taken and the level's record READ by ovl_level_load(), outside the bracket,
 * where a refusal is a toast; what is left is the data itself - a record that
 * names a piece the style bank has not got draws without it, exactly as an
 * unused slot does. */
static int ovl_compose(int level)
{
    unsigned base, seg, off;
    int i, slot, b0, b1, b2, b3;
    int x, yv, y, id, flags, w, h, row;

    if (!lem_rat_ok || !lem_ent_here(level) || !lem_claimed)
        return 0;
    base = (unsigned)lem_ent_u8(level, LEM_E_RAWSECT) * LEM_LVL_SIZE;

    /* THE WORLD IN RAM (SPEC.md 116.7.2's composer rows): on VGA the T
     * claim's three colour planes and the mask as plane 3, on the two shadow
     * backends the mask alone - one routine, one pass a piece. */
    lem_r_wsetup(lem_kind_id() == LEM_RKIND_VGA ? lem_cl_t : 0, lem_cl_mask);

    /* A SPECIAL LEVEL HAS NO TERRAIN LIST (measured over all four, SPEC.md
     * 116.3.1) and its picture is in the bank claim, so the list's slots
     * would index a picture as a piece table: it composes the picture and
     * then reads its ground set's bank over it, for the objects */
    if (lem_ent_u8(level, LEM_E_SPECIAL) != 0xFF) {
        if (!ovl_spec_compose(lem_ent_u8(level, LEM_E_STYLE)))
            return 0;
        i = LEM_LVL_NTERR;          /* the list below is not walked */
    } else {
        i = 0;
    }
    for (; i < LEM_LVL_NTERR; i++) {
        /* the bar: the compose's share of the minimap's well is 0..75 */
        if ((i & 3) == 0)
            ovl_load_bar((i >> 2) - (i >> 4));
        slot = LEM_LVL_TERR + 4 * i;
        b0 = ovl_pk(lem_cl_rec, base + slot);
        b1 = ovl_pk(lem_cl_rec, base + slot + 1);
        /* SPEC.md 116.3.1: a slot whose first WORD is 0xFFFF is SKIPPED and
         * never breaks the walk (Taxing 27, lemtool/REPORT.md) */
        if (b0 == 0xFF && b1 == 0xFF)
            continue;
        b2 = ovl_pk(lem_cl_rec, base + slot + 2);
        b3 = ovl_pk(lem_cl_rec, base + slot + 3);

        flags = b0 >> 4;                    /* 8 no-overwrite, 4 upside-down,
                                             * 2 erase - the format document's
                                             * own words */
        x = (((b0 & 0x0F) << 8) | b1) - 16; /* the 12-bit mask, 0x0010 = 0 */
        yv = (b2 << 1) | (b3 >> 7);         /* nine bits, bleeding into b3 */
        y = yv - ((yv >= 0x100) ? 516 : 4);
        id = b3 & 0x3F;

        row = LEM_GR_TERTAB + id * LEM_GR_TERSTRIDE;
        w = ovl_pk(lem_cl_bank, row + LEM_GT_W);
        h = ovl_pk(lem_cl_bank, row + LEM_GT_H);
        if (w == 0 || h == 0)
            continue;                       /* an unused slot in this set */
        ovl_far(lem_cl_bank,
                ovl_pk16(lem_cl_bank, row + LEM_GT_OFF),
                ovl_pk16(lem_cl_bank, row + LEM_GT_OFF + 2));
        seg = lem_far_seg;
        off = lem_far_off;
        lem_r_wpiece(seg, off, x, y, w, h, flags);
    }

    /* the objects and the steel, out of the record - BEFORE the object map
     * overwrites the claim the record is in */
    ovl_types();
    ovl_objects(base);
    ovl_objmap();
    ovl_game_init(level);
    return 1;
}

/* ovl_types - the sixteen object types of the set, the 30 lemming animation
 * rows and the six stencils, out of the two banks into bss. */
/* ovl_dims_ok - can lemsprite.inc's fixed row buffers hold a w x h picture?
 * EVERY BYTE READ OFF A DISK IS HOSTILE, and these two come straight out of a
 * band file into routines whose buffers are fixed: a row of (w + 7) / 8 bytes,
 * shifted, is one more (lb_srow, 24), DOUBLED on a CGA it is 2 x bpr + 1
 * (lb_sorb, lb_srow), and a VGA draw keeps every shifted mask row in lb_smk
 * (LEM_SMK = 800). So bpr <= 11 and (bpr + 1) x h <= 800, which also keeps
 * w x h far below the 32,767 the per-frame size is computed in. The largest
 * object in the five sets is 10 bytes x 20 rows and the tallest 3 x 71
 * (tools/os88lem.py asserts the same limits by name first, and
 * tests/unit/t_lemdat.py a third time). A picture that fails is ZEROED, and a
 * zero-width entry is never drawn. */
#define LEM_BPR_MAX 11
#define LEM_SMK_MAX 800
static int ovl_dims_ok(int w, int h)
{
    int bpr = (w + 7) >> 3;

    return bpr <= LEM_BPR_MAX && (bpr + 1) * h <= LEM_SMK_MAX;
}

static void ovl_types(void)
{
    int i, r, a, b;
    struct lem_ot *t;
    struct lem_ar *q;

    for (i = 0; i < 16; i++) {
        t = &lem_ty[i];
        r = LEM_GR_OBJTAB + (i << 4);
        t->w = (unsigned char)ovl_pk(lem_cl_bank, r + LEM_GO_W);
        t->h = (unsigned char)ovl_pk(lem_cl_bank, r + LEM_GO_H);
        t->frames = (unsigned char)ovl_pk(lem_cl_bank, r + LEM_GO_FRAMES);
        t->eff = (unsigned char)ovl_pk(lem_cl_bank, r + LEM_GO_EFFECT);
        t->sound = (unsigned char)ovl_pk(lem_cl_bank, r + LEM_GO_SOUND);
        t->start = (unsigned char)ovl_pk(lem_cl_bank, r + LEM_GO_START);
        t->anim = (unsigned char)ovl_pk(lem_cl_bank, LEM_GR_ANIMFL + i);
        t->off_lo = (unsigned)ovl_pk16(lem_cl_bank, r + LEM_GO_OFF);
        t->off_hi = (unsigned)ovl_pk16(lem_cl_bank, r + LEM_GO_OFF + 2);
        if (!ovl_dims_ok((int)t->w, (int)t->h)) {
            t->w = 0;               /* ovl_obj_draw() skips a zero width */
            t->h = 0;
        }
        t->per = (unsigned)((((unsigned)t->w * t->h) >> 3) * 5);
        /* GRND's encoding, decoded as Lemmix TMetaObject.AssignFromDos does:
         * left * 4, top * 4 - 4, width * 4, height * 4 */
        t->tl = ovl_pk16(lem_cl_bank, r + LEM_GO_TRIGL) * 4;
        t->tt = ovl_pk16(lem_cl_bank, r + LEM_GO_TRIGT) * 4 - 4;
        t->tw = ovl_pk(lem_cl_bank, r + LEM_GO_TRIGW) * 4;
        t->th = ovl_pk(lem_cl_bank, r + LEM_GO_TRIGH) * 4;
        if (t->frames == 0)
            t->frames = 1;
    }
    lem_gfx_ok = 0;
    lem_ptseg = 0;                  /* the spray: drawn only when the bank
                                     * has it (an LMNB 3 always does) */
    if (ovl_item(LEM_ITEM_SPRAY)) {
        lem_ptseg = lem_far_seg;
        lem_ptoff = lem_far_off;
    }
    if (!ovl_item(LEM_ITEM_ANIMTAB))
        return;
    a = 0;
    for (i = 0; i < LEM_NANIM; i++) {
        q = &lem_ar[i];
        b = lem_far_off + (i << 3);
        q->frames = (unsigned char)ovl_pk(lem_far_seg, b);
        q->w = (unsigned char)ovl_pk(lem_far_seg, b + 1);
        q->h = (unsigned char)ovl_pk(lem_far_seg, b + 2);
        q->bpp = (unsigned char)ovl_pk(lem_far_seg, b + 3);
        q->off = (unsigned)ovl_pk16(lem_far_seg, b + 4);
        q->per = (unsigned)ovl_pk16(lem_far_seg, b + 6);
        if (!ovl_dims_ok((int)q->w, (int)q->h) || q->bpp > 4)
            q->w = 0;               /* lem_anim_at() refuses a zero width */
    }
    if (!ovl_item(LEM_ITEM_MASKTAB))
        return;
    for (i = 0; i < LEM_NMASK; i++) {
        q = &lem_mk[i];
        b = lem_far_off + (i << 3);
        q->frames = (unsigned char)ovl_pk(lem_far_seg, b);
        q->w = (unsigned char)ovl_pk(lem_far_seg, b + 1);
        q->h = (unsigned char)ovl_pk(lem_far_seg, b + 2);
        q->bpp = (unsigned char)ovl_pk(lem_far_seg, b + 3);
        q->off = (unsigned)ovl_pk16(lem_far_seg, b + 4);
        q->per = (unsigned)ovl_pk16(lem_far_seg, b + 6);
        if (!ovl_dims_ok((int)q->w, (int)q->h))
            q->w = 0;               /* lem_mask_at() refuses it the same way */
    }
    if (!ovl_item(LEM_ITEM_ANIM))
        return;
    lem_anseg = lem_far_seg;
    lem_anoff = lem_far_off;
    if (!ovl_item(LEM_ITEM_MASKS))
        return;
    lem_mkseg = lem_far_seg;
    lem_mkoff = lem_far_off;
    lem_gfx_ok = 1;
    (void)a;
}

/* ovl_objects - Level.Loader.pas TranslateLevel's objects: 32 slots of eight
 * bytes at 0x20, BIG-ENDIAN like the rest of the record; an all-zero slot is
 * skipped. Left is the word less 16, the type is the low nibble of byte 5,
 * modifier bit 0x80 is no-overwrite and 0x40 only-on-terrain, and display
 * mode 0x8F is upside down. The 32 steel slots at 0x760 are copied out raw for
 * ovl_objmap(), because the map is about to overwrite the record. */
static unsigned char lem_steel[128];

static void ovl_objects(unsigned base)
{
    int i, k, z, info;
    unsigned r;
    struct lem_o *o;

    lem_nob = 0;
    lem_nord = -1;                  /* the drawing order is this level's */
    lem_nvis = -1;
    lem_nent = 0;
    info = 0;
    for (i = 0; i < 32; i++) {
        r = base + 0x20 + (i << 3);
        z = 0;
        for (k = 0; k < 8; k++)
            z |= ovl_pk(lem_cl_rec, r + k);
        if (z == 0)
            continue;
        o = &lem_ob[lem_nob];
        o->x = ((ovl_pk(lem_cl_rec, r) << 8) | ovl_pk(lem_cl_rec, r + 1)) - 16;
        o->y = (ovl_pk(lem_cl_rec, r + 2) << 8) | ovl_pk(lem_cl_rec, r + 3);
        o->id = (unsigned char)(ovl_pk(lem_cl_rec, r + 5) & 15);
        o->fl = 0;
        k = ovl_pk(lem_cl_rec, r + 6);
        if (k & 0x80)
            o->fl |= 4;
        if (k & 0x40)
            o->fl |= 1;
        if (ovl_pk(lem_cl_rec, r + 7) == 0x8F)
            o->fl |= 2;
        o->frame = lem_ty[o->id].start;
        if (o->frame >= lem_ty[o->id].frames)
            o->frame = 0;
        o->trig = 0;
        o->drawn = 0xFF;
        o->dirty = 0;
        o->ent = (unsigned char)(o->id == LEM_OTYPE_ENTRANCE);
        if (o->ent) {
            if (lem_nent < 4)       /* Max4EnabledEntrances */
                lem_ent[lem_nent++] = (unsigned char)lem_nob;
        } else {
            if (info < 16)          /* DisableObjectsAfter15 */
                lem_oinfo[info] = (unsigned char)lem_nob;
            info++;
        }
        lem_nob++;
    }
    lem_noinfo = info;
    for (i = 0; i < 128; i++)
        lem_steel[i] = (unsigned char)ovl_pk(lem_cl_rec, base + 0x760 + i);
    /* the release order: the ORIG set's OldEntranceABBAOrder (Game.pas:1557) */
    lem_entord[0] = 0;
    lem_entord[1] = 0;
    lem_entord[2] = 0;
    lem_entord[3] = 0;
    if (lem_nent == 2) {
        lem_entord[1] = 1;
        lem_entord[2] = 1;
    } else if (lem_nent == 3) {
        lem_entord[1] = 1;
        lem_entord[2] = 2;
        lem_entord[3] = 1;
    } else if (lem_nent == 4) {
        lem_entord[1] = 1;
        lem_entord[2] = 2;
        lem_entord[3] = 3;
    }
}

/* ovl_objmap - InitializeObjectMap (:2251): DOM_NONE everywhere, the steel,
 * then each of the first sixteen non-hatch objects' trigger rectangle - a
 * triggered trap writes its own index, everything else 128 + its effect.
 * Steel's left is the high nine bits of word 0 times 4 less 16 and its width
 * the high nibble of byte 2, in 4-pixel units plus one (TranslateLevel). */
static void ovl_objmap(void)
{
    int i, b0, b1, b2, v, n;
    struct lem_o *o;
    struct lem_ot *t;

    lem_o_setup(lem_cl_rec);
    lem_o_clear();
    for (i = 0; i < 32; i++) {
        b0 = lem_steel[i * 4];
        b1 = lem_steel[i * 4 + 1];
        b2 = lem_steel[i * 4 + 2];
        if ((b0 | b1 | b2 | lem_steel[i * 4 + 3]) == 0)
            continue;
        lem_o_fill(((b0 << 1) + (b1 >> 7)) * 4 - 16, (b1 & 0x7F) * 4,
                   (b2 >> 4) * 4 + 4, (b2 & 15) * 4 + 4, DOM_STEEL);
    }
    n = lem_noinfo < 16 ? lem_noinfo : 16;
    for (i = 0; i < n; i++) {
        o = &lem_ob[(int)lem_oinfo[i]];
        t = &lem_ty[o->id];
        v = (t->eff == 4) ? i : t->eff + 128;
        lem_o_fill((o->x & ~3) + t->tl, (o->y & ~3) + t->tt, t->tw, t->th, v);
    }
}

/* ovl_game_init - Start (:1473) for this level's header. */
static void ovl_game_init(int level)
{
    int k;

    lem_rr = lem_ent_u16(level, LEM_E_RATE);
    if (lem_rr < 1)
        lem_rr = 1;
    if (lem_rr > 99)
        lem_rr = 99;
    lem_rrmin = lem_rr;
    lem_maxlem = lem_ent_u16(level, LEM_E_COUNT);
    if (lem_maxlem > LEM_MAXLEM)
        lem_maxlem = LEM_MAXLEM;
    if (lem_maxlem < 1)
        lem_maxlem = 1;
    lem_save = lem_ent_u16(level, LEM_E_SAVE);
    lem_min = lem_ent_u16(level, LEM_E_MINUTES);
    if (lem_min < 1)
        lem_min = 1;
    if (lem_min > 99)
        lem_min = 99;
    lem_sec = 0;
    for (k = 0; k < 8; k++)
        lem_cnt[k] = lem_ent_u8(level, LEM_E_SKILLS + k);
    lem_iter = 0;
    lem_clockf = 0;
    lem_released = 0;
    lem_out = 0;
    lem_saved = 0;
    lem_inpct = 0;                  /* SetInfoLemmingsSaved(0, 1) (:1635) */
    lem_removed = 0;
    lem_rrdir = 0;
    lem_skill = LB_CLIMBER;         /* "BtnClimber; // this is not recorded
                                     * because not playing yet" */
    lem_countdown = 20;
    lem_opened = 0;
    lem_entdone = 0;
    lem_paused = 0;
    lem_glitchp = 0;
    lem_pfin = 0;
    lem_super = (lem_ent_u8(level, LEM_E_FLAGS2) & LEM_EF2_SUPER) != 0;
    lem_nuked = 0;
    lem_nukeing = 0;
    lem_nukeix = 0;
    lem_finished = 0;
    lem_timeup = 0;
    lem_cursor = 0;
    lem_infr = 0;
    lem_ndmg = 0;
    lem_tmin = lem_min;
    lem_tsec = 0;
    lem_hit_n = 0;
    lem_hit_l1 = -1;
    lem_hit_l2 = -1;
    lem_hit_last = -1;              /* Start (:1562) */
    lem_sndq = -1;
    for (k = 0; k < LEM_MM_H * LEM_MM_STRIDE; k++)
        lem_mmd[k] = 0;
}

/* ovl_postview - the results, as Lemmix src/GameScreen.Postview.pas
 * GetScreenText builds them: the top line, "You rescued", "You needed", the
 * result text the per cent earns, and the footer. The access-code line is
 * omitted with the Level Code row's fact (SPEC.md 116.8), and both branches
 * then take the failure branch's AddLineFeed(5), so the two are identical.
 *
 * TWO FORMS SINCE WAVE 5:
 *
 *   LEM_PO_SCR   the original's screen, inside the bracket (SPEC.md 116.4.3):
 *                the top line at row 0, AddLineFeed(1), the two counts,
 *                AddLineFeed(1), the result text's two lines, then :189's
 *                AddLineFeed(18 - CountChar(CR)) FORCES the footer to line 18
 *                whatever came above - and "Press right mouse button for
 *                menu" on line 19, because inside the bracket os88_mouse()'s
 *                bit 1 is this program's and the button does what the line
 *                says. The CGA's 12-row surface collapses the blank runs of
 *                this same text (ovl_scr_lines), so there is one text and two
 *                layouts of it, as there is for the preview.
 *   LEM_PO_WIN   the WINDOW's: what the launcher shows when 'f' takes the
 *                postview out of the bracket or the screens' mode was refused
 *                (lem_r_mode's failure, a normal path). The original's spacing
 *                where the box holds line 18, else one blank before the footer
 *                (the compression LEMS_GREY_EGA names); and NO menu line,
 *                because a right click in an os8088 window is the kernel's
 *                EVT_RDOWN and never reaches os88_onclick (SPEC.md 12.4) - Esc
 *                leaves that screen. */
#define LEM_PO_FOOT 18              /* the footer's forced line, :189 */
#define LEM_PO_ROWS (LEM_PO_FOOT + 1)
#define LEM_PO_WIN   0
#define LEM_PO_SCR   1

static void ovl_po_foot(int spaced)
{
    int i, n;

    if (!spaced) {
        ovl_para_nl();
        return;
    }
    /* EVERY break counts, the result texts' own '\r' with the builder's
     * '\n': Lemmix's AddLineFeed(18 - Result.CountChar(CR)) counts them all,
     * and the congratulation alone carries sixteen (wave 4 review) */
    n = 0;
    for (i = 0; i < lem_paran; i++)
        if (lem_para[i] == '\n' || lem_para[i] == '\r')
            n++;
    while (n++ < LEM_PO_FOOT)
        ovl_para_nl();
}

static int ovl_postview(int rating, int level, int form)
{
    int cnt, lc, target, done, ix, success, spaced;

    cnt = lem_ent_u16(level, LEM_E_COUNT);
    target = cnt ? (lem_ent_u16(level, LEM_E_SAVE) * 100) / cnt : 0;
    lc = lem_nuked ? lem_maxlem : cnt;  /* SetGameResult's NUKE GLITCH (:4477):
                                     * after a nuke the per cent is of the
                                     * lemmings released, not of the level's */
    done = lc ? (lem_saved * 100) / lc : 0;
    success = done >= target;
    /* rows 1..lem_rows-1 carry the window's text: the postview has no footer
     * row of its own there (lem_row_text), so a 20-row box holds line 18 */
    spaced = form == LEM_PO_SCR || (lem_rows - 1) >= LEM_PO_ROWS;
    ovl_para_reset();
    if (success && rating == LEM_RATINGS - 1 && level == LEM_PERRAT - 1) {
        ovl_para_add(lem_str(LEMS_PO_CONGRATS));
        ovl_para_nl();
        ovl_po_foot(spaced);
        ovl_para_add(lem_str(LEMS_PO_CONTINUE));
        return success;
    }
    ovl_para_add(lem_str(lem_timeup ? LEMS_PO_TIMEUP : LEMS_PO_ACCOUNTEDFOR));
    ovl_para_nl();
    ovl_para_nl();
    ovl_arg_reset();
    ovl_arg_pct((unsigned)lem_saved, (unsigned)lc);
    ovl_para_fmt(lem_str(LEMS_PO_YOURESCUED));
    ovl_para_nl();
    ovl_arg_reset();
    ovl_arg_pct((unsigned)lem_ent_u16(level, LEM_E_SAVE), (unsigned)cnt);
    ovl_para_fmt(lem_str(LEMS_PO_YOUNEEDED));
    ovl_para_nl();
    ovl_para_nl();
    /* GetResultText's ladder, word for word */
    if (done == 100)
        ix = 8;
    else if (done == 0)
        ix = 0;
    else if (done < target / 2)
        ix = 1;
    else if (done < target - 5)
        ix = 2;
    else if (done < target - 1)
        ix = 3;
    else if (done == target - 1)
        ix = 4;
    else if (done == target)
        ix = 5;
    else if (done < target + 20)
        ix = 6;
    else
        ix = 7;
    lem_po_ix = ix;                 /* the harness reads which text it was */
    ovl_para_add(lem_str(LEMS_PO_RESULT0 + ix));
    ovl_para_nl();
    ovl_po_foot(spaced);
    ovl_para_add(lem_str(success ? LEMS_PO_NEXTLEVEL : LEMS_PO_RETRYLEVEL));
    if (form == LEM_PO_SCR) {
        ovl_para_nl();
        ovl_para_add(lem_str(LEMS_PO_MENU));
    }
    return success;
}

/* ============================================================================
 * WAVE 5: THE ORIGINAL'S TWO SCREENS, INSIDE THE BRACKET (SPEC.md 116.4.3)
 *
 * Drawn in MAIN.DAT's own 16x16 purple font over its own brown tile, by
 * lemfont.inc's lem_p_* - no kernel drawing slot is legal in a foreign mode
 * (SPEC.md 53.1) - on the 640x350 surface each adapter's second mode gives
 * them. These functions say WHERE, in the original's own coordinates, and
 * never which card it is: the one decision about the card is the CGA's, whose
 * 640x200 holds 12 of the original's 22 rows, and it is made by name here
 * (lem_scr_cga) and stated on the greyed Mode row (LEMS_GREY_EGA).
 *
 * THE COLOURS - A DEPARTURE, THE OWNER'S TO ACCEPT. Lemmix's source records
 * the 1991 screen's recolour per preview line - the title Red, Number of
 * Lemmings Blue, To Be Saved Green, Release Rate Brown (GameScreen.Preview.pas
 * :301-316, aColors) - and its TFontRecolor type is commented "// not used
 * yet" (GameScreen.Base.pas:25): the recolour is unfinished in Lemmix, and
 * DrawPurpleText takes the argument and never reads it (:230-282). Every line
 * here is the font's own purple, which matches Lemmix's unfinished build and
 * not the original's look. The recolours would want hue-shifted copies of the
 * four purples in a sixteen-entry palette whose 8..15 are the level picture's
 * (lem_p_pal), and one recolour pass a glyph (SPEC.md 116.4.3.1).
 *
 * ATTRIBUTION. The layout is Lemmix's - GameScreen.Preview.pas BuildScreen
 * and GetScreenLinesAndColors, GameScreen.Postview.pas BuildScreen and
 * GetScreenText - and the words are the band's (LEMSTR.LEM). Lemmings is (C)
 * 1991 DMA Design / Psygnosis.
 * ==========================================================================*/
#define LEM_PV_PICH    80           /* the picture's rows: Lemmix's previewLevel
                                     * Height without KeepLevelRatio, which is
                                     * not in its MiscOptions DEFAULT */
#define LEM_PV_PICHC   64           /* ...on the CGA's 200 rows (GREY_EGA) */
#define LEM_PV_Y       82           /* y := 80 + 2 */
#define LEM_PV_YC      66           /* ...64 + 2 on the CGA */
#define LEM_SCR_PITCH  16
#define LEM_PH_ROWS   350           /* the original's surface */
#define LEM_PCGA_ROWS 200           /* ...and what a CGA shows of it */

/* for each of ovl_fmt's seven lines, its row in GetScreenLinesAndColors'
 * sixteen (aLines[]), so line i is drawn at y = 82 + lem_pv_slot[i] * 16.
 * aLines[02] is the Replay string, empty in ordinary play, and no line maps
 * to it; the footer is row 15 and is drawn by the caller; an empty line (a
 * special level's Style) is skipped by the caller, not marked here. */
static const signed char lem_pv_slot[LEM_PV_LINES] = {
    0, 3, 5, 7, 9, 11, 13
};

/* ovl_scr_id - THE SECOND MODE, the one the two screens are drawn in
 * (SPEC.md 116.4.3's table): mode 12h on a VGA, the Hercules' own 720x348 on
 * a Hercules, mode 6 on a CGA and on an EGA (kernel/fsx.inc gives an EGA the
 * CGA-compatible modes only). -1 = this display offers none of them, and the
 * session goes straight to the level and leaves the postview to the window -
 * lem_r_mode's failure as a normal path (ovl_r_mode). */
static int ovl_scr_id(void)
{
    if (lem_vidkind == OS88_VID_HERC)
        return (lem_caps > 0 && (lem_caps & (1 << OS88_FSXM_HERC)))
             ? OS88_FSXM_HERC : -1;
    if (lem_caps > 0 && (lem_caps & (1 << OS88_FSXM_VGA12)))
        return OS88_FSXM_VGA12;
    if (lem_caps > 0 && (lem_caps & (1 << OS88_FSXM_CGA640)))
        return OS88_FSXM_CGA640;
    return -1;
}

/* ovl_scr_cga - is that surface the CGA's 640x200? */
static int ovl_scr_cga(void)
{
    return ovl_scr_id() == OS88_FSXM_CGA640;
}

/* ovl_scr_pal - the screens' sixteen colours (VGA; lem_p_pal is a no-op on
 * the other two): the menu palette out of LEMMAIN's header, the level's own
 * custom eight out of the bank claim - which holds a SPECIAL level's picture
 * header before its compose and the style bank with the picture's colours
 * poked over it after (ovl_spec_compose), at the same offset either way. */
static void ovl_scr_pal(void)
{
    lem_p_pal(lem_cl_main, LEM_MN_MENUPAL, lem_cl_bank, LEM_GR_CUSTOM);
}

/* ovl_scr_text - one line of text at surface row y: centred, or at column
 * `col` (16 pixels each). */
static void ovl_scr_text(const char *t, int n, int col, int y)
{
    int x;

    if (!ovl_item(LEM_ITEM_PURPLE))
        return;
    if (col < 0) {
        x = 320 - 8 * n;            /* DrawPurpleTextCentered: (640 - 16n) / 2 */
        if (x < 0)
            x = 0;
    } else {
        x = col << 4;
    }
    lem_p_text(lem_far_seg, lem_far_off, x, y, t, n);
}

/* ovl_scr_tile - the brown tile from row y down (TileBackgroundBitmap). */
static void ovl_scr_tile(int y)
{
    if (ovl_item(LEM_ITEM_BROWN))
        lem_p_tile(lem_far_seg, lem_far_off, y, LEM_PH_ROWS);
}

/* ovl_pv_draw - the preview screen's background and its lines, the picture's
 * well left black for the compose (GameScreen.Preview.pas BuildScreen:
 * target.Clear(0), the picture into 0..79, TileBackgroundBitmap(0, 80), the
 * lines from y = 82 at 16 a line, the first fifteen at x = 0 - the ten-space
 * IndentStr is IN the value lines - and the sixteenth centred). The CGA keeps
 * every line's 16-pixel pitch and true width and compresses only the blank
 * lines out: eight content lines and a 64-row picture are 192 of its 200. */
static void ovl_pv_draw(int level)
{
    int i, y, n, cga, k;

    cga = ovl_scr_cga();
    lem_p_clear(0, cga ? LEM_PV_PICHC : LEM_PV_PICH, lem_scr_fresh);
                                    /* the well: the tile covers the rest,
                                     * and a VGA's margins only after a set
                                     * (lemfont.inc) */
    ovl_scr_pal();
    ovl_scr_tile(cga ? LEM_PV_PICHC : LEM_PV_PICH);
    k = 0;
    for (i = 0; i < LEM_PV_LINES; i++) {
        ovl_fmt(i, level, lem_line, sizeof(lem_line));
        if (lem_line[0] == 0)
            continue;               /* a special level's Style line */
        y = cga ? LEM_PV_YC + k * LEM_SCR_PITCH
                : LEM_PV_Y + lem_pv_slot[i] * LEM_SCR_PITCH;
        n = (int)os88_strlen(lem_line);
        /* the value rows' IndentStr is ten SPACES, which DrawPurpleText
         * advances over without drawing - so it is a column, not text */
        ovl_scr_text(lem_line, n, lem_pv[i].indent ? 10 : 0, y);
        k++;
    }
    y = cga ? LEM_PV_YC + k * LEM_SCR_PITCH : LEM_PV_Y + 15 * LEM_SCR_PITCH;
    lem_scr_fy = y;                 /* the harness checks the footer's row */
    ovl_scr_text(lem_str(LEMS_PV_PRESS),
                 (int)os88_strlen(lem_str(LEMS_PV_PRESS)), -1, y);
}

/* ovl_pv_pic - one row of the shrunk level picture (lemfont.inc lem_p_mini):
 * row k of 80 is world row 2k, of the CGA's 64 world row 5k/2 - 160 rows
 * either way. */
static void ovl_pv_pic(int k)
{
    if (ovl_scr_cga())
        lem_p_mini(k, (k * 5) >> 1);
    else
        lem_p_mini(k, k << 1);
}

/* ovl_scr_lines - lem_para's lines, every one CENTRED (DrawPurpleTextCentered,
 * GameScreen.Postview.pas:62), from row y at 16 a line. On the CGA the runs of
 * blank lines collapse to ONE and the leading ones go, and the block is
 * centred in the 200 rows: the postview is ten lines there, the
 * congratulation ten (SPEC.md 116.4.3). */
static void ovl_scr_lines(int y)
{
    int i, s, n, cga, blank, pass, rows;

    cga = ovl_scr_cga();
    rows = 0;
    for (pass = cga ? 0 : 1; pass < 2; pass++) {
        if (pass == 1 && cga)
            y = (LEM_PCGA_ROWS - rows * LEM_SCR_PITCH) >> 1;
        blank = 1;                  /* a leading blank run is dropped */
        s = 0;
        for (i = 0; i <= lem_paran; i++) {
            if (i < lem_paran && lem_para[i] != '\n' && lem_para[i] != '\r')
                continue;
            n = i - s;
            if (n == 0 && cga) {
                if (!blank) {
                    blank = 1;
                    if (pass == 1)
                        y += LEM_SCR_PITCH;
                    else
                        rows++;
                }
            } else {
                blank = 0;
                if (pass == 1) {
                    if (n)
                        ovl_scr_text(lem_para + s, n, -1, y);
                    lem_scr_fy = y;
                    y += LEM_SCR_PITCH;
                } else {
                    rows++;
                }
            }
            s = i + 1;
        }
    }
}

/* ovl_po_draw - the postview: the tile over the whole surface and the text
 * from y = 16 (GameScreen.Postview.pas BuildScreen: TileBackgroundBitmap(0,
 * 0), DrawPurpleTextCentered(Temp, GetScreenText, 16)). */
static int ovl_po_draw(int level)
{
    int success;

    success = ovl_postview(lem_rating, level, LEM_PO_SCR);
    lem_p_clear(0, 0, lem_scr_fresh);   /* no rows: a VGA's margins, and
                                     * only after a set; the tile is the rest */
    ovl_scr_pal();
    ovl_scr_tile(0);
    ovl_scr_lines(16);
    return success;
}
