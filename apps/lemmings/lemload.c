/* ============================================================================
 * os8088 - apps/lemmings/lemload.c   (#included by lemmings.c)
 *
 * THE BAND READER, RESIDENT HALF (SPEC.md 92.3, docs/lemband-format.md).
 *
 * Wave 1 reads three of the band files and nothing else: LEMMAN.LEM (the
 * 512-byte manifest header and its cost table), LEMSTR.LEM (the string resource
 * band, through lemtext.c) and one LEMR<n>.LEM at a time (a rating's thirty
 * 64-byte level entries, with the preview fields already unpacked and the
 * ODDTABLE overrides already folded in on the host). The style banks, the
 * MAIN.DAT bank, the raw level records and the special pictures are read into
 * HEAP CLAIMS by os88_file_read_seg() and that arrives with the raster in
 * wave 2; the composition driver is in lemovl.c from the start, because nothing
 * that runs once per level belongs in the resident image (SPEC.md 73.14).
 *
 * EVERY READ HERE IS A WHOLE FILE, os88_file_read(), AND NEVER
 * os88_file_read_at(). That call's offset AND cap must each be a whole number
 * of CLUSTERS; the cluster is 512 bytes on a 1.44MB or 1.2MB disk and 1,024 on
 * a 720KB or 360KB one (tools/os88disk.py:124), and it is neither of those on
 * the RAM disk The Wire unpacks this package onto (SPEC.md 92.3.2). That is why
 * the RATING is the file and not a window into a 10 KB index: a whole small
 * file has no alignment rule of any kind, and the package carries one rating's
 * 2,048 bytes of bss rather than four ratings' 8,192. Four extra directory rows
 * against RD_MAXENT's 96 is the price and there is room
 * (docs/lemband-format.md says the same thing from the converter's side).
 *
 * A REFUSAL HERE IS NOT FATAL. The window comes up and says what is missing,
 * which is SPEC.md 47's ordinary path and a great deal more use than a launch
 * that silently aborts.
 *
 * ATTRIBUTION. The level headers and names this file reads are the original's
 * (Lemmings, (C) 1991 DMA Design / Psygnosis), decoded on the host by
 * tools/os88lem.py; the bit unpacking is Lemmix's Level.Loader.pas TranslateLevel
 * and the field offsets are lemmings_3ds's lemmings_lvl_file_format.txt. Nothing
 * of either is in this file - it reads a format this port defined.
 * ==========================================================================*/

static unsigned char lem_manbuf[LEM_MAN_SIZE];
static unsigned char lem_ratbuf[LEM_RATBUF];
static int lem_man_ok;              /* the manifest is loaded and sane */
static int lem_rat_ok;              /* ...and so is the rating in the buffer */
static int lem_rat_have;            /* which rating that is, or -1 */

/* --- the manifest ---------------------------------------------------------- */

static int lem_man_u16(int off)
{
    if (!lem_man_ok)
        return 0;
    return lem_u16(lem_manbuf, off);
}

static int lem_man_u8(int off)
{
    if (!lem_man_ok)
        return 0;
    return lem_u8(lem_manbuf, off);
}

/* lem_geom - "1440K" / "1200K" / "720K" / "360K", NUL-padded in the header. It
 * is what every greying fact's second marker prints, so it is read rather than
 * guessed from a disk size this program never asks for. */
static const char *lem_geom(void)
{
    if (!lem_man_ok)
        return "";
    return (const char *)(lem_manbuf + LEM_MAN_GEOM);
}

/* --- the cost table (LEMMAN.LEM offset 60, tools/os88lem.py build_manifest) --
 * Nine rows of eight bytes, styles 0-4 then the four special pictures, written
 * whether the bank is on this disk or not - which is the whole reason it is
 * there. A greyed level row asks how big the bank it is missing WOULD have
 * been, and that cannot be measured from a disk the bank is not on. */
static int lem_cost_row(int kind, int n)
{
    int row;

    row = (kind == LEM_BANK_SPEC) ? (5 + n) : n;
    if (row < 0 || row > 8)
        return -1;
    return LEM_MAN_COSTS + row * LEM_COST_STRIDE;
}

static int lem_cost_clus(int kind, int n)
{
    int off = lem_cost_row(kind, n);

    if (off < 0)
        return 0;
    return lem_man_u16(off + LEM_COST_CLUS);
}

static int lem_cost_parts(int kind, int n)
{
    int off = lem_cost_row(kind, n);

    if (off < 0)
        return 0;
    return lem_man_u8(off + LEM_COST_PARTS);
}

/* lem_bandname - the 8.3 name of a bank's FIRST file, for a fact that has to
 * say which file is not here. A bank that fits one 64,512-byte band keeps its
 * plain name and a bank written as numbered parts is LEMGR<n>_0.LEM
 * (SPEC.md 92.3.2). Uppercase 8.3 in [A-Z0-9_-] by construction.
 *
 * ONE BUFFER, so the answer may not be live across the next call - the caller
 * here stages it as a format argument and formats immediately (LESSONS.md 13). */
static const char *lem_bandname(int kind, int n)
{
    int i = 0;

    lem_f_scratch[i++] = 'L';
    lem_f_scratch[i++] = 'E';
    lem_f_scratch[i++] = 'M';
    if (kind == LEM_BANK_SPEC) {
        lem_f_scratch[i++] = 'S';
        lem_f_scratch[i++] = 'P';
    } else {
        lem_f_scratch[i++] = 'G';
        lem_f_scratch[i++] = 'R';
    }
    lem_f_scratch[i++] = (char)('0' + (n & 7));
    if (lem_cost_parts(kind, n) > 1) {
        lem_f_scratch[i++] = '_';
        lem_f_scratch[i++] = '0';
    }
    lem_f_scratch[i++] = '.';
    lem_f_scratch[i++] = 'L';
    lem_f_scratch[i++] = 'E';
    lem_f_scratch[i++] = 'M';
    lem_f_scratch[i] = 0;
    return lem_f_scratch;
}

/* --- one rating's thirty entries ------------------------------------------- */

/* lem_entry - the row'th 64-byte entry of the LOADED rating. Rows are 0..29 and
 * never 0..119: the launcher holds one rating at a time by design (see the file
 * header). The stride is a shift and not an imul (LESSONS.md 3). */
static const unsigned char *lem_entry(int row)
{
    if (!lem_rat_ok || row < 0 || row >= LEM_PERRAT)
        return 0;
    return lem_ratbuf + (row << 6);
}

static int lem_ent_u8(int row, int off)
{
    const unsigned char *e = lem_entry(row);

    if (e == 0)
        return 0;
    return (int)e[off];
}

static int lem_ent_u16(int row, int off)
{
    const unsigned char *e = lem_entry(row);

    if (e == 0)
        return 0;
    return lem_u16(e, off);
}

/* lem_ent_name - the level's title, into a caller-proof scratch.
 *
 * A NAME OF EXACTLY 32 CHARACTERS FILLS THE FIELD WITH NO TERMINATOR - four of
 * the 120 are that long - so this reads at most 32 bytes and stops at a NUL or
 * at 32. tests/unit/t_lemdat.py found that one by reading the originals rather
 * than what the converter wrote, which is what an independent second reader is
 * for. The LEADING spaces are the original's own centring and are trimmed at
 * DISPLAY time (lem_trim), the way Lemmix's Title.Trim does. */
static char lem_namebuf[LEM_E_NAMELEN + 1];

static const char *lem_ent_name(int row)
{
    const unsigned char *e = lem_entry(row);
    int i;

    if (e == 0) {
        lem_namebuf[0] = 0;
        return lem_namebuf;
    }
    for (i = 0; i < LEM_E_NAMELEN; i++) {
        if (e[LEM_E_NAME + i] == 0)
            break;
        lem_namebuf[i] = (char)e[LEM_E_NAME + i];
    }
    while (i > 0 && lem_namebuf[i - 1] == ' ')
        i--;
    lem_namebuf[i] = 0;
    return lem_namebuf;
}

static int lem_ent_here(int row)
{
    return (lem_ent_u8(row, LEM_E_FLAGS) & LEM_EF_HERE) != 0;
}

/* --- loading --------------------------------------------------------------- */

/* lem_rating_load - read LEMR<r>.LEM whole. The digit is patched into the one
 * name buffer; the file is always exactly 2,048 bytes on every disk and every
 * geometry, so one rating's bss is a constant (docs/lemband-format.md). */
static int lem_rating_load(int rating)
{
    unsigned n;

    lem_rat_ok = 0;
    lem_rat_have = -1;
    if (rating < 0 || rating >= LEM_RATINGS)
        return 0;
    lem_f_rat[LEM_F_RAT_DIGIT] = (char)('0' + rating);
    n = os88_file_read(lem_f_rat, lem_ratbuf, LEM_RATBUF);
    if (n < (unsigned)(LEM_PERRAT << 6))
        return 0;
    lem_rat_ok = 1;
    lem_rat_have = rating;
    return 1;
}

/* lem_data_load - the manifest, the string band and rating 0, in that order.
 *
 * IT IS CALLED FROM os88_main(), WHERE THE FILE SLOTS ARE LEGAL AND THE GFX
 * LOCK IS NOT HELD (SPEC.md 20.2). What may NOT be done there is load the
 * overlay: there is no instance yet to resolve a module for, which is RunCPM's
 * lesson one level up (LESSONS.md 13), so ovl_ready() is called from the first
 * wake instead.
 *
 * 0 = the launcher comes up and says the data is not in this folder. */
static int lem_data_load(void)
{
    unsigned n;

    lem_man_ok = 0;
    n = os88_file_read(lem_f_man, lem_manbuf, LEM_MAN_SIZE);
    if (n < (unsigned)LEM_MAN_SIZE)
        return 0;
    if (lem_manbuf[0] != 'O' || lem_manbuf[1] != 'S' || lem_manbuf[2] != '8' ||
        lem_manbuf[3] != '8' || lem_manbuf[4] != 'L' || lem_manbuf[5] != 'E' ||
        lem_manbuf[6] != 'M' || lem_manbuf[7] != 0)
        return 0;
    lem_man_ok = 1;
    if (lem_u16(lem_manbuf, LEM_MAN_VERSION) != LEM_MAN_VER) {
        lem_man_ok = 0;
        return 0;
    }
    if (lem_u16(lem_manbuf, LEM_MAN_LESIZE) != LEM_ENTSZ) {
        lem_man_ok = 0;
        return 0;
    }
    if (!lem_str_load()) {
        lem_man_ok = 0;
        return 0;
    }
    return lem_rating_load(0);
}

/* ============================================================================
 * WAVE 2: THE CLAIMS AND THE BANK READER (SPEC.md 92.6.1)
 *
 * Everything above reads a small file into bss. Everything here reads a BANK -
 * 32 KB to 96 KB - into a HEAP CLAIM through os88_file_read_seg(), which is
 * the only call in the SDK that can move that much: os88_file_read() takes a
 * DS-relative buffer and this package's whole image and bss cap at 60 KB
 * (APP_MAX_SIZE), and os88_file_read_at()'s offset and cap must each be a whole
 * number of CLUSTERS, which is 512 bytes on two geometries, 1,024 on the other
 * two and neither of those on the RAM disk The Wire unpacks this onto
 * (SPEC.md 92.3.2).
 *
 * A _seg BASE MUST BE 512-BYTE ALIGNED (SPEC.md 2.1.1, apps/cc/os88.h): a
 * claim's own base is, and so is any offset into it that is a multiple of
 * 0x200 - seg + 0x20, + 0x40 - but seg + 0x10 is NOT, and int 13h answers a run
 * that then straddles a 64 KB physical page with error 09h ON REAL HARDWARE,
 * which QEMU never shows (LESSONS.md 13 paid for this one in RunCPM). Every
 * part is 64,512 bytes = 4,032 paragraphs, so part p lands at seg + p * 4032
 * and the rule is met by construction rather than by arithmetic.
 *
 * CLAIM RECORDS ARE A BUDGET TOO and this is where they are counted:
 * kernel/memory.inc gives MEM_MAX = 32 on kern_big and 20 on kern_small,
 * SYSTEM-WIDE, shared with every Disk window and driver already open. This
 * program takes THREE on a VGA and FIVE on the two 1bpp adapters, plus the
 * package's own region and the overlay module's, and says so in its refusal.
 * ==========================================================================*/

/* The claim sizes, in KB, and every one of them is arithmetic rather than a
 * round number:
 *
 *   MASK    the solid mask, 200 bytes x 160 rows = 32,000 (lemmask.inc)
 *   BANK    the largest STYLE bank of the five - set 1 at 96,804 - which is
 *           what it has to hold whichever level is played, and which also
 *           holds a 76,800-byte VGASPEC picture in wave 4
 *   MAIN    LEMMAIN.LEM, 52,224 on every geometry
 *   WORLD   the 1bpp/2bpp world picture: CGA 400 bytes x 160 = 64,000,
 *           Hercules 200 x 160 = 32,000. VGA has none - its world lives in
 *           VRAM, which is the whole point of the mode 0Dh backend
 *   SHADOW  the composed screen on those two, 80 bytes x 200 rows = 16,000
 *   REC     transient, and freed before the level starts: one LEMLV<n>.LEM,
 *           16,384 bytes, read whole because the record inside it is at a
 *           2,048-byte offset that os88_file_read_at() cannot express on
 *           every medium (see the header above) */
#define LEM_KB_MASK    32
#define LEM_KB_BANK    96
#define LEM_KB_MAIN    52
#define LEM_KB_WORLD_C 63
#define LEM_KB_WORLD_H 32
#define LEM_KB_SHADOW  16
#define LEM_KB_REC     16

static unsigned lem_cl_mask;
static unsigned lem_cl_bank;
static unsigned lem_cl_main;
static unsigned lem_cl_world;       /* 0 on VGA */
static unsigned lem_cl_shadow;      /* 0 on VGA */
static int      lem_claimed;
static int      lem_bank_have;      /* the style in lem_cl_bank, or -1 */
static int      lem_main_have;

/* lem_far - a dword offset inside a claim as a SEGMENT and a 0..15 offset.
 *
 * A bank is bigger than a segment, so a piece's offset genuinely needs 32 bits
 * and this dialect has no `long` (docs/C-TOOLCHAIN.md). The two halves are read
 * as words and the segment is base + (hi << 12) + (lo >> 4), which is
 * base + off/16 without ever forming off. lem_far_off() is the remainder.
 *
 * IT ANSWERS THROUGH A STATIC because half of this API is out-parameters and
 * every one of them is a static (rule 1, LESSONS.md 4). */
static unsigned lem_far_seg;
static unsigned lem_far_off;

static void lem_far(unsigned base, int lo, int hi)
{
    lem_far_seg = base + ((unsigned)hi << 12) + ((unsigned)lo >> 4);
    lem_far_off = (unsigned)lo & 15;
}

/* lem_pk / lem_pk16 - one byte or one little-endian word out of a claim.
 *
 * ~11 us of near call each (apps/cc/os88.h), so this is right for a HEADER and
 * wrong for a copy. The whole of what it reads is the bank headers and the
 * terrain list, which is ~600 reads once per level; the pixels are moved by
 * the assembly rasters, which take a segment. */
static int lem_pk(unsigned seg, unsigned off)
{
    return os88_peek(seg, off) & 0xFF;
}

static int lem_pk16(unsigned seg, unsigned off)
{
    return lem_pk(seg, off) | (lem_pk(seg, off + 1) << 8);
}

/* lem_claim_all - the three or five claims, or 0 with the arithmetic said.
 *
 * IT IS ALL OR NOTHING. A program that got the mask and not the bank would
 * refuse later, further in, with the machine already in a foreign mode - so
 * every claim is taken here, before the bracket, and a refusal frees what it
 * got and answers 0 with a sentence the user can act on (SPEC.md 47,
 * WEAVE-SPEC 1.4's precedent). */
static int lem_claim_all(void)
{
    if (lem_claimed)
        return 1;
    lem_cl_mask = os88_mem_claim(LEM_KB_MASK);
    if (lem_cl_mask == 0)
        return 0;
    lem_cl_bank = os88_mem_claim(LEM_KB_BANK);
    if (lem_cl_bank == 0) {
        lem_free_all();
        return 0;
    }
    lem_cl_main = os88_mem_claim(LEM_KB_MAIN);
    if (lem_cl_main == 0) {
        lem_free_all();
        return 0;
    }
    if (lem_kind_id() != LEM_RKIND_VGA) {
        lem_cl_world = os88_mem_claim(lem_kind_id() == LEM_RKIND_CGA
                                      ? LEM_KB_WORLD_C : LEM_KB_WORLD_H);
        if (lem_cl_world == 0) {
            lem_free_all();
            return 0;
        }
        lem_cl_shadow = os88_mem_claim(LEM_KB_SHADOW);
        if (lem_cl_shadow == 0) {
            lem_free_all();
            return 0;
        }
    }
    lem_claimed = 1;
    lem_bank_have = -1;
    lem_main_have = 0;
    return 1;
}

static void lem_free_all(void)
{
    if (lem_cl_shadow)
        os88_mem_free(lem_cl_shadow);
    if (lem_cl_world)
        os88_mem_free(lem_cl_world);
    if (lem_cl_main)
        os88_mem_free(lem_cl_main);
    if (lem_cl_bank)
        os88_mem_free(lem_cl_bank);
    if (lem_cl_mask)
        os88_mem_free(lem_cl_mask);
    lem_cl_shadow = 0;
    lem_cl_world = 0;
    lem_cl_main = 0;
    lem_cl_bank = 0;
    lem_cl_mask = 0;
    lem_claimed = 0;
    lem_bank_have = -1;
    lem_main_have = 0;
}

/* lem_need_kb - what this adapter's claims add up to, for the refusal to say.
 * The package's own region and the module's are NOT in it: the kernel already
 * granted those or this code would not be running. */
static int lem_need_kb(void)
{
    int n = LEM_KB_MASK + LEM_KB_BANK + LEM_KB_MAIN;

    if (lem_kind_id() != LEM_RKIND_VGA)
        n += LEM_KB_SHADOW + (lem_kind_id() == LEM_RKIND_CGA
                              ? LEM_KB_WORLD_C : LEM_KB_WORLD_H);
    return n;
}

/* lem_parts_read - a banded file, part after part, into ONE claim.
 *
 * `base` is the 8.3 stem without its extension ("LEMGR0", "LEMMAIN"), already
 * in lem_f_scratch. A bank of ONE part keeps its plain name and a bank of more
 * is numbered, which is the converter's own rule (docs/lemband-format.md) and
 * the reason this takes the part count rather than probing for files.
 *
 * 0 = a part was short or missing, which means the disk is not the one the
 * manifest describes; the caller says so and stays in the window. */
static int lem_parts_read(int nparts, unsigned seg)
{
    int p, n, i;

    if (nparts < 1)
        nparts = 1;
    for (p = 0; p < nparts; p++) {
        i = 0;
        while (lem_f_scratch[i] != 0)
            i++;
        if (nparts > 1) {
            lem_f_scratch[i++] = '_';
            lem_f_scratch[i++] = (char)('0' + p);
        }
        lem_f_scratch[i++] = '.';
        lem_f_scratch[i++] = 'L';
        lem_f_scratch[i++] = 'E';
        lem_f_scratch[i++] = 'M';
        lem_f_scratch[i] = 0;
        /* 4,032 paragraphs a part: 64,512 bytes, which is 126 sectors, so
         * every part's base is 512-aligned by construction (see the header). */
        n = os88_file_read_seg(lem_f_scratch, seg + (unsigned)p * 4032,
                              (unsigned)LEM_PARTMAX);
        if (n == 0)
            return 0;
        while (i > 0 && lem_f_scratch[i] != '_')
            i--;
        if (nparts > 1 && lem_f_scratch[i] == '_')
            lem_f_scratch[i] = 0;
        else {
            i = 0;
            while (lem_f_scratch[i] != 0 && lem_f_scratch[i] != '.')
                i++;
            lem_f_scratch[i] = 0;
        }
    }
    return 1;
}

/* lem_stem - "LEMGR<n>" or "LEMMAIN" into lem_f_scratch, ready for
 * lem_parts_read to hang a part number and an extension off. */
static void lem_stem_gr(int n)
{
    lem_f_scratch[0] = 'L';
    lem_f_scratch[1] = 'E';
    lem_f_scratch[2] = 'M';
    lem_f_scratch[3] = 'G';
    lem_f_scratch[4] = 'R';
    lem_f_scratch[5] = (char)('0' + (n & 7));
    lem_f_scratch[6] = 0;
}

static void lem_stem_main(void)
{
    lem_f_scratch[0] = 'L';
    lem_f_scratch[1] = 'E';
    lem_f_scratch[2] = 'M';
    lem_f_scratch[3] = 'M';
    lem_f_scratch[4] = 'A';
    lem_f_scratch[5] = 'I';
    lem_f_scratch[6] = 'N';
    lem_f_scratch[7] = 0;
}

/* lem_main_read - LEMMAIN.LEM into its claim, once per session. It is the same
 * bytes for every level, so a retry or the next level does not re-read it -
 * which matters because it is 52 KB and ~130 int 13h calls on the target. */
static int lem_main_read(void)
{
    if (lem_main_have)
        return 1;
    /* ONE PART, always: LEMMAIN.LEM is 52,224 bytes on every geometry and
     * WIRE_FILEMAX is 64,512, so it keeps its plain name (the converter's own
     * rule - a bank that fits one part is not numbered). If it ever grows past
     * that, the converter refuses on the host before this could read half of
     * it (tools/os88lem.py's Disk.add). */
    lem_stem_main();
    if (!lem_parts_read(1, lem_cl_main))
        return 0;
    if (lem_pk(lem_cl_main, 0) != 'L' || lem_pk(lem_cl_main, 1) != 'M' ||
        lem_pk(lem_cl_main, 2) != 'N' || lem_pk(lem_cl_main, 3) != 'B')
        return 0;
    lem_main_have = 1;
    return 1;
}

/* lem_bank_read - one STYLE bank into its claim, and only when it is not
 * already the one in there. Five sets, one claim, and a level is much more
 * likely to reuse the last style than not (the four ratings are grouped by
 * difficulty, not by set, but a retry always is). */
static int lem_bank_read(int style)
{
    if (lem_bank_have == style)
        return 1;
    lem_bank_have = -1;
    if (style < 0 || style > 4)
        return 0;
    lem_stem_gr(style);
    if (!lem_parts_read(lem_cost_parts(LEM_BANK_STYLE, style), lem_cl_bank))
        return 0;
    if (lem_pk(lem_cl_bank, 0) != 'L' || lem_pk(lem_cl_bank, 1) != 'G' ||
        lem_pk(lem_cl_bank, 2) != 'R' || lem_pk(lem_cl_bank, 3) != 'B')
        return 0;
    if (lem_pk(lem_cl_bank, LEM_GR_STYLE) != style)
        return 0;
    lem_bank_have = style;
    return 1;
}

/* lem_item - one LEMMAIN item's segment and offset, into lem_far_seg/off.
 * 0 = this bank does not carry it, which is how a wave that has not converted
 * its item yet finds out rather than drawing a bank header as a bitmap. */
static int lem_item(int id)
{
    int i, off;

    if (!lem_main_have)
        return 0;
    for (i = 0; i < 16; i++) {
        off = LEM_MN_ITEMS + i * LEM_MN_ISTRIDE;
        if (lem_pk(lem_cl_main, off + LEM_MI_ID) != id)
            continue;
        if (lem_pk16(lem_cl_main, off + LEM_MI_NBYTES) == 0 &&
            lem_pk16(lem_cl_main, off + LEM_MI_NBYTES + 2) == 0)
            return 0;
        lem_far(lem_cl_main,
                lem_pk16(lem_cl_main, off + LEM_MI_OFF),
                lem_pk16(lem_cl_main, off + LEM_MI_OFF + 2));
        return 1;
    }
    return 0;
}
