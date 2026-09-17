#!/usr/bin/env python3
"""What the RAD replayer's gates share (SPEC.md 96.8): symbols, the RADGATE
client, reading the driver and the tune's claim, the register log.

Not a test: tests/radopl3.py, radopl2.py, radrtc.py and radmove.py import it.
"""
import os
import re
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests", "unit"))
sys.path.insert(0, HERE)
import os88geom                                             # noqa: E402
import os88sym                                              # noqa: E402
import radsim                                               # noqa: E402

S = os88sym.linear
DSV_TICK = os88geom.DSV_TICK
SND_ROW, DRVR_SEG = 0, 2
RO_STATE, RO_CHIP, RO_CLASS = 32, 33, 34
RST_LEN = 72


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def u32(b, i=0):
    return u16(b, i) | (u16(b, i + 2) << 16)


def _map(src, defines=(), incs=()):
    """Assemble `src` with a symbol map; {label: offset}."""
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "s.asm"), os.path.join(d, "s.map")
        open(cp, "w").write(open(os.path.join(ROOT, src)).read()
                            + "\n[map symbols %s]\n" % mp)
        cmd = ["nasm", "-f", "bin", "-w+error"] + ["-D" + x for x in defines]
        for i in incs:
            cmd += ["-I", os.path.join(ROOT, i)]
        subprocess.run(cmd + ["-o", os.path.join(d, "s.bin"), cp], check=True,
                       cwd=ROOT)
        out = {}
        for line in open(mp):
            f = line.split()
            if len(f) == 3 and re.fullmatch(r"[0-9A-F]+", f[0]):
                out[f[2]] = int(f[0], 16)
        return out


def drv_syms(defines=()):
    return _map("drivers/sound/sound.asm", defines,
                ("drivers/sound/", "drivers/", "apps/"))


def ovl_syms(defines=()):
    return _map("drivers/sound/radplay.asm", defines,
                ("drivers/sound/", "drivers/", "apps/"))


def gate_syms():
    return _map("tests/radgate/radgate.asm", (), ("apps/", "build/"))


class Box:
    """One machine: the emulator `m` (QEMU's Qemu or a MartyPC) and the maps."""

    def __init__(self, m, defines=()):
        self.m = m
        self.D = drv_syms(defines)
        self.O = ovl_syms(defines)
        self.G = gate_syms()
        self.gseg = None

    def dseg(self):
        return u16(self.m.read(S("drv_tab") + SND_ROW * 16 + DRVR_SEG, 2))

    def dbyte(self, name):
        return self.m.read((self.dseg() << 4) + self.D[name], 1)[0]

    def dword(self, name):
        return u16(self.m.read((self.dseg() << 4) + self.D[name], 2))

    def kcell(self):
        return u16(self.m.read(S("drv_svc") + DSV_TICK, 2))

    def dcell(self):
        return u16(self.m.read((self.dseg() << 4) + self.D["snd_services"]
                               + DSV_TICK, 2))

    def caps(self):
        return u16(self.m.read(S("drv_svc"), 2))

    def claim(self):
        return self.dword("rad_seg")

    def obyte(self, name):
        return self.m.read((self.claim() << 4) + self.O[name], 1)[0]

    def ostat(self):
        """The overlay's own status block (not a verb 6 copy)."""
        seg = self.claim()
        if not seg:
            return None
        return self.m.read((seg << 4) + self.O["rd_st"], RST_LEN)

    def frames(self):
        st = self.ostat()
        return None if st is None else u32(st, 12)

    def tick(self):
        return u16(self.m.read(0x46C, 2))

    # --- RADGATE ---------------------------------------------------------
    def gread(self, name, n):
        return self.m.read((self.gseg << 4) + self.G[name], n)

    def res(self):
        """RADGATE's last answer: (CF, AL, AH, CX, count)."""
        b = self.gread("rt_res", 6)
        return b[0], b[1], b[2], u16(b, 3), b[5]

    def rlog(self):
        seg = self.dword("rad_logseg")
        if not seg:
            return None
        n = u16(self.m.read(seg << 4, 2))
        self.logfull = bool(n & 0x8000)
        n &= 0x7FFF
        return radsim.decode_rlg(self.m.read((seg << 4) + 2, n))

    def held(self, seg):
        """Is `seg` still a claim in the kernel's mem_tab (SPEC.md 50)?"""
        n = os88geom.MC_SIZE
        raw = self.m.read(S("mem_tab"), 32 * n)
        return any(u16(raw, i * n) == seg for i in range(32))

    def ahrows(self):
        """RADGATE's `a`: [(CF, AL, AH, CX)] x 6 (tests/radgate/radgate.asm)."""
        raw = self.gread("rt_ares", 30)
        return [(raw[5 * i], raw[5 * i + 1], raw[5 * i + 2], u16(raw, 5 * i + 3))
                for i in range(6)]

    def table(self):
        """[(CF, AL, AH, CX)] for every row RADGATE's `v` fed to verb 4."""
        n = u16(self.gread("rt_vn", 2))
        raw = self.gread("rt_vres", 5 * n)
        return [(raw[5 * i], raw[5 * i + 1], raw[5 * i + 2], u16(raw, 5 * i + 3))
                for i in range(n)]


def table_diffs(got, opl3):
    """RADGATE's answers against radsim's, row by row. Verb 4 answers
    (CF=0) for an accepted row; radsim answers None."""
    import radrows
    bad = []
    for (name, data, _w, _o), (cf, al, ah, cx) in zip(radrows.ROWS, got):
        want = radsim.check(data, opl3=opl3)
        if want is None:
            ok = cf == 0 and al == data[0x10]
            have = None if cf == 0 else (al, ah, cx)
        else:
            # AH exactly (RADGATE calls with AH = A5h, so a refusal that left
            # it would show), and CX preserved - the file's length - on every
            # refusal but RADE_CORRUPT, where it is the offset (SPEC.md 34.12)
            code, detail, off = want
            have = (al, ah, cx if al == 3 else 0) if cf else None
            ok = cf == 1 and have == want and (al == 3 or cx == len(data))
            if cf and al != 3 and cx != len(data):
                have = have + ("CX %04x, not the length %04x" % (cx, len(data)),)
        if not ok:
            bad.append((name, have, want))
    if len(got) != len(radrows.ROWS):
        bad.append(("row count", len(got), len(radrows.ROWS)))
    return bad


RADE_NOTUNE, RADE_BADARG = 8, 10
AHROWS = [                          # RADGATE `a`'s rows: (name, CF, AL, AH, CX)
    ("verb 5, sub-op 9, a tune loaded", 1, RADE_BADARG, 0, 0x5A5A),
    ("verb 6, a buffer wrapping its segment, CX = 100h", 1, RADE_BADARG, 0, 0x100),
    ("verb 5 stop with no tune (sub-op 3 in AH)", 1, RADE_NOTUNE, 0, 0x5A5A),
    ("verb 4, SI + CX = 10100h, AH = A5h", 1, RADE_BADARG, 0, 0x200),
    ("verb 6 with no tune, CX = 100h, AH = A5h", 1, RADE_NOTUNE, 0, 0x100),
    ("verb 7 (undefined), AH = 77h", 1, RADE_BADARG, 0, 0x5A5A),
]


def ahrow_diffs(got):
    return [(name, g, (cf, al, ah, cx)) for (name, cf, al, ah, cx), g
            in zip(AHROWS, got) if g != (cf, al, ah, cx)]


def compare_log(log, tune, opl3):
    frames = sum(1 for r, v in log if r == 0xFFFF)
    ref = radsim.sent_stream(radsim.reference_stream(tune, frames, opl3=opl3))
    if log == ref:
        return frames, None
    for i, (a, b) in enumerate(zip(log, ref)):
        if a != b:
            fr = sum(1 for r, v in log[:i] if r == 0xFFFF)
            return frames, ("record %d (frame %d): driver %03x=%02x, radsim %03x=%02x"
                            % (i, fr, a[0], a[1], b[0], b[1]))
    return frames, "lengths differ: driver %d records, radsim %d" % (len(log), len(ref))


def loud(path, floor=300, stride=64):
    """Samples over `floor` (16-bit) in a MartyPC / QEMU capture, every
    `stride`th one - a not-silent test that does not load 70MB of floats."""
    import struct as st
    with open(path, "rb") as f:
        head = f.read(12)
        if head[:4] != b"RIFF" or head[8:12] != b"WAVE":
            return 0
        bits = 16
        while True:
            h = f.read(8)
            if len(h) < 8:
                return 0
            cid, sz = h[:4], st.unpack("<I", h[4:])[0]
            if cid == b"fmt ":
                body = f.read(sz + (sz & 1))
                bits = st.unpack_from("<H", body, 14)[0]
            elif cid == b"data":
                break
            else:
                f.seek(sz + (sz & 1), 1)
        if bits != 16:
            return 0
        count = 0
        while True:
            chunk = f.read(1 << 20)
            if len(chunk) < 2:
                return count
            vals = st.unpack("<%dh" % (len(chunk) // 2), chunk[:len(chunk) & ~1])
            count += sum(1 for v in vals[::stride] if abs(v) > floor)
