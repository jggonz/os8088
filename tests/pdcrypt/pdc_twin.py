"""The host twin (apps/pagedesk/pdctwin.c) as a pdc_vec backend, through ctypes."""
import ctypes, os, subprocess, struct
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUILD = os.environ.get("PDC_BUILD", os.path.join(ROOT, "build", "pdcrypt"))

def words(wl): return [wl[i] | (wl[i + 1] << 16) for i in range(0, len(wl), 2)]
def words64(wl): return [sum(wl[i + k] << (16 * k) for k in range(4)) for i in range(0, len(wl), 4)]

class Twin:
    def __init__(self):
        os.makedirs(BUILD, exist_ok=True)
        so = os.path.join(BUILD, "pdctwin.so")
        ad = os.path.join(ROOT, "apps/pagedesk")
        subprocess.check_call(["cc", "-O1", "-shared", "-fPIC", "-DPD_HOST", "-Wall", "-I", ad, "-o", so,
                               os.path.join(ad, "pdctwin.c")])
        self.l = ctypes.CDLL(so)
    def _st(self, n, init):
        st = (ctypes.c_uint16 * n)(); init(st); return st
    def md5_init(self):
        st = self._st(8, self.l.pdc_md5_init); return words(list(st))
    def _put(self, vals, per):
        out = []
        for v in vals:
            for k in range(per): out.append((v >> (16 * k)) & 0xFFFF)
        return (ctypes.c_uint16 * len(out))(*out)
    def md5_block(self, st, blk):
        s = self._put(st, 2); self.l.pdc_md5_block(s, bytes(blk)); return words(list(s))
    def sha256_init(self):
        return words(list(self._st(16, self.l.pdc_sha256_init)))
    def sha256_block(self, st, blk):
        s = self._put(st, 2); self.l.pdc_sha256_block(s, bytes(blk)); return words(list(s))
    def sha512_init(self): return words64(list(self._st(32, self.l.pdc_sha512_init)))
    def sha384_init(self): return words64(list(self._st(32, self.l.pdc_sha384_init)))
    def sha512_block(self, st, blk):
        s = self._put(st, 4); self.l.pdc_sha512_block(s, bytes(blk)); return words64(list(s))
    def rc4(self, key, data):
        s = ctypes.create_string_buffer(258); self.l.pdc_rc4_ks(s, key, len(key))
        b = ctypes.create_string_buffer(bytes(data), len(data) or 1); self.l.pdc_rc4(s, b, len(data))
        return b.raw[:len(data)]
    def aes_ks(self, key, nk):
        rk = ctypes.create_string_buffer(240); self.l.pdc_aes_ks(rk, key, nk); return rk.raw[:16 * (nk + 7)]
    def aes_enc(self, rk, nr, blk):
        o = ctypes.create_string_buffer(16); self.l.pdc_aes_enc(rk, nr, bytes(blk), o); return o.raw
    def aes_dks(self, rk, nr):
        b = ctypes.create_string_buffer(bytes(rk), len(rk)); self.l.pdc_aes_dks(b, nr); return b.raw[:len(rk)]
    def aes_dec(self, rk, nr, blk):                     # rk: a standard schedule; the decryption one is made here
        o = ctypes.create_string_buffer(16); self.l.pdc_aes_dec(self.aes_dks(rk, nr), nr, bytes(blk), o); return o.raw
    def _cbc(self, f, rk, nr, iv, data):
        iv = ctypes.create_string_buffer(bytes(iv), 16); b = ctypes.create_string_buffer(bytes(data), len(data))
        f(rk, nr, iv, b, len(data) // 16); return b.raw[:len(data)], iv.raw
    def cbc_enc(self, rk, nr, iv, data): return self._cbc(self.l.pdc_cbc_enc, rk, nr, iv, data)
    def cbc_dec(self, rk, nr, iv, data): return self._cbc(self.l.pdc_cbc_dec, self.aes_dks(rk, nr), nr, iv, data)

if __name__ == "__main__":
    import sys; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import pdc_vec
    print("pdc twin: %d vectors ok" % pdc_vec.check(Twin(), "twin"))
