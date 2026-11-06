/* os8088 - apps/pagedesk/hosttest/pde_host.c
 * The OS file calls PDENG makes, over a stdio file, with
 * OSAPI_FILE_READ_SEQ's contract ENFORCED (SPEC.md 18.4.8): the capacity and
 * the offset must be cluster multiples, a short answer means the end, the
 * cursor advances by what was delivered. A call that breaks the contract
 * aborts the run loudly - the target would answer FERR_NAME or read the wrong
 * bytes. Fault injection: pdh_failat fails the Nth read; pdh_budget aborts a
 * run that makes more reads than a verb sequence can need (a hang guard). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../pdmath.h"
#include "../pde_os88.h"

unsigned char *pdh_file;
unsigned long pdh_size;
char pdh_name[14] = "DOC.PDF";
int pdh_cluster = 1;                    /* sectors per cluster */
unsigned long pdh_reads, pdh_failat, pdh_budget;
unsigned long pdh_bytes;
static int ferr;

int pdh_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END); pdh_size = (unsigned long)ftell(f); fseek(f, 0, SEEK_SET);
    pdh_file = malloc(pdh_size ? pdh_size : 1);
    if (pdh_size && fread(pdh_file, 1, pdh_size, f) != pdh_size) return -1;
    fclose(f);
    return 0;
}
void pdh_set(unsigned char *buf, unsigned long n) { pdh_file = buf; pdh_size = n; }

int os88_ferr(void) { return ferr; }
int os88_disk_cluster_sectors(void) { return pdh_cluster; }

int os88_file_find(int ordinal, struct os88_find *f)
{
    memset(f, 0, sizeof *f);
    if (ordinal != 0) { ferr = 5; return -1; }
    strcpy(f->name, pdh_name);
    f->type = 0;
    f->size_lo = (pdw)(pdh_size & 0xFFFF);
    f->size_hi = (pdw)(pdh_size >> 16);
    ferr = 0;
    return 1;
}

extern unsigned char pd_host_mem[];
pdw os88_file_read_seq(const char *name, pdw seg, pdw off, pdw cap, pdw *cur)
{
    unsigned long o, n, cb = (unsigned long)pdh_cluster * 512;
    pdh_reads++;
    if (pdh_budget && pdh_reads > pdh_budget) { fprintf(stderr, "STEP LIMIT: %lu reads\n", pdh_reads); exit(97); }
    if (strcmp(name, pdh_name)) { fprintf(stderr, "READ_SEQ: wrong name %s\n", name); exit(98); }
    if (pdh_failat && pdh_reads == pdh_failat) { ferr = 5; return 0; }
    o = (unsigned long)cur[6] | ((unsigned long)cur[7] << 16);
    if (cap == 0 || cap % cb || o % cb) {
        fprintf(stderr, "READ_SEQ CONTRACT: cap=%u off=%lu cluster=%lu\n", cap, o, cb); exit(99);
    }
    if ((unsigned long)off + cap > 0x10000UL) { fprintf(stderr, "READ_SEQ: buffer wraps\n"); exit(99); }
    ferr = 0;
    if (o >= pdh_size) return 0;
    n = pdh_size - o; if (n > cap) n = cap;
    memcpy(pd_host_mem + ((unsigned long)seg << 4) + off, pdh_file + o, n);
    o += n;
    cur[0] = 1; cur[4] = (pdw)(pdh_size & 0xFFFF); cur[5] = (pdw)(pdh_size >> 16);
    cur[6] = (pdw)(o & 0xFFFF); cur[7] = (pdw)(o >> 16);
    pdh_bytes += n;
    return (pdw)n;
}
