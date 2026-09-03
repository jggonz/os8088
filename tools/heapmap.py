#!/usr/bin/env python3
"""The claim map, as a TIMELINE rather than a snapshot (SPEC.md 50).

Two snapshots say what the heap ended up like. What decides whether a
transient boot allocation can be given back is the ORDER claims are taken in
and whether each one can move, so this samples `mem_tab` while the machine
boots and keeps every distinct map it sees.

Reads, per sample:
  [mem_base] / [mem_top]  - the arena's ends (.bss, KERNEL_SEG)
  [spl_live]              - is the loading screen still up (.text)
  mem_tab                 - MEM_MAX records of MC_SIZE (.lowbss, LOW_SEG)

A record is base segment, size in paragraphs, owner, the page-safe DMA head,
and MC_RLOC - which is 0 for PINNED and the near offset of a relocation proc
otherwise (SPEC.md 66.2). That last word is the whole point: "can this claim
be compacted" is a machine-readable fact, not something to grep the drivers
for.

Import it, or run it against a live QMP socket:

    python3 tools/heapmap.py build/qmp.sock
"""
import json
import os
import socket
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import os88sym                                              # noqa: E402

MEM_MAX = 32
MC_SIZE = 10
MC_SEG, MC_PARA, MC_OWN, MC_DMA, MC_RLOC = 0, 2, 4, 6, 8
INST_MAX = 12

# 0xFF00 | tag - the kernel's own claims (SPEC.md 50.2); 0xFB..0xFE in the
# high byte is a PURGEABLE tag carrying its priority (50.6.4).
KTAG = {0xFF01: "SAVE",  0xFF03: "DRV",   0xFF04: "COPY",
        0xFF06: "ASC",   0xFF07: "CLIP",  0xFF08: "MOD",  0xFF09: "CLONE",
        0xFF0A: "BAND",  0xFF0B: "OVL",   0xFF0C: "HIB"}
# Purgeable RANGES: base -> (name, count). The consumer adds an ordinal to the
# base, so these are decoded before the exact-match table (SPEC.md 50.6).
# 0xFF05 was MEM_K_FATW until the FAT window became a cache (SPEC.md 18.8.4).
PGRANGE = {0xFB10: ("WSAVE", 16), 0xFD20: ("FATW", 8)}
PGO_MIN, PGO_MAX = 0xFB, 0xFE
PURGE = {0xFB: "TRIV", 0xFC: "LOW", 0xFD: "MED", 0xFE: "HIGH"}


def owner(w):
    hi = w >> 8
    for base, (nm, n) in PGRANGE.items():
        if 0 <= w - base < n:
            return "purge:%s/%s%d" % (PURGE[base >> 8], nm, w - base)
    if hi in PURGE:
        return "purge:%s/%02X" % (PURGE[hi], w & 0xFF)
    if w in KTAG:
        return "kern:" + KTAG[w]
    if hi == 0xFF:
        return "kern:?%02X" % (w & 0xFF)
    if w < INST_MAX:
        return "inst %d" % w
    return "seg %04X" % w


class Qmp:
    """One connection per command - `-qmp ...,server,nowait` serves a single
    client, so a held-open monitor makes every other tool sit in the backlog
    for ever (tests/ethernet.py's note, and it cost a session there)."""

    def __init__(self, path):
        self.path = path
        self.tmp = tempfile.mkdtemp()

    def hmp(self, cmd):
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(self.path)
        f = s.makefile("rw")

        def send(o):
            f.write(json.dumps(o) + "\n")
            f.flush()
            while True:
                line = f.readline()
                if not line:
                    raise RuntimeError("QMP closed")
                m = json.loads(line)
                if "event" not in m:
                    return m
        try:
            json.loads(f.readline())
            send({"execute": "qmp_capabilities"})
            r = send({"execute": "human-monitor-command",
                      "arguments": {"command-line": cmd}})
            return r.get("return", "")
        finally:
            f.close()
            s.close()

    def read(self, linear, n):
        p = os.path.join(self.tmp, "m.bin")
        # the filename MUST be quoted: HMP parses a bare /tmp/... as an
        # expression and answers "invalid char 't'", which reads as a bad
        # address rather than a bad argument
        self.hmp('pmemsave 0x%X %d "%s"' % (linear, n, p))
        with open(p, "rb") as f:
            return f.read()


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


class Claim(object):
    __slots__ = ("seg", "para", "own", "dma", "rloc")

    def __init__(self, r):
        self.seg = u16(r, MC_SEG)
        self.para = u16(r, MC_PARA)
        self.own = u16(r, MC_OWN)
        self.dma = u16(r, MC_DMA)
        self.rloc = u16(r, MC_RLOC)

    @property
    def end(self):
        return self.seg + self.para

    @property
    def kb(self):
        return self.para / 64.0

    @property
    def pinned(self):
        return self.rloc == 0

    @property
    def purgeable(self):
        return PGO_MIN <= (self.own >> 8) <= PGO_MAX

    def key(self):
        return (self.seg, self.para, self.own, self.rloc)

    def __repr__(self):
        return ("%05X..%05X %7.1fK  %-14s %s%s"
                % (self.seg << 4, self.end << 4, self.kb, owner(self.own),
                   "PINNED " if self.pinned else "movable",
                   "  dma-head %d para" % self.dma if self.dma else ""))


class Map(object):
    """One sample of the claim heap, read while the guest is RUNNING.

    Every field is a separate `pmemsave`, so a sample straddles whatever the
    guest did in between - and one thing it does is move both ends at once:
    mem_unblob lowers [mem_base] onto the floor and then compacts the movable
    claims down onto it (SPEC.md 50.3.3). Read the ends before the table and
    that sample pairs the OLD base with the NEW claims, which is a heap that
    has claims below its own floor - a torn read reported as a bug.

    So the ends are read twice, either side of the table, and the sample is
    taken again if they moved. Bounded, because the ends move exactly twice in
    a boot; a guest that kept moving them would fail with the torn sample
    rather than spin.
    """

    def __init__(self, q, sym):
        for _ in range(4):
            base = u16(q.read(sym["mem_base"], 2))
            top = u16(q.read(sym["mem_top"], 2))
            live = q.read(sym["spl_live"], 1)[0]
            raw = q.read(sym["mem_tab"], MEM_MAX * MC_SIZE)
            if (base, top) == (u16(q.read(sym["mem_base"], 2)),
                               u16(q.read(sym["mem_top"], 2))):
                break
        self.base, self.top, self.live = base, top, live
        self.claims = sorted((Claim(raw[i * MC_SIZE:(i + 1) * MC_SIZE])
                              for i in range(MEM_MAX)),
                             key=lambda c: c.seg)
        self.claims = [c for c in self.claims if c.seg]

    def key(self):
        return (self.base, self.top, tuple(c.key() for c in self.claims))

    def runs(self, drop_purgeable=False):
        """Free runs in the arena, as (base_para, para).

        `drop_purgeable` answers the question that actually decides a refusal:
        mem_claim SHEDS and retries before it gives up (SPEC.md 50.6.2), so
        what a claimant can have is the arena with the caches gone - not the
        arena as it stands. A cache in the WRONG PLACE looks free either way
        and hands its room to a corner; this is the number that says so.
        """
        out, at = [], self.base
        for c in self.claims:
            if drop_purgeable and c.purgeable:
                continue
            if c.seg > at:
                out.append((at, c.seg - at))
            at = max(at, c.end)
        if self.top > at:
            out.append((at, self.top - at))
        return out

    def compacted(self):
        """Free runs after mem_claim's FULL refusal path (SPEC.md 66.9).

        `runs(drop_purgeable=True)` models the shed and stops there, which was
        the whole answer while a cache was a barrier the compactor walked past.
        Since 66.9 it is not: a pass with a claim waiting DISSOLVES a cache
        instead of treating it as a wall, and the movable claims above it then
        pack down through the room it leaves. So the number a claimant can
        actually have needs mem_cp_plan's arithmetic, not just the shed - and
        the two differ by every movable claim that sits above a cache, which
        on a 640K boot is `kern:ASC` and 6.5K of stranded floor.

        This is a MODEL of the kernel's walk and not a reading of it, which is
        the one thing about this line to keep in mind: SPEC.md 66.4 says the
        plan and the run disagreeing is how this feature promises room it does
        not deliver, and a third copy of the walk living on the host is a
        third thing that can disagree. It is here because nothing else can see
        the number at all.
        """
        out, at = [], self.base
        for c in self.claims:
            if c.purgeable:                     # dissolved, not walked past
                continue
            if c.pinned:                        # a barrier: the gap under it
                if c.seg > at:                  # is a run, and the fill point
                    out.append((at, c.seg - at))  # resumes above it
                at = c.end
            else:
                at += c.para                    # packs down onto the fill point
        if self.top > at:
            out.append((at, self.top - at))
        return out

    def report(self, label):
        print("\n=== %s ===" % label)
        print("  arena %05X..%05X   %.0fK   splash %s"
              % (self.base << 4, self.top << 4,
                 (self.top - self.base) / 64.0,
                 "UP" if self.live else "handed over"))
        for c in self.claims:
            print("    " + repr(c))
        rs = self.runs()
        if rs:
            print("  free runs: " + ", ".join("%.1fK@%05X" % (p / 64.0, b << 4)
                                              for b, p in rs))
            print("  LARGEST CONTIGUOUS: %.1fK   total free %.1fK   in %d run(s)"
                  % (max(p for _, p in rs) / 64.0,
                     sum(p for _, p in rs) / 64.0, len(rs)))
            sh = self.runs(drop_purgeable=True)
            if any(c.purgeable for c in self.claims):
                print("  ...WITH THE CACHES SHED: %.1fK, in %d run(s)"
                      % (max(p for _, p in sh) / 64.0, len(sh)))
            cp = self.compacted()
            print("  ...AND AFTER A COMPACTION: %.1fK, in %d run(s) - what a "
                  "claimant can actually have (modelled)"
                  % (max(p for _, p in cp) / 64.0, len(cp)))
        else:
            print("  free runs: none")


def symbols():
    return {n: os88sym.linear(n) for n in
            ("mem_base", "mem_top", "spl_live", "mem_tab")}


def main():
    q = Qmp(sys.argv[1] if len(sys.argv) > 1 else "build/qmp.sock")
    Map(q, symbols()).report("now")
    return 0


if __name__ == "__main__":
    sys.exit(main())
