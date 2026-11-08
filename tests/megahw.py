#!/usr/bin/env python3
"""apps/os88huge.inc IN A GUEST: the walker across a bridge's cut, and on a
paragraph step, from ONE build (SPEC.md 50.3.8, docs/plans/MEGA-HUGE-PLAN.md
F5).

    python3 tests/megahw.py            kern_mega, Bochs, a 4MB machine
    python3 tests/megahw.py --box      kern_mega, 86Box's at286_vga_sb, 640K + 384K
    python3 tests/megahw.py --big      kern_big, MartyPC's 5150, 640K, 360KB disks
    python3 tests/megahw.py ... --keep  ...and leave the boot's directory

MEGAHW (tests/megahw) claims the largest OSAPI_MEM_HUGE claim with a 64KB
step through hw_claim - on kern_mega the AX = 0 ask's answer, a BRIDGE of
the pool and the heap on both machines here; on kern_big 200 KB, CX = 1000h
- and tests at B, the cut (HW_CUT) where there is one and window 2's start
where there is not: spans put and got back across B, from its last byte, at
odd lengths and the claim's two ends; hw_aim's reach one byte at B - 1 and a
window at B; the 64KB about B written by the KERNEL's own OSAPI_FILE_WRITE
and by hw_fwrite, each file read back and held to the pattern; and the span
zeroed and read back by hw_fread with HW_FOFF. Every answer is 1 for right
(MEGAHW's MH_RES); this asserts each, and the shape: kern_mega bridged
(HW_CUT > 0, a step of 8), kern_big one run of paragraphs (1000h, no cut).

THE FAILING CASE (docs/WRITING-TESTS.md 1): the include with hw_aim's
window's-end cap taken out (tests/os88huge.py --break nowin's edit, applied
here by --break nowin to a rebuilt MEGAHW) fails every leg that crosses B -
on kern_mega the spans and the aim with the cut reached past (a #GP at the
window's limit, or pool memory), on kern_big the spans read the next
window's bytes as this one's. Measured when the row was written.
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
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)

fails = []
FIELDS = ["kb", "ccf", "step", "cut", "b", "spans", "aim", "fill", "kwr",
          "wwr", "kchk", "wchk", "fread", "fget", "fat", "done"]
SAY = {
    "spans": "every span put and got back the pattern",
    "aim": "hw_aim: one byte's reach at B - 1, a window's at B, both read "
           "the pattern",
    "fill": "the 64KB about B filled",
    "kwr": "the kernel's OSAPI_FILE_WRITE of it (HWK.DAT)",
    "wwr": "hw_fwrite of it onto an os88_wseq stream (HWW.DAT)",
    "kchk": "HWK.DAT read back: the pattern",
    "wchk": "HWW.DAT read back: the pattern",
    "fread": "hw_fread of HWK.DAT into the zeroed span: 64KB delivered",
    "fget": "...and got back: the pattern",
}


def check(what, ok, more=""):
    print("   %s  %s%s" % ("ok  " if ok else "FAIL", what,
                           (" - " + str(more)) if more != "" and not ok
                           else ""), flush=True)
    if not ok:
        fails.append(what)
    return ok


def verdict(r, mega):
    print("megahw: %s" % " ".join("%s=%X" % (k, r[k]) for k in FIELDS))
    if not check("the claim of %dK granted" % r["kb"], r["ccf"] == 0):
        return
    if mega:
        check("kern_mega: a BRIDGE (the cut at window %d), a step of 8"
              % r["cut"], r["cut"] > 0 and r["step"] == 8,
              "cut %d step %X" % (r["cut"], r["step"]))
    else:
        check("kern_big: one run of paragraphs (step 1000h, no cut)",
              r["cut"] == 0 and r["step"] == 0x1000,
              "cut %d step %X" % (r["cut"], r["step"]))
    print("   --    B = window %d (%s)" % (r["b"], "the cut" if r["cut"]
                                           else "window 2's start"))
    for k, what in SAY.items():
        more = r[k]
        if k == "spans" and r[k] >= 2:
            more = "span %d wrong or refused" % (r[k] - 2)
        if k == "fget" and r[k] == 2:
            more = "a byte wrong %X into the span" % r["fat"]
        check(what, r[k] == 1, more)


def unpack(raw):
    at = raw.find(b"HWRES001")
    if at < 0:
        return None
    w = struct.unpack_from("<%dH" % len(FIELDS), raw, at + 8)
    r = dict(zip(FIELDS, w))
    return r if r["done"] == 1 else None


def insts(m, u16):
    """the region of every live MEGAHW in inst_tab (32-byte rows: +0 live,
    +6 the region, +12 the name)"""
    raw = m.read(m.sym("inst_tab"), 12 * 32)
    return [u16(raw, i * 32 + 6) for i in range(12)
            if raw[i * 32] == 1 and
            raw[i * 32 + 12:i * 32 + 28].split(b"\0")[0] == b"MEGAHW"]


def pkg(brk, tmp):
    """build/megahw.o88, or one rebuilt with --break's edit to the include"""
    if not brk:
        return os.path.join(ROOT, "build", "megahw.o88")
    import os88huge
    src = open(os.path.join(ROOT, "apps", "os88huge.inc")).read()
    old, new = os88huge.BREAKS[brk]
    inc = os.path.join(tmp, "inc")
    os.makedirs(inc)
    open(os.path.join(inc, "os88huge.inc"), "w").write(src.replace(old, new, 1))
    b, o = os.path.join(tmp, "megahw.bin"), os.path.join(tmp, "megahw.o88")
    subprocess.check_call(["nasm", "-f", "bin", "-w+error", "-I", inc + "/",
                           "-I", os.path.join(ROOT, "apps") + "/", "-o", b,
                           os.path.join(ROOT, "tests", "megahw", "megahw.asm")])
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools",
                                                        "os88pkg.py"), b,
                           "-o", o], stdout=subprocess.DEVNULL)
    return o


def run_bochs(keep, brk):
    import os88bochs
    from os88fixture import need
    for p in ["build/megahw.o88", "build/megarun.o88", "build/mega.img",
              "build/mbr.bin"]:
        need(p)
    out = tempfile.mkdtemp(prefix="megahw-")
    os88bochs.MEGS = 4
    box = os88bochs.Box(out, [pkg(brk, out)])
    lines = []
    try:
        if box.wait_pm(240):
            box.up()
            box.megarun()
            end = time.time() + 300
            while time.time() < end:
                lines = box.said()
                if any(l.startswith(("kern_mega FAULT", "kern_mega CLOSED",
                                     "MEGAHW: ")) for l in lines):
                    break
                time.sleep(0.3)
            box.guest_sleep(1)
            lines = box.said()
        else:
            fails.append("kern_mega never switched to protected mode in 240s")
    finally:
        box.stop()
        if not keep:
            shutil.rmtree(out, ignore_errors=True)
        else:
            print("megahw: kept %s" % out)
    for l in lines:
        if l.startswith(("kern_mega FAULT", "kern_mega CLOSED")):
            fails.append(l)
    got = [l for l in lines if l.startswith("MEGAHW: ")]
    if not check("MEGAHW answered", bool(got)):
        return
    w = [int(x, 16) for x in got[-1].split()[1:]]
    if len(w) < len(FIELDS):
        fails.append("short line: %s" % got[-1])
        return
    verdict(dict(zip(FIELDS, w)), True)


def run_ui(keep, brk, mega):
    if mega:
        os.environ.setdefault("OS88_MEGA", "1")
        os.environ.setdefault("OS88_MEGA_EMU", "86box")
        os.environ["OS88_MEGA_MEGS"] = os.environ.get("OS88_MEGA_MEGS",
                                                      "1.375")
    import os88build
    import os88marty as M
    import os88ui
    if mega:
        import megajw as J      # (its import arms kern_mega's defaults)
    B = lambda p: os88build.at(os.path.join("build", p))     # noqa: E731
    tmp = tempfile.mkdtemp(prefix="megahw-")
    try:
        disk = os.path.join(tmp, "b.img")
        subprocess.check_call(
            [sys.executable, os.path.join(ROOT, "tools", "os88disk.py"), "-o",
             disk, "--size", "1440" if mega else "360", pkg(brk, tmp) if brk else
             B("megahw.o88"), "--folder", "SYSTEM/APPDATA"],
            stdout=subprocess.DEVNULL)
        kw = dict(machine=J.MACHINE) if mega else {}
        u16 = lambda b, i: b[i] | b[i + 1] << 8         # noqa: E731
        with os88ui.boot(B("mega.img") if mega else B("os8088-360.img"),
                         apps=disk, label="megahw", **kw) as ui:
            m = ui.m
            if mega:
                ext = u16(m.read(m.sym("mega_extkb"), 2), 0)
                print("megahw: %sMB, %dK above 1MB" % (
                    os.environ["OS88_MEGA_MEGS"], ext))
            ui.open_drive("B")
            ui.open("MEGAHW.O88", expect=None)
            r = None
            for _ in range(240):
                # the program works inside its entry proc, before inst_tab
                # names it: the loader's [ld_base] first (megabrdg's way)
                cands = [u16(m.read(m.sym("ld_base"), 2), 0)] + \
                    insts(m, u16)
                for sp in cands:
                    if sp:
                        r = unpack(m.readseg(sp, 0, 0x6A00)) or r
                if r:
                    break
                M.guest_sleep(m, 1)
            if not check("MEGAHW answered", r is not None,
                         "titles %s" % ui.titles()):
                if mega:
                    for l in m.faults()[-12:]:
                        print("   fault: " + l)
                return
            verdict(r, mega)
            if mega:
                bad = [l for l in m.faults() if "MEGAHW" in l]
                check("no exception charged to MEGAHW", not bad,
                      "; ".join(bad[:3]))
    finally:
        if not keep:
            shutil.rmtree(tmp, ignore_errors=True)
        else:
            print("megahw: kept %s" % tmp)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--box", action="store_true", help="86Box, 640K + 384K")
    ap.add_argument("--big", action="store_true", help="kern_big, MartyPC")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--break", dest="brk",
                    help="rebuild MEGAHW with tests/os88huge.py's edit NAME")
    a = ap.parse_args()
    t0 = time.time()
    if a.box or a.big:
        run_ui(a.keep, a.brk, a.box)
    else:
        run_bochs(a.keep, a.brk)
    print("megahw: %s in %.0fs" % ("PASS" if not fails else "FAIL (%s)"
                                   % "; ".join(fails), time.time() - t0))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
