/* ============================================================================
 * os8088 - apps/pagedesk/pdctwin.c     HOST twins of pdcrypto.h (PD_HOST only)
 *
 * Plain C99 with the host's own 32 and 64-bit integers, written from the
 * standards (RFC 1321, FIPS 180-4, FIPS 197) and nothing else: the reference
 * the assembly is held to. Never compiled for the target.
 * ==========================================================================*/
#include <stdint.h>
#include <string.h>
#include "pdabi.h"
#include "pdcrypto.h"

/* weak: pdeng.c and pdxk.c each include this file on the host, and a harness links both */
#define PDC_WEAK __attribute__((weak))

#define ROL32(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#define ROR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define ROR64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))

static uint32_t g32(const pdw *p, int i) { return (uint32_t)p[2*i] | ((uint32_t)p[2*i+1] << 16); }
static void s32(pdw *p, int i, uint32_t v) { p[2*i] = (pdw)v; p[2*i+1] = (pdw)(v >> 16); }
static uint64_t g64(const pdw *p, int i)
{
    return (uint64_t)p[4*i] | ((uint64_t)p[4*i+1] << 16) | ((uint64_t)p[4*i+2] << 32) | ((uint64_t)p[4*i+3] << 48);
}
static void s64(pdw *p, int i, uint64_t v)
{
    p[4*i] = (pdw)v; p[4*i+1] = (pdw)(v >> 16); p[4*i+2] = (pdw)(v >> 32); p[4*i+3] = (pdw)(v >> 48);
}

/* ---- MD5 ---------------------------------------------------------------- */
static const uint32_t md5_k[64] = {
0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391 };
static const uint8_t md5_s[64] = {
7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22, 5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23, 6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21 };

PDC_WEAK pdb pdc_ok[16];
PDC_WEAK void pdc_okey(const pdb *key, pdw klen, pdw num, pdw gen, pdw aes)
{
    pdb b[64]; pdw st[8], n = 0, i;
    memset(b, 0, sizeof b);
    for (i = 0; i < klen; i++) b[n++] = key[i];
    b[n++] = (pdb)num; b[n++] = (pdb)(num >> 8); b[n++] = 0; b[n++] = (pdb)gen; b[n++] = (pdb)(gen >> 8);
    if (aes) { b[n++] = 's'; b[n++] = 'A'; b[n++] = 'l'; b[n++] = 'T'; }
    b[n] = 0x80; b[56] = (pdb)(n << 3);
    pdc_md5_init(st); pdc_md5_block(st, b);
    for (i = 0; i < 8; i++) { pdc_ok[2 * i] = (pdb)st[i]; pdc_ok[2 * i + 1] = (pdb)(st[i] >> 8); }
}

PDC_WEAK void pdc_md5_init(pdw *st)
{
    s32(st, 0, 0x67452301); s32(st, 1, 0xefcdab89); s32(st, 2, 0x98badcfe); s32(st, 3, 0x10325476);
}
PDC_WEAK void pdc_md5_block(pdw *st, const pdb *blk)
{
    uint32_t m[16], a = g32(st,0), b = g32(st,1), c = g32(st,2), d = g32(st,3), f, t;
    int i, g;
    for (i = 0; i < 16; i++)
        m[i] = (uint32_t)blk[4*i] | ((uint32_t)blk[4*i+1] << 8) | ((uint32_t)blk[4*i+2] << 16) | ((uint32_t)blk[4*i+3] << 24);
    for (i = 0; i < 64; i++) {
        if (i < 16)      { f = (b & c) | (~b & d); g = i; }
        else if (i < 32) { f = (d & b) | (~d & c); g = (5*i + 1) & 15; }
        else if (i < 48) { f = b ^ c ^ d;          g = (3*i + 5) & 15; }
        else             { f = c ^ (b | ~d);       g = (7*i) & 15; }
        t = d; d = c; c = b;
        b = b + ROL32(a + f + md5_k[i] + m[g], md5_s[i]);
        a = t;
    }
    s32(st,0,g32(st,0)+a); s32(st,1,g32(st,1)+b); s32(st,2,g32(st,2)+c); s32(st,3,g32(st,3)+d);
}

/* ---- SHA-256 ------------------------------------------------------------ */
static const uint32_t k256[64] = {
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
PDC_WEAK void pdc_sha256_init(pdw *st)
{
    static const uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    int i; for (i = 0; i < 8; i++) s32(st, i, h[i]);
}
PDC_WEAK void pdc_sha256_block(pdw *st, const pdb *blk)
{
    uint32_t w[64], v[8], t1, t2; int i;
    for (i = 0; i < 16; i++)
        w[i] = ((uint32_t)blk[4*i] << 24) | ((uint32_t)blk[4*i+1] << 16) | ((uint32_t)blk[4*i+2] << 8) | blk[4*i+3];
    for (i = 16; i < 64; i++) {
        uint32_t s0 = ROR32(w[i-15],7) ^ ROR32(w[i-15],18) ^ (w[i-15] >> 3);
        uint32_t s1 = ROR32(w[i-2],17) ^ ROR32(w[i-2],19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    for (i = 0; i < 8; i++) v[i] = g32(st, i);
    for (i = 0; i < 64; i++) {
        t1 = v[7] + (ROR32(v[4],6) ^ ROR32(v[4],11) ^ ROR32(v[4],25)) + ((v[4] & v[5]) ^ (~v[4] & v[6])) + k256[i] + w[i];
        t2 = (ROR32(v[0],2) ^ ROR32(v[0],13) ^ ROR32(v[0],22)) + ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
        v[7]=v[6]; v[6]=v[5]; v[5]=v[4]; v[4]=v[3]+t1; v[3]=v[2]; v[2]=v[1]; v[1]=v[0]; v[0]=t1+t2;
    }
    for (i = 0; i < 8; i++) s32(st, i, g32(st, i) + v[i]);
}

/* ---- SHA-512 / SHA-384 ---------------------------------------------------- */
static const uint64_t k512[80] = {
0x428a2f98d728ae22ULL,0x7137449123ef65cdULL,0xb5c0fbcfec4d3b2fULL,0xe9b5dba58189dbbcULL,0x3956c25bf348b538ULL,
0x59f111f1b605d019ULL,0x923f82a4af194f9bULL,0xab1c5ed5da6d8118ULL,0xd807aa98a3030242ULL,0x12835b0145706fbeULL,
0x243185be4ee4b28cULL,0x550c7dc3d5ffb4e2ULL,0x72be5d74f27b896fULL,0x80deb1fe3b1696b1ULL,0x9bdc06a725c71235ULL,
0xc19bf174cf692694ULL,0xe49b69c19ef14ad2ULL,0xefbe4786384f25e3ULL,0x0fc19dc68b8cd5b5ULL,0x240ca1cc77ac9c65ULL,
0x2de92c6f592b0275ULL,0x4a7484aa6ea6e483ULL,0x5cb0a9dcbd41fbd4ULL,0x76f988da831153b5ULL,0x983e5152ee66dfabULL,
0xa831c66d2db43210ULL,0xb00327c898fb213fULL,0xbf597fc7beef0ee4ULL,0xc6e00bf33da88fc2ULL,0xd5a79147930aa725ULL,
0x06ca6351e003826fULL,0x142929670a0e6e70ULL,0x27b70a8546d22ffcULL,0x2e1b21385c26c926ULL,0x4d2c6dfc5ac42aedULL,
0x53380d139d95b3dfULL,0x650a73548baf63deULL,0x766a0abb3c77b2a8ULL,0x81c2c92e47edaee6ULL,0x92722c851482353bULL,
0xa2bfe8a14cf10364ULL,0xa81a664bbc423001ULL,0xc24b8b70d0f89791ULL,0xc76c51a30654be30ULL,0xd192e819d6ef5218ULL,
0xd69906245565a910ULL,0xf40e35855771202aULL,0x106aa07032bbd1b8ULL,0x19a4c116b8d2d0c8ULL,0x1e376c085141ab53ULL,
0x2748774cdf8eeb99ULL,0x34b0bcb5e19b48a8ULL,0x391c0cb3c5c95a63ULL,0x4ed8aa4ae3418acbULL,0x5b9cca4f7763e373ULL,
0x682e6ff3d6b2b8a3ULL,0x748f82ee5defb2fcULL,0x78a5636f43172f60ULL,0x84c87814a1f0ab72ULL,0x8cc702081a6439ecULL,
0x90befffa23631e28ULL,0xa4506cebde82bde9ULL,0xbef9a3f7b2c67915ULL,0xc67178f2e372532bULL,0xca273eceea26619cULL,
0xd186b8c721c0c207ULL,0xeada7dd6cde0eb1eULL,0xf57d4f7fee6ed178ULL,0x06f067aa72176fbaULL,0x0a637dc5a2c898a6ULL,
0x113f9804bef90daeULL,0x1b710b35131c471bULL,0x28db77f523047d84ULL,0x32caab7b40c72493ULL,0x3c9ebe0a15c9bebcULL,
0x431d67c49c100d4cULL,0x4cc5d4becb3e42b6ULL,0x597f299cfc657e2aULL,0x5fcb6fab3ad6faecULL,0x6c44198c4a475817ULL };
static void init512(pdw *st, const uint64_t *h) { int i; for (i = 0; i < 8; i++) s64(st, i, h[i]); }
PDC_WEAK void pdc_sha512_init(pdw *st)
{
    static const uint64_t h[8] = {0x6a09e667f3bcc908ULL,0xbb67ae8584caa73bULL,0x3c6ef372fe94f82bULL,0xa54ff53a5f1d36f1ULL,
        0x510e527fade682d1ULL,0x9b05688c2b3e6c1fULL,0x1f83d9abfb41bd6bULL,0x5be0cd19137e2179ULL};
    init512(st, h);
}
PDC_WEAK void pdc_sha384_init(pdw *st)
{
    static const uint64_t h[8] = {0xcbbb9d5dc1059ed8ULL,0x629a292a367cd507ULL,0x9159015a3070dd17ULL,0x152fecd8f70e5939ULL,
        0x67332667ffc00b31ULL,0x8eb44a8768581511ULL,0xdb0c2e0d64f98fa7ULL,0x47b5481dbefa4fa4ULL};
    init512(st, h);
}
PDC_WEAK void pdc_sha512_block(pdw *st, const pdb *blk)
{
    uint64_t w[80], v[8], t1, t2; int i, j;
    for (i = 0; i < 16; i++) { w[i] = 0; for (j = 0; j < 8; j++) w[i] = (w[i] << 8) | blk[8*i+j]; }
    for (i = 16; i < 80; i++) {
        uint64_t s0 = ROR64(w[i-15],1) ^ ROR64(w[i-15],8) ^ (w[i-15] >> 7);
        uint64_t s1 = ROR64(w[i-2],19) ^ ROR64(w[i-2],61) ^ (w[i-2] >> 6);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    for (i = 0; i < 8; i++) v[i] = g64(st, i);
    for (i = 0; i < 80; i++) {
        t1 = v[7] + (ROR64(v[4],14) ^ ROR64(v[4],18) ^ ROR64(v[4],41)) + ((v[4] & v[5]) ^ (~v[4] & v[6])) + k512[i] + w[i];
        t2 = (ROR64(v[0],28) ^ ROR64(v[0],34) ^ ROR64(v[0],39)) + ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
        v[7]=v[6]; v[6]=v[5]; v[5]=v[4]; v[4]=v[3]+t1; v[3]=v[2]; v[2]=v[1]; v[1]=v[0]; v[0]=t1+t2;
    }
    for (i = 0; i < 8; i++) s64(st, i, g64(st, i) + v[i]);
}

/* ---- RC4 ---------------------------------------------------------------- */
PDC_WEAK void pdc_rc4_ks(pdb *s, const pdb *key, pdw n)
{
    int i, j = 0; pdb t;
    for (i = 0; i < 256; i++) s[i] = (pdb)i;
    for (i = 0; i < 256; i++) { j = (j + s[i] + key[i % n]) & 255; t = s[i]; s[i] = s[j]; s[j] = t; }
    s[256] = 0; s[257] = 0;
}
PDC_WEAK void pdc_rc4(pdb *s, pdb *buf, pdw n)
{
    unsigned i = s[256], j = s[257]; pdb t;
    while (n--) {
        i = (i + 1) & 255; j = (j + s[i]) & 255;
        t = s[i]; s[i] = s[j]; s[j] = t;
        *buf++ ^= s[(s[i] + s[j]) & 255];
    }
    s[256] = (pdb)i; s[257] = (pdb)j;
}

/* ---- AES ---------------------------------------------------------------- */
static uint8_t sb[256], isb[256];
static int sb_ready;
static uint8_t xt(uint8_t a) { return (uint8_t)((a << 1) ^ ((a & 0x80) ? 0x1b : 0)); }
static uint8_t gmul(uint8_t a, uint8_t b)
{
    uint8_t r = 0; while (b) { if (b & 1) r ^= a; a = xt(a); b >>= 1; } return r;
}
static void sb_init(void)
{
    int i, p = 1, q = 1;
    if (sb_ready) return;
    /* the standard generator walk (FIPS 197 5.1.1's S-box by construction) */
    do {
        uint8_t x;
        p = p ^ (p << 1) ^ ((p & 0x80) ? 0x11b : 0); p &= 255;
        q ^= q << 1; q ^= q << 2; q ^= q << 4; q &= 255; if (q & 0x80) q ^= 0x09;
        x = (uint8_t)(q ^ ((q << 1) | (q >> 7)) ^ ((q << 2) | (q >> 6)) ^ ((q << 3) | (q >> 5)) ^ ((q << 4) | (q >> 4)));
        sb[p] = x ^ 0x63;
    } while (p != 1);
    sb[0] = 0x63;
    for (i = 0; i < 256; i++) isb[sb[i]] = (uint8_t)i;
    sb_ready = 1;
}
PDC_WEAK void pdc_aes_ks(pdb *rk, const pdb *key, pdw nk)
{
    int i, total = 4 * (nk + 7); uint8_t rcon = 1, t[4], u;
    sb_init();
    memcpy(rk, key, 4 * nk);
    for (i = nk; i < total; i++) {
        memcpy(t, rk + 4 * (i - 1), 4);
        if (i % nk == 0) {
            u = t[0]; t[0] = sb[t[1]] ^ rcon; t[1] = sb[t[2]]; t[2] = sb[t[3]]; t[3] = sb[u];
            rcon = xt(rcon);
        } else if (nk > 6 && i % nk == 4) {
            t[0] = sb[t[0]]; t[1] = sb[t[1]]; t[2] = sb[t[2]]; t[3] = sb[t[3]];
        }
        for (u = 0; u < 4; u++) rk[4*i + u] = rk[4*(i - nk) + u] ^ t[u];
    }
}
PDC_WEAK void pdc_aes_enc(const pdb *rk, pdw nr, const pdb *in, pdb *out)
{
    uint8_t s[16], t[16]; int r, i, c;
    sb_init();
    for (i = 0; i < 16; i++) s[i] = in[i] ^ rk[i];
    for (r = 1; r <= (int)nr; r++) {
        for (i = 0; i < 16; i++) t[i] = sb[s[i]];
        for (c = 0; c < 4; c++) for (i = 0; i < 4; i++) s[4*c + i] = t[4*((c + i) & 3) + i];
        if (r != (int)nr)
            for (c = 0; c < 4; c++) {
                uint8_t a0 = s[4*c], a1 = s[4*c+1], a2 = s[4*c+2], a3 = s[4*c+3];
                s[4*c]   = xt(a0) ^ (xt(a1) ^ a1) ^ a2 ^ a3;
                s[4*c+1] = a0 ^ xt(a1) ^ (xt(a2) ^ a2) ^ a3;
                s[4*c+2] = a0 ^ a1 ^ xt(a2) ^ (xt(a3) ^ a3);
                s[4*c+3] = (xt(a0) ^ a0) ^ a1 ^ a2 ^ xt(a3);
            }
        for (i = 0; i < 16; i++) s[i] ^= rk[16*r + i];
    }
    memcpy(out, s, 16);
}
/* the decryption schedule (FIPS 197 5.3.5): rk[1..nr-1] through InvMixColumns, in place */
static void imc(uint8_t *s)
{
    int c;
    for (c = 0; c < 4; c++) {
        uint8_t a0 = s[4*c], a1 = s[4*c+1], a2 = s[4*c+2], a3 = s[4*c+3];
        s[4*c]   = gmul(a0,14) ^ gmul(a1,11) ^ gmul(a2,13) ^ gmul(a3,9);
        s[4*c+1] = gmul(a0,9)  ^ gmul(a1,14) ^ gmul(a2,11) ^ gmul(a3,13);
        s[4*c+2] = gmul(a0,13) ^ gmul(a1,9)  ^ gmul(a2,14) ^ gmul(a3,11);
        s[4*c+3] = gmul(a0,11) ^ gmul(a1,13) ^ gmul(a2,9)  ^ gmul(a3,14);
    }
}
PDC_WEAK void pdc_aes_dks(pdb *rk, pdw nr)
{
    pdw r;
    sb_init();
    for (r = 1; r < nr; r++) imc(rk + 16 * r);
}
PDC_WEAK void pdc_aes_dec(const pdb *rk, pdw nr, const pdb *in, pdb *out)     /* rk: the decryption schedule */
{
    uint8_t s[16], t[16]; int r, i, c;
    sb_init();
    for (i = 0; i < 16; i++) s[i] = in[i] ^ rk[16*nr + i];
    for (r = (int)nr - 1; r >= 0; r--) {
        for (c = 0; c < 4; c++) for (i = 0; i < 4; i++) t[4*((c + i) & 3) + i] = s[4*c + i];   /* InvShiftRows */
        for (i = 0; i < 16; i++) s[i] = isb[t[i]];
        if (r != 0) imc(s);
        for (i = 0; i < 16; i++) s[i] ^= rk[16*r + i];
    }
    memcpy(out, s, 16);
}
PDC_WEAK void pdc_cbc_enc(const pdb *rk, pdw nr, pdb *iv, pdb *buf, pdw nblk)
{
    int i;
    while (nblk--) {
        for (i = 0; i < 16; i++) buf[i] ^= iv[i];
        pdc_aes_enc(rk, nr, buf, buf);
        memcpy(iv, buf, 16); buf += 16;
    }
}
PDC_WEAK void pdc_cbc_dec(const pdb *rk, pdw nr, pdb *iv, pdb *buf, pdw nblk)
{
    pdb c[16]; int i;
    while (nblk--) {
        memcpy(c, buf, 16);
        pdc_aes_dec(rk, nr, buf, buf);
        for (i = 0; i < 16; i++) buf[i] ^= iv[i];
        memcpy(iv, c, 16); buf += 16;
    }
}
