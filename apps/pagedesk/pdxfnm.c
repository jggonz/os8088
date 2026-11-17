/* ============================================================================
 * os8088 - apps/pagedesk/pdxfnm.c      PDXF: glyph names
 *
 * #included by pdxf.c. PAGEDESK-SPEC 20.22.8. Adobe names for PD Latin
 * (Windows-1252) and the CFF standard strings, and Unicode <-> PD Latin.
 * ==========================================================================*/
#include "pdxfnm.h"

/* the idx'th NUL-separated string of tab, or 0 */
static const char *nm_nth(const char *tab, pdw idx)
{
    while (idx--) { while (*tab) tab++; tab++; if (!*tab) return 0; }
    return tab;
}
static pdw nm_eq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

/* PD Latin code (32..255) of an Adobe name, 0 = none; space/hyphen/bullet first */
static pdw nm_lat(const char *name)
{
    const char *p = pdx_latnames;
    pdw i = 0;
    if (nm_eq(name, "bullet")) return 149;
    if (nm_eq(name, "space")) return 32;
    if (nm_eq(name, "hyphen")) return 45;
    while (*p) {
        if (nm_eq(p, name)) return (pdw)(32 + i);
        while (*p) p++;
        p++; i++;
    }
    return 0;
}
/* Unicode of a PD Latin code */
static pdw lat_uni(pdw c)
{
    if (c >= 0x80 && c < 0xA0) return pdx_cp1252[c - 0x80];
    return c;
}
/* PD Latin code of a Unicode value, 0 = it has none */
static pdw uni_lat(pdw u)
{
    pdw i;
    if (u < 0x80 || (u >= 0xA0 && u <= 0xFF)) return u;
    for (i = 0; i < 32; i++) if (pdx_cp1252[i] == u && u) return (pdw)(0x80 + i);
    return 0;
}
static pdw hexv(pdw c)
{
    if (c >= '0' && c <= '9') return (pdw)(c - '0');
    if (c >= 'A' && c <= 'F') return (pdw)(c - 'A' + 10);
    if (c >= 'a' && c <= 'f') return (pdw)(c - 'a' + 10);
    return 0xFFFF;
}
/* Unicode of a glyph name: uniXXXX, uXXXX, else a PD Latin name; 0 = unknown */
static pdw nm_uni(const char *n)
{
    pdw i, v = 0, h, lc;
    if (n[0] == 'u' && n[1] == 'n' && n[2] == 'i') {
        for (i = 3; i < 7; i++) { h = hexv((pdb)n[i]); if (h == 0xFFFF) { v = 0; break; } v = (pdw)(v * 16 + h); }
        if (i == 7 && (!n[7] || n[7] == '.')) return v;
        v = 0;
    } else if (n[0] == 'u') {
        for (i = 1; i < 5; i++) { h = hexv((pdb)n[i]); if (h == 0xFFFF) { v = 0; break; } v = (pdw)(v * 16 + h); }
        if (i == 5 && !n[5]) return v;
    }
    lc = nm_lat(n);
    return lc ? lat_uni(lc) : 0;
}
