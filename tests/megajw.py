#!/usr/bin/env python3
"""kern_mega: ONE CLAIM GETS WHAT IS FREE, after a session that fragments the
heaps - a pinned block may not sit in the middle of either (docs/plans/
KERN-MEGA-PLAN.md 14.166, SPEC.md 50.6.10).

    python3 tests/megajw.py              the row (86Box, a 1MB mr286)
    python3 tests/megajw.py --keep DIR   ...and keep the maps and pictures

The owner's words (2026-11-08): *"if someone wants to play an mp3, we don't
want to end up with '200k high, then a block, then 550k low' or somesuch, and
prevent them from loading a 600k file."* kern_mega's memory is two regions,
the pool above 1MB and the conventional heap, and OSAPI_MEM_HUGE bridges them
- so what limits the largest claim is the largest free run in EACH region
after a pack, which is decided by where the PINNED blocks stand.

This boots kern_mega on 86Box's at286_vga_sb (the owner's mr286, a Sound
Blaster) with 640K and 384K above 1MB (86Box's mem_size 1408, the 1MB AT
whose chipset puts its last 384K at 1MB; `mega_extkb` is asserted), a
scratch B: of the kern_mega arms, and runs the session that fragments both
heaps: Sheet opened (its region and claims fill the pool, taken top-down,
and spill into the conventional heap bottom-up), a WAV opened in the Audio
Player and PAUSED - a pause is a play, so both of its rings stay PINNED: the
Audio Player's look-ahead, a DMA door's pool block (KERN-MEGA-PLAN 14.160),
and SOUND.DRV's staging pool, a conventional claim with a page-safe head the
card plays out of - then Sheet closed. Then MEGAJW (tests/megajw) asks the
kernel what is free and what one claim can be given, claims exactly that,
and asks for the hold XT-MP3 would take (OSAPI_XMEM_ALLOC of every KB
OSAPI_XMEM_CAPS says is free, SPEC.md 121.6.1). It asserts:

  1. the session did fragment: a PINNED pool block (the Audio Player's ring,
     MX_FDMA | MX_FPIN) and a PINNED conventional ring (a head) stand;
  2. each heap is ONE free run: no pinned block has free KB on both sides of
     it, past a page's tail for each pin after the first (the stretch model mx_maxrun and mem_avail
     answer from, read off mx_tab and mem_tab);
  3. MEGAJW's answer is within MARGIN_KB of what is free - the pool's free KB
     cut to whole 64KB windows plus the heap's free KB less its 64KB reserve,
     the two losses the bridge has by design (SPEC.md 50.3.5) - and a claim
     of exactly it is granted, its windows read back, and one KB more is
     refused;
  4. XT-MP3's hold: ONE pool block of every free pool KB is granted;
  5. the Audio Player is still open at the end (no fault closed it).

THE FAILING CASE (docs/WRITING-TESTS.md 1): the tree before 14.166 put the
pool ring top-down, UNDER Sheet's blocks (at 105C00: 23K free below it, 217K
above), and SOUND.DRV's ring bottom-up on the fill point over Sheet's spill
(at 25800: 86K below, 474K above) - red on (2) for both, (3) 86K short and
(4) refused, the blockers' addresses and the runs named. Measured:
KERN-MEGA-PLAN 14.166's table.

It reads build/ and writes only to a temporary folder (--keep keeps it).
"""
import argparse
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
os.environ.setdefault("OS88_MEGA", "1")
os.environ.setdefault("OS88_MEGA_EMU", "86box")
import os88build  # noqa: E402
import os88marty as M  # noqa: E402
import os88ui  # noqa: E402

MACHINE = "at286_vga_sb"
MEGS = os.environ.get("OS88_MEGA_MEGS", "1.375")   # 1408K: 384K above 1MB
MARGIN_KB = 8                   # past the bridge's two designed losses
TAIL_KB = 63                    # a page's tail a SECOND page-safe pin may strand
RATE, SECS = 11025, 120         # the WAV: long enough to play through it all

MEM_MAX, MC_SIZE = 32, 10
MX_NBLK, MX_BLKSZ = 96, 10
MX_FPURGE, MX_FPIN, MX_FDMA = 4, 8, 0x80
fails = []


def check(what, ok, more=""):
    print("   %s  %s%s" % ("ok  " if ok else "FAIL", what,
                           (" - " + more) if more else ""), flush=True)
    if not ok:
        fails.append(what)
    return ok


def u16(b, i):
    return struct.unpack_from("<H", b, i)[0]


def insts(m):
    raw = m.read(m.sym("inst_tab"), 12 * 32)
    out = {}
    for i in range(12):
        r = raw[i * 32:(i + 1) * 32]
        if r[0] == 1:
            out[i] = (r[12:28].split(b"\0")[0].decode("latin-1"), u16(r, 6))
    return out


def pool(m):
    """mx_tab's blocks: (off KB, kb, owner name, flags), by offset"""
    ins = insts(m)
    xr = m.read(m.tail("mx_tab"), MX_NBLK * MX_BLKSZ)
    out = []
    for i in range(MX_NBLK):
        r = xr[i * MX_BLKSZ:(i + 1) * MX_BLKSZ]
        if not u16(r, 2):
            continue
        own, fl = r[4], r[5]
        if own < 0x80 or own & 0xE0 in (0x80, 0xA0):
            who = ins.get(own & 0x1F, ("slot %d" % (own & 0x1F),))[0]
        else:
            who = "%02X" % own
        out.append((u16(r, 0), u16(r, 2), who, fl))
    return sorted(out)


def conv(m):
    """mem_tab's claims: (base para, para, pinned, cache, head), by base,
    and the heap's ends"""
    base = u16(m.read(m.sym("mem_base"), 2), 0)
    top = u16(m.read(m.sym("mem_top"), 2), 0)
    raw = m.read(m.sym("mem_tab"), MEM_MAX * MC_SIZE)
    out = []
    for i in range(MEM_MAX):
        seg, para, own, dma, rloc = struct.unpack_from("<HHHHH", raw, i * MC_SIZE)
        if seg:
            out.append((seg, para, rloc == 0, 0xFB <= own >> 8 <= 0xFE,
                        dma & 0x3FFF, own))
    return base, top, sorted(out)


def cstretches(base, top, cl):
    """the conventional heap's stretches between pinned non-cache claims, in
    KB: what the two compaction passes can make of each (caches dissolved)"""
    walls = [c for c in cl if c[2] and not c[3]]
    out = []
    for lo in [base] + [c[0] + c[1] for c in walls]:
        hi = min([c[0] for c in walls if c[0] >= lo] + [top])
        if hi > lo:
            used = sum(c[1] for c in cl if not c[2] and not c[3] and lo <= c[0] < hi)
            out.append((lo, hi, max(0, hi - lo - used) / 64.0))
    return out


def tail(pins):
    """what page-safe placement may strand: a page's tail between two pins
    whose heads cannot share a page, so none at all with one pin standing"""
    return TAIL_KB * max(0, len(pins) - 1)


def middle(pins, st, at, size):
    """each pin with free KB on BOTH sides of it, past tail()'s allowance"""
    out = []
    for p in pins:
        lo_f = sum(f for lo, hi, f in st if hi <= at(p))
        hi_f = sum(f for lo, hi, f in st if lo >= at(p) + size(p))
        if lo_f > tail(pins) and hi_f > 0:
            out.append((p, lo_f, hi_f))
    return out


def stretches(blocks, xkb):
    """mx_maxrun's model: the stretches between pinned blocks, each its KB
    less the movable, non-cache KB that start in it - (lo, hi, free)"""
    pins = [b for b in blocks if b[3] & MX_FPIN]
    out = []
    for lo in [0] + [o + k for o, k, _, _ in pins]:
        hi = min([o for o, _, _, _ in pins if o >= lo] + [xkb])
        if hi > lo:
            used = sum(k for o, k, _, f in blocks if not f & (MX_FPIN | MX_FPURGE)
                       and lo <= o < hi)
            out.append((lo, hi, max(0, hi - lo - used)))
    return out


def show(m, label):
    xkb = u16(m.read(m.sym("xm_kb"), 2), 0)
    b = pool(m)
    print("  --- %s: the pool, %dK from 100000h" % (label, xkb))
    for o, k, who, fl in b:
        print("    %06X..%06X %4dK %-10s fl %02X%s" % (
            0x100000 + o * 1024, 0x100000 + (o + k) * 1024, k, who, fl,
            " PINNED" if fl & MX_FPIN else ""))
    st = stretches(b, xkb)
    print("    stretches: " + ", ".join("%06X..%06X %dK free" % (
        0x100000 + lo * 1024, 0x100000 + hi * 1024, f) for lo, hi, f in st))
    cb, ct, cl = conv(m)
    print("  --- %s: the conventional heap %05X..%05X" % (label, cb << 4, ct << 4))
    for c in cl:
        print("    %05X..%05X %6.1fK own %04X %s%s%s" % (
            c[0] << 4, (c[0] + c[1]) << 4, c[1] / 64.0, c[5],
            "PINNED" if c[2] else "movable", " cache" if c[3] else "",
            " head %d" % c[4] if c[4] else ""))
    print("    stretches: " + ", ".join("%05X..%05X %.1fK free" % (
        lo << 4, hi << 4, f) for lo, hi, f in cstretches(cb, ct, cl)))
    return xkb, b, st


def probe(m):
    """MEGAJW's MJ_RES, out of its image"""
    for _, (nm, sp) in sorted(insts(m).items(), reverse=True):
        if nm != "MEGAJW":
            continue
        img = m.readseg(sp, 0, 1024)
        at = img.find(b"MJWRES01")
        if at >= 0 and u16(img, at + 36) == 1:
            f = lambda o: u16(img, at + o)          # noqa: E731
            return dict(max=f(8), qcf=f(10), ccf=f(12), first=f(18) | f(20) << 16,
                        last=f(22) | f(24) << 16, caps=f(26), large=f(28),
                        total=f(30), touch=f(32), over=f(34), xhold=f(38))
    return None


def wav(path):
    data = bytes(int(128 + 90 * math.sin(2 * math.pi * 440 * i / RATE))
                 for i in range(RATE * SECS))
    open(path, "wb").write(
        b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " +
        struct.pack("<IHHIIHH", 16, 1, 1, RATE, RATE, 1, 8) + b"data" +
        struct.pack("<I", len(data)) + data)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--keep", help="a directory to keep the pictures in")
    a = ap.parse_args()
    B = lambda p: os88build.at(os.path.join("build", p))     # noqa: E731
    tmp = tempfile.mkdtemp(prefix="megajw-")
    try:
        clip = os.path.join(tmp, "SND.WAV")
        wav(clip)
        disk = os.path.join(tmp, "b.img")
        subprocess.check_call(
            [sys.executable, os.path.join(ROOT, "tools", "os88disk.py"), "-o", disk,
             "--size", "1440", clip, B("megajw.o88"), B("megak/apps/audio.o88"),
             B("megak/apps/sheet.o88"), "--folder", "SYSTEM/APPDATA"], stdout=subprocess.DEVNULL)
        os.environ["OS88_MEGA_MEGS"] = MEGS
        with os88ui.boot(B("mega.img"), apps=disk, machine=MACHINE,
                         label="megajw") as ui:
            m = ui.m
            ext = u16(m.read(m.sym("mega_extkb"), 2), 0)
            print("megajw: %sMB, %dK above 1MB" % (MEGS, ext))
            check("the machine: 384K above 1MB", ext == 384, "%dK" % ext)
            show(m, "boot")
            ui.path("B:/SHEET.O88", limit=180)
            show(m, "Sheet open")
            ui.raise_window(ui.window("Disk"))
            ui.open("SND.WAV", limit=180, win=ui.window("Disk"))
            M.guest_sleep(m, 2)
            ui.raise_window(ui.window("Audio"))
            m.key("Space")                  # PAUSED: a pause is a play
            M.guest_sleep(m, 2)
            show(m, "the Audio Player paused")
            ui.close(ui.window("Sheet"))
            for _ in range(30):             # ...until Sheet's claims are gone
                if not any(n == "SHEET" for n, _ in insts(m).values()):
                    break
                M.guest_sleep(m, 1)
            M.guest_sleep(m, 2)
            xkb, b, st = show(m, "Sheet closed, the play paused")
            ring = [x for x in b if x[2].startswith("AUDIO") and x[3] & MX_FDMA]
            cb, ct, cl = conv(m)
            crings = [c for c in cl if c[2] and c[4] and not c[3]]
            check("the session pins a block in each heap: the Audio Player's "
                  "look-ahead a PINNED DMA door pool block, and a PINNED "
                  "conventional ring", ring and ring[0][3] & MX_FPIN and crings,
                  "pool %s, conventional %s" % (ring, crings))
            free = xkb - sum(k for _, k, _, f in b if not f & MX_FPURGE)
            best = max(f for _, _, f in st) if st else 0
            pins = [x for x in b if x[3] & MX_FPIN]
            mid = middle(pins, st, lambda x: x[0], lambda x: x[1])
            check("the pool is one free run: its best stretch %dK of %dK free"
                  % (best, free), best + tail(pins) >= free and not mid,
                  "; ".join("%s %dK at %06X: %dK free under it, %dK over it" % (
                      x[2], x[1], 0x100000 + x[0] * 1024, lo, hi)
                      for x, lo, hi in mid))
            cst = cstretches(cb, ct, cl)
            ctot = sum(f for _, _, f in cst)
            cbest = max(f for _, _, f in cst) if cst else 0
            cpins = [c for c in cl if c[2] and not c[3]]
            cmid = middle(cpins, cst, lambda c: c[0], lambda c: c[1])
            check("the conventional heap is one free run: its best stretch "
                  "%.1fK of %.1fK free" % (cbest, ctot),
                  cbest + tail(cpins) >= ctot and not cmid,
                  "; ".join("own %04X %.1fK at %05X: %.1fK free under it, "
                            "%.1fK over it" % (c[5], c[1] / 64.0, c[0] << 4, lo,
                                               hi) for c, lo, hi in cmid))
            ui.raise_window(ui.window("Disk"))
            ui.open("MEGAJW.O88", limit=180, win=ui.window("Disk"))
            p = None
            for _ in range(60):
                p = probe(m)
                if p:
                    break
                M.guest_sleep(m, 1)
            print("megajw: %s" % p)
            if check("MEGAJW answered", p is not None):
                want = (p["caps"] & ~63) + max(0, p["total"] - 64)
                check("one claim of %dK: within %dK of what is free (%dK in "
                      "the pool in whole 64KB windows, %dK of the heap less "
                      "its 64KB reserve)" % (p["max"], MARGIN_KB, p["caps"] & ~63,
                                             max(0, p["total"] - 64)),
                      p["max"] + MARGIN_KB >= want, "%dK short" % (want - p["max"]))
                check("the claim of it granted, every window read back, one KB "
                      "more refused", p["ccf"] == 0 and p["touch"] == 1
                      and p["over"] == 1, "cf %d touch %d over %d" % (
                          p["ccf"], p["touch"], p["over"]))
                check("XT-MP3's hold: ONE pool block of the %dK XMEM_CAPS says "
                      "is free" % p["caps"], p["xhold"] == 1,
                      "XMEM_ALLOC %s" % {2: "REFUSED", 0: "not asked"}.get(
                          p["xhold"], p["xhold"]))
                if a.keep:
                    os.makedirs(a.keep, exist_ok=True)
                    m.shot(os.path.join(a.keep, "megajw.png"))
            check("the Audio Player still open, none of the programs closed "
                  "by a fault", any("Audio" in t for t in ui.titles()),
                  "titles %s" % ui.titles())
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("megajw: %s" % ("PASS" if not fails else "FAIL (%s)" % "; ".join(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
