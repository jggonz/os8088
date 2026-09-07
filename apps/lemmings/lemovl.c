/* ============================================================================
 * os8088 - apps/lemmings/lemovl.c   (#included by lemmings.c)
 *
 * EVERYTHING ovl_*, WHICH IS EVERYTHING IN LEMMINGS.OVL (SPEC.md 73.14, 92.6).
 *
 * A function whose name begins with `ovl_` has its CODE emitted into a second
 * segment that ships beside the package as LEMMINGS.OVL and is far-called both
 * ways; every global, literal and bss byte it names stays RESIDENT and
 * DS-relative, which is what makes the mechanism possible in C at all. The
 * rename is the entire mechanism - there is nothing else to declare
 * (LESSONS.md 5).
 *
 * THE SPLIT IS BY FREQUENCY AND NEVER BY SIZE. Nothing here runs on a game
 * frame and nothing here runs on a keystroke that could not afford a far call:
 * the About card, the sentence behind a greyed control, the preview screen's
 * text, the launcher's chrome, the progress file and - in wave 2 -
 * ovl_compose(), the terrain-list walk that runs once per level and never
 * during play. At cword's MEASURED 5.7 bytes a line of resident C this program
 * does not fit one segment and never could, so the split is from wave 1 rather
 * than from the wave the ceiling binds in (SPEC.md 92.6).
 *
 * AN OVERLAY FUNCTION ANSWERS A STATUS AND 0 MEANS IT DID NOT HAPPEN. A refused
 * load - no heap, no file, a stale module, a worker task asking - toasts the
 * reason and returns without running (SPEC.md 47). Every caller here is
 * designed so 0 is a normal path, and lem_ovl remembers a refusal so the
 * toast happens ONCE rather than on every keystroke.
 *
 * NEVER FROM A WORKER, AND NEVER ITS ADDRESS. The worker gate is decided from
 * SP rather than from a flag (LESSONS.md 5); this package has no worker, which
 * is deliberate, and tools/cc8086.py refuses an ovl_ address by name.
 *
 * ATTRIBUTION. The preview wording, the level names and the greying sentences
 * this file assembles are the original's own strings and the converter's own
 * prose, read out of LEMSTR.LEM (SPEC.md 92.3). Lemmings is (C) 1991 DMA Design
 * / Psygnosis; the layout of the preview screen is Lemmix
 * GameScreen.Preview.pas GetScreenLinesAndColors and its wording
 * Base.Strings.pas:411-418. README.TXT carries the full list.
 * ==========================================================================*/

/* ovl_ready - the cheapest thing in the module, and its only job is to BE in
 * the module. Calling it from the first wake forces LEMMINGS.OVL resident while
 * a window is up and a toast is an ordinary thing; from then on cc_ovneed is a
 * four-byte no-op and no later call can reach its refusal path. That matters
 * more than it looks: cc_ovneed TOASTS ITS OWN REFUSAL, a kernel drawing call
 * this C never wrote and cannot intercept, and once wave 2's bracket owns the
 * screen a kernel toast is painted with DESKTOP geometry into a foreign mode
 * (SPEC.md 92.5, 53.1). It cannot be called from os88_main() either - there is
 * no instance yet to resolve a module for (SPEC.md 73.14, LESSONS.md 13). */
static int ovl_ready(void)
{
    return 1;
}

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
    lem_arg_reset();
    if (line == 0) {
        lem_arg_num((unsigned)(level + 1));
        lem_arg_add(lem_trim(lem_ent_name(level)));
    } else if (line == 1) {
        lem_arg_num((unsigned)lem_ent_u16(level, LEM_E_COUNT));
    } else if (line == 2) {
        /* A PERCENTAGE, WHICH IS WHAT THE ORIGINAL'S SCREEN SAYS - "10% To Be
         * Saved" on Fun 1 and not "1". Lemmix GameScreen.Preview.pas:275-278
         * with LemmingsPercentages, which is in TMiscOptions.DEFAULT; the raw
         * count is the non-default arm. See lem_arg_pct(). */
        lem_arg_pct((unsigned)lem_ent_u16(level, LEM_E_SAVE),
                    (unsigned)lem_ent_u16(level, LEM_E_COUNT));
    } else if (line == 3) {
        lem_arg_num((unsigned)lem_ent_u16(level, LEM_E_RATE));
    } else if (line == 4) {
        lem_arg_num((unsigned)lem_ent_u16(level, LEM_E_MINUTES));
    } else if (line == 5) {
        lem_arg_add(lem_str((int)lem_rating_str[lem_rating]));
    } else {
        lem_arg_add(lem_str(lem_style_name(lem_ent_u8(level, LEM_E_STYLE))));
    }
    lem_fmt(dst, cap, lem_str((int)lem_pv[line].strid));
}

/* ovl_preview - the level preview screen's TEXT, into the paragraph buffer.
 *
 * WAVE 1 DRAWS IT IN THE WINDOW, in the kernel's own face. Wave 5 draws this
 * same content inside SPEC.md 53's bracket, in the second foreign mode each
 * adapter needs to hold the original's 640x350 surface, lettered in MAIN.DAT's
 * own 16x16 purple font over its own brown background (SPEC.md 92.4.3) - the
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

    lem_para_reset();
    for (i = 0; i < LEM_PV_LINES; i++) {
        if (i == 1 || (spaced && i > 1))
            lem_para_nl();
        if (i == 1 && spaced)
            lem_para_nl();          /* the original's aLines[02] */
        if (lem_pv[i].indent)
            lem_para_add(lem_str(LEMS_PV_INDENT));
        ovl_fmt(i, level, lem_line, sizeof(lem_line));
        lem_para_add(lem_line);
        lem_para_nl();
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
    int style, spec;

    if (strid == LEMS_NONE)
        return 0;
    lem_arg_reset();

    if (strid == LEMS_MISS_STYLE) {
        /* "%s is not on a %s disk: the %s graphic set is %s bytes = %s clusters
         *  of this disk's %s, with %s already spent." */
        style = lem_ent_u8(level, LEM_E_STYLE);
        if (style < 0 || style > 4)
            style = 0;                  /* the byte comes off a disk and every
                                         * byte read off one is hostile
                                         * (CLAUDE.md); lem_cost_row would
                                         * answer -1 and the u32 read would be
                                         * one byte before the buffer */
        lem_arg_add(lem_bandname(LEM_BANK_STYLE, style));
        lem_arg_add(lem_geom());
        lem_arg_add(lem_str(lem_style_name(style)));
        lem_arg_u32(lem_manbuf, lem_cost_row(LEM_BANK_STYLE, style) +
                                LEM_COST_BYTES);
        lem_arg_num((unsigned)lem_cost_clus(LEM_BANK_STYLE, style));
        lem_arg_num((unsigned)lem_man_u16(LEM_MAN_DISKCLUS));
        lem_arg_num((unsigned)lem_man_u16(LEM_MAN_SETCLUS));
    } else if (strid == LEMS_MISS_SPEC) {
        /* "%s is not on a %s disk: the four special pictures are %s bytes = %s
         *  clusters of this disk's %s, with %s already spent." */
        spec = lem_ent_u8(level, LEM_E_SPECIAL);
        if (spec > 3)
            spec = 0;
        lem_arg_add(lem_bandname(LEM_BANK_SPEC, spec));
        lem_arg_add(lem_geom());
        lem_arg_u32(lem_manbuf, LEM_MAN_SPECBYTE);
        lem_arg_num((unsigned)lem_man_u16(LEM_MAN_SPECCLUS));
        lem_arg_num((unsigned)lem_man_u16(LEM_MAN_DISKCLUS));
        lem_arg_num((unsigned)lem_man_u16(LEM_MAN_SETCLUS));
    } else if (strid == LEMS_MISS_ROOM) {
        /* "This level's own record is not on a %s disk: the graphic sets and
         *  the levels that fit took %s of this disk's %s clusters, and a group
         *  of eight more records costs %s." */
        lem_arg_add(lem_geom());
        lem_arg_num((unsigned)lem_man_u16(LEM_MAN_SETCLUS));
        lem_arg_num((unsigned)lem_man_u16(LEM_MAN_DISKCLUS));
        lem_arg_num((unsigned)lem_man_u16(LEM_MAN_LEVCLUS));
    }

    lem_para_reset();
    if (labelid != LEMS_NONE) {
        lem_para_add(lem_str(labelid));
        lem_para_nl();
        lem_para_nl();
    } else if (level >= 0 && lem_rat_ok) {
        lem_setn(lem_num2, sizeof(lem_num2), (unsigned)(level + 1));
        lem_para_add(lem_num2);
        lem_para_add(" ");
        lem_para_add(lem_trim(lem_ent_name(level)));
        lem_para_nl();
        lem_para_nl();
    }
    /* Straight into the paragraph and not through lem_line: a fact is 200-260
     * characters and lem_line is one screen row wide. */
    lem_para_fmt(lem_str(strid));
    return 1;
}

/* --- the About card (SPEC.md 12.2, 20.5.1.1, 92.1's last row) --------------
 * TWELVE ROWS IS THE CEILING and it is a 640x200 number: the panel clamps to
 * the content box but a control's y is 6 + row*10 and nothing clamps THAT, so a
 * 19-row card put its OK button on the DESKTOP on CGA and Hercules
 * (LESSONS.md 8). This one is nine.
 *
 * IT CARRIES THE PRODUCT, WHAT THIS PORT IS, AND THE TWO PRINCIPALS, and
 * nothing about how the build renders: the frame rate goes to PERFORMANCE.md
 * and SPEC.md 92.7, the synthesised tones to the greyed Music row and to
 * README.TXT. Lemmix's SCredits block is EIGHT lines on its own, which with a
 * product and a port line is already twelve, so the full attribution list lives
 * in README.TXT beside the package and this names the two principals and points
 * at it (SPEC.md 92.2).
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
    lem_ab[1] = "";
    lem_ab[2] = lem_ab1;
    lem_ab[3] = lem_ab2;
    lem_ab[4] = "";
    lem_ab[5] = lem_str(LEMS_CREDIT1);
    lem_ab[6] = lem_str(LEMS_CREDIT2);
    /* THE RIGHTSHOLDER, AND IT REPLACES A BLANK SPACER RATHER THAN ADDING A
     * ROW. The card is capped at twelve (SPEC.md 92.1) and this uses nine
     * either way, so the CGA card - the narrow one, and already nearly full -
     * does not grow by a pixel. apps/cword/cwovl.c names Microsoft on cword's
     * card and this port's posture is cword's (SPEC.md 92.2). */
    lem_ab[7] = lem_str(LEMS_COPYRIGHT);
    lem_ab[8] = lem_str(LEMS_README);
    lem_ab[9] = 0;
    os88_about_card(win, lem_ab);
}

/* --- progress (SPEC.md 19.9, 92.9) -----------------------------------------
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
 * is LEMS_GREY_SAVE. See the note in lemmings.c's header: the band's sentence
 * names the live CD specifically while the predicate here is the broader "the
 * write path refused", and closing that gap is a one-line change to
 * tools/os88lem.py's string table, which is another agent's file. */
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
        for (i = 0; i < LEM_RATINGS; i++)
            lem_prog[i] = (int)lem_savbuf[4 + i] - 1;
    }
    return 1;
}

static int ovl_progress_write(int rating, int level)
{
    int i, ok;

    if (rating < 0 || rating >= LEM_RATINGS)
        return 0;
    if (level > lem_prog[rating])
        lem_prog[rating] = level;
    lem_savbuf[0] = 'L';
    lem_savbuf[1] = 'E';
    lem_savbuf[2] = 'M';
    lem_savbuf[3] = 1;
    for (i = 0; i < LEM_RATINGS; i++)
        lem_savbuf[4 + i] = (unsigned char)(lem_prog[i] + 1);
    if (!ovl_data_enter())
        return 0;
    ok = os88_file_write(lem_f_sav, lem_savbuf, LEM_SAV_SIZE) == 0;
    ovl_data_leave();
    return ok;
}

/* ============================================================================
 * WAVE 2: ovl_compose - THE TERRAIN LIST, ONCE PER LEVEL (SPEC.md 92.4)
 *
 * IT IS IN THE MODULE BECAUSE IT RUNS ONCE PER LEVEL AND NEVER DURING PLAY,
 * which is SPEC.md 73.14's split by FREQUENCY exactly. It is also the largest
 * single piece of decision-making in the program - 400 slots, four flag bits,
 * a bank table lookup and a clip per piece - and none of it is on any path a
 * keystroke or a frame reaches.
 *
 * IT IS CALLED FROM INSIDE THE BRACKET, and that is safe for one reason and
 * one only: the module was forced RESIDENT before the bracket was entered
 * (lem_play, SPEC.md 92.5). cc_ovneed toasts its own refusal, and a kernel
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
 * the y boundary at 0x100 - are SPEC.md 92.3.1's, measured over all 120 levels
 * in lemtool/REPORT.md. Lemmings is (C) 1991 DMA Design / Psygnosis.
 * ==========================================================================*/

/* ovl_far_add - lem_far_seg/off advanced by `n` bytes, renormalised.
 *
 * A piece's offset inside the bank is a DWORD (the bank is bigger than a
 * segment) and this dialect has no `long`, so the only 32-bit arithmetic in
 * this program is lem_far()'s and this. `n` is at most 3 * 1,024 here, so the
 * sum cannot overflow the 16-bit offset it is added to. */
static void ovl_far_add(unsigned n)
{
    unsigned o = lem_far_off + n;

    lem_far_seg += o >> 4;
    lem_far_off = o & 15;
}

/* ovl_compose - 0 = nothing was drawn and the caller says so IN A WINDOW.
 *
 * The transient 16 KB claim is taken and freed inside this call. A whole
 * LEMLV<n>.LEM is read rather than the 2,048 bytes wanted, because
 * os88_file_read_at()'s offset must be a whole number of CLUSTERS and the
 * cluster is 512 on two geometries, 1,024 on the other two and neither on the
 * RAM disk The Wire unpacks this onto (SPEC.md 92.3.2). One extra revolution
 * once per level against a read that is wrong on one medium in three. */
static int ovl_compose(int level)
{
    unsigned rec, base, seg, off;
    int i, slot, b0, b1, b2, b3;
    int x, y, yv, id, flags, w, h, row, stride;

    if (!lem_rat_ok || !lem_ent_here(level))
        return 0;

    rec = os88_mem_claim(LEM_KB_REC);
    if (rec == 0)
        return 0;

    lem_f_scratch[0] = 'L';
    lem_f_scratch[1] = 'E';
    lem_f_scratch[2] = 'M';
    lem_f_scratch[3] = 'L';
    lem_f_scratch[4] = 'V';
    lem_f_scratch[5] = (char)('0' + (lem_ent_u8(level, LEM_E_RAWFILE) % 10));
    lem_f_scratch[6] = '.';
    lem_f_scratch[7] = 'L';
    lem_f_scratch[8] = 'E';
    lem_f_scratch[9] = 'M';
    lem_f_scratch[10] = 0;
    if (os88_file_read_seg(lem_f_scratch, rec,
                           (unsigned)(LEM_LVL_GROUP * LEM_LVL_SIZE)) == 0) {
        os88_mem_free(rec);
        return 0;
    }
    base = (unsigned)lem_ent_u8(level, LEM_E_RAWSECT) * LEM_LVL_SIZE;

    for (i = 0; i < LEM_LVL_NTERR; i++) {
        /* THE ONLY THING THE USER CAN SEE FOR THE NEXT TWENTY-FIVE SECONDS
         * (SPEC.md 92.7.1 conclusion 2). One cell of the loading bar every
         * fourth slot is one lb_rpix against 53.8 ms for the piece beside it,
         * and it is a call from the module back into the resident image, which
         * is an ordinary far call the toolchain makes (never the reverse: an
         * ovl_ ADDRESS is refused by name). */
        if ((i & 3) == 0)
            lem_load_bar(i >> 2);
        slot = LEM_LVL_TERR + 4 * i;
        b0 = lem_pk(rec, base + slot);
        b1 = lem_pk(rec, base + slot + 1);
        /* SPEC.md 92.3.1: a slot whose first WORD is 0xFFFF is SKIPPED and
         * never breaks the walk. Breaking renders Taxing 27 with 68 of its
         * 395 pieces (lemtool/REPORT.md measured it). */
        if (b0 == 0xFF && b1 == 0xFF)
            continue;
        b2 = lem_pk(rec, base + slot + 2);
        b3 = lem_pk(rec, base + slot + 3);

        flags = b0 >> 4;                    /* 8 no-overwrite, 4 upside-down,
                                             * 2 erase - the format document's
                                             * own words */
        x = (((b0 & 0x0F) << 8) | b1) - 16; /* the 12-bit mask, and 0x0010 = 0 */
        yv = (b2 << 1) | (b3 >> 7);         /* nine bits, bleeding into b3 */
        y = yv - ((yv >= 0x100) ? 516 : 4);
        id = b3 & 0x3F;

        row = LEM_GR_TERTAB + id * LEM_GR_TERSTRIDE;
        w = lem_pk(lem_cl_bank, row + LEM_GT_W);
        h = lem_pk(lem_cl_bank, row + LEM_GT_H);
        if (w == 0 || h == 0)
            continue;                       /* an unused slot in this set */

        lem_far(lem_cl_bank,
                lem_pk16(lem_cl_bank, row + LEM_GT_OFF),
                lem_pk16(lem_cl_bank, row + LEM_GT_OFF + 2));
        seg = lem_far_seg;
        off = lem_far_off;

        /* THE PICTURE FIRST AND THE MASK SECOND, and the order is load-bearing:
         * the no-overwrite flag means "draw only where there is no terrain
         * yet", and the cheapest place to ask that is the solid mask, which at
         * this instant still describes the world WITHOUT this piece
         * (lemblit.inc's lb_pmask). */
        lem_r_piece(seg, off, x, y, w, h, flags);

        stride = (w >> 3) * h;
        ovl_far_add((unsigned)(3 * stride));    /* plane 3 IS the mask */
        lem_m_piece(lem_far_seg, lem_far_off, x, y, w, h, flags);
    }

    os88_mem_free(rec);
    return 1;
}
