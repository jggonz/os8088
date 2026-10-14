#!/usr/bin/env python3
"""Guest-cycle cost of Solitaire's hot paths, A/B-able across builds
(SPEC.md 43.11.5).

    python3 tools/solperf.py OUT.json IMG [machine]

solcheck's seeded deal, then New Game (N), Restart (R), three stock clicks,
solcheck's own PLAY move sequence and six passes of a drag's outline - each
priced entry to return in guest cycles: a breakpoint on the routine's entry
reads the return address off the guest's stack and re-arms on it. Two builds
dealt the same seed play the same moves, so the rows pair up step for step.
The same SOLBIN / SOLSRC / SOLSMALL as tools/solcheck.py. Timer and mouse
interrupts land inside a span now and then - a few thousand cycles, once.
"""
import json, os, re, struct, subprocess, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty
from os88mouse import Mouse
from os88geom import W_SEG, WIN_SIZE
import sucheck as su
import subcheck as sc
import solcheck as sk

def labels():
    lst = os.path.join(ROOT, "build/.solperf.lst")
    defs = ["-DAPP_SMALL"] if sk.SOLSMALL else []
    subprocess.check_call(["nasm", "-f", "bin", "-w+error"] + defs +
                          ["-I", ROOT + "/apps/", "-o", lst + ".bin", "-l", lst,
                           os.path.join(ROOT, sk.SOLSRC)])
    assert open(lst + ".bin", "rb").read() == open(os.path.join(ROOT, sk.SOLBIN), "rb").read(), "listing != build"
    out, last = {}, None
    for l in open(lst):
        m = re.match(r"\s*\d+\s+([0-9A-F]{8})\s", l)
        if m:
            last = int(m.group(1), 16)
            for name in pend:
                out[name] = last
            pend.clear()
        m2 = re.match(r"\s*\d+\s+(?:[0-9A-F]{8}\s+\S+\s+)?(sol_\w+):", l)
        if m2:
            pend.append(m2.group(1))
    return out
pend = []

def main():
    outp, img = sys.argv[1], sys.argv[2]
    machine = sys.argv[3] if len(sys.argv) > 3 else "os8088_5150_cga_gla"
    L = labels()
    res = {"machine": machine, "bin": sk.SOLBIN, "rows": []}
    with os88marty.launch(os.path.join(ROOT, "build/os8088-360.img"),
                          apps=img, machine=machine) as m:
        mo = Mouse(marty=m)
        os88marty.no_saver(m)
        mo.dblclick(*su.zone(m, 1))
        os88marty.until(m, lambda _: any(w.visible for w in su.windows(m)),
                        "the Disk window", poll=0.1, limit=60)
        disk = [w for w in su.windows(m) if w.visible][0]
        mo.dblclick(*su.row(disk, 0))
        def solwins():
            return [w for w in su.windows(m) if w.visible and w.title.upper().startswith("SOL")]
        os88marty.until(m, lambda _: solwins(), "Solitaire", poll=0.1, limit=60)
        os88marty.settle(m)
        sol = solwins()[0]
        seg = struct.unpack("<H", m.read(m.sym("wm_wins") + sol.i * WIN_SIZE + W_SEG, 2))[0]
        base = seg << 4
        def cur():
            return [w for w in su.windows(m) if w.visible and w.i == sol.i][0]

        def priced(label, gesture, n=1, what=None):
            """Cycles from `label`'s entry to its return, for each of n entries."""
            entry = base + L[label]
            spans, st = [], {}
            def hit(mm, rec):
                r = mm.regs()
                flat = (r["cs"] << 4) + r["ip"]
                if flat == entry and "ret" not in st:
                    ret = int.from_bytes(mm.read((r["ss"] << 4) + r["sp"], 2), "little")
                    st["ret"] = (r["cs"] << 4) + ret
                    st["c0"] = rec["cycles"]
                    mm.breakpoints([{"type": "exec", "addr": st["ret"]}])
                elif "ret" in st and flat == st["ret"]:
                    spans.append(rec["cycles"] - st["c0"])
                    st.clear()
                    mm.breakpoints([{"type": "exec", "addr": entry}] if len(spans) < n else [])
                return None
            with os88marty.bp_trace(m, {"type": "exec", "addr": entry}, on_hit=hit) as tr:
                gesture()
                tr.until(lambda: len(spans) >= n, what or label, limit=120.0, required=False)
            return spans

        m.write(m.sym("osapi_seed"), struct.pack("<H", sk.SEED))
        sc.pclick(mo, sol.x + sol.w // 2, sol.y + sol.h - 30)
        os88marty.settle(m)
        m.write(m.sym("osapi_seed"), struct.pack("<H", sk.SEED))
        sp = priced("sol_onkey", lambda: m.key("KeyN"), what="N")
        res["rows"].append(("newgame N", sp)); print("newgame", sp)
        os88marty.settle(m)
        sp = priced("sol_onkey", lambda: m.key("KeyR"), what="R")
        res["rows"].append(("restart R", sp)); print("restart", sp)
        os88marty.settle(m)
        s = cur()
        b = sk.Board(m, seg, sk.solsyms())
        for k in range(3):
            sp = priced("sol_onkey", lambda: m.key("Space"), what="space")
            res["rows"].append(("deal %d" % k, sp)); print("deal", k, sp)
            os88marty.settle(m)
        b.reload()
        # the PLAY session, each drop priced
        dry = 0
        for step in range(sk.PLAY):
            mv = sk.find_move(b)
            if mv is None:
                fp = sk.find_fnd(b)
                if fp is not None:
                    px, py = sk.grabxy(b, fp, b.cnt[fp] - 1)
                    sp = priced("sol_drop", lambda: sc.pclick(mo, px, py, settle=False), what="fnd drop")
                    res["rows"].append(("step %02d fnd" % step, sp)); print("step", step, "fnd", sp)
                    os88marty.settle(m); b.reload(); dry = 0
                    continue
                sp = priced("sol_onkey", lambda: m.key("Space"), what="space")
                res["rows"].append(("step %02d deal" % step, sp)); print("step", step, "deal", sp)
                os88marty.settle(m); b.reload(); dry += 1
                if dry > 24:
                    break
                continue
            dry = 0
            src, idx, dst = mv
            px, py = sk.grabxy(b, src, idx)
            qx, qy = b.pilexy(dst)
            qx = b.ox + qx + b.cw // 2
            qy = b.oy + qy + (b.yoff(dst, b.cnt[dst]) if b.cnt[dst] else 0) + 4
            sp = priced("sol_drop", lambda: sc.pdrag(mo, px, py, qx, qy, settle=False), what="drop")
            res["rows"].append(("step %02d drop" % step, sp)); print("step", step, "drop", src, idx, dst, sp)
            os88marty.settle(m); b.reload()
        # one drag pass: sol_dxor
        s = cur()
        # press on the waste/a column top and hold: measure 4 dxor
        if b.cnt[sk.P_WASTE]:
            px, py = sk.grabxy(b, sk.P_WASTE, b.cnt[sk.P_WASTE] - 1)
        else:
            c = max(range(7), key=lambda i: b.cnt[i])
            px, py = sk.grabxy(b, c, b.cnt[c] - 1)
        def hold():
            mo.to(px, py); mo._edge(True)
            for k in range(4):
                mo.to(px + 5 + 3 * k, py + 7 + 2 * k, l=True)
        sp = priced("sol_dxor", hold, n=6, what="dxor")
        mo._edge(False)
        res["rows"].append(("dxor", sp)); print("dxor", sp)
        os88marty.settle(m)
        m.quit()
    json.dump(res, open(outp, "w"), indent=1)

if __name__ == "__main__":
    main()
