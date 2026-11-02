#!/usr/bin/env python3
"""vidrmbems - EMS.DRV through kern_mega's REAL-MODE BRACKET (SPEC.md 53.2.3
and 107, docs/plans/MEGA-VIDEO-REALMODE-PLAN.md W3).

    make mega build/megarmbe.o88 && python3 tests/vidrmbems.py [--keep] [--build DIR]

W3 carries a DRVC_EMS driver the bracket's list names below 1MB at the
freeze - its image AND ITS STATE, copied into a conventional claim - serves
OSAPI_DRV_CALL inside through the twin (rms_drvx, the caller's slot in BH as
drv_pkg_call_x hands it), and copies it all back up at the exit. It is the
same code as DRVC_DISK's (vidrmbhdd) and nothing ran it until this row:
no emulator gave kern_mega an EMS board.

THE MACHINE is vm/286-mega-ems - vm/286-mega's mr286 (16 MHz, 4MB) with
86Box's "Lo-tech EMS Board" (`isamem0_type = lotechems`: registers 260h,
frame E000h, 2 MB), the consecutive register family EMS.DRV drives. The
system disk is build/mega.img with a SYSTEM.CFG wanting EMS.DRV (bit 7);
B: carries tests/megarmb/megarmbe.asm's MEGARMBE, opened by name in B:'s
Disk window (megacdrom's walk off the guest's own tables), which does,
on its window's timer (every answer a word of its bss, read here - 86Box has
no port E9):

  BEFORE  protected mode: IDENT, CAPS (128 pages free, four quarters),
          ALLOC 4 (H1), FRAME all four quarters - a SELECTOR - and H1's
          pages 0 and 1 mapped into quarters 0 and 1, filled and checked
  R       a bracket listing FSXL_DRV DRVC_EMS: REAL mode inside; IDENT and
          CAPS through the twin (the frame the paragraph E000h, no quarter
          free, H1's pages gone); BASE H1 - protected mode's handle is still
          the caller's; quarter 0 as protected mode left it; ALLOC 2 (H2),
          its pages mapped into quarters 2 and 3, filled and checked; H1's
          page 0 mapped into quarter 1 as well and seen there, and back
  N       a bracket with an EMPTY list: DRVC_EMS not carried, DRV_CALL
          answers no driver (CF = 1, AX = 0)
  AFTER   protected mode: CAPS counts H2 - made INSIDE - and no quarter
          free; BASE H2 its run (pages 4-5) and H1 its own; every quarter as
          real mode left it, unmapped; H2's page 1 MAPPED into quarter 0 and
          seen; both FREEd, UNFRAMEd, and CAPS back to 128 and four quarters

and this reads, off the emulator rather than the package: EMS.DRV's row
LOADED with DRVE_OK, its probe's port, frame and size; its image outside its
state byte for byte the same before and after the brackets; its handle and
quarter tables empty at the end; the board's frame at E000:0 holding exactly
what the last maps put there; and the kernel at rest - no bracket up,
[fsx_rwhy] FFh, the service area's claim given back.

THE FAILING CASE (docs/WRITING-TESTS.md 1), in MEGA-VIDEO-REALMODE-PLAN's W3
entry: fsx_rcopyf with the drivers' copy back UP taken out turns AFTER red -
H2 forgotten (CAPS 124 free, BASE H2 EMSE_BAD, the map refused) and the
quarters and H1's handle never released.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path[:0] = [os.path.join(ROOT, "tools"), HERE]

NAME = "MEGARMBE"
ROW_EMS = 6                     # drv_tab's EMS row (SPEC.md 107.1)
DRVR_SIZE, DRVR_SEG, DRVR_ERR = 16, 2, 14
CFG_EMS = 1 << 7                # drv_cfgbit: EMS.DRV is bit 7
EM = 0x4D45                     # IDENT's 'EM'
NPG = 128                       # the profile's 2 MB board
FRAME = 0xE000
PORT = 0x260
PM = 0x10
FAILS = []


def want(cond, what):
    print("   %s  %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        FAILS.append(what)


def u16(b, i=0):
    return b[i] | b[i + 1] << 8


# the result block (tests/megarmb/megarmbe.asm's me_res equates): name ->
# (offset, bytes)
RES = dict(fin=(0, 1), err=(4, 2), h1=(8, 2), h2=(10, 2), bfeat=(12, 1),
           rfeat=(13, 1), afeat=(14, 1), nfeat=(15, 1), bident=(16, 2),
           bfree=(18, 2), bnpg=(20, 2), bq=(22, 2), bfr=(24, 2),
           bbad=(26, 2), rcf=(28, 1), rwhy=(29, 1), rran=(30, 1),
           nran=(31, 1), rident=(32, 2), rfree=(34, 2), rnpg=(36, 2),
           rcfr=(38, 2), rq=(40, 2), rb1=(42, 2), rl1=(44, 2), rfr=(46, 2),
           rbad0=(48, 2), rbad1=(50, 2), rbad2=(52, 2), ncf=(54, 1),
           nwhy=(55, 1), nident=(56, 2), nicf=(58, 2), afree=(60, 2),
           aq=(62, 2), ab2=(64, 2), al2=(66, 2), ab1=(68, 2), al1=(70, 2),
           afr=(72, 2), abad=(74, 2), amap=(76, 2), afree2=(78, 2),
           aq2=(80, 2))
RESN = 82


def parse(raw):
    return {k: (raw[o] if n == 1 else u16(raw, o)) for k, (o, n) in RES.items()}


def pattern(salt):
    s = salt * 0x0101
    return struct.pack("<8192H", *[k ^ s for k in range(8192)])


def judge(r):
    print("\n   before (protected mode)")
    want(r["fin"] == 1, "%s ran to the end" % NAME)
    want(r["err"] == 0, "no verb answered CF = 1 (first: %04x - leg << 8 | "
         "verb)" % r["err"])
    want(r["bfeat"] & PM, "protected mode (feat %02x)" % r["bfeat"])
    want(r["bident"] == EM and r["bnpg"] == NPG and r["bfree"] == NPG
         and r["bq"] == 0x0F,
         "IDENT %04x, CAPS %d of %d free, quarters %x free (want 'EM', %d, "
         "every quarter)" % (r["bident"], r["bfree"], r["bnpg"], r["bq"], NPG))
    want(1 <= r["h1"] <= 8 and r["bfr"] & 4,
         "ALLOC 4 -> H1 %d; FRAME -> %04x, an LDT selector"
         % (r["h1"], r["bfr"]))
    want(r["bbad"] == 0, "H1's pages 0, 1 filled and read back through the "
         "selector (%d words wrong)" % r["bbad"])
    print("\n   R (FSXL_DRV DRVC_EMS)")
    want(r["rcf"] == 0 and r["rwhy"] == 0 and r["rran"] == 1
         and not r["rfeat"] & PM,
         "a REAL-mode bracket: CF %02x, SI %d, ran %d, feat %02x"
         % (r["rcf"], r["rwhy"], r["rran"], r["rfeat"]))
    want(r["rident"] == EM, "IDENT through the twin (%04x)" % r["rident"])
    want(r["rfree"] == NPG - 4 and r["rnpg"] == NPG and r["rq"] == 0,
         "CAPS inside: %d of %d free, quarters %x free (H1 counted, all four "
         "ours - the state came DOWN)" % (r["rfree"], r["rnpg"], r["rq"]))
    want(r["rcfr"] == FRAME and r["rfr"] == FRAME,
         "the frame is the PARAGRAPH %04x inside (CAPS %04x, FRAME %04x)"
         % (FRAME, r["rcfr"], r["rfr"]))
    want(r["rb1"] == 0 and r["rl1"] == 4,
         "BASE H1: pages %d + %d - protected mode's handle is the caller's "
         "inside (the slot in BH)" % (r["rb1"], r["rl1"]))
    want(r["rbad0"] == 0, "quarter 0 at E000:0000 as protected mode wrote "
         "it (%d words wrong)" % r["rbad0"])
    want(1 <= r["h2"] <= 8 and r["h2"] != r["h1"] and r["rbad1"] == 0,
         "ALLOC 2 inside -> H2 %d; its pages mapped into quarters 2, 3, "
         "filled and read back (%d wrong)" % (r["h2"], r["rbad1"]))
    want(r["rbad2"] == 0, "H1 page 0 seen through quarter 1 too, and page 1 "
         "back (%d wrong)" % r["rbad2"])
    print("\n   N (an empty list)")
    want(r["ncf"] == 0 and r["nwhy"] == 0 and r["nran"] == 1
         and not r["nfeat"] & PM,
         "a REAL-mode bracket: CF %02x, SI %d, ran %d, feat %02x"
         % (r["ncf"], r["nwhy"], r["nran"], r["nfeat"]))
    want(r["nicf"] == 0xFFFF and r["nident"] == 0,
         "DRVC_EMS not carried: DRV_CALL CF %s, AX %04x (want CF, 0)"
         % ("1" if r["nicf"] else "0", r["nident"]))
    print("\n   after (protected mode)")
    want(r["afeat"] & PM, "protected mode again (feat %02x)" % r["afeat"])
    want(r["afree"] == NPG - 6 and r["aq"] == 0,
         "CAPS: %d free, quarters %x free (want %d - H2, made INSIDE, came "
         "back UP - and none)" % (r["afree"], r["aq"], NPG - 6))
    want(r["ab2"] == 4 and r["al2"] == 2,
         "BASE H2: pages %d + %d (want 4 + 2)" % (r["ab2"], r["al2"]))
    want(r["ab1"] == r["rb1"] and r["al1"] == 4,
         "BASE H1: pages %d + %d" % (r["ab1"], r["al1"]))
    want(r["afr"] & 4, "FRAME -> %04x, a selector again" % r["afr"])
    want(r["abad"] == 0, "all four quarters as real mode left them, "
         "unmapped (%d words wrong)" % r["abad"])
    want(r["amap"] == 0, "H2 page 1 MAPPED into quarter 0 and seen (%d "
         "wrong)" % r["amap"])
    want(r["afree2"] == NPG and r["aq2"] == 0x0F,
         "FREE both, UNFRAME: CAPS %d free, quarters %x (want %d, f)"
         % (r["afree2"], r["aq2"], NPG))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--vm", default="286-mega-ems")
    ap.add_argument("--build", default=os.path.join(ROOT, "build"),
                    help="the tree to boot (a private `make BUILD=` one)")
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    import os88box
    import os88geom as G
    import megacdrom as MC
    from cycweb import pkg_syms
    B = os.path.abspath(a.build)
    for f in ("mega.img", "megak/kernel.map", "megarmbe.o88", "megarmbe.bin",
              "ems.bin"):
        if not os.path.exists(os.path.join(B, f)):
            sys.exit("vidrmbems: %s/%s is not built - `make mega build/megarmbe.o88`"
                     % (B, f))
    es, eimg = pkg_syms("drivers/ems/ems.asm", ("drivers/", "apps/"))
    want(eimg == open(os.path.join(B, "ems.bin"), "rb").read(),
         "EMS.DRV re-assembles to the image on the disk")
    tmp = tempfile.mkdtemp(prefix="vidrmbems")
    img = os.path.join(tmp, "mega.img")
    shutil.copy(os.path.join(B, "mega.img"), img)
    cfg = os.path.join(tmp, "SYSTEM.CFG")
    with open(cfg, "wb") as fh:
        fh.write(b"O88CFG\0\0" + (3).to_bytes(2, "little") + b"DW" +
                 bytes([1, 2]) + CFG_EMS.to_bytes(2, "little") + b"\0\0")
    fat = [sys.executable, os.path.join(ROOT, "tools", "os88fat.py")]
    subprocess.run(fat + ["del", img, "SYSTEM.CFG"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(fat + ["add", img, cfg], check=True,
                   stdout=subprocess.DEVNULL)
    stage = os.path.join(tmp, "stage")
    os.makedirs(stage)
    pkg = os.path.join(stage, NAME + ".O88")
    shutil.copy(os.path.join(B, "megarmbe.o88"), pkg)
    bimg = os.path.join(tmp, "megab.img")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                    "-o", bimg, "--size", "1440", pkg], check=True,
                   stdout=subprocess.DEVNULL)
    with open(os.path.join(B, "megarmbe.bin"), "rb") as f:
        roff = u16(f.read(12), 8)
    t0 = time.time()
    b = os88box.launch(a.vm, name="vidrmbems%d" % os.getpid(), hold=True,
                       fresh=True, a=img, b=bimg, temp=not a.keep, turbo=True,
                       kmap=os.path.join(B, "megak", "kernel.map"))
    res = after = d0 = d1 = frame = None
    bad = []
    try:
        MC.mr_setup(b)
        b.kernel_up(guest_ms=90000, idle=False)
        b.until(lambda: b.byte("mega_on"), "protected mode", 30000, 250)
        b.ui_done(cap_ms=10000)

        def drv():
            row = b.memr(b.sym("drv_tab") + ROW_EMS * DRVR_SIZE, DRVR_SIZE)
            sel = u16(row, DRVR_SEG)
            img = b.memr(b.lin(sel), len(eimg) + 64) if sel else b""
            return sel, row[DRVR_ERR], img
        d0 = drv()
        have = set(MC.win_recs(b))      # B:'s Disk window, by its zone off
        bx, by = G.drive_pt(MC.Guest(b), "B")   # the guest's own tables...
        b.to(bx, by)
        b.click(2)
        b.until(lambda: set(MC.win_recs(b)) - have, "a Disk window for B:",
                20000, 100)
        b.ui_done(cap_ms=10000)
        slot = sorted(set(MC.win_recs(b)) - have)[0]
        blk = MC.fs_block(b, slot)
        b.until(lambda: MC.listing(b, blk), "B:'s listing", 20000, 100)
        miss = MC.pick(b, slot, blk, NAME + ".O88", "open " + NAME)
        if miss:                        # ...and the package by its name
            raise SystemExit(miss)

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
                120000, 250)
        res = b.memr(base + roff, RESN)
        b.ui_done(limit_ms=20000, lock=False)
        d1 = drv()
        frame = b.memr(FRAME * 16, 0x10000)
        import os88sym                  # (.lowbss: LOW_SEG's, by the map)
        off, sect, equ = os88sym._parse_map(os.path.join(B, "megak",
                                                         "kernel.map"))
        after = (b.byte("fsx_rwhy"), b.byte("fsx_rmode"), b.byte("fsx_task"),
                 u16(b.memr(equ["LOW_SEG"] * 16 + off["fsx_rsvc"], 2)))
        if a.keep:
            b.shot(os.path.join(tmp, "after.png"))
    except SystemExit as e:
        want(False, "the machine stopped answering or a step missed: %s" % e)
    finally:
        bad = MC.image_faults(b.run)
        b.quit(remove=not a.keep)
    print("vidrmbems: %.0fs under 86Box's %s" % (time.time() - t0, a.vm))
    if d0:
        sel, err, im = d0
        print("\n   EMS.DRV")
        want(sel and err == 0, "loaded at boot, DRVE_OK (selector %04x, "
             "error %d)" % (sel, err))
        if sel:
            want(u16(im, es["em_port"]) == PORT
                 and u16(im, es["em_frame"]) == FRAME
                 and u16(im, es["em_npg"]) == NPG,
                 "its probe: port %03x, frame %04x, %d pages (want %03x, "
                 "%04x, %d)" % (u16(im, es["em_port"]),
                                u16(im, es["em_frame"]),
                                u16(im, es["em_npg"]), PORT, FRAME, NPG))
    if res is None:
        want(False, "%s's result block read" % NAME)
    else:
        judge(parse(res))
    if d0 and d1:
        print("\n   the driver after the brackets")
        (s0, e0, i0), (s1, e1, i1) = d0, d1
        want(s1 == s0 and e1 == 0, "the same row: selector %04x, error %d"
             % (s1, e1))
        st, fw = es["os88_state"], es["em_fwin"]
        keep = list(range(0, fw)) + list(range(fw + 2, es["em_npg"] + 2))
        moved = [k for k in keep if i0[k] != i1[k]]
        want(not moved, "its code, its probe and the bytes between them byte "
             "for byte the same before and after (%d differ%s)"
             % (len(moved), (" from +%x" % moved[0]) if moved else ""))
        loader = [k for k in range(0x20, st) if i0[k] != eimg[k]]
        want(not loader, "...and its code the disk's (%d bytes differ%s)"
             % (len(loader), (" from +%x" % loader[0]) if loader else ""))
        print("   (em_fwin %04x before, %04x after: the window em_fsel re-aims"
              " each time - the paragraph itself after a real-mode answer)"
              % (u16(i0, fw), u16(i1, fw)))
        q, hl = es["em_qown"], es["em_hlen"]
        want(i1[q:q + 4] == b"\0" * 4 and i1[hl:hl + 32] == b"\0" * 32,
             "every quarter and every handle free (quarters %s, handles %s)"
             % (i1[q:q + 4].hex(), i1[hl:hl + 32].hex()))
    if frame:
        q = [frame[k * 0x4000:(k + 1) * 0x4000] for k in range(4)]
        want(q == [pattern(0xB2), pattern(0xA2), pattern(0xB1),
                   pattern(0xB2)],
             "the board, read off the emulator at E000:0: H2 page 1, H1 page "
             "1, H2 page 0, H2 page 1 (%s)" % ", ".join(
                 "ok" if x == y else "wrong" for x, y in
                 zip(q, (pattern(0xB2), pattern(0xA2), pattern(0xB1),
                         pattern(0xB2)))))
    if after:
        why, rm, ft, svc = after
        want(why == 0xFF and rm == 0 and ft == 0xFF and svc == 0,
             "the kernel at rest: [fsx_rwhy] %02x, [fsx_rmode] %d, no "
             "bracket up (%02x), [fsx_rsvc] %04x" % (why, rm, ft, svc))
    want(not bad, "no exception charged to a loaded image (%d%s)" % (
        len(bad), "".join("\n        " + x for x in bad[:6])))
    if a.keep:
        print("vidrmbems: kept %s" % tmp)
    else:
        shutil.rmtree(tmp, ignore_errors=True)
    print("vidrmbems: %s" % ("FAIL - %d" % len(FAILS) if FAILS else "ok"))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
