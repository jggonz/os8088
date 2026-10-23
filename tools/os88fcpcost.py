#!/usr/bin/env python3
"""A/B the copy engine: guest cycles and int 13h per Paste, and the bytes landed.

    python3 tools/os88fcpcost.py ROOT LABEL > out.json

The instrument behind docs/reports/FILECP-SIZE-IMPL-2026-10-18.md. ROOT is an
os8088 tree with `make` done; its build/martypc may be a symlink to another
tree's. Symbols resolve against ROOT's OWN kernel (os88sym re-assembles the
source it runs from), so an A/B is two runs, one per tree - a base commit in a
`git worktree` and the working tree. Each Paste is bracketed by breakpoints on
`fcp_paste` and `fm_paste_res` with the guest's cycle counter read while it
is stopped, and every `int 13h` between them is counted by direction and by
floppy/fixed disk. Cycles and calls are exact; on a floppy the cycles carry
the drive's rotational phase, so compare two runs of the SAME kernel before
reading a difference there. The JSON follows the harness's log lines on
stdout.

Scenario A: B:\\TREE (a 360KB floppy) pasted into C:\\ - a folder tree with a
            200KB file; the floppy is the OTHER side of every chunk, so the
            READ_SEQ cursor is cold each time, and the walk is over FAT12.
Scenario B: C:\\TREE\\SUB pasted into C:\\ - one fixed disk, folder copy with
            the same 200KB file; the cursor stays hot after the first chunks.
"""
import json
import os
import random
import shutil
import subprocess
import sys
import tempfile

if len(sys.argv) != 3:
    sys.exit(__doc__)
ROOT = os.path.abspath(sys.argv[1])
LABEL = sys.argv[2]
os.chdir(ROOT)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import os88marty as M                                       # noqa: E402
import os88ui                                               # noqa: E402
import instdeep as ID                                       # noqa: E402

MACHINE = "os8088_xt_hdd"
TEMPLATE = os.path.join(M.base_run_dir(), "media/hdds/default_xtide.vhd")
rnd = random.Random(8088)


def blob(n):
    return bytes(rnd.getrandbits(8) for _ in range(n))


FILES = {}
for i in range(1, 13):
    FILES["TREE/T%02d.DAT" % i] = blob(rnd.randint(1000, 8000))
for i in range(1, 7):
    FILES["TREE/SUB/S%02d.DAT" % i] = blob(rnd.randint(2000, 10000))
FILES["TREE/SUB/HBIG.BIN"] = blob(200 * 1024 + 123)
for i in range(1, 4):
    FILES["TREE/SUB/DEEP/D%d.DAT" % i] = blob(3000 + i)


def select(ui, name, win):
    i, _ = ui.entry(name, win)
    row = ui.scroll_to(i, win=win)
    ui.mo.click(*ui.row_xy(win, row))
    ui.settle(limit=10.0)


def paste(m, ui, what):
    hits = []

    def on_hit(mm, rec):
        return int(mm.status()["cycles"])
    with M.bp_trace(m, "fcp_paste", "fm_paste_res",
                    {"type": "int", "addr": 0x13}, regs=True, cap=20000,
                    on_hit=on_hit) as tr:
        ui.menu_pick("Edit", "Paste")
        M.until(m, lambda mm: mm.read(mm.sym("fcp_busy"), 1)[0] == 0,
                "the paste to finish", guest=600.0, poll=0.5)
        M.until(m, lambda mm: tr.count("fm_paste_res") >= 1,
                "fm_paste_res", guest=60.0, poll=0.2)
    hits = tr.hits
    err = m.read(m.sym("fcp_err"), 1)[0]
    c0 = next(h["hit"] for h in hits if h["name"] == "fcp_paste")
    c1 = next(h["hit"] for h in hits if h["name"] == "fm_paste_res")
    n13 = {"hd_r": 0, "hd_w": 0, "fd_r": 0, "fd_w": 0, "other": 0}
    secs = {"hd_r": 0, "hd_w": 0, "fd_r": 0, "fd_w": 0}
    for h in hits:
        if h["name"].startswith("?") or h.get("type") == "int" or \
                h["name"] not in ("fcp_paste", "fm_paste_res"):
            if not (c0 <= h["hit"] <= c1):
                continue
            r = h.get("regs", {})
            ah, dl, al = r.get("ax", 0) >> 8, r.get("dx", 0) & 0xFF, \
                r.get("ax", 0) & 0xFF
            k = ("hd_" if dl & 0x80 else "fd_")
            if ah == 2:
                n13[k + "r"] += 1
                secs[k + "r"] += al
            elif ah == 3:
                n13[k + "w"] += 1
                secs[k + "w"] += al
            else:
                n13["other"] += 1
    return {"what": what, "err": err, "cycles": c1 - c0,
            "ms": round((c1 - c0) / 4772.727, 1), "int13": n13,
            "sectors": secs, "overflow": tr.overflowed}


def main():
    tmp = tempfile.mkdtemp(prefix="fcpcost-")
    out = {"label": LABEL}
    try:
        fl = []
        for p, d in FILES.items():
            hp = os.path.join(tmp, p.replace("/", "_"))
            open(hp, "wb").write(d)
            folder, name = p.rsplit("/", 1)
            # os88disk names a file by its host basename: stage one per name
            dst = os.path.join(tmp, "stage", folder)
            os.makedirs(dst, exist_ok=True)
            shutil.copyfile(hp, os.path.join(dst, name))
            fl.append("%s:%s" % (folder, os.path.join(dst, name)))
        flop = os.path.join(tmp, "b.img")
        subprocess.check_call(["python3", "tools/os88disk.py", "-o", flop,
                               "--size", "360"] + fl,
                              stdout=subprocess.DEVNULL)
        vhd = os.path.join(tmp, "c.vhd")
        subprocess.check_call(
            ["python3", "tools/os88hdd.py", "--template", TEMPLATE, "--out",
             vhd, "--kernel", "build/kernel.sys", "--vbr", "build/boothd.bin",
             "--mbr", "build/mbr.bin", "--file", "HDD.DRV=build/hdd.drv"],
            stdout=subprocess.DEVNULL)
        m = M.launch(None, apps=flop, machine=MACHINE,
                     extra=["--mount", "hd:0:" + vhd])
        try:
            ui = os88ui.UI(m)
            ui.ready(limit=300)
            src = ui.open_drive("B")
            select(ui, "TREE", src)
            ui.menu_pick("Edit", "Copy")
            ui.open_drive("C")
            out["A"] = paste(m, ui, "B:\\TREE -> C:\\")
            ui.settle(limit=60)
            w = ui.open("TREE")
            select(ui, "SUB", w)
            ui.menu_pick("Edit", "Copy")
            ui.open("..")
            out["B"] = paste(m, ui, "C:\\TREE\\SUB -> C:\\")
            ui.settle(limit=60)
        finally:
            m.quit()
        v = ID.partition(vhd)
        bad = []
        for p, d in FILES.items():
            for q in (p, p[len("TREE/"):] if p.startswith("TREE/SUB/") else None):
                if q is None:
                    continue
                got = v.read(q)
                if got != d:
                    bad.append("%s: %s" % (q, "missing" if got is None
                                           else "%d bytes differ/len %d"
                                           % (len(d), len(got))))
        # every chain disjoint: no cross-link
        seen, xl = {}, []

        def walk(first, path):
            for name, attr, clus, size in v.entries(first):
                if name in (".", ".."):
                    continue
                for c in v.chain(clus):
                    if c in seen:
                        xl.append("%s/%s shares %d with %s"
                                  % (path, name, c, seen[c]))
                    seen[c] = "%s/%s" % (path, name)
                if attr & 0x10:
                    walk(clus, path + "/" + name)
        walk(0, "")
        out["verify"] = {"bad": bad, "crosslinks": xl[:5],
                         "files": len(FILES)}
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print(json.dumps(out, indent=1))
    return 1 if out.get("verify", {}).get("bad") or \
        out.get("verify", {}).get("crosslinks") else 0


if __name__ == "__main__":
    sys.exit(main())
