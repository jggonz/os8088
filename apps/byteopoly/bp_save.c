/* Byteopoly - save files and the file half of paired checkpoints
 * (SPEC.md 131.7). Lane: save. FOUNDATION STUB; replaced by the save lane. */
int bp_save_local(const char *name, const BPState *s) { (void)name; (void)s; return BP_EIO; }
int bp_load_local(const char *name, BPState *out) { (void)name; (void)out; return BP_EIO; }
int bp_ckpt_write(const BPCkpt *meta, const BPState *s) { (void)meta; (void)s; return BP_EIO; }
int bp_ckpt_commit(const BPCkpt *meta) { (void)meta; return BP_EIO; }
int bp_ckpt_load(int role, BPCkpt *meta, BPState *out) { (void)role; (void)meta; (void)out; return BP_EIO; }
