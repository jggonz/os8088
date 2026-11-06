/* os88.h stand-in for the PDREND host build (PAGEDESK-SPEC 2.5) */
#ifndef PDR_HOST_H
#define PDR_HOST_H
void os88_gfx_lock(void);
void os88_gfx_unlock(void);
int  os88_wm_clip_set(void *win);
#endif
