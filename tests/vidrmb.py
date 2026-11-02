#!/usr/bin/env python3
"""vidrmb - what a REAL-MODE BRACKET reads (SPEC.md 53.2.3,
docs/plans/MEGA-VIDEO-REALMODE-PLAN.md W3).

    make mega megarun && python3 tests/vidrmb.py [--hdd | --box86] [--keep]

The Video Player's real-mode bracket (W5) streams its file through
OSAPI_FILE_READ_SEQ and banks it in extended memory through OSAPI_XMEM_COPY,
and on a hard disk HDD.DRV reads rung 1 straight into XMS (HDV_ARM). None of
that is in protected mode's reach once the bracket has left it: the door
walks each listed file into an EXTENT LIST, the twins read it by int 13h or
through a LOW COPY of the block driver, and XMEM_COPY moves by LOADALL (a
286) or a loaded base (a 386). tests/megarmb/megarmbx.asm's MEGARMBX does
each of those inside a real-mode bracket and checks every dword of every
file against its offset (plus the file's salt), both in the conventional
buffer READ_SEQ filled and read back out of XMS. Its legs, and what is
asserted of each (a record per leg in its bss):

  C  RMBXC.DAT (197,608 bytes) in its own folder, REAL MODE: every byte
     right in the buffer and back from XMS, seven calls. Bochs: C:\\RUN, a
     BIOS volume - or with --hdd a DRVC_DISK volume through HDD.DRV's LOW
     COPY (Bochs's IDE: its rung 1). --box86: B:\\
  B  RMBXB.DAT (100,000 bytes) on B:, a BIOS floppy: real mode, every byte
  F  FRAGA.DAT on B:, FSXL_RUNS (224) single-sector runs - walked to the
     CAP: real mode, every byte
  G  FRAGB.DAT, one run more: FSXR_FRAG, the bracket in PROTECTED mode, and
     every byte still right (the fallback reads as today's)
  D  RMBXB.DAT with FSXL_DRV DRVC_NET: FSXR_DRV (W4 carries DRVC_SOUND)
  A  HDV_ARM through the OSAPI_DRV_CALL twin: with --hdd, HDD.DRV armed to
     the XMS block's CURRENT base, RMBXC.DAT's first 32 KB read, disarmed -
     every sector's bit in the map and every dword right in XMS (zeroed
     first); without HDD.DRV, DRV_CALL answers no driver (CF, AX = 0)
  N  inside a real-mode bracket: an unlisted name FERR_NAME, a listed file
     that is not there FERR_NOENT, READ_AT and XMEM_ALLOC refused,
     XMEM_CAPS what protected mode answered, and XMEM_COPY reading back what
     protected mode wrote (D's second 32 KB)
  and every READ leg's XMS is read back once more AFTER its bracket, in
  protected mode - the kernel's own token translation against the twin's,
  which a round trip through the twin alone could not tell apart
  and after: no bracket up, [fsx_rwhy] FFh, the service area and every
  driver copy's claim given back ([fsx_rsvc] 0), the table byte for byte,
  with --hdd HDD.DRV's image header as it was, no kern_mega FAULT.

--box86 runs the same on 86Box's 286-victory (the RESET way down, and
XMEM_COPY by LOADALL), with every file on B: and the verdicts read off the
package's bss (86Box has no port E9).

THE FAILING CASES (docs/WRITING-TESTS.md 1) are in MEGA-VIDEO-REALMODE-PLAN's
W3 entry: an extent off by one sector turns C, B and F red.
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
sys.path[:0] = [os.path.join(ROOT, "tools"), HERE,
                os.path.join(HERE, "rdgcore")]

NAME = "MEGARMBX"
PKG = os.path.join(ROOT, "build", "megarmbx.o88")
BIN = os.path.join(ROOT, "build", "megarmbx.bin")
KSEG = 0x0060
TBL, TLEN = 0x0010, 0x04D9
CFG_HDD = 1 << 1                # drv_cfgbit: HDD.DRV is bit 1
FSXL_RUNS = 224
LEGS = "CBFGDAN"                # their order in the result block
LEG_SZ = 24
FILES = {                       # name: (salt, bytes)
    "RMBXC.DAT": (0x11, 192 * 1024 + 1000),
    "RMBXB.DAT": (0x22, 100000),
    "FRAGA.DAT": (0x33, FSXL_RUNS * 512 - 96),
    "FRAGB.DAT": (0x44, (FSXL_RUNS + 1) * 512 - 96),
}
FERR_NAME, FERR_NOENT = 3, 4
PM = 0x10
FAILS = []


def want(cond, what):
    print("   %s  %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        FAILS.append(what)


def u16(b, i=0):
    return b[i] | b[i + 1] << 8


def u32(b, i=0):
    return u16(b, i) | u16(b, i + 2) << 16


def body(salt, n):
    words = (n + 3) // 4
    out = struct.pack("<%dI" % words, *[(k * 4 + (salt << 24)) & 0xFFFFFFFF
                                        for k in range(words)])
    return out[:n]


def make_files(d):
    out = {}
    for nm, (salt, n) in FILES.items():
        p = os.path.join(d, nm)
        with open(p, "wb") as f:
            f.write(body(salt, n))
        out[nm] = p
    return out


def frag_add(fat, name, data):
    """add NAME to the FAT12 image with every cluster ALTERNATE - free
    clusters skipped between them - so each is a run of its own"""
    csz = fat.spc * fat.bps
    need = (len(data) + csz - 1) // csz
    use = fat.free_clusters()[::2][:need]
    if len(use) < need:
        raise SystemExit("vidrmb: no room on B: for %s" % name)
    for k, c in enumerate(use):
        fat.put(c, 0xFFF if k == need - 1 else use[k + 1])
        o = fat.cluster_off(c)
        ch = data[k * csz:(k + 1) * csz]
        fat.img[o:o + len(ch)] = ch
    slot = next(i for i in range(fat.nroot)
                if fat.img[fat.root_off + i * 32] in (0, 0xE5))
    ent = bytearray(32)
    ent[0:11] = fat.raw11(name)
    ent[11] = 0x20
    struct.pack_into("<H", ent, 24, 0x5C21)
    struct.pack_into("<H", ent, 26, use[0])
    struct.pack_into("<I", ent, 28, len(data))
    fat.img[fat.root_off + slot * 32:fat.root_off + slot * 32 + 32] = ent
    runs = 1 + sum(1 for a, b in zip(use, use[1:]) if b != a + 1)
    return runs


def make_b(d, files, extra=()):
    """B:, 1.44MB: RMBXB.DAT contiguous, FRAGA and FRAGB alternate"""
    import os88fat
    img = os.path.join(d, "bdisk.img")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                    "-o", img, "--size", "1440"] + list(extra) +
                   [files["RMBXB.DAT"]], check=True, stdout=subprocess.DEVNULL)
    fat = os88fat.Fat12(img)
    if fat.spc != 1:
        raise SystemExit("vidrmb: B: has %d sectors a cluster - a run each "
                         "wants 1" % fat.spc)
    ra = frag_add(fat, "FRAGA.DAT", open(files["FRAGA.DAT"], "rb").read())
    rb = frag_add(fat, "FRAGB.DAT", open(files["FRAGB.DAT"], "rb").read())
    fat.save()
    if (ra, rb) != (FSXL_RUNS, FSXL_RUNS + 1):
        raise SystemExit("vidrmb: FRAGA is %d runs and FRAGB %d - want %d, "
                         "%d" % (ra, rb, FSXL_RUNS, FSXL_RUNS + 1))
    return img


def resoff():
    with open(BIN, "rb") as f:
        return u16(f.read(12), 8)


def parse(res):
    """the result block -> {leg: dict}, plus the caps pair"""
    out = {"fin": res[0], "capso": u16(res, 4), "capsi": u16(res, 6),
           "xtok": u32(res, 8), "xphys": u32(res, 12)}
    for k, c in enumerate(LEGS):
        r = res[16 + k * LEG_SZ:16 + (k + 1) * LEG_SZ]
        out[c] = dict(cf=r[0], why=r[1], feat=r[2], ran=r[3],
                      err=u16(r, 4), bytes=u32(r, 6), bad=u16(r, 10),
                      xbad=u16(r, 12), n=u16(r, 14),
                      x=[u16(r, 16 + 2 * i) for i in range(4)])
    return out


def judge(r, hdd):
    def read_leg(c, nm, why, pm, what):
        d = r.get(c, {})
        n = FILES[nm][1]
        calls = (n + 32767) // 32768
        want(d.get("cf") == 0 and d.get("why") == why and d.get("ran") == 1
             and bool(d.get("feat", 0) & PM) == pm,
             "%s: %s - cf %s, SI %s (want %d), %s inside (feat %02x)"
             % (c, what, d.get("cf"), d.get("why"), why,
                "protected" if pm else "real", d.get("feat", 0)))
        want(d.get("err") == 0 and d.get("bytes") == n and d.get("n") == calls,
             "%s: %s delivered whole - %d bytes in %d calls (want %d in %d), "
             "err %04x" % (c, nm, d.get("bytes", 0), d.get("n", 0), n, calls,
                           d.get("err", 0)))
        want(d.get("bad") == 0 and d.get("xbad") == 0,
             "%s: every dword right in the buffer (%d wrong) and back from "
             "XMS (%d wrong)" % (c, d.get("bad", -1), d.get("xbad", -1)))
        want(d.get("x", [1])[0] == 0,
             "%s: ...and read back from XMS AGAIN in protected mode, through "
             "the kernel's own token translation (%d wrong)"
             % (c, d.get("x", [-1])[0]))
    read_leg("C", "RMBXC.DAT", 0, False, "its own folder%s" %
             (", a DRVC_DISK volume through HDD.DRV's low copy" if hdd
              else ""))
    read_leg("B", "RMBXB.DAT", 0, False, "B:, a BIOS floppy")
    read_leg("F", "FRAGA.DAT", 0, False, "%d runs, the cap" % FSXL_RUNS)
    read_leg("G", "FRAGB.DAT", 7, True, "%d runs, past it: FSXR_FRAG"
             % (FSXL_RUNS + 1))
    read_leg("D", "RMBXB.DAT", 8, True, "FSXL_DRV DRVC_NET: FSXR_DRV")
    a = r.get("A", {})
    want(a.get("cf") == 0 and a.get("why") == 0 and a.get("ran") == 1,
         "A: real mode (SI %s)" % a.get("why"))
    if hdd:
        want(a.get("x", [0])[0] == 0x1111 and a.get("err") == 0
             and a.get("bytes") == 32768,
             "A: HDV_ARM taken through the DRV_CALL twin (%04x), 32 KB read "
             "(%d, err %04x)" % (a.get("x", [0])[0], a.get("bytes", 0),
                                 a.get("err", 0)))
        want(a.get("x", [0, 0, 0, 0])[3] == 0xFFFF,
             "A: every sector went STRAIGHT TO XMS (the map's AND %04x)"
             % a.get("x", [0, 0, 0, 0])[3])
        want(a.get("xbad") == 0 and a.get("x", [0, 0])[1] == 0,
             "A: and every dword is right there (%d wrong, copy %04x)"
             % (a.get("xbad", -1), a.get("x", [0, 0])[1]))
    else:
        want(a.get("x", [0])[0] == 0x4000,
             "A: no DRVC_DISK driver here - DRV_CALL answers CF, AX = 0 "
             "(%04x)" % a.get("x", [0])[0])
    n = r.get("N", {})
    x = n.get("x", [0, 0, 0, 0])
    want(n.get("why") == 0 and n.get("ran") == 1, "N: real mode (SI %s)"
         % n.get("why"))
    want(x[0] == FERR_NAME and x[1] == FERR_NOENT,
         "N: an unlisted name answers FERR_NAME (%d), a listed missing one "
         "FERR_NOENT (%d)" % (x[0], x[1]))
    want(x[2] == 0xFFFF and x[3] == 0xFFFF,
         "N: READ_AT (%04x) and XMEM_ALLOC (%04x) refused" % (x[2], x[3]))
    want(n.get("xbad") == 0, "N: the COPY twin reads what protected mode "
         "wrote (D's RMBXB.DAT, its second 32 KB: %d wrong)"
         % n.get("xbad", -1))
    want(r.get("capsi") == r.get("capso") and r.get("capso"),
         "N: XMEM_CAPS inside %d KB, outside %d KB" % (r.get("capsi", 0),
                                                         r.get("capso", 0)))


def bochs(a):
    import os88bochs
    from os88fixture import need
    for p in (PKG, BIN, os.path.join(ROOT, "build", "megarun.o88"),
              os.path.join(ROOT, "build", "mega.img"),
              os.path.join(ROOT, "build", "mbr.bin")):
        need(os.path.relpath(p, ROOT))
    out = tempfile.mkdtemp(prefix="vidrmb-")
    t0 = time.time()
    files = make_files(out)
    bimg = make_b(out, files)
    os88bochs.MEGS = 4
    kw = dict(put=("RUN:" + files["RMBXC.DAT"],), bimg=bimg, btype="1_44")
    if a.hdd:
        kw["cfg"] = CFG_HDD
    else:                       # C: THE KERNEL'S OWN BIOS VOLUME: booted
        kw["cfg"] = 0           # from, with SYSTEM.CFG wanting no driver
        kw["bootc"] = True      # (a floppy boot mounts C: through HDD.DRV)
    box = os88bochs.Box(out, [PKG], **kw)
    roff = resoff()
    lines, res, after, moved, hd0, hd1 = [], None, None, False, None, None
    try:
        if not box.wait_pm(240):
            raise RuntimeError("kern_mega never reached protected mode in 240s")
        box.up()
        tbl0, = box.reads([(KSEG * 16 + TBL, TLEN)])
        hd0 = hdd_head(box)
        cv, = box.reads([(box.sym("dsk_vtab") + 2 * 16, 4)])
        print("   C: is volume kind %d, unit %02x, class %d; HDD.DRV %s"
              % (cv[0], cv[1], cv[3], "loaded" if hd0 else "not loaded"))
        if a.hdd:
            want(hd0 is not None and cv[0] == 1 and cv[3] == 2,
                 "C: is HDD.DRV's (DVK_DRV, DRVC_DISK)")
        else:
            want(hd0 is None and cv[0] == 0 and cv[1] >= 0x80,
                 "C: is the BIOS's (DVK_BIOS, a fixed unit), no HDD.DRV")
        box.megarun()
        end = time.time() + 400
        while time.time() < end:
            ls = box.lines()
            if any(l.startswith(NAME + ": done") for l in ls) or box.fault():
                break
            time.sleep(0.5)
        else:
            want(False, "%s finished its legs in 400s" % NAME)
        box.guest_sleep(1)
        lines = box.lines() + box.faults()   # (MEGA_E9: the kernel
                                            # says nothing on E9)
        if not box.fault():
            base = box.pkg_base(NAME)
            res = box.read(base + roff, 16 + len(LEGS) * LEG_SZ)
            box.ui_done("the desktop after the brackets", lock=False)
            after = box.reads([(KSEG * 16 + TBL, TLEN), ("fsx_rwhy", 1),
                               ("fsx_rmode", 1), ("fsx_task", 1),
                               ("fsx_rsvc", 2)])
            if a.hdd:
                hd1 = hdd_head(box)
            box.capture()
            p0 = box.mouse()
            try:
                box.goto(300, 250)
                moved = box.mouse() != p0
            except RuntimeError as e:
                print("   (pointer: %s)" % e)
    finally:
        box.stop()
    print("vidrmb: %.0fs under Bochs%s" % (time.time() - t0,
                                           " with HDD.DRV" if a.hdd else ""))
    for l in lines:
        if l.startswith((NAME, "kern_mega FAULT", "kern_mega CLOSED")):
            print("   | " + l)
        if l.startswith(("kern_mega FAULT", "kern_mega CLOSED")):
            FAILS.append(l)
    if res is None:
        want(False, "%s's result block read" % NAME)
    else:
        r = parse(res)
        want(r["fin"] == 1, "%s ran every leg" % NAME)
        judge(r, a.hdd)
    if after:
        t1, why, rm, ft, svc = after
        print("\n   after")
        want(t1 == tbl0, "the table is byte for byte what it was")
        want(why[0] == 0xFF and rm[0] == 0 and ft[0] == 0xFF,
             "[fsx_rwhy] at rest (%02x), [fsx_rmode] 0, no bracket up (%02x)"
             % (why[0], ft[0]))
        want(u16(svc) == 0, "the service area's claim is given back "
             "([fsx_rsvc] %04x)" % u16(svc))
        if a.hdd:
            want(hd0 is not None and hd1 == hd0,
                 "HDD.DRV's image header and device row as they were")
        want(moved, "the pointer moves on the desktop")
    if a.keep or FAILS:
        print("vidrmb: the boot is in " + out)
    else:
        shutil.rmtree(out, ignore_errors=True)
    print("vidrmb: %s" % ("FAIL - %d" % len(FAILS) if FAILS else "ok"))
    return 1 if FAILS else 0


def hdd_head(box):
    """HDD.DRV's header and first device row (its rung's kind), or None"""
    row = box.sym("drv_tab") + 1 * 16   # drv_tab's order: HDD.DRV's row
    seg = u16(box.read(row + 2, 2))     # (SPEC.md 31.1), DRVR_SEG at +2
    if not seg:
        return None
    base = box.sel_base(seg) if seg & 4 else seg << 4
    return box.read(base, 32 + 14)


def box86(a):
    import os88box
    import os88geom as G
    B = os.path.join(ROOT, "build")
    for f in ("mega.img", "megak/kernel.map", "megarun.o88", "megarmbx.o88",
              "megarmbx.bin"):
        if not os.path.exists(os.path.join(B, f)):
            sys.exit("vidrmb: build/%s is not built - `make mega megarun`" % f)
    tmp = tempfile.mkdtemp(prefix="vidrmb286")
    files = make_files(tmp)
    img = os.path.join(tmp, "mega.img")
    shutil.copy(os.path.join(B, "mega.img"), img)
    stage = os.path.join(tmp, "stage")
    os.makedirs(stage)
    extra = []
    for src, dst in (("megarun.o88", "0RUN.O88"),
                     ("megarmbx.o88", "MEGARMBX.O88")):
        shutil.copy(os.path.join(B, src), os.path.join(stage, dst))
        extra.append(os.path.join(stage, dst))
    extra.append(files["RMBXC.DAT"])
    bimg = make_b(tmp, files, extra)
    shutil.move(bimg, os.path.join(tmp, "megab.img"))
    bimg = os.path.join(tmp, "megab.img")
    prompts = (("bypass MEMORY test", "esc"), ("to RESUME", "f1"),
               ("EXIT FOR BOOT", "return"))
    b = os88box.launch(a.vm, name="vidrmb286%d" % os.getpid(), hold=True,
                       fresh=True, a=img, b=bimg, temp=not a.keep, turbo=True)
    roff = resoff()
    res, after = None, None
    t0 = time.time()
    try:
        b.kernel_up(prompts=prompts)
        b.until(lambda: b.byte("mega_on"), "protected mode", 30000, 250)
        b.ui_done(cap_ms=10000)
        b.to(592, 105)
        b.click(2)
        b.ui_done(limit_ms=20000)
        b.to(160, 127)
        b.click(2)

        def sptr():
            raw = b.memr(b.sym("inst_tab"), G.INST_MAX * G.I_RECSZ)
            for i in range(G.INST_MAX):
                r = raw[i * G.I_RECSZ:(i + 1) * G.I_RECSZ]
                nm = r[G.I_NAME:G.I_NAME + 16].split(b"\0")[0]
                if r[G.I_STATE] == 1 and nm == NAME.encode():
                    return u16(r, G.I_SPTR)
            return None
        b.until(lambda: sptr() is not None, "%s to start" % NAME, 60000, 200)
        base = b.lin(sptr())
        b.until(lambda: b.memr(base + roff, 1)[0] == 1, "%s's legs" % NAME,
                600000, 500)
        res = b.memr(base + roff, 16 + len(LEGS) * LEG_SZ)
        b.ui_done(limit_ms=20000, lock=False)
        import os88sym                  # (.lowbss: LOW_SEG's, by the map)
        off, sect, equ = os88sym._parse_map(os.path.join(B, "megak",
                                                         "kernel.map"))
        after = (b.byte("fsx_rwhy"), b.byte("fsx_rmode"), b.byte("fsx_task"),
                 u16(b.memr(equ["LOW_SEG"] * 16 + off["fsx_rsvc"], 2)))
    except SystemExit as e:
        want(False, "the machine stopped answering or a step missed: %s" % e)
    finally:
        b.quit(remove=not a.keep)
    print("vidrmb: %.0fs under 86Box's %s" % (time.time() - t0, a.vm))
    if res is None:
        want(False, "%s's result block read" % NAME)
    else:
        r = parse(res)
        want(r["fin"] == 1, "%s ran every leg" % NAME)
        judge(r, False)
    if after:
        why, rm, ft, svc = after
        want(why == 0xFF and rm == 0 and ft == 0xFF and svc == 0,
             "after: [fsx_rwhy] %02x, [fsx_rmode] %d, no bracket up, "
             "[fsx_rsvc] %04x" % (why, rm, svc))
    if a.keep:
        print("vidrmb: kept %s" % tmp)
    else:
        shutil.rmtree(tmp, ignore_errors=True)
    print("vidrmb: %s" % ("FAIL - %d" % len(FAILS) if FAILS else "ok"))
    return 1 if FAILS else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--hdd", action="store_true",
                    help="HDD.DRV wanted: C: a DRVC_DISK volume, its rung 1")
    ap.add_argument("--box86", action="store_true",
                    help="86Box's 286 (--vm): the reset and LOADALL")
    ap.add_argument("--vm", default="286-victory")
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    return box86(a) if a.box86 else bochs(a)


if __name__ == "__main__":
    sys.exit(main())
