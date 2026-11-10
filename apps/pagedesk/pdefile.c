/* ============================================================================
 * pdefile.c - the file layer (PAGEDESK-SPEC 6.1, 6.2)
 *
 * A block cache over OSAPI_FILE_READ_SEQ. PD_BLK is the cluster size, never
 * under 2 KiB (one READ_SEQ call a cluster on a 512-byte-cluster disk would
 * be 4 calls for the same 2 KiB); the cache claim holds up to PDE_MAXBLK
 * blocks, LRU. A cluster over 32 KiB or over half the claim is refused
 * PDE_NOMEM: READ_SEQ's capacity must be a cluster multiple, so there is no
 * "4 KiB window" of a bigger cluster (deviation from 6.1, see 20.2).
 * The whole file is never resident; offsets are pd_u32.
 * ==========================================================================*/

/* perf: v_dict_get's array cursor (pdeverb.c): the array (its first item's
 * offset, 0 = none), the element last answered and the offset after it */
static struct pd_u32 dc_arr, dc_pos, dc_a;
static pdw dc_idx;
/* ...and its key cache: the object whose top-level key[0] was found last,
 * and the offset just after that name (dk_obj 0 = none) */
static struct pd_u32 dk_pos;
static pdw dk_obj, dk_gen;

static void fl_invalidate(void)
{
    pdw i;
    u32_zero(&dc_arr);
    dk_obj = 0; an_pg1 = 0;
    for (i = 0; i < PDE_MAXBLK; i++) { fl_valid[i] = 0; fl_stamp[i] = 0; }
    fl_clock = 0;
    for (i = 0; i < 8; i++) fl_cur[i] = 0;
}

static int fl_nameq(const char *a, const char *b)
{
    while (*a && *b) {
        if (upc((pdb)*a) != upc((pdb)*b)) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

/* fl_name's directory entry into fl_fnd, and fl_name respelled as the
 * directory spells it (READ_SEQ takes the name on every call). PDE_IO when it
 * is not in the current folder. */
static struct os88_find fl_fnd;
static pdw fl_find(void)
{
    int ord, nx;
    pdw i;
    for (ord = 0;;) {
        nx = os88_file_find(ord, &fl_fnd);
        if (nx < 0 || nx <= ord) return PDE_IO;     /* not in this folder (or a find that does not advance) */
        ord = nx;
        if (fl_fnd.type >= OS88_FT_DIR) continue;
        if (fl_nameq(fl_fnd.name, fl_name)) break;
    }
    for (i = 0; i < 13 && fl_fnd.name[i]; i++) fl_name[i] = fl_fnd.name[i];
    fl_name[i] = 0;
    return PDE_OK;
}

/* Name, size and packed-ness from the directory; geometry from the claim. */
static pdw fl_open(const char *name)
{
    pdw cs, k, i, bytes;
    cs = (pdw)os88_disk_cluster_sectors();
    if (cs == 0) cs = 1;
    if (cs > 64) return PDE_NOMEM;                  /* clusters over 32 KiB */
    bytes = (pdw)(cs * 512);                        /* <= 32768              */
    fl_blk = PDE_MINBLK;
    if (bytes > fl_blk) fl_blk = bytes;
    fl_shift = 0;
    for (k = fl_blk; k > 1; k >>= 1) fl_shift++;
    /* the claim's blocks: cache_kb / (blk in KB), at most PDE_MAXBLK */
    k = (pdw)(fl_blk >> 10);
    fl_nblk = (pdw)(g_ckb / k);
    if (fl_nblk > PDE_MAXBLK) fl_nblk = PDE_MAXBLK;
    if (fl_nblk > (pdw)(64 / k)) fl_nblk = (pdw)(64 / k);   /* offsets < 64 KiB */
    if (fl_nblk < 2) return PDE_NOMEM;
    for (i = 0; i < 13 && name[i]; i++) fl_name[i] = name[i];
    fl_name[i] = 0;
    fl_invalidate();
    i = fl_find();
    if (i) return i;
    if (fl_fnd.rsvd & 1) { err_detail = PDX_PACKED; return PDE_SYNTAX; }
    fl_size.lo = fl_fnd.size_lo; fl_size.hi = fl_fnd.size_hi;
    if (fl_size.hi >= PDE_FILEMAX_HI) return PDE_LIMIT;
    g_filed = 1;
    return PDE_OK;
}

/* Slot holding block (bhi:blo), reading it if need be. -1 = at/after the end,
 * -2 = an I/O error (pde_ioerr set). */
static int fl_getblock(pdw blo, pdw bhi)
{
    pdw i, v, got;
    int best;
    for (i = 0; i < fl_nblk; i++)
        if (fl_valid[i] && fl_blo[i] == blo && fl_bhi[i] == bhi) {
            if (++fl_clock == 0) {
                for (v = 0; v < fl_nblk; v++) fl_stamp[v] = 0;
                fl_clock = 1;
            }
            fl_stamp[i] = fl_clock;
            return (int)i;
        }
    best = 0;
    for (i = 0; i < fl_nblk; i++) {
        if (!fl_valid[i]) { best = (int)i; break; }
        if (fl_stamp[i] < fl_stamp[best]) best = (int)i;
    }
    pdm_u32_set(&f_a, bhi, blo);
    pdm_u32_shl(&f_a, &f_a, fl_shift);
    if (pdm_u32_cmp(&f_a, &fl_size) >= 0) return -1;
    if (fl_cur[6] != f_a.lo || fl_cur[7] != f_a.hi) {
        for (i = 0; i < 8; i++) fl_cur[i] = 0;
        fl_cur[6] = f_a.lo; fl_cur[7] = f_a.hi;
    }
    fl_valid[best] = 0;
    fl_reads++;
    got = os88_file_read_seq(fl_name, g_cseg, (pdw)(best * fl_blk), fl_blk, fl_cur);
    if (got == 0) {
        if (os88_ferr()) { pde_ioerr = PDE_IO; return -2; }
        return -1;
    }
    fl_blo[best] = blo; fl_bhi[best] = bhi; fl_valid[best] = got;
    if (++fl_clock == 0) {
        for (v = 0; v < fl_nblk; v++) fl_stamp[v] = 0;
        fl_clock = 1;
    }
    fl_stamp[best] = fl_clock;
    return best;
}

/* Copy up to n bytes from *pos into a near buffer (dst) or a far one (dseg:
 * doff, dst == 0). Advances *pos. Returns the bytes copied (< n at the end
 * or on an I/O error, which sets pde_ioerr). */
static pdw fl_copy(struct pd_u32 *pos, pdb *dst, pdw dseg, pdw doff, pdw n)
{
    pdw done = 0, inb, take, mask;
    int slot;
    mask = (pdw)(fl_blk - 1);
    while (done < n) {
        pdm_u32_shr(&f_b, pos, fl_shift);
        slot = fl_getblock(f_b.lo, f_b.hi);
        if (slot < 0) break;
        inb = (pdw)(pos->lo & mask);
        if (inb >= fl_valid[slot]) break;
        take = (pdw)(fl_valid[slot] - inb);
        if (take > (pdw)(n - done)) take = (pdw)(n - done);
        if (dst) pd_fcopy_in(g_cseg, (pdw)(slot * fl_blk + inb), dst + done, take);
        else if (pd_fcopy(dseg, (pdw)(doff + done), g_cseg, (pdw)(slot * fl_blk + inb), take) != take)
            break;                                  /* refused (it would wrap): stop short */
        pdm_u32_add16(pos, take);
        done = (pdw)(done + take);
    }
    return done;
}
static pdw fl_read(struct pd_u32 *pos, pdb *dst, pdw n) { return fl_copy(pos, dst, 0, 0, n); }
static pdw fl_read_far(struct pd_u32 *pos, pdw dseg, pdw doff, pdw n) { return fl_copy(pos, 0, dseg, doff, n); }

/* 6.2: size, CRC-16 of the first and last KiB, read fresh off the disk.
 * The size is the directory's NOW, not OPEN's, and the tail is read at it: a
 * file another program appended an update to keeps its first KiB and the KiB
 * that used to be last, so a fingerprint taken at OPEN's size could not see
 * the change the writer asks this verb about (14.4). The open document's own
 * fl_size is put back afterwards and the cache dropped again: what was read
 * under the new size is not the document that was parsed. */
static struct pd_u32 fp_keep;
/* The CRC of PDE_TAIL bytes from s_c, read 128 at a time through tmpb (any
 * PDF: the 1 KiB tail buffer is gone - OPEN borrows the stream contexts). */
static pdw fp_crc(void)
{
    pdw crc = 0xFFFF, k, n;
    for (k = 0; k < PDE_TAIL / 128; k++) {
        n = fl_read(&s_c, tmpb, 128);
        crc = pdm_crc16(crc, tmpb, n);
        if (n < 128) break;
    }
    return crc;
}

static pdw fl_fingerprint(struct pdxb_fingerprint *fp)
{
    pdw rc;
    fl_invalidate();
    pde_ioerr = 0;
    rc = fl_find();
    if (rc) return rc;
    u32_cp(&fp_keep, &fl_size);
    fl_size.lo = fl_fnd.size_lo; fl_size.hi = fl_fnd.size_hi;
    fp->size.lo = fl_size.lo; fp->size.hi = fl_size.hi;
    u32_zero(&s_c);
    fp->crc_head = fp_crc();
    if (fl_size.hi == 0 && fl_size.lo <= PDE_TAIL) fp->crc_tail = fp->crc_head;
    else {
        u32_cp(&s_c, &fl_size);
        u32_sub16(&s_c, PDE_TAIL);
        fp->crc_tail = fp_crc();
    }
    u32_cp(&fl_size, &fp_keep);
    fl_invalidate();
    return pde_ioerr ? PDE_IO : PDE_OK;
}
