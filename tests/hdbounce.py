#!/usr/bin/env python3
"""kern_mega: a file read off a FIXED DISK into a buffer above 1MB costs the
disk what a conventional buffer does (KERN-MEGA-PLAN 14.167, SPEC.md 51.8.1).

    OS88_MEGA=1 OS88_MEGA_EMU=86box python3 tests/hdbounce.py [--leg L ...]
        [--machine at286_vga_hdd] [--times N] [--no-count] [--keep]

Boots kern_mega's own kernel.sys off a 16-head, 63-sector IDE disk on 86Box
(the mr286 profile, MR BIOS's type 47) with tests/megard's MEGARD and
BIG.DAT on C:, launches MEGARD from C: and has it read BIG.DAT - 120
sectors, one contiguous run - into its OS88_HIGHMEM pool claim (above 1MB:
PiXEL's, Audio's and the typeface buffers' shape) and into a conventional
claim, on command, each read timed by the guest's PIT. Then again with two
breakpoints armed, which count what each read cost the disk:

  drv_blk_call_x   every DSV_BLK call into a driver, with its CX sectors
  mega_fd13        every int 13h in protected mode (the gate's own first
                   step), a transfer (AH 02h/03h) on a fixed disk counted
                   with its AL sectors - each one REFLECTED to the ROM, on
                   a 286 a CPU reset

THE LEGS are the three transports C: can have (SPEC.md 52.1):

  bios  HDD.DRV left off the disk: C: is the kernel's own BIOS row
        (DVK_BIOS), every transfer the kernel's int 13h
  ide   HDD.DRV on it, as shipped: the BIOS sniff loads it (51.3.2) and
        52.1.1 TAKES C: onto rung 1, the task file, `rep insw`
  rom   HDD.DRV with its one device row put on rung 0 (HDD_KIND = BIOS)
        after the take: the driver's own int 13h
  shed  the bios leg with the read-ahead's LANDING shed first - MEGARD
        claims conventional memory until the heap refuses and frees it all
        - so the read finds [dsk_rah_bnc] 0

THE BOUNDS, per leg, are the row's assertion:

  ide   each read - the pool block's and the conventional claim's - is at
        most 8 driver calls for its 120 sectors (the data is ONE call; the
        rest are the directory and the FAT), the high read no more than the
        low, and the high read reflects no int 13h (rung 1 straight into
        the selector)
  rom   the high read's int 13h calls carry DSK_BNC_SECS sectors each, but
        at a track or the request's end (the landing, not dsk_secbuf)
  bios  the same, through the kernel's own int 13h
  shed  the same, and the landing back after the read

`--big` times kern_big's legs on the same machine (no counts: the bar), and
OS88_MEGA_EMU=bochs runs the four under Bochs's 386 and ATA disk.

THE FAILING CASE (docs/WRITING-TESTS.md 1), measured on the tree before
14.167, where dsk_xfer's .drvhi moved a sector a call through dsk_secbuf for
every LDT selector - and since MC7.4 EVERY claim is one, a conventional
claim's too - and the BIOS bounce's landing was nine sectors and came back
only at a mount: ide 122 one-sector driver calls for EACH read, rom 122
one-sector int 13h, bios twelve runs of 9, shed 115 one-sector int 13h and
the landing still 0 - all four red. HDD.DRV's DSV_CAPS put back to 0 turns
ide alone red: 9 driver calls a read, the kernel's run bounce of 18.
"""
import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
BIG = "--big" in sys.argv      # kern_big on the same machine: the BAR
if BIG:
    os.environ.pop("OS88_MEGA", None)
    os.environ.pop("OS88_MEGA_EMU", None)
    os.environ.setdefault("OS88_EMU", "86box")
else:
    os.environ.setdefault("OS88_MEGA", "1")
    os.environ.setdefault("OS88_MEGA_EMU", "86box")
import os88marty, os88ui, os88build, os88geom as geom  # noqa: E402

KB = "build/" if BIG else "build/megak/"
SECTORS = 120                   # BIG.DAT: 60KB, fits a 64KB claim
BNC = int((re.search(r"^DSK_BNC_SECS equ (\d+)",     # the landing, as
                     open(os.path.join(ROOT, "kernel", "disk.inc")).read(),
                     re.M) or [0, 18])[1])   # kernel/disk.inc builds it
HDD_KIND, HDK_BIOS = 0, 1       # drivers/hdd/hddabi.inc
DV_KIND, DV_SIZE, DVK_BIOS, DVK_DRV = 0, 16, 0, 1
DRVR_SEG, DRVR_SIZE = 2, 16     # kernel/driver.inc


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def syms_of(src, incs, out, defs=()):
    """(symbols, image) of an org-0 nasm source, assembled into OUT"""
    mp = os.path.join(out, os.path.basename(src) + ".map")
    bp = os.path.join(out, os.path.basename(src) + ".bin")
    cp = os.path.join(out, os.path.basename(src) + ".asm")
    with open(src) as f, open(cp, "w") as g:
        g.write(f.read() + "\n[map symbols %s]\n" % mp)
    cmd = ["nasm", "-f", "bin", "-w+error"]
    for i in incs:
        cmd += ["-I", os.path.join(ROOT, i)]
    cmd += ["-D" + d for d in defs]
    subprocess.run(cmd + ["-o", bp, cp], check=True, cwd=ROOT)
    syms = {}
    for ln in open(mp):
        f = ln.split()
        if len(f) == 3 and all(c in "0123456789ABCDEF" for c in f[0]):
            syms[f[2]] = int(f[0], 16)
    return syms, bp


def big_dat():
    return bytes((i * 7 + (i >> 9)) & 0xFF for i in range(SECTORS * 512))


def leg(name, a, tmp, msyms, mbin, hsyms):
    bad = []
    vhd = os.path.join(tmp, name + ".vhd")
    pkg = os.path.join(tmp, "megard.o88")
    subprocess.run([sys.executable, "tools/os88pkg.py", mbin, "-o", pkg],
                   check=True, capture_output=True, cwd=ROOT)
    big = os.path.join(tmp, "BIG.DAT")
    data = big_dat()
    with open(big, "wb") as f:
        f.write(data)
    files = ["MEGARD.O88=" + pkg, "BIG.DAT=" + big,
             "CTRL.DRV=" + os88build.at(KB + "ctrl.drv")]
    if name != "bios" and name != "shed":
        files.append("HDD.DRV=" + os88build.at("build/hdd.drv"))
    cmd = [sys.executable, "tools/os88hdd.py", "--out", vhd,
           "--spt", "63", "--heads", "16", "--cyls", "65",
           "--kernel", os88build.at(KB + "kernel.sys"),
           "--vbr", os88build.at(KB + "boothd.bin"),
           "--mbr", os88build.at("build/mbr.bin")]
    for f in files:
        cmd += ["--file", f]
    subprocess.run(cmd, check=True, capture_output=True, cwd=ROOT)
    m = os88marty.launch(None, machine=a.machine, boot=False,
                         extra=["--mount", "hd:0:" + vhd])
    out = {"leg": name}
    try:
        m.run()
        ui = os88ui.UI(m)
        ui.up(limit=240)
        os88marty.no_saver(m)
        S = ui._S
        # --- C:'s transport, read off the volume table and the driver ---
        vt = m.read(S("dsk_vtab"), 8 * DV_SIZE)
        crow = None
        for i in range(8):
            r = vt[i * DV_SIZE:(i + 1) * DV_SIZE]
            if r[0] in (DVK_BIOS, DVK_DRV) and (r[1] & 0x80 or r[0] == DVK_DRV) and i >= 2:
                crow = (i, r[0], r[1], r[14])
                break
        out["c"] = crow
        hseg = u16(m.read(S("drv_tab") + 1 * DRVR_SIZE + DRVR_SEG, 2))
        out["hdd_loaded"] = bool(hseg)
        if hseg:
            hbase = m.lin(hseg)
            dev0 = m.read(hbase + hsyms["hd_devs"], 12)
            out["dev0"] = dev0.hex()
            if name == "rom":
                m.write(hbase + hsyms["hd_devs"] + HDD_KIND,
                        bytes([HDK_BIOS]))
        print("   %s: C: row %s, HDD.DRV %s, device 0 %s" %
              (name, crow, "loaded" if hseg else "absent",
               out.get("dev0")))
        want_kind = DVK_BIOS if name in ("bios", "shed") else DVK_DRV
        if not crow or crow[1] != want_kind:
            bad.append("%s: C: is not a %s volume (%s)" %
                       (name, "BIOS" if want_kind == DVK_BIOS else "driver",
                        crow))
        # --- MEGARD, from C: ---
        try:
            ui.path("C:/MEGARD.O88", limit=30)
        except Exception as e:      # no window: the launch is what we want
            print("   (launch: %s)" % str(e)[:80])
        # MEGARD has no window and does its work in its ENTRY PROC, so it
        # is not yet a live instance: it is found where the loader put it -
        # its header ('O8', the name at +16) on a paragraph of conventional
        # memory (it is not OS88_HIGH)
        base = None
        end = time.time() + 120
        while time.time() < end and base is None:
            low = m.read(0, 0xA0000)
            i = low.find(b"MEGARD\0")
            cands = []
            while i >= 0:
                h = i - 16
                if h >= 0 and h % 16 == 0 and low[h:h + 2] == b"O8":
                    cands.append(h)
                i = low.find(b"MEGARD\0", i + 1)
            ldb = m.lin(u16(m.read(S("ld_base"), 2)))
            if ldb in cands:            # the image the loader is running,
                base = ldb              # not the file's bytes in a buffer
            if base is None:
                time.sleep(0.5)
        if base is None:
            bad.append("%s: MEGARD never started" % name)
            return out, bad
        rb = lambda n: m.read(base + msyms[n], 1)[0]
        rw = lambda n: u16(m.read(base + msyms[n], 2))
        os88marty.until(m, lambda mm: rb("mr_state") != 0, "MEGARD's claims",
                        poll=0.05, limit=60.0, guest=10.0)
        if rb("mr_state") != 1:
            bad.append("%s: MEGARD's claims were refused" % name)
            return out, bad
        hsel, lsel = rw("mr_hi"), rw("mr_lo")
        out["hi"] = "%04X -> %06X" % (hsel, m.lin(hsel))
        out["lo"] = "%04X -> %06X" % (lsel, m.lin(lsel))
        print("   %s: pool block %s, conventional %s" %
              (name, out["hi"], out["lo"]))
        if not BIG and m.lin(hsel) < 0x100000:
            bad.append("%s: MEGARD's OS88_HIGHMEM claim is not above 1MB"
                       % name)
        want_sum = sum(data) & 0xFFFF

        def cmd(c):
            s0 = rb("mr_seq")
            m.write(base + msyms["mr_cmd"], bytes([c]))
            os88marty.until(m, lambda mm: rb("mr_seq") != s0, "read %d" % c,
                            poll=0.02, limit=300.0, guest=60.0)
            t = struct.unpack("<I", m.read(base + msyms["mr_t"], 4))[0]
            ok = (rb("mr_cf") == 0 and rw("mr_len") == len(data)
                  and rw("mr_sum") == want_sum)
            if not ok:
                bad.append("%s: read %d: CF %d err %04X len %d sum %04X "
                           "(want %d, %04X)" % (name, c, rb("mr_cf"),
                                                rw("mr_err"), rw("mr_len"),
                                                rw("mr_sum"), len(data),
                                                want_sum))
            return t * 0.838e-3     # ms

        def squeeze():
            s0 = rb("mr_seq")
            m.write(base + msyms["mr_cmd"], b"\x03")
            os88marty.until(m, lambda mm: rb("mr_seq") != s0, "the squeeze",
                            poll=0.02, limit=120.0, guest=30.0)
            return u16(m.read(S("dsk_rah_bnc"), 2))

        # --- timed, unarmed ---
        times = {1: [], 2: []}
        for _ in range(a.times):
            for c in (1, 2):
                if name == "shed":  # the landing shed before every read
                    squeeze()
                times[c].append(cmd(c))
        out["ms_hi"] = min(times[1])
        out["ms_lo"] = min(times[2])
        print("   %s: high %.1f ms, conventional %.1f ms (best of %d; "
              "%.0f / %.0f KB/s)" %
              (name, out["ms_hi"], out["ms_lo"], a.times,
               60000 / out["ms_hi"], 60000 / out["ms_lo"]))
        if a.no_count or BIG:
            m.write(base + msyms["mr_cmd"], b"\xff")
            return out, bad
        # --- counted ---
        k_drv = os88marty_sym(m, "drv_blk_call_x")
        k_13 = os88marty_sym(m, "mega_fd13")

        def counted(c):
            calls = []
            m.breakpoints([{"type": "exec", "addr": k_drv},
                           {"type": "exec", "addr": k_13}])
            s0 = rb("mr_seq")
            m.write(base + msyms["mr_cmd"], bytes([c]))
            m.run()
            end = time.time() + 600
            while time.time() < end:
                if m.wait_stop(limit=2.0):
                    ip = m.status().get("flat_ip")
                    r = m.regs()
                    if ip == k_drv:
                        calls.append(("drv", r["cx"]))
                    elif ip == k_13:
                        fr = m.read(m.lin(r["ss"], r["bp"]) + 14, 6)
                        ah, al, dl = fr[5], fr[4], fr[0]
                        if ah in (2, 3) and dl & 0x80:
                            calls.append(("i13", al))
                    m.run()
                    continue
                if rb("mr_seq") != s0:
                    break
            m.breakpoints([])
            m.run()
            return calls

        if name == "shed":
            out["land_before"] = squeeze()
        hi = counted(1)
        out["land_after"] = u16(m.read(S("dsk_rah_bnc"), 2))
        lo = counted(2)
        for tag, cs in (("hi", hi), ("lo", lo)):
            d = [n for k, n in cs if k == "drv"]
            i = [n for k, n in cs if k == "i13"]
            out[tag + "_drv"] = d
            out[tag + "_i13"] = i
            print("   %s %s: %d driver calls %s, %d int 13h %s" %
                  (name, tag, len(d), d[:12], len(i), i[:12]))
        if name == "ide":
            # 120 sectors, one contiguous run: a call or two for the data
            # (a 64KB claim's offset never wraps) and a few for the FAT and
            # the directory - where a sector a call is 120 and more
            for tag in ("hi", "lo"):
                if len(out[tag + "_drv"]) > 8:
                    bad.append("ide: the %s read made %d driver calls for "
                               "%d sectors" % (tag, len(out[tag + "_drv"]),
                                               SECTORS))
            if len(out["hi_drv"]) > len(out["lo_drv"]):
                bad.append("ide: the high read made %d driver calls, the "
                           "conventional one %d" % (len(out["hi_drv"]),
                                                    len(out["lo_drv"])))
            if out["hi_i13"]:
                bad.append("ide: the high read reflected %d int 13h"
                           % len(out["hi_i13"]))
        else:
            runs = out["hi_i13"]
            short = [n for n in runs[:-1] if n < BNC]
            # a run may end at a TRACK as well (63 sectors here), so a short
            # call is allowed only where the track or the request ends
            if not runs or len(short) > (SECTORS + 62) // 63 + 1:
                bad.append("%s: the high read's int 13h calls carried %s "
                           "sectors - runs under %d" % (name, runs, BNC))
        if name == "shed":
            print("   shed: landing before the read %04X, after %04X" %
                  (out["land_before"], out["land_after"]))
            if out["land_before"] != 0:
                bad.append("shed: the squeeze did not shed the landing")
            if out["land_after"] == 0:
                bad.append("shed: the landing was not taken back")
        m.write(base + msyms["mr_cmd"], b"\xff")
        return out, bad
    finally:
        try:
            m.close()
        except Exception:
            pass


def os88marty_sym(m, name):
    return m.sym(name)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--leg", action="append",
                    choices=("bios", "ide", "rom", "shed"))
    ap.add_argument("--machine", default="at286_vga_hdd")
    ap.add_argument("--times", type=int, default=3)
    ap.add_argument("--no-count", action="store_true")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--big", action="store_true",
                    help="kern_big on the same machine, timed only: the bar")
    a = ap.parse_args()
    legs = a.leg or ["ide", "rom", "bios", "shed"]
    os.chdir(ROOT)
    t0 = time.time()
    bad = []
    tmp = tempfile.mkdtemp(prefix="hdbounce-", dir=os.path.join(ROOT,
                                                               "build"))
    try:
        msyms, mbin = syms_of("tests/megard/megard.asm", ("apps/",), tmp)
        tool = os.path.getsize(os88build.at("build/hddtool.bin"))
        hsyms, hbin = syms_of("drivers/hdd/hdd.asm",
                              ("drivers/hdd/", "drivers/", "apps/", "build/"),
                              tmp, ("HDTOOL_PARA=%d" % ((tool + 15) // 16),))
        if open(hbin, "rb").read() != open(os88build.at("build/hdd.bin"),
                                           "rb").read():
            sys.exit("hdbounce: build/hdd.bin is behind the tree - run `make`")
        for L in legs:
            o, b = leg(L, a, tmp, msyms, mbin, hsyms)
            bad += b
    finally:
        if not a.keep:
            shutil.rmtree(tmp, ignore_errors=True)
    print("hdbounce: %.0fs" % (time.time() - t0))
    for b in bad:
        print("FAIL: " + b)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
