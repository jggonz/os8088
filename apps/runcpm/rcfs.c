/* ============================================================================
 * os8088 - apps/runcpm/rcfs.c          drives are folders, files are whole
 *                                      (SPEC.md 74.3)
 *
 * Part of RUNCPM (SPEC.md 74), a reimplementation of RunCPM 6.9 by Marcelo
 * Dantas / "Mockba the Borg" (https://github.com/MockbaTheBorg/RunCPM, MIT
 * licence, Copyright (c) 2017 Mockba the Borg). #included by runcpm.c.
 *
 * WHAT IS RUNCPM'S HERE - the disk layer of disk.h and abstraction_posix.h,
 * behaviour for behaviour: the host layout (drive A..P is a folder named by
 * its letter below FILEBASE, user area 0..F a folder named by its hex digit
 * inside it, A/0 the master disk; _sys_select tests the LETTER's folder only,
 * _SetUser -> _MakeUserDir creates the user folder on USER n for 0..15
 * (NOHIGHUSER), _sys_makedisk (BDOS 249) creates <letter> and <letter>/0);
 * the FCB (36 bytes) and directory entry (32 bytes) layouts; the name rules
 * of _FCBtoHostname (7-bit mask, > 32 kept, upper case, '/' -> '_' NOSLASH,
 * the dot only with an extension, '?' marks a pattern, dr '?' = every name)
 * and _HostnameToFCB / _HostnameToFCBname; match(); _mockupDirEntry's
 * extent, rc and allocation-block synthesis INCLUDING the multi-entry chain a
 * search with EX = '?' walks for a file above one physical extent (two
 * logical extents = 32KB a directory entry, exm 1; blocks of 4KB, 8 16-bit
 * al entries, the phoney block numbers from firstBlockAfterDir = 8); the
 * search state (dirPos, allUsers, allExtents, currFindUser); every operation's
 * return code - _OpenFile 0/0xFF (H = errSELECT after a Select error), _Close
 * (a modified FCB on an R/O drive is 'Bdos Err on X: R/O'; the CCP's $$$.SUB
 * FCB at CCPaddr+0x7AC is TRUNCATED to rc records on close), _MakeFile
 * (fopen "a": an existing file keeps its bytes and the FCB starts at record
 * 0), _SearchFirst/_SearchNext (0 = the entry is at DMA+0, tmpFCB holds the
 * found name), _DeleteFile (every match, PUN/LST capture closed by name),
 * _RenameFile (the new name's drive byte is the old one's), _ReadSeq /
 * _WriteSeq / _ReadRand / _WriteRand (record 128 bytes, cr/ex/s2 stepped as
 * disk.h steps them, 1 = past the end / unwritten, 6 = seek past the disk,
 * 0x10 = a file that is not there, unwritten bytes of a partial record read
 * as 0x1A, a gap written past the end is zeros), _GetFileSize (records,
 * rounded up), _SetRandom, _CheckSUB ($???????.??? on drive A - BATCHA - in
 * the current user area, answering 0xFF to DRV_ALLRESET so the DRI CCP runs
 * SUBMIT's $$$.SUB), A_WRITE/L_WRITE capture into A/0/PUN.TXT and LST.TXT
 * (cpm.h 795-818: created - truncated - the FIRST time in the session and
 * appended to after; deleting the file starts it over).
 *
 * WHAT IS THIS PLATFORM'S: WHOLE FILES IN CLAIMS (SPEC.md 74.3). RunCPM
 * fopens the host file BY NAME on every record and seeks; the file API a
 * package reaches (SPEC.md 18.4, 19) reads and writes WHOLE files with a
 * 16-bit count and no seek, and every file call resolves in the instance's
 * current folder. So:
 *
 *   - THE PLACE. A (drive,user) is a folder cluster, found once by walking
 *     the launch folder with os88_file_find and remembered (the place table);
 *     standing there is os88_file_goto_q_mark - a word, no disk - and every
 *     operation stands in its (drive,user) first (rc_fs_cd). SELECT is a fact
 *     about the drive letter alone; an absent user folder is an EMPTY area
 *     (as a glob of A/3/* is upstream) and never a Select error.
 *   - THE DIRECTORY CACHE. F_SFIRST/F_SNEXT read a 128-entry cache of a
 *     (drive,user)'s files - name in FCB form and the 32-bit size, in name
 *     order as upstream's glob() sorts them, so DIR reads the same - in
 *     TWO SLOTS, the least recently wanted one refilled (a session on B:
 *     keeps A:'s slot for the .COMs and $$$.SUB; PIP B:=A:*.* refills
 *     neither), each filled by ONE os88_file_find walk. THE WALK IS PRICED
 *     in os88_file_find calls: dsk_find is by ordinal and re-reads the
 *     folder's directory sectors from its first one each call, so a folder
 *     of F files is sum(ceil(k/16), k = 1..F+1) sector reads - MEASURED
 *     238 int 13h calls for A\0's 77 files on a DIRW1=1 kernel (no
 *     sector cache; ~560 at its 126-file cap) and 0 with SPEC.md 18.95's
 *     track cache, which serves every re-seek from RAM (SPEC.md 74.3) - a
 *     machine with no room for that cache pays ~400 ms a call, ~95 s,
 *     with the glass frozen inside this wake, so the fill SAYS so first
 *     (rc_say_now: an immediate toast, and the terminal line when
 *     fullscreen). It is paid at the first DIR, TYPE or open of an area
 *     (never at boot: _CheckSUB is a remembered fact) and kept: our own MAKE/DELETE/RENAME update it in place, and it survives
 *     warm boots (a walk per ^C would be seconds on the target; what another
 *     program puts in the folder while RunCPM runs is not seen until the
 *     area's slot is refilled).
 *   - THE OPEN-FILE TABLE. Eight entries, each a heap claim holding the WHOLE
 *     file: F_OPEN (and any record call on an FCB the table does not know -
 *     upstream is by name, so a read after a close simply works) reads it in
 *     with os88_file_read_seg; a record read is a 128-byte copy out of the
 *     claim, a record write a copy in (the claim regrown when it must, a gap
 *     zero-filled); F_CLOSE writes a modified file back WHOLE with
 *     os88_file_write_seg and KEEPS the entry, clean, so a reopen in the same
 *     program (SAVE then LOAD, PIP's passes) reads nothing; a warm boot or
 *     the exit flushes what is left and releases every claim - RunCPM does
 *     not hold heap for programs that are over. A ninth open evicts the
 *     least recently used CLEAN entry, and when none is clean the open errs
 *     'Bdos Err on X: CP/M ERR' with a toast (SPEC.md 74.4). A file above
 *     65,535 bytes cannot be held
 *     - the read is a 16-bit count - so open/make/write refuse it as a FACT
 *     with a toast naming the file (SPEC.md 47, 74.4); DIR still lists it
 *     with its whole extent chain; the master disk's three such files are
 *     not shipped. Records stop at 511 (65,408 bytes written), the 16-bit
 *     size's last whole record.
 *
 * Every function here is called on the UI task with no lock held (SPEC.md
 * 74.1: the BDOS runs on the wake), which is what makes the file slots legal
 * from a BDOS call at all. Every buffer is static (SPEC.md 73.5).
 *
 * TWO SEGMENTS (SPEC.md 73.14, 71): the resident image + bss passed the
 * 55,000 trigger in wave 4, so the PER-COMMAND half of this file is module
 * code in RUNCPM.OVL - the ovl_* names: ERA/REN/MAKE, F_OPEN/F_CLOSE, the
 * search and _mockupDirEntry, F_SIZE/F_RANDREC, USER n's folder, BDOS 249,
 * _CheckSUB - and what a RECORD, a punched byte or a keystroke touches stays
 * resident: F_READ/F_WRITE and the random records, the capture files, the
 * open-file table (rc_oft_get loads a file for a record call after a close),
 * the directory cache, the names. The module is loaded by rc_boot's
 * ovl_patch_cpm on the first wake before any folder move and the machine
 * does not boot without it, so no ovl_* here can find the module absent and
 * every one keeps its natural BDOS code (0 = success). Never take the
 * address of one (SPEC.md 73.14).
 * ==========================================================================*/

#define RC_DRIVES 16
#define RC_USERS  16

/* globals.h: the CP/M record geometry */
#define RC_BLKSZ   128                       /* BlkSZ - a record */
#define RC_BLKEX   128                       /* BlkEX - records an extent */
#define RC_MAXEX   31
#define RC_MAXS2   15
#define RC_MAXCR   128
#define RC_EXTPDE  2                         /* extentsPerDirEntry = exm + 1 */
#define RC_FBAD    8                         /* firstBlockAfterDir: al0 0xFF */
#define RC_BATCHFCB (RC_CCPADDR + 0x7AC)     /* CCP_DR: the $$$.SUB FCB */

/* the last record this model can READ, 511 (a 65,535-byte file's tail),
 * and the last it can WRITE, 510 (65,408 bytes: the 16-bit size's last
 * whole record) - SPEC.md 74.3 */
#define RC_MAXREC_R 511
#define RC_MAXREC_W 510

/* --- the place: (drive,user) -> folder --------------------------------- */
static struct os88_place rc_home;            /* the launch folder: FILEBASE */
static struct os88_place rc_cur;             /* where the instance stands */
static struct os88_find rc_ff;               /* os88_file_find's answer */
static unsigned rc_pl_clus[RC_DRIVES * RC_USERS];    /* (drive,user) -> the
                                              * folder's first cluster... */
static unsigned char rc_pl_ok[RC_DRIVES * RC_USERS]; /* ...once resolved: 1
                                              * = known, 2 = known absent, 3
                                              * = absent and UNMAKEABLE this
                                              * session (a USER n whose mkdir
                                              * the disk refused: full or
                                              * protected - the DRI CCP sets
                                              * the user at every warm boot,
                                              * and one refusal is enough
                                              * disk work; an ERA that frees
                                              * something, or BDOS 249,
                                              * resets it) */
static unsigned rc_dr_clus[RC_DRIVES];       /* drive -> the letter folder's
                                              * first cluster... */
static unsigned char rc_dr_ok[RC_DRIVES];    /* ...once resolved: 1 = known,
                                              * 2 = known absent (the Select
                                              * fact, per letter) */
static char rc_fs_name[13];                  /* a folder or file name */
static unsigned rc_n_find;                   /* os88_file_find calls: the
                                              * measure of a folder walk */

/* --- the directory cache: TWO (drive,user) areas' files ---------------- */
#define RC_DC_MAX 128
#define RC_DC_SLOTS 2
struct rc_dent {
    char name[11];                           /* FCB form: 8 + 3, spaces */
    unsigned char big;                       /* size_hi != 0: > 65,535 */
    unsigned size_lo, size_hi;
};
struct rc_dcache {
    int valid;                               /* the slot describes... */
    int drive, user;                         /* ...this (drive,user) */
    int here;                                /* ...and its folder EXISTS (0 =
                                              * an absent user area: empty,
                                              * nothing can be made there) */
    int n;
    unsigned lru;                            /* rc_dc_clock at last want */
    struct rc_dent e[RC_DC_MAX];             /* 16 bytes each: 2KB */
};
static struct rc_dcache rc_dcs[RC_DC_SLOTS]; /* two slots, least recently
                                              * wanted replaced: a session on
                                              * B: keeps A:'s (the CCP's
                                              * .COMs, $$$.SUB, PUN.TXT) and
                                              * PIP B:=A:*.* walks neither
                                              * twice - 4KB of bss */
static struct rc_dcache *rc_dcc;             /* the slot rc_dc_want chose */
static unsigned rc_dc_clock;
static char rc_dc_n11[11];                   /* the fill's name scratch */
static unsigned char rc_subfact[RC_USERS];   /* _CheckSUB's answer per user
                                              * area of A: - 0 unknown, 1 no
                                              * $-file, 2 one there (kept by
                                              * our own MAKE/ERA/REN and by
                                              * every fill of an (A,u) slot;
                                              * resolved by NAME while
                                              * unknown) */

/* --- the open-file table ------------------------------------------------ */
#define RC_OFT 8
struct rc_ofile {
    char name[11];
    unsigned char drive, user, dirty, live, pad0;
    unsigned seg;                            /* the claim */
    unsigned size;                           /* bytes, 0..65535 */
    unsigned kb;                             /* the claim's size */
    unsigned lru;                            /* rc_oft_clock at last use */
    unsigned pad1[4];                        /* 32 bytes a row: index by shift */
};
static struct rc_ofile rc_oft[RC_OFT];
static unsigned rc_oft_clock;
static int rc_pun_open, rc_lst_open;         /* cpm.h pun_open / lst_open */

/* --- the FCB at DE, copied out; the search state ------------------------ */
static unsigned char rc_fcb[36];
static unsigned rc_fcbaddr;
static char rc_hname[13];                    /* "NAME.EXT" for the OS */
static char rc_hname2[13];                   /* ...the second (rename) */
static char rc_n11[11];                      /* an 11-char FCB name */
static int rc_s_drive, rc_s_user, rc_s_pos;  /* the search: drive, the user
                                              * folder walked (currFindUser),
                                              * dirPos */
static int rc_s_allusers, rc_s_allext;
static char rc_s_pat[11];
static char rc_s_name[11];                   /* the file whose chain runs */
static unsigned rc_s_recs, rc_s_exts, rc_s_used, rc_s_ffab;   /* fileRecords,
                                              * fileExtents, fileExtentsUsed,
                                              * firstFreeAllocBlock */
static unsigned char rc_dirent[32];
static char rc_tmsg[26];                     /* a toast: 24 characters */

static struct rc_dcache *rc_dc_slot(int d, int u);
static void rc_say_now(const char *msg);

static void rc_fs_init(void)
{
    os88_file_here(&rc_home);
    rc_cur.clus = rc_home.clus;
    rc_cur.vol = rc_home.vol;
}

/* rc_fs_subdir - the first cluster of folder `name` in the CURRENT folder,
 * or -1 (as an int: a cluster is never 0xFFFF on a FAT12 floppy). One
 * os88_file_find walk: O(entries) int 13h calls the first time a folder is
 * looked at, which is why the answer is remembered in rc_pl_clus. */
static int rc_fs_subdir(const char *name)
{
    int o = 0;
    while ((o = os88_file_find(o, &rc_ff)) >= 0) {
        rc_n_find++;
        if (rc_ff.type != OS88_FT_DIR)
            continue;
        if (rc_ff.name[0] == name[0] && rc_ff.name[1] == name[1] &&
            rc_ff.name[2] == 0)
            return (int)rc_ff.clus;
    }
    rc_n_find++;
    return -1;
}

/* rc_fs_stand - stand the instance in a folder of the launch volume */
static int rc_fs_stand(unsigned clus)
{
    if (rc_cur.clus == clus && rc_cur.vol == rc_home.vol)
        return 0;
    if (os88_file_goto_q_mark(clus, rc_home.vol) < 0)
        return -1;
    rc_cur.clus = clus;
    rc_cur.vol = rc_home.vol;
    return 0;
}

/* rc_fs_home - stand where this instance was launched (FILEBASE) */
static int rc_fs_home(void)
{
    return rc_fs_stand(rc_home.clus);
}

/* rc_fs_drive_exists - _sys_select's fact: is there a folder named by the
 * drive's letter below the launch folder? Resolved once per letter and
 * remembered; the resolution stands the instance in the launch folder to
 * walk it, and nothing else moves. */
static int rc_fs_drive_exists(int d)
{
    int c;
    if (d < 0 || d >= RC_DRIVES)
        return 0;
    if (rc_dr_ok[d] == 0) {
        if (rc_fs_home() < 0)
            return 0;
        rc_fs_name[0] = (char)('A' + d);
        rc_fs_name[1] = 0;
        c = rc_fs_subdir(rc_fs_name);
        if (c < 0) {
            rc_dr_ok[d] = 2;
        } else {
            rc_dr_clus[d] = (unsigned)c;
            rc_dr_ok[d] = 1;
        }
    }
    return rc_dr_ok[d] == 1;
}

/* rc_fs_cd - stand the instance in drive d (0 = A) user u (0..31): the
 * folder <letter>\<hex digit> below the launch folder. 0 = standing in the
 * user folder; 1 = the drive's folder exists but the user folder does not
 * (users 16..31 have no folder: NOHIGHUSER) - the instance stands in the
 * DRIVE's folder, and the area reads as EMPTY (rc_dc_want); -1 = there is
 * no such DRIVE (SPEC.md 74.3: 'Bdos Err on X: Select', the fact _sys_select
 * decides from the letter alone), and the instance is left where it was.
 * Each fact is looked up ONCE per drive and once per (drive,user). */
static int rc_fs_cd(int d, int u)
{
    int i, c;
    unsigned dst;
    if (u < 0 || !rc_fs_drive_exists(d))
        return -1;
    i = (d << 4) + (u & 15);
    if (u >= RC_USERS) {
        dst = rc_dr_clus[d];
        goto stand;
    }
    if (rc_pl_ok[i] == 0) {                  /* the user folder, once */
        if (rc_fs_stand(rc_dr_clus[d]) < 0)
            return -1;
        rc_fs_name[0] = (char)(u < 10 ? '0' + u : 'A' + u - 10);
        rc_fs_name[1] = 0;
        c = rc_fs_subdir(rc_fs_name);
        if (c < 0) {
            rc_pl_ok[i] = 2;
        } else {
            rc_pl_clus[i] = (unsigned)c;
            rc_pl_ok[i] = 1;
        }
    }
    dst = (rc_pl_ok[i] == 1) ? rc_pl_clus[i] : rc_dr_clus[d];
stand:
    if (rc_fs_stand(dst) < 0)
        return -1;
    return (u < RC_USERS && rc_pl_ok[i] == 1) ? 0 : 1;
}

/* ovl_pl_unmark - every 'unmakeable' user folder (rc_pl_ok 3) is merely
 * absent again: something was deleted, so the disk may have room */
static void ovl_pl_unmark(void)
{
    int i;
    for (i = 0; i < RC_DRIVES * RC_USERS; i++)
        if (rc_pl_ok[i] == 3)
            rc_pl_ok[i] = 2;
}

/* ovl_fs_mkuser - _MakeUserDir: create the user folder <hex> of (d,u) when
 * the drive exists and the folder does not, and stand in it. Silent on any
 * refusal (upstream ignores mkdir's answer): a full disk leaves the area
 * absent, and reads as empty. */
static void ovl_fs_mkuser(int d, int u)
{
    int i, c;
    if (u >= RC_USERS || rc_fs_cd(d, u) != 1)
        return;                              /* absent drive, or it exists */
    i = (d << 4) + u;
    if (rc_pl_ok[i] == 3)
        return;                              /* refused once this session:
                                              * not asked again at every ^C */
    rc_fs_name[0] = (char)(u < 10 ? '0' + u : 'A' + u - 10);
    rc_fs_name[1] = 0;
    if (os88_file_mkdir(rc_fs_name) < 0 && os88_ferr() != OS88_FERR_EXIST) {
        rc_pl_ok[i] = 3;                     /* full, or protected */
        return;
    }
    c = rc_fs_subdir(rc_fs_name);            /* standing in the drive folder */
    if (c < 0)
        return;
    rc_pl_clus[i] = (unsigned)c;
    rc_pl_ok[i] = 1;
    rc_fs_stand((unsigned)c);
    if (rc_dc_slot(d, u))
        rc_dcc->valid = 0;                   /* it was cached as absent */
}

/* ==========================================================================
 * NAMES - disk.h _FCBtoHostname / _HostnameToFCB / _HostnameToFCBname
 * ========================================================================*/

/* rc_fcb2host - the FCB's fn/tp as "NAME.EXT" (no drive, no folder: the
 * folder is where the instance stands). Answers 1 when the name is unique
 * (no '?'), 0 for a pattern; out[0] == 0 is disk.h's `!filename[4]` -
 * an invalid (empty) name. */
static int rc_fcb2host(const unsigned char *f, char *out)
{
    int i, unique = 1, dot = 1, c;
    if (f[0] == '?') {
        for (i = 0; i < 8; i++)
            *out++ = '?';
        *out++ = '.';
        for (i = 0; i < 3; i++)
            *out++ = '?';
        *out = 0;
        return 0;
    }
    for (i = 0; i < 8; i++) {
        c = f[1 + i] & 0x7F;
        if (c == '/')
            c = '_';                         /* NOSLASH */
        if (c > 32) {
            if (c >= 'a' && c <= 'z')
                c -= 32;
            *out++ = (char)c;
        }
        if (c == '?')
            unique = 0;
    }
    for (i = 0; i < 3; i++) {
        c = f[9 + i] & 0x7F;
        if (c > 32) {
            if (dot) {
                dot = 0;
                *out++ = '.';                /* only with an extension */
            }
            if (c == '/')
                c = '_';
            if (c >= 'a' && c <= 'z')
                c -= 32;
            *out++ = (char)c;
        }
        if (c == '?')
            unique = 0;
    }
    *out = 0;
    return unique;
}

/* rc_host2n11 - "NAME.EXT" -> 11 characters, space padded, upper case
 * (_HostnameToFCBname without the terminating NUL) */
static void rc_host2n11(const char *h, char *to)
{
    int i = 0, c;
    while (*h && *h != '.') {
        c = *h++;
        if (c >= 'a' && c <= 'z')
            c -= 32;
        if (i < 8)
            to[i] = (char)c;
        i++;
    }
    while (i < 8)
        to[i++] = ' ';
    if (*h == '.')
        h++;
    i = 0;
    while (*h) {
        c = *h++;
        if (c >= 'a' && c <= 'z')
            c -= 32;
        if (i < 3)
            to[8 + i] = (char)c;
        i++;
    }
    while (i < 3)
        to[8 + i++] = ' ';
}

/* rc_n11_host - 11 characters -> "NAME.EXT" for the OS */
static void rc_n11_host(const char *n, char *out)
{
    int i, e;
    for (i = 0; i < 8 && n[i] != ' '; i++)
        *out++ = n[i];
    for (e = 0; e < 3 && n[8 + e] != ' '; e++)
        ;
    if (e) {
        *out++ = '.';
        for (i = 0; i < e; i++)
            *out++ = n[8 + i];
    }
    *out = 0;
}

static int rc_n11_eq(const char *a, const char *b)
{
    int i;
    for (i = 0; i < 11; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

/* disk.h match(): '?' in the pattern matches any character */
static int rc_match(const char *name, const char *pat)
{
    int i;
    for (i = 0; i < 11; i++)
        if (pat[i] != '?' && pat[i] != name[i])
            return 0;
    return 1;
}

/* the FCB at DE, in and out */
static void rc_fcb_load(unsigned addr)
{
    rc_fcbaddr = addr;
    rc_zcopy_out(rc_fcb, addr, 36);
}
static void rc_fcb_store(void)
{
    rc_zcopy_in(rc_fcbaddr, rc_fcb, 36);
}

/* rc_seldisk - disk.h _SelectDisk(dr): the FCB's drive byte, 0 or '?' for
 * the current drive, else 1 = A. Stands the instance in (drive, user) -
 * the fact tested is the LETTER's folder (rc_fs_cd answers 0 or 1 for it,
 * -1 only for an absent drive). Answers the drive 0..15, or -1 after
 * printing 'Bdos Err on X: Select' (rc_error: the caller answers 0xFF and
 * rc_bdos finishes the wait). */
static int rc_seldisk(int dr, int u)
{
    int d;
    if (dr == 0 || dr == '?')
        d = rc_cdrive;
    else
        d = dr - 1;
    if (rc_fs_cd(d, u) < 0) {
        rc_cdrive = rc_odrive = d;
        rc_error(RC_ERR_SELECT);
        return -1;
    }
    rc_login |= 1 << d;
    return d;
}

/* ==========================================================================
 * THE DIRECTORY CACHE
 * ========================================================================*/

/* rc_dc_insert - a row into the cache IN NAME ORDER - the order of the HOST
 * name "NAME.EXT" (upstream's glob() sorts the host strings, so DIR is
 * alphabetical there and here; the FCB form orders "A-B.TXT" after "A.TXT",
 * the host form before, '-' < '.' - so both rows are compared in host form,
 * the new one converted once, the probed one per probe of a binary search:
 * ~7 conversions an insert, ~550 for A\0's fill); the caller has checked
 * there is room */
static char rc_dc_hn[13], rc_dc_hq[13];
static void rc_dc_insert(const char *n11, unsigned lo, unsigned hi)
{
    struct rc_dent *q, *p;                  /* by pointer: rows are 16 bytes */
    int lo_i = 0, hi_i = rc_dcc->n, mid, j;
    rc_n11_host(n11, rc_dc_hn);
    while (lo_i < hi_i) {
        mid = (lo_i + hi_i) >> 1;
        rc_n11_host(rc_dcc->e[mid].name, rc_dc_hq);
        for (j = 0; rc_dc_hq[j] && rc_dc_hq[j] == rc_dc_hn[j]; j++)
            ;
        if ((unsigned char)rc_dc_hq[j] > (unsigned char)rc_dc_hn[j])
            hi_i = mid;                      /* the row is after the new one */
        else
            lo_i = mid + 1;
    }
    q = rc_dcc->e + lo_i;
    for (p = rc_dcc->e + rc_dcc->n; p > q; p--)
        os88_memcpy(p, p - 1, sizeof(struct rc_dent));
    os88_memcpy(q->name, n11, 11);
    q->size_lo = lo;
    q->size_hi = hi;
    q->big = (hi != 0);
    rc_dcc->n++;
}

/* rc_dc_slot - the slot describing (d,u), made rc_dcc, or 0 (rc_dcc left) */
static struct rc_dcache *rc_dc_slot(int d, int u)
{
    struct rc_dcache *p = rc_dcs;            /* walked by pointer: an index
                                              * would be an imul by 2,060 */
    int i;
    for (i = 0; i < RC_DC_SLOTS; i++, p++)
        if (p->valid && p->drive == d && p->user == u) {
            rc_dcc = p;
            return p;
        }
    return 0;
}

/* rc_dc_subfact - the $-file fact of A\u, from a slot that describes it */
static void rc_dc_subfact(void)
{
    int i;
    if (rc_dcc->drive != 0 || rc_dcc->user >= RC_USERS)
        return;
    rc_subfact[rc_dcc->user] = 1;
    for (i = 0; i < rc_dcc->n; i++)
        if (rc_dcc->e[i].name[0] == '$') {
            rc_subfact[rc_dcc->user] = 2;
            return;
        }
}

/* rc_dc_want - a slot describes (d,u), and it is rc_dcc: filled if none did
 * (the least recently wanted slot is the one refilled). Answers 0 with the
 * folder present, 1 when the user area is absent (the slot is empty and
 * nothing can be made there), -1 for an absent drive. The fill is the one
 * os88_file_find walk (rc_n_find counts it: SPEC.md 74.3 prices it). */
static int rc_dc_want(int d, int u)
{
    struct rc_dcache *p;
    int r, o, i;
    if (rc_dc_slot(d, u)) {
        rc_dcc->lru = ++rc_dc_clock;
        return rc_dcc->here ? 0 : 1;
    }
    r = rc_fs_cd(d, u);
    if (r < 0)
        return -1;
    rc_dcc = rc_dcs;
    for (i = 1, p = rc_dcs + 1; i < RC_DC_SLOTS; i++, p++)
        if (!p->valid || (rc_dcc->valid && p->lru < rc_dcc->lru))
            rc_dcc = p;                      /* the least recently wanted */
    rc_dcc->valid = 1;
    rc_dcc->drive = d;
    rc_dcc->user = u;
    rc_dcc->here = (r == 0);
    rc_dcc->lru = ++rc_dc_clock;
    rc_dcc->n = 0;
    if (!rc_dcc->here) {
        rc_dc_subfact();
        return 1;
    }
    /* the walk is seconds on a machine without the track cache (SPEC.md
     * 74.3: 238 int 13h calls for A\0), inside THIS wake with the glass
     * frozen - say so first: "RunCPM: reading A:0" (19 chars) */
    os88_strcpy(rc_tmsg, "RunCPM: reading ", sizeof(rc_tmsg));
    rc_tmsg[16] = (char)('A' + d);
    rc_tmsg[17] = ':';
    if (u < 10) {
        rc_tmsg[18] = (char)('0' + u);
        rc_tmsg[19] = 0;
    } else {
        rc_tmsg[18] = '1';
        rc_tmsg[19] = (char)('0' + u - 10);
        rc_tmsg[20] = 0;
    }
    rc_say_now(rc_tmsg);
    o = 0;
    while (rc_dcc->n < RC_DC_MAX && (o = os88_file_find(o, &rc_ff)) >= 0) {
        rc_n_find++;
        if (rc_ff.type >= OS88_FT_DIR)
            continue;
        rc_host2n11(rc_ff.name, rc_dc_n11);  /* its own scratch: the caller
                                              * may be holding rc_n11 */
        rc_dc_insert(rc_dc_n11, rc_ff.size_lo, rc_ff.size_hi);
    }
    rc_n_find++;
    rc_dc_subfact();
    return 0;
}

static int rc_dc_find(const char *n11)
{
    int i;
    for (i = 0; i < rc_dcc->n; i++)
        if (rc_n11_eq(rc_dcc->e[i].name, n11))
            return i;
    return -1;
}

/* rc_dc_note - our own change to (d,u): a file made or resized. Only the
 * cache that describes (d,u) is touched; another one is refilled when it
 * is next wanted. */
static void rc_dc_note(int d, int u, const char *n11, unsigned size)
{
    int i;
    if (d == 0 && u < RC_USERS && n11[0] == '$')
        rc_subfact[u] = 2;                   /* a $-file on A: - whether or
                                              * not a slot describes A\u */
    if (!rc_dc_slot(d, u))
        return;
    i = rc_dc_find(n11);
    if (i < 0) {
        if (rc_dcc->n >= RC_DC_MAX)
            return;
        rc_dc_insert(n11, size, 0);
        return;
    }
    rc_dcc->e[i].size_lo = size;
    rc_dcc->e[i].size_hi = 0;
    rc_dcc->e[i].big = 0;
}

static void rc_dc_forget(int d, int u, const char *n11)
{
    int i;
    if (!rc_dc_slot(d, u)) {
        if (d == 0 && u < RC_USERS && n11[0] == '$')
            rc_subfact[u] = 0;               /* unknown again: resolved by
                                              * name at the next _CheckSUB */
        return;
    }
    i = rc_dc_find(n11);
    if (i >= 0) {
        rc_dcc->n--;
        while (i < rc_dcc->n) {
            os88_memcpy(&rc_dcc->e[i], &rc_dcc->e[i + 1], sizeof(struct rc_dent));
            i++;
        }
    }
    if (n11[0] == '$')
        rc_dc_subfact();
}

/* ==========================================================================
 * THE OPEN-FILE TABLE
 * ========================================================================*/

/* rc_say - a refusal's FACT (SPEC.md 47), <= 24 characters: a toast - AND,
 * while the window is fullscreen (Alt+F), the same text on the terminal as
 * rc_error prints its line, "\r\n" msg "\r\n" (one row = one band), where
 * upstream's disk.h prints - because a toast is the BAR's strip and the bar
 * is under a fullscreen window (kernel/menu.inc menu_draw_bar early-outs on
 * wm_fs_vis), so it would expire unseen (SPEC.md 74.4). */
static void rc_say(const char *msg)
{
    os88_toast(msg, 0);
    if (rc_full) {
        rc_puts("\r\n");
        rc_puts(msg);
        rc_puts("\r\n");
    }
}

/* rc_say_now - rc_say for a WAIT, not a refusal: the caller is about to
 * spend seconds inside this wake (the walk of an area on a machine without
 * SPEC.md 18.95's track cache) and the message must reach the glass BEFORE,
 * not after. The BDOS runs on the wake without the lock, so a plain toast
 * takes toast_show's STAGED path (SPEC.md 59.4) and appears after the walk;
 * under os88_gfx_lock it takes toast_now's immediate path. Fullscreen: the
 * terminal line is flushed on the spot, the slice's own bracket - one band.
 * One lock take and one strip draw per area per session. */
static void rc_say_now(const char *msg)
{
    os88_gfx_lock();
    rc_say(msg);
    if (rc_full && os88_wm_clip_set(rc_win) == 0)
        rc_flush(rc_win);
    os88_gfx_unlock();
}

static void rc_toast_name(const char *head, const char *n11, const char *tail)
{
    unsigned n;
    os88_strcpy(rc_tmsg, head, sizeof(rc_tmsg));
    n = os88_strlen(rc_tmsg);
    rc_n11_host(n11, rc_tmsg + n);
    n = os88_strlen(rc_tmsg);
    os88_strcpy(rc_tmsg + n, tail, sizeof(rc_tmsg) - n);
    rc_say(rc_tmsg);
}

static int rc_oft_find(int d, int u, const char *n11)
{
    int i;
    for (i = 0; i < RC_OFT; i++)
        if (rc_oft[i].live && rc_oft[i].drive == d && rc_oft[i].user == u &&
            rc_n11_eq(rc_oft[i].name, n11))
            return i;
    return -1;
}

/* rc_oft_drop - the entry goes, its bytes with it (a delete, an eviction) */
static void rc_oft_drop(int i)
{
    if (rc_oft[i].live && rc_oft[i].seg)
        os88_mem_free(rc_oft[i].seg);
    rc_oft[i].live = 0;
    rc_oft[i].seg = 0;
}

/* rc_oft_flush - write a modified file back WHOLE. The entry STAYS, clean:
 * a program that closes and reopens the same file (MBASIC's SAVE then LOAD,
 * TE's save-and-continue, PIP's second pass, an editor's .BAK cycle) does
 * not read it again - the claim goes when the table or the heap needs it
 * (rc_oft_evict) or when the program ends (rc_fs_flush_all). Answers 0, or
 * -1 when the write failed (the reason toasted: the disk is full,
 * write-protected, or the folder is gone) - then the entry is dropped and
 * the bytes are lost, as upstream's would be on a failed fwrite. */
static int rc_oft_flush(int i)
{
    int r = 0;
    if (!rc_oft[i].live)
        return 0;
    if (rc_oft[i].dirty) {
        if (rc_fs_cd(rc_oft[i].drive, rc_oft[i].user) == 0) {
            rc_n11_host(rc_oft[i].name, rc_hname);
            r = os88_file_write_seg(rc_hname, rc_oft[i].seg, rc_oft[i].size);
            if (r == 0)
                rc_dc_note(rc_oft[i].drive, rc_oft[i].user, rc_oft[i].name,
                           rc_oft[i].size);
        } else {
            r = -1;
        }
        if (r < 0) {
            /* the FACT, in a toast (SPEC.md 47): the folder has no free
             * slot (the kernel does not grow a directory, SPEC.md 18.5 -
             * A\0 ships with spare ones, a USER folder holds 14 files on a
             * 512-byte-cluster disk), the disk has no cluster, the file is
             * protected, or the folder is gone / a read error */
            int e = os88_ferr();
            rc_toast_name(e == OS88_FERR_DIRFULL ? "Dir full: " :
                          e == OS88_FERR_FULL ? "Disk full: " :
                          (e == OS88_FERR_PROT || e == OS88_FERR_WPROT) ? "Protected: " :
                          "Not saved: ", rc_oft[i].name, "");
            rc_oft_drop(i);
            return r;
        }
        rc_oft[i].dirty = 0;
    }
    return 0;
}

/* rc_fs_flush_all - a warm boot, or the exit: every open file written back
 * and RELEASED - the program that held them is gone (main.c's loop laps),
 * and RunCPM does not hold heap for programs that are over (SPEC.md 74.3:
 * the next program's open of the same file reads it again, as a CP/M
 * machine's CCP reads a .COM from the disk every time) */
static void rc_fs_flush_all(void)
{
    int i;
    for (i = 0; i < RC_OFT; i++) {
        rc_oft_flush(i);
        rc_oft_drop(i);
    }
}

/* rc_oft_evict - drop the least recently used CLEAN entry; 0 = none */
static int rc_oft_evict(void)
{
    int i, v = -1;
    for (i = 0; i < RC_OFT; i++)
        if (rc_oft[i].live && !rc_oft[i].dirty &&
            (v < 0 || rc_oft[i].lru < rc_oft[v].lru))
            v = i;
    if (v < 0)
        return 0;
    rc_oft_drop(v);
    return 1;
}

/* rc_oft_slot - a free row, evicting a clean one if the table is full;
 * -1 with the toast when every entry is a modified file (SPEC.md 74.4) */
static int rc_oft_slot(void)
{
    int i;
    for (;;) {
        for (i = 0; i < RC_OFT; i++)
            if (!rc_oft[i].live)
                return i;
        if (!rc_oft_evict()) {
            rc_say("RunCPM: 8 files open");
            rc_error(RC_ERR_CPM);            /* 'Bdos Err on X: CP/M ERR' */
            return -1;
        }
    }
}

/* rc_oft_claim - a claim of kb KB, evicting clean entries until it can be
 * had; 0 = refused */
static unsigned rc_oft_claim(int kb)
{
    unsigned s;
    while ((s = os88_mem_claim(kb)) == 0)
        if (!rc_oft_evict())
            return 0;
    return s;
}

/* rc_oft_grow - room for `need` bytes in entry i (regrow, moving if it
 * must; clean entries evicted for the room); 0 = ok, -1 = refused */
static int rc_oft_grow(int i, unsigned need)
{
    unsigned s;
    int kb;
    if (rc_oft[i].kb >= 64 || need <= (rc_oft[i].kb << 10))
        return 0;
    kb = (int)((need >> 10) + ((need & 1023) ? 1 : 0)) + 3;   /* 3KB of slack:
                                              * a SAVE growing
                                              * a record at a time regrows
                                              * every 24 records, not 8 */
    if (kb > 64)
        kb = 64;
    while ((s = os88_mem_regrow(rc_oft[i].seg, kb)) == 0)
        if (!rc_oft_evict())
            return -1;
    rc_oft[i].seg = s;
    rc_oft[i].kb = (unsigned)kb;
    return 0;
}

/* rc_oft_get - the entry for (d,u,name), loading the file when the table
 * does not hold it and `mode` allows: 0 = only if it exists (the whole file
 * read into a claim), 1 = create empty if it does not exist (F_MAKE: an
 * existing file keeps its bytes, fopen "a"), 2 = create empty, TRUNCATING
 * an existing one (the capture files' first open). Answers the row, or
 * -1: not found / too big (toasted) / no memory (toasted) / no folder. */
static int rc_oft_get(int d, int u, const char *n11, int mode)
{
    int i, k, exists;
    unsigned size, n, cap;
    i = rc_oft_find(d, u, n11);
    if (i >= 0) {
        if (mode == 2) {
            rc_oft[i].size = 0;
            rc_oft[i].dirty = 1;
        }
        rc_oft[i].lru = ++rc_oft_clock;
        return i;
    }
    if (rc_dc_want(d, u) != 0)
        return -1;                           /* absent area: nothing here, and
                                              * nothing can be made here */
    k = rc_dc_find(n11);
    exists = (k >= 0);
    size = 0;
    if (exists) {
        if (rc_dcc->e[k].big) {
            rc_toast_name("RunCPM: ", n11, ">64K");
            return -1;
        }
        size = rc_dcc->e[k].size_lo;
    } else if (mode == 0) {
        return -1;
    }
    i = rc_oft_slot();
    if (i < 0)
        return -1;
    if (mode == 2)
        size = 0;
    k = (int)((size >> 10) + ((size & 1023) ? 1 : 0));   /* 16 bits: never
                                              * size + 1023 */
    if (k < 1)
        k = 1;
    rc_oft[i].seg = rc_oft_claim(k);
    if (rc_oft[i].seg == 0) {
        rc_toast_name("RunCPM RAM? ", n11, "");
        return -1;
    }
    rc_oft[i].kb = (unsigned)k;
    rc_oft[i].live = 1;
    rc_oft[i].drive = (unsigned char)d;
    rc_oft[i].user = (unsigned char)u;
    os88_memcpy(rc_oft[i].name, n11, 11);
    rc_oft[i].size = size;
    rc_oft[i].dirty = (mode != 0 && (!exists || mode == 2));
    rc_oft[i].lru = ++rc_oft_clock;
    if (exists && mode != 2 && size) {
        rc_n11_host(n11, rc_hname);
        cap = (k >= 64) ? 0xFFFF : (unsigned)(k << 10);
        n = 0;
        if (rc_fs_cd(d, u) == 0)             /* STAND THERE: rc_dc_want does
                                              * not move when the slot was
                                              * already valid, and the last
                                              * flush may have left the
                                              * instance in another area */
            n = os88_file_read_seg(rc_hname, rc_oft[i].seg, cap);
        if (n != size) {
            /* the file changed under the cache, or the read failed */
            rc_oft_drop(i);
            if (rc_dc_slot(d, u))
                rc_dcc->valid = 0;
            return -1;
        }
    }
    return i;
}

/* the FCB's (drive, name) as an OFT key: the drive from rc_seldisk, the
 * name from fn/tp, the user the current one (upstream is by name and by
 * the current userCode on every call) */
static int rc_oft_of_fcb(int d, int mode)
{
    if (!rc_fcb2host(rc_fcb, rc_hname))
        return -1;                           /* a pattern is not a file */
    if (rc_hname[0] == 0)
        return -1;
    rc_host2n11(rc_hname, rc_n11);
    return rc_oft_get(d, rc_user, rc_n11, mode);
}

/* ==========================================================================
 * RECORDS - disk.h _ReadSeq/_WriteSeq/_ReadRand/_WriteRand's record moves,
 * against the claim: 128 bytes between the DMA and offset rec * 128
 * ========================================================================*/

/* rc_rec_read - answers disk.h's codes: 0 read, 1 past the end (nothing
 * moved) */
static int rc_rec_read(int i, unsigned rec)
{
    unsigned off, n;
    if (rec > RC_MAXREC_R)
        return 1;
    off = (rec << 7) & 0xFFFF;
    if (off >= rc_oft[i].size)
        return 1;
    n = rc_oft[i].size - off;
    if (n > RC_BLKSZ)
        n = RC_BLKSZ;
    rc_zzcopy_in(rc_dma, rc_oft[i].seg, off, n);
    if (n < RC_BLKSZ)                        /* the partial last record: the
                                              * rest is ^Z, as _sys_readseq's
                                              * 0x1A-filled dmabuf */
        rc_zfill((rc_dma + n) & 0xFFFF, 0x1A, RC_BLKSZ - n);
    rc_oft[i].lru = ++rc_oft_clock;
    return 0;
}

/* rc_rec_write - 0 written; 0xFF = the record could not be held (past what
 * this model holds - toasted - or no memory), the code upstream's
 * _sys_writeseq/_sys_writerand answer for a failed fwrite; a gap past the
 * end is zeros (fseek + fwrite) */
static int rc_rec_write(int i, unsigned rec)
{
    unsigned off, end;
    if (rec > RC_MAXREC_W) {
        rc_toast_name("RunCPM: ", rc_oft[i].name, ">64K");
        return 0xFF;
    }
    off = (rec << 7) & 0xFFFF;
    end = off + RC_BLKSZ;                    /* <= 65,408: rec <= 510 */
    if (rc_oft_grow(i, end) < 0) {
        rc_toast_name("RunCPM RAM? ", rc_oft[i].name, "");
        return 0xFF;
    }
    if (off > rc_oft[i].size)
        rc_sfill(rc_oft[i].seg, rc_oft[i].size, 0, off - rc_oft[i].size);
    rc_zzcopy_out(rc_oft[i].seg, off, rc_dma, RC_BLKSZ);
    if (end > rc_oft[i].size)
        rc_oft[i].size = end;
    rc_oft[i].dirty = 1;
    rc_oft[i].lru = ++rc_oft_clock;
    return 0;
}

/* the sequential record the FCB names: (s2 & 15) * 4096 + ex * 128 + cr */
static unsigned rc_fcb_rec(void)
{
    return (((unsigned)(rc_fcb[14] & RC_MAXS2) << 12) +
            ((unsigned)rc_fcb[12] << 7) + rc_fcb[32]) & 0xFFFF;
}

/* ==========================================================================
 * THE BDOS FUNCTIONS. Each answers HL (0..0xFFFF), and rc_bdos finishes an
 * error's wait when rc_errwait is set on the way out.
 * ========================================================================*/

#define RC_RW (rc_rovec & (1 << rc_cdrive))

/* F_OPEN (15) - _OpenFile */
static unsigned ovl_fs_open(void)
{
    int d, i;
    unsigned recs;
    rc_fcb_load(rc_z.de);
    d = rc_seldisk(rc_fcb[0], rc_user);
    if (d < 0)
        return 0xFF;
    i = rc_oft_of_fcb(d, 0);
    if (i < 0)
        return 0xFF;
    recs = (rc_oft[i].size >> 7) + ((rc_oft[i].size & 127) ? 1 : 0);
    rc_fcb[13] = 0;                          /* s1 */
    rc_fcb[14] = 0x80;                       /* s2: unmodified */
    rc_fcb[15] = (unsigned char)(recs > RC_BLKEX ? RC_BLKEX : recs);
    os88_memset(rc_fcb + 16, 0, 16);         /* al */
    rc_fcb_store();
    return 0;
}

/* F_CLOSE (16) - _CloseFile */
static unsigned ovl_fs_close(void)
{
    int d, i;
    rc_fcb_load(rc_z.de);
    d = rc_seldisk(rc_fcb[0], rc_user);
    if (d < 0)
        return 0xFF;
    if (!rc_fcb2host(rc_fcb, rc_hname) || rc_hname[0] == 0)
        return 0xFF;                         /* invalid filename */
    rc_host2n11(rc_hname, rc_n11);
    i = rc_oft_find(d, rc_user, rc_n11);
    if (rc_fcb[14] & 0x80)                   /* not modified: nothing owed
                                              * (the entry stays for a
                                              * reopen; rc_oft_flush) */
        return 0;
    if (RC_RW) {
        rc_error(RC_ERR_WRITEPROT);
        return 0xFF;
    }
    if (rc_fcbaddr == RC_BATCHFCB) {
        /* the CCP's $$$.SUB: truncated to rc records (SUBMIT's next line
         * is the LAST record; the CCP reads it, decrements rc, clears s2
         * and closes - CCP-DR.60K at 0xE56D) - loaded for it if the table
         * let it go */
        unsigned n = (unsigned)rc_fcb[15] << 7;
        if (i < 0)
            i = rc_oft_get(d, rc_user, rc_n11, 0);
        if (i >= 0 && n < rc_oft[i].size) {
            rc_oft[i].size = n;
            rc_oft[i].dirty = 1;
        }
    }
    if (i < 0)
        return 0;                            /* modified, but nothing of it
                                              * is held: nothing to write */
    return rc_oft_flush(i) < 0 ? 0xFF : 0;
}

/* F_MAKE (22) - _MakeFile */
static unsigned ovl_fs_make(void)
{
    int d, i;
    rc_fcb_load(rc_z.de);
    d = rc_seldisk(rc_fcb[0], rc_user);
    if (d < 0)
        return 0xFF;
    if (RC_RW) {
        rc_error(RC_ERR_WRITEPROT);
        return 0xFF;
    }
    i = rc_oft_of_fcb(d, 1);
    if (i < 0)
        return 0xFF;
    rc_dc_note(d, rc_user, rc_n11, rc_oft[i].size);
    rc_fcb[12] = 0;                          /* ex */
    rc_fcb[13] = 0;                          /* s1 */
    rc_fcb[14] = 0;                          /* s2: modified already */
    rc_fcb[15] = 0;                          /* rc */
    os88_memset(rc_fcb + 16, 0, 16);
    rc_fcb[32] = 0;                          /* cr */
    rc_fcb_store();
    return 0;
}

/* --- the search: disk.h _SearchFirst/_SearchNext over abstraction_posix.h
 * _findfirst/_findnext, with _mockupDirEntry ---------------------------- */

/* ovl_mock_dirent - _mockupDirEntry: the 32-byte entry at the DMA for the
 * file whose chain state is rc_s_* (records left, extents left, extents
 * used, next phoney block) */
static void ovl_mock_dirent(void)
{
    unsigned blocks, i, e;
    os88_memset(rc_dirent, 0, 32);
    os88_memcpy(rc_dirent + 1, rc_s_name, 11);
    rc_dirent[0] = (unsigned char)(rc_s_allusers ? rc_s_user : rc_user);
    if (rc_s_exts <= RC_EXTPDE) {            /* fits one directory entry */
        if (rc_s_exts) {
            e = rc_s_exts - 1 + rc_s_used;
            rc_dirent[12] = (unsigned char)(e & RC_MAXEX);      /* % 32 */
            rc_dirent[14] = (unsigned char)(e >> 5);            /* / 32 */
            rc_dirent[15] = (unsigned char)(rc_s_recs - ((rc_s_exts - 1) << 7));
        }
        blocks = (rc_s_recs >> 5) + ((rc_s_recs & 31) ? 1 : 0);
        rc_s_recs = 0;
        rc_s_exts = 0;
        rc_s_used = 0;
    } else {                                 /* maxed out: the chain goes on */
        e = RC_EXTPDE - 1 + rc_s_used;
        rc_dirent[12] = (unsigned char)(e & RC_MAXEX);
        rc_dirent[14] = (unsigned char)(e >> 5);
        rc_dirent[15] = RC_BLKEX;
        blocks = 8;
        rc_s_recs -= RC_BLKEX * RC_EXTPDE;
        rc_s_exts -= RC_EXTPDE;
        rc_s_used += RC_EXTPDE;
    }
    if (blocks > 8)
        blocks = 8;                          /* 8 16-bit al entries */
    for (i = 0; i < blocks; i++) {
        rc_dirent[16 + (i << 1)] = (unsigned char)(rc_s_ffab & 0xFF);
        rc_dirent[17 + (i << 1)] = (unsigned char)(rc_s_ffab >> 8);
        rc_s_ffab++;
    }
    rc_zcopy_in(rc_dma, rc_dirent, 32);
}

/* ovl_search_next - _findnext(isdir): the next entry of the walk. isdir = 1
 * mocks the directory entry at the DMA (F_SFIRST/F_SNEXT); 0 only names the
 * file in tmpFCB (F_DELETE, _CheckSUB). Answers 0 (found: the entry is at
 * DMA+0) or 0xFF. */
static unsigned ovl_search_next(int isdir)
{
    int i, r;
    unsigned lo, hi;
    if (rc_s_allext && rc_s_recs) {          /* the last file's next extent */
        ovl_mock_dirent();
        return 0;
    }
    for (;;) {
        r = rc_dc_want(rc_s_drive, rc_s_user);
        if (r < 0)
            return 0xFF;
        while (r == 0 && rc_s_pos < rc_dcc->n) {
            i = rc_s_pos++;
            if (!rc_match(rc_dcc->e[i].name, rc_s_pat))
                continue;
            os88_memcpy(rc_s_name, rc_dcc->e[i].name, 11);
            if (isdir) {
                /* records = bytes rounded up to a whole record, in 32
                 * bits: (hi:lo + 127) >> 7 - a uint16 as upstream's */
                lo = rc_dcc->e[i].size_lo;
                hi = rc_dcc->e[i].size_hi;
                rc_s_recs = ((hi << 9) + (lo >> 7) + ((lo & 127) ? 1 : 0)) & 0xFFFF;
                rc_s_exts = (rc_s_recs >> 7) + ((rc_s_recs & 127) ? 1 : 0);
                rc_s_used = 0;
                rc_s_ffab = RC_FBAD;
                ovl_mock_dirent();
            } else {
                rc_s_recs = rc_s_exts = rc_s_used = 0;
                rc_s_ffab = RC_FBAD;
            }
            rc_wr(RC_TMPFCB, rc_s_drive + 1);       /* tmpFCB: the name found */
            rc_zcopy_in(RC_TMPFCB + 1, rc_s_name, 11);
            return 0;
        }
        if (!rc_s_allusers || rc_s_user >= RC_USERS - 1)
            return 0xFF;
        rc_s_user++;                         /* dr = '?': every user area of
                                              * the drive, in turn */
        rc_s_pos = 0;
    }
}

/* ovl_search_first - _SearchFirst(fcbaddr, isdir) */
static unsigned ovl_search_first(unsigned fcbaddr, int isdir)
{
    int d, i;
    rc_fcb_load(fcbaddr);
    d = rc_seldisk(rc_fcb[0], rc_user);
    if (d < 0)
        return 0xFF;
    rc_fcb2host(rc_fcb, rc_hname);
    if (rc_hname[0] == 0)
        return 0xFF;                         /* invalid filename */
    rc_s_allusers = (rc_fcb[0] == '?');
    rc_s_allext = (rc_fcb[12] == '?');
    if (rc_s_allusers) {
        for (i = 0; i < 11; i++)
            rc_s_pat[i] = '?';
    } else {
        rc_host2n11(rc_hname, rc_s_pat);
    }
    rc_s_drive = d;
    rc_s_user = rc_s_allusers ? 0 : rc_user;
    rc_s_pos = 0;
    rc_s_recs = rc_s_exts = rc_s_used = 0;
    return ovl_search_next(isdir);
}

/* F_SNEXT (18) - _SearchNext: the drive is tmpFCB's */
static unsigned ovl_fs_snext(void)
{
    if (rc_seldisk(rc_rd(RC_TMPFCB), rc_user) < 0)
        return 0xFF;
    return ovl_search_next(1);
}

/* F_DELETE (19) - _DeleteFile: every file the pattern matches; PUN/LST
 * capture closed by name */
static unsigned ovl_fs_delete(void)
{
    int d, i;
    unsigned r, deleted = 0xFF;
    rc_fcb_load(rc_z.de);
    d = rc_seldisk(rc_fcb[0], rc_user);
    if (d < 0)
        return 0xFF;
    if (RC_RW) {
        rc_error(RC_ERR_WRITEPROT);
        return 0xFF;
    }
    r = ovl_search_first(rc_z.de, 0);
    while (r != 0xFF) {
        if (rc_errwait)
            return 0xFF;
        i = rc_oft_find(rc_s_drive, rc_user, rc_s_name);
        if (i >= 0)
            rc_oft_drop(i);                  /* its bytes go with it */
        /* disk.h 489-500: the capture is closed by NAME alone, whatever
         * drive or user the ERA ran on (ERA B:PUN.TXT restarts A\0\PUN.TXT
         * at the next punch, upstream and here) */
        if (rc_n11_eq(rc_s_name, "PUN     TXT"))
            rc_pun_open = 0;
        if (rc_n11_eq(rc_s_name, "LST     TXT"))
            rc_lst_open = 0;
        rc_n11_host(rc_s_name, rc_hname);
        rc_fs_cd(rc_s_drive, rc_user);
        if (os88_file_delete(rc_hname) == 0 || os88_ferr() == OS88_FERR_NOENT) {
            /* NOENT: the disk never had it - a file MADE and not yet
             * written back (upstream's fopen "a" creates the host file at
             * once; here the whole-file model creates it at close), and it
             * is deleted all the same */
            deleted = 0;
            rc_dc_forget(rc_s_drive, rc_user, rc_s_name);
            ovl_pl_unmark();                  /* room again, perhaps: a USER
                                              * n refused for a full disk
                                              * may be tried once more */
        } else {
            rc_error(RC_ERR_WRITEPROT);
            break;
        }
        r = ovl_search_first(rc_z.de, 0);
    }
    return deleted;
}

/* F_RENAME (23) - _RenameFile: fn/tp -> the name at FCB+17 */
static unsigned ovl_fs_rename(void)
{
    int d, i;
    unsigned lo, hi;
    rc_fcb_load(rc_z.de);
    d = rc_seldisk(rc_fcb[0], rc_user);
    if (d < 0)
        return 0xFF;
    if (RC_RW) {
        rc_error(RC_ERR_WRITEPROT);
        return 0xFF;
    }
    rc_fcb[16] = rc_fcb[0];                  /* no move between folders */
    rc_wr(rc_z.de + 16, rc_fcb[0]);
    rc_fcb2host(rc_fcb + 16, rc_hname2);
    rc_fcb2host(rc_fcb, rc_hname);
    if (rc_hname2[0] == 0 || rc_hname[0] == 0)
        return 0xFF;
    rc_host2n11(rc_hname, rc_n11);
    i = rc_oft_find(d, rc_user, rc_n11);
    if (i >= 0)
        rc_oft_flush(i);                     /* the bytes reach the disk
                                              * under the old name first */
    if (rc_dc_want(d, rc_user) != 0)
        return 0xFF;
    if (os88_file_rename(rc_hname, rc_hname2) < 0)
        return 0xFF;
    if (i >= 0 && rc_oft[i].live)            /* the held bytes follow the
                                              * name (rc_oft_flush kept them) */
        rc_host2n11(rc_hname2, rc_oft[i].name);
    i = rc_dc_find(rc_n11);
    if (i >= 0) {                            /* out and back in: the slot
                                              * stays in name order (glob's) */
        lo = rc_dcc->e[i].size_lo;
        hi = rc_dcc->e[i].size_hi;
        rc_dc_forget(d, rc_user, rc_n11);
        rc_host2n11(rc_hname2, rc_dc_n11);
        rc_dc_insert(rc_dc_n11, lo, hi);
        if (d == 0 && rc_dc_n11[0] == '$')
            rc_dc_subfact();
    }
    return 0;
}

/* F_READ (20) - _ReadSeq */
static unsigned rc_fs_readseq(void)
{
    int d, i, r;
    rc_fcb_load(rc_z.de);
    d = rc_seldisk(rc_fcb[0], rc_user);
    if (d < 0)
        return 0xFF;
    i = rc_oft_of_fcb(d, 0);
    if (i < 0)
        return 0x10;                         /* _sys_readseq: no such file */
    r = rc_rec_read(i, rc_fcb_rec());
    if (r != 0)
        return (unsigned)r;
    if (++rc_fcb[32] >= RC_MAXCR) {          /* cr, ex, s2 stepped */
        rc_fcb[32] = 0;
        rc_fcb[12]++;
    }
    if (rc_fcb[12] > RC_MAXEX) {
        rc_fcb[12] = 0;
        rc_fcb[14]++;
    }
    rc_fcb_store();
    if ((rc_fcb[14] & 0x7F) > RC_MAXS2)
        return 0xFE;
    return 0;
}

/* F_WRITE (21) - _WriteSeq */
static unsigned rc_fs_writeseq(void)
{
    int d, i, r;
    rc_fcb_load(rc_z.de);
    d = rc_seldisk(rc_fcb[0], rc_user);
    if (d < 0)
        return 0xFF;
    if (RC_RW) {
        rc_error(RC_ERR_WRITEPROT);
        return 0xFF;
    }
    i = rc_oft_of_fcb(d, 0);
    if (i < 0)
        return 0x10;
    r = rc_rec_write(i, rc_fcb_rec());
    if (r != 0)
        return (unsigned)r;
    rc_fcb[14] &= 0x7F;                      /* modified */
    if (++rc_fcb[32] >= RC_MAXCR) {
        rc_fcb[32] = 0;
        rc_fcb[12]++;
        rc_fcb[15] = 1;                      /* the new extent's first */
    } else {
        rc_fcb[15]++;
    }
    if (rc_fcb[12] > RC_MAXEX) {
        rc_fcb[12] = 0;
        rc_fcb[14]++;
        rc_fcb[15] = 1;
    }
    rc_fcb_store();
    if ((rc_fcb[14] & 0x7F) > RC_MAXS2)
        return 0xFE;
    return 0;
}

/* the random record r0/r1/r2 -> the FCB's cr/ex/s2 */
static void rc_fcb_setrec(unsigned rec, int keep_unmod)
{
    rc_fcb[32] = (unsigned char)(rec & (RC_MAXCR - 1));
    rc_fcb[12] = (unsigned char)((rec >> 7) & RC_MAXEX);
    rc_fcb[14] = (unsigned char)(((rec >> 12) & RC_MAXS2) |
                                 (keep_unmod ? (rc_fcb[14] & 0x80) : 0));
}

/* F_READRAND (33) - _ReadRand */
static unsigned rc_fs_readrand(void)
{
    int d, i, r;
    unsigned rec;
    rc_fcb_load(rc_z.de);
    d = rc_seldisk(rc_fcb[0], rc_user);
    if (d < 0)
        return 0xFF;
    i = rc_oft_of_fcb(d, 0);
    if (i < 0)
        return 0x10;
    rec = rc_fcb[33] | ((unsigned)rc_fcb[34] << 8);
    if (rc_fcb[35])
        r = 1;                               /* r2 set: _sys_readrand's fseek
                                              * past the end SUCCEEDS on every
                                              * host, fread moves nothing -
                                              * 1, and cr/ex/s2 are stepped
                                              * from the record's low bits
                                              * (6 is only a FAILED fseek) */
    else
        r = rc_rec_read(i, rec);             /* 0, or 1: unwritten */
    rc_fcb_setrec(rec, 1);
    rc_fcb_store();
    return (unsigned)r;
}

/* F_WRITERAND (34) and F_WRITEZF (40) - _WriteRand */
static unsigned rc_fs_writerand(void)
{
    int d, i, r;
    unsigned rec;
    rc_fcb_load(rc_z.de);
    d = rc_seldisk(rc_fcb[0], rc_user);
    if (d < 0)
        return 0xFF;
    if (RC_RW) {
        rc_error(RC_ERR_WRITEPROT);
        return 0xFF;
    }
    i = rc_oft_of_fcb(d, 0);
    if (i < 0)
        return 0x10;
    if (rc_fcb[35])
        return 6;
    rec = rc_fcb[33] | ((unsigned)rc_fcb[34] << 8);
    if (rec > RC_MAXREC_W) {
        rc_toast_name("RunCPM: ", rc_oft[i].name, ">64K");
        return 6;                            /* seek past the disk (74.4) */
    }
    r = rc_rec_write(i, rec);
    if (r != 0)
        return (unsigned)r;
    rc_fcb_setrec(rec, 0);                   /* resets the unmodified flag */
    rc_fcb_store();
    return 0;
}

/* F_SIZE (35) - _GetFileSize: r0..r2 = the records, rounded up */
static unsigned ovl_fs_size(void)
{
    int d, i, k;
    unsigned lo, hi, r_lo, r_hi;
    rc_fcb_load(rc_z.de);
    d = rc_seldisk(rc_fcb[0], rc_user);
    if (d < 0)
        return 0xFF;
    if (!rc_fcb2host(rc_fcb, rc_hname) || rc_hname[0] == 0)
        return 0xFF;
    rc_host2n11(rc_hname, rc_n11);
    i = rc_oft_find(d, rc_user, rc_n11);
    if (i >= 0) {
        lo = rc_oft[i].size;
        hi = 0;
    } else {
        if (rc_dc_want(d, rc_user) != 0)
            return 0xFF;
        k = rc_dc_find(rc_n11);
        if (k < 0)
            return 0xFF;
        lo = rc_dcc->e[k].size_lo;
        hi = rc_dcc->e[k].size_hi;
    }
    /* (hi:lo + 127) >> 7, 24 bits of it, in words */
    r_lo = (lo >> 7) + ((lo & 127) ? 1 : 0);          /* <= 512 */
    r_hi = hi >> 7;
    hi = ((hi & 0x7F) << 9) & 0xFFFF;
    r_lo = (r_lo + hi) & 0xFFFF;
    if (r_lo < hi)
        r_hi++;                                        /* the carry */
    rc_fcb[33] = (unsigned char)(r_lo & 0xFF);
    rc_fcb[34] = (unsigned char)(r_lo >> 8);
    rc_fcb[35] = (unsigned char)(r_hi & 0xFF);
    rc_fcb_store();
    return 0;
}

/* F_RANDREC (36) - _SetRandom */
static unsigned ovl_fs_randrec(void)
{
    unsigned rec;
    rc_fcb_load(rc_z.de);
    rec = rc_fcb_rec();
    rc_fcb[33] = (unsigned char)(rec & 0xFF);
    rc_fcb[34] = (unsigned char)(rec >> 8);
    rc_fcb[35] = 0;
    rc_fcb_store();
    return 0;
}

/* F_USERNUM (32) set - _SetUser: 0..31 as BDOS does, the folder made for
 * 0..15 (NOHIGHUSER) */
static void ovl_fs_setuser(int u)
{
    rc_user = u & 0x1F;
    if (rc_user < RC_USERS)
        ovl_fs_mkuser(rc_cdrive, rc_user);
}

/* BDOS 249 - _MakeDisk -> _sys_makedisk: <letter> and <letter>/0 below the
 * launch folder. 0 made, 0xFE the letter's folder could not be made (it
 * exists), 0xFF no such drive. */
static unsigned ovl_fs_makedisk(void)
{
    int dr = rc_rd(rc_z.de), d, c;
    if (dr < 1 || dr > 16)
        return 0xFF;
    d = dr - 1;
    if (rc_fs_home() < 0)
        return 0xFE;
    rc_fs_name[0] = (char)('A' + d);
    rc_fs_name[1] = 0;
    if (os88_file_mkdir(rc_fs_name) < 0)
        return 0xFE;
    rc_dr_ok[d] = 0;                         /* known absent no longer */
    c = rc_fs_subdir(rc_fs_name);
    if (c < 0)
        return 0xFE;
    rc_dr_clus[d] = (unsigned)c;
    rc_dr_ok[d] = 1;
    rc_pl_ok[d << 4] = 0;
    ovl_fs_mkuser(d, 0);                      /* <letter>/0 */
    return 0;
}

/* DRV_ALLRESET's answer - _CheckSUB: 0xFF when a $???????.??? is on drive
 * A (BATCHA) in the current user area - the DRI CCP then runs $$$.SUB.
 * ASKED AT EVERY WARM BOOT, so it is answered from a remembered FACT and
 * never by a search: rc_subfact[u] is exact once any slot has described
 * A\u (a fill scans the names; our own MAKE/ERA/REN keep it), and while it
 * is unknown - the first boot of a session, before any DIR - it is resolved
 * by ONE by-name look-up of $$$.SUB, the $-file SUBMIT makes and the only
 * one the DRI CCP ever opens (a by-name look-up is one pass over the
 * folder's directory sectors: it costs no walk and puts none on the boot
 * path). Stated in SPEC.md 74.3: until A\u has been listed, another $-name
 * put there from outside is not the batch flag - it would be upstream. */
static unsigned ovl_fs_checksub(void)
{
    static const char sub[8] = "$$$.SUB";
    int u = rc_user, r;
    if (u >= RC_USERS)
        return 0;                            /* no folder: nothing there */
    if (rc_subfact[u] == 0) {
        if (rc_dc_slot(0, u)) {
            rc_dc_subfact();
        } else {
            r = rc_fs_cd(0, u);
            if (r < 0) {
                rc_error(RC_ERR_SELECT);     /* no A: at all - as the search
                                              * would have said */
                return 0;
            }
            rc_subfact[u] = 1;
            if (r == 0) {
                os88_file_read(sub, rc_dirent, 32);      /* the look-up: FERR_BIG
                                              * (decided from the entry, no
                                              * data read), OK or NOENT */
                if (os88_ferr() != OS88_FERR_NOENT)
                    rc_subfact[u] = 2;
            }
        }
    }
    return rc_subfact[u] == 2 ? 0xFF : 0x00;
}

/* A_WRITE (4) / L_WRITE (5) - the capture files A/0/PUN.TXT and LST.TXT:
 * created (truncated) the first time in the session, appended after (cpm.h
 * pun_open/lst_open outlive every program; the entry itself is flushed at
 * warm boot like any other and reopened for append) */
static void rc_fs_capture(int lst, int c)
{
    int i, first;
    unsigned s;
    if (lst) {
        first = !rc_lst_open;
        rc_lst_open = 1;
    } else {
        first = !rc_pun_open;
        rc_pun_open = 1;
    }
    i = rc_oft_get(0, 0, lst ? "LST     TXT" : "PUN     TXT", first ? 2 : 1);
    if (i < 0)
        return;                              /* pun_dev == NULL: dropped */
    s = rc_oft[i].size;
    if (s == 0xFFFF || rc_oft_grow(i, s + 1) < 0)
        return;
    os88_poke(rc_oft[i].seg, s, c);
    rc_oft[i].size = s + 1;
    rc_oft[i].dirty = 1;
    if (first)
        rc_dc_note(0, 0, rc_oft[i].name, 0);
}
