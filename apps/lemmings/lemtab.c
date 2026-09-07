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
 * 92.3); each carries the reference file that defines it in a comment in
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
 * lem_u8() are the only things that read them.
 *
 * THE FILE IS 512 BYTES AND IS READ WHOLE, with os88_file_read() and never
 * os88_file_read_at(): that call's offset AND cap must each be a whole number
 * of CLUSTERS, the cluster is 512 bytes on a 1.44MB or 1.2MB disk and 1,024 on
 * a 720KB or 360KB one (tools/os88disk.py:124), and it is neither of those on
 * the RAM disk The Wire unpacks this package onto (SPEC.md 92.3.2). A whole
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
#define LEM_MAN_GEOM     22        /* "1440K" / "720K" / "360K" / "1200K" */
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
#define LEM_E_NAME       32        /* the original's 32 bytes. A NAME OF EXACTLY
                                    * 32 CHARACTERS HAS NO TERMINATOR - four of
                                    * the 120 are that long - so a reader takes
                                    * at most 32 bytes and stops at a NUL or at
                                    * 32 (tools/os88lem.py, tests/unit/
                                    * t_lemdat.py caught it) */
#define LEM_E_NAMELEN    32

#define LEM_EF_HERE    0x01

/* --- LEMSTR.LEM's header ---------------------------------------------------- */
#define LEM_STR_MAGIC   0          /* "LSTR" */
#define LEM_STR_COUNT   4
#define LEM_STR_DIR     6          /* count words, each an offset from the START
                                    * OF THE FILE of a NUL-terminated string */

/* --- the band file names ----------------------------------------------------
 * Uppercase 8.3 in [A-Z0-9_-] (SPEC.md 92.3.2), and resolved in the LAUNCHING
 * instance's own directory (SPEC.md 19.2.1, 73.14) - which is why the package,
 * LEMMINGS.OVL and every band file are in ONE folder on every disk (SPEC.md
 * 92.6).
 *
 * The two patched ones are not const: lem_rating_load() writes the rating's
 * digit into lem_f_rat, and lem_bandname() writes a set number and a part digit
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
 * 92.8) and not about the hardware. Save Progress is greyed when the write path
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
 * SPEC.md 92.1 records as deliberate, in the wording tools/os88lem.py carries.
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
 *   (SPEC.md 92.3.2's own mechanism) that the package reads in sequence into
 *   ONE claim at successive 512-aligned offsets. Part p starts at logical
 *   offset p * 64,512, which is 4,032 paragraphs - exact, because 64,512 is
 *   itself 126 sectors.
 *
 *   A PIECE'S OFFSET IS A DWORD, and it has to be: the bank is bigger than a
 *   segment. lem_far() turns one into a segment and a 0..15 offset, which is
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
#define LEM_GT_W          0
#define LEM_GT_H          1
#define LEM_GT_OFF        2        /* dword, from the START of the logical bank */
#define LEM_GT_NBYTES     6

/* --- the MAIN BANK, LEMMAIN.LEM, magic "LMNB" -------------------------------
 * Ten items in a sixteen-row table, each a dword offset and a dword length. The
 * two this wave draws are item 0, the 320x40 4bpp skill panel (MAIN.DAT section
 * 6 offset 0), and item 1, the 38-glyph 8x16 3bpp green status font (section 6
 * offset 0x1900). The other eight - the animations, the destruction masks, the
 * rating signs, the purple font and the brown background - arrive with the
 * waves that draw them. */
#define LEM_MN_MAGIC      0        /* "LMNB" */
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

/* --- the RAW LEVEL RECORD, LEMLV<n>.LEM ------------------------------------
 * Eight 2,048-byte records back to back, no header, ODDTABLE already folded
 * into the RATING entry (docs/lemband-format.md). EVERY MULTI-BYTE FIELD IN
 * HERE IS BIG-ENDIAN - it is the original's own .LVL record, untouched - which
 * is why lemovl.c reads it a byte at a time and never with lem_u16().
 *
 * The terrain list is 400 four-byte slots; a slot whose first WORD is 0xFFFF is
 * SKIPPED and never breaks the walk (SPEC.md 92.3.1: it is Taxing 27, 'Call in
 * the bomb squad', whose slot 68 holds 0xFFFF22A6 and whose slots 69-399 hold
 * 327 further normal entries - breaking renders it with 68 pieces of 395). */
#define LEM_LVL_SIZE   2048
#define LEM_LVL_TERR  0x120        /* 400 slots of 4 */
#define LEM_LVL_NTERR   400
#define LEM_LVL_STEEL 0x760        /* 32 slots of 4 */
#define LEM_LVL_GROUP     8        /* records per LEMLV<n>.LEM */

/* WIRE_FILEMAX (SPEC.md 88.13, 92.3.2): no band file exceeds this, which is
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

static int lem_style_name(int style)
{
    if (style < 0 || style > 4)
        return LEMS_NONE;
    return (int)lem_style_str[style];
}
