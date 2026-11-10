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

#define PDE_RC4N 3              /* RC4 streams open at once (their state is 258 bytes each) */

static pdw   cr_on;             /* a file key is set                            */
static pdb   cr_key[32];
static pdw   cr_klen;           /* 5..16, or 32 for AESV3                        */
static pdw   cr_stm, cr_str;    /* PDCM_* for streams and strings               */
static pdw   cr_meta;           /* /EncryptMetadata                             */
static pdb   cr_rk[240];        /* the AES schedule in use                       */
static pdw   cr_rkn, cr_rkg;    /* ...whose it is: an object (0 none), or 0xFFFF/0xFFFF the file's key */
static pdb   cr_rc[PDE_RC4N][258];  /* RC4 states of open streams                */

#define CRI ((struct pdxb_crypt *)ctxs)        /* CRYPTINFO / SETKEY use the idle ctxs as their buffer */

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
    if (method == PDCM_AESV3) pdc_aes_ks(cr_rk, cr_key, 8);
    else { cr_objkey(num, gen, 1); pdc_aes_ks(cr_rk, pdc_ok, 4); }
    cr_rkn = num; cr_rkg = gen;
}

/* Called by st_push as the context is made (nctx is its index): the caller left
 * the stream's object in cr_sn/cr_sg (0 = none: an xref stream, or a caller that
 * did not say). PDE_LIMIT: an RC4 stream deeper than PDE_RC4N. */
static pdw cr_ctx_open(struct sctx *c)
{
    pdw n;
    c->cmode = 0; c->civ = 0; c->cn = cr_sn; c->cg = cr_sg;
    cr_rkn = 0;
    n = (pdw)(cr_on && cr_sn && !cr_skip && cr_stm >= PDCM_RC4 && cr_stm <= PDCM_AESV3);
    cr_sn = 0; cr_sg = 0; cr_skip = 0;
    if (!n) return PDE_OK;
    c->cmode = cr_stm;
    c->crs = c->cv;
    if (cr_stm == PDCM_RC4) {
        if (nctx >= PDE_RC4N) return PDE_LIMIT;
        c->crs = cr_rc[nctx];
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
        pdc_rc4_ks(tailbuf, pdc_ok, p);
        pdc_rc4(tailbuf, ov.text, n);
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

/* ---- PDV_ENG_CRYPTINFO: the /Encrypt dictionary ---------------------------------- */
static struct pd_u32 cr_dpos, cr_cpos;  /* the dictionary, just inside; a crypt filter's */
static pdb   cr_nm[2][24];              /* /StmF /StrF                                 */
static pdw   cr_gv, cr_cflen, ci_v, ci_r, ci_m[2];   /* (no address of a local: SPEC.md 73) */

static pdw cr_find(const char *k)
{
    L = &fobj;
    lx_seek(&cr_dpos);
    return ob_dict_find(k);
}

/* The value of key k, references followed. kind 0 an integer into cr_gv, 1 a
 * string into dst (cr_gv its length, at most max), 2 a name into dst, 3 a
 * boolean into cr_gv. PDE_NOTFOUND when absent; a value of another kind is 0. */
static pdw cr_get(const char *k, pdw kind, pdb *dst, pdw max)
{
    pdw rc = cr_find(k), n, i;
    cr_gv = 0;
    if (rc) return rc;
    rc = ob_value();
    if (rc) return rc;
    if (ov.kind == PDK_REF) {
        lx_tell(&on_pos);
        rc = ob_follow();
        if (rc) return rc;
        L = &fobj;
        lx_seek(&on_pos);
    }
    if (kind == 0) { if (ov.kind == PDK_INT && !ov.neg && !ov.iv.hi) cr_gv = ov.iv.lo; }
    else if (kind == 3) { if (ov.kind == PDK_BOOL) cr_gv = (ov.fx.lo || ov.fx.hi) ? 1 : 0; }
    else if (ov.kind == (kind == 1 ? PDK_STR : PDK_NAME)) {
        n = ov.len < max ? ov.len : max;
        if (n > PDE_VT - 1) n = PDE_VT - 1;
        for (i = 0; i < n; i++) dst[i] = ov.text[i];
        if (kind == 2) dst[n] = 0;
        cr_gv = n;
    }
    return PDE_OK;
}

/* a crypt filter's method by its name into *meth; 0xFF for one not implemented */
static pdw cr_cfm(const pdb *name, pdw *meth)
{
    pdw rc;
    *meth = PDCM_IDENTITY;
    if (!name[0] || str_eq(name, "Identity")) return PDE_OK;
    rc = cr_find("CF");
    if (!rc) rc = ob_value();
    if (rc || ov.kind != PDK_DICT) return rc == PDE_NOTFOUND ? PDE_OK : rc;
    rc = ob_dict_find((const char *)name);
    if (!rc) rc = ob_value();
    if (rc || ov.kind != PDK_DICT) return rc == PDE_NOTFOUND ? PDE_OK : rc;
    lx_tell(&cr_cpos);                                 /* the crypt filter's dictionary */
    rc = ob_dict_find("Length");
    if (!rc && ob_value() == PDE_OK && ov.kind == PDK_INT && !ov.neg) cr_cflen = ov.iv.lo;
    L = &fobj;
    lx_seek(&cr_cpos);
    rc = ob_dict_find("CFM");
    if (!rc) rc = ob_value();
    if (rc || ov.kind != PDK_NAME) return rc == PDE_NOTFOUND ? PDE_OK : rc;
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
    pdw rc, i;
    if (nctx != 0 || tr_enc_k == 0) return PDE_RANGE;
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
    rc = cr_get("Filter", 2, cr_nm[0], 23);
    if (rc || !str_eq(cr_nm[0], "Standard")) return rc ? rc : PDE_ENCRYPT;
    rc = cr_get("V", 0, 0, 0); ci_v = cr_gv;
    if (!rc) { rc = cr_get("R", 0, 0, 0); ci_r = cr_gv; }
    if (rc || (ci_v != 1 && ci_v != 2 && ci_v != 4 && ci_v != 5) || ci_r < 2 || ci_r > 6) return rc ? rc : PDE_ENCRYPT;
    x->v = ci_v; x->r = ci_r;
    rc = cr_get("Length", 0, 0, 0);
    if (rc && rc != PDE_NOTFOUND) return rc;
    x->length = (ci_v == 1 || cr_gv == 0) ? 40 : cr_gv;
    rc = cr_find("P");
    if (!rc) rc = ob_value();
    if (rc || ov.kind != PDK_INT) return rc ? rc : PDE_ENCRYPT;
    x->p_lo = ov.iv.lo; x->p_hi = ov.iv.hi;
    if (ov.neg) {                                      /* two's complement */
        x->p_lo = (pdw)(~ov.iv.lo + 1);
        x->p_hi = (pdw)(~ov.iv.hi + (ov.iv.lo == 0));
    }
    rc = cr_get("O", 1, x->o, 48); x->o_len = cr_gv;
    if (!rc) { rc = cr_get("U", 1, x->u, 48); x->u_len = cr_gv; }
    if (!rc && ci_r >= 5) {
        rc = cr_get("OE", 1, x->oe, 32);
        if (!rc) rc = cr_get("UE", 1, x->ue, 32);
        if (!rc) rc = cr_get("Perms", 1, x->perms, 16);
    }
    if (rc && rc != PDE_NOTFOUND) return rc;
    x->encmeta = 1;
    rc = cr_get("EncryptMetadata", 3, 0, 0);
    if (!rc) x->encmeta = cr_gv;
    else if (rc != PDE_NOTFOUND) return rc;
    x->stm = PDCM_RC4; x->str = PDCM_RC4;
    if (ci_v >= 4) {
        rc = cr_get("StmF", 2, cr_nm[0], 23);
        if (!rc || rc == PDE_NOTFOUND) { if (rc) cr_nm[0][0] = 0; rc = cr_get("StrF", 2, cr_nm[1], 23); if (rc) cr_nm[1][0] = 0; }
        if (rc && rc != PDE_NOTFOUND) return rc;
        cr_cflen = 0;
        rc = cr_cfm(cr_nm[0], &ci_m[0]);
        if (!rc) rc = cr_cfm(cr_nm[1], &ci_m[1]);
        if (rc) return rc;
        if (ci_m[0] == 0xFF || ci_m[1] == 0xFF) return PDE_ENCRYPT;
        x->stm = ci_m[0]; x->str = ci_m[1];
        if (x->stm == PDCM_AESV2) x->length = 128;
        else if (x->stm == PDCM_AESV3) x->length = 256;
        else if (cr_cflen) x->length = cr_cflen < 40 ? (pdw)(cr_cflen << 3) : cr_cflen;
    }
    x->eff = x->stm;
    x->id0_len = tr_idlen < 32 ? tr_idlen : 32;
    for (i = 0; i < x->id0_len; i++) x->id0[i] = tr_id[i];
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
    if (!((x->key_len >= 5 && x->key_len <= 16) || x->key_len == 32) || x->stm > PDCM_IDENTITY || x->str > PDCM_IDENTITY) return PDE_RANGE;
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

/* ---- PDV_ENG_CRYPT: encrypt or decrypt a far buffer with an explicit key ---------------
 * RC4 in any chunking (phase bit 0 = the first chunk: the key is set up). AES-CBC in
 * ONE call (phase 3): the writer's buffers are small. Block at a time. */
#define COP ((struct pdxb_cryptop *)tailbuf)
static pdb cr_blk[16], cr_nxt[16];

static pdw cr_crypt(void)
{
    struct pdxb_cryptop *o = COP;
    pdw i, k, n, nr, np, tail;
    pd_fcopy_in(g_xb, 0, o, sizeof(struct pdxb_cryptop));
    if (o->key_len < 5 || (o->key_len > 16 && o->key_len != 32)) return PDE_RANGE;
    if (o->method == PDCM_IDENTITY || o->method == PDCM_NONE) return PDE_OK;
    if (o->method == PDCM_RC4) {
        if (o->phase & 1) { pdc_okey(o->key, o->key_len, o->objnum, o->gen, 0); n = (pdw)(o->key_len + 5); pdc_rc4_ks(o->rc4, pdc_ok, n > 16 ? 16 : n); }
        for (k = 0; k < o->len; k += n) {
            n = (pdw)(o->len - k); if (n > 128) n = 128;
            pd_fcopy_in(o->seg, (pdw)(o->off + k), tmpb, n);
            pdc_rc4(o->rc4, tmpb, n);
            pd_fcopy_out(tmpb, o->seg, (pdw)(o->off + k), n);
        }
        pd_fcopy_out(o, g_xb, 0, sizeof(struct pdxb_cryptop));
        return PDE_OK;
    }
    if ((o->method != PDCM_AESV2 && o->method != PDCM_AESV3) || o->phase != 3) return PDE_RANGE;
    if (o->method == PDCM_AESV3) { pdc_aes_ks(cr_rk, o->key, 8); nr = 14; }
    else { pdc_okey(o->key, o->key_len, o->objnum, o->gen, 1); pdc_aes_ks(cr_rk, pdc_ok, 4); nr = 10; }
    cr_rkn = 0;
    np = (pdw)(o->len >> 4);
    if (!o->dir) {                                              /* decrypt, in place: IV, blocks, padding */
        if (np < 2) { o->len = 0; goto done; }
        pd_fcopy_in(o->seg, o->off, o->iv, 16);
        for (k = 1; k < np; k++) {
            pd_fcopy_in(o->seg, (pdw)(o->off + (k << 4)), cr_blk, 16);
            pdc_cbc_dec(cr_rk, nr, o->iv, cr_blk, 1);
            n = 16;
            if (k + 1 == np && cr_blk[15] >= 1 && cr_blk[15] <= 16) n = (pdw)(16 - cr_blk[15]);
            pd_fcopy_out(cr_blk, o->seg, (pdw)(o->off + ((k - 1) << 4)), n);
            o->len = (pdw)(((k - 1) << 4) + n);
        }
        goto done;
    }
    tail = (pdw)(o->len & 15);                                  /* encrypt: IV, blocks, the padded tail */
    k = (pdw)((np << 4) + 32);
    if (k > o->room || k < 32) return PDE_RANGE;
    for (i = 0; i < 16; i++) cr_nxt[i] = 0;
    pd_fcopy_in(o->seg, (pdw)(o->off + (np << 4)), cr_nxt, tail);       /* the tail, before it is overwritten */
    for (i = tail; i < 16; i++) cr_nxt[i] = (pdb)(16 - tail);
    for (k = np; k > 0; k--) {                                 /* from the end, so a block's output never lands on one not yet read */
        pd_fcopy_in(o->seg, (pdw)(o->off + ((k - 1) << 4)), cr_blk, 16);
        pd_fcopy_out(cr_blk, o->seg, (pdw)(o->off + (k << 4)), 16);
    }
    pd_fcopy_out(o->iv, o->seg, o->off, 16);                   /* now the data sits at +16: encrypt it in place, forwards */
    pd_fcopy_out(cr_nxt, o->seg, (pdw)(o->off + 16 + (np << 4)), 16);
    for (k = 0; k <= np; k++) {
        pd_fcopy_in(o->seg, (pdw)(o->off + 16 + (k << 4)), cr_blk, 16);
        pdc_cbc_enc(cr_rk, nr, o->iv, cr_blk, 1);
        pd_fcopy_out(cr_blk, o->seg, (pdw)(o->off + 16 + (k << 4)), 16);
    }
    o->len = (pdw)(16 + ((np + 1) << 4));
done:
    pd_fcopy_out(o, g_xb, 0, sizeof(struct pdxb_cryptop));
    return PDE_OK;
}
