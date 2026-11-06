/* ============================================================================
 * os8088 - apps/pagedesk/pdwio.c        the writer's file layer (20.5.11)
 *
 * #included by pdwrt.c. The ONLY place the writer touches a file. Two
 * bodies behind one set of signatures:
 *
 *   target  OSAPI_FILE_WRITE (the first chunk creates), OSAPI_FILE_WRITE_SEQ
 *           (HELD: the FAT and the size are committed once, at the close),
 *           OSAPI_FILE_READ_SEQ, OSAPI_FILE_RENAME / DELETE
 *   host    stdio, with fault injection for the harness: a byte budget that
 *           answers PDE_DISKFULL, a write-protect switch, and a checker that
 *           flags a non-final append that is not a whole number of clusters
 *
 * Every chunk but the last is a whole number of clusters (OSAPI_FILE_APPEND's
 * rule, SPEC.md 18.4.7), which is why pw_flush keeps the tail.
 * ==========================================================================*/
#ifdef PD_HOST
#include <stdio.h>
#include <string.h>
#include <unistd.h>
extern unsigned char pd_host_mem[];
pdw   pdw_host_clu = 512;
long  pdw_host_budget = -1;     /* bytes the disk still takes; -1 = no limit */
pdw   pdw_host_wprot;           /* 1 = every create answers PDE_WPROT       */
pdw   pdw_host_viol;            /* appends after a short chunk              */
pdw   pdw_host_calls;           /* creates + appends + closes               */
pdw   pdw_host_reads;           /* source reads that returned data          */
char  pdw_host_srcdir[128], pdw_host_tgtdir[128];   /* D14: the two folders (absolute)   */
pdw   pdw_host_swaps;           /* folder switches made                       */
static pdw h_cross;
pdw   pdw_host_tamper;          /* 1 = damage the tail as the file closes (the self-check must see it) */
static FILE *h_out, *h_src;
static pdw   h_short;

pdw pdw_io_clu(void) { return pdw_host_clu; }
static pdw h_put(FILE *f, pdw seg, pdw off, pdw n)
{
    if (pdw_host_budget >= 0) {
        if ((long)n > pdw_host_budget) { pdw_host_budget = 0; return PDE_DISKFULL; }
        pdw_host_budget -= (long)n;
    }
    if (fwrite(pd_host_mem + (((unsigned long)seg << 4) + off), 1, n, f) != n) return PDE_IO;
    return PDE_OK;
}
pdw pdw_io_create(const char *name, pdw seg, pdw off, pdw n)
{
    pdw_host_calls++;
    if (pdw_host_wprot) return PDE_WPROT;
    if (h_out) fclose(h_out);
    h_out = fopen(name, "wb");
    h_short = 0;
    if (!h_out) return PDE_IO;
    if (n % pdw_host_clu) h_short = 1;
    return h_put(h_out, seg, off, n);
}
pdw pdw_io_append(const char *name, pdw seg, pdw off, pdw n)
{
    (void)name; pdw_host_calls++;
    if (!h_out) return PDE_IO;
    if (h_short) pdw_host_viol++;
    if (n % pdw_host_clu) h_short = 1;
    return h_put(h_out, seg, off, n);
}
pdw pdw_io_close(const char *name)
{
    (void)name; pdw_host_calls++;
    if (h_out && pdw_host_tamper) { fseek(h_out, -4, SEEK_END); fputs("XXX\n", h_out); }
    if (h_out) { if (fclose(h_out)) { h_out = 0; return PDE_IO; } h_out = 0; }
    return PDE_OK;
}
pdw   pdw_host_delfail;         /* 1 = deleting anything but the temp file fails (a read-only target) */
pdw pdw_io_delete(const char *name)
{
    if (h_out) { fclose(h_out); h_out = 0; }
    if (pdw_host_delfail && strcmp(name, PDW_TEMPNAME)) return PDE_WPROT;
    return remove(name) ? PDE_NOTFOUND : PDE_OK;
}
pdw pdw_io_rename(const char *from, const char *to)
{
    FILE *t = fopen(to, "rb");
    if (t) { fclose(t); return PDE_EXISTS; }
    return rename(from, to) ? PDE_IO : PDE_OK;
}
void pdw_cross(pdw clus, pdw vol) { (void)clus; h_cross = vol != 0; }
char  pdw_host_fdir[4][128];    /* Insert: the folder of foreign slot clus (clus 1-3); empty = the current one */
void pdw_src_dir(pdw clus, pdw vol1) { (void)vol1; if (clus < 4 && pdw_host_fdir[clus][0]) { strcpy(pdw_host_srcdir, pdw_host_fdir[clus]); h_cross = 1; } }
void pdw_in_src(void) { if (h_cross && pdw_host_srcdir[0]) { if (chdir(pdw_host_srcdir)) pdw_host_swaps += 1000; pdw_host_swaps++; } }
void pdw_in_tgt(void) { if (h_cross && pdw_host_tgtdir[0]) { if (chdir(pdw_host_tgtdir)) pdw_host_swaps += 1000; pdw_host_swaps++; } }
static char h_srcname[14];
pdw pdw_src_open(const char *name)
{
    if (h_src) fclose(h_src);
    h_src = 0;
    strncpy(h_srcname, name, 13); h_srcname[13] = 0;
    pdw_in_src();                               /* the open is by name in the SOURCE folder */
    h_src = fopen(name, "rb");
    pdw_in_tgt();
    return h_src ? PDE_OK : PDE_NOTFOUND;
}
pdw pdw_src_read(pdw seg, pdw off, pdw cap, pdw *got)
{
    size_t r;
    if (!h_src) return PDE_IO;
    r = fread(pd_host_mem + (((unsigned long)seg << 4) + off), 1, cap, h_src);
    *got = (pdw)r;
    if (r == 0 && ferror(h_src)) return PDE_IO;
    if (r == 0) { fclose(h_src); h_src = 0; } else pdw_host_reads++;
    return PDE_OK;
}
pdw pdw_src_back(const char *name, const struct pd_u32 *at, pdw seg, pdw off, pdw cap, pdw *got)
{
    FILE *f = fopen(name, "rb"); size_t r;
    if (!f) return PDE_NOTFOUND;
    fseek(f, (long)(((unsigned long)at->hi << 16) | at->lo), SEEK_SET);
    r = fread(pd_host_mem + (((unsigned long)seg << 4) + off), 1, cap, f);
    fclose(f); *got = (pdw)r; pdw_host_reads++;
    return PDE_OK;
}
pdw pdw_ferr(void) { return PDE_IO; }

#else  /* ---- the target ------------------------------------------------- */
static unsigned g_tok;          /* WRITE_SEQ's token                         */
static pdw      g_seq;          /* a stream was started (an append happened)  */
static unsigned g_cur[8];       /* READ_SEQ's 16-byte cursor                 */
static char     g_srcname[14];

pdw pdw_ferr(void)
{
    switch (os88_ferr()) {
    case OS88_FERR_FULL: case OS88_FERR_DIRFULL: return PDE_DISKFULL;
    case OS88_FERR_PROT: case OS88_FERR_WPROT:   return PDE_WPROT;
    case OS88_FERR_EXIST:                        return PDE_EXISTS;
    case OS88_FERR_NOENT:                        return PDE_NOTFOUND;
    default:                                     return PDE_IO;
    }
}
static struct os88_place g_tpl;         /* the TARGET's folder (D14)                 */
static pdw      g_cross, g_scl, g_svol;
void pdw_cross(pdw clus, pdw vol)       /* 20.11.3: src_vol is the volume + 1, 0 = one folder */
{
    g_cross = 0;
    if (vol) { os88_file_here(&g_tpl); g_scl = clus; g_svol = (pdw)(vol - 1); g_cross = 1; }
}
void pdw_in_src(void) { if (g_cross) os88_file_goto_q_mark(g_scl, (int)g_svol); }
void pdw_in_tgt(void) { if (g_cross) os88_file_goto_q_mark(g_tpl.clus, g_tpl.vol); }
void pdw_src_dir(pdw clus, pdw vol1)    /* 20.16: we stand in the target's folder between calls */
{
    if (!g_cross) os88_file_here(&g_tpl);
    g_scl = clus; g_svol = (pdw)(vol1 - 1); g_cross = 1;
}
pdw pdw_io_clu(void)                    /* 0 = unknown; 0xFFFF = past 16 KiB */
{
    int s = os88_disk_cluster_sectors();
    if (s <= 0) return 0;
    if (s > 32) return 0xFFFFu;         /* 64 sectors x 512 would wrap to 0 */
    return (pdw)(s * 512);
}
pdw pdw_io_create(const char *name, pdw seg, pdw off, pdw n)
{
    g_tok = 0; g_seq = 0;
    if (os88_file_write_segoff(name, seg, off, n)) return pdw_ferr();
    return PDE_OK;
}
pdw pdw_io_append(const char *name, pdw seg, pdw off, pdw n)
{
    g_seq = 1;
    if (os88_file_write_seq(name, seg, off, n, 1, &g_tok)) return pdw_ferr();
    return PDE_OK;
}
pdw pdw_io_close(const char *name)
{
    if (!g_seq) return PDE_OK;             /* a one-write file has nothing held */
    g_seq = 0;
    if (os88_file_write_seq(name, 0, 0, 0, 1, &g_tok)) return pdw_ferr();
    return PDE_OK;
}
pdw pdw_io_delete(const char *name)
{
    return os88_file_delete(name) ? pdw_ferr() : PDE_OK;
}
pdw pdw_io_rename(const char *from, const char *to)
{
    return os88_file_rename(from, to) ? pdw_ferr() : PDE_OK;
}
pdw pdw_src_open(const char *name)
{
    pdw i;
    for (i = 0; i < 14; i++) { g_srcname[i] = name[i]; if (!name[i]) break; }
    for (i = 0; i < 8; i++) g_cur[i] = 0;
    return PDE_OK;
}
pdw pdw_src_read(pdw seg, pdw off, pdw cap, pdw *got)
{
    pdw r = PDE_OK;
    pdw_in_src();
    *got = os88_file_read_seq(g_srcname, seg, off, cap, g_cur);
    if (*got == 0 && os88_ferr()) r = pdw_ferr();
    pdw_in_tgt();
    return r;
}
pdw pdw_src_back(const char *name, const struct pd_u32 *at, pdw seg, pdw off, pdw cap, pdw *got)
{
    static unsigned cur[8]; pdw i;
    for (i = 0; i < 8; i++) cur[i] = 0;
    cur[6] = at->lo; cur[7] = at->hi;
    *got = os88_file_read_seq(name, seg, off, cap, cur);
    if (*got == 0 && os88_ferr()) return pdw_ferr();
    return PDE_OK;
}
#endif
