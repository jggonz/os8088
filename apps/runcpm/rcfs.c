/* ============================================================================
 * os8088 - apps/runcpm/rcfs.c          drives are folders (SPEC.md 71.3)
 *
 * Part of RUNCPM (SPEC.md 71), a reimplementation of RunCPM 6.9 by Marcelo
 * Dantas / "Mockba the Borg" (https://github.com/MockbaTheBorg/RunCPM, MIT
 * licence, Copyright (c) 2017 Mockba the Borg). #included by runcpm.c.
 *
 * WHAT IS RUNCPM'S HERE. The layout of a CP/M disk on the host is RunCPM's
 * (abstraction_posix.h, disk.h): drive A..P is a folder named by its letter
 * below FILEBASE, and user area 0..15 a folder named by its hex digit inside
 * it - A/0 is drive A user 0, and that is where the master disk lives. The
 * FCB and directory-entry layouts, the name rules, the extent/record
 * synthesis and every file operation's return code will come from disk.h in
 * WAVE 4, over this platform's whole-file model (SPEC.md 71.3): an 8-entry
 * open-file table in heap claims, a 128-entry directory cache per (drive,
 * user), files up to 65,535 bytes.
 *
 * WHAT IS THIS PLATFORM'S: THE PLACE. A package's file calls resolve in ITS
 * INSTANCE's current folder (SPEC.md 19.2.1), and every call first re-stands
 * the machine there. So a drive/user switch is os88_file_goto_q_mark() - the
 * quiet stand that MOVES the instance (SPEC.md 71.1) - and the folder's
 * cluster is found by walking the launch folder with os88_file_find() once
 * and remembering it. rc_fs_cd() below is that switch; wave 4 grows the
 * directory cache and the open-file table around it.
 *
 * WAVE 3's DRV_SET and BIOS SELDSK (rccpm.c) select a drive through
 * rc_fs_cd() and answer disk.h's 'Bdos Err on X: Select' EXACTLY where
 * upstream does - when the DRIVE's folder is not there: _SelectDisk calls
 * _sys_select("A"+dr), which stats FILEBASE/<letter> ONLY (abstraction_
 * posix.h 147-152); the user folder is never a Select error - _SetUser ->
 * _MakeUserDir creates it on USER n (disk.h 862-870, abstraction_posix.h
 * 344-354), and that creation is wave 4's (with the file calls that would
 * populate it) - so USER 1 followed by a warm boot must NOT loop on Select
 * (the DRI CCP restarts with C = DSKByte = 0x10: setuser(1), select(0)).
 * Until wave 4, a (drive,user) whose user folder is absent stands in the
 * drive's folder and every search there answers 'no file'.
 * SELDSK's rc_fs_cd() MOVES the instance as a side effect where cpm.h's
 * B_SELDSK only tests _sys_select() - a stand-in for an existence test that
 * wave 4 replaces with a look-up in the place table that does not move.
 * WAVE 1 carried rc_fs_cd() and a probe that proved the mechanism from a
 * wake handler; wave 2's debug loader (runcpm.c) is the living proof now -
 * it stands in A\0 through rc_fs_cd(0, 0) and reads the .COM it was named
 * from there, in the context every BDOS call will have: the UI task, no
 * lock, the dispatch billed to this instance.
 * ==========================================================================*/

#define RC_DRIVES 16
#define RC_USERS  16

static struct os88_place rc_home;            /* the launch folder: FILEBASE */
static struct os88_place rc_cur;             /* where the instance stands */
static struct os88_find rc_ff;               /* os88_file_find's answer */
static unsigned rc_pl_clus[RC_DRIVES * RC_USERS];    /* (drive,user) -> the
                                              * folder's first cluster... */
static unsigned char rc_pl_ok[RC_DRIVES * RC_USERS]; /* ...once resolved: 1
                                              * = known, 2 = known absent
                                              * (wave 4's USER n creation
                                              * clears the entry) */
static unsigned rc_dr_clus[RC_DRIVES];       /* drive -> the letter folder's
                                              * first cluster... */
static unsigned char rc_dr_ok[RC_DRIVES];    /* ...once resolved: 1 = known,
                                              * 2 = known absent (the Select
                                              * fact, per letter) */
static char rc_fs_name[13];                  /* a folder name being sought */

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
        if (rc_ff.type != OS88_FT_DIR)
            continue;
        if (rc_ff.name[0] == name[0] && rc_ff.name[1] == name[1] &&
            rc_ff.name[2] == 0)
            return (int)rc_ff.clus;
    }
    return -1;
}

/* rc_fs_home - stand where this instance was launched (FILEBASE) */
static int rc_fs_home(void)
{
    if (rc_cur.clus == rc_home.clus && rc_cur.vol == rc_home.vol)
        return 0;
    if (os88_file_goto_q_mark(rc_home.clus, rc_home.vol) < 0)
        return -1;
    rc_cur.clus = rc_home.clus;
    rc_cur.vol = rc_home.vol;
    return 0;
}

/* rc_fs_cd - stand the instance in drive d (0 = A) user u (0..15): the
 * folder <letter>\<hex digit> below the launch folder. 0 = standing in the
 * user folder; 1 = the drive's folder exists but the user folder does not -
 * the instance stands in the DRIVE's folder (searches there answer 'no
 * file'; wave 4's USER n creates the folder, _MakeUserDir); -1 = there is no
 * such DRIVE (SPEC.md 71.3: 'Bdos Err on X: Select', the fact _sys_select
 * decides from the letter alone), and the instance is left where it was
 * launched. Each fact is looked up ONCE per (drive) and once per (drive,
 * user): rc_dr_clus/rc_pl_clus remember the answer. */
static int rc_fs_cd(int d, int u)
{
    int i, c;
    unsigned dst;
    if (d < 0 || d >= RC_DRIVES || u < 0)
        return -1;
    i = (d << 4) + (u & 15);
    if (rc_dr_ok[d] == 0) {                  /* the letter, once */
        if (rc_fs_home() < 0)
            return -1;
        rc_fs_name[0] = (char)('A' + d);
        rc_fs_name[1] = 0;
        c = rc_fs_subdir(rc_fs_name);
        if (c < 0) {
            rc_dr_ok[d] = 2;
            return -1;
        }
        rc_dr_clus[d] = (unsigned)c;
        rc_dr_ok[d] = 1;
    }
    if (rc_dr_ok[d] == 2)
        return -1;
    if (u >= RC_USERS) {                     /* user areas 16..31 - BDOS's
                                              * unofficial ones (_SetUser
                                              * keeps 0-31; upstream would
                                              * make G..V) - have no place
                                              * slot: the drive's folder,
                                              * 'no file', never Select */
        dst = rc_dr_clus[d];
        goto stand;
    }
    if (rc_pl_ok[i] == 0) {                  /* the user folder, once */
        if (rc_cur.clus != rc_dr_clus[d] || rc_cur.vol != rc_home.vol) {
            if (os88_file_goto_q_mark(rc_dr_clus[d], rc_home.vol) < 0)
                return -1;
            rc_cur.clus = rc_dr_clus[d];
            rc_cur.vol = rc_home.vol;
        }
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
    if (rc_cur.clus != dst || rc_cur.vol != rc_home.vol) {
        if (os88_file_goto_q_mark(dst, rc_home.vol) < 0)
            return -1;
        rc_cur.clus = dst;
        rc_cur.vol = rc_home.vol;
    }
    return (u < RC_USERS && rc_pl_ok[i] == 1) ? 0 : 1;
}
