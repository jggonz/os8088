/* ============================================================================
 * os8088 - apps/pagedesk/pdxk.c                     PDXK.MOD (PAGEDESK-SPEC 20.22.5)
 *
 * The key occupant: the password side of the Standard security handler.
 * Algorithms 2, 4, 5, 6 and 7 of ISO 32000-1 (R2 to R4, MD5 and RC4) and 2.A,
 * 2.B of ISO 32000-2 (R5 and R6, SHA-2 and AES), as AUTHENTICATION: a user or an
 * owner password goes in, the file key comes out. Everything that runs per
 * byte of the file is in PDENG (pdecrypt.c); this module runs at open only.
 *
 * One translation unit (SPEC.md 73.1): compiled for the target by cc8086
 * behind pdxk.asm's door, and for the host with -DPD_HOST, where hosttest/
 * pdk_test.c #includes THIS file so it can reach the statics, and the crypto
 * blocks are pdctwin.c's.
 *
 * Verbs: PDV_X_INIT 0xC0, PDV_KEY_AUTH 0xC1 (resumable: PDE_BUSY), PDV_KEY_COST
 * 0xC2. Registers and the door: pdxk.asm. R6 is a coroutine over the streaming
 * hash of pdcsum.c; one call is a slice of about 40 units (an AES block is 1, a
 * SHA-256 block 4, a SHA-512 block 14: about half a second on an 8088).
 * ==========================================================================*/
#ifdef PD_HOST
#include "pdabi.h"
#include "pdmath.h"
#else
#include "os88.h"
#include "pdabi.h"
#include "pdmath.h"
#endif
#include "pdcrypto.h"
#ifdef PD_HOST
#include "pdctwin.c"           /* the host twins of the assembly bodies */
#endif

pdw pdk_engcf;                          /* CF of the last PDENG call          */
pdw pdk_cf;                             /* CF of this door's answer           */
pdw pdk_engvec[2];                      /* PDENG's door, offset then segment  */
pdw pdk_engcall(pdw verb, pdw bx, pdw cx, pdw dx); /* AX; asm on the target   */

static pdw g_xb;                        /* the exchange block's segment       */
static pdw g_cpu;                       /* pdxb_init.cpu                      */

#include "pdcsum.c"

static struct pdxb_crypt cr;            /* the XB's struct, a working copy    */

static const pdb PADSTR[32] = {
    0x28, 0xBF, 0x4E, 0x5E, 0x4E, 0x75, 0x8A, 0x41, 0x64, 0x00, 0x4E, 0x56, 0xFF, 0xFA, 0x01, 0x08,
    0x2E, 0x2E, 0x00, 0xB6, 0xD0, 0x68, 0x3E, 0x80, 0x2F, 0x0C, 0xA9, 0xFE, 0x64, 0x53, 0x69, 0x7A };

/* ---- R2 to R4: MD5 and RC4 -------------------------------------------------- */
static pdb a2_buf[128];                 /* pad, O, P, ID, flag: up to 104     */
static pdb a2_dig[16];
static pdb a2_pad[32];                  /* the password, padded               */
static pdb a2_key[16];
static pdb a2_n;
static pdb a2_h[16];
static pdb a2_rc[258];
static pdb a2_k2[16];
static pdb a2_u[32];                    /* RC4 scratch for the owner path     */

static void a2_pwpad(const pdb *pw, pdw n)
{
    pdw i;
    if (n > 32) n = 32;
    for (i = 0; i < n; i++) a2_pad[i] = pw[i];
    for (; i < 32; i++) a2_pad[i] = PADSTR[i - n];
}

static void a2_md5(const pdb *p, pdw n, pdb *out)
{
    hs_init(HS_MD5); hs_feed(p, n); hs_final(out);
}

static pdw a2_keylen(void)
{
    pdw n = (cr.r == 2) ? 5 : (pdw)(cr.length >> 3);
    if (n < 5) n = 5;
    if (n > 16) n = 16;
    return n;
}

/* algorithm 2: the file key from the PADDED password in a2_pad -> a2_key */
static void a2_filekey(void)
{
    pdw i, n = 0, k;
    for (i = 0; i < 32; i++) a2_buf[n++] = a2_pad[i];
    for (i = 0; i < 32; i++) a2_buf[n++] = cr.o[i];
    a2_buf[n++] = (pdb)cr.p_lo; a2_buf[n++] = (pdb)(cr.p_lo >> 8);
    a2_buf[n++] = (pdb)cr.p_hi; a2_buf[n++] = (pdb)(cr.p_hi >> 8);
    k = cr.id0_len; if (k > 32) k = 32;
    for (i = 0; i < k; i++) a2_buf[n++] = cr.id0[i];
    if (cr.r >= 4 && !cr.encmeta) { a2_buf[n++] = 0xFF; a2_buf[n++] = 0xFF; a2_buf[n++] = 0xFF; a2_buf[n++] = 0xFF; }
    a2_md5(a2_buf, n, a2_dig);
    k = a2_keylen();
    if (cr.r >= 3) for (i = 0; i < 50; i++) { a2_md5(a2_dig, k, a2_h); for (n = 0; n < 16; n++) a2_dig[n] = a2_h[n]; }
    for (i = 0; i < 16; i++) a2_key[i] = a2_dig[i];
    a2_n = (pdb)k;
}

/* algorithms 4 and 5: does the key in a2_key make /U? 1 = yes */
static pdw a2_check(void)
{
    pdw i, j, n = a2_n, ok = 1;
    if (cr.r == 2) {
        for (i = 0; i < 32; i++) a2_u[i] = PADSTR[i];
        pdc_rc4_ks(a2_rc, a2_key, n);
        pdc_rc4(a2_rc, a2_u, 32);
        for (i = 0; i < 32; i++) if (a2_u[i] != cr.u[i]) ok = 0;
        return ok;
    }
    for (i = 0; i < 32; i++) a2_buf[i] = PADSTR[i];
    n = 32;
    j = cr.id0_len; if (j > 32) j = 32;
    for (i = 0; i < j; i++) a2_buf[n++] = cr.id0[i];
    a2_md5(a2_buf, n, a2_h);
    n = a2_n;
    pdc_rc4_ks(a2_rc, a2_key, n);
    pdc_rc4(a2_rc, a2_h, 16);
    for (j = 1; j <= 19; j++) {
        for (i = 0; i < n; i++) a2_k2[i] = (pdb)(a2_key[i] ^ j);
        pdc_rc4_ks(a2_rc, a2_k2, n);
        pdc_rc4(a2_rc, a2_h, 16);
    }
    for (i = 0; i < 16; i++) if (a2_h[i] != cr.u[i]) ok = 0;
    return ok;
}

/* the user path: password -> padded -> key -> check */
static pdw a2_user(void)
{
    a2_filekey();
    return a2_check();
}

/* algorithm 7: the owner password -> the user's padded password in a2_pad */
static void a2_ownerpad(void)
{
    pdw i, j, n;
    a2_md5(a2_pad, 32, a2_h);
    if (cr.r >= 3) for (i = 0; i < 50; i++) { a2_md5(a2_h, 16, a2_dig); for (n = 0; n < 16; n++) a2_h[n] = a2_dig[n]; }
    n = a2_keylen();
    for (i = 0; i < 32; i++) a2_u[i] = cr.o[i];
    if (cr.r == 2) {
        pdc_rc4_ks(a2_rc, a2_h, n);
        pdc_rc4(a2_rc, a2_u, 32);
    } else {
        for (j = 20; j > 0; j--) {
            for (i = 0; i < n; i++) a2_k2[i] = (pdb)(a2_h[i] ^ (j - 1));
            pdc_rc4_ks(a2_rc, a2_k2, n);
            pdc_rc4(a2_rc, a2_u, 32);
        }
    }
    for (i = 0; i < 32; i++) a2_pad[i] = a2_u[i];
}

/* ---- R5 and R6 ------------------------------------------------------------- */
#define K6_BUDGET 40            /* units a slice may spend                    */
#define K6_MAXPW  127

static pdb k6_pw[K6_MAXPW + 1];
static pdw k6_pwl;
static pdb k6_udata[48];
static pdw k6_ul;
static pdb k6_in[200];          /* pw + salt + udata for the first hash       */
static pdb k6_k[64];            /* K                                          */
static pdw k6_kl;               /* 32, 48 or 64                               */
static pdb k6_u0[240];          /* pw + K + udata                             */
static pdw k6_l;                /* its length                                 */
static pdb k6_rk[240];
static pdb k6_iv[16];
static pdb k6_blk[16];
static pdw k6_round;            /* i                                          */
static pdw k6_ix;               /* index into u0                              */
static pdw k6_rep;              /* blocks produced in this round (of 4 * L)   */
static pdw k6_nblk;             /* 4 * L: blocks in a round                   */
static pdw k6_sum;
static pdw k6_last;             /* E's last byte so far                       */
static pdw k6_phase;            /* 0 idle, 1 in a round, 2 done               */
static pdw k6_r5;               /* 1: R5, a plain SHA-256                     */
static pdw k6_units;

/* start 2.B (or 2.A for R5): hash(pw || salt8 || udata) */
static void k6_begin(const pdb *salt, const pdb *udata, pdw ul)
{
    pdw i, n = 0;
    k6_ul = ul;
    for (i = 0; i < ul; i++) k6_udata[i] = udata[i];
    for (i = 0; i < k6_pwl; i++) k6_in[n++] = k6_pw[i];
    for (i = 0; i < 8; i++) k6_in[n++] = salt[i];
    for (i = 0; i < ul; i++) k6_in[n++] = udata[i];
    hs_init(HS_SHA256); hs_feed(k6_in, n); hs_final(k6_k);
    k6_kl = 32; k6_round = 0;
    k6_phase = k6_r5 ? 2 : 0;       /* R5 is done; R6 starts its rounds */
    k6_units = 0;
}

/* set up round k6_round: K1 = (pw K udata) x 64, AES-128-CBC under K */
static void k6_round_begin(void)
{
    pdw i, n = 0;
    for (i = 0; i < k6_pwl; i++) k6_u0[n++] = k6_pw[i];
    for (i = 0; i < k6_kl; i++) k6_u0[n++] = k6_k[i];
    for (i = 0; i < k6_ul; i++) k6_u0[n++] = k6_udata[i];
    k6_l = n;
    k6_nblk = (pdw)(4 * n);
    pdc_aes_ks(k6_rk, k6_k, 4);
    for (i = 0; i < 16; i++) k6_iv[i] = k6_k[16 + i];
    k6_ix = 0; k6_rep = 0; k6_sum = 0; k6_last = 0;
    k6_phase = 1;
}

/* one block of the round; returns the units it cost */
static pdw k6_step(void)
{
    pdw i, cost = 1, mod;
    for (i = 0; i < 16; i++) {
        k6_blk[i] = k6_u0[k6_ix];
        if (++k6_ix == k6_l) k6_ix = 0;
    }
    pdc_cbc_enc(k6_rk, 10, k6_iv, k6_blk, 1);
    if (k6_rep == 0) {
        for (i = 0; i < 16; i++) k6_sum = (pdw)(k6_sum + k6_blk[i]);
        mod = k6_sum % 3;
        hs_init(mod == 0 ? HS_SHA256 : (mod == 1 ? HS_SHA384 : HS_SHA512));
    }
    k6_last = k6_blk[15];
    {   pdw before = hs_n, bsz = hs_bsz();
        hs_feed(k6_blk, 16);
        if ((pdw)(before + 16) >= bsz) cost += (hs_kind == HS_SHA256) ? 4 : 14;
    }
    k6_rep++;
    if (k6_rep == k6_nblk) {
        hs_final(k6_k);
        k6_kl = (hs_kind == HS_SHA256) ? 32 : (hs_kind == HS_SHA384 ? 48 : 64);
        k6_round++;
        if (k6_round >= 64 && k6_last <= (pdw)(k6_round - 32)) k6_phase = 2;
        else k6_phase = 0;
    }
    return cost;
}

/* run for about one slice. 1 when the hash is finished (k6_k[0..31]) */
static pdw k6_run(void)
{
    pdw spent = 0;
    if (k6_phase == 2) return 1;
    while (spent < K6_BUDGET) {
        if (k6_phase == 0) k6_round_begin();
        spent = (pdw)(spent + k6_step());
        if (k6_phase == 2) return 1;
    }
    return 0;
}

static pdw k6_progress(void)
{
    pdw p = (pdw)((k6_round << 4) - (k6_round << 1)); /* x14: about 71 rounds, 1000/71 */
    if (k6_phase != 2 && p > 990) p = 990;
    return k6_phase == 2 ? 1000 : p;
}

static pdb k6_cmp[32];          /* UE/OE in, the file key out                 */
static pdb k6_ek[32];
static pdb k6_iv0[16];

static pdw k6_eq32(const pdb *a, const pdb *b)
{
    pdw i, d = 0;
    for (i = 0; i < 32; i++) d |= (pdw)(a[i] ^ b[i]);
    return d == 0;
}

/* the file key: AES-256-CBC, no padding, zero IV, under the intermediate key k6_k */
static void k6_unwrap(const pdb *enc)
{
    pdw i;
    for (i = 0; i < 32; i++) { k6_cmp[i] = enc[i]; k6_ek[i] = k6_k[i]; }
    for (i = 0; i < 16; i++) k6_iv0[i] = 0;
    pdc_aes_ks(k6_rk, k6_ek, 8);
    pdc_cbc_dec(k6_rk, 14, k6_iv0, k6_cmp, 2);
}

static pdb kx_empty[1];
static pdw kx_stage;            /* 0 not started; 1..4 the R5/R6 sequence     */
static pdw kx_live;             /* an attempt is in progress in this image    */

static void kx_wipe(void)
{
    pdw i;
    for (i = 0; i <= K6_MAXPW; i++) k6_pw[i] = 0;
    for (i = 0; i < 240; i++) k6_rk[i] = 0;
    for (i = 0; i < 32; i++) { k6_ek[i] = 0; k6_k[i] = 0; }
    for (i = 0; i < 32; i++) a2_pad[i] = 0;
    kx_live = 0; kx_stage = 0; k6_phase = 0;
}

/* R5 and R6. PDE_OK with cr.key set, PDE_BUSY, or PDE_PASSWORD */
static pdw kx_r56(void)
{
    pdw i;
    for (;;) {
        switch (kx_stage) {
        case 0:
            k6_r5 = (cr.r == 5);
            k6_pwl = cr.pw_len; if (k6_pwl > K6_MAXPW) k6_pwl = K6_MAXPW;
            for (i = 0; i < k6_pwl; i++) k6_pw[i] = cr.pw[i];
            k6_begin(cr.u + 32, kx_empty, 0);
            kx_stage = 1;
            break;
        case 1:
            if (!k6_run()) { cr.progress = k6_progress(); return PDE_BUSY; }
            if (k6_eq32(k6_k, cr.u)) { k6_begin(cr.u + 40, kx_empty, 0); kx_stage = 2; }
            else if (cr.r >= 6 && cr.pw_len == 0) return PDE_PASSWORD;   /* the silent attempt tries the user key only: the owner's costs another 1.7 runs (20.22.5.1) */
            else { k6_begin(cr.o + 32, cr.u, 48); kx_stage = 3; }
            break;
        case 2:
            if (!k6_run()) { cr.progress = k6_progress(); return PDE_BUSY; }
            k6_unwrap(cr.ue);
            cr.auth = PDCA_USER;
            goto got;
        case 3:
            if (!k6_run()) { cr.progress = k6_progress(); return PDE_BUSY; }
            if (!k6_eq32(k6_k, cr.o)) return PDE_PASSWORD;
            k6_begin(cr.o + 40, cr.u, 48); kx_stage = 4;
            break;
        default:
            if (!k6_run()) { cr.progress = k6_progress(); return PDE_BUSY; }
            k6_unwrap(cr.oe);
            cr.auth = PDCA_OWNER;
            goto got;
        }
    }
got:
    for (i = 0; i < 32; i++) cr.key[i] = k6_cmp[i];
    cr.key_len = 32;
    cr.progress = 1000;
    return PDE_OK;
}

/* R2 to R4: the user password, then the owner's (one call, about a second) */
static pdw kx_r24(void)
{
    pdw i;
    a2_pwpad(cr.pw, cr.pw_len);
    if (a2_user()) { cr.auth = PDCA_USER; goto got; }
    a2_pwpad(cr.pw, cr.pw_len);
    a2_ownerpad();
    if (a2_user()) { cr.auth = PDCA_OWNER; goto got; }
    return PDE_PASSWORD;
got:
    for (i = 0; i < 16; i++) cr.key[i] = a2_key[i];
    cr.key_len = a2_n;
    cr.progress = 1000;
    return PDE_OK;
}

/* ---- PDV_KEY_COST: seconds for ONE run of 2.B on this CPU (20.22.5.1) ------
 * 71 rounds on average, each 4 L AES blocks and 64 L bytes through SHA-256, -384
 * or -512 (L = password + K (32 to 64, 48 on average) + user data). Measured on an
 * 8088 in 20.22.5.1: ms = 127 L a round. Faster classes divide. */
static const pdb cpu_div[4] = { 1, 6, 24, 60 };
static pdw kx_cost(void)
{
    pdw l = (pdw)(cr.pw_len + 48), s, d;
    if (cr.r < 6) return 0;
    if (cr.pw_len > K6_MAXPW) l = K6_MAXPW + 48;
    s = (pdw)((l << 3) + l);               /* seconds on an XT: 71 * 127 L / 1000 = 9.0 L */
    d = cpu_div[g_cpu > 3 ? 3 : g_cpu];
    s = (pdw)(s / d);
    return s ? s : 1;
}

/* ---- the /Encrypt dictionary, read through PDENG's DICT_GET (PAGEDESK-SPEC 20.22.5) -------
 * PDENG says where it is (CRYPTINFO: a reference, or direct in the trailer); every key is one
 * DICT_GET, which is cheap beside what follows. A direct dictionary is reached as the trailer's
 * /Encrypt, two keys deep at most, so a V4 or V5 file whose /Encrypt is direct (nothing writes one)
 * is PDE_ENCRYPT. P is a 16.16 integer there: the signed values every writer uses; a P outside
 * +-32767 is PDE_ENCRYPT for R2 to R4, where it enters the key, and ignored for R5 and R6. */
static struct pdxb_dictget dg;
static struct pdxb_crypt ci;
static struct pdxb_trailer trl;
static pdw dg_n, dg_g;

static void dg_name(char *dst, const char *src)
{
    pdw i;
    for (i = 0; i < 31 && src && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}

static pdw dget(const char *k0, const char *k1, const char *k2)
{
    pdw r, i;
    for (i = 0; i < sizeof dg; i++) ((pdb *)&dg)[i] = 0;
    dg.objnum = dg_n; dg.gen = dg_g;
    if (dg_n) { dg_name(dg.key[0], k0); dg_name(dg.key[1], k1); dg_name(dg.key[2], k2); }
    else {
        if (k2) return PDE_ENCRYPT;
        dg_name(dg.key[0], "Encrypt"); dg_name(dg.key[1], k0); dg_name(dg.key[2], k1);
    }
    pd_fcopy_out(&dg, g_xb, 0, sizeof dg);
    r = pdk_engcall(PDV_ENG_DICT_GET, 0, 0, 0);
    if (pdk_engcf) return r;
    pd_fcopy_in(g_xb, 0, &dg, sizeof dg);
    return PDE_OK;
}

static pdw dg_is(const char *s)
{
    pdw i;
    for (i = 0; s[i]; i++) if (dg.text[i] != s[i]) return 0;
    return dg.text[i] == 0;
}

/* an integer key: PDE_OK with the value in *out, PDE_NOTFOUND absent, else the engine's code */
static pdw dgi(const char *k, pdw *out)
{
    pdw r = dget(k, 0, 0);
    if (r) return r;
    if (dg.kind != PDK_INT || dg.num.hi < 0) return PDE_NOTFOUND;
    *out = (pdw)dg.num.hi;
    return PDE_OK;
}

/* a string key into dst (at most max), its true length into *len */
static pdw dgs(const char *k, pdb *dst, pdw max, pdw *len)
{
    pdw r = dget(k, 0, 0), i, n;
    if (r) return r;
    if (dg.kind != PDK_STR) return PDE_NOTFOUND;
    n = dg.len < max ? dg.len : max;
    if (n > 127) n = 127;
    for (i = 0; i < n; i++) dst[i] = (pdb)dg.text[i];
    *len = n;
    return PDE_OK;
}

static char kx_nm[2][32];       /* /StmF and /StrF                            */
static pdw kx_cflen, kx_m[2];

/* the method of crypt filter kx_nm[w] */
static pdw kx_cfm(pdw w)
{
    pdw r;
    kx_m[w] = PDCM_IDENTITY;
    if (!kx_nm[w][0] || (kx_nm[w][0] == 'I' && kx_nm[w][1] == 'd')) return PDE_OK;
    if (!dg_n) return PDE_ENCRYPT;                      /* a direct dictionary three levels down: not reachable */
    r = dget("CF", kx_nm[w], "CFM");
    if (r) return r == PDE_NOTFOUND ? PDE_ENCRYPT : r;
    if (dg_is("V2")) kx_m[w] = PDCM_RC4;
    else if (dg_is("AESV2")) kx_m[w] = PDCM_AESV2;
    else if (dg_is("AESV3")) kx_m[w] = PDCM_AESV3;
    else if (dg_is("None")) kx_m[w] = PDCM_NONE;
    else return PDE_ENCRYPT;
    r = dget("CF", kx_nm[w], "Length");
    if (!r && dg.kind == PDK_INT && dg.num.hi > 0 && !kx_cflen) kx_cflen = (pdw)dg.num.hi;
    return PDE_OK;
}

/* cr = the file's parameters (cr.pw and cr.pw_len are the caller's and stay) */
static pdw kp_v, kp_r, kp_len, kp_n;           /* (no address of a local: SPEC.md 73) */
static pdw kx_params(void)
{
    pdw r, v, rr, len, i;
    r = pdk_engcall(PDV_ENG_CRYPTINFO, 0, 0, 0);               /* the whole /ID[0] */
    if (pdk_engcf) return r;
    pd_fcopy_in(g_xb, 0, &ci, sizeof ci);
    r = pdk_engcall(PDV_ENG_TRAILER, 0, 0, 0);                 /* where /Encrypt is */
    if (pdk_engcf) return r;
    pd_fcopy_in(g_xb, 0, &trl, sizeof trl);
    if (!trl.encrypt_num && !(trl.tflags & PDTR_ENCDIRECT)) return PDE_RANGE;
    dg_n = trl.encrypt_num; dg_g = trl.encrypt_gen;
    r = dget("Filter", 0, 0);
    if (r || !dg_is("Standard")) return r ? r : PDE_ENCRYPT;
    r = dgi("V", &kp_v);
    if (!r) r = dgi("R", &kp_r);
    if (r) return r == PDE_NOTFOUND ? PDE_ENCRYPT : r;
    v = kp_v; rr = kp_r;
    if ((v != 1 && v != 2 && v != 4 && v != 5) || rr < 2 || rr > 6) return PDE_ENCRYPT;
    cr.v = v; cr.r = rr;
    kp_len = 40;
    r = dgi("Length", &kp_len); len = kp_len;
    if (r && r != PDE_NOTFOUND) return r;
    cr.length = (v == 1) ? 40 : len;
    r = dget("P", 0, 0);
    if (r == PDE_OK && dg.kind == PDK_INT) { cr.p_lo = (pdw)dg.num.hi; cr.p_hi = (dg.num.hi < 0) ? 0xFFFF : 0; }
    else if (rr < 5) return PDE_ENCRYPT;
    r = dgs("O", cr.o, 48, &kp_n); cr.o_len = kp_n;
    if (!r) { r = dgs("U", cr.u, 48, &kp_n); cr.u_len = kp_n; }
    if (r) return r == PDE_NOTFOUND ? PDE_ENCRYPT : r;
    if (rr >= 5) {
        r = dgs("OE", cr.oe, 32, &kp_n);
        if (!r) r = dgs("UE", cr.ue, 32, &kp_n);
        if (!r) r = dgs("Perms", cr.perms, 16, &kp_n);
        if (r) return r == PDE_NOTFOUND ? PDE_ENCRYPT : r;
    }
    cr.encmeta = 1;
    r = dget("EncryptMetadata", 0, 0);
    if (r == PDE_OK && dg.kind == PDK_BOOL) cr.encmeta = (dg.num.hi || dg.num.lo) ? 1 : 0;
    cr.stm = PDCM_RC4; cr.str = PDCM_RC4;
    if (v >= 4) {
        kx_nm[0][0] = 0; kx_nm[1][0] = 0; kx_cflen = 0;
        if (dget("StmF", 0, 0) == PDE_OK && dg.kind == PDK_NAME) for (i = 0; i < 31 && dg.text[i]; i++) { kx_nm[0][i] = dg.text[i]; kx_nm[0][i + 1] = 0; }
        if (dget("StrF", 0, 0) == PDE_OK && dg.kind == PDK_NAME) for (i = 0; i < 31 && dg.text[i]; i++) { kx_nm[1][i] = dg.text[i]; kx_nm[1][i + 1] = 0; }
        r = kx_cfm(0);
        if (!r) r = kx_cfm(1);
        if (r) return r;
        cr.stm = kx_m[0]; cr.str = kx_m[1];
        if (cr.stm == PDCM_AESV2) cr.length = 128;
        else if (cr.stm == PDCM_AESV3) cr.length = 256;
        else if (kx_cflen) cr.length = kx_cflen < 40 ? (pdw)(kx_cflen << 3) : kx_cflen;
    }
    cr.eff = cr.stm;
    cr.id0_len = ci.id0_len;
    for (i = 0; i < 32; i++) cr.id0[i] = ci.id0[i];
    return PDE_OK;
}

/* ---- the verbs ----------------------------------------------------------------- */
static pdb sv_pw[128];
static pdw sv_pwl;

static pdw v_init(pdw bx)
{
    static struct pdxb_init in;
    g_xb = bx;
    pd_fcopy_in(bx, 0, &in, sizeof in);
    pdk_engvec[0] = in.eng_off; pdk_engvec[1] = in.eng_seg;
    g_cpu = in.cpu;
    kx_wipe();
    return PDE_OK;
}

static pdw v_auth(void)
{
    pdw r, i;
    pd_fcopy_in(g_xb, 0, &cr, sizeof cr);
    if (cr.state == 0 || !kx_live) {
        kx_wipe();
        sv_pwl = cr.pw_len; if (sv_pwl > 128) sv_pwl = 128;
        for (i = 0; i < sv_pwl; i++) sv_pw[i] = cr.pw[i];
        if (cr.v == 0) {                      /* the parameters: the /Encrypt dictionary, through PDENG */
            r = kx_params();
            if (r) return r;
        }
        cr.pw_len = sv_pwl;
        for (i = 0; i < sv_pwl; i++) cr.pw[i] = sv_pw[i];
        for (i = 0; i < 128; i++) sv_pw[i] = 0;
        if (cr.r < 2 || cr.r > 6) return PDE_ENCRYPT;
        if (cr.r >= 5 && (cr.u_len < 48 || cr.o_len < 48)) return PDE_ENCRYPT;
        if (cr.r < 5 && (cr.u_len < 32 || cr.o_len < 32)) return PDE_ENCRYPT;
        kx_live = 1;
    }
    r = (cr.r >= 5) ? kx_r56() : kx_r24();
    if (r == PDE_BUSY) cr.state = (pdw)(kx_stage + 1);
    else {
        cr.state = 0;
        if (r != PDE_OK) { cr.auth = PDCA_NONE; cr.key_len = 0; }
        kx_wipe();                            /* the password stays no longer than the attempt */
        for (i = 0; i < 128; i++) cr.pw[i] = 0;
    }
    pd_fcopy_out(&cr, g_xb, 0, sizeof cr);
    return r;
}

static pdw v_params(void)                         /* the file's parameters into the XB, no password tried */
{
    pdw r;
    pd_fcopy_in(g_xb, 0, &cr, sizeof cr);
    r = kx_params();
    if (r) return r;
    pd_fcopy_out(&cr, g_xb, 0, sizeof cr);
    return PDE_OK;
}

static pdw kx_cerr;
static pdw v_cost(void)
{
    pdw r;
    kx_cerr = 0;
    pd_fcopy_in(g_xb, 0, &cr, sizeof cr);
    if (cr.v == 0) {                              /* an empty struct: read the file's parameters first */
        sv_pwl = cr.pw_len;
        r = kx_params();
        if (r) { kx_cerr = 1; return r; }
        cr.pw_len = sv_pwl;
        pd_fcopy_out(&cr, g_xb, 0, sizeof cr);   /* the caller learns r and the methods from this */
    }
    return kx_cost();
}

unsigned pdk_verb(unsigned verb, unsigned bx, unsigned cx, unsigned dx)
{
    pdw r;
    (void)cx; (void)dx;
    switch (verb) {
    case PDV_X_INIT:   r = v_init(bx); break;
    case PDV_KEY_AUTH: r = v_auth(); break;
    case PDV_KEY_COST: r = v_cost(); break;
    case PDV_KEY_PARAMS: r = v_params(); break;
    default:           r = PDE_RANGE; break;
    }
    if (verb == PDV_KEY_COST) pdk_cf = kx_cerr;
    else pdk_cf = (pdw)(r != PDE_OK && r != PDE_BUSY);
    return r;
}
