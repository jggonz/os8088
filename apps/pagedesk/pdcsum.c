/* ============================================================================
 * os8088 - apps/pagedesk/pdcsum.c     streaming MD5 / SHA-256 / SHA-384 / SHA-512
 * (PAGEDESK-SPEC 20.22.5). #included by pdxk.c. One hash is live at a time:
 * the state is a static, nothing is addressed on the stack (SPEC.md 73).
 * The block functions are pdcrypto.h's (assembly on the target, pdctwin.c on
 * the host); this file is padding, buffering and the digest's byte order.
 * ==========================================================================*/
#define HS_MD5     0
#define HS_SHA256  1
#define HS_SHA384  2
#define HS_SHA512  3

static const pdw iv256[16] = {
    0xe667, 0x6a09, 0xae85, 0xbb67, 0xf372, 0x3c6e, 0xf53a, 0xa54f,
    0x527f, 0x510e, 0x688c, 0x9b05, 0xd9ab, 0x1f83, 0xcd19, 0x5be0 };
static const pdw iv384[32] = {
    0x9ed8, 0xc105, 0x9d5d, 0xcbbb, 0xd507, 0x367c, 0x292a, 0x629a,
    0xdd17, 0x3070, 0x015a, 0x9159, 0x5939, 0xf70e, 0xecd8, 0x152f,
    0x0b31, 0xffc0, 0x2667, 0x6733, 0x1511, 0x6858, 0x4a87, 0x8eb4,
    0x8fa7, 0x64f9, 0x2e0d, 0xdb0c, 0x4fa4, 0xbefa, 0x481d, 0x47b5 };
static const pdw iv512[32] = {
    0xc908, 0xf3bc, 0xe667, 0x6a09, 0xa73b, 0x84ca, 0xae85, 0xbb67,
    0xf82b, 0xfe94, 0xf372, 0x3c6e, 0x36f1, 0x5f1d, 0xf53a, 0xa54f,
    0x82d1, 0xade6, 0x527f, 0x510e, 0x6c1f, 0x2b3e, 0x688c, 0x9b05,
    0xbd6b, 0xfb41, 0xd9ab, 0x1f83, 0x2179, 0x137e, 0xcd19, 0x5be0 };

static pdw hs_st[32];           /* the chaining state (pdcrypto.h layouts)   */
static pdb hs_buf[128];
static pdw hs_n;                /* bytes in hs_buf                           */
static pdw hs_lo, hs_hi;        /* bytes hashed so far (2^32 is far enough)  */
static pdw hs_kind;

static void hs_init(pdw kind)
{
    pdw i;
    hs_kind = kind; hs_n = 0; hs_lo = 0; hs_hi = 0;
    if (kind == HS_MD5) { pdc_md5_init(hs_st); return; }
    if (kind == HS_SHA256) { for (i = 0; i < 16; i++) hs_st[i] = iv256[i]; return; }
    for (i = 0; i < 32; i++) hs_st[i] = (kind == HS_SHA384) ? iv384[i] : iv512[i];
}

static pdw hs_bsz(void) { return hs_kind >= HS_SHA384 ? 128 : 64; }

static void hs_block(void)
{
    if (hs_kind == HS_MD5) pdc_md5_block(hs_st, hs_buf);
    else if (hs_kind == HS_SHA256) pdc_sha256_block(hs_st, hs_buf);
    else pdc_sha512_block(hs_st, hs_buf);
    hs_n = 0;
}

static void hs_feed(const pdb *p, pdw n)
{
    pdw bsz = hs_bsz(), old = hs_lo;
    hs_lo = (pdw)(hs_lo + n);
    if (hs_lo < old) hs_hi++;
    while (n--) {
        hs_buf[hs_n++] = *p++;
        if (hs_n == bsz) hs_block();
    }
}

/* the digest into out: 16, 32, 48 or 64 bytes */
static void hs_final(pdb *out)
{
    pdw bsz = hs_bsz(), lim = bsz - (hs_kind >= HS_SHA384 ? 16 : 8), i, k, w, nw;
    pdw blo = (pdw)(hs_lo << 3), bmid = (pdw)((hs_lo >> 13) | (hs_hi << 3)), bhi = (pdw)(hs_hi >> 13);
    hs_buf[hs_n++] = 0x80;
    if (hs_n > lim) {
        while (hs_n < bsz) hs_buf[hs_n++] = 0;
        hs_block();
    }
    while (hs_n < bsz) hs_buf[hs_n++] = 0;
    if (hs_kind == HS_MD5) {                   /* 64-bit little-endian length */
        hs_buf[56] = (pdb)blo; hs_buf[57] = (pdb)(blo >> 8);
        hs_buf[58] = (pdb)bmid; hs_buf[59] = (pdb)(bmid >> 8);
        hs_buf[60] = (pdb)bhi; hs_buf[61] = (pdb)(bhi >> 8);
    } else {                                   /* big-endian, in the last 8 bytes */
        hs_buf[bsz - 1] = (pdb)blo; hs_buf[bsz - 2] = (pdb)(blo >> 8);
        hs_buf[bsz - 3] = (pdb)bmid; hs_buf[bsz - 4] = (pdb)(bmid >> 8);
        hs_buf[bsz - 5] = (pdb)bhi; hs_buf[bsz - 6] = (pdb)(bhi >> 8);
    }
    hs_block();
    if (hs_kind == HS_MD5) {
        for (i = 0; i < 8; i++) { out[2 * i] = (pdb)hs_st[i]; out[2 * i + 1] = (pdb)(hs_st[i] >> 8); }
        return;
    }
    k = 0;
    if (hs_kind == HS_SHA256) {                /* eight words, hi then lo, big-endian */
        for (i = 0; i < 8; i++) {
            w = hs_st[2 * i + 1]; out[k++] = (pdb)(w >> 8); out[k++] = (pdb)w;
            w = hs_st[2 * i];     out[k++] = (pdb)(w >> 8); out[k++] = (pdb)w;
        }
        return;
    }
    nw = (hs_kind == HS_SHA384) ? 6 : 8;       /* qwords: four pdw, most significant last */
    for (i = 0; i < nw; i++) {
        for (w = 4; w > 0; w--) { out[k++] = (pdb)(hs_st[4 * i + w - 1] >> 8); out[k++] = (pdb)hs_st[4 * i + w - 1]; }
    }
}
