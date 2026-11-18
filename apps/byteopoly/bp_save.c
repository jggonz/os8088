/* Byteopoly - save files and the file half of paired checkpoints
 * (SPEC.md 131.7.3, 131.7.4). Original. Lane: save.
 *
 * Thin: the file image (header, CRC, validation) is bp_codec.c, OS-free and
 * host-tested; this file only moves whole files through os88_file_*. Every
 * body is an ovl_* function (UI task, menu commands only) behind a resident
 * wrapper. A file goes through bpc_io, the codec's 512-byte scratch. */

static BPCkpt bps_cm;            /* local meta / commit scratch */
static BPCkpt bps_sl[2];         /* the two slots' headers */
static BPByte bps_sk[2];         /* slot kind: 0 missing or invalid, 1 candidate, 2 committed */
static int    bps_se[2];         /* a slot file that existed but failed: its error code */
static char   bps_nm[13];        /* the slot file name being used */

static int ovl_bp_fmap(int fe)
{
    if (fe == OS88_FERR_FULL || fe == OS88_FERR_DIRFULL
        || fe == OS88_FERR_PROT || fe == OS88_FERR_WPROT) return BP_EFULL;
    if (fe == OS88_FERR_BIG) return BP_EFORMAT;
    return BP_EIO;
}

/* the whole file into bpc_io: its length, or -(a BP_E* code) */
static int ovl_bp_rd(const char *name)
{
    unsigned n;
    int fe;
    n = os88_file_read(name, bpc_io, BP_SAVE_MAX);
    fe = os88_ferr();
    if (fe) return -ovl_bp_fmap(fe);
    if (!n) return -BP_EFORMAT;
    return (int)n;
}

/* write bpc_io[0..n) as the whole file, then read it back: same length, a good
 * CRC and the CRC that was written. bpc_io is clobbered by the read-back. */
static int ovl_bp_wr(const char *name, unsigned n)
{
    unsigned t, c, m;
    int fe;
    t = bpc_io[n - 2] | ((unsigned)bpc_io[n - 1] << 8);
    if (os88_file_write(name, bpc_io, n) != 0 || os88_ferr()) {
        fe = os88_ferr();
        return fe ? ovl_bp_fmap(fe) : BP_EIO;
    }
    m = os88_file_read(name, bpc_io, BP_SAVE_MAX);
    if (m != n || os88_ferr()) return BP_EIO;
    c = bp_crc16(bpc_io, n - 2, 0xFFFFu);
    if (c != t || c != (bpc_io[n - 2] | ((unsigned)bpc_io[n - 1] << 8))) return BP_EIO;
    return BP_OK;
}

static int ovl_bp_save_local(const char *name, const BPState *s)
{
    unsigned n;
    int r;
    r = ovl_bp_validate(s);
    if (r) return r;
    os88_memset(&bps_cm, 0, sizeof(BPCkpt));
    bps_cm.role = 0; bps_cm.state = 2;
    n = ovl_bp_file_build(&bps_cm, s, bpc_io, BP_SAVE_MAX);
    if (!n) return BP_EFORMAT;
    return ovl_bp_wr(name, n);
}
int bp_save_local(const char *name, const BPState *s) { return ovl_bp_save_local(name, s); }

static int ovl_bp_load_local(const char *name, BPState *out)
{
    int n, r;
    n = ovl_bp_rd(name);
    if (n < 0) return -n;
    r = ovl_bp_file_parse(bpc_io, (unsigned)n, &bps_cm, out);
    if (r) return r;
    if (bps_cm.role != 0) return BP_EMATCH;       /* a cable checkpoint: the copy action's */
    return BP_OK;
}
int bp_load_local(const char *name, BPState *out) { return ovl_bp_load_local(name, out); }

/* ---- checkpoint slots (131.7.4) ------------------------------------------ */
static void ovl_bp_slotname(int role, int slot)
{
    os88_strcpy(bps_nm, role == BP_MODE_HOST ? "BPHOSTA.BPC" : "BPCLNTA.BPC", 13);
    bps_nm[6] = (char)('A' + slot);
}

static int ovl_bp_idgt(const BPU32 *a, const BPU32 *b)
{
    return a->hi > b->hi || (a->hi == b->hi && a->lo > b->lo);
}
static int ovl_bp_ideq(const BPU32 *a, const BPU32 *b)
{
    return a->hi == b->hi && a->lo == b->lo;
}

/* read both slots' headers into bps_sl / bps_sk / bps_se (bpc_io is clobbered) */
static void ovl_bp_scan(int role)
{
    int i, n, r;
    for (i = 0; i < 2; i++) {
        os88_memset(&bps_sl[i], 0, sizeof(BPCkpt));
        bps_sk[i] = 0; bps_se[i] = 0;
        ovl_bp_slotname(role, i);
        n = ovl_bp_rd(bps_nm);
        if (n < 0) continue;
        r = ovl_bp_file_peek(bpc_io, (unsigned)n, &bps_sl[i]);
        if (r) { os88_memset(&bps_sl[i], 0, sizeof(BPCkpt)); bps_se[i] = r; continue; }
        if (bps_sl[i].role != role) {
            os88_memset(&bps_sl[i], 0, sizeof(BPCkpt)); bps_se[i] = BP_EMATCH; continue;
        }
        bps_sk[i] = bps_sl[i].state;
    }
}

/* the slot of this kind with the highest ckpt_id (slot A on a tie), or -1 */
static int ovl_bp_pick(int kind)
{
    int best = -1, i;
    for (i = 0; i < 2; i++) {
        if (bps_sk[i] != kind) continue;
        if (best < 0 || ovl_bp_idgt(&bps_sl[i].ckpt_id, &bps_sl[best].ckpt_id)) best = i;
    }
    return best;
}

static int ovl_bp_ckpt_write(const BPCkpt *meta, const BPState *s)
{
    int r, v, o;
    unsigned n;
    if (meta->role < 1 || meta->role > 2) return BP_EMATCH;
    r = ovl_bp_validate(s);
    if (r) return r;
    if (!ovl_bp_ideq(&meta->match_id, &s->match_id) || !ovl_bp_ideq(&meta->rev, &s->rev))
        return BP_EMATCH;
    if (meta->digest.lo | meta->digest.hi) {        /* the caller's digest must be the state's */
        bp_digest(s, &bpc_t2);
        if (!ovl_bp_ideq(&meta->digest, &bpc_t2)) return BP_EMATCH;
    }
    os88_memcpy(&bps_cm, meta, sizeof(BPCkpt));
    bps_cm.state = 1;
    ovl_bp_scan(meta->role);
    /* the victim: the lowest by (kind, ckpt_id), slot B on a tie */
    v = 0;
    if (bps_sk[1] < bps_sk[0]
        || (bps_sk[1] == bps_sk[0] && !ovl_bp_idgt(&bps_sl[1].ckpt_id, &bps_sl[0].ckpt_id))) v = 1;
    n = ovl_bp_file_build(&bps_cm, s, bpc_io, BP_SAVE_MAX);
    if (!n) return BP_EFORMAT;
    ovl_bp_slotname(meta->role, v);
    r = ovl_bp_wr(bps_nm, n);
    if (r) return r;
    o = 1 - v;                                  /* a slot of another match is stale */
    if (bps_sk[o] && !ovl_bp_ideq(&bps_sl[o].match_id, &s->match_id)) {
        ovl_bp_slotname(meta->role, o);
        if (os88_file_delete(bps_nm) != 0) return BP_EIO;
    }
    return BP_OK;
}
int bp_ckpt_write(const BPCkpt *meta, const BPState *s) { return ovl_bp_ckpt_write(meta, s); }

static int ovl_bp_ckpt_commit(const BPCkpt *meta)
{
    int i, n, c;
    unsigned crc;
    if (meta->role < 1 || meta->role > 2) return BP_EMATCH;
    ovl_bp_scan(meta->role);
    c = -1;
    for (i = 0; i < 2; i++) {
        if (!bps_sk[i] || !ovl_bp_ideq(&bps_sl[i].ckpt_id, &meta->ckpt_id)
            || !ovl_bp_ideq(&bps_sl[i].match_id, &meta->match_id)) continue;
        if (bps_sk[i] == 2) return BP_OK;        /* already committed */
        c = i;
    }
    if (c < 0) return BP_EMATCH;
    ovl_bp_slotname(meta->role, c);
    n = ovl_bp_rd(bps_nm);
    if (n < 0) return -n;
    i = ovl_bp_file_peek(bpc_io, (unsigned)n, &bps_cm);
    if (i) return i;
    bpc_io[6] = 2;                               /* the commit marker */
    crc = bp_crc16(bpc_io, (unsigned)n - 2, 0xFFFFu);
    bpc_io[n - 2] = (BPByte)crc; bpc_io[n - 1] = (BPByte)(crc >> 8);
    return ovl_bp_wr(bps_nm, (unsigned)n);
}
int bp_ckpt_commit(const BPCkpt *meta) { return ovl_bp_ckpt_commit(meta); }

static int ovl_bp_ckpt_open(int role, int which, BPCkpt *meta, BPState *out)
{
    int slot, cur, n;
    if (role < 1 || role > 2 || which < 0 || which > 2) return BP_EMATCH;
    ovl_bp_scan(role);
    cur = ovl_bp_pick(2);
    slot = -1;
    if (which == 0) slot = cur;
    else if (which == 1) { if (cur >= 0 && bps_sk[1 - cur] == 2) slot = 1 - cur; }
    else slot = ovl_bp_pick(1);
    if (slot < 0) {
        if (bps_se[0]) return bps_se[0];
        if (bps_se[1]) return bps_se[1];
        if (which == 0 && (bps_sk[0] == 1 || bps_sk[1] == 1)) return BP_EMATCH;
        return BP_EIO;
    }
    ovl_bp_slotname(role, slot);
    n = ovl_bp_rd(bps_nm);
    if (n < 0) return -n;
    if (!out) return ovl_bp_file_peek(bpc_io, (unsigned)n, meta);
    return ovl_bp_file_parse(bpc_io, (unsigned)n, meta, out);
}
int bp_ckpt_open(int role, int which, BPCkpt *meta, BPState *out)
{
    return ovl_bp_ckpt_open(role, which, meta, out);
}
int bp_ckpt_load(int role, BPCkpt *meta, BPState *out)
{
    return ovl_bp_ckpt_open(role, 0, meta, out);
}

static int ovl_bp_ckpt_damaged(int role)
{
    ovl_bp_scan(role);
    return (bps_se[0] != 0) + (bps_se[1] != 0);
}
int bp_ckpt_damaged(int role) { return ovl_bp_ckpt_damaged(role); }

/* the checkpoint with this ckpt_id, committed (preferred) or candidate; meta->state says which */
static int ovl_bp_ckpt_load_id(int role, const BPU32 *id, BPCkpt *meta, BPState *out)
{
    int i, slot, n;
    if (role < 1 || role > 2) return BP_EMATCH;
    ovl_bp_scan(role);
    slot = -1;
    for (i = 0; i < 2; i++)
        if (bps_sk[i] && ovl_bp_ideq(&bps_sl[i].ckpt_id, id) && (slot < 0 || bps_sk[i] == 2)) slot = i;
    if (slot < 0) {
        if (bps_se[0]) return bps_se[0];
        if (bps_se[1]) return bps_se[1];
        return BP_EIO;
    }
    ovl_bp_slotname(role, slot);
    n = ovl_bp_rd(bps_nm);
    if (n < 0) return -n;
    return ovl_bp_file_parse(bpc_io, (unsigned)n, meta, out);
}
int bp_ckpt_load_id(int role, const BPU32 *id, BPCkpt *meta, BPState *out)
{
    return ovl_bp_ckpt_load_id(role, id, meta, out);
}
