/* os8088 - apps/pagedesk/pde_os88.h - what pdeng.c asks of the OS, for the
 * HOST build only (-DPD_HOST). On the target pdeng.c includes apps/cc/os88.h
 * and these are the thunks in apps/cc/os88thunk.asm. hosttest/pde_host.c
 * implements them over stdio with READ_SEQ's contract enforced. */
#ifndef PDE_OS88_H
#define PDE_OS88_H
#include "pdabi.h"
struct os88_find {
    char name[13];
    unsigned char attr;
    pdw type;
    pdw clus;
    pdw size_lo, size_hi;
    pdw rsvd;
};
#define OS88_FT_DIR 2
int os88_file_find(int ordinal, struct os88_find *f);
pdw os88_file_read_seq(const char *name, pdw seg, pdw off, pdw cap, pdw *cursor);
int os88_ferr(void);
int os88_disk_cluster_sectors(void);
#endif
