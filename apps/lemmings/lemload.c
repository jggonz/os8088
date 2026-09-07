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
