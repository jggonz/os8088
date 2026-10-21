/* ============================================================================
 * os8088 - apps/lemmings/lemtab.c   (#included by lemmings.c - SPEC.md 73.1)
 *
 * CONST TABLES AND FIELD OFFSETS ONLY - nothing here is executable except the
 * three one-line lookups at the foot, which exist so that no caller indexes a
 * table by hand.
 *
 * A STRUCT TABLE MAY BE INDEXED; ONLY COPYING IS FORBIDDEN (LESSONS.md 4).
 * The first version of cword's menu table was five PARALLEL ARRAYS "so that
 * nothing could be copied by accident", and it was wrong on its first day -
 * four items dropped and every attribute below them out of step. A
 * `static const struct` indexed by row copies nothing and is the safe option,
 * not the dangerous one.
 *
 * ATTRIBUTION. The rating and style names, the preview wording and every
 * greying fact this file names by id are the original's own strings or this
 * port's own text, and they live in LEMSTR.LEM rather than here (SPEC.md
 * 116.3); each carries the reference file that defines it in a comment in
 * tools/os88lem.py's table and in the generated build/lemstr.h. Lemmings is
 * (C) 1991 DMA Design / Psygnosis; README.TXT carries the full list.
 *
 * WHAT LATER WAVES ADD HERE, so the shape is known before it arrives: the 4x30
 * level order with its odd-table flags, the 28-entry animation metadata, the
 * 16-entry float table, the action dispatch and its 18 draw-offset pairs, the
 * trigger-effect ids, the twelve button hit boxes, the five status-line write
 * offsets, the 38-entry status-font index map and the nine result-tier
 * thresholds. What is here now is what the LAUNCHER reads.
 * ==========================================================================*/

/* --- scan codes, the launcher's own -----------------------------------------
 * The PLAY SCREEN's map is the original's - F1 slower, F2 faster, F3-F10 the
 * eight skills, Ctrl+F1/F2 the release-rate extremes (Lemmix
 * GameScreen.Player.pas:519-531) - and it arrives inside the bracket with the
 * play screen in wave 3. These eight are the DESKTOP WINDOW's, and they are
 * the desktop's own conventions rather than anything of Lemmings'. */
#define LEM_SC_HOME   0x47
#define LEM_SC_UP     0x48
#define LEM_SC_PGUP   0x49
#define LEM_SC_LEFT   0x4B
#define LEM_SC_RIGHT  0x4D
#define LEM_SC_END    0x4F
#define LEM_SC_DOWN   0x50
#define LEM_SC_PGDN   0x51

/* --- what lem_hit() answers -------------------------------------------------
 * Ranges rather than a list, so a caller subtracts a base and gets an index.
 * LEM_HIT_ROW is last and open-ended: it is +row within the VISIBLE page, not
 * +level, because the page is sized from the live window (LESSONS.md 8). */
#define LEM_HIT_NONE    0
#define LEM_HIT_PLAY    1
#define LEM_HIT_TAB     4          /* +0..3, the four ratings */
#define LEM_HIT_STATE   8          /* +0..3, the four state rows */
#define LEM_HIT_ROW    16          /* +0.., a visible level row */

/* --- LEMMAN.LEM's header, field by field (docs/lemband-format.md) -----------
 * Byte offsets rather than a struct, because a struct read out of a byte buffer
 * means either a cast that assumes alignment or a copy - and a copy is a string
 * instruction, which is refused (SPEC.md 73.5.1, os88.h rule 2). lem_u16() and
 * ovl_u8() are the only things that read them.
 *
 * THE FILE IS 512 BYTES AND IS READ WHOLE, with os88_file_read() and never
 * os88_file_read_at(): that call's offset AND cap must each be a whole number
 * of CLUSTERS, the cluster is 512 bytes on a 1.44MB or 1.2MB disk and 1,024 on
 * a 720KB or 360KB one (tools/os88disk.py:124), and it is neither of those on
 * the RAM disk The Wire unpacks this package onto (SPEC.md 116.3.2). A whole
 * small file has no alignment rule of any kind. */
#define LEM_MAN_MAGIC     0        /* "OS88LEM" and a NUL */
#define LEM_MAN_VERSION   8
#define LEM_MAN_NLEVELS  10
#define LEM_MAN_LESIZE   12
#define LEM_MAN_LEOFF    14        /* the first entry IN A RATING FILE = 0 */
#define LEM_MAN_STYLES   16        /* byte: bit n = LEMGRn is on this disk */
#define LEM_MAN_SPECIALS 17        /* byte: bit n = special n is here */
#define LEM_MAN_SETKB    18        /* KILOBYTES: the 1.44MB set is ~962,000
                                    * bytes and a word cannot hold it. The
                                    * exact 32-bit count is at 42 and this
                                    * dialect has no `long` to read it with */
#define LEM_MAN_SETCLUS  20        /* clusters ALREADY SPENT on this disk */
#define LEM_MAN_GEOM     22        /* "1.44MB" / "720KB" / "360KB" / "1.2MB" */
#define LEM_MAN_STRBYTES 30
#define LEM_MAN_NSTYLES  32
#define LEM_MAN_NPARTS   34        /* four words, one per special picture */

/* ...and the block docs/lemband-format.md reserves and zeroes, which the
 * converter fills with THE COST TABLE (tools/os88lem.py build_manifest). It is
 * here because the three "not on this disk" templates ask for numbers about
 * banks that are NOT on the disk, so their size cannot be measured from it. */
#define LEM_MAN_STPARTS  46        /* five bytes, parts per style bank */
#define LEM_MAN_NLEVFILE 52
#define LEM_MAN_NSTRINGS 54
#define LEM_MAN_DISKCLUS 56        /* this disk's data clusters (713 at 720KB) */
#define LEM_MAN_LEVCLUS  58        /* what one group of eight records costs */
#define LEM_MAN_COSTS    60        /* NINE rows of 8, styles 0-4 then the four
                                    * specials, present or not:
                                    *   0 dword bytes   4 word clusters
                                    *   6 byte parts    7 byte present */
#define LEM_COST_STRIDE   8
#define LEM_COST_BYTES    0
#define LEM_COST_CLUS     4
#define LEM_COST_PARTS    6
#define LEM_COST_HERE     7
#define LEM_MAN_SPECBYTE 132       /* all four pictures together, dword */
#define LEM_MAN_SPECCLUS 136

#define LEM_MAN_VER       1
#define LEM_MAN_SIZE    512

/* --- a 64-byte level entry, 30 of them per rating file ---------------------- */
#define LEM_E_RATING      0
#define LEM_E_NUMBER      1
#define LEM_E_STYLE       2
#define LEM_E_SPECIAL     3        /* the VGASPEC index, or 0xFF */
#define LEM_E_RATE        4
#define LEM_E_COUNT       6
#define LEM_E_SAVE        8
#define LEM_E_MINUTES    10
#define LEM_E_SKILLS     12        /* eight bytes, in the panel's order */
#define LEM_E_STARTX     20
#define LEM_E_RAWFILE    22
#define LEM_E_RAWSECT    23
#define LEM_E_FLAGS      24        /* bit 0 = playable on THIS disk */
#define LEM_E_MISSING    25        /* LEMS_MISS_* naming what is missing, or 0 */
#define LEM_E_ODDTABLE   26
#define LEM_E_FLAGS2     28        /* bit 0 = SUPERLEMMING (wave 4) */
#define LEM_E_NAME       32        /* the original's 32 bytes. A NAME OF EXACTLY
                                    * 32 CHARACTERS HAS NO TERMINATOR - four of
                                    * the 120 are that long - so a reader takes
                                    * at most 32 bytes and stops at a NUL or at
                                    * 32 (tools/os88lem.py, tests/unit/
                                    * t_lemdat.py caught it) */
#define LEM_E_NAMELEN    32

#define LEM_EF_HERE    0x01
#define LEM_EF2_SUPER  0x01        /* LVL 0x001E-0x001F = 0xFFFF: Lemmix
                                    * Level.Loader.pas:104. No level of the 120
                                    * sets it (measured, tools/os88lem.py) */

/* --- LEMSTR.LEM's header ---------------------------------------------------- */
#define LEM_STR_MAGIC   0          /* "LSTR" */
#define LEM_STR_COUNT   4
#define LEM_STR_DIR     6          /* count words, each an offset from the START
                                    * OF THE FILE of a NUL-terminated string */

/* --- the band file names ----------------------------------------------------
 * Uppercase 8.3 in [A-Z0-9_-] (SPEC.md 116.3.2), and resolved in the LAUNCHING
 * instance's own directory (SPEC.md 19.2.1, 73.14) - which is why the package
 * and every band file are in ONE folder on every disk (SPEC.md 116.6). The
 * overlay is a PART inside LEMMINGS.O88 and no file of its own.
 *
 * The two patched ones are not const: lem_rating_load() writes the rating's
 * digit into lem_f_rat, and ovl_bandname() writes a set number and a part digit
 * into lem_f_scratch when a greyed row has to NAME the file that is missing.
 * Both are single-buffer and neither may be live across a call that reuses it
 * (LESSONS.md 13's shared-scratch trap). */
static const char lem_f_man[] = "LEMMAN.LEM";
static const char lem_f_str[] = "LEMSTR.LEM";
static char       lem_f_rat[] = "LEMR0.LEM";
#define LEM_F_RAT_DIGIT 4

static char lem_f_scratch[14];

/* --- the four state rows, and the FACT that greys each one ------------------
 * SPEC.md 47: grey a FACT, never a guess, and the predicate that greys the
 * control is the one the action itself would refuse on (rule 4).
 *
 * Mode is the row that is not always greyed: it names the adapter and the mode
 * this display can play in, and greys where the 320x200x16 mode has no FSXM id
 * on it - which is what os88_fsx_caps() answers, asked with OUR window, and is
 * the same bit os88_fsx_mode() would refuse on. Music and Level Code are greyed
 * on EVERY machine, because what greys them is a fact about the DATA (SPEC.md
 * 116.8) and not about the hardware. Save Progress is greyed when the write path
 * refuses.
 *
 * The strings are in the band; this is the id map. */
struct lem_staterow {
    unsigned char label;            /* LEMS_ROW_* - the label on the row */
    unsigned char fact;             /* LEMS_GREY_* - the fact that greys it */
};

static const struct lem_staterow lem_state[LEM_STATE_ROWS] = {
    { LEMS_ROW_MODE,  LEMS_GREY_MODE },
    { LEMS_ROW_MUSIC, LEMS_GREY_MUSIC },
    { LEMS_ROW_CODE,  LEMS_GREY_CODE },
    { LEMS_ROW_SAVE,  LEMS_GREY_SAVE },
};

/* --- the five style names and the four rating names -------------------------
 * Style names come out of the LVL format document, which is the only reference
 * that names the DOS sets at all: "0x0000 is dirt, 0x0001 is fire, 0x0002 is
 * squasher, 0x0003 is pillar, 0x0004 is crystal". They are LOWERCASE PROSE
 * there and the band CAPITALISES them, so the preview reads "Style Dirt" beside
 * "Rating Fun" rather than disagreeing with itself one row along - a departure
 * SPEC.md 116.1 records as deliberate, in the wording tools/os88lem.py carries.
 * Rating names are Lemmix Styles.Dos.pas SectionTable's.
 * These are the id maps, not the strings. */
static const unsigned char lem_style_str[5] = {
    LEMS_STYLE0, LEMS_STYLE1, LEMS_STYLE2, LEMS_STYLE3, LEMS_STYLE4
};

static const unsigned char lem_rating_str[LEM_RATINGS] = {
    LEMS_RATING0, LEMS_RATING1, LEMS_RATING2, LEMS_RATING3
};

/* --- the STYLE BANK, LEMGR<n>[_<part>].LEM, magic "LGRB" --------------------
 * tools/os88lem.py's docstring pins this byte for byte and this is the reader's
 * half of it. Two facts shape everything below:
 *
 *   NO STYLE BANK FITS ONE 64,512-byte file. The smallest is 78,732 and set 1
 *   is 96,804, so every one of the five is written as NUMBERED PARTS
 *   (SPEC.md 116.3.2's own mechanism) that the package reads in sequence into
 *   ONE claim at successive 512-aligned offsets. Part p starts at logical
 *   offset p * 64,512, which is 4,032 paragraphs - exact, because 64,512 is
 *   itself 126 sectors.
 *
 *   A PIECE'S OFFSET IS A DWORD, and it has to be: the bank is bigger than a
 *   segment. ovl_far() turns one into a segment and a 0..15 offset, which is
 *   what the assembly rasters take.
 *
 * A terrain piece is FOUR PLANES of w*h/8 bytes and PLANE 3 IS THE MASK, which
 * is what makes the stored nibble the mode-0Dh colour directly: planes 0-2 are
 * the style's 0-7 and plane 3 lifts it into the level's 8-15
 * (docs/lemband-format.md). An object frame is FIVE - four colour and a mask. */
#define LEM_GR_MAGIC      0        /* "LGRB" */
#define LEM_GR_VERSION    4
#define LEM_GR_STYLE      6
#define LEM_GR_NTER       7        /* USED pieces; the table still has 64 rows */
#define LEM_GR_NOBJ       8
#define LEM_GR_PARTS      9
#define LEM_GR_TOTLEN    10        /* dword: bytes before the last part's pad */
#define LEM_GR_CUSTOM    32        /* 8 x 3, the LEVEL's colours -> 8..15 */
#define LEM_GR_STDPAL    56        /* 8 x 3, the lemmings' and the panel's */
#define LEM_GR_TERTAB   128        /* 64 rows of 8 */
#define LEM_GR_OBJTAB   640        /* 16 rows of 16 */
#define LEM_GR_TERROWS   64
#define LEM_GR_TERSTRIDE  8
#define LEM_GR_ANIMFL   896        /* 16 bytes, the objects' animation
                                    * flags (wave 3, LGRB version 2) */
#define LEM_GR_PREVIEW  912        /* 16 bytes, their preview frames */
#define LEM_GR_VER        2

/* --- a SPECIAL picture's bank, LEMSP<n> (wave 4, docs/lemband-format.md) ----
 * A 512-byte header and then the 960x160 picture as PIECES, row-major, each
 * laid out as a terrain piece is - planes 0-3 one plane stride apart - so
 * lem_r_wpiece composes a special level exactly as it composes a terrain
 * piece. The two palettes sit at the STYLE bank's own offsets (LEM_GR_CUSTOM,
 * LEM_GR_STDPAL), so lem_r_pal reads either bank the same way. */
#define LEM_SP_VERSION    4
#define LEM_SP_INDEX      6
#define LEM_SP_COLS      17
#define LEM_SP_ROWS      18
#define LEM_SP_PW        20        /* word */
#define LEM_SP_PH        22        /* word */
#define LEM_SP_X         24        /* word: 304, where every reader draws it */
#define LEM_SP_HEAD     512
#define LEM_SP_VER        2
#define LEM_GO_W          0        /* an object table row, 16 bytes */
#define LEM_GO_H          1
#define LEM_GO_FRAMES     2
#define LEM_GO_EFFECT     3
#define LEM_GO_TRIGL      4        /* word, x4 resolution as stored */
#define LEM_GO_TRIGT      6        /* word: the reader subtracts 4 */
#define LEM_GO_TRIGW      8
#define LEM_GO_TRIGH      9
#define LEM_GO_SOUND     10
#define LEM_GO_START     11
#define LEM_GO_OFF       12        /* dword, from the start of the bank */
#define LEM_GT_W          0
#define LEM_GT_H          1
#define LEM_GT_OFF        2        /* dword, from the START of the logical bank */
#define LEM_GT_NBYTES     6

/* --- the MAIN BANK, LEMMAIN.LEM, magic "LMNB" -------------------------------
 * Ten items in a sixteen-row table, each a dword offset and a dword length:
 * the 320x40 4bpp skill panel (MAIN.DAT section 6 offset 0), the 38-glyph 8x16
 * 3bpp green status font (section 6 offset 0x1900), the count digits, the
 * animations and the destruction masks with their two registration tables,
 * the rating signs - and, drawn since wave 5 on the preview and postview
 * screens, the 94-glyph 16x16 3bpp purple font (section 4 offset 0x69B0) and
 * the 320x104 2bpp brown background (section 3 offset 0) - and since wave 6
 * an eleventh, the explosion's spray, out of PARTICLES.DAT (Lemmix's
 * src/Data/Particles/Particles.dat, which was extracted from the game's
 * executable; SPEC.md 116.8). */
#define LEM_MN_MAGIC      0        /* "LMNB" */
#define LEM_MN_VERSION    4
#define LEM_MN_VER        3        /* 3: item 10, the spray (wave 6); 2:
                                    * the menu palette (wave 5) */
#define LEM_MN_NITEMS     6
#define LEM_MN_PARTS      7
#define LEM_MN_ITEMS     32        /* 16 rows of 16 */
#define LEM_MN_ISTRIDE   16
#define LEM_MI_ID         0
#define LEM_MI_BPP        1
#define LEM_MI_W          2
#define LEM_MI_H          4
#define LEM_MI_FRAMES     6
#define LEM_MI_OFF        8        /* dword */
#define LEM_MI_NBYTES    12        /* dword */
#define LEM_SCR_INK       7        /* the preview's load bar: the font's
                                    * lightest purple, the menu palette's 7 */
#define LEM_MN_MENUPAL  288        /* the MAIN-MENU palette, 16 x 3 six-bit
                                    * (wave 5: the preview and postview
                                    * screens' colours - MAIN.DAT does not
                                    * carry it, tools/os88lem.py writes the
                                    * format document's table here) */

#define LEM_ITEM_PANEL     0
#define LEM_ITEM_STATFONT  1
#define LEM_ITEM_DIGITS    2
#define LEM_ITEM_ANIM      3
#define LEM_ITEM_MASKS     4
#define LEM_ITEM_SIGNS     5
#define LEM_ITEM_PURPLE    6
#define LEM_ITEM_BROWN     7
#define LEM_ITEM_ANIMTAB   8
#define LEM_ITEM_MASKTAB   9
#define LEM_ITEM_SPRAY    10        /* the explosion's spray (wave 6, decision
                                     * 5): PARTICLES.DAT's 51 rows of 80
                                     * signed (dx, dy) pairs, then 51 rows of
                                     * four signed bounds - dx min, dx max, dy
                                     * min, dy max of the particles drawn
                                     * (docs/lemband-format.md) */
#define LEM_PFR           51        /* the spray's table rows */
#define LEM_PROW         160        /* ...a row's bytes: 80 pairs */
#define LEM_PTIME         52        /* ...and the frames it is DRAWN for, the
                                     * last row twice: Lemmix's
                                     * PARTICLE_FRAMECOUNT (src/Game.pas:785),
                                     * ParticleTimer 52 down to 1 while
                                     * ParticleFrame stops at 50 (:4452-4455) -
                                     * and fParticleFinishTimer's 52, the
                                     * frames the level's end waits (:3375) */
#define LEM_PBINS         10        /* lemsprite.inc's bins: 16-row bands */

/* --- the RAW LEVEL RECORD, LEMLV<n>.LEM ------------------------------------
 * Eight 2,048-byte records back to back, no header, ODDTABLE already folded
 * into the RATING entry (docs/lemband-format.md). EVERY MULTI-BYTE FIELD IN
 * HERE IS BIG-ENDIAN - it is the original's own .LVL record, untouched - which
 * is why lemovl.c reads it a byte at a time and never with lem_u16().
 *
 * The terrain list is 400 four-byte slots; a slot whose first WORD is 0xFFFF is
 * SKIPPED and never breaks the walk (SPEC.md 116.3.1: it is Taxing 27, 'Call in
 * the bomb squad', whose slot 68 holds 0xFFFF22A6 and whose slots 69-399 hold
 * 327 further normal entries - breaking renders it with 68 pieces of 395). */
#define LEM_LVL_SIZE   2048
#define LEM_LVL_TERR  0x120        /* 400 slots of 4 */
#define LEM_LVL_NTERR   400
#define LEM_LVL_STEEL 0x760        /* 32 slots of 4 */
#define LEM_LVL_GROUP     8        /* records per LEMLV<n>.LEM */

/* WIRE_FILEMAX (The Wire: SPEC.md 92.13; LEMMINGS: 116.3.2): no band file exceeds this, which is
 * what makes a bank's parts land at exact 4,032-paragraph boundaries and what
 * every os88_file_read_seg() below is capped at. */
#define LEM_PARTMAX   64512u

/* --- the seven preview lines, in the original's order -----------------------
 * Lemmix GameScreen.Preview.pas GetScreenLinesAndColors and Base.Strings.pas
 * :411-418. The ORDER is the original's and so is the wording; what this table
 * carries is which format string each line uses and how many values it takes,
 * so ovl_fmt() is one loop rather than seven cases.
 *
 *   0 'Level %s %s'            the number and the name    - two values
 *   1 'Number of Lemmings %s'                             - one
 *   2 '%s To Be Saved'                                    - one
 *   3 'Release Rate %s'                                   - one
 *   4 'Time %s Minutes'                                   - one
 *   5 'Rating %s'                                         - one
 *   6 'Style %s'                                          - one
 *
 * Line 0 is NOT indented on the original's screen and lines 1-6 are, by the ten
 * spaces LEMS_PV_INDENT carries verbatim (GameScreen.Preview.pas IndentStr). */
#define LEM_PV_LINES 7

struct lem_pvline {
    unsigned char strid;            /* LEMS_PV_* - the format string */
    unsigned char indent;           /* 1 = the ten-space indent goes in front */
};

static const struct lem_pvline lem_pv[LEM_PV_LINES] = {
    { LEMS_PV_LEVEL,  0 },
    { LEMS_PV_NUMBER, 1 },
    { LEMS_PV_SAVED,  1 },
    { LEMS_PV_RATE,   1 },
    { LEMS_PV_TIME,   1 },
    { LEMS_PV_RATING, 1 },
    { LEMS_PV_STYLE,  1 },
};

/* --- the three lookups ------------------------------------------------------ */

/* lem_state_fact - the sentence behind a greyed state row.
 *
 * THE MODE ROW HAS THREE FACTS AND NOT ONE, because three different cards grey
 * it for three different reasons and SPEC.md 47's rule is that a greyed control
 * names the fact THE ACTION would refuse on. The table's single cell was
 * written for an EGA - "a sixteen-colour card gets four colours" - and shown on
 * a CGA it is a sentence about somebody else's hardware: a CGA is not a
 * sixteen-colour card and nothing was taken away from it. kernel/fsx.inc's
 * fsx_capstab is the authority for all three rows (VGA 0x01EF, HERC 0x0011,
 * CGA 0x000F, EGA 0x000F), and lem_vidkind is the kind os88_fsx_caps() answered
 * with for OUR window - re-asked on every W_ONRESIZE, because that callback is
 * delivered on a change of adapter (SPEC.md 39.16.3.3). A VGA carries
 * FSXM_VGA0D and so never reaches this row at all. */
static int lem_state_fact(int row)
{
    if (row < 0 || row >= LEM_STATE_ROWS)
        return LEMS_NONE;
    if (row == LEM_ROW_MODE) {
        if (lem_vidkind == OS88_VID_CGA)
            return LEMS_GREY_MODE_CGA;
        if (lem_vidkind == OS88_VID_HERC)
            return LEMS_GREY_MODE_HERC;
    }
    return (int)lem_state[row].fact;
}

static int lem_state_label(int row)
{
    if (row < 0 || row >= LEM_STATE_ROWS)
        return LEMS_NONE;
    return (int)lem_state[row].label;
}

static int ovl_style_name(int style)
{
    if (style < 0 || style > 4)
        return LEMS_NONE;
    return (int)lem_style_str[style];
}

/* ============================================================================
 * WAVE 3: THE GAME'S TABLES (SPEC.md 116.4.7)
 *
 * ATTRIBUTION. Every number below is the original's, read out of one of the
 * three reimplementations that document it and cited at the table that carries
 * it: Lemmix (Eric Langedijk, under Lemmix's own Licence.txt - zlib's text
 * with an added clause, 'use it for free, use it for fun') src/Game.pas,
 * src/Styles.Base.pas,
 * src/Dos.Consts.pas and src/Game.SkillPanel.pas; lemmings_3ds (public domain)
 * src/lemming.c; and ccexplore's mechanics notes as Lemmix's comments quote
 * them. Lemmings is (C) 1991 DMA Design / Psygnosis.
 * ==========================================================================*/

/* --- the eighteen actions, in Lemmix's TLemmingAction order less None
 * (src/Dos.Consts.pas:82). The ORDER is the status line's word table's too. */
#define LA_WALK    0
#define LA_JUMP    1
#define LA_DIG     2
#define LA_CLIMB   3
#define LA_DROWN   4
#define LA_HOIST   5
#define LA_BUILD   6
#define LA_BASH    7
#define LA_MINE    8
#define LA_FALL    9
#define LA_FLOAT  10
#define LA_SPLAT  11
#define LA_EXIT   12
#define LA_FRY    13
#define LA_BLOCK  14
#define LA_SHRUG  15
#define LA_OHNO   16
#define LA_BOMB   17
#define LA_COUNT  18

/* --- each action's animation: the converter's 30-row registration table's
 * right- and left-facing rows (tools/os88lem.py MAIN_ANIMS, which is
 * Lemmings.ts's order), the frame count, LOOP or ONCE, the foot position and
 * the status line's word. Frames, loop and feet are Lemmix
 * src/Styles.Base.pas TLemmingAnimationSet.InitMetadata - "foot positions from
 * ccexplore's emails" - and the floater's eight frames are the converter's
 * preumbrella row followed by its umbrella row, which is the one Lemmix
 * animation 'Umbrella' (8 frames at $3930) stored as two. */
struct lem_anim {
    unsigned char rowr;             /* the right-facing registration row */
    unsigned char rowl;             /* ...and the left-facing one */
    unsigned char frames;
    unsigned char loop;             /* 1 = wraps to 0, 0 = holds the last */
    unsigned char footx;
    unsigned char footy;
    unsigned char word;             /* LEMS_LEM_* */
    unsigned char flags;            /* LAF_* below */
};

/* The ACTION SETS Lemmix tests with ActionIn(), one bit each (src/Game.pas):
 *   PRIO   PrioritizedHitTest's prioActions (:3604)
 *   BLK/BLD/BSH/MIN/DIG   the five Assign*'s actionSets (:2089-2195)
 *   NOCF   AssignClimber/AssignFloater refuse it (:2039, 2059)
 *   NOBMB  AssignBomber refuses it (:2075) */
#define LAF_PRIO   0x01
#define LAF_BLK    0x02
#define LAF_BLD    0x04
#define LAF_BSH    0x08
#define LAF_MIN    0x10
#define LAF_DIG    0x20
#define LAF_NOCF   0x40
#define LAF_NOBMB  0x80

#define LAF_WORK   (LAF_BLK | LAF_BLD | LAF_BSH | LAF_MIN | LAF_DIG)

static const struct lem_anim lem_an[LA_COUNT] = {
    {  0,  2,  8, 1,  8, 10, LEMS_LEM_WALKER,   LAF_WORK },
    {  1,  3,  1, 0,  8, 10, LEMS_LEM_JUMPER,   0 },
    {  4,  4, 16, 1,  8, 12, LEMS_LEM_DIGGER,   LAF_PRIO | LAF_WORK & ~LAF_DIG },
    {  5,  6,  8, 1,  8, 12, LEMS_LEM_CLIMBER,  0 },
    {  7,  7, 16, 0,  8, 10, LEMS_LEM_DROWNER,  0 },
    {  8,  9,  8, 0,  8, 12, LEMS_LEM_HOISTER,  0 },
    { 10, 11, 16, 1,  8, 13, LEMS_LEM_BUILDER,  LAF_PRIO | LAF_WORK & ~LAF_BLD },
    { 12, 13, 32, 1,  8, 10, LEMS_LEM_BASHER,   LAF_PRIO | LAF_WORK & ~LAF_BSH },
    { 14, 15, 24, 1,  8, 13, LEMS_LEM_MINER,    LAF_PRIO | LAF_WORK & ~LAF_MIN },
    { 16, 17,  4, 1,  8, 10, LEMS_LEM_FALLER,   0 },
    { 18, 20,  8, 1,  8, 16, LEMS_LEM_FLOATER,  0 },
    { 22, 22, 16, 0,  8, 10, LEMS_LEM_SPLATTER, LAF_NOCF | LAF_NOBMB },
    { 23, 23,  8, 0,  8, 13, LEMS_LEM_EXITER,   0 },
    { 24, 24, 14, 0,  8, 14, LEMS_LEM_FRIER,    LAF_NOBMB },
    { 25, 25, 16, 1,  8, 10, LEMS_LEM_BLOCKER,  LAF_PRIO | LAF_NOCF },
    { 26, 27,  8, 0,  8, 10, LEMS_LEM_SHRUGGER, LAF_PRIO | LAF_WORK },
    { 28, 28, 16, 0,  8, 10, LEMS_LEM_OHNOER,   LAF_PRIO | LAF_NOBMB },
    { 29, 29,  1, 0, 16, 25, LEMS_LEM_BOMBER,   LAF_NOCF | LAF_NOBMB },
};

/* --- the floater's sixteen faked frames: dy and the animation frame shown.
 * Lemmix src/Game.pas:731 FloatParametersTable, and lemmings_3ds
 * src/lemming.c float_param[] agrees value for value. After 15 it goes back
 * to 8. */
static const signed char lem_fdy[16] = {
    3, 3, 3, 3, -1, 0, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2
};
static const unsigned char lem_ffr[16] = {
    1, 2, 3, 5, 5, 5, 5, 5, 5, 6, 7, 7, 6, 5, 4, 4
};

/* --- the twelve panel buttons, in Lemmix's TSkillPanelButton order less None
 * (src/Dos.Consts.pas:104-118). Hit box n is Rect(1 + 16n, 16, 15 + 16n, 38)
 * (src/Game.SkillPanel.pas SetButtonRects, :487 - the first is (1,16,15,38)
 * and each next is offset 16 in x); the count is drawn at x = 16n + 4, y = 17
 * (DrawSkillCount, "x = 3, 19, 35 etc. are the black holes for the numbers").
 * Buttons 2..9 are the eight skills in the order the level header lists their
 * counts (docs/lemband-format.md, the entry's eight skill bytes). */
#define LB_SLOWER   0
#define LB_FASTER   1
#define LB_CLIMBER  2
#define LB_UMBRELLA 3
#define LB_EXPLODE  4
#define LB_BLOCKER  5
#define LB_BUILDER  6
#define LB_BASHER   7
#define LB_MINER    8
#define LB_DIGGER   9
#define LB_PAUSE   10
#define LB_NUKE    11
#define LB_COUNT   12

/* --- the object map's values (Lemmix src/Game.pas:768-778): 0..127 is a
 * triggered trap's object index and 128 + n the GROUND trigger effect n. */
#define DOM_NONE       128
#define DOM_EXIT       129
#define DOM_FORCELEFT  130
#define DOM_FORCERIGHT 131
#define DOM_WATER      133
#define DOM_FIRE       134
#define DOM_ONEWAYLEFT 135
#define DOM_ONEWAYRIGHT 136
#define DOM_STEEL      137
#define DOM_BLOCKER    138

/* --- the bounds (Lemmix src/Game.pas:780-783) ----------------------------- */
#define LEM_HEADMINY   (-5)
#define LEM_MINX         0
#define LEM_MAXX      1647
#define LEM_MAXY       163
#define LEM_MAXFALL     60            /* MAX_FALLDISTANCE */

/* --- the sounds (SPEC.md 116.11). The EVENTS are the original's - Lemmix
 * src/Game.pas's CueSoundEffect calls, one per place the DOS game plays one -
 * and the TONES are this port's own: the original's are FM patches inside
 * ADLIB.DAT, which is not in the 26 files this port fetches at all. A tone is
 * a pitch and a length in ticks on the PC speaker through os88_snd_tone(). */
#define SND_LETSGO      0
#define SND_ENTRANCE    1
#define SND_SKILLBTN    2
#define SND_ASSIGN      3
#define SND_YIPPEE      4
#define SND_SPLAT       5
#define SND_OHNO        6
#define SND_EXPLODE     7
#define SND_DROWN       8
#define SND_FRY         9
#define SND_STEEL      10
#define SND_BRICKS     11            /* the builder's last-three warning */
#define SND_TRAP       12
#define SND_COUNT      13
/* THREE EVENTS LEMMIX HAS SLOTS FOR ARE NOT HERE, ON PURPOSE: TSoundEffect's
 * Miner, Digger, Basher, OpenUmbrella and Nuke are "// custom sound" (Lemmix
 * src/Dos.Consts.pas:295-300), kept out of the default set by IsCustom
 * (:348-351) and loaded only from a user's custom-sounds folder
 * (src/Game.Sound.pas:478, :494) - so a stock Lemmix, like the 1991 game, is
 * SILENT at a dig, a bash, a mine stroke, an umbrella and a nuke. Wave 3 cued
 * all five and lost the trap's sound to the digger's (the lowest number wins
 * a frame); its review struck them (SPEC.md 116.11). */

static const unsigned int lem_sndhz[SND_COUNT] = {
    523, 220, 1000, 1500, 1320, 110, 440, 80, 300, 600, 2000, 880, 200
};
static const unsigned char lem_sndt[SND_COUNT] = {
    4, 6, 1, 1, 3, 3, 4, 6, 4, 3, 1, 1, 4
};

/* --- the opening (Lemmix src/Game.pas IncrementIteration, :3498): "let's go"
 * at iteration 15, the entrance sound at 34, the hatches open at 35 - and 17
 * game frames make a second of the level's clock. */
#define LEM_IT_LETSGO   15
#define LEM_IT_DOORSND  34
#define LEM_IT_OPEN     35
#define LEM_FPS         17
