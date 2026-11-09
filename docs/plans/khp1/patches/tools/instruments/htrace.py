"""Exact single-step trace of a kernel call on MartyPC, attributed by routine."""
import sys, os, time, bisect, json, struct
def _os88_root():
    """The checkout this file sits in ($OS88_ROOT overrides): the nearest
    directory above holding kernel/kernel.asm."""
    import os
    r = os.environ.get("OS88_ROOT")
    if r:
        return r
    d = os.path.dirname(os.path.abspath(__file__))
    while d != os.path.dirname(d):
        if os.path.isfile(os.path.join(d, "kernel", "kernel.asm")):
            return d
        d = os.path.dirname(d)
    raise SystemExit("no os8088 checkout above " + __file__)
ROOT = _os88_root()
sys.path.insert(0, ROOT + "/tools"); sys.path.insert(0, ROOT + "/tests")
import os88marty, os88sym
import os88vidprof

HZ = 4772727.0

class Table:
    def __init__(self, m):
        self.t = os88vidprof.kern_table(m)
        off = os88sym.syms(); sec = os88sym.sections()
        ks = os88sym.equates().get("KERNEL_SEG", 0x60)
        self.t += [(ks * 16 + off[n], n) for n in off if sec[n] == ".vga"]
        self.t.sort()
        self.addrs = [a for a, n in self.t]
        self.starts = {}
        for a, n in self.t:
            if "." not in n:
                self.starts.setdefault(a, n)
    def name(self, flat):
        i = bisect.bisect_right(self.addrs, flat) - 1
        if i < 0:
            return "?%05x" % flat
        a, n = self.t[i]
        if flat - a > 4096:
            return "?%05x" % flat
        return n
    def routine(self, flat):
        return self.name(flat).split(".")[0]

def wait_stop(m, mark, limit=120.0):
    t0 = time.time()
    while time.time() - t0 < limit:
        st = m.status()
        if st.get("state") != "running" and int(st.get("stops", 0)) > mark:
            return st
        time.sleep(0.005)
    raise RuntimeError("no stop")

def step_until_ret(m, max_steps=400000, regs=True, stop=None):
    """From a stop at a routine's first instruction, step until it returns.
    -> list of (cycles, flat_ip, sp)"""
    r = m.regs()
    ss, sp = r["ss"], r["sp"]
    raw = m.read(ss * 16 + sp, 4)
    ret_ip = raw[0] | raw[1] << 8
    st = m.status()
    cs = st["cs"]
    near = (cs * 16 + ret_ip)
    farcs = raw[2] | raw[3] << 8
    far = farcs * 16 + ret_ip
    out = [(int(st["cycles"]), st["flat_ip"], sp)]
    for k in range(max_steps):
        st = m.step(1)
        f = st["flat_ip"]
        if regs:
            spn = m.regs()["sp"]
        else:
            spn = 0
        out.append((int(st["cycles"]), f, spn))
        if stop is not None and f == stop:
            return out
        if (f == near or f == far) and (not regs or spn > sp):
            return out
    raise RuntimeError("did not return in %d steps" % max_steps)

def arm_and_catch(m, sym, trigger, accept=None, limit=120.0, tries=50):
    m.bp_exec(sym)
    mark = int(m.status().get("stops", 0))
    trigger()
    for _ in range(tries):
        st = wait_stop(m, mark, limit)
        mark = int(st["stops"])
        if accept is None or accept(m):
            m.bp_exec()
            return st
        m.run()
    m.bp_exec()
    raise RuntimeError("never accepted")

def profile(tr, tab, top=40, ex_label=False):
    """exclusive cycles per routine (or per label)"""
    agg = {}
    cnt = {}
    for i in range(len(tr) - 1):
        c0, f, _ = tr[i]
        d = tr[i + 1][0] - c0
        n = tab.name(f) if ex_label else tab.routine(f)
        agg[n] = agg.get(n, 0) + d
        cnt[n] = cnt.get(n, 0) + 1
    tot = tr[-1][0] - tr[0][0]
    rows = sorted(agg.items(), key=lambda x: -x[1])
    lines = ["total %d cycles = %.3f ms, %d instructions" % (tot, tot*1000/HZ, len(tr)-1)]
    for n, c in rows[:top]:
        lines.append("%8d %5.1f%% %6d instr  %s" % (c, 100.0*c/tot, cnt[n], n))
    return "\n".join(lines)

def inclusive(tr, tab):
    """for every routine-start entered by a call (sp dropped), cycles until sp rises above.
    -> {routine: [cycles per call]}"""
    res = {}
    n = len(tr)
    for i in range(1, n):
        c, f, sp = tr[i]
        nm = tab.starts.get(f)
        if nm is None:
            continue
        psp = tr[i-1][2]
        if not (psp - sp in (2, 4)):
            continue
        # find return: first j>i with sp > sp_i
        for j in range(i+1, n):
            if tr[j][2] > sp:
                res.setdefault(nm, []).append(tr[j][0] - c)
                break
    return res
