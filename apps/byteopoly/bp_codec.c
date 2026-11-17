/* Byteopoly - canonical encoding, validation and digest (SPEC.md 131.7).
 * Lane: save. FOUNDATION STUB; the save lane replaces this file. */
BPWord bp_crc16(const BPByte *p, unsigned n, BPWord crc) { (void)p; (void)n; return crc; }
unsigned bp_encode(const BPState *s, BPByte *buf, unsigned cap) { (void)s; (void)buf; (void)cap; return 0; }
int bp_decode(const BPByte *buf, unsigned n, BPState *out) { (void)buf; (void)n; (void)out; return BP_EFORMAT; }
int bp_validate(const BPState *s) { (void)s; return BP_OK; }
void bp_digest(const BPState *s, BPU32 *out) { (void)s; bp_u32_set(out, 0, 0); }
