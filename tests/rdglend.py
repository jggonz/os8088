#!/usr/bin/env python3
"""RealDMG's side features in LENT bank slots (86Box).

    make && python3 tests/rdglend.py [--vm 286-victory] [--red]

docs/REALDMG-SPEC.md section 10.3.3. On a heap the plan gave to code images
and bank slots, a replay's buttons and the log's records found no room - the
owner's Link's Awakening, both refused - so a side buffer that the heap cannot
hold lives in bank slots the cache LENDS for it, cut top down, and every lent
slot is the cache's again when the last buffer goes. This builds the
precompiled package of GBBANK.GB (tools/gbprof/gbbank.asm, ours: 1MB, a
fetch at nearly every switch, every switch checked by the bank's bytes and
by a routine run from 4000-7FFF) with -DRW_LENDTEST, which never asks the
heap - so the loans are certain whatever the machine's heap - and then:

  * Save State > Record Replay (F6): a slot lent, the buttons in it;
  * Machine > Log: the records in the same lent slot, nothing lent more;
  * ten seconds of the cartridge on fewer slots, every bank still right;
  * F6 again and Machine > Log again: both written, nothing lent, every
    slot the cache's;
  * GBBANK.SS1, GBBANK.RP1 (frames recorded) and GBBANK00.LOG on the disk.

RED: --red builds -DRW_NOLEND too, no slot lent - a full heap as the owner
met it - and both features must refuse, which fails the row.

The package's bss is read through a map of the build (the precompiled image
is realdmg.asm as RDG_AOT); its segment is the owner most of the heap's
claims name. 86Box is not cycle-accurate (docs/86BOX.md): nothing here is a
speed.
"""
import argparse
import collections
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tools"), os.path.join(ROOT, "tests"),
                os.path.join(ROOT, "tests", "rdgcore")]
import gbaot  # noqa: E402
import gbstatic  # noqa: E402
import heapmap  # noqa: E402
import os88box  # noqa: E402
import os88flush  # noqa: E402
import rdgwin  # noqa: E402

INC = os.path.join(ROOT, "apps", "realdmg")
NAMES = ("rw_nslot", "rw_slotlent", "rw_loans", "rw_logon", "rw_rpseg", "rw_logseg",
         "rw_slotseg", "rw_rpmode", "rdg_viewseg")


def package(tmp, red):
    """A.O88: GBBANK.GB precompiled, its RDG_AOT image built -DRW_LENDTEST
    (and -DRW_NOLEND for --red) in a build of its own, with a map"""
    rom_p = subprocess.check_output([sys.executable, os.path.join(ROOT, "tools", "gbours.py"),
                                     "gbbank"], text=True).strip().splitlines()[-1]
    build = os.path.join(tmp, "build")
    rom = gbstatic.Rom(open(rom_p, "rb").read())
    found, extra = gbaot.walk(rom)
    img = gbaot.translate(build, rom, gbaot.leaders(rom, found, extra), log=lambda *x: None)
    out = os.path.join(tmp, "img")
    gbaot.save(img, out, rom)
    gbaot.aot_image(build)                  # (the build dir's prerequisites)
    d = gbaot.aotdir(build)
    mapf = os.path.join(tmp, "a.map")
    wrap = os.path.join(tmp, "a.asm")
    with open(wrap, "w") as f:
        f.write('[map symbols %s]\n%%include "realdmg.asm"\n' % mapf)
    defs = ["-DRDG_AOT", "-DRW_LENDTEST"] + (["-DRW_NOLEND"] if red else [])
    subprocess.check_call(["nasm", "-f", "bin", "-w+error"] + defs +
                          ["-I", os.path.join(ROOT, "apps") + "/", "-I", INC + "/",
                           "-I", os.path.join(ROOT, "drivers", "sound") + "/", "-I", d + "/",
                           "-o", os.path.join(d, "realdmgaot.bin"), wrap])
    o88 = os.path.join(tmp, "A.O88")
    gbaot.package(build, gbaot.load(out), rom, o88, "GBBANK.GB", log=lambda *x: None)
    sym = {}
    for ln in open(mapf):
        p = ln.split()
        if len(p) == 3 and p[2] in NAMES:
            sym[p[2]] = int(p[1], 16)
    return rom_p, o88, sym


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--vm", default="286-victory")
    ap.add_argument("--red", action="store_true")
    a = ap.parse_args()
    tmp = tempfile.mkdtemp(prefix="rdglend-")
    rom_p, o88, sym = package(tmp, a.red)
    os.environ["RDGWIN_O88"] = o88          # A.O88 lists first: the boot's
    os.environ["RDGWIN_GBNAME"] = "GBBANK.GB"   # double-click opens it
    o = argparse.Namespace(rom=rom_p, vm=a.vm, keep="", mem=4096, press="", fs=False,
                           quick=True)
    fails = []
    b = rdgwin.boot(o)

    class Q:
        def read(self, at, n):
            return b.memr(at, n)

    def seg():                              # the package: most claims' owner
        m = heapmap.Map(Q(), heapmap.symbols())
        c = collections.Counter(x.own for x in m.claims if x.own >= heapmap.INST_MAX)
        return c.most_common(1)[0][0] if c else None

    def st(s):
        rd = lambda k, n=2: int.from_bytes(bytes(b.memr(s * 16 + sym[k], n)), "little")
        return dict(nslot=rd("rw_nslot"), lent=rd("rw_slotlent", 1), loans=rd("rw_loans", 1),
                    logon=rd("rw_logon", 1), rpseg=rd("rw_rpseg"), logseg=rd("rw_logseg"),
                    slotseg=rd("rw_slotseg"), rpmode=rd("rw_rpmode", 1))

    def wram(s):                        # GBBANK's: switches checked, wrong
        view = int.from_bytes(bytes(b.memr(s * 16 + sym["rdg_viewseg"], 2)), "little")
        m = bytes(b.memr(view * 16 + 0xC000, 16))
        return m[0] | m[1] << 8, m[2]

    def say(tag, s):
        v = st(s)
        print("  %-12s slots %d, lent %d, buffers %d, log %d, replay %d (rpseg %04X, logseg %04X, "
              "slots at %04X)" % (tag, v["nslot"], v["lent"], v["loans"], v["logon"], v["rpmode"],
                                  v["rpseg"], v["logseg"], v["slotseg"]), flush=True)
        return v

    def inslots(v, x):
        return v["slotseg"] <= x < v["slotseg"] + (v["nslot"] + v["lent"]) * 0x400

    try:
        b.until(lambda: seg() is not None, "the package's claims", 60000, 1000)
        b.cmd("step 8000")
        s = seg()
        v0 = say("running", s)
        if v0["slotseg"] == 0 or v0["nslot"] <= 4:
            fails.append("the cartridge is not in the bank cache with slots to lend "
                         "(%d slots)" % v0["nslot"])
            raise SystemExit
        b.to(200, 68)                       # (RealDMG's window fronted)
        b.click(1)
        b.cmd("step 1500")
        b.tap("f6")
        b.cmd("step 8000")
        v1 = say("replay on", s)
        if not (v1["lent"] >= 1 and v1["loans"] == 1 and inslots(v1, v1["rpseg"])):
            fails.append("Record Replay took no lent slot (lent %d, buffers %d)"
                         % (v1["lent"], v1["loans"]))
        rdgwin.machine_item(b, rdgwin.ITEM_LOG)
        b.cmd("step 4000")
        v2 = say("log on", s)
        if not (v2["logon"] and v2["loans"] == 2 and inslots(v2, v2["logseg"])):
            fails.append("the log's records are not in a lent slot (log %d, buffers %d)"
                         % (v2["logon"], v2["loans"]))
        w0 = wram(s)
        b.cmd("step 10000")
        w1 = wram(s)
        print("  on lent slots: %d switches checked, %d wrong" % ((w1[0] - w0[0]) & 0xFFFF, w1[1]))
        if w1[1] or w1[0] == w0[0]:
            fails.append("the cartridge on fewer slots: %d switches checked, %d wrong"
                         % ((w1[0] - w0[0]) & 0xFFFF, w1[1]))
        b.tap("f6")
        b.cmd("step 8000")
        say("replay off", s)
        rdgwin.machine_item(b, rdgwin.ITEM_LOG)
        b.cmd("step 8000")
        v4 = say("log off", s)
        if v4["lent"] or v4["loans"] or v4["nslot"] != v0["nslot"]:
            fails.append("the lent slots are not the cache's again (%d slots, lent %d, "
                         "buffers %d; %d before)" % (v4["nslot"], v4["lent"], v4["loans"],
                                                     v0["nslot"]))
    except SystemExit:
        pass
    finally:
        b.quit()
    vol = os88flush.Volume(open(os.path.join(os88box.RUNS, b.name, "fdd2.img"), "rb").read())
    got = dict((e.name.upper(), e) for e in vol.listdir(""))
    print("  on the disk: %s" % " ".join(sorted(got)))
    for nm in ("GBBANK.SS1", "GBBANK.RP1", "GBBANK00.LOG"):   # (named for the
        if nm not in got:                                   #   cartridge's file)
            fails.append("%s was not written" % nm)
    if "GBBANK.RP1" in got:
        p = os.path.join(tmp, "GBBANK.RP1")
        open(p, "wb").write(vol.read("GBBANK.RP1"))
        n = gbaot.replay(p)[2]
        print("  the replay: %d frames" % n)
        if n < 10:                      # (GBBANK fetches at nearly every
            fails.append("the replay holds %d frames" % n)   # switch: ~4 a second)
    if a.red:
        print("  RED RUN: -DRW_NOLEND")
    for f in fails:
        print("FAIL rdglend: " + f)
    print("rdglend: %s" % ("FAIL" if fails else "ok"))
    shutil.rmtree(tmp, ignore_errors=True)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
