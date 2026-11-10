/* ============================================================================
 * pdecrypt.c - the Standard security handler's per-byte side
 * (PAGEDESK-SPEC 20.22.5). #included by pdeng.c after pdestrm.c.
 *
 * What lives here: parsing /Encrypt (PDV_ENG_CRYPTINFO), keeping the file key
 * (PDV_ENG_SETKEY), the per-object key, decryption of a stream's raw bytes
 * (cr_read: the hook in pdestrm.c's raw_get, st_decode and v_strm_read) and of
 * a string lexed from the file (cr_string: the hook in ob_value), the writer's
 * explicit-key verb (PDV_ENG_CRYPT), and the hooks eng calls: cr_open(),
 * cr_raw_plain(). What does NOT live here: the password, the key derivation,
 * SHA-2 - that is PDXK (pdxk.c).
 *
 * The cipher bodies are assembly (pdcmd5.inc, pdcrc4.inc, pdcaes.inc:
 * pdeng.asm includes them); on the host pdctwin.c answers the same calls.
 * ==========================================================================*/
#include "pdcrypto.h"
#ifdef PD_HOST
#include "pdctwin.c"           /* the host twins of the assembly bodies */
#endif

#ifdef CR_DEBUG
#include <stdio.h>
#define CRDBG(...) fprintf(stderr, __VA_ARGS__)
#else
#define CRDBG(...) ((void)0)
#endif
#define PDE_RC4N 3              /* RC4 streams open at once (their state is 258 bytes each) */

static pdw   cr_on;             /* a file key is set                            */
static pdb   cr_key[32];
static pdw   cr_klen;           /* 5..16, or 32 for AESV3                        */
static pdw   cr_stm, cr_str;    /* PDCM_* for streams and strings               */
static pdw   cr_meta;           /* /EncryptMetadata                             */
static pdb   cr_rk[240];        /* the AES schedule in use                       */
static pdw   pl_ok;             /* the borrowed ctx of cr_raw_plain stands at pl_n/pl_g/pl_pos */
static pdw   cr_rkn, cr_rkg;    /* ...whose it is: an object (0 none), or 0xFFFF/0xFFFF the file's key */
static pdb   cr_rc[PDE_RC4N + 2][258];  /* RC4 states: open streams, the object-stream window's, a string's */
#define CR_RCWIN PDE_RC4N       /* the window's: it outlives the pushes (20.22.3.3) */
#define CR_RCSTR (PDE_RC4N + 1) /* a string's, while a stream may be open (the engine has no tail buffer) */
static pdw   cr_win;            /* os_rewind's call: the ctx is the window's (20.22.3.3) */

#define CRI ((struct pdxb_crypt *)ctxs)        /* SETKEY uses the idle ctxs as its buffer */

/* the per-object key: pdc_ok, and its length (7.6.3 algorithm 1) */
static pdw cr_objkey(pdw num, pdw gen, pdw aes)
{
    pdw kl = (pdw)(cr_klen + 5);
    pdc_okey(cr_key, cr_klen, num, gen, aes);
    return kl > 16 ? 16 : kl;
}

/* the schedule for (method, object): AESV3 is the file key's, once */
static void cr_sched(pdw method, pdw num, pdw gen)
{
    if (method == PDCM_AESV3) { num = 0xFFFF; gen = 0xFFFF; }
    if (cr_rkn == num && cr_rkg == gen) return;
    if (method == PDCM_AESV3) { pdc_aes_ks(cr_rk, cr_key, 8); pdc_aes_dks(cr_rk, 14); }
    else { cr_objkey(num, gen, 1); pdc_aes_ks(cr_rk, pdc_ok, 4); pdc_aes_dks(cr_rk, 10); }       /* (cr_rk is only ever decrypted with) */
    cr_rkn = num; cr_rkg = gen;
}

/* Called by st_push as the context is made (nctx is its index): the caller left
 * the stream's object in cr_sn/cr_sg (0 = none: an xref stream, or a caller that
 * did not say). PDE_LIMIT: an RC4 stream deeper than PDE_RC4N. */
static pdw cr_ctx_open(struct sctx *c)
{
    pdw n;
    c->cmode = 0; c->civ = 0; c->cn = cr_sn; c->cg = cr_sg;
    cr_rkn = 0; pl_ok = 0;                            /* (a ctx pushed over cr_raw_plain's borrowed one) */
    n = (pdw)(cr_on && cr_sn && !cr_skip && cr_stm >= PDCM_RC4 && cr_stm <= PDCM_AESV3);
    cr_sn = 0; cr_sg = 0; cr_skip = 0;
    if (!n) return PDE_OK;
    c->cmode = cr_stm;
    c->crs = c->cv;
    if (cr_stm == PDCM_RC4) {
        if (!cr_win && nctx >= PDE_RC4N) return PDE_LIMIT;
        c->crs = cr_rc[cr_win ? CR_RCWIN : nctx];
        n = cr_objkey(c->cn, c->cg, 0);
        pdc_rc4_ks(c->crs, pdc_ok, n);
    }
    return PDE_OK;
}

/* The decrypted bytes of the raw stream of c, up to want (at most 256), into buf;
 * 0 at the end. An AES stream is IV + blocks; the padding is stripped from the
 * last block, which the stream's length identifies, so nothing is held back. A
 * short read of the file is lx_err, as the plain paths have it. */
static pdw cr_read(struct sctx *c, pdb *buf, pdw want)
{
    pdw n;
    if (c->rrem.hi == 0 && c->rrem.lo < want) want = c->rrem.lo;
    if (c->cmode == PDCM_RC4) {
        n = fl_read(&c->rpos, buf, want);
        u32_sub16(&c->rrem, n);
        if (n < want) lx_err = eof_err();
        pdc_rc4(c->crs, buf, n);
        return n;
    }
    if (!c->civ) {                                    /* the IV */
        if (c->rrem.hi == 0 && c->rrem.lo < 32) goto end;
        if (fl_read(&c->rpos, c->cv, 16) < 16) { lx_err = eof_err(); return 0; }
        u32_sub16(&c->rrem, 16);
        c->civ = 1;
        if (c->rrem.hi == 0 && c->rrem.lo < want) want = c->rrem.lo;
    }
    want = (pdw)(want & 0xFFF0);
    if (!want) goto end;
    n = fl_read(&c->rpos, buf, want);
    u32_sub16(&c->rrem, n);
    if (n < want) { lx_err = eof_err(); n = (pdw)(n & 0xFFF0); }
    if (!n) return 0;
    cr_sched(cr_stm, c->cn, c->cg);
    pdc_cbc_dec(cr_rk, cr_stm == PDCM_AESV3 ? 14 : 10, c->cv, buf, (pdw)(n >> 4));
    if (c->rrem.hi == 0 && c->rrem.lo < 16) {         /* the last block: strip the padding */
        u32_zero(&c->rrem);
        want = buf[n - 1];
        if (want >= 1 && want <= 16) n = (pdw)(n - want);
    }
    return n;
end:
    u32_zero(&c->rrem);
    return 0;
}

/* ---- strings: ob_value's PDT_STR, once ov holds the first piece --------------- */
static void cr_string(void)
{
    pdw n, i, p;
    if (!cr_on || cr_str == PDCM_NONE || cr_str == PDCM_IDENTITY) return;
    if (cr_curn == 0 || (tr_enc_k == 1 && cr_curn == tr_enc_n)) return;   /* the trailer, a member, the /Encrypt dictionary */
    n = ov.len < PDE_VT - 1 ? ov.len : PDE_VT - 1;
    if (cr_str == PDCM_RC4) {
        p = cr_objkey(cr_curn, cr_curg, 0);
        pdc_rc4_ks(cr_rc[CR_RCSTR], pdc_ok, p);
        pdc_rc4(cr_rc[CR_RCSTR], ov.text, n);
        return;
    }
    if (n < 32) { ov.len = 0; ov.text[0] = 0; return; }    /* an IV and nothing, or less */
    cr_sched(cr_str, cr_curn, cr_curg);
    if (cr_str != PDCM_AESV3) cr_rkn = 0;
    p = (pdw)((n - 16) & 0xFFF0);
    pdc_cbc_dec(cr_rk, cr_str == PDCM_AESV3 ? 14 : 10, ov.text, ov.text + 16, (pdw)(p >> 4));
    for (i = 0; i < p; i++) ov.text[i] = ov.text[i + 16];
    if (ov.len < PDE_VT - 1) {                             /* the whole string: strip the padding */
        n = ov.text[p - 1];
        if (n >= 1 && n <= 16) p = (pdw)(p - n);
    } else p = (pdw)(ov.len - 16);
    ov.len = p;
    ov.text[p < PDE_VT - 1 ? p : PDE_VT - 1] = 0;
}

/* ---- PDV_ENG_CRYPTINFO: what SETKEY set, and the whole /ID[0] ----------------------------
 * pdxb_crypt out: `state` 1 when a key is set, then `key`, `key_len`, `auth`, `stm`, `str`,
 * `eff`, `encmeta`; `id0`, `id0_len` always (R2 to R4 hash it into the key). Where the dictionary
 * is comes from TRAILER (encrypt_num/gen, PDTR_ENCDIRECT); PDXK reads it with DICT_GET. Both are
 * allowed from the moment OPEN has read the cross-reference (20.22.5). */
static pdw cr_auth, cr_eff;     /* PDCA_* SETKEY was told; the embedded-file method  */
static pdw cr_info(void)
{
    struct pdxb_crypt *x = CRI;
    pdw i;
    if (nctx != 0) return PDE_RANGE;
    for (i = 0; i < sizeof(struct pdxb_crypt); i++) ((pdb *)x)[i] = 0;
    x->state = cr_on; x->auth = cr_auth; x->key_len = cr_klen; x->stm = cr_stm; x->str = cr_str; x->eff = cr_eff; x->encmeta = cr_meta;
    for (i = 0; i < 32; i++) { x->key[i] = cr_key[i]; x->id0[i] = tr_id[i]; }
    x->id0_len = tr_idlen < 32 ? tr_idlen : 32;
    pd_fcopy_out(x, g_xb, 0, sizeof(struct pdxb_crypt));
    for (i = 0; i < sizeof(struct pdxb_crypt); i++) ((pdb *)x)[i] = 0;     /* (the key was in the idle ctxs) */
    return PDE_OK;
}

/* ---- PDV_ENG_SETKEY ------------------------------------------------------------------ */
static pdw cr_setkey(void)
{
    struct pdxb_crypt *x = CRI;
    pdw i;
    if (nctx != 0) return PDE_RANGE;
    pd_fcopy_in(g_xb, 0, x, sizeof(struct pdxb_crypt));
    if (!((x->key_len >= 5 && x->key_len <= 16) || x->key_len == 32) || x->stm > PDCM_IDENTITY || x->str > PDCM_IDENTITY) return PDE_RANGE;
    for (i = 0; i < 32; i++) cr_key[i] = x->key[i];
    cr_klen = x->key_len; cr_stm = x->stm; cr_str = x->str; cr_meta = x->encmeta; cr_auth = x->auth; cr_eff = x->eff;
    cr_rkn = 0;
    cr_on = 1;
    for (i = 0; i < sizeof(struct pdxb_crypt); i++) ((pdb *)x)[i] = 0;     /* the key was in the XB's copy */
    pd_fcopy_out(x, g_xb, 0, sizeof(struct pdxb_crypt));
    return PDE_OK;
}

static void cr_reset(void)
{
    pdw i;
    cr_on = 0; cr_sn = 0; cr_sg = 0; cr_skip = 0; cr_rkn = 0; cr_curn = 0;
    for (i = 0; i < 32; i++) cr_key[i] = 0;
    for (i = 0; i < 240; i++) cr_rk[i] = 0;
}

/* eng's OPEN, state OPS_CRYPT: PDE_PASSWORD until a key is set */
static pdw cr_open(void)
{
    if (tr_enc_k == 0) return PDE_OK;
    if (!cr_on) return PDE_PASSWORD;
    doc_flags |= PDO_ENCRYPTED;
    return PDE_OK;
}

/* ---- RAW_COPY with mode PDRC_PLAIN on an encrypted document (the lead's 00:00 decision, option 2) ----
 * eng calls this for an object in no object stream, after ob_extent (sp_kind, sp_off, sp_dstart, sp_len are
 * the object's) and takes any answer but PDE_NOTFOUND as the whole of the verb's. What is returned:
 *   - the dictionary's STRINGS STAY CIPHERTEXT, and rw->mode gains PDRC_STRCIPHER: the object's strings are
 *     encrypted under (objnum, gen) and the caller decrypts them (PDWRT holds the shared source);
 *   - a stream whose data this document encrypts comes out as: the file's bytes up to the /Length value,
 *     " <plain length>", the file's bytes to the end of "stream" and its EOL, the DECRYPTED data (still in
 *     its filters), "\nendstream\nendobj\n". Anything else (a plain dictionary or array, an xref stream, a
 *     /Crypt /Identity stream, an unencrypted /Metadata) is the file's bytes: PDE_NOTFOUND.
 * Sequential reads cost nothing extra; a `done` that does not follow the last call restarts the stream's
 * cipher and skips (RC4 cannot seek). The data runs through a ctx borrowed above nctx. */
static struct pd_u32 pl_a, pl_b, pl_dl, pl_pos;
static pdw pl_n, pl_g, pl_la, pl_lb;
static pdb pl_dig[14];
static pdb pl_tb[32];
static const pdb pl_tail[18] = { '\n','e','n','d','s','t','r','e','a','m','\n','e','n','d','o','b','j','\n' };

static pdw pl_open(struct sctx *c, pdw num, pdw gen)       /* the borrowed ctx, at the data's start */
{
    c->lx.buf = c->ib; c->lx.n = 0; c->lx.i = 0; c->nflt = 0;
    u32_cp(&c->rpos, &sp_dstart); u32_cp(&c->rrem, &sp_len);
    cr_sn = num; cr_sg = gen; cr_skip = 0;
    return cr_ctx_open(c);
}

static pdw cr_raw_plain(struct pdxb_rawcopy *rw)
{
    struct sctx *c = &ctxs[nctx];
    pdw rc, num = rw->objnum, gen = rw->gen, want, got = 0, k, n, d, head;
    if (cr_str >= PDCM_RC4 && cr_str <= PDCM_AESV3 && !(tr_enc_k == 1 && num == tr_enc_n)) rw->mode |= PDRC_STRCIPHER;
    if (sp_kind != PDK_STRM || !cr_on || cr_stm < PDCM_RC4 || cr_stm > PDCM_AESV3) return PDE_NOTFOUND;
    rc = ob_filters1(num);                                   /* sp_crypt */
    if (rc) return rc;
    if (cr_skipchk()) return PDE_NOTFOUND;
    if (nctx >= PDE_MAXCTX) return PDE_LIMIT;
    /* where /Length's value is, and what the data comes to */
    L = &fobj;
    lx_seek(&ds_pos);
    rc = ob_dict_find("Length");
    if (rc) return rc == PDE_NOTFOUND ? PDE_SYNTAX : rc;
    lx_tell(&pl_a);
    rc = ob_value();
    if (rc) return rc;
    lx_tell(&pl_b);
    u32_cp(&pl_dl, &sp_len);
    if (cr_stm != PDCM_RC4) {                                /* AES: IV + blocks, less the last block's padding */
        pl_dl.lo &= 0xFFF0;
        if (pl_dl.hi == 0 && pl_dl.lo < 32) u32_zero(&pl_dl);
        else {
            pdm_u32_add(&s_c, &sp_dstart, &pl_dl);
            u32_sub16(&s_c, 32);
            if (fl_read(&s_c, pl_tb, 32) < 32) return pde_ioerr ? PDE_IO : PDE_TRUNC;
            cr_sched(cr_stm, num, gen);
            pdc_cbc_dec(cr_rk, cr_stm == PDCM_AESV3 ? 14 : 10, pl_tb, pl_tb + 16, 1);
            n = (pdw)(pl_tb[31] <= 16 && pl_tb[31] ? pl_tb[31] + 16 : 16);
            u32_sub16(&pl_dl, n);
        }
    }
    pdm_u32_todec((char *)pl_dig + 1, &pl_dl, 0);
    pl_dig[0] = ' ';
    for (pl_lb = 1; pl_dig[pl_lb]; pl_lb++) ;
    pl_la = (pdw)(pl_a.lo - sp_off.lo);                       /* (the header is far under 64 KiB) */
    head = (pdw)(pl_la + pl_lb + (pdw)(sp_dstart.lo - pl_b.lo));
    want = rw->dmax; if (want > 0xFFF0) want = 0xFFF0;
    if (rw->doff != 0 && want > (pdw)(0 - rw->doff)) return PDE_RANGE;
    while (got < want) {
        k = (pdw)(want - got);
        d = rw->done.lo;
        if (rw->done.hi == 0 && d < head) {                   /* before the data: the file's bytes, and the new length */
            if (d < pl_la) { if (k > (pdw)(pl_la - d)) k = (pdw)(pl_la - d); pdm_u32_add(&s_c, &sp_off, &rw->done); }
            else if (d < (pdw)(pl_la + pl_lb)) {
                if (k > (pdw)(pl_la + pl_lb - d)) k = (pdw)(pl_la + pl_lb - d);
                if (!pd_fcopy_out(pl_dig + (d - pl_la), rw->dseg, (pdw)(rw->doff + got), k)) return PDE_RANGE;
                goto step;
            } else { if (k > (pdw)(head - d)) k = (pdw)(head - d); u32_cp(&s_c, &pl_b); pdm_u32_add16(&s_c, (pdw)(d - pl_la - pl_lb)); }
            if (fl_read_far(&s_c, rw->dseg, (pdw)(rw->doff + got), k) < k) return pde_ioerr ? PDE_IO : PDE_TRUNC;
            goto step;
        }
        pdm_u32_set(&s_c, 0, head); pdm_u32_sub(&s_c, &rw->done, &s_c);     /* the offset in the plain data and the tail */
        if (pdm_u32_cmp(&s_c, &pl_dl) >= 0) {                 /* the tail */
            d = (pdw)(s_c.lo - pl_dl.lo);
            if (d >= 18) break;                               /* the end: got < dmax (0 when done is at the end) */
            if (k > (pdw)(18 - d)) k = (pdw)(18 - d);
            if (!pd_fcopy_out(pl_tail + d, rw->dseg, (pdw)(rw->doff + got), k)) return PDE_RANGE;
            goto step;
        }
        /* the data, sequential from 0 (a restart is done == the data's start); the plaintext comes in blocks
         * into the borrowed ctx's own ib and goes out in whatever pieces the caller asks for */
        if (s_c.hi == 0 && s_c.lo == 0 && (!pl_ok || pl_n != num || pl_g != gen || pl_pos.lo || pl_pos.hi)) {
            rc = pl_open(c, num, gen);
            if (rc) return rc;
            pl_n = num; pl_g = gen; pl_ok = 1; u32_zero(&pl_pos);
        }
        if (!pl_ok || pl_n != num || pl_g != gen || pdm_u32_cmp(&s_c, &pl_pos)) return PDE_RANGE;     /* not sequential */
        if (c->lx.i >= c->lx.n) {
            n = cr_read(c, c->ib, 256);
            if (!n) return lx_err ? lx_err : PDE_TRUNC;
            c->lx.n = n; c->lx.i = 0;
        }
        n = (pdw)(c->lx.n - c->lx.i);
        if (k > n) k = n;
        if (!pd_fcopy_out(c->ib + c->lx.i, rw->dseg, (pdw)(rw->doff + got), k)) return PDE_RANGE;
        c->lx.i = (pdw)(c->lx.i + k);
        pdm_u32_add16(&pl_pos, k);
step:
        pdm_u32_add16(&rw->done, k);
        got = (pdw)(got + k);
    }
    rw->got = got;
    pd_fcopy_out(rw, g_xb, 0, sizeof(struct pdxb_rawcopy));
    return PDE_OK;
}

/* ---- a stream the key does not apply to (7.6.5): the stream dictionary is at ds_pos ---- */
static pdw cr_skipchk(void)
{
    pdw rc, hops;
    if (!cr_on) return 0;
    L = &fobj;
    lx_seek(&ds_pos);
    rc = ob_dict_find("Type");
    if (!rc && ob_value() == PDE_OK && ov.kind == PDK_NAME) {
        if (str_eq(ov.text, "XRef")) return 1;
        if (str_eq(ov.text, "Metadata") && !cr_meta) return 1;
    }
    if (!sp_crypt) return 0;
    L = &fobj;
    lx_seek(&ds_pos);
    rc = ob_dict_find("DecodeParms");
    if (rc) return 1;                                   /* /Crypt with no parameters: /Identity */
    rc = ob_value();
    for (hops = 0; !rc && ov.kind == PDK_ARR && hops < 8; hops++) {     /* the first dictionary */
        rc = ob_value();
        if (ov.kind == PDK_DICT || ov.kind == PDK_CLOSE) break;
    }
    if (rc || ov.kind != PDK_DICT) return 1;
    rc = ob_dict_find("Name");
    if (!rc) rc = ob_value();
    if (rc || ov.kind != PDK_NAME) return 1;
    return str_eq(ov.text, "Identity") ? 1 : 0;
}

