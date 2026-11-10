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

static pdw   cr_on;             /* a file key is set                            */
static pdb   cr_key[32];
static pdw   cr_klen;           /* 5..16, or 32 for AESV3                        */
static pdw   cr_stm, cr_str;    /* PDCM_* for streams and strings               */
static pdw   cr_meta;           /* /EncryptMetadata                             */
static pdb   cr_rk[240];        /* the AES schedule in use                       */
static pdw   cr_rkn, cr_rkg;    /* ...whose it is: an object (0 none), or 0xFFFF/0xFFFF the file's key */
static pdb   cr_ok[16];         /* a per-object key                              */
static pdb   cr_mb[64];         /* the MD5 block                                 */
static pdw   cr_md[8];          /* the MD5 state                                 */

#define CRI ((struct pdxb_crypt *)ctxs)        /* CRYPTINFO / SETKEY use the idle ctxs as their buffer */

/* ---- the object key (7.6.3 algorithm 1): cr_ok; its length is the answer ------ */
static pdw cr_objkeyk(const pdb *key, pdw klen, pdw num, pdw gen, pdw aes)
{
    pdw i, n = 0, kl;
    for (i = 0; i < 64; i++) cr_mb[i] = 0;
    for (i = 0; i < klen; i++) cr_mb[n++] = key[i];
    cr_mb[n++] = (pdb)num; cr_mb[n++] = (pdb)(num >> 8); cr_mb[n++] = 0;
    cr_mb[n++] = (pdb)gen; cr_mb[n++] = (pdb)(gen >> 8);
    if (aes) { cr_mb[n++] = 0x73; cr_mb[n++] = 0x41; cr_mb[n++] = 0x6C; cr_mb[n++] = 0x54; }
    cr_mb[n] = 0x80;
    cr_mb[56] = (pdb)(n << 3);
    pdc_md5_init(cr_md);
    pdc_md5_block(cr_md, cr_mb);
    for (i = 0; i < 8; i++) { cr_ok[2 * i] = (pdb)cr_md[i]; cr_ok[2 * i + 1] = (pdb)(cr_md[i] >> 8); }
    kl = (pdw)(klen + 5);
    return kl > 16 ? 16 : kl;
}
static pdw cr_objkey(pdw num, pdw gen, pdw aes) { return cr_objkeyk(cr_key, cr_klen, num, gen, aes); }

/* the schedule for (method, object): AESV3 is the file key's, once */
static void cr_sched(pdw method, pdw num, pdw gen)
{
    if (method == PDCM_AESV3) { num = 0xFFFF; gen = 0xFFFF; }
    if (cr_rkn == num && cr_rkg == gen) return;
    if (method == PDCM_AESV3) pdc_aes_ks(cr_rk, cr_key, 8);
    else { cr_objkey(num, gen, 1); pdc_aes_ks(cr_rk, cr_ok, 4); }
    cr_rkn = num; cr_rkg = gen;
}

/* Called by st_push as the context is made: the caller left the stream's
 * object in cr_sn/cr_sg (0 = none: an xref stream, or a caller that did not say). */
static void cr_ctx_open(struct sctx *c)
{
    pdw n;
    c->cmode = 0; c->civ = 0; c->cn = cr_sn; c->cg = cr_sg;
    cr_rkn = 0;
    if (cr_on && cr_sn && !cr_skip && (cr_stm == PDCM_RC4 || cr_stm == PDCM_AESV2 || cr_stm == PDCM_AESV3)) {
        c->cmode = cr_stm;
        if (cr_stm == PDCM_RC4) { n = cr_objkey(cr_sn, cr_sg, 0); pdc_rc4_ks(c->cs, cr_ok, n); }
    }
    cr_sn = 0; cr_sg = 0; cr_skip = 0;
}

/* The decrypted bytes of the raw stream of c, up to want (a multiple of 16, at
 * most 256), into buf; 0 at the end. An AES stream is IV + blocks; the padding
 * is stripped from the last block, which the stream's length identifies, so
 * nothing is held back. A short read of the file is lx_err, as the plain paths
 * have it. */
static pdw cr_read(struct sctx *c, pdb *buf, pdw want)
{
    pdw n, nb;
    if (c->rrem.hi == 0 && c->rrem.lo < want) want = c->rrem.lo;
    if (c->cmode == PDCM_RC4) {
        if (!want) return 0;
        n = fl_read(&c->rpos, buf, want);
        u32_sub16(&c->rrem, n);
        if (n < want) lx_err = eof_err();
        pdc_rc4(c->cs, buf, n);
        return n;
    }
    if (!c->civ) {                                    /* the IV */
        if (c->rrem.hi == 0 && c->rrem.lo < 32) { u32_zero(&c->rrem); return 0; }
        if (fl_read(&c->rpos, c->cs, 16) < 16) { lx_err = eof_err(); return 0; }
        u32_sub16(&c->rrem, 16);
        c->civ = 1;
        if (c->rrem.hi == 0 && c->rrem.lo < want) want = c->rrem.lo;
    }
    want = (pdw)(want & 0xFFF0);
    if (!want) { u32_zero(&c->rrem); return 0; }
    n = fl_read(&c->rpos, buf, want);
    u32_sub16(&c->rrem, n);
    if (n < want) { lx_err = eof_err(); n = (pdw)(n & 0xFFF0); }
    nb = (pdw)(n >> 4);
    if (!nb) return 0;
    cr_sched(cr_stm, c->cn, c->cg);
    pdc_cbc_dec(cr_rk, cr_stm == PDCM_AESV3 ? 14 : 10, c->cs, buf, nb);
    if (c->rrem.hi == 0 && c->rrem.lo < 16) {         /* the last block: strip the padding */
        u32_zero(&c->rrem);
        want = buf[n - 1];
        if (want >= 1 && want <= 16) n = (pdw)(n - want);
    }
    return n;
}

/* ---- strings: ob_value's PDT_STR, once ov holds the first piece --------------- */
static pdw cr_encobj(void)
{
    return cr_curn == 0 || (tr_enc_k == 1 && cr_curn == tr_enc_n);
}
static void cr_string(void)
{
    pdw n, i, nb, p;
    if (!cr_on || cr_str == PDCM_NONE || cr_str == PDCM_IDENTITY || cr_encobj()) return;
    n = ov.len < PDE_VT - 1 ? ov.len : PDE_VT - 1;
    if (cr_str == PDCM_RC4) {
        p = cr_objkey(cr_curn, cr_curg, 0);
        pdc_rc4_ks(tailbuf, cr_ok, p);
        pdc_rc4(tailbuf, ov.text, n);
        return;
    }
    if (n < 32) { ov.len = 0; ov.text[0] = 0; return; }    /* an IV and nothing, or less */
    nb = (pdw)((n - 16) >> 4);
    cr_sched(cr_str, cr_curn, cr_curg);
    if (cr_str != PDCM_AESV3) cr_rkn = 0;
    pdc_cbc_dec(cr_rk, cr_str == PDCM_AESV3 ? 14 : 10, ov.text, ov.text + 16, nb);
    p = (pdw)(nb << 4);
    for (i = 0; i < p; i++) ov.text[i] = ov.text[i + 16];
    if (ov.len < PDE_VT - 1) {                             /* the whole string: strip the padding */
        n = ov.text[p - 1];
        if (n >= 1 && n <= 16) p = (pdw)(p - n);
    }
    ov.len = ov.len < PDE_VT - 1 ? p : (pdw)(ov.len - 16);
    ov.text[p] = 0;
}

/* ---- PDV_ENG_CRYPTINFO: the /Encrypt dictionary ---------------------------------- */
static struct pd_u32 cr_dpos, cr_cpos;  /* the dictionary, just inside; the /CF dictionary */
static pdb   cr_nm[3][24];              /* /StmF /StrF /EFF                            */
static pdw   cr_cflen;                  /* a crypt filter's /Length                    */

static pdw cr_find(const char *k)
{
    L = &fobj;
    lx_seek(&cr_dpos);
    return ob_dict_find(k);
}

/* the value at the lexer, references followed, as ov */
static pdw cr_val(void)
{
    pdw rc = ob_value();
    if (rc) return rc;
    if (ov.kind == PDK_REF) {
        lx_tell(&on_pos);
        rc = ob_follow();
        if (rc) return rc;
        L = &fobj;
        lx_seek(&on_pos);
    }
    return PDE_OK;
}

static pdw cr_geti(const char *k, pdw def, pdw *out)
{
    pdw rc = cr_find(k);
    *out = def;
    if (rc == PDE_NOTFOUND) return PDE_OK;
    if (rc) return rc;
    rc = cr_val();
    if (rc) return rc;
    if (ov.kind == PDK_INT && !ov.neg && !ov.iv.hi) *out = ov.iv.lo;
    return PDE_OK;
}

static pdw cr_gets(const char *k, pdb *dst, pdw max, pdw *len)
{
    pdw rc = cr_find(k), n, i;
    *len = 0;
    if (rc == PDE_NOTFOUND) return PDE_OK;
    if (rc) return rc;
    rc = ob_value();
    if (rc) return rc;
    if (ov.kind != PDK_STR) return PDE_OK;
    n = ov.len < max ? ov.len : max;
    if (n > PDE_VT - 1) n = PDE_VT - 1;
    for (i = 0; i < n; i++) dst[i] = ov.text[i];
    *len = n;
    return PDE_OK;
}

static pdw cr_getn(const char *k, pdb *dst)
{
    pdw rc = cr_find(k), i;
    dst[0] = 0;
    if (rc == PDE_NOTFOUND) return PDE_OK;
    if (rc) return rc;
    rc = cr_val();
    if (rc) return rc;
    if (ov.kind == PDK_NAME) { for (i = 0; i < 23 && ov.text[i]; i++) dst[i] = ov.text[i]; dst[i] = 0; }
    return PDE_OK;
}

/* a crypt filter's method by its name; PDE_ENCRYPT-worthy values are 0xFF */
static pdw cr_cfm(const pdb *name, pdw *meth)
{
    pdw rc;
    *meth = PDCM_IDENTITY;
    if (!name[0] || str_eq(name, "Identity")) return PDE_OK;
    rc = cr_find("CF");
    if (rc == PDE_NOTFOUND) return PDE_OK;
    if (rc) return rc;
    rc = cr_val();
    if (rc) return rc;
    if (ov.kind != PDK_DICT) return PDE_OK;
    L = &fobj;
    rc = ob_dict_find((const char *)name);
    if (rc == PDE_NOTFOUND) return PDE_OK;
    if (rc) return rc;
    rc = cr_val();
    if (rc) return rc;
    if (ov.kind != PDK_DICT) return PDE_OK;
    lx_tell(&cr_cpos);                                 /* the crypt filter's dictionary */
    rc = ob_dict_find("Length");
    if (rc == PDE_OK) { rc = cr_val(); if (!rc && ov.kind == PDK_INT && !ov.neg) cr_cflen = ov.iv.lo; }
    else if (rc != PDE_NOTFOUND) return rc;
    L = &fobj;
    lx_seek(&cr_cpos);
    rc = ob_dict_find("CFM");
    if (rc == PDE_NOTFOUND) return PDE_OK;
    if (rc) return rc;
    rc = cr_val();
    if (rc) return rc;
    if (ov.kind != PDK_NAME) return PDE_OK;
    if (str_eq(ov.text, "V2")) *meth = PDCM_RC4;
    else if (str_eq(ov.text, "AESV2")) *meth = PDCM_AESV2;
    else if (str_eq(ov.text, "AESV3")) *meth = PDCM_AESV3;
    else if (str_eq(ov.text, "None")) *meth = PDCM_NONE;
    else *meth = 0xFF;
    return PDE_OK;
}

static pdw cr_info(void)
{
    struct pdxb_crypt *x = CRI;
    pdw rc, i, v, r, len, dp, nfix, m1, m2, m3;
    if (nctx != 0) return PDE_RANGE;
    if (tr_enc_k == 0) return PDE_RANGE;
    L = &fobj;
    cr_curn = 0;
    if (tr_enc_k == 1) {
        rc = ob_enter(tr_enc_n, tr_enc_g);
        if (rc) return rc;
        if (ob_null) return PDE_ENCRYPT;
        rc = ob_value();
        if (rc) return rc;
    } else {
        lx_seek(&tr_enc_pos);
        ov_clear(); ov.kind = PDK_DICT;
    }
    if (ov.kind != PDK_DICT) return PDE_ENCRYPT;
    lx_tell(&cr_dpos);
    for (i = 0; i < sizeof(struct pdxb_crypt); i++) ((pdb *)x)[i] = 0;
    rc = cr_getn("Filter", cr_nm[0]);
    if (rc) return rc;
    if (!str_eq(cr_nm[0], "Standard")) return PDE_ENCRYPT;
    rc = cr_geti("V", 0, &v);
    if (!rc) rc = cr_geti("R", 0, &r);
    if (!rc) rc = cr_geti("Length", 40, &len);
    if (rc) return rc;
    if (v != 1 && v != 2 && v != 4 && v != 5) return PDE_ENCRYPT;
    if (r < 2 || r > 6) return PDE_ENCRYPT;
    x->v = v; x->r = r; x->length = (v == 1) ? 40 : len;
    rc = cr_find("P");
    if (rc) return rc == PDE_NOTFOUND ? PDE_ENCRYPT : rc;
    rc = cr_val();
    if (rc) return rc;
    if (ov.kind != PDK_INT) return PDE_ENCRYPT;
    x->p_lo = ov.iv.lo; x->p_hi = ov.iv.hi;
    if (ov.neg) {                                      /* two's complement */
        x->p_lo = (pdw)(~ov.iv.lo + 1);
        x->p_hi = (pdw)(~ov.iv.hi + (ov.iv.lo == 0));
    }
    rc = cr_gets("O", x->o, 48, &x->o_len);
    if (!rc) rc = cr_gets("U", x->u, 48, &x->u_len);
    if (!rc && r >= 5) {
        rc = cr_gets("OE", x->oe, 32, &len);
        if (!rc) rc = cr_gets("UE", x->ue, 32, &len);
        if (!rc) rc = cr_gets("Perms", x->perms, 16, &len);
    }
    if (rc) return rc;
    x->encmeta = 1;
    rc = cr_find("EncryptMetadata");
    if (rc == PDE_OK) { rc = ob_value(); if (!rc && ov.kind == PDK_BOOL) x->encmeta = (ov.fx.lo || ov.fx.hi) ? 1 : 0; }
    if (rc && rc != PDE_NOTFOUND) return rc;
    x->stm = PDCM_RC4; x->str = PDCM_RC4; x->eff = PDCM_RC4;
    if (v >= 4) {
        rc = cr_getn("StmF", cr_nm[0]);
        if (!rc) rc = cr_getn("StrF", cr_nm[1]);
        if (!rc) rc = cr_getn("EFF", cr_nm[2]);
        if (rc) return rc;
        cr_cflen = 0;
        rc = cr_cfm(cr_nm[0], &m1);
        if (!rc) rc = cr_cfm(cr_nm[1], &m2);
        if (!rc) rc = cr_cfm(cr_nm[2], &m3);
        if (rc) return rc;
        if (m1 == 0xFF || m2 == 0xFF || m3 == 0xFF) return PDE_ENCRYPT;
        x->stm = m1; x->str = m2; x->eff = m3;
        if (m3 == PDCM_IDENTITY && cr_nm[2][0] == 0) x->eff = m1;
        if (x->stm == PDCM_AESV2) x->length = 128;
        else if (x->stm == PDCM_AESV3) x->length = 256;
        else if (cr_cflen) x->length = cr_cflen < 40 ? (pdw)(cr_cflen << 3) : cr_cflen;
    }
    dp = tr_idlen < 32 ? tr_idlen : 32;
    for (i = 0; i < dp; i++) x->id0[i] = tr_id[i];
    x->id0_len = dp;
    (void)nfix;
    pd_fcopy_out(x, g_xb, 0, sizeof(struct pdxb_crypt));
    return PDE_OK;
}

/* ---- PDV_ENG_SETKEY ------------------------------------------------------------------ */
static pdw cr_setkey(void)
{
    struct pdxb_crypt *x = CRI;
    pdw i;
    if (nctx != 0) return PDE_RANGE;
    pd_fcopy_in(g_xb, 0, x, sizeof(struct pdxb_crypt));
    if (!((x->key_len >= 5 && x->key_len <= 16) || x->key_len == 32)) return PDE_RANGE;
    if (x->stm > PDCM_IDENTITY || x->str > PDCM_IDENTITY) return PDE_RANGE;
    for (i = 0; i < 32; i++) cr_key[i] = x->key[i];
    cr_klen = x->key_len; cr_stm = x->stm; cr_str = x->str; cr_meta = x->encmeta;
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

/* eng's RAW_COPY with mode PDRC_PLAIN on an encrypted file: see 20.22.5 */
static pdw cr_raw_plain(void)
{
    return PDE_ENCRYPT;
}

/* ---- PDV_ENG_CRYPT: encrypt or decrypt a far buffer with an explicit key ---------------
 * RC4 in any chunking; AES-CBC when every chunk but the last is a whole number of
 * blocks. Block at a time: the writer's buffers are small, and so is this. */
#define COP ((struct pdxb_cryptop *)tailbuf)
static pdb cr_blk[16], cr_nxt[16], cr_w2[16];

static pdw cr_crypt(void)
{
    struct pdxb_cryptop *o = COP;
    pdw i, k, n, nr, first, last, nin, pi, oo, np, tail, pad;
    pd_fcopy_in(g_xb, 0, o, sizeof(struct pdxb_cryptop));
    first = o->phase & 1; last = (o->phase >> 1) & 1;
    if (o->key_len < 5 || (o->key_len > 16 && o->key_len != 32)) return PDE_RANGE;
    if (o->method == PDCM_IDENTITY || o->method == PDCM_NONE) return PDE_OK;
    if (o->method == PDCM_RC4) {
        if (first) { n = cr_objkeyk(o->key, o->key_len, o->objnum, o->gen, 0); pdc_rc4_ks(o->rc4, cr_ok, n); }
        for (k = 0; k < o->len; k += n) {
            n = (pdw)(o->len - k); if (n > 128) n = 128;
            pd_fcopy_in(o->seg, (pdw)(o->off + k), tmpb, n);
            pdc_rc4(o->rc4, tmpb, n);
            pd_fcopy_out(tmpb, o->seg, (pdw)(o->off + k), n);
        }
        pd_fcopy_out(o, g_xb, 0, sizeof(struct pdxb_cryptop));
        return PDE_OK;
    }
    if (o->method != PDCM_AESV2 && o->method != PDCM_AESV3) return PDE_RANGE;
    if (o->method == PDCM_AESV3) { pdc_aes_ks(cr_rk, o->key, 8); nr = 14; }
    else { cr_objkeyk(o->key, o->key_len, o->objnum, o->gen, 1); pdc_aes_ks(cr_rk, cr_ok, 4); nr = 10; }
    cr_rkn = 0;
    if (!last && (o->len & 15)) return PDE_RANGE;
    if (!o->dir) {                                              /* decrypt, in place, lagging */
        pi = first ? 16 : 0; oo = 0;
        if (first) {
            if (o->len < 16) { o->len = 0; goto done; }
            pd_fcopy_in(o->seg, o->off, o->iv, 16);
        }
        nin = (pdw)(o->len & 0xFFF0);
        for (; pi < nin; pi += 16) {
            pd_fcopy_in(o->seg, (pdw)(o->off + pi), cr_blk, 16);
            pdc_cbc_dec(cr_rk, nr, o->iv, cr_blk, 1);
            if (last && pi + 16 >= nin) {
                tail = cr_blk[15];
                if (tail >= 1 && tail <= 16) { pd_fcopy_out(cr_blk, o->seg, (pdw)(o->off + oo), (pdw)(16 - tail)); oo = (pdw)(oo + 16 - tail); break; }
            }
            pd_fcopy_out(cr_blk, o->seg, (pdw)(o->off + oo), 16);
            oo = (pdw)(oo + 16);
        }
        o->len = oo;
        goto done;
    }
    np = (pdw)(o->len >> 4);                                    /* encrypt: whole blocks, then (last) the padded tail */
    tail = (pdw)(o->len & 15);
    if (last) np = (pdw)(np + 1);
    oo = first ? 16 : 0;
    if ((pdw)(oo + (np << 4)) > o->room || (pdw)(oo + (np << 4)) < oo) return PDE_RANGE;
    for (i = 0; i < 16; i++) cr_nxt[i] = 0;
    if (np) {                                                   /* P0 */
        if (last && np == 1) {
            pd_fcopy_in(o->seg, o->off, cr_nxt, tail);
            pad = (pdw)(16 - tail);
            for (i = tail; i < 16; i++) cr_nxt[i] = (pdb)pad;
        } else pd_fcopy_in(o->seg, o->off, cr_nxt, 16);
    }
    if (first) pd_fcopy_out(o->iv, o->seg, o->off, 16);
    for (k = 0; k < np; k++) {
        for (i = 0; i < 16; i++) cr_blk[i] = cr_nxt[i];
        if (k + 1 < np) {                                       /* the next plain block, before this one's output lands on it */
            if (last && k + 2 == np) {
                for (i = 0; i < 16; i++) cr_nxt[i] = 0;
                pd_fcopy_in(o->seg, (pdw)(o->off + ((k + 1) << 4)), cr_nxt, tail);
                pad = (pdw)(16 - tail);
                for (i = tail; i < 16; i++) cr_nxt[i] = (pdb)pad;
            } else pd_fcopy_in(o->seg, (pdw)(o->off + ((k + 1) << 4)), cr_nxt, 16);
        }
        pdc_cbc_enc(cr_rk, nr, o->iv, cr_blk, 1);
        pd_fcopy_out(cr_blk, o->seg, (pdw)(o->off + oo + (k << 4)), 16);
    }
    o->len = (pdw)(oo + (np << 4));
done:
    pd_fcopy_out(o, g_xb, 0, sizeof(struct pdxb_cryptop));
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
    if (rc == PDE_OK) {
        rc = cr_val();
        if (!rc && ov.kind == PDK_NAME) {
            if (str_eq(ov.text, "XRef")) return 1;
            if (str_eq(ov.text, "Metadata") && !cr_meta) return 1;
        }
    }
    if (!sp_crypt) return 0;
    L = &fobj;
    lx_seek(&ds_pos);
    rc = ob_dict_find("DecodeParms");
    if (rc == PDE_NOTFOUND) { L = &fobj; lx_seek(&ds_pos); rc = ob_dict_find("DP"); }
    if (rc) return 1;                                   /* /Crypt with no parameters: /Identity */
    rc = ob_value();
    if (rc) return 1;
    if (ov.kind == PDK_ARR) {                           /* the first dictionary */
        for (hops = 0; hops < 8; hops++) {
            rc = ob_value();
            if (rc || ov.kind == PDK_CLOSE) return 1;
            if (ov.kind == PDK_DICT) break;
        }
    }
    if (ov.kind != PDK_DICT) return 1;
    rc = ob_dict_find("Name");
    if (rc) return 1;
    rc = ob_value();
    if (rc || ov.kind != PDK_NAME) return 1;
    return str_eq(ov.text, "Identity") ? 1 : 0;
}
