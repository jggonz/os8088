/* os8088 - apps/pagedesk/hosttest/pdhost.c - the host's 1 MiB "real mode"
 * arena that pdmath.c's far-memory movers address as seg*16+off, and
 * pd_ds() for a host build (PAGEDESK-SPEC 2.5). Shared by every host
 * harness; a harness that models several modules sets pd_host_ds itself. */
#include "../pdmath.h"
unsigned char pd_host_mem[0x110000];
pdw pd_host_ds = 0x1000;
pdw pd_ds(void) { return pd_host_ds; }
