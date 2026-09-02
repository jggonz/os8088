/* ============================================================================
 * os8088 - apps/loom/hosttest/lmhost.c
 *
 * THE HOST HALF OF THE PACK GATE (WEAVE-SPEC 11.1, 12.4).
 *
 * It compiles LOOM's five compiler sources - apps/loom/lmerr.c, lmatom.c,
 * lmwml.c, lmwjs.c, lmsheet.c and lmwrite.c, THE SHIPPING TEXT and not a copy
 * of it - with the host's cc, stands the scratch workspace and the output
 * image up as plain arrays, and packs a project. tests/unit/t_lmpack.py
 * then diffs the result against `python3 tools/weavesim.py --pack` for every
 * demo and every template, and runs tests/weave/packerr/ for WEAVE-SPEC
 * 10.5's sentence identity - a fast-tier row, so it runs on every `make`.
 *
 * IT IS THE DEV LOOP AND IT IS NOT THE GATE. The gate is `weavepack`
 * (WEAVE-SPEC 12.3), which packs on the MACHINE and reads the bundle back off
 * the guest's floppy - because this host build differs from the 8086 one in
 * the one way that matters: `int` is 32 bits here and 16 bits there. So the
 * compilers are written to never depend on the width (every place 4.2's
 * 16-bit wrap is the answer masks explicitly with 0xFFFF), and this harness
 * proves the LOGIC while the machine proves the ARITHMETIC. Two instruments,
 * and the second one is the one a wave is allowed to close on. The
 * apps/cword/hosttest/cwuitest.c precedent, said about a compiler.
 *
 *   cc -std=c89 -Wall -o build/lmhost apps/loom/hosttest/lmhost.c
 *   build/lmhost <project.wml> <out.wab>
 * ==========================================================================*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* --- the os88 surface the compilers use, stubbed --------------------------
 * Six calls, and no more: the compilers do no I/O, take no claim and draw
 * nothing. That is itself worth checking - a compiler that reached for a
 * window could not be run here at all. */
static unsigned os88_strlen(const char *s)
{
    return (unsigned) strlen(s);
}

static void os88_strcpy(char *dst, const char *src, unsigned cap)
{
    unsigned i = 0;
    if (cap == 0)
        return;
    while (src[i] != 0 && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static char *os88_itoa(int v, char *dst7)
{
    sprintf(dst7, "%d", v);
    return dst7;
}

static char *os88_utoa(unsigned v, char *dst6)
{
    sprintf(dst6, "%u", v);
    return dst6;
}

static void os88_memset(void *p, int c, unsigned n)
{
    memset(p, c, n);
}

static void os88_memcpy(void *d, const void *s, unsigned n)
{
    memcpy(d, s, n);
}

/* wfx_frac - WEAVE-SPEC 5.1's decimal-to-16.16, which on the machine is ten
 * instructions in apps/weave/wnum.inc and here is the same arithmetic in a
 * `long`. It is the one routine this harness re-implements rather than
 * shares, and it is named as such: nasm is not in this build. */
static unsigned wfx_frac(unsigned num, unsigned den)
{
    return (unsigned) (((unsigned long) num * 65536UL + den / 2) / den);
}

/* --- the two claims, as arrays ------------------------------------------- */

#include "../../weave/weave.h"
#include "../loom.h"

static unsigned char lmh_work[LMW_END];
static unsigned char lmh_out[63488];
static unsigned char lmh_src[LM_NSLOT][LM_TEXTMAX + 1];
static unsigned      lmh_srcn[LM_NSLOT];

unsigned lm_wb(unsigned off)
{
    if (off >= LMW_END) {
        fprintf(stderr, "lmhost: scratch read out of range at %u\n", off);
        exit(3);
    }
    return lmh_work[off];
}

void lm_wpb(unsigned off, unsigned v)
{
    if (off >= LMW_END) {
        fprintf(stderr, "lmhost: scratch write out of range at %u\n", off);
        exit(3);
    }
    lmh_work[off] = (unsigned char) (v & 0xFF);
}

unsigned lm_ww(unsigned off)
{
    return lm_wb(off) | (lm_wb(off + 1) << 8);
}

void lm_wpw(unsigned off, unsigned v)
{
    lm_wpb(off, v & 0xFF);
    lm_wpb(off + 1, (v >> 8) & 0xFF);
}

void lm_wfill(unsigned off, unsigned v, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++)
        lm_wpb(off + i, v);
}

unsigned lm_ob(unsigned off)
{
    if (off >= sizeof lmh_out) {
        fprintf(stderr, "lmhost: image read out of range at %u\n", off);
        exit(3);
    }
    return lmh_out[off];
}

void lm_opb(unsigned off, unsigned v)
{
    if (off >= sizeof lmh_out) {
        fprintf(stderr, "lmhost: image write out of range at %u\n", off);
        exit(3);
    }
    lmh_out[off] = (unsigned char) (v & 0xFF);
}

void lm_opw(unsigned off, unsigned v)
{
    lm_opb(off, v & 0xFF);
    lm_opb(off + 1, (v >> 8) & 0xFF);
}

void lm_ofill(unsigned off, unsigned v, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++)
        lm_opb(off + i, v);
}


unsigned lm_srclen(int slot)
{
    if (slot < 0 || slot >= LM_NSLOT)
        return 0;
    return lmh_srcn[slot];
}

unsigned lm_sb(int slot, unsigned off)
{
    if (slot < 0 || slot >= LM_NSLOT || off >= lmh_srcn[slot])
        return 0;
    return lmh_src[slot][off];
}

/* --- THE SHIPPING COMPILERS ---------------------------------------------- */

#include "../lmerr.c"
#include "../lmatom.c"
#include "../lmwml.c"
#include "../lmwjs.c"
#include "../lmsheet.c"
#include "../lmwrite.c"

/* --- the project loader --------------------------------------------------
 * WEAVE-SPEC 11.2's rule, and it is tools/weavesim.py's pack_project()
 * verbatim: companions are found beside the .WML by the .WML's own stem
 * first, then by 11.2's project spellings. The two packers must agree about
 * WHICH FILE they read or the byte-identity gate compares two projects. */

static int lmh_load(int slot, const char *path)
{
    FILE *f = fopen(path, "rb");
    size_t n;

    if (!f)
        return 0;
    n = fread(lmh_src[slot], 1, LM_TEXTMAX, f);
    if (!feof(f)) {
        fprintf(stderr, "lmhost: %s is over LM_TEXTMAX (%d)\n", path,
                LM_TEXTMAX);
        exit(3);
    }
    fclose(f);
    lmh_srcn[slot] = (unsigned) n;
    return 1;
}

/* A case-insensitive 8.3 lookup in the project directory, which is what the
 * machine's FAT12 does by nature and what weavesim's _find_file() does by
 * hand. */
static int lmh_find(char *out, const char *dir, const char *name)
{
    static const char *tries[3];
    FILE *f;
    int i;
    char buf[512];
    char up[32], lo[32];
    unsigned k;

    for (k = 0; name[k] && k < 31; k++) {
        up[k] = (char) toupper((unsigned char) name[k]);
        lo[k] = (char) tolower((unsigned char) name[k]);
    }
    up[k] = 0;
    lo[k] = 0;
    tries[0] = name;
    tries[1] = up;
    tries[2] = lo;
    for (i = 0; i < 3; i++) {
        sprintf(buf, "%s%s%s", dir, dir[0] ? "/" : "", tries[i]);
        f = fopen(buf, "rb");
        if (f) {
            fclose(f);
            strcpy(out, buf);
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    char dir[512], stem[64], base[128], path[600], want[64];
    const char *p;
    size_t i;
    FILE *f;

    if (argc < 3) {
        fprintf(stderr, "usage: lmhost <project.wml> <out.wab>\n");
        return 2;
    }
    strcpy(path, argv[1]);
    p = strrchr(path, '/');
    if (p) {
        i = (size_t) (p - path);
        memcpy(dir, path, i);
        dir[i] = 0;
        strcpy(base, p + 1);
    } else {
        dir[0] = 0;
        strcpy(base, path);
    }
    strcpy(stem, base);
    p = strrchr(stem, '.');
    if (p)
        stem[(size_t) (p - stem)] = 0;

    if (!lmh_load(LM_SLOT_WML, argv[1])) {
        fprintf(stderr, "lmhost: cannot read %s\n", argv[1]);
        return 2;
    }
    lm_setfname(LM_SLOT_WML, base);

    /* The .WFX and .WSP are found before the WML is parsed, exactly as
     * weavesim finds them - the WML's <script src=""> names the .WJS, so
     * that one is loaded after ovl_wml() has run. */
    sprintf(want, "%s.WFX", stem);
    if (lmh_find(path, dir, want) || lmh_find(path, dir, "SHEET.WFX")) {
        lmh_load(LM_SLOT_WFX, path);
        p = strrchr(path, '/');
        lm_setfname(LM_SLOT_WFX, p ? p + 1 : path);
    }
    sprintf(want, "%s.WSP", stem);
    if (lmh_find(path, dir, want) || lmh_find(path, dir, "SPRITES.WSP")) {
        lmh_load(LM_SLOT_WSP, path);
        p = strrchr(path, '/');
        lm_setfname(LM_SLOT_WSP, p ? p + 1 : path);
    }

    if (!ovl_wml()) {
        fprintf(stderr, "%s\n", lm_errtext());
        return 1;
    }
    if (lm_hasscript) {
        if (!lmh_find(path, dir, lm_scriptsrc)) {
            char msg[LM_ERRMAX];
            msg[0] = 0;
            lm_cat(msg, "script: src=\"");
            lm_cat(msg, lm_scriptsrc);
            lm_cat(msg, "\" not found beside the .WML");
            lm_perr(LM_SLOT_WML, lm_scriptline, msg);
            fprintf(stderr, "%s\n", lm_errtext());
            return 1;
        }
        lmh_load(LM_SLOT_WJS, path);
        p = strrchr(path, '/');
        lm_setfname(LM_SLOT_WJS, p ? p + 1 : path);
    }
    if (!ovl_pack_rest()) {
        fprintf(stderr, "%s\n", lm_errtext());
        return 1;
    }
    f = fopen(argv[2], "wb");
    if (!f) {
        fprintf(stderr, "lmhost: cannot write %s\n", argv[2]);
        return 2;
    }
    fwrite(lmh_out, 1, lm_outlen(), f);
    fclose(f);
    return 0;
}
