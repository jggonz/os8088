#!/usr/bin/env python3
"""CHOOSE the boot canary's sector, per build arm (SPEC.md 18.93.1, 2.9.13.3).

    python3 tools/os88ksig.py choose --blob N --geom 360:spc=1 --geom 720 ...
            (--kz "-DKZ_SECS=.. -DKZ_RPARA=.. -DKZ_HEADSEC=.." --file KZ1 | --plain)
            --inc build/ksig.inc
    python3 tools/os88ksig.py verify --blob N --geom ... --file KERNEL.SYS
            (--json build/kernel.kz.json | --plain) --inc build/ksig.inc
    python3 tools/os88ksig.py seed --inc build/ksig.inc
    python3 tools/os88ksig.py head --map build/kernel.map --min N
    python3 tools/os88ksig.py band --blob N --geom ... --kz ... [--head-bound cylinder]

KSIG_OFF WAS TYPED, AND EVERY MOVE OF IT WAS A HAND RE-DERIVATION (the big
review's boot-10). It was typed twice - the Makefile and boot/boot2.asm - with
per-arm arithmetic in both so the FILE sector stayed put while the blob length
moved, and `t_canary` checked the result after the fact. It moved half a dozen
times: with BOOT2_SECS, with the set of shipped geometries, with the size of
`.text`, and in BR-1 because the model of where runs START was wrong for two
months while the gate agreed with the model. And BOOT3-PLAN wave 4 moves every
blob length at once, which a constant checked at 13-15 sectors cannot follow.

So the build CHOOSES it: this walks the runs stage 2 cuts on every system disk
the arm ships on, with the arm's own blob, plain head, packed length and R, and
writes the LOWEST memory offset whose sector

  1. lies in the PACKED TAIL (KZIP; boot2.asm addresses it as `KSIG_OFF +
     KZ_RPARA*16`, which only names a tail sector) - or anywhere in the plain
     read (NOKZIP) - inside the 64KB the handoff's ES names, and inside what
     stage 2 reads;
  2. is read AFTER A HEAD FLIP, in a run that began BEFORE it, on EVERY one of
     the arm's disks (the rule is about the run, not the sector: a run loads
     correctly up to the flip, so a canary in a run's first half is loaded
     correctly on exactly the machine it exists to catch);
  3. sums differently from both neighbours (KSIGDEF2's signature is the
     sector's 16-bit sum, and a sector equal to its neighbour passes a read
     shifted by one - the padding trap).

**IT FAILS THE BUILD RATHER THAN PICK AN INERT SECTOR.** If no sector meets all
three on every disk, there is nothing to write: it exits non-zero naming the
disk that has no candidate, and the answer is a design decision (a geometry, a
blob length, a head), never a looser rule.

THE MODEL IS stage 2's read_run, written out again here: a run stops at the
first of [b2_runmax] (the CYLINDER on an 8088, which is where the canary is
asked at all - boot2.asm's `.nocross` skips it on a track-bounded load), the
sectors still wanted, and the 64KB DMA page of the destination. Under KZIP the
plain head is read first in runs of its own (at the TRACK since BR-1's part 2;
`--head-bound cylinder` models the other answer to the owner's open question
about part 2), and the packed tail restarts BEHIND it, R paragraphs up. The
head's bound cannot move a tail run's start - the tail starts where the head
ENDS, whatever the head's runs were - so both answers to part 2 choose the same
sector, and `choose` asserts that on every build rather than claiming it.

IT IS INDEPENDENT OF THE GATE ON PURPOSE (boot-10's fit lens). BR-1 was the
gate and the model agreeing while both were wrong, so tests/unit/t_canary.py
imports nothing from here: it reads the sector the build ARMED back out of
each disk's own boot sector and the kernel's map, walks the runs with its own
code over the BPB the disk really carries, and passes only if the two
implementations agree. The geometries here come from tools/os88disk.py - the
code that WRITES the BPBs - and the gate reads the BPBs off the images, which
is the same independence the two have about every other fact.

`ksig.inc` IS A GENERATED INCLUDE in $(BUILD), like buildnum.inc: one line,
`KSIG_OFF equ N`, which boot/boot2.asm includes. Any tool that re-assembles the
kernel with `-I $(BUILD)/` therefore gets the number the build chose and stays
byte-identical, with no define for anybody to forget. `seed` writes a
placeholder (0) when the file is missing, for pass 1, whose blob is never
shipped; `choose` writes the real one between the passes.
"""
import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import os88disk                                          # noqa: E402

KERNEL_SEG = 0x0060             # kernel.asm's: where stage 2 lands the image
SECTOR = 512


def fail(msg):
    sys.exit("os88ksig: " + msg)


def geometry(spec):
    """'360:spc=1' / '720:fatcap=2' / '1440' -> (name, data_lba, spt, heads),
    by the same arithmetic os88disk.py lays the volume out with. KERNEL.SYS is
    allocated first and contiguously from cluster 2 (os88disk.py's system-disk
    note), so its first sector IS the data area's."""
    size, _, opt = spec.partition(":")
    try:
        size = int(size)
        spt, heads, tot, spc, fatsz, root_ent, _media = os88disk.GEOMETRY[size]
    except (ValueError, KeyError):
        fail("--geom %r: a size os88disk.py knows (%s)"
             % (spec, ", ".join(map(str, sorted(os88disk.GEOMETRY)))))
    if opt:
        k, _, v = opt.partition("=")
        if k == "spc":
            spc, fatsz = os88disk.spcfat(size, int(v))
        elif k == "fatcap":
            spc, fatsz = os88disk.fatcap(size, int(v))
        else:
            fail("--geom %r: the option is spc=N or fatcap=N" % spec)
    lay = os88disk.Layout(spc, 1, 2, root_ent, tot, fatsz)
    return spec, lay.data_lba, spt, heads


def runs(first, count, dest, bound):
    """[(lba, n)] as read_run cuts `count` sectors from LBA `first` into
    paragraph `dest`: the run bound (`bound` sectors, counted from LBA 0, so
    a cylinder or a track boundary), what is left, and the 64KB DMA page."""
    out = []
    lba = first
    while count > 0:
        n = min(bound - lba % bound, count)
        room = 0x10000 - ((dest * 16) & 0xFFFF)      # bytes to the page end
        n = min(n, room // SECTOR)
        out.append((lba, n))
        lba += n
        count -= n
        dest += n * (SECTOR // 16)
    return out


class Arm:
    """One build arm's load, on one geometry at a time."""

    def __init__(self, blob, kz=None, plain=None, head_bound="track"):
        if blob % SECTOR:
            fail("--blob %d is not whole sectors" % blob)
        self.bsecs = blob // SECTOR
        self.kz = kz                    # {ksecs, rpara, headsecs} or None
        self.plain = plain              # NOKZIP: sectors past the blob, or None
        self.head_bound = head_bound

    def walk(self, data_lba, spt, heads):
        """{'head': [...], 'tail': [...]} or {'plain': [...]}, in LBAs."""
        cyl = spt * heads
        lba0 = data_lba + self.bsecs
        if self.kz is None:
            # the whole file past the blob in one plain read; its length is
            # only a bound on the END, so an unknown one reads to the disk's
            # 64KB of KERNEL_SEG at most (the canary's own limit below)
            n = self.plain if self.plain else 0x10000 // SECTOR
            return {"plain": runs(lba0, n, KERNEL_SEG, cyl)}
        h = self.kz["headsecs"]
        hb = spt if self.head_bound == "track" else cyl
        head = runs(lba0, h, KERNEL_SEG, hb)
        tail = runs(lba0 + h, self.kz["ksecs"] - h,
                    KERNEL_SEG + h * (SECTOR // 16) + self.kz["rpara"], cyl)
        return {"head": head, "tail": tail}

    def live(self, fsec, data_lba, spt, heads):
        """Is FILE sector `fsec` read after a head flip, in a run that began
        before it? A run never crosses a cylinder, so a track boundary
        strictly after its start and at or before the sector is a flip."""
        c = data_lba + fsec
        w = self.walk(data_lba, spt, heads)
        for part in ("tail", "plain"):
            for lba, n in w.get(part, ()):
                if lba <= c < lba + n:
                    return c // spt != lba // spt
        return False

    def legal(self, fsec, length=None):
        """Rule 1, as boot2.asm's three %errors state it, in FILE sectors."""
        mem = (fsec - self.bsecs) * SECTOR
        if self.kz is not None:
            if mem < self.kz["headsecs"] * SECTOR:
                return False                    # the plain head
            if mem + self.kz["rpara"] * 16 + SECTOR > 0x10000:
                return False                    # out of the handoff's ES
            if mem + SECTOR > self.kz["ksecs"] * SECTOR:
                return False                    # past what stage 2 reads
        else:
            if mem < 0 or mem + SECTOR > 0x10000:
                return False
        if length is not None and (fsec + 1) * SECTOR > length:
            return False
        return True


def ssum(b):
    return sum(int.from_bytes(b[i:i + 2], "little")
               for i in range(0, SECTOR, 2)) & 0xFFFF


def distinct(data, fsec):
    """Rule 3: the sector's sum differs from each neighbour that exists."""
    o = fsec * SECTOR
    if len(data) < o + SECTOR:
        return False
    s = ssum(data[o:o + SECTOR])
    for step in (-SECTOR, SECTOR):
        p = o + step
        if 0 <= p and p + SECTOR <= len(data) and ssum(data[p:p + SECTOR]) == s:
            return False
    return True


def kzdefs(text):
    n = {}
    for k, key in (("KZ_SECS", "ksecs"), ("KZ_RPARA", "rpara"),
                   ("KZ_HEADSEC", "headsecs")):
        m = re.search(r"-D%s=(\d+)" % k, text)
        if not m:
            fail("--kz %r carries no -D%s" % (text, k))
        n[key] = int(m.group(1))
    return n


def candidates(arm, geoms, data=None):
    """Every FILE sector meeting rules 1-3 on every geometry, lowest first,
    and {geometry: its own live sectors} for a refusal that names one."""
    top = (len(data) // SECTOR) if data is not None else \
        arm.bsecs + 0x10000 // SECTOR
    per = {}
    for g, d, spt, heads in geoms:
        per[g] = {f for f in range(arm.bsecs, top)
                  if arm.legal(f, len(data) if data is not None else None)
                  and arm.live(f, d, spt, heads)}
    common = set.intersection(*per.values()) if per else set()
    if data is not None:
        common = {f for f in common if distinct(data, f)}
    return sorted(common), per


def write_inc(path, off, why):
    # through a rename, so an assembly running beside the build never reads
    # half a file
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        f.write("; GENERATED by tools/os88ksig.py - not tracked, not edited by "
                "hand.\n; SPEC.md 18.93.1's canary: a MEMORY offset from "
                "KERNEL_SEG, %s.\nKSIG_OFF equ %d\n" % (why, off))
    os.replace(tmp, path)


def read_inc(path):
    try:
        m = re.search(r"^KSIG_OFF\s+equ\s+(\d+)", open(path).read(), re.M)
    except OSError:
        m = None
    if not m:
        fail("%s holds no `KSIG_OFF equ` - `choose` writes it" % path)
    return int(m.group(1))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("cmd", choices=("choose", "verify", "seed", "band", "head"))
    ap.add_argument("--blob", type=int, help="BOOT2_PAD of this arm")
    ap.add_argument("--geom", action="append", default=[],
                    help="a shipped system disk of this arm: SIZE[:spc=N|"
                         ":fatcap=N], as its os88disk.py line builds it")
    ap.add_argument("--kz", help="os88kz.py --defines' line (KZIP)")
    ap.add_argument("--json", help="kernel.kz.json (verify, KZIP); `verify` "
                    "also records ksig_off in it")
    ap.add_argument("--plain", action="store_true", help="NOKZIP")
    ap.add_argument("--file", help="the packed KERNEL.SYS (or pass 1's kz1)")
    ap.add_argument("--inc", help="the generated include")
    ap.add_argument("--map", help="head: pass 1's kernel.map")
    ap.add_argument("--min", type=int, default=0,
                    help="head: KZ_HEADMIN, the Makefile's floor in sectors")
    ap.add_argument("--head-bound", choices=("track", "cylinder"),
                    default="track")
    a = ap.parse_args()

    if a.cmd == "head":
        # THE PLAIN HEAD'S LENGTH IN BYTES, for os88kz.py --head (the big
        # review's boot-4): max(SPL_RESIDENT, KZ_HEADMIN), SPL_RESIDENT read
        # off the map pass 1 just wrote - kernel.asm's foot derives it from
        # spw_resident_end and [vid_cgamode], and the map lists every
        # absolute equ
        m = re.search(r"^([0-9A-F]+)\s+SPL_RESIDENT\s*$",
                      open(a.map, errors="replace").read(), re.M)
        if not m:
            fail("%s lists no SPL_RESIDENT - kernel.asm's foot derives it, so "
                 "this is not a kernel map or not this tree's" % a.map)
        print(max(int(m.group(1), 16), a.min) * SECTOR)
        return 0
    if a.cmd == "seed":
        if not os.path.exists(a.inc):
            write_inc(a.inc, 0, "a PASS-1 PLACEHOLDER (its blob is never "
                      "shipped; `choose` writes the real one)")
        return 0
    if a.blob is None or not a.geom:
        fail("%s needs --blob and at least one --geom" % a.cmd)
    geoms = [geometry(s) for s in a.geom]
    data = open(a.file, "rb").read() if a.file else None
    if a.plain:
        plain = None if data is None else \
            (len(data) - a.blob + SECTOR - 1) // SECTOR
        arm, alt = Arm(a.blob, plain=plain), None
    else:
        if a.json:
            j = json.load(open(a.json))
            kz = {k: j[k] for k in ("ksecs", "rpara", "headsecs")}
        elif a.kz:
            kz = kzdefs(a.kz)
        else:
            fail("%s needs --kz (or --json) or --plain" % a.cmd)
        arm = Arm(a.blob, kz=kz, head_bound=a.head_bound)
        alt = Arm(a.blob, kz=kz, head_bound="cylinder"
                  if a.head_bound == "track" else "track")

    if a.cmd == "band":
        got, per = candidates(arm, geoms, data)
        for g, *_ in geoms:
            print("%-14s live file sectors: %s" % (g, compress(per[g])))
        print("common%s: %s" % (" and distinct" if data else "",
                                compress(set(got))))
        return 0

    if a.cmd == "choose":
        got, per = candidates(arm, geoms, data)
        if alt is not None and candidates(alt, geoms, data)[0] != got:
            fail("the head's run bound moved the tail's band - the model "
                 "says it cannot (the tail starts where the head ENDS), so "
                 "this file is wrong; fix it before trusting either answer")
        if not got:
            empty = [g for g, *_ in geoms if not per[g]]
            fail("NO SECTOR QUALIFIES on this arm's disks (blob %d sectors, "
                 "%s) - %s. The canary would be INERT, and the build stops "
                 "rather than ship one (SPEC.md 18.93.1): the band is the "
                 "intersection over every disk below, so the answer is a "
                 "geometry, a blob length or a head decision, not a looser "
                 "rule.\n%s"
                 % (arm.bsecs, "KZIP head %d, %d sectors, R %d"
                    % (arm.kz["headsecs"], arm.kz["ksecs"], arm.kz["rpara"])
                    if arm.kz else "NOKZIP",
                    ("no live sector at all on " + ", ".join(empty)) if empty
                    else "each disk has live sectors and they do not "
                    "intersect" + (" in a sector distinct from its "
                                   "neighbours" if data else ""),
                    "\n".join("  %-14s %s" % (g, compress(per[g]))
                              for g, *_ in geoms)))
        f = got[0]
        off = (f - arm.bsecs) * SECTOR
        write_inc(a.inc, off, "FILE sector %d, chosen for %s (blob %d "
                  "sectors)" % (f, " ".join(a.geom), arm.bsecs))
        print("os88ksig: KSIG_OFF %d - file sector %d, the lowest of %d "
              "qualifying on %s" % (off, f, len(got), " ".join(a.geom)))
        return 0

    # verify: the FINAL file and the number the kernel was assembled with
    off = read_inc(a.inc)
    if off % SECTOR:
        fail("KSIG_OFF %d is not a whole sector" % off)
    f = off // SECTOR + arm.bsecs
    bad = []
    if not arm.legal(f, len(data) if data is not None else None):
        bad.append("file sector %d is not one stage 2 addresses as the "
                   "canary (rule 1)" % f)
    for g, d, spt, heads in geoms:
        if not arm.live(f, d, spt, heads):
            bad.append("%s: file sector %d (LBA %d) is not read after a head "
                       "flip" % (g, f, d + f))
    if data is not None and not distinct(data, f):
        bad.append("file sector %d sums the same as a neighbour" % f)
    if bad:
        fail("the canary the kernel was built with is INERT in the file "
             "that ships:\n  " + "\n  ".join(bad) + "\n(a NOKZIP kernel "
             "chooses before it exists, so this is where it is caught; a "
             "KZIP one chose on pass 1's identical tail)")
    if a.json:
        j = json.load(open(a.json))
        j["ksig_off"] = off
        json.dump(j, open(a.json, "w"), indent=1)
    return 0


def compress(s):
    s = sorted(s)
    out, i = [], 0
    while i < len(s):
        j = i
        while j + 1 < len(s) and s[j + 1] == s[j] + 1:
            j += 1
        out.append(str(s[i]) if i == j else "%d-%d" % (s[i], s[j]))
        i = j + 1
    return ", ".join(out) or "(none)"


if __name__ == "__main__":
    sys.exit(main())
