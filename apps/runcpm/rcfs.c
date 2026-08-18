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
 * WAVE 1 carries rc_fs_cd() and one PROBE that proves the mechanism from a
 * wake handler - the place where the dispatch is billed to this instance and
 * where every BDOS call will run: stand in A/0, read a file that exists only
 * there, come home. Its output is scaffolding and wave 4 removes it.
 * ==========================================================================*/

#define RC_DRIVES 16
#define RC_USERS  16

static struct os88_place rc_home;            /* the launch folder: FILEBASE */
static struct os88_place rc_cur;             /* where the instance stands */
static struct os88_find rc_ff;               /* os88_file_find's answer */
static unsigned rc_pl_clus[RC_DRIVES * RC_USERS];    /* (drive,user) -> the
                                              * folder's first cluster... */
static unsigned char rc_pl_ok[RC_DRIVES * RC_USERS]; /* ...once resolved: 1
                                              * = known, 2 = known absent */
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
 * folder <letter>\<hex digit> below the launch folder. 0 = standing there;
 * -1 = there is no such folder (SPEC.md 71.3: 'Bdos Err on X: Select'), and
 * the instance is left where it was launched. */
static int rc_fs_cd(int d, int u)
{
    int i = (d << 4) + u, c;
    if (rc_pl_ok[i] == 0) {
        if (rc_fs_home() < 0)
            return -1;
        rc_fs_name[0] = (char)('A' + d);
        rc_fs_name[1] = 0;
        c = rc_fs_subdir(rc_fs_name);
        if (c >= 0) {
            if (os88_file_goto_q_mark((unsigned)c, rc_home.vol) < 0)
                return -1;
            rc_cur.clus = (unsigned)c;
            rc_fs_name[0] = (char)(u < 10 ? '0' + u : 'A' + u - 10);
            c = rc_fs_subdir(rc_fs_name);
        }
        if (c < 0) {
            rc_pl_ok[i] = 2;
            rc_fs_home();
            return -1;
        }
        rc_pl_clus[i] = (unsigned)c;
        rc_pl_ok[i] = 1;
    }
    if (rc_pl_ok[i] == 2)
        return -1;
    if (rc_cur.clus == rc_pl_clus[i] && rc_cur.vol == rc_home.vol)
        return 0;
    if (os88_file_goto_q_mark(rc_pl_clus[i], rc_home.vol) < 0)
        return -1;
    rc_cur.clus = rc_pl_clus[i];
    rc_cur.vol = rc_home.vol;
    return 0;
}

/* --- WAVE 1 SCAFFOLDING: the goto_q_mark proof (docs/RUNCPM-PORT-PLAN.md,
 * wave 1 'done when') - removed by wave 4 when the real drive layer lands.
 * Called ONCE from the first os88_onwake, because that is the context every
 * BDOS call will have: the UI task, no lock, the dispatch billed to this
 * instance. Reads A\0\RCPROBE.TXT - a file the wave-1 disk carries there and
 * nowhere else - into `out`, and comes home. Answers the byte count, or a
 * negative code naming the step that failed. */
static char rc_probe_buf[80];
static int rc_fs_probe(void)
{
    unsigned n;
    if (rc_fs_cd(0, 0) < 0)
        return -1;                          /* no A\0 folder */
    n = os88_file_read("RCPROBE.TXT", rc_probe_buf, sizeof(rc_probe_buf) - 1);
    if (n == 0 && os88_ferr() != 0) {
        rc_fs_home();
        return -2;                          /* standing there, no file */
    }
    rc_probe_buf[n] = 0;
    if (rc_fs_home() < 0)
        return -3;
    return (int)n;
}
