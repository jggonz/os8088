/* ============================================================================
 * os8088 - apps/pagedesk/pdcrypto.h        PageDesk's crypto primitives
 * (PAGEDESK-SPEC 20.22.5)
 *
 * Every function here has an 8086 body (pdcmd5.inc, pdcrc4.inc, pdcaes.inc,
 * pdcsha2.inc: SmallerC cdecl, near pointers into the module's DS) and a HOST
 * twin in pdctwin.c that uses the compiler's own 32/64-bit integers, so the
 * assembly is held to the published vectors twice: once by the twin's
 * harness and once by tests/pdcrypt/pdc_diff.py, which runs the shipping text
 * in Unicorn.
 *
 * LAYOUTS (shared by the C twin, the assembly and the callers):
 *   MD5 state     pdw[8]   A,B,C,D, each little-endian lo,hi
 *   SHA-256 state pdw[16]  eight words, each lo,hi (the word's VALUE, not
 *                          its big-endian bytes)
 *   SHA-512 state pdw[32]  eight 64-bit words, each four pdw, least first
 *   AES schedule  pdb[240] the FIPS 197 expanded key, byte order as printed
 *   RC4 state     pdb[258] S[0..255], then i, then j
 * Blocks are plain byte arrays; the procedures do the endianness.
 * ==========================================================================*/
#ifndef PDCRYPTO_H
#define PDCRYPTO_H

void pdc_md5_init(pdw *st);
void pdc_md5_block(pdw *st, const pdb *blk);              /* 64 bytes     */
void pdc_sha256_init(pdw *st);
void pdc_sha256_block(pdw *st, const pdb *blk);           /* 64 bytes     */
void pdc_sha384_init(pdw *st);                            /* SHA-512 block */
void pdc_sha512_init(pdw *st);
void pdc_sha512_block(pdw *st, const pdb *blk);           /* 128 bytes    */

void pdc_rc4_ks(pdb *s, const pdb *key, pdw n);           /* n 1..32      */
void pdc_rc4(pdb *s, pdb *buf, pdw n);                    /* in place     */

/* nk = key words: 4 (AES-128) or 8 (AES-256); fills (nk + 7) * 16 bytes */
void pdc_aes_ks(pdb *rk, const pdb *key, pdw nk);
void pdc_aes_enc(const pdb *rk, pdw nr, const pdb *in, pdb *out);
void pdc_aes_dec(const pdb *rk, pdw nr, const pdb *in, pdb *out);
/* CBC over nblk whole blocks, in place; iv (16) is updated to the last
 * ciphertext block, so the next run chains */
void pdc_cbc_enc(const pdb *rk, pdw nr, pdb *iv, pdb *buf, pdw nblk);
void pdc_cbc_dec(const pdb *rk, pdw nr, pdb *iv, pdb *buf, pdw nblk);

#endif
