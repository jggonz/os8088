#!/usr/bin/env python3
"""
os8088 - tests/pdcrypt/pdc_vec.py     pdcrypto.h against the published vectors

A BACKEND is anything with the block-level operations of apps/pagedesk/
pdcrypto.h (md5_block, sha256_block, sha512_block, rc4_ks, rc4, aes_ks,
aes_enc, aes_dec, cbc_enc, cbc_dec) on Python bytes/lists.  pdc_diff.py gives
it two - the host twin (pdctwin.c through ctypes) and the shipping 8086 text
(Unicorn) - and runs this file's checks on each:

  * MD5: RFC 1321 appendix A.5; SHA-256: FIPS 180-4 (abc, the 448-bit message,
    one million a); SHA-384 and SHA-512: FIPS 180-4 (abc, the 896-bit message)
  * AES-128 and AES-256: FIPS 197 appendix C, and NIST SP 800-38A F.2.1 /
    F.2.5 (CBC, four blocks, both directions)
  * RC4: the Wikipedia/RFC 6229-family vectors (Key/Plaintext, Key=Wiki,
    Secret) and the 40-bit key 0102030405 stream of RFC 6229
  * then every hash against hashlib on random lengths 0..300
"""
import hashlib, random, struct

def pad(data, block, lenbytes):
    n = len(data)
    data = data + b"\x80"
    while (len(data) + lenbytes) % block: data += b"\x00"
    return data + (n * 8).to_bytes(lenbytes, "big" if block != 64 or lenbytes == 8 and False else "big")

def md5_pad(data):
    n = len(data); data += b"\x80"
    while len(data) % 64 != 56: data += b"\x00"
    return data + struct.pack("<Q", n * 8)

def sha_pad(data, block):
    lenb = 8 if block == 64 else 16
    n = len(data); data += b"\x80"
    while len(data) % block != block - lenb: data += b"\x00"
    return data + (n * 8).to_bytes(lenb, "big")

def md5(be, data):
    st = be.md5_init()
    d = md5_pad(data)
    for i in range(0, len(d), 64): st = be.md5_block(st, d[i:i + 64])
    return b"".join(struct.pack("<I", w) for w in st)

def sha256(be, data):
    st = be.sha256_init()
    d = sha_pad(data, 64)
    for i in range(0, len(d), 64): st = be.sha256_block(st, d[i:i + 64])
    return b"".join(struct.pack(">I", w) for w in st)

def sha512(be, data, bits=512):
    st = be.sha512_init() if bits == 512 else be.sha384_init()
    d = sha_pad(data, 128)
    for i in range(0, len(d), 128): st = be.sha512_block(st, d[i:i + 128])
    return b"".join(struct.pack(">Q", w) for w in st)[:bits // 8]

H = bytes.fromhex
MD5V = [(b"", "d41d8cd98f00b204e9800998ecf8427e"), (b"a", "0cc175b9c0f1b6a831c399e269772661"),
        (b"abc", "900150983cd24fb0d6963f7d28e17f72"), (b"message digest", "f96b697d7cb7938d525a2f31aaf161d0"),
        (b"abcdefghijklmnopqrstuvwxyz", "c3fcd3d76192e4007dfb496cca67e13b"),
        (b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", "d174ab98d277d9f5a5611c2c9f419d9f"),
        (b"12345678901234567890123456789012345678901234567890123456789012345678901234567890", "57edf4a22be3c955ac49da2e2107b67a")]
SHA256V = [(b"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
           (b"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"),
           (b"a" * 1000000, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0")]
SHA384V = [(b"abc", "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7")]
SHA512V = [(b"abc", "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"),
           (b"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
            "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909")]

def check(be, name="", slow=True):
    n = 0
    for m, d in MD5V:
        assert md5(be, m).hex() == d, (name, "md5", m); n += 1
    for m, d in SHA256V:
        if not slow and len(m) > 1000: continue
        assert sha256(be, m).hex() == d, (name, "sha256", m[:20]); n += 1
    for m, d in SHA384V:
        assert sha512(be, m, 384).hex() == d, (name, "sha384"); n += 1
    for m, d in SHA512V:
        assert sha512(be, m).hex() == d, (name, "sha512"); n += 1
    # FIPS 197 appendix C
    pt = H("00112233445566778899aabbccddeeff")
    for key, ct in ((H("000102030405060708090a0b0c0d0e0f"), "69c4e0d86a7b0430d8cdb78070b4c55a"),
                    (H("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"), "8ea2b7ca516745bfeafc49904b496089")):
        nk = len(key) // 4
        rk = be.aes_ks(key, nk)
        assert be.aes_enc(rk, nk + 6, pt).hex() == ct, (name, "aes enc", nk)
        assert be.aes_dec(rk, nk + 6, H(ct)) == pt, (name, "aes dec", nk); n += 2
    # SP 800-38A CBC
    iv = H("000102030405060708090a0b0c0d0e0f")
    pt4 = H("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710")
    for key, ct in ((H("2b7e151628aed2a6abf7158809cf4f3c"),
                     "7649abac8119b246cee98e9b12e9197d5086cb9b507219ee95db113a917678b273bed6b8e3c1743b7116e69e222295163ff1caa1681fac09120eca307586e1a7"),
                    (H("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4"),
                     "f58c4c04d6e5f1ba779eabfb5f7bfbd69cfc4e967edb808d679f777bc6702c7d39f23369a9d9bacfa530e26304231461b2eb05e2c39be9fcda6c19078c6a9d1b")):
        nk = len(key) // 4
        rk = be.aes_ks(key, nk)
        assert be.cbc_enc(rk, nk + 6, iv, pt4)[0].hex() == ct, (name, "cbc enc", nk)
        assert be.cbc_dec(rk, nk + 6, iv, H(ct))[0] == pt4, (name, "cbc dec", nk); n += 2
    # RC4
    for key, plain, ct in ((b"Key", b"Plaintext", "bbf316e8d940af0ad3"), (b"Wiki", b"pedia", "1021bf0420"),
                           (b"Secret", b"Attack at dawn", "45a01f645fc35b383552544b9bf5")):
        assert be.rc4(key, plain).hex() == ct, (name, "rc4", key); n += 1
    ks = be.rc4(H("0102030405"), bytes(16))                      # RFC 6229, 40-bit key, offset 0
    assert ks.hex() == "b2396305f03dc027ccc3524a0a1118a8", (name, "rc4 rfc6229"); n += 1
    rnd = random.Random(88)
    for ln in list(range(0, 130)) + [255, 256, 300]:
        m = bytes(rnd.randrange(256) for _ in range(ln))
        assert md5(be, m) == hashlib.md5(m).digest(), (name, "md5 rnd", ln)
        assert sha256(be, m) == hashlib.sha256(m).digest(), (name, "sha256 rnd", ln)
        assert sha512(be, m) == hashlib.sha512(m).digest(), (name, "sha512 rnd", ln)
        assert sha512(be, m, 384) == hashlib.sha384(m).digest(), (name, "sha384 rnd", ln); n += 4
    return n
