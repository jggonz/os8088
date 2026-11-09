import sys, os, pickle, bisect, subprocess
D = os.path.dirname(os.path.abspath(__file__))
def load(name):
    d = pickle.load(open(D + "/%s.pkl" % name, "rb"))
    t = d["tab"]; addrs = [a for a, n in t]
    def lab(f):
        i = bisect.bisect_right(addrs, f) - 1
        a, n = t[i]
        return n, f - a
    starts = {a: n for a, n in t if "." not in n}
    return d["tr"], lab, starts, d["codes"]
_dis = {}
def dis(code):
    if code in _dis: return _dis[code]
    p = subprocess.run(["ndisasm", "-b16", "-"], input=code, capture_output=True)
    l = p.stdout.decode().splitlines()[0].split(None, 2)
    r = (len(l[1]) // 2, l[2] if len(l) > 2 else "")
    _dis[code] = r; return r
def calls(tr, starts, routine):
    res = []
    for i in range(1, len(tr)):
        c, f, sp = tr[i]
        if starts.get(f) != routine: continue
        if tr[i-1][2] - sp not in (2, 4): continue
        for j in range(i+1, len(tr)):
            if tr[j][2] > sp:
                res.append((i, j, tr[j][0] - c)); break
    return res
def listing(tr, lab, codes, i, j):
    out = []
    for k in range(i, j):
        c, f, sp = tr[k]; d = tr[k+1][0] - c
        n, o = lab(f); ln, txt = dis(codes[f])
        out.append("%5d %2db  %-30s %s" % (d, ln, "%s+%d" % (n, o), txt))
    return "\n".join(out)
if __name__ == "__main__":
    name, routine = sys.argv[1], sys.argv[2]
    which = sys.argv[3] if len(sys.argv) > 3 else "min"
    tr, lab, starts, codes = load(name)
    cs = calls(tr, starts, routine)
    print(routine, "calls:", [x[2] for x in cs])
    cs.sort(key=lambda x: x[2])
    sel = cs[0] if which == "min" else cs[int(which)]
    print(listing(tr, lab, codes, sel[0] - 1, sel[1]))
