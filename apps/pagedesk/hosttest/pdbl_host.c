/* os8088 - apps/pagedesk/hosttest/pdbl_host.c
 * Host driver for PDXI's bilevel decoders (PAGEDESK-SPEC 20.22.12): CCITT and JBIG2 against a
 * file of coded bytes. The shell's byte source is a memory buffer here.
 *
 *   pdbl_host ccitt CODED K COLS ROWS FLAGS H WANT OUT
 *   pdbl_host jbig2 CODED GLOBALS|- H WANT OUT
 *
 * OUT is the rows concatenated, bl_orb bytes each; the header line goes to stdout:
 *   mode ow oh rb scale
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PD_HOST 1
#include "../pdabi.h"

static unsigned char *ix_buf[2]; static unsigned long ix_len[2], ix_pos; static int ix_cur;
static int ix_get(void) { return ix_pos < ix_len[ix_cur] ? ix_buf[ix_cur][ix_pos++] : -1; }
static pdw ix_open(pdw which) { ix_cur = which; ix_pos = 0; return which < 2 && ix_buf[which] ? 0 : 1; }
#include "../pdxiccit.c"
#ifdef HAVE_JB2
#include "../pdxijb2.c"
#endif

static unsigned char *slurp(const char *p, unsigned long *n)
{
    FILE *f = fopen(p, "rb"); unsigned char *b; long l;
    if (!f) { perror(p); exit(2); }
    fseek(f, 0, SEEK_END); l = ftell(f); fseek(f, 0, SEEK_SET);
    b = malloc(l + 1); if (fread(b, 1, l, f) != (size_t)l) exit(2); fclose(f); *n = l; return b;
}

int main(int argc, char **argv)
{
    FILE *o; pdw e, r;
    if (argc < 4) return 2;
    ix_buf[0] = slurp(argv[2], &ix_len[0]);
    if (!strcmp(argv[1], "ccitt")) {
        if (argc < 10) return 2;
        ix_open(0);
        e = cf_open((pdw)(short)atoi(argv[3]), (pdw)atoi(argv[4]), (pdw)atoi(argv[5]), (pdw)atoi(argv[6]),
                    (pdw)atoi(argv[7]), (pdw)atoi(argv[8]));
        o = fopen(argv[9], "wb");
        if (e) { printf("error %u\n", e); return 1; }
        printf("%u %u %u %u %u\n", bl_omode, bl_ow, bl_oh, bl_orb, bl_scale);
        for (r = 0; r < bl_oh; r++) {
            e = cf_getrow(r); if (e) { printf("rowerr %u at %u\n", e, r); return 1; }
            fwrite(bl_out, 1, bl_orb, o);
        }
        fclose(o); return 0;
    }
#ifdef HAVE_JB2
    if (!strcmp(argv[1], "jbig2")) {
        if (argc < 7) return 2;
        if (strcmp(argv[3], "-")) ix_buf[1] = slurp(argv[3], &ix_len[1]);
        e = jb_open(ix_buf[1] != 0, (pdw)atoi(argv[4]), (pdw)atoi(argv[5]));
        o = fopen(argv[6], "wb");
        if (e) { printf("error %u\n", e); return 1; }
        printf("%u %u %u %u %u\n", bl_omode, bl_ow, bl_oh, bl_orb, bl_scale);
        for (r = 0; r < bl_oh; r++) {
            e = jb_getrow(r); if (e) { printf("rowerr %u at %u\n", e, r); return 1; }
            fwrite(bl_out, 1, bl_orb, o);
        }
        fclose(o); return 0;
    }
#endif
    return 2;
}
