#!/usr/bin/env python3
"""os88vidbuf - what a stream's read-ahead would buy, with and without XMS.

    python3 tools/os88vidbuf.py CLIP.V88 [--profile 286-speeddemon] [--media KB/s]
        [--pio MS] [--xcopy MS] [--ring K] [--xms KB ...] [--wait S ...]
        [--minrate] [--deficit KB/s ...] [--cache FILE]

AN INSTRUMENT, not a gate: docs/plans/VIDEO-XMS-PLAN.md's numbers come off
it, so they can be re-derived for any clip rather than argued about. It reads
a STREAMED .V88 (SPEC.md 98.1.4) and plays it in a model of the player's
reader (98.3) on a model of the machine, then says whether the play stalls.

THE PLAYER, as built and as VIDEO-XMS-PLAN 4 proposes it:
  - a ring of K 32 KB chunks; chunk c may be loaded while c < the hook's
    super-packet's chunk + K (vp_fill's test); a super-packet is drawable
    once every chunk it touches is in
  - with --xms, a FIFO of that many KB in extended memory AHEAD of the ring
    (the plan's "bank"): a free ring slot is filled from the bank's head
    when the bank has it (one copy DOWN), else from the disk; a full ring
    with room in the bank reads the disk into a bounce buffer and copies it
    UP. So a banked byte costs one disk read and two copies, and a byte read
    while the bank is empty costs the disk read alone - the plan's 4.2
  - the play starts once the ring and the bank are full (or the file is all
    read), or at --wait seconds, whichever comes first: that is the PREFILL

THE MACHINE, per millisecond step:
  - the decode is the hook's and runs first (it is an ISR): each frame's
    cost is the profile's own measured decode (os88venc.profile_table,
    SPEC.md 98.2.3.3) at the profile's speed
  - the reader gets what is left. A disk chunk takes the MEDIA's time and,
    on a disk the CPU copies (every AT-class controller; --pio), that much
    CPU: progress is the lesser of the two. A copy to or from XMS is CPU
    alone (--xcopy, ms a KB): int 15h AH=87h on a 286, unreal mode on a
    386 (SPEC.md 41.5). THE 286's FIGURE IS AN ESTIMATE until VIDDISK's X
    row has been run on one (VIDEO-XMS-PLAN 3.2)
  - a frame due whose super-packet is not drawable is a STALL: the clock
    waits (with a card, a pause), and the stall's length is counted

--minrate bisects the slowest MEDIA rate that plays with no stall, for each
--xms and --wait. --deficit is the ENCODER's side (VIDEO-XMS-PLAN 5): the
file's own frames are taken as what a lossless encode WANTS, a token bucket
as deep as the read-ahead is refilled at that rate, and the bytes it could
not send are summed - the share of the picture an encode at that rate would
have to leave out, before the next frames repair it.
"""
import argparse
import json
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88vid as vid                                           # noqa: E402

CHUNK = 32768
KB = 1024.0
# the player's claims besides the ring, KB: the mirror is one slot (two for
# BIGSP, 98.1.4.1), the card's ring 17 and a seek's entry 4 (98.2.1.3.1)
SND_KB, ENTRY_KB = 17, 4
# what a profile's disk costs the CPU, ms a KB, when the CPU copies it:
# 1 / the rate VIDDISK measured IDLE, which its other rows bear out (the
# rate falls as the decode's share rises, as one CPU split two ways would)
PIO = {"286-speeddemon": 1000.0 / 1318.3, "486-dx2-66": 1000.0 / 4151.0}
# the XMS copy, ms a KB: 286 MEASURED on the owner's real 286 (int 15h
# AH=87h, 11.3 ms a 32 KB call: docs/reports/VIDDISK-XMS-286-2026-10-08.md),
# 486 ESTIMATED (a dword move in unreal mode, ~20 MB/s)
XCOPY = {"286-speeddemon": 0.345, "486-dx2-66": 0.05}


def stream(r):
    """[(offset from the stream's start, bytes, first frame, frames)]"""
    out, at, n, f = [], r.sp0, r.sp0n, 0
    while n:
        nf, nxt = struct.unpack_from("<HH", r.d, at)
        out.append((at - r.sp0, n * vid.SECTOR, f, nf))
        f += nf
        at, n = at + n * vid.SECTOR, nxt
    return out


def decode_us(r, profn, cache=None):
    """each frame's decode on the profile's machine, microseconds"""
    if cache and os.path.exists(cache):
        return json.load(open(cache))
    import os88venc as venc
    prof = venc.PROFILES[profn]
    tab, sub = venc.profile_table(prof, r.g.layout)
    k = vid.HZ / 1e6 * (prof.get("speed") or 1)
    us = [vid.cycles_of(rec, planar=r.g.planes > 1, layout=r.g.layout,
                        table=tab, sub=sub, sub2=r.g.sub2,
                        wm3=venc.wm3_cost(prof)) / k
          for rec, _, _ in r.records()]
    if cache:
        json.dump(us, open(cache, "w"))
    return us


class Play:
    def __init__(self, sps, us, fps, ring, xms_kb, media, pio, xcopy,
                 wait=None, step=0.001, cache=0.0):
        self.sps, self.us, self.fps = sps, us, fps
        self.K, self.M = ring, int(xms_kb * KB) // CHUNK
        self.media, self.pio, self.xcopy = media, pio, xcopy
        self.wait, self.step, self.cache = wait, step, cache
        total = sps[-1][0] + sps[-1][1]
        self.nchunk = (total + CHUNK - 1) // CHUNK
        # each super-packet's first and last chunk
        self.c0 = [o // CHUNK for o, n, f, nf in sps]
        self.c1 = [(o + n - 1) // CHUNK for o, n, f, nf in sps]
        self.sp_of = []
        for k, (o, n, f, nf) in enumerate(sps):
            self.sp_of += [k] * nf

    def run(self):
        st = self.step
        ring_hi = 0          # chunks [0, ring_hi) have been in the ring
        bank_lo = bank_hi = 0  # the bank holds chunks [bank_lo, bank_hi)
        disk = 0             # the next chunk the disk reads
        op = None            # (kind, disk KB left, copy CPU left s)
        drv = 0.0            # KB the drive has read ahead into its cache
        hook_sp = 0
        t = 0.0
        started = False
        start = None
        nf = len(self.us)
        f = 0                # the next frame to draw
        due = None           # its time
        backlog = 0.0        # decode owed, seconds
        stalls, stall_t = 0, 0.0
        copies = disk_kb = up_kb = down_kb = 0
        lost = 0.0
        while f < nf:
            cpu = st
            # the hook: draw what is due, if it is in
            if started:
                if backlog > 0:
                    d = min(backlog, cpu)
                    backlog -= d
                    cpu -= d
                while f < nf and t >= due:
                    k = self.sp_of[f]
                    if self.c1[k] >= ring_hi:
                        stalls += 1
                        stall_t += st
                        due += st
                        lost += st
                        break
                    hook_sp = k
                    backlog += self.us[f] / 1e6
                    f += 1
                    due = start + lost + f / self.fps
                if backlog > 0 and cpu > 0:
                    d = min(backlog, cpu)
                    backlog -= d
                    cpu -= d
            lo = self.c0[hook_sp] if started else 0
            # the reader, with what is left. A disk chunk is `op[1]` KB
            # still to move: the media brings media x st KB a step into the
            # drive's own buffer (at most `cache` KB carried to the next
            # step - 0, an MFM drive with none, loses what the CPU was too
            # busy to take), and the CPU copies out what it has time for
            avail = drv + self.media * st
            drv = 0.0
            while cpu > 1e-12:
                if op is None:
                    room_ring = ring_hi < lo + self.K and \
                        ring_hi < self.nchunk
                    if room_ring and bank_lo < bank_hi:
                        op = ["down", 0.0, CHUNK / KB * self.xcopy / 1e3]
                    elif room_ring and disk < self.nchunk:
                        op = ["ring", CHUNK / KB, 0.0]
                    elif self.M and bank_hi - bank_lo < self.M and \
                            disk < self.nchunk:
                        op = ["bank", CHUNK / KB,
                              CHUNK / KB * self.xcopy / 1e3]
                    else:
                        break
                if op[1] > 0:
                    per = self.pio / 1e3                # s of CPU a KB
                    kb = min(op[1], avail, cpu / per if per > 0 else op[1])
                    op[1] -= kb
                    avail -= kb
                    cpu -= kb * per
                    if op[1] > 1e-9:
                        break                   # the media or the CPU binds
                    op[1] = 0.0
                    disk += 1
                    disk_kb += CHUNK / KB
                    if op[0] == "ring":
                        ring_hi += 1
                        bank_lo = bank_hi = disk
                        op = None
                        continue
                # a copy, up or down: CPU alone
                d = min(op[2], cpu)
                op[2] -= d
                cpu -= d
                if op[2] > 1e-12:
                    break
                if op[0] == "down":
                    ring_hi += 1
                    bank_lo += 1
                    down_kb += CHUNK / KB
                else:
                    bank_hi += 1
                    up_kb += CHUNK / KB
                op = None
            if disk < self.nchunk:
                drv = min(self.cache, avail)
            t += st
            if not started:
                full = ring_hi >= min(self.K, self.nchunk) and (
                    not self.M or disk >= self.nchunk or
                    bank_hi - bank_lo >= self.M)
                if full or (self.wait is not None and t >= self.wait):
                    started, start = True, t
                    due = start
        return dict(start=start, stalls=stalls, stall_t=stall_t,
                    disk_kb=disk_kb, up_kb=up_kb, down_kb=down_kb,
                    play=t - start)


def minrate(sps, us, fps, ring, xms, pio, xcopy, wait, tol, cache):
    """the slowest media rate, KB/s, whose play stalls for no more than
    `tol` seconds in all; None when no rate does - the CPU binds"""
    lo, hi = 20.0, 20000.0
    if Play(sps, us, fps, ring, xms, hi, pio, xcopy, wait,
            cache=cache).run()["stall_t"] > tol:
        return None
    for _ in range(18):
        mid = (lo * hi) ** 0.5
        if Play(sps, us, fps, ring, xms, mid, pio, xcopy, wait,
                cache=cache).run()["stall_t"] > tol:
            lo = mid
        else:
            hi = mid
    return hi


def deficit(sps, fps, depth_kb, rate, prefill):
    """bytes a token bucket `depth_kb` deep at `rate` KB/s could not send,
    the file's own super-packets taken as what the encode wanted; the
    bucket starts full with `prefill`, half full without"""
    depth = depth_kb * KB
    tok = depth if prefill else depth / 2
    short = 0.0
    want = 0.0
    last = 0.0
    for o, n, f, nf in sps:
        t = f / fps
        tok = min(depth, tok + (t - last) * rate * KB)
        last = t
        want += n
        if n <= tok:
            tok -= n
        else:
            short += n - tok
            tok = 0.0
    return short / want


def profile_name(v):
    """an old profile name (286-vga, 486...) as its new one"""
    import os88venc as venc
    return venc.PROFILE_RENAMED.get(v, v)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("clip")
    ap.add_argument("--profile", default="286-speeddemon",
                    type=profile_name,
                    help="the machine whose decode prices each frame (an "
                         "old name - 286-vga, 486 - is taken as its new)")
    ap.add_argument("--media", type=float, default=None,
                    help="the disk's own rate, KB/s (default: the "
                         "profile's idle VIDDISK row)")
    ap.add_argument("--pio", type=float, default=None,
                    help="CPU a disk KB costs, ms (default: the profile's)")
    ap.add_argument("--xcopy", type=float, default=None,
                    help="CPU an XMS KB costs, ms (default: the profile's)")
    ap.add_argument("--dcache", type=float, default=0.0,
                    help="KB the DRIVE reads ahead on its own while the "
                         "CPU is busy (0: an MFM drive; a 1990s IDE drive "
                         "or a CF card: 32 to 256)")
    ap.add_argument("--ring", type=int, default=10,
                    help="ring slots (a 640 KB VGA machine with a card: 10)")
    ap.add_argument("--xms", type=float, nargs="*",
                    default=[0, 1024, 2048, 4096, 8192, 15360])
    ap.add_argument("--wait", nargs="*", default=["full"],
                    help="the most the prefill may take, s, or `full`: "
                         "until the ring and the bank are full")
    ap.add_argument("--minrate", action="store_true")
    ap.add_argument("--tol", type=float, default=0.1,
                    help="stall seconds a play may total and still pass")
    ap.add_argument("--deficit", type=float, nargs="*", default=None,
                    help="KB/s an encode would be budgeted at")
    ap.add_argument("--cache", default=None,
                    help="a JSON file to keep the per-frame decode in")
    a = ap.parse_args()
    r = vid.Reader(a.clip)
    if r.resident:
        sys.exit("a RESIDENT file has no stream")
    sps = stream(r)
    fps = r.fps
    total = sps[-1][0] + sps[-1][1]
    secs = r.frames / fps
    print("%s: %d frames at %.3f fps, %.1f s; stream %.1f MB, mean %.0f "
          "KB/s; header ring %d" % (os.path.basename(a.clip), r.frames, fps,
                                   secs, total / KB / KB, total / KB / secs,
                                   r.ring))
    if a.deficit:
        ring_res = (a.ring - 2) * 32
        print("\nENCODER: share of the lossless want an encode at R could "
              "not send (bucket = ring less two slots + XMS)")
        print("%-10s" % "R KB/s" + "".join(
            "%20s" % ("+%dK XMS" % x if x else "ring %dK" % ring_res)
            for x in a.xms))
        for R in a.deficit:
            row = "%-10d" % R
            for x in a.xms:
                d0 = deficit(sps, fps, ring_res + x, R, False)
                d1 = deficit(sps, fps, ring_res + x, R, True) if x else d0
                row += "%20s" % ("%.1f%%/%.1f%% %ds" % (
                    100 * d0, 100 * d1, (ring_res + x) / R)
                    if x else "%.1f%%" % (100 * d0))
            print(row)
        print("(with XMS: started half full / started FULL, and the wait "
              "that full prefill takes at R)")
    if not a.minrate:
        return
    us = decode_us(r, a.profile, a.cache)
    pio = a.pio if a.pio is not None else PIO.get(a.profile, 0.0)
    xc = a.xcopy if a.xcopy is not None else XCOPY.get(a.profile, 0.4)
    print("\nPLAYER on %s: decode the profile's, disk %.3f ms/KB of CPU, "
          "XMS copy %.3f ms/KB; ring %d slots; drive cache %d KB"
          % (a.profile, pio, xc, a.ring, a.dcache))
    for w in [None if x == "full" else float(x) for x in a.wait]:
        for x in a.xms:
            R = minrate(sps, us, fps, a.ring, x, pio, xc, w, a.tol, a.dcache)
            if R is None:
                p = Play(sps, us, fps, a.ring, x, 20000.0, pio, xc,
                         w, cache=a.dcache).run()
                print("  xms %6d KB  wait %-5s: stalls %.1f s at any disk "
                      "rate (the CPU binds)" % (x, w, p["stall_t"]))
                continue
            p = Play(sps, us, fps, a.ring, x, R, pio, xc, w,
                     cache=a.dcache).run()
            print("  xms %6d KB  wait %-5s: slowest disk %5.0f KB/s, "
                  "prefill %5.1f s, %.0f%% of the stream through XMS"
                  % (x, w, R, p["start"], 100 * p["up_kb"] * KB / total))


if __name__ == "__main__":
    main()
