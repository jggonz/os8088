/* os88.h stand-in for the PDREND host build (PAGEDESK-SPEC 2.5) */
#ifndef PDR_HOST_H
#define PDR_HOST_H
struct os88_place { unsigned clus; int vol; };
void os88_gfx_lock(void);
void os88_gfx_unlock(void);
int  os88_wm_clip_set(void *win);
void os88_file_here(struct os88_place *p);
int  os88_file_goto_q_mark(unsigned clus, int vol);
int  os88_disk_cluster_sectors(void);
unsigned os88_file_read_seq(const char *name, unsigned seg, unsigned off, unsigned cap, unsigned *cursor);
#endif
