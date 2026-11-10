/* os8088 - apps/pagedesk/hosttest/pdc_fuzz.c
 * ASan/UBSan fuzz of the encryption path (PAGEDESK-SPEC 20.22.5): PDXK's authentication and PDENG's
 * decryption over mutated encrypted files. Deterministic (a seed), in-process, no corpus files:
 * each iteration copies a fixture, mutates it - a random byte, a run set to 00 or FF, a digit of a
 * /Length, a cut tail, a duplicated block, a short or oversized ciphertext (the bytes after
 * "stream" are cut to 0..40) - and then runs the SAME sequence the primary does (INIT, OPEN,
 * KEY_AUTH, SETKEY, OPEN, every stream, every string). Any result is fine; a crash, a hang (the
 * read budget), or a sanitizer report is the failure.
 *
 *   pdc_fuzz SEED ITERATIONS PASSWORD FILE...
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define main e2e_main
#include "pdc_e2e.c"
#undef main

extern unsigned char *pdh_file;
extern unsigned long pdh_size, pdh_budget;
extern void pdh_set(unsigned char *buf, unsigned long n);

static unsigned long rs;
static unsigned rnd(void) { rs = rs * 6364136223846793005UL + 1442695040888963407UL; return (unsigned)(rs >> 33); }

static void mutate(unsigned char *b, unsigned long *n)
{
    unsigned k, m = 1 + rnd() % 6, i, at, len;
    for (k = 0; k < m; k++) {
        if (*n < 160) break;
        switch (rnd() % 8) {
        case 0: b[rnd() % *n] ^= (unsigned char)(1 << (rnd() % 8)); break;
        case 1: b[rnd() % *n] = (unsigned char)rnd(); break;
        case 2: at = rnd() % *n; len = 1 + rnd() % 64; for (i = 0; i < len && at + i < *n; i++) b[at + i] = (rnd() & 1) ? 0xFF : 0; break;
        case 3: {                                              /* a digit after /Length */
            unsigned char *p = memmem(b, *n, "/Length ", 8);
            if (p) { p += 8; if (p < b + *n && *p >= '0' && *p <= '9') *p = (unsigned char)('0' + rnd() % 10); }
            break;
        }
        case 4: *n = 16 + rnd() % (*n - 16); break;            /* a cut tail */
        case 5: {                                              /* the data after "stream" cut to 0..40 bytes (the /Length now lies) */
            unsigned char *p = memmem(b, *n, "stream\n", 7), *q;
            if (p) {
                p += 7;
                q = memmem(p, (size_t)(b + *n - p), "endstream", 9);
                len = rnd() % 41;
                if (q && (unsigned long)(q - p) > len) { memmove(p + len, q, (size_t)(b + *n - q)); *n -= (unsigned long)(q - p) - len; }
            }
            break;
        }
        case 6: at = rnd() % (*n - 64); memmove(b + at, b + rnd() % (*n - 64), 8 + rnd() % 48); break;
        case 7: { unsigned char *p = memmem(b, *n, "/O <", 4); if (!p) p = memmem(b, *n, "/U (", 4); if (p) p[4 + rnd() % 16] ^= 0x55; break; }
        }
    }
}

int main(int argc, char **argv)
{
    unsigned long seed = strtoul(argv[1], 0, 10), iters = strtoul(argv[2], 0, 10), it;
    const char *pw = argv[3];
    int f, rc, ok = 0, tot = 0;
    static unsigned char buf[1 << 17];
    for (f = 4; f < argc; f++) {
        FILE *fp = fopen(argv[f], "rb");
        unsigned char *orig;
        unsigned long on;
        if (!fp) { fprintf(stderr, "cannot read %s\n", argv[f]); return 2; }
        fseek(fp, 0, SEEK_END); on = (unsigned long)ftell(fp); fseek(fp, 0, SEEK_SET);
        orig = malloc(on);
        if (fread(orig, 1, on, fp) != on) return 2;
        fclose(fp);
        for (it = 0; it < iters; it++) {
            unsigned long n = on;
            rs = seed * 1000003UL + it * 7919UL + (unsigned long)f;
            memcpy(buf, orig, on);
            if (it) mutate(buf, &n);
            pdh_set(buf, n);
            pdh_reads = 0; pdh_budget = 6000;
            if (do_init()) continue;
            memset(&op, 0, sizeof op);
            strcpy(op.name, "DOC.PDF");
            rc = open_loop();
            tot++;
            if (rc == PDE_PASSWORD) {
                if (do_auth(strcmp(pw, "-") ? pw : "-") == 0) {
                    do_setkey();
                    rc = open_loop();
                } else continue;
            }
            if (rc == 0) {
                struct pdxb_trailer tr;
                unsigned o;
                call(PDV_ENG_TRAILER, 0, 0, 0);
                xb_get(&tr, sizeof tr);
                for (o = 1; o < tr.size && o < 64; o++) { dump_stream(o, 0, 0, 0); dump_strings(o); }
                dump_strings(tr.info_num);
                do_plain(0, 61);
                ok++;
            }
            call(PDV_ENG_CLOSE, 0, 0, 0);
        }
        free(orig);
    }
    fprintf(stderr, "pdc_fuzz: %d runs, %d opened\n", tot, ok);
    return 0;
}
