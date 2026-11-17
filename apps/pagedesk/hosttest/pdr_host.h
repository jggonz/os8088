/* os88.h stand-in for the PDREND host build (PAGEDESK-SPEC 2.5) */
#ifndef PDR_HOST_H
#define PDR_HOST_H
struct os88_place { unsigned clus; int vol; };
/* the package's folder and the document's: different folders on different volumes */
#define PDRH_PKG_CLUS 0x0123u
#define PDRH_PKG_VOL  1
#define PDRH_DOC_CLUS 0x0456u
#define PDRH_DOC_VOL  0
extern unsigned pdrh_here_clus; extern int pdrh_here_vol; extern long pdrh_wrongdir;
int pdrh_in_doc(void);
void os88_gfx_lock(void);
void os88_gfx_unlock(void);
int  os88_wm_clip_set(void *win);
unsigned os88_ticks(void);              /* the slice's clock (pdri.c interp_run, QA R27) */
void os88_file_here(struct os88_place *p);
int  os88_file_goto_q_mark(unsigned clus, int vol);
int  os88_disk_cluster_sectors(void);
unsigned os88_file_read_seq(const char *name, unsigned seg, unsigned off, unsigned cap, unsigned *cursor);
#endif
